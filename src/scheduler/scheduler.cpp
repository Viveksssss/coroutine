#include "scheduler.h"
#include "debug.h"
#include <algorithm>
#include <iostream>
using namespace vik::debug;
namespace vik::scheduler{

static thread_local Scheduler*t_scheduler = nullptr;
/* 当前线程在调度器中的线程id, 在 run() 开始时设置, idle() 用它判断队列里是否有自己的任务 */
static thread_local int t_thread_id = -1;

Scheduler::Scheduler(std::size_t threads, bool use_caller, std::string const &name)
    :_name(name),_thread_count(threads),_use_caller(use_caller)
{
    assert(threads > 0 && Scheduler::GetThis() == nullptr);
    SetThis();
    vik::thread::Thread::SetThreadName(_name);
    if (use_caller){
        _thread_count -- ;
        vik::fiber::Fiber::GetThis();
        _scheduler_fiber.reset(new vik::fiber::Fiber(std::bind(&Scheduler::run,this),0,false)); // false -> 该调度协程推出后返回主线程

        vik::fiber::Fiber::SetSchedulerFiber(_scheduler_fiber.get());
        _root_thread = vik::thread::Thread::GetThreadId();
        _thread_ids.push_back(_root_thread);
    }
    if constexpr (debug::debug){
        std::cout << "Scheduler::Scheduler() success\n";
    }
}

Scheduler::~Scheduler() {
    assert(stopping()== true);
    if (GetThis() == this){
        t_scheduler = nullptr;
    }
    if constexpr (debug::debug){
        std::cout << "Scheduler::~Scheduler() success\n";
    }
}

Scheduler *Scheduler::GetThis() {
    return t_scheduler;
}

void Scheduler::SetThis() {
    t_scheduler = this;
}

void Scheduler::start() {
    std::lock_guard<std::mutex>lock(_mutex);
    if (_stopping){
        std::cerr << "Scheduler is stopped" << std::endl;
        return;
    }
    assert(_threads.empty());
    _threads.resize(_thread_count);
    for(size_t i = 0 ; i < _thread_count;++i){
        std::string thread_name = _name+"_"+std::to_string(i);
        _threads[i].reset(new vik::thread::Thread(std::bind(&Scheduler::run,this),std::move(thread_name)));
        _thread_ids.push_back(_threads[i]->getThreadId());
    }

    if constexpr(debug::debug){
        std::cout << "Scheduler::start() success\n";
    }
}

void Scheduler::stop() {
    if constexpr (debug::debug){
        std::cout << "Scheduler::stop() starts in thread: "<<vik::thread::Thread::GetThreadId() << std::endl;
    }
    {
        std::lock_guard<std::mutex>lock(_mutex);
        if (_stopping)return;
        _stopping = true;
    }

    /* stop() 会 join 工作线程, 因此不能在调度器自己的工作线程里调用;
     * use_caller 时还必须在主线程里调用(要切到主线程的调度协程把队列跑空) */
    int const current_thread = vik::thread::Thread::GetThreadId();
    if (_use_caller){
        assert(current_thread == _root_thread && "stop() must be called in the caller thread");
    }else{
        assert(std::find(_thread_ids.begin(),_thread_ids.end(),current_thread) == _thread_ids.end()
               && "stop() must not be called in a scheduler worker thread");
    }

    /* 唤醒所有空闲线程: 让它们重新判断 stopping() 并退出 run() 循环 */
    tickle();

    if constexpr (debug::debug){
        std::cout << "stop: _stopping=" << _stopping
                    << " _tasks.size()=" << _tasks.size()
                    << " _active_threads=" << _active_threads << std::endl;
    }

    if (_use_caller && _scheduler_fiber){
        /* 主线程自己也进入调度循环, 把队列(含绑定到主线程的任务)跑空;
         * 若此时已经运行在调度协程里(比如在任务中调用 stop()), 就无需再切换 */
        if (_scheduler_fiber->getState() == vik::fiber::Fiber::READY){
            _scheduler_fiber->resume();
        }
    }

    /* Thread 析构只做 detach, 这里必须等线程真正退出, 否则调度器析构后
     * 工作线程还在访问 _tasks/_mutex */
    for(auto &thread : _threads){
        thread->join();
    }

    if constexpr (debug::debug){
        std::cout << "Scheduler::stop() success\n";
        std::cout << "stop: _stopping=" << _stopping
                            << " _tasks.size()=" << _tasks.size()
                            << " _active_threads=" << _active_threads << std::endl;
    }
}

void Scheduler::tickle() {
    /* 状态的修改与谓词的判断都在 _mutex 保护下, 所以通知时不必持锁, 也不会丢唤醒;
     * 用 notify_all 是因为任务可能绑定到某个具体线程, notify_one 可能唤醒错误的线程 */
    _wait_cv.notify_all();
}

void Scheduler::run() {
    int thread_id = vik::thread::Thread::GetThreadId();
    t_thread_id = thread_id;
    if constexpr (debug::debug) {
        std::cout << "Scheduler::run() starts in thread: " << thread_id << std::endl;
    }

    SetThis();
    if (thread_id != _root_thread){
        vik::fiber::Fiber::GetThis();
    }

    std::shared_ptr<vik::fiber::Fiber>idle_fiber = std::make_shared<vik::fiber::Fiber>(std::bind(&Scheduler::idle,this));
    Task task;
    while(true){
        task.reset();
        bool tickle_me = false;

        {
            std::lock_guard<std::mutex>lock(_mutex);
            for(auto it = _tasks.begin() ; it != _tasks.end() ; ++it){
                if (it->_thread!=-1 && it->_thread !=thread_id){
                    // 这个任务被指定给了别的线程: 通知它来取
                    tickle_me = true;
                    continue;
                }
                assert(it->_fiber || it->_cb);
                task = *it;
                _tasks.erase(it);
                _active_threads++;
                break;
            }
        }
        if (tickle_me){
            tickle();
        }

        if (task._fiber){
            {
                std::lock_guard<std::mutex>lock(task._fiber->_mtx);
                if (task._fiber->getState()!=vik::fiber::Fiber::TERM){
                    if constexpr (debug::debug){
                        std::cout << "thread_id" << thread_id << std::endl;
                    }
                    task._fiber->resume();
                }
            }
            /* 只有 READY 才放回队列: 那是协程主动 yield 让出时间片, 应该继续被调度;
             * SUSPEND 表示协程在等外部条件(被 hook 的 sleep/read/write 等定时器或 epoll 事件),
             * 放回队列就等于让 yield 立刻返回, 定时器形同虚设。唤醒由持有该协程的
             * 定时器回调 / 事件通知调用 add(fiber) 完成。 */
            if (task._fiber->getState() == vik::fiber::Fiber::READY) {
                std::lock_guard<std::mutex> lock(_mutex);
                _tasks.push_back(task);
            }

            task_finished();
            task.reset();
        }else if (task._cb){
            std::shared_ptr<vik::fiber::Fiber>cb_fiber = std::make_shared<vik::fiber::Fiber>(task._cb);
            {
                std::lock_guard<std::mutex>lock(cb_fiber->_mtx);
                if constexpr (debug::debug){
                    std::cout << "thread_id" << thread_id << std::endl;
                }
                cb_fiber->resume();
            }
            /* 同上: 主动 yield 的继续调度, 挂起等事件的交给唤醒者保管 */
            if (cb_fiber->getState() == vik::fiber::Fiber::READY){
                task._fiber = cb_fiber;
                task._cb = nullptr;
                std::lock_guard<std::mutex>lock(_mutex);
                _tasks.push_back(task);
            }
            task_finished();
            task.reset();
        }else{
            if (idle_fiber->getState()==vik::fiber::Fiber::TERM){
                if constexpr (debug::debug){
                    std::cout << "Schedule::run() ends in thread: "<<thread_id << std::endl;
                }
                break;
            }
            _idel_threads ++;
            idle_fiber -> resume();
            _idel_threads --;
        }
    }

}

void Scheduler::task_finished() {
    --_active_threads;
    /* 关闭阶段: 活跃任务归零可能让等待中的线程满足 stopping(), 必须唤醒它们 */
    if (_stopping){
        _wait_cv.notify_all();
    }
}

bool Scheduler::stopping() {
    std::lock_guard<std::mutex>lock(_mutex);
    return stopping_locked();
}

bool Scheduler::stopping_locked() const {
    return _stopping && _tasks.empty() && _active_threads == 0;
}

bool Scheduler::has_task_locked(int thread_id) const {
    for(auto const &task : _tasks){
        if (task._thread == -1 || task._thread == thread_id){
            return true;
        }
    }
    return false;
}

void Scheduler::idle() {
    if constexpr (debug::debug){
        std::cout << "Scheduler::idle() starts in thread: " << t_thread_id << std::endl;
    }
    std::unique_lock<std::mutex>lock(_mutex);
    /* 队列为空时真正睡在条件变量上(不再 sleep(1) 轮询), 被唤醒后
     * 再把执行权交回 run() 去取任务; 能退出说明 stopping() 成立 */
    while(!stopping_locked()){
        _wait_cv.wait(lock,[this]{
            return has_task_locked(t_thread_id) || stopping_locked();
        });
        if (stopping_locked()){
            break;
        }
        lock.unlock();
        vik::fiber::yield();
        lock.lock();
    }
}
} // namespace vik::scheduler
