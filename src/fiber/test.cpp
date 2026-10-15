#include "fiber.h"
#include <iostream>

using namespace vik::fiber;
void taskA() {
    std::cout << "A start\n";
    // vik::fiber::Fiber::GetThis()->yield();      // 让出，切回主协程
    yield();
    std::cout << "A resume\n";
}

void taskB() {
    std::cout << "B start\n";
    // vik::fiber::Fiber::GetThis()->yield();
    yield();
    std::cout << "B resume\n";
}

int main() {
    // 1. 先创建主协程（第一次 GetThis）
    // vik::fiber::Fiber::GetThis();

    // 2. 创建业务协程，run_in_scheduler = false
    auto fa = std::make_shared<vik::fiber::Fiber>(taskA, 128*1024, false);
    auto fb = std::make_shared<vik::fiber::Fiber>(taskB, 128*1024, false);

    // 3. 主协程直接 resume 它们
    fa->resume();     // 切到 A
    fb->resume();     // 切到 B
    fa->resume();     // 切回 A，让它跑完
    fb->resume();     // 切回 B，让它跑完

    std::cout << "main end\n";
    std::cout << fa.use_count() << std::endl;
    std::cout << fb.use_count() << std::endl;
    return 0;
}