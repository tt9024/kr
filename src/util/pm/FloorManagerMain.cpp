#include "FloorManager.h"
#include <csignal>
#include <stdio.h>
#include <atomic>

namespace
{
    pm::FloorManager& fmgr = pm::FloorManager::get();
}

void signal_handler(int signal)
{
    fmgr.stop();
    std::atomic_signal_fence(std::memory_order_release);
    fprintf(stderr, "received signal %d, floor manager stop() called...\ndump_state\n%s\n", 
            signal, fmgr.toString().c_str());
}

int main(int argc, char**argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    fprintf(stderr, "starting at %s\n", utils::TimeUtil::frac_UTC_to_string(0,0).c_str());
    fmgr.start();
    fprintf(stderr, "exit at %s\n", utils::TimeUtil::frac_UTC_to_string(0,0).c_str());
    return 0;
}
