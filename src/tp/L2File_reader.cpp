#include <bookL2.hpp>

#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

using namespace tp;
using namespace utils;
using namespace std;

volatile bool user_stopped = false;
void sig_handler(int signo)
{
  if (signo == SIGINT) {
    printf("Received SIGINT, exiting...\n");
  }
  user_stopped = true;
}

int main(int argc, char**argv) {
    if (argc < 4) {
        printf("Usage: %s symbol [L2|L1|L1n] [full|tail] [throttle_micro(250000)] [start_utc_second(-1)] [end_utc_second(0x7fffffff)]\n", argv[0]);
        printf("\nL2 subscriptions: ");
        std::vector<std::string> l2 = plcc_getStringArr("SubL2");
        for (auto s : l2) {
        	printf(" %s ", s.c_str());
        }
        printf("\nL1 subscriptions: ");
        std::vector<std::string> l1 = plcc_getStringArr("SubL1");
        for (auto s : l1) {
        	printf(" %s ", s.c_str());
        }
        printf("\nL1n subscriptions: ");
        std::vector<std::string> l1n = plcc_getStringArr("SubL1n");
        for (auto s : l1n) {
        	printf(" %s ", s.c_str());
        }
        printf("\n");
        return 0;
    }
    std::string bt;
    bool next_contract = false;
    if (strcmp(argv[2], "L2") == 0) {
    	bt = "L2";
    } else if (strcmp(argv[2], "L1") == 0) {
    	bt = "L1";
    } else if (strcmp(argv[2], "L1n") == 0) {
    	bt = "L1";
    	next_contract = true;
    }
    bool tail = false;
    if (argc>3 && strcmp(argv[3], "tail")==0) {
    	tail = true;
    }

    int64_t throttle_micro = 250000LL;
    if (argc>4) {
        throttle_micro = (int64_t)atoi(argv[4]);
    }

    int64_t start_utc = -1;
    if (argc>5) {
        start_utc = (int64_t)atoi(argv[5]);
    }

    int64_t end_utc = 0x7fffffff;
    if (argc>6) {
        end_utc = (int64_t)atoi(argv[6]);
    }
    end_utc*=1000000LL;

    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    utils::PLCC::instance("L2Reader");
    BookConfig bcfg(argv[1],bt,next_contract);
    L2DeltaReader reader(bcfg, tail);
    user_stopped = false;
    const BookDepot* book;
    int64_t last_micro = 0;
    while (!user_stopped) {
        book = reader.readNext();
        if (book) {
            if ((int64_t)book->update_ts_micro < start_utc) {
                continue;
            }
            if ((int64_t)book->update_ts_micro > end_utc) {
                break;
            }
        	if ((int64_t)book->update_ts_micro - last_micro > throttle_micro || book->update_type == 2) {
        		printf("%s\n", book->prettyPrint().c_str());
        		last_micro = book->update_ts_micro;
        	}
        } else {
        	usleep(1000);
        }
    }
    printf("Done.\n");
    return 0;
}
