#pragma once

#include <memory>       
#include <atomic>       
#include <functional>   
#include <cassert>      
#include <ucontext.h>   
#include <unistd.h>
#include <mutex>

namespace vik::fiber
{
class Fiber;
extern thread_local Fiber*t_fiber;
class Fiber : public std::enable_shared_from_this<Fiber>
{
public:

    /*协程状态*/
    enum State{
        READY,      /* 可运行: 主动 yield 让出, 调度器会把它放回队列继续调度 */
        RUNNING,
        SUSPEND,    /* 挂起: 在等外部条件(定时器/epoll 事件), 只能由别人 resume */
        TERM
    };
    
private:
    /*协程仅有GetThis()调用*/
    Fiber();

public:
    Fiber(std::function<void()> cb, size_t stacksize = 0,bool run_in_scheduler = true);
    ~Fiber();

    /* 重置协程 */
    void reset(std::function<void()> cb);
    /* 恢复执行 */
    void resume();
    /* 让出执行权利: 置为 READY, 之后调度器会重新调度它 */
    void yield();
    /* 挂起: 置为 SUSPEND, 一直等到别人 resume() 才继续执行 */
    void suspend();

    inline uint64_t getId()const{return _id;}
    inline State getState()const{return _state;}

public:
    /* 设置当前协程 */
    static void SetThis(Fiber* f);
    /* 获取当前协程*/
    static std::shared_ptr<Fiber> GetThis();
    /*设置调度协程*/
    static void SetSchedulerFiber( Fiber* f);
    /*当前运行的协程的id */
    static uint64_t GetFiberId();
    /*协程函数 */
    static void MainFunc();


private:
    /* yield 与 suspend 的公共实现: 先把状态改成目标状态, 再切回调度协程 */
    void yieldTo(State target);
    /* id */
    uint64_t _id = 0;
    /* 协程状态 */
    State _state = READY;
    /* 协程栈大小 */
    uint32_t _stack_size = 0;
    /* 协程上下文*/
    ucontext_t _ctx;
    /* 协程栈指针 */
    void* _stack_ptr = nullptr;
    /* 协程函数 */
    std::function<void()> _cb;
    /* 是否让出执行权给调度协程 */
    bool _run_in_scheduler = true;
public:
    std::mutex _mtx;
};

inline void yield(){
    Fiber*self = t_fiber;
    if(!self)return; // 还没有进入协程环境
    self->yield();
}

inline void suspend(){
    Fiber*fiber = t_fiber;
    if (!fiber)return;
    fiber->suspend();
}

}