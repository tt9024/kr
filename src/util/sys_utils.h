#pragma once

// socket stuffs
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <netinet/tcp.h>

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

    bool set_socket_non_blocking(int sockfd) {
        // get socket flags
        int flags = fcntl(sockfd, F_GETFL);
        if (flags == -1)
            return false;

        // set non-blocking mode
        return ( fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == 0);
    };


    int tcp_socket( const char *host, unsigned int port, bool blocking)
    {
        // reset errno
        errno = 0;
        int fd = 0;

        // create socket
        fd = socket(AF_INET, SOCK_STREAM, 0);

        // cannot create socket
        if( fd < 0) {
            perror("cannot create a tcp socket");
            return -1;
        }

        // use local machine if no host passed in
        if ( !( host && *host)) {
            host = "127.0.0.1";
        }

        // starting to connect to server
        struct sockaddr_in sa;
        memset( &sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons( port);
        sa.sin_addr.s_addr = inet_addr(host);

        // try to connect
        if( (connect( fd, (struct sockaddr *) &sa, sizeof( sa))) < 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "tcp socket %d cannot connect to %s:%u", fd, host, port);
            perror(buf);
            return -1;
        }

        int flag = 1;
        int result = setsockopt(fd,            /* socket affected */
                             IPPROTO_TCP,     /* set option at TCP level */
                             TCP_NODELAY,     /* name of option */
                             (char *) &flag,  /* the cast is historical cruft */
                             sizeof(int));    /* length of option value */

        if (result < 0) {
            char buf[256];
           snprintf(buf, sizeof(buf), "tcp socket %d cannot set TCP_NODELAY "
                    "(connected to %s:%u)", fd, host, port);
            perror(buf);
        }

        if (!blocking) {
            if ( !set_socket_non_blocking(fd)){
                char buf[256];
                snprintf(buf, sizeof(buf), "tcp socket %d cannot set non-blocking "
                        "(connected to %s:%u)", fd, host, port);
                perror(buf);
            }
        }

        // successfully connected
        return fd;
    }


    int tcp_send(int fd, const char* buf, size_t sz)
    {
        int nResult = ::send( fd, buf, sz, MSG_NOSIGNAL);
        if( nResult < 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "tcp socket %d cannot send %d bytes of data."
                    ,fd, (int)sz);
            perror(buf);

            switch (errno) {
            case EWOULDBLOCK:
            case EMSGSIZE:
            case ENOBUFS:
            {
                return 0;
            }
            default:
                return -1;
            }
        }
        return nResult;
    }

    int tcp_receive(int fd, char* buf, size_t sz)
    {
        int nResult = ::recv( fd, buf, sz, 0);
        if( nResult <= 0) {
            if (errno == EWOULDBLOCK) {
                return 0;
            }
            char buf[256];
            snprintf(buf, sizeof(buf), "tcp socket %d receive error." ,fd);
            perror(buf);
            return -1;
        }
        return nResult;
    }

}
