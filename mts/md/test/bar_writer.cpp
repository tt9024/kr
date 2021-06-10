#include "md_bar.h"
#include <csignal>
#include <stdio.h>
#include <atomic>
#include "symbol_map.h"

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

    // pub to WTI_N1, SPX_N1
    const auto* ti (utils::SymbolMapReader::get().getByMtsSymbol("WTI_N1"));
    md::BookConfig bcfg(ti->_venue, ti->_tradable, "L1");

    const auto* tispx (utils::SymbolMapReader::get().getByMtsSymbol("SPX_N1"));
    md::BookConfig bcfgspx(tispx->_venue, tispx->_tradable, "L1");

    // TODO - create bconfig
    bthread.add(bcfg);
    bthread.add(bcfgspx);
    bthread.run();

    return 0;
}

