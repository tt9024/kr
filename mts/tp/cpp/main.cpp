#include <iostream>
#include "CApplication.h"
#include <csignal>

#define MTS_VERSION_HASH "3.0 2021"
namespace
{
    Mts::Application::CApplication objApp(MTS_VERSION_HASH);
}

void signal_handler(int signal)
{
    objApp.stop();
    std::atomic_signal_fence(std::memory_order_release);
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    try {
        objApp.run(".");
    }
    catch(std::exception & e) {
        std::cout << "Exception thrown" << std::endl;
        std::cout << e.what() << std::endl;
    }

    return 0;
}

