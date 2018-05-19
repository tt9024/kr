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
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    utils::PLCC::instance("tickrecord");
    std::vector<std::string> symL1(plcc_getStringArr("SubL1"));
    if (symL1.size()<1) {
    	throw std::runtime_error("No L1 symbol in config");
    }
    // create BookConfig, Book Reader and Bar Writers
    std::vector<BARType*> bws;
    int bsec = plcc_getInt("BarSec");
    long long next_bar_micro= ((long long)time(NULL)+1)*1000000LL;

    for (const auto& sym : symL1 ) {
        BookConfig bcfg(sym,"L1");
        BARType*bw(new BARType(bcfg,bsec));
        bws.push_back(bw);
    }
    BookDepot myBook;
    //uint64_t start_tm = utils::TimeUtil::cur_time_micro();
    user_stopped = false;
    long long fcnt=0;
    while (!user_stopped) {
    	sleepApproxBefore(next_bar_micro);
        while ((long long)utils::TimeUtil::cur_time_micro() < next_bar_micro) {
            // spin
        }
    	for (auto bw : bws) {
    		bw->update((time_t) (next_bar_micro/1000000LL));
    	}
    	next_bar_micro+=((long long)bsec*1000000LL);
    	long long i = fcnt;
    	for (; i<fcnt+(long long)bws.size(); ++i) {
    		if ((long long)utils::TimeUtil::cur_time_micro() < next_bar_micro - 10*1000LL)
    			bws[(i%(long long)bws.size())]->flush();
    		else
    			break;
    	}
    	fcnt=i;
    }
    for (auto bw : bws) {
    	delete bw;
    }
    printf("Done.\n");
    return 0;
}
