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
    if (argc != 4) {
        printf("Usage: %s symbol [L2|L1|L1n] [full|tail]\n", argv[0]);
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
    if (argc>2 && strcmp(argv[3], "tail")==0) {
    	tail = true;
    }
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
    uint64_t last_micro = 0;
    while (!user_stopped) {
        book = reader.readNext();
        if (book) {
        	if (book->update_ts_micro - last_micro > 250000 || book->update_type == 2) {
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
