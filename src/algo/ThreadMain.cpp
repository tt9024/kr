#include "AlgoThread.h"
#include <memory>
#include <csignal>
#include "plcc/PLCC.hpp"

namespace {
    std::shared_ptr<algo::AlgoThread> g_thread;
}

void signal_handler(int signal)
{
    if (g_thread) {
        g_thread->stopAll();
        std::atomic_signal_fence(std::memory_order_release);
        logInfo("received signal %d, stop all!", signal);
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage %s instance cfg_file\n", argv[0]);
        return 1;
    }
    g_thread = std::make_shared<algo::AlgoThread>(argv[1], argv[2]);

    // run on the main thread
    g_thread->run();

    logInfo("%s done!", argv[1]);
    return 0;
}
