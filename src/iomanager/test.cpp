#include "iomanager.h"
#include <iostream>

int main(){

    vik::iomanager::IOManager iom(2);

    // iom.addTimer(0,[](){
    //     std::cout << "Timer is out" << std::endl;
    // });

    // iom.add([](){
    //     std::cout << "Only add function" << std::endl;
    // });

    // iom.addTimer(300,[](){
    //     std::cout << "Time out recurring" << std::endl;
    // },true);

    // iom.addTimer(500,[](){
    //     std::cout << "Time out for 500ms" << std::endl; 
    //     vik::fiber::yield();
    //     std::cout << "resumed from yield" << std::endl;
    // });

    int fds[2];
    pipe(fds);   // fds[0] 读端，fds[1] 写端

    // 给读端注册 READ 事件
    iom.addEvent(fds[0], vik::iomanager::IOManager::READ, [fds]() {
        char buf[64] = {0};
        read(fds[0], buf, sizeof buf);
        std::cout << "read: " << buf << std::endl;
    });

    // 200ms 后往写端写数据，触发 READ
    iom.addTimer(200, [fds]() {
        std::cout << "writing data..." << std::endl;
        write(fds[1], "hello", 5);
    },true);


    return 0;

}
