# vik

一个 C++ 协程库，包含有栈协程、多线程调度、基于 epoll 的 IO 事件管理和定时器。目标是让业务代码用同步写法表达异步逻辑。

当前完成度：协程调度、定时器、fd 读写事件均可运行，测试用例在 `test/` 下。尚未实现协程池和连接管理。

## 目录结构
```shell
vik/
├── fiber/ 有栈协程，基于 ucontext
├── thread/ pthread 封装
├── scheduler/ 多线程调度器
├── timer/ 定时器管理
├── iomanager/ epoll + 定时器，事件循环
├── hook/ 钩子代码
├── debug/ 调试输出
└── test/ 测试代码
```

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

## 安装

```bash
mkdir -p build && cd build && cmake .. && ninja   # 没有 ninja 就用 make
cd ..
./install.sh                                      # 不是 root 会自动加 sudo
```

装到两个目录，没有别的：

```
/usr/include/coroutine/*.h              头文件
/usr/lib/coroutine/libcoroutine.so      动态库
```

卸载：

```bash
sudo rm -rf /usr/include/coroutine /usr/lib/coroutine
```

## 基本用法

### 1. 最小用例

```cpp
#include <coroutine/iomanager.h>
#include <coroutine/hook.h>

#include <iostream>
#include <unistd.h>

int main() {
    vik::iomanager::IOManager iom(1);   // 1 个调度线程; 主线程也参与调度
    vik::hook::set_hook_enable(true);   // 打开 hook(thread_local, 在当前线程生效)

    iom.add([] {                        // 提交任务: 普通函数/lambda 即可, 同步写法
        std::cout << "开始睡 2 秒" << std::endl;
        sleep(2);                       // 被 hook: 协程挂起, 线程不阻塞
        std::cout << "睡醒了" << std::endl;
    });

    iom.add([] {
        std::cout << "睡的那 2 秒里, 这个任务照跑" << std::endl;
    });

    iom.stop();                         // 阻塞到所有任务/定时器结束
    return 0;
}
```

编译：

```bash
g++ -std=c++17 -pthread main.cpp \
    -L/usr/lib/coroutine -lcoroutine -Wl,-rpath,/usr/lib/coroutine -o main
```

输出（顺序说明 `sleep` 确实让出了执行权）：

```
开始睡 2 秒
睡的那 2 秒里, 这个任务照跑
睡醒了
```

三个关键点：

- `IOManager iom(线程数)`：构造即启动。`use_caller` 默认 true，主线程也进调度循环，所以单线程场景下任务跑在主线程里。
- `iom.add(...)`：把任务提交给调度器。任务里写同步代码，遇到被 hook 的 `sleep` / `read` / `write` 会自动让出，恢复后从下一行继续。
- `iom.stop()`：等队列跑空、定时器和 fd 事件都没了才返回。不写它也行，`~IOManager` 里会调一次。

### 2. 定时器

```cpp
// 500ms 后执行一次
iom.addTimer(500, [] { std::cout << "500ms" << std::endl; });

// 每 300ms 执行一次(recurring=true), 返回的 timer 可以随时取消
auto timer = iom.addTimer(300, [] { std::cout << "tick" << std::endl; }, true);
timer->cancel();      // 取消
timer->refresh();     // 重新计时
timer->reset(1000);   // 改成 1000ms
```

条件定时器绑一个 `weak_ptr`，对象销毁后回调不再触发（适合“连接还活着才做心跳”这类场景）：

```cpp
std::shared_ptr<Conn> conn = ...;
iom.addConditionTimer(1000, [] { /* 心跳 */ }, conn);
```

### 3. socket 读写

```cpp
#include <coroutine/fd_manager.h>   // FdMgr 在这里

int sv[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
vik::fdmanager::FdMgr::GetInstance()->get(sv[0], true);   // 登记 fd
vik::fdmanager::FdMgr::GetInstance()->get(sv[1], true);

iom.add([&] {
    char buf[64] = {0};
    ssize_t n = read(sv[0], buf, sizeof buf);   // 没数据 -> 协程挂起, 等 epoll 通知
    std::cout << "读到 " << n << " 字节: " << buf << std::endl;
});

iom.addTimer(500, [&] {
    write(sv[1], "hello", 5);                   // 触发上面的 READ 事件
});
```

不用协程时 `read` 会把整个线程堵住；这里 `read` 发现 `EAGAIN` 就把当前协程挂起，事件就绪后由 epoll 唤醒，线程继续跑别的任务。

### 4. 多线程调度

```cpp
vik::iomanager::IOManager iom(4);   // 4 个调度线程(use_caller 为 true 时主线程也算一个)

iom.add([] {
    vik::hook::set_hook_enable(true);   // 在每个任务里开, 见下面注意事项
    sleep(1);
});

iom.add([] { /* 由任意一个空闲线程取走 */ });
```

`add` 的第二个参数可以指定线程 id（默认 `-1` = 任意线程），但库里没有对外暴露 tid 的接口，实际用不上，不传就好。

## 注意事项

- **`t_hook_enable` 是 `thread_local`**。多线程时只在 main 里 `set_hook_enable(true)` 是不够的，跑任务的线程看不到。习惯写法是每个任务开头调一次。
- **fd 必须是被 hook 的 `socket()` 创建的**（或手动 `FdMgr::GetInstance()->get(fd, true)` 登记），否则 `read` / `write` 会被直接放行，退化成阻塞调用。`pipe`、`socketpair` 这类就要手动登记。
- **`std::this_thread::sleep_for` 不会被 hook**，它睡的是线程不是协程。协程里用 `sleep` / `usleep` / `nanosleep`。
- **`stop()` 不能在 worker 线程里调用**（它要 join 工作线程），代码里用 assert 卡住了。要主动结束就在主线程调。

完整的可运行示例见 `examples/quickstart.cpp`，`cd examples && make run` 直接跑。