#include <bookL2.hpp>

#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>

typedef tp::L2DeltaWriter<utils::ShmCircularBuffer> L2Type;
volatile bool user_stopped = false;

void sig_handler(int signo)
{
  if (signo == SIGINT) {
    printf("Received SIGINT, exiting...\n");
  }
  user_stopped = true;
}

int main() {
	// read all the l2 symbols and write to bar files
    if ((signal(SIGINT, sig_handler) == SIG_ERR) ||
    	(signal(SIGTERM, sig_handler) == SIG_ERR) )
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    utils::PLCC::instance("tickrecL2");
    std::vector<std::string> symL2(plcc_getStringArr("SubL2"));
    if (symL2.size()<1) {
    	throw std::runtime_error("No L2 symbol in config");
    }
    // create BookConfig, Book Reader and Bar Writers
    std::vector<L2Type*> dws;
    for (const auto& sym : symL2 ) {
        tp::BookConfig bcfg(sym,"L2");
        L2Type* dw(new L2Type(bcfg));
        dws.push_back(dw);
    }

    // adding the L1 queues to write
    std::vector<std::string> symL1(plcc_getStringArr("SubL1"));
    for (const auto& sym : symL1 ) {
    	if (std::find(symL2.begin(), symL2.end(), sym) != symL2.end()) {
    		continue;
    	}
        tp::BookConfig bcfg(sym,"L1");
        L2Type* dw(new L2Type(bcfg));
        dws.push_back(dw);
    }

    // adding the L1 future back contracts
    std::vector<std::string> symL1n(plcc_getStringArr("SubL1n"));
    for (const auto& sym : symL1n) {
    	if (std::find(symL2.begin(), symL2.end(), sym) != symL2.end()) {
    		continue;
    	}
        tp::BookConfig bcfg(sym,"L1",true);
        L2Type* dw(new L2Type(bcfg));
        dws.push_back(dw);
    }

    //uint64_t start_tm = utils::TimeUtil::cur_time_micro();
    user_stopped = false;
	unsigned int runCnt = 0;
	unsigned int idleCnt = 0;
    while (!user_stopped) {
    	for (auto dw : dws) {
    		if (dw->update()) {
    			++runCnt;
    		} else {
    			++idleCnt;
    		}
    	}
    	if (idleCnt > 4 * dws.size()) {
    		if (runCnt == 0) {
    			usleep(1000);
    		}
    		idleCnt = 0;
    		runCnt /= 2;
    	}
    }
    for (auto dw : dws) {
    	delete dw;
    }
    printf("Done.\n");
    return 0;
}

