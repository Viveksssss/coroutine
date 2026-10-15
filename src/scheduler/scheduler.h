#pragma once 

#include "fiber.h"
#include "thread.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>


namespace vik::scheduler
{
class Scheduler
{
public:
    Scheduler(std::size_t threads = 1,bool use_caller = true,const std::string& name = "Scheduler");
    virtual ~Scheduler();

public:
    static Scheduler* GetThis();

protected:
    void SetThis();

public:
    template<typename FiberOrCb>
    void add(FiberOrCb f,int thread = -1){
        // 标记队列的状态
        bool need_tickle = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            // 有空闲线程在等待就必须唤醒它, 否则它要一直等到超时;
            // 队列原本为空时也通知一次, 避免刚入队的任务没人来取
            need_tickle = _tasks.empty() || _idel_threads > 0;
            Task task(f,thread);
            if (task.executable()){
                _tasks.push_back(std::move(task));
            }
        }
        if (need_tickle){
            tickle();
        }
    }

    virtual void start();

    virtual void stop();

protected:
    /* 唤醒线程 */
    virtual void tickle();
    /* 线程函数 */
    virtual void run();
    /* 能否关闭 */
    virtual bool stopping();
    /* 空闲协程函数 */
    virtual void idle();
    /* 是否有空闲线程 */
    inline bool any_idle_threads(){return _idel_threads > 0;}

protected:
    /* stopping() 的无锁版本, 调用者必须已经持有 _mutex */
    bool stopping_locked() const;
    /* 队列中是否有属于 thread_id(或是未绑定线程)的任务, 调用者必须已经持有 _mutex */
    bool has_task_locked(int thread_id) const;
    /* 一个任务执行结束: 递减活跃计数, 关闭阶段需要唤醒等待中的空闲线程 */
    void task_finished();

private:
    struct Task{
        std::shared_ptr<vik::fiber::Fiber>_fiber;
        std::function<void()>_cb;
        int _thread; // 指定任务需要运行的线程id

        Task(){
            _fiber = nullptr;
            _cb = nullptr;
            _thread = -1;
        }

        Task(std::shared_ptr<vik::fiber::Fiber>f,int thr){
            _fiber = f;
            _thread = thr;
        }
        Task(std::function<void()>f,int thr){
            _cb = f;
            _thread = thr;
        }
        Task(std::shared_ptr<vik::fiber::Fiber>*f,int thr){
            _fiber.swap(*f);
            _thread = thr;
        }
        Task(std::function<void()>*f,int thr){
            _cb.swap(*f);
            _thread = thr;
        }

        void reset(){
            _fiber = nullptr;
            _cb = nullptr;
            _thread = -1;
        }

        bool executable(){
            return (_fiber || _cb);
        }
    };

private:
    /* 空闲线程等待的条件变量, 与 _mutex 配对使用:
     * 谓词判断和状态修改都在 _mutex 保护下, 所以不会丢失唤醒, 也就不需要轮询超时 */
    std::condition_variable _wait_cv;
    /* 状态锁: 同时保护 _tasks/_stopping/_active_threads 与上面的条件变量 */
    std::mutex _mutex;
    /* 调度器名称 */
    std::string _name;
    /* 线程池 */
    std::vector<std::shared_ptr<vik::thread::Thread>>_threads;
    /* 任务队列 */
    std::vector<Task>_tasks;
    /* 存储工作线程的id */
    std::vector<int>_thread_ids;
    /* 需要创建的线程数 */
    std::size_t _thread_count = 0;
    /* 活跃的线程数 */
    std::atomic<size_t>_active_threads = {0};
    /* 空闲的线程数 */
    std::atomic<size_t>_idel_threads = {0};
    /* 主线程是否用作工作线程 */
    bool _use_caller;
    /* use_caller为true,创建额外的调度协程 */
    std::shared_ptr<vik::fiber::Fiber>_scheduler_fiber;
    /* use_caller为true,记录线程id */
    int _root_thread = -1;
    /* 是否正在关闭: stop() 之后为 true, 可能被 run() 无锁读取 */
    std::atomic<bool>_stopping = {false};

};


}
