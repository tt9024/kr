#include <md_snap.h>

#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

using namespace md;
using namespace utils;
using namespace std;

typedef BookQ<ShmCircularBuffer> BookQType;
volatile bool user_stopped = false;

void sig_handler(int signo)
{
  if (signo == SIGINT) {
    printf("Received SIGINT, exiting...\n");
  }
  user_stopped = true;
}

int main(int argc, char**argv) {
    if (argc < 3) {
        printf("Usage: %s venue/symbol L1|L2 -d(all ticks)\n", argv[0]);
        printf("\n");
        return 0;
    }
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    utils::PLCC::instance("booktap");
    BookConfig bcfg(argv[1], argv[2]);
    bool trade_only=false;
    bool dump_all = false;
    if (argc>3 && strcmp(argv[3], "-d")==0) {
        printf("setting dump all to true\n");
        dump_all=true;
    }
    BookQType bq(bcfg, true);
    auto book_reader = bq.newReader();
    BookDepot myBook;

    //uint64_t start_tm = utils::TimeUtil::cur_time_micro();
    user_stopped = false;
    while (!user_stopped) {
        if (dump_all) {
            if (book_reader->getNextUpdate(myBook)) {
                    printf("%s\n", myBook.prettyPrint().c_str());
            } else {
                usleep(1000);
            }
        } else {
            if (book_reader->getLatestUpdateAndAdvance(myBook))
            {
                if (!trade_only || myBook.update_type==2)
                printf("%s\n", myBook.prettyPrint().c_str());
            } else {
                usleep(100*1000);
            }
        }
    }
    printf("Done.\n");
    return 0;
}
