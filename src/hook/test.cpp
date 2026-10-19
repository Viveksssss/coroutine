#include "fd_manager.h"
#include "iomanager.h"
#include "hook.h"
#include <iostream>

#include <chrono>
#include <iostream>

int main() {
    vik::iomanager::IOManager iom(1);

    auto t0 = std::chrono::steady_clock::now();
    auto ts = [&]() {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        return ms;
    };

    iom.add([&]() {
        std::cout << "[" << ts() << "ms] start to sleep for 5s" << std::endl;
        sleep(5);
        std::cout << "[" << ts() << "ms] end of sleep" << std::endl;
    });

    iom.add([&]() {
        std::cout << "[" << ts() << "ms] do yes" << std::endl;
    });

    return 0;
}