#pragma once

#include <memory>
#include <shared_mutex>
#include <atomic>
#include "thread.h"


namespace vik::fdmanager
{

class FdCtx:public std::enable_shared_from_this<FdCtx>
{
private:
    bool _is_init = false;
    bool _is_socket = false;
    bool _sys_nonblock = false;
    bool _use_nonblock = false;
    bool _is_closed = false;
    int _fd;

    uint64_t _recv_timeout = (uint64_t ) -1;
    uint64_t _send_timeout = (uint64_t ) -1;
public:
    FdCtx(int fd);
    ~FdCtx();

    bool init();
    bool isInit()const{return _is_init;}
    bool isSocket()const{return _is_socket;}
    bool isClosed()const{return _is_closed;}
    
    void setUserNonblock(bool v){_use_nonblock = v;}
    bool getUserNonblock()const{return _use_nonblock;}

    void setSysNonblock(bool v){_sys_nonblock = v;}
    bool getSysNonblock()const {return _sys_nonblock;}

    void setTimeout(int type,uint64_t v);
    uint64_t getTimeout(int type);
};


class FdManager
{
public:
    FdManager();
    std::shared_ptr<FdCtx>get(int fd,bool auto_create = false);
    void del(int fd);
private:
    std::shared_mutex _mutex;
    std::vector<std::shared_ptr<FdCtx>>_datas;
};

template<typename T>
class Singleton
{
private:
    /* atomic 保证双重检查加锁时其他线程能看到完整的指针值 */
    static std::atomic<T*>instance;
    static std::mutex mutex;
protected:
    Singleton(){}
public:
    Singleton(const Singleton&) = delete;
    Singleton&operator=(const Singleton&) = delete;

    static T*GetInstance(){
        T*ptr = instance.load(std::memory_order_acquire);
        if (!ptr) {
            std::lock_guard<std::mutex> lk(mutex);
            ptr = instance.load(std::memory_order_relaxed);
            if (!ptr) {
                ptr = new T();
                instance.store(ptr,std::memory_order_release);
            }
        }
        return ptr;
    }

    static void DestroyInstance()
    {
        std::lock_guard<std::mutex> lk(mutex);
        delete instance.exchange(nullptr,std::memory_order_acq_rel);
    }

};

using FdMgr = Singleton<FdManager>;

/* 显式实例化声明: Singleton<FdManager> 的实体在 fd_manager.cpp 里实例化,
   这里声明 extern 避免各 TU 隐式实例化(会触发 -Wundefined-var-template) */
extern template class Singleton<FdManager>;
// extern template std::atomic<FdManager*> Singleton<FdManager>::instance;  

}