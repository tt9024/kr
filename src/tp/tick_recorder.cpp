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

typedef BarLine<ShmCircularBuffer> BARType;
volatile bool user_stopped = false;

void sig_handler(int signo)
{
  if (signo == SIGINT) {
    printf("Received SIGINT, exiting...\n");
  }
  user_stopped = true;
}

void sleepApproxBefore(long long next_bar_micro, long long spin_micro=5*1000LL) {
	long long cur= (long long) utils::TimeUtil::cur_time_micro();
	long long diff = next_bar_micro-cur;
	if (diff > spin_micro) {
		usleep(diff-spin_micro);
	}
}


int main() {
	// read all the l1 symbols and write to bar files
    if ((signal(SIGINT, sig_handler) == SIG_ERR) ||
    	(signal(SIGTERM, sig_handler) == SIG_ERR) )
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    utils::PLCC::instance("tickrec");
    std::vector<std::string> symL1(plcc_getStringArr("SubL1"));
    if (symL1.size()<1) {
    	throw std::runtime_error("No L1 symbol in config");
    }
    std::vector<std::string> symL1n(plcc_getStringArr("SubL1n"));
    // create BookConfig, Book Reader and Bar Writers
    std::vector<BARType*> bws;
    int bsec = plcc_getInt("BarSec");

    for (const auto& sym : symL1 ) {
        BookConfig bcfg(sym,"L1");
        BARType*bw(new BARType(bcfg,bsec));
        bws.push_back(bw);
    }
    // the future back contracts, if any
    for (const auto& sym : symL1n ) {
        BookConfig bcfg(sym,"L1", true);
        BARType*bw(new BARType(bcfg,bsec));
        bws.push_back(bw);
    }

    BookDepot myBook;
    //uint64_t start_tm = utils::TimeUtil::cur_time_micro();
    user_stopped = false;
    long long fcnt=0;
    const int64_t bar_micro = bsec * 1000000LL;
    const int64_t MAX_SLEEP_MICRO = 50000LL;

    int64_t cur_micro = utils::TimeUtil::cur_time_micro();
    int64_t next_bar = (cur_micro / bar_micro  + 1) * bar_micro;
    int flush_need = 0; // zero bar files need to be flushed
    while (!user_stopped) {
    	bool has_update = false;
    	for (auto bw : bws) {
            cur_micro = utils::TimeUtil::cur_time_micro();
            if (cur_micro >= next_bar) {
                has_update = true;
                break;
            }
    		has_update |= bw->update_continous(cur_micro);
    	}
        if (cur_micro >= next_bar) {
            for (auto bw2 : bws) {
                bw2->onBar(next_bar);
            }
            next_bar += bar_micro;
            flush_need = (int)bws.size();
        }

    	if (!has_update) {
            int64_t sleep_micro = (next_bar - cur_micro) - MAX_SLEEP_MICRO;
            if (sleep_micro > MAX_SLEEP_MICRO) {
                if (flush_need > 0) {
                    long long i = fcnt;
                    for (; flush_need>0 ; ++i, --flush_need) {
                        if (cur_micro >= next_bar) {
                            break;
                        }
                        bws[(i%(long long)bws.size())]->flush();
                        cur_micro = utils::TimeUtil::cur_time_micro();
                    }
                    fcnt=i;
                } else {
                    usleep(MAX_SLEEP_MICRO);
                }
            }
        }
    }
    for (auto bw : bws) {
    	delete bw;
    }
    printf("Done.\n");
    return 0;
}
