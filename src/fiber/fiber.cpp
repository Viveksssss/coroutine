#include "fiber.h"
#include <iostream>

static bool debug = false;
namespace vik::fiber
{
/*正在运行的协程 */
static thread_local Fiber*t_fiber = nullptr;
/*主协程 */
static thread_local std::shared_ptr<Fiber>t_thread_fiber = nullptr;
/*协程调度*/
static thread_local Fiber*t_scheduler_fiber = nullptr;
/* 协程id */
static std::atomic<uint64_t>s_fiber_id{0};
/* 协程数量计数器 */
static std::atomic<uint64_t>s_fiber_count{0};

void Fiber::SetThis(Fiber *f) {
    t_fiber = f;
}

std::shared_ptr<Fiber> Fiber::GetThis() {
    if (t_fiber){
        return t_fiber -> shared_from_this();
    }
    std::shared_ptr<Fiber> f(new Fiber());
    t_thread_fiber = f;
    t_scheduler_fiber = f.get();

    assert(t_fiber == f.get());
    return t_fiber -> shared_from_this();

}

void Fiber::SetSchedulerFiber(Fiber *f) {
    t_scheduler_fiber = f;
}

uint64_t Fiber::GetFiberId() {
    if(t_fiber){
        return t_fiber -> getId();
    }
    return (uint64_t) -1;
}

void Fiber::MainFunc() {
    std::shared_ptr<Fiber>curr = GetThis();
    assert(curr != nullptr);
    curr->_cb();
    curr->_cb = nullptr;
    curr->_state = TERM;

    auto raw_ptr = curr.get();
    curr.reset();
    raw_ptr->yield();
}

Fiber::Fiber(){
    SetThis(this);
    _state = RUNNING;
    if (getcontext(&_ctx)){
        std::cerr << "Fiber() failed\n";
        pthread_exit(0);
    }
    _id = s_fiber_id++;
    s_fiber_count++; // 活跃协程+1
    if (debug){
        std::cout << "Fiber() id: " << _id << " state: " << _state << std::endl;
    }
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) 
    :_cb(cb),_run_in_scheduler(run_in_scheduler)
{
    _state = READY;
    _stack_size = stacksize ? stacksize : 12800; // 默认栈大小为1MB
    _stack_ptr = malloc(_stack_size);
    if (getcontext(&_ctx)){
        std::cerr << "Fiber() failed\n";
        pthread_exit(0);
    }

    _ctx.uc_link = nullptr;
    _ctx.uc_stack.ss_sp = _stack_ptr;
    _ctx.uc_stack.ss_size = _stack_size;
    makecontext(&_ctx, &Fiber::MainFunc, 0);

    _id = s_fiber_id++;
    s_fiber_count++; // 活跃协程+1
    if (debug){
        std::cout << "Fiber() id: " << _id << " state: " << _state << std::endl;
    }
}

Fiber::~Fiber() {
    s_fiber_count--;
    if (_stack_ptr){
        free(_stack_ptr);
    }
    if (debug){
        std::cout << "~Fiber() id: " << _id << " state: " << _state << std::endl;
    }
}

void Fiber::reset(std::function<void()> cb) {
    assert(_stack_ptr!=nullptr && _state == TERM);
    _state = READY;
    _cb = cb;
    if (getcontext(&_ctx)){
        std::cerr << "Fiber::reset() failed\n";
        pthread_exit(0);
    }

    _ctx.uc_link = nullptr;
    _ctx.uc_stack.ss_sp = _stack_ptr;
    _ctx.uc_stack.ss_size = _stack_size;
    makecontext(&_ctx, &Fiber::MainFunc, 0);
}

void Fiber::resume() {
    if (!t_thread_fiber){
        GetThis();
    }
    assert(_state == READY);
    _state = RUNNING;
    if (_run_in_scheduler){
        SetThis(this);
        if (swapcontext(&t_scheduler_fiber->_ctx, &_ctx)){
            std::cerr << "Fiber::resume() failed\n";
            pthread_exit(0);
        }
    }else{
        SetThis(this);
        if (swapcontext(&t_thread_fiber->_ctx, &_ctx)){
            std::cerr << "Fiber::resume() failed\n";
            pthread_exit(0);
        }
    }
    
}

void Fiber::yield() {
    assert(_state==RUNNING || _state==TERM);
    if (_state == RUNNING){
        _state = READY;
    }

    if (_run_in_scheduler){
        SetThis(t_scheduler_fiber);
        if (swapcontext(&_ctx, &t_scheduler_fiber->_ctx)){
            std::cerr << "Fiber::yield() failed\n";
            pthread_exit(0);
        }
    }else{
        SetThis(t_thread_fiber.get());
        if (swapcontext(&_ctx, &t_thread_fiber->_ctx)){
            std::cerr << "Fiber::yield() failed\n";
            pthread_exit(0);
        }
    }
}

} // namespace vik::fiber
