#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

#include "iomanager.h"
#include "debug.h"
#include "errno.h"
#include <iostream>

namespace vik::iomanager{

IOManager* IOManager::GetThis()
{
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

IOManager::FdContext::EventContext& vik::iomanager::IOManager::FdContext::getEventContext(Event event)
{
    assert(event == READ || event == WRITE);
    switch (static_cast<Event>(event)){
        case READ :
        return read;
        case WRITE:
        return write;
        default:
            goto e;
    }
e:
    throw std::invalid_argument("Unsupported event type");

}

void IOManager::FdContext::resetEventContext(EventContext& ctx)
{
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

void IOManager::FdContext::triggerEvent(Event event)
{
    assert(events & event);
    events = (Event)(events & ~event);

    EventContext&ctx = getEventContext(event);
    if (ctx.cb){
        ctx.scheduler->add(&ctx.cb);
    }else{
        ctx.scheduler->add(&ctx.fiber);
    }
    resetEventContext(ctx);
    return;

}

IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
    : Scheduler(threads,use_caller,name),TimerManager()
{
    _epfd = epoll_create1(EPOLL_CLOEXEC);
    if (_epfd < 0) {
        std::cerr << "epoll_create1 failed: " << strerror(errno) << std::endl;
    }
    assert(_epfd > 0);

    int rt = pipe(_tickleFds);
    assert(!rt);

    epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = _tickleFds[0];

    rt = fcntl(_tickleFds[0],F_SETFL,O_NONBLOCK);
    assert(!rt);

    rt = epoll_ctl(_epfd,EPOLL_CTL_ADD,_tickleFds[0],&event);
    assert(!rt);

    contextResize(32);

    start();
}

IOManager::~IOManager()
{
    stop();
    close(_epfd);
    close(_tickleFds[0]);
    close(_tickleFds[1]);

    auto size = _fdContexts.size();
    for(size_t i = 0; i < size;++i){
        if (_fdContexts[i]){
            delete _fdContexts[i];
        }
    }
}
int IOManager::addEvent(int fd, Event event, std::function<void()> cb)
{
    FdContext*fd_ctx = nullptr;

    std::shared_lock<std::shared_mutex>read_lock(_mutex);
    if (static_cast<int>(_fdContexts.size()) > fd){
        fd_ctx = _fdContexts[fd];
        read_lock.unlock();
    }else{
        read_lock.unlock();
        std::unique_lock<std::shared_mutex>write_lock(_mutex);
        contextResize(fd*1.5);
        fd_ctx = _fdContexts[fd];
    }

    std::lock_guard<std::mutex>lock(fd_ctx->mutex);

    if (fd_ctx->events & event){
        return -1;
    }

    int op = fd_ctx -> events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent; 
    epevent.events = static_cast<uint32_t>(EPOLLET) | static_cast<uint32_t>(fd_ctx->events) | static_cast<uint32_t>(event);
    epevent.data.ptr = fd_ctx;
    int rt = epoll_ctl(_epfd,op,fd_ctx->fd,&epevent);
    if (rt){
        std::cerr << "addEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
        return -1;
    }
    ++ _pendingEventCount;
    fd_ctx->events = (Event)(fd_ctx->events | event);

    FdContext::EventContext & event_ctx = fd_ctx->getEventContext(event);
    assert(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb){
        event_ctx.cb.swap(cb);
    }else{
        event_ctx.fiber = vik::fiber::Fiber::GetThis();
        assert(event_ctx.fiber->getState() == vik::fiber::Fiber::RUNNING);
    }
    return 0;
}
bool IOManager::delEvent(int fd, Event event)
{
    FdContext *fd_ctx = nullptr;
    std::shared_lock<std::shared_mutex>read_lock(_mutex);
    if ((int)_fdContexts.size() > fd){
        fd_ctx = _fdContexts[fd];
        read_lock.unlock();
    }else{
        read_lock.unlock();
        return false;
    }

    std::lock_guard<std::mutex>lock(fd_ctx->mutex);
    if (!(fd_ctx->events & event)){
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = static_cast<uint32_t>(EPOLLET) | static_cast<uint32_t>(new_events);
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(_epfd,op,fd,&epevent);
    if (rt){
        std::cerr << "delEvent::epoll_ctl failed: " << strerror(errno) <<std::endl;
        return false;
    }

    --_pendingEventCount;
    fd_ctx->events = new_events;
    FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}
bool IOManager::cancelEvent(int fd, Event event)
{
    FdContext *fd_ctx = nullptr;
    std::shared_lock<std::shared_mutex>read_lock(_mutex);
    if ((int)_fdContexts.size() > fd){
        fd_ctx = _fdContexts[fd];
        read_lock.unlock();
    }else{
        read_lock.unlock();
        return false;
    }

    std::lock_guard<std::mutex>lock(fd_ctx->mutex);
    if (!(fd_ctx->events & event)){
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = static_cast<uint32_t>(EPOLLET) | static_cast<uint32_t>(new_events);
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(_epfd,op,fd,&epevent);
    if (rt){
        std::cerr << "delEvent::epoll_ctl failed: " << strerror(errno) <<std::endl;
        return false;
    }

    --_pendingEventCount;
    fd_ctx->triggerEvent(event);
    return true;
}
bool IOManager::cancelAll(int fd)
{
    FdContext *fd_ctx = nullptr;
    std::shared_lock<std::shared_mutex>read_lock(_mutex);
    if ((int)_fdContexts.size() > fd){
        fd_ctx = _fdContexts[fd];
        read_lock.unlock();
    }else{
        read_lock.unlock();
        return false;
    }

    std::lock_guard<std::mutex>lock(fd_ctx->mutex);
    if (!fd_ctx->events){
        return false;
    }

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(_epfd,op,fd,&epevent);
    if (rt){
        std::cerr << "delEvent::epoll_ctl failed: " << strerror(errno) <<std::endl;
        return false;
    }

    if (fd_ctx->events&READ){
        fd_ctx->triggerEvent(READ);
        --_pendingEventCount;
    }
    if (fd_ctx->events &WRITE){
        fd_ctx->triggerEvent(WRITE);
        --_pendingEventCount;
    }

    assert(fd_ctx->events == 0);
    return true;
}


void IOManager::tickle() {
    if (!any_idle_threads()){
        return ;
    }
    int rt = write(_tickleFds[1],"T",1);
    assert(rt == 1);
}
bool IOManager::stopping()
{
    uint64_t timeout = getNextTimer();
    return timeout == ~0ull && _pendingEventCount == 0 && Scheduler::stopping();
}

void IOManager::idle() {
    static constexpr uint64_t MAX_EVENTS = 256;
    std::unique_ptr<epoll_event[]>events (new epoll_event[MAX_EVENTS]);

    while(true){
        if constexpr (debug::debug){
           std::cout << "IOManager::idle,run in thread:" << vik::thread::Thread::GetThreadId() << std::endl;
        }

        if (stopping()){
            if constexpr (debug::debug){
                std::cout << "name = " << getName() << " idle exits in the thread:" << vik::thread::Thread::GetThreadId() << std::endl;
            }
            break;
        }

        int rt = 0;
        while(true){
            static const uint64_t MAX_TIMEOUT = 5000;
            uint64_t next_timeout = getNextTimer();
            next_timeout = std::min(next_timeout,MAX_TIMEOUT);

            rt = epoll_wait(_epfd,events.get(),MAX_EVENTS,static_cast<int>(next_timeout));
            if (rt == -1){

                if (errno == EINTR){
                    continue;
                }
                std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
                return;
            }else{
                break;
            }
        }

        std::vector<std::function<void()>>cbs;
        listExpiredCb(cbs);
        if (!cbs.empty()){
            for(const auto&cb : cbs){
                add(cb);
            }
            cbs.clear();
        }

        for(int i = 0; i < rt ; ++i){
            epoll_event&event = events[i];

            if (event.data.fd == _tickleFds[0]){
                uint8_t dummy[256];
                /* 读完多余的数据 */
                while(read(_tickleFds[0],dummy,sizeof dummy)>0);
                /* 此时跳过其余操作 */
                continue;
            }


            FdContext*fd_ctx = (FdContext*)event.data.ptr;
            std::lock_guard<std::mutex>lock(fd_ctx->mutex);

            if (event.events & (EPOLLERR | EPOLLHUP)){
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }

            int real_event = NONE;
            if (event.events & EPOLLIN){
                real_event |= READ;
            }
            if (event.events & EPOLLOUT){
                real_event |= WRITE;
            }
            real_event&=fd_ctx->events;

            if (fd_ctx->events == NONE){
                continue;
            }

            int left_events = (fd_ctx->events & ~real_event);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            int rt2 = epoll_ctl(_epfd,op,fd_ctx->fd,&event);
            if (rt2){
                std::cerr << "idle:::epoll_ctl failed: " << strerror(errno) << std::endl;
                continue;
            }
            if (real_event & READ){
                fd_ctx->triggerEvent(READ);
                --_pendingEventCount;
            }
            if (real_event & WRITE){
                fd_ctx->triggerEvent(WRITE);
                --_pendingEventCount;
            }
        }
        vik::fiber::Fiber::GetThis() -> yield();
    }
}

void IOManager::onTimerInsertedAtFront() {
    tickle();
}

void IOManager::contextResize(std::size_t size) {

    _fdContexts.resize(size);
    auto sizes = _fdContexts.size();
    for(size_t i = 0; i < sizes;++i){
        if (_fdContexts[i] == nullptr){
            _fdContexts[i] = new FdContext();
            _fdContexts[i]->fd = i;
        }
    }
}
} // namespace vik::iomanager
