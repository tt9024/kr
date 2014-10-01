#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "tp.hpp"
#include "sys_utils.h"

using namespace tp;
using namespace utils;
using namespace std;

#define TRADER_ID 1
#define BARSIZE 30
typedef BookQ<ShmCircularBuffer> BookQType;
typedef OrderQ<ShmCircularBuffer> OrderQType;
typedef FillQ<ShmCircularBuffer> FillQType;

bool interrupted;

BookDepot books[utils::TotalSecurity];
unsigned int oid;
int model_fd[utils::TotalSecurity];

bool makeOrder(eSecurity secid, bool is_buy, int size, OrderQType::Writer* owriter) {
    bool is_bid = !is_buy;
    Price px = books[(int)secid].getBestPrice(is_bid);
    if (px <= 0) {
        return false;
    }
    owriter->newOrder(++oid, secid, TIF_GTC, OT_Limit, is_buy?BS_Buy:BS_Sell, px, size);
    logInfo("sent new order OID(%lu): %s %d %s at %.7lf",
        (unsigned long) oid, is_buy?"Buy":"Sell", size, getSymbol(secid), pxToDouble(secid, px));
    return true;
}

void sig_handler(int signo)
{
  if (signo == SIGINT) {
    printf("Received SIGINT, exiting...\n");
    interrupted = true;
  }
}


int main(int argc, char** argv) {
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    // hand shake with the model first
    if (argc != 4) {
        printf("Usage: %s IP Port Symbol\n", argv[0]);
        return 0;
    }
    BookQType bq(IBClientBook, true);
    OrderQType oq(IBClientOrder, false);
    FillQType eq("trader1", true);

    BookQType::Reader* breader = bq.newReader();
    OrderQType::Writer* owriter = oq.newWriter(TRADER_ID);
    FillQType::Reader* ereader = eq.newReader();

    BookDepot myBook;
    FillEvent fe;
    interrupted = false;
    oid = 10;

    memset(books, 0, sizeof(books));
    eSecurity secid = getSymbolID(argv[3]);
    BarLine bar(BARSIZE);

    time_t cur_sec = ::time(NULL);
    char buf[256];
    int fd = 0;
    bool is_connected = false;
    buf[255] = 0;
    time_t cover_time = 0;
    bool cover_buy = false;

    time_t next_reconnect_sec = cur_sec -1;

    while (!interrupted) {
        // read book update
        if (breader->getNextUpdate(myBook)) {
            if (myBook.security_id == (uint16_t) secid) {
                books[(int)myBook.security_id] = myBook;
                bar.update(myBook);
            }
        }
        time_t this_sec = ::time(NULL);
        if (this_sec >  cur_sec) {
            int buf_len = sizeof(buf);
            if (bar.oneSecond(this_sec, true, buf, &buf_len)) {
                printf("%s", buf);
                if (is_connected) {
                    if (utils::tcp_send(fd, buf, buf_len) < 0)
                        is_connected = false;
                }
            }
            cur_sec = this_sec;
        }

        if (this_sec == cover_time) {
            if (cover_buy)
                makeOrder(secid, true, 100000, owriter);
            else
                makeOrder(secid, false, 100000, owriter);
            cover_time = 0;
        };

        if (ereader->getNextUpdate(fe)) {
            logInfo("Execution Event: %s", fe.toString().c_str());
        }

        if (!is_connected) {
            if (this_sec > next_reconnect_sec) {
                fd = utils::tcp_socket( argv[1], atoi(argv[2]), false);
                if (fd < 0) {
                    printf("cannot connect to: Host %s, Port %s, retry in 5 seconds\n", argv[1], argv[2]);
                    next_reconnect_sec = this_sec + 5;
                    continue;
                }
                printf("connected to Host %s Port %s\n", argv[1], argv[2]);
                const char* hello_msg = "modelID\n";
                if (utils::tcp_send(fd, hello_msg, strlen(hello_msg)) <= 0) {
                    printf("send error, retry in 5 seconds\n");
                    next_reconnect_sec = this_sec + 5;
                    continue;
                }
                is_connected = true;
            }
        }

        if (is_connected) {
            int recv_bytes = tcp_receive(fd, buf, sizeof(buf)-1);
            if (recv_bytes > 0) {
                buf[recv_bytes] = 0;
                logInfo("received %s", buf);
                if (strncmp(buf+11, "buy", 3) == 0) {
                    makeOrder(secid, true, 100000, owriter);
                    cover_time = ::time(NULL) + 300;
                    cover_buy = false;
                } else if (strncmp(buf+11, "sell", 4) == 0) {
                    makeOrder(secid, false, 100000, owriter);
                    cover_time = ::time(NULL) + 300;
                    cover_buy = true;
                }
            } else if (recv_bytes < 0) {
                printf("receive failed.\n");
                is_connected = false;
            }
        }
    }


    printf("Done.\n");
    return 0;
}
