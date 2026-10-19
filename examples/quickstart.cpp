/**
 * 最小可用示例: 只依赖安装后的头文件 + libcoroutine.so。
 *
 * 先装:  ./install.sh
 *        头文件 -> /usr/include/coroutine/
 *        动态库 -> /usr/lib/coroutine/
 * 再编:  g++ -std=c++17 -pthread quickstart.cpp \
 *            -L/usr/lib/coroutine -lcoroutine -Wl,-rpath,/usr/lib/coroutine -o quickstart
 *   或:  make run
 */
#include <coroutine/fd_manager.h>
#include <coroutine/hook.h>
#include <coroutine/iomanager.h>

#include <chrono>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    /* use_caller=true: 主线程也参与调度, 任务跑在主线程的调度循环里 */
    vik::iomanager::IOManager iom(1);

    const auto t0 = std::chrono::steady_clock::now();
    auto ms = [&t0] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0).count();
    };
    auto say = [&ms](const std::string& s) {
        std::cout << "[" << ms() << "ms] " << s << std::endl;
    };

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        std::cerr << "socketpair failed" << std::endl;
        return 1;
    }
    /* socketpair 不经过被 hook 的 socket(), 手动登记, 顺带会被设为非阻塞 */
    vik::fdmanager::FdMgr::GetInstance()->get(sv[0], true);
    vik::fdmanager::FdMgr::GetInstance()->get(sv[1], true);

    /* 任务 A: 被 hook 的 usleep 会把协程挂起, 600ms 后由定时器唤醒 */
    iom.add([&] {
        /* t_hook_enable 是 thread_local, 必须在真正执行任务的线程里打开 */
        vik::hook::set_hook_enable(true);
        say("A: 开始 600ms 的等待");
        usleep(600 * 1000);
        say("A: 醒来了");
    });

    /* 任务 B: 被 hook 的 read 在没数据时挂起, 等 epoll 的 READ 事件 */
    iom.add([&] {
        vik::hook::set_hook_enable(true);
        say("B: 阻塞读 socket, 等数据");
        char buf[32] = {0};
        ssize_t n = read(sv[0], buf, sizeof buf);
        say(std::string("B: 读到 ") + std::to_string(n) + " 字节: " + buf);
    });

    /* 300ms 后往写端塞数据, 触发 B 的 READ 事件 */
    iom.addTimer(300, [&] {
        say("timer: 往 socket 写 hello");
        ssize_t n = write(sv[1], "hello", 5);
        say("timer: write 返回 " + std::to_string(n));
    });

    say("main: 任务都提交了, stop() 会一直跑到所有任务结束");
    /* 不调用 stop() 也行: ~IOManager 里会 stop(), 效果一样 */
    iom.stop();
    say("main: 全部任务结束");
    return 0;
}
