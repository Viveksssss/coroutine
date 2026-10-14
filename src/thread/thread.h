#pragma once


#include <mutex>
#include <condition_variable>
#include <functional>
#include <string> 

#include "thread.h"


namespace vik::thread
{

class Semaphore{
private:
    std::mutex _mtx;
    std::condition_variable _cv;
    int _count;
public:
    Semaphore(int count = 0):_count(count){}

    void wait(){
        std::unique_lock<std::mutex>lock(_mtx);
        while(_count == 0){
            _cv.wait(lock);
        }
        _count --;
    }

    void signal(){
        std::unique_lock<std::mutex>lock(_mtx);
        _count ++;
        _cv.notify_one();
    }

};


class Thread{
public:
    Thread(std::function<void()>cb,const std::string&name = "");
    ~Thread();
    inline pid_t getThreadId()const {return _id;}
    inline const std::string& getThreadName()const{return _name;}
    void join();
public:
    static pid_t GetThreadId();
    static Thread*GetThis();
    static const std::string&GetThreadName();
    static void SetThreadName(const std::string&name);
private:
    static void*run(void*arg);
private:
    pid_t _id = -1;
    pthread_t _thread = 0;

    std::function<void()>_cb;
    std::string _name;
    Semaphore _sem;
};
};