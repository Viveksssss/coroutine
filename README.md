# vik

一个 C++ 协程库，包含有栈协程、多线程调度、基于 epoll 的 IO 事件管理和定时器。目标是让业务代码用同步写法表达异步逻辑。

当前完成度：协程调度、定时器、fd 读写事件均可运行，测试用例在 `test/` 下。尚未实现协程池和连接管理。

## 目录结构
vik/
├── fiber/ 有栈协程，基于 ucontext
├── thread/ pthread 封装
├── scheduler/ 多线程调度器
├── timer/ 定时器管理
├── iomanager/ epoll + 定时器，事件循环
├── hook/ 钩子代码
├── debug/ 调试输出
└── test/ 测试代码


依赖关系自底向上：`thread` → `fiber` → `scheduler` → `iomanager`，`timer` 由 `iomanager` 使用。

## 分层设计

### thread

对 pthread 的薄封装，统一线程名、线程 id 等基础能力。

### fiber

有栈协程，使用 `ucontext` 的 `makecontext` / `swapcontext` 实现。每个协程持有独立栈（默认 128KB，可配置），状态机为 `INIT → RUNNING → READY → TERM`。

选择有栈协程的原因：同步写法自然。IO 阻塞时 `yield` 让出，就绪后 `resume` 从挂起点继续，业务代码无需改造。代价是每个协程一份栈，内存占用高于无栈协程。

### scheduler

N 个工作线程 + 一个任务队列 `_tasks`。每个线程运行 `run()` 循环，从队列取任务执行。

无任务时不忙等，而是切到 `idle` 协程阻塞在条件变量上，有任务时 `tickle()` 唤醒。`use_caller` 为 true 时主线程参与调度，减少一次线程切换。`stop()` 会 join 所有工作线程，因此不能在 worker 线程内调用，代码中用 assert 约束。

### timer

基于 `std::set` 的定时器集合，按到期时间排序。`getNextTimer()` 返回最近定时器剩余毫秒数，作为 `epoll_wait` 的超时参数——这是定时器与 epoll 唯一的连接点，定时器本身不进入 epoll。

支持一次性、循环、条件定时器（绑定 `weak_ptr`，对象销毁后不触发）。

### iomanager

同时继承 `Scheduler` 和 `TimerManager`，自行管理一个 epoll 实例。

`idle()` 是事件循环核心：


```cpp

while (true) {
    if (stopping()) break;
    timeout = getNextTimer(); // 最近定时器剩余时间
    epoll_wait(epfd, timeout); // 等待 fd 就绪或超时
    listExpiredCb(cbs); // 收集到期定时器回调
    for (cb : cbs) add(cb); // 提交到调度器队列
    for (event : events) { // 处理 fd 事件
        triggerEvent(...); // 同样提交到调度器队列
    }
    yield(); // 让出，回到 run 执行队列
}

```


两个触发源（定时器、epoll）最终都汇入 `Scheduler::_tasks`，由协程统一执行。因此定时器回调并不在 `idle` 的调用栈中直接运行。

使用 tickle 管道（`pipe` 创建，读端注册进 epoll）。定时器插入队首时、`stop()` 时，向写端写入一个字节，使 `epoll_wait` 立即返回并重算超时，避免新定时器因旧超时迟到。


## 编译与测试

```bash
    cd <目录名>
    mkdir build && cd build && cmake .. && make
```