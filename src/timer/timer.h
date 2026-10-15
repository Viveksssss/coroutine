#pragma once


#include <set>
#include <queue>
#include <memory>
#include <vector>
#include <mutex>
#include <cassert>
#include <shared_mutex>
#include <functional>
#include <cstdint>


namespace vik::timer
{
class TimerManager;

class Timer : public std::enable_shared_from_this<Timer>
{
    friend class TimerManager;
public:
    bool cancel();
    bool refresh();
    bool reset(std::uint64_t ms,bool from_now = false);

private:
    Timer(std::uint64_t ms,std::function<void()>cb,bool recurring,TimerManager*manager);

private:
    /* 是否循环 */
    bool _recurring = false;
    /* 超时时间 */
    std::uint64_t _ms = 0;
    /* 绝对超时时间 */
    std::chrono::time_point<std::chrono::system_clock>_next;
    /* 超时回调 */
    std::function<void()>_cb;
    /* timer的管理者 */
    TimerManager*_manager = nullptr;

private:
    /* 最小堆对象的比较函数,依据是绝对时间 */
    struct Comparator
    {
        bool operator()(const std::shared_ptr<Timer>&lhs,const std::shared_ptr<Timer>&rhs)const;
    };
};

class TimerManager
{
    friend class Timer;
public:
    TimerManager();
    virtual ~TimerManager();

    std::shared_ptr<Timer>addTimer(std::uint64_t ms,std::function<void()>cb,bool recurring=false);

    std::shared_ptr<Timer>addConditionTimer(std::uint64_t ms,std::function<void()>cb,std::weak_ptr<void>weak_cond, bool recurring = false);

    std::uint64_t getNextTimer();

    void listExpiredCb(std::vector<std::function<void()>>&cbs);
    /* 是否有定时器 */
    bool hasTimer() const;
protected:
    virtual void onTimerInsertAtFront();
    void addTimer(std::shared_ptr<Timer>timer);
private:
    bool detectClockRollover();
private:
    mutable std::shared_mutex _mutex;
    std::set<std::shared_ptr<Timer>,Timer::Comparator>_timers;
    /* 下次next时间到的时候,是否需要修改 */
    bool _tickled = false;
    /* 系统时间是否回退的绝对时间 */
    std::chrono::time_point<std::chrono::system_clock>_previous_time;
};

}