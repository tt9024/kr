#include "md_bar.h"
#include <csignal>
#include <stdio.h>
#include <atomic>

#include "time_util.h"

namespace
{
    volatile bool should_run;
}

void signal_handler(int signal)
{
    should_run = false;
    logInfo("Signal %d received, stopping bar read", signal);
}

void printBar(const BarPrice& bp, int barsec) {
    printf("Bar %d update@ %s - %s\n", barsec, 
            utils::TimeUtil::frac_UTC_to_string(0, 6).c_str(),
            bp.toCSVLine().c_str());
}

int main() {
    std::signal(SIGINT, signal_handler); 
    std::signal(SIGTERM, signal_handler);

    md::BookConfig bcfg("CNE", "CLF1", "L1");
    md::BookQ<utils::ShmCircularBuffer> bq(bcfg, false);
    auto& bw = bq.theWriter();

    should_run = true;

    //sleep for 10 second before start
    //to test the empty book cases
    utils::TimeUtil::micro_sleep(10*1000*1000);

    while (should_run) {
        // generate tick
        uint64_t cur_micro = utils::TimeUtil::cur_micro();
        bool is_bid = (cur_micro%2);
        double price = 40.0 + (double)(cur_micro%100.0)/100.0;
        double size = 1;

        bool is_trade = ((cur_micro%5) == 0);
        if (is_trade) {
            bw.updTrade(price, size);
        } else {
            bw.updBBO(price, size, is_bid, cur_micro);
        }

        // wait for a random time
        uint64_t sleep_micro = cur_micro%100 + 1;
        utils::TimeUtil::micro_sleep( sleep_micro*sleep_micro*100 );
    }

    return 0;
}

