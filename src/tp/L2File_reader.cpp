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
    if (argc < 2) {
        printf("Usage: %s symbol [tail]\n", argv[0]);
        printf("\nL2 subscriptions: ");
        std::vector<std::string> l2 = plcc_getStringArr("SubL2");
        for (auto s : l2) {
        	printf(" %s ", s.c_str());
        }
        printf("\n");
        return 0;
    }
    bool tail = false;
    if (argc>2 && strcmp(argv[2], "tail")==0) {
    	tail = true;
    }
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    utils::PLCC::instance("L2Reader");
    BookConfig bcfg(argv[1],"L2");
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
