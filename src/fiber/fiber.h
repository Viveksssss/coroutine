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
        READY,
        RUNNING,
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
    /* 让出执行权利 */
    void yield();

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

}