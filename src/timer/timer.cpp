#include "timer.h"

namespace vik::timer{

static void OnTimer(std::weak_ptr<void>weak_cond,std::function<void()>cb){
    auto tmp = weak_cond.lock();
    if (tmp){
        cb();
    }
}

Timer::Timer(std::uint64_t ms,std::function<void()>cb,bool recurring,TimerManager*manager)
    : _ms(ms),_cb(cb),_recurring(recurring),_manager(manager)
{
    auto now = std::chrono::system_clock::now();
    _next = now + std::chrono::milliseconds(_ms);
}

bool Timer::cancel()
{
    std::unique_lock<std::shared_mutex>write_lock(_manager->_mutex);
    if (_cb == nullptr){
        return false;
    }else{
        _cb = nullptr;
    }
    auto it = _manager->_timers.find(shared_from_this());
    if (it!=_manager->_timers.end()){
        _manager->_timers.erase(it);
    }
    return true;
}
bool Timer::refresh()
{
    std::unique_lock<std::shared_mutex>write_lock(_manager->_mutex);
    if (!_cb){
        return false;
    }
    auto it = _manager->_timers.find(shared_from_this());
    if (it == _manager->_timers.end()){
        return false;
    }
    _manager->_timers.erase(it);
    _next = std::chrono::system_clock::now() + std::chrono::milliseconds(_ms);
    _manager->_timers.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now)
{
    if (_ms == ms && !from_now){
        return true;
    }
    {
        std::unique_lock<std::shared_mutex>write_lock(_manager->_mutex);
        if (!_cb){
            return false;
        }
        auto it = _manager->_timers.find(shared_from_this());
        if (it == _manager->_timers.end()){
            return false;
        }
        _manager->_timers.erase(it);
    }
    auto start = from_now ? std::chrono::system_clock::now() : _next - std::chrono::milliseconds(_ms);
    _ms = ms;
    _next = start + std::chrono::milliseconds(_ms);
    _manager->addTimer(shared_from_this());
    return true;
}

bool Timer::Comparator::operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const
{
    assert(lhs!=nullptr&&rhs!=nullptr);
    return lhs->_next < rhs->_next;
}

TimerManager::TimerManager() {
    _previous_time = std::chrono::system_clock::now();
}

TimerManager::~TimerManager() {
}

std::shared_ptr<Timer> TimerManager::addTimer(std::uint64_t ms, std::function<void()> cb, bool recurring)
{
    std::shared_ptr<Timer> timer(new Timer(ms,cb,recurring,this)); 
    addTimer(timer);
    return timer;
}

std::shared_ptr<Timer> TimerManager::addConditionTimer(std::uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring)
{
    return addTimer(ms,std::bind(&OnTimer,weak_cond,cb),recurring);
}

std::uint64_t TimerManager::getNextTimer()
{
    std::unique_lock<std::shared_mutex>read_lock(_mutex);

    _tickled = false;

    if (_timers.empty()){
        return ~0ull;
    }

    auto now = std::chrono::system_clock::now();
    auto time = (*(_timers.begin())) -> _next;
    if (now >= time){
        return 0;
    }else{
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time -now);
        return static_cast<uint64_t>(duration.count());
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()>> &cbs) {
    auto now = std::chrono::system_clock::now();
    std::unique_lock<std::shared_mutex>write_lock(_mutex);

    bool rollover = detectClockRollover();
    while(!_timers.empty() && (rollover || (*_timers.begin())->_next <= now)){
        std::shared_ptr<Timer>temp = *_timers.begin();
        _timers.erase(_timers.begin());

        cbs.push_back(temp->_cb);
        if (temp->_recurring){
            temp->_next = now + std::chrono::milliseconds(temp->_ms);
            _timers.insert(temp);
        }else{
            temp->_cb = nullptr;
        }
    }
}

bool TimerManager::hasTimer()const 
{
    std::shared_lock<std::shared_mutex>lock(_mutex);
    return !_timers.empty();
}

void TimerManager::onTimerInsertedAtFront() {
}

void TimerManager::addTimer(std::shared_ptr<Timer> timer) {
    bool at_front = false;
    {
        std::unique_lock<std::shared_mutex>lock(_mutex);
        auto it = _timers.insert(timer).first;
        at_front = (it == _timers.begin() && !_tickled);
        if (at_front){
            _tickled = true;
        }
    }
    if (at_front){
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover()
{
    bool rollover = false;
    auto now = std::chrono::system_clock::now();
    if (now < (_previous_time - std::chrono::milliseconds(60 * 60 * 1000)) ){
        rollover = true;
    }
    _previous_time = now;
    return rollover;
}

} // namespace vik::timer
