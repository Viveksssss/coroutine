#include "fd_manager.h"
#include "hook.h"
#include <sys/types.h>
#include <sys/stat.h>


namespace vik::fdmanager{

template<typename T>
std::atomic<T*>Singleton<T>::instance{nullptr};
template<typename T>
std::mutex Singleton<T>::mutex;

/* 显式实例化定义必须放在静态成员的定义之后,否则静态成员的实体不会被实例化出来 */
template class Singleton<FdManager>;


FdCtx::FdCtx(int fd)
    : _fd(fd)
{
    init();
}

FdCtx::~FdCtx() {
}

bool vik::fdmanager::FdCtx::init()
{
    if (_is_init){
        return true;
    }

    struct stat statbuf;
    if (fstat(_fd,&statbuf) == -1){
        _is_init = false;
        _is_socket = false;
    }else{
        _is_init = true;
        _is_socket = S_ISSOCK(statbuf.st_mode);
    }

    if (_is_socket){
        int flags = vik::hook::fcntl_f(_fd,F_GETFL,0);
        if (!(flags&O_NONBLOCK)){
            vik::hook::fcntl_f(_fd,F_SETFL,flags | O_NONBLOCK);
        }
        _sys_nonblock = true;
    }else{
        _sys_nonblock = false;
    }
    return _is_init;
}

void FdCtx::setTimeout(int type, uint64_t v) {
    if (type == SO_RCVTIMEO){
        _recv_timeout = v;
    }else{
        _send_timeout = v;
    }

}
uint64_t FdCtx::getTimeout(int type)
{
    if (type == SO_RCVTIMEO){
        return _recv_timeout;
    }else{
        return _send_timeout;
    }
}

FdManager::FdManager() {
    _datas.resize(64);
}
std::shared_ptr<FdCtx> FdManager::get(int fd, bool auto_create)
{
    if (fd == -1){
        return nullptr;
    }
    std::shared_lock<std::shared_mutex>lock(_mutex);
    if (_datas.size() <= fd){
        if (auto_create == false){
            return nullptr;
        }
    }else{
        if (_datas[fd] || !auto_create){
            return _datas[fd];
        }
    }

    lock.unlock();
    std::unique_lock<std::shared_mutex>write_lock(_mutex);

    if (_datas.size() <= fd){
        _datas.resize(fd*1.5);
    }
    _datas[fd] = std::make_shared<FdCtx>(fd);
    return _datas[fd];
}

void FdManager::del(int fd) {
    std::unique_lock<std::shared_mutex>lock(_mutex);
    if (_datas.size() <= fd){
        return;
    }
    _datas[fd].reset();
}
} // namespace vik::fdmanager
