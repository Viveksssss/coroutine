#pragma once

#include "scheduler.h"
#include "timer.h"


namespace vik::iomanager 
{
class IOManager: public vik::scheduler::Scheduler,public vik::timer::TimerManager
{
public:
    enum Event{
        NONE = 0x0,
        READ = 0x1,
        WRITE = 0x4,
    };
private:
    struct FdContext{
        struct EventContext{
            vik::scheduler::Scheduler*scheduler = nullptr;
            std::shared_ptr<vik::fiber::Fiber>fiber;
            std::function<void()>cb;
        };

        EventContext read;
        EventContext write;
        int fd = 0;
        Event events = NONE;
        std::mutex mutex;
        EventContext&getEventContext(Event event);
        void resetEventContext(EventContext&ctx);
        void triggerEvent(Event evnet);
    };
public:
    IOManager(size_t threads = 1,bool use_caller = true,const std::string &name="IOManager");
    ~IOManager();

    int addEvent(int fd,Event event,std::function<void()>cb = nullptr);
    bool delEvent(int fd,Event event);
    bool cancelEvent(int fd,Event event);
    bool cancelAll(int fd);
    static IOManager*GetThis();

protected:
    void tickle()override;
    bool stopping()override;
    void idle()override;
    void onTimerInsertedAtFront()override;
    void contextResize(std::size_t size);

private:
    /* epoll fd */
    int _epfd = 0;
    /* 0 for read and 1 for write */
    int _tickleFds[2];
    std::atomic<std::size_t> _pendingEventCount = {0};
    std::shared_mutex _mutex;
    std::vector<FdContext*>_fdContexts;
};
}