#include "thread.h"
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>  

using namespace vik::thread;

void func()
{
    std::cout << "id: " << Thread::GetThreadId() << ", name: " << Thread::GetThreadName();
    std::cout << ", this id: " << Thread::GetThis()->getThreadId() << ", this name: " << Thread::GetThis()->GetThreadName() << std::endl;

    sleep(60);
}

int main() {
    std::vector<std::shared_ptr<Thread>> thrs;

    for(int i=0;i<5;i++)
    {
        std::shared_ptr<Thread> thr = std::make_shared<Thread>(&func, "thread_"+std::to_string(i));
        thrs.push_back(thr);
    }

    for(int i=0;i<5;i++)
    {
        thrs[i]->join();
    }

    return 0;
}