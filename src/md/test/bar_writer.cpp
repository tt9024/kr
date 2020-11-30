#include "md_bar.h"
#include <csignal>
#include <stdio.h>
#include <atomic>

namespace
{
    md::BarWriterThread<utils::TimeUtil> bthread;
}

void signal_handler(int signal)
{
    bthread.stop();
    logInfo("Signal %d received, stopping bar writer", signal);
}

int main() {
    std::signal(SIGINT, signal_handler); 
    std::signal(SIGTERM, signal_handler);

    // TODO - create bconfig
    md::BookConfig bcfg("CNE", "CLF1", "L1");

    bthread.add(bcfg);
    bthread.start();

    return 0;
}

