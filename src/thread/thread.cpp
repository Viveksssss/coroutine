#include <iostream>
#include <unistd.h>
#include <sys/syscall.h> 
#include "thread.h"
namespace vik::thread
{

static thread_local Thread* t_thread = nullptr;
static thread_local std::string t_thread_name = "UNKNOW";
Thread::Thread(std::function<void()> cb, std::string const &name) 
    :_cb(cb),_name(name)
{
    int rt = pthread_create(&_thread,nullptr,&Thread::run,this);
    if (rt){
        std::cerr << "pthread_create thread fail, rt=" << rt << std::endl;
        throw std::logic_error("pthread_create error");
    }
    _sem.wait();
}

Thread::~Thread() {
    if (_thread){
        pthread_detach(_thread);
        _thread = 0;
    }
}

void Thread::join() {
    if (_thread){
        int rt = pthread_join(_thread,nullptr);
        if (rt){
            std::cerr << "pthread_join thread fail, rt=" << rt << std::endl;
            throw std::logic_error("pthread_join error");
        }
        _thread = 0;
    }
}

pid_t Thread::GetThreadId() {
    return syscall(SYS_gettid);
}

Thread *Thread::GetThis() {
    return t_thread;
}

std::string const &Thread::GetThreadName() {
    return t_thread_name;
}

void Thread::SetThreadName(std::string const &name) {
    if (t_thread){
        t_thread->_name = name;
    }
    t_thread_name = name;
}

void *Thread::run(void *arg) {
    Thread*thread = (Thread*)arg;

    t_thread = thread;
    t_thread_name = thread->_name;
    thread->_id = GetThreadId();
    pthread_setname_np(pthread_self(),thread->_name.substr(0,15).c_str());

    std::function<void()> cb;
    cb.swap(thread->_cb);
    thread->_sem.signal();
    cb();
    return 0;
}
} // namespace vik::thread
