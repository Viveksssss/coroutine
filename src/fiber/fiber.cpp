#include "fiber.h"
#include "debug.h"
#include <iostream>
namespace vik::fiber
{
/*正在运行的协程 */
thread_local Fiber*t_fiber = nullptr;
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
    if constexpr(debug::debug){
        std::cout << "use_count:" << GetThis().use_count() << "  Fiber::MainFunc() id: " << GetFiberId() << std::endl;
    }
    // std::shared_ptr<Fiber>curr = GetThis();
    Fiber*self = t_fiber;
    assert(self != nullptr);

    if constexpr (debug::debug){
        std::cout << "Fiber::MainFunc() id: " << GetFiberId() << " state: " << self->_state << "  cb start"<<std::endl;
    }

    self->_cb();
    self->_cb = nullptr;
    self->_state = TERM;

    if constexpr (debug::debug){
        std::cout << "Fiber::MainFunc() id: " << GetFiberId() << " state: " << self->_state << "  cb finished" <<std::endl;
    }
    self->yield();
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
    if constexpr(debug::debug){
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
    if constexpr(debug::debug){
        std::cout << "Fiber() id: " << _id << " state: " << _state << std::endl;
    }
}

Fiber::~Fiber() {
    s_fiber_count--;
    assert(!(_stack_ptr && _state == RUNNING) && "协程正在运行时被析构: 外部提前释放了最后一个引用");
    if (_stack_ptr){
        free(_stack_ptr);
        _stack_ptr = nullptr;
    }
    if constexpr(debug::debug){
        std::cout << "~Fiber() id: " << _id << " state: " << _state << std::endl;
    }
}

void Fiber::reset(std::function<void()> cb) {
    // TODO: 这里需要考虑协程状态的合法性
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
    assert(_run_in_scheduler ? t_scheduler_fiber !=        
 nullptr : t_thread_fiber != nullptr);
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
