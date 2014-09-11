#pragma once

#include <sys/select.h>
#define STDIN_FILENO 0

namespace utils {

    // non-blocking key board reader
    // return non-zero on input, 0 otherwise
    std::string kbhit()
    {
        struct timeval tv;
        fd_set fds;
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
        select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
        int k =  FD_ISSET(STDIN_FILENO, &fds);
        if (k > 0) {
            std::string cmd;
            getline(std::cin, cmd);
            return cmd;
        } else {
            return "";
        }
    };
}
