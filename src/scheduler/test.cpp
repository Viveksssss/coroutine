#include "scheduler.h"
#include <iostream>
#include <atomic>
#include <thread>

using namespace std::chrono_literals;
using namespace vik::scheduler;

int main(){
    Scheduler sched(8,false,"test");
    sched.start();

    std::cout << "begin to sleep 1s" << std::endl;
    std::this_thread::sleep_for(1s);
    std::cout << "wake from sleep" << std::endl;

    sched.add([](){
        std::this_thread::sleep_for(2s);

        std::cout << "finished in scheduler after 2s" << std::endl;
    });
    sched.add([](){
        vik::fiber::yield();
        std::cout << "finished in scheduler" << std::endl;
    });
    sched.add([](){

        vik::fiber::yield();
        std::cout << "finished in scheduler" << std::endl;
    });

    sched.stop();
    std::cout << "done " << std::endl;
    return 0;
}