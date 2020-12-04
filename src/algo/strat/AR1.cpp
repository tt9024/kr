#include "AR1.h"

namespace algo {

    AR1::AR1 (const std::string& name, const std::string& cfg, pm::FloorBase& floor)
    : AlgoBase(name, floor), 
      _cfg(cfg),
      _param(_cfg),
      _next_trigger_idx(0)
    {
        logInfo("%s created with cfg %s", m_name.c_str(), _cfg.c_str());
    }

    AR1::~AR1() {
        logInfo("%s destructed", m_name.c_str());
    }


    void AR1::onReload(uint64_t cur_micro, const std::string& config_file) {
        // re-read the paramter
    }

    void AR1::onStart(uint64_t cur_micro) {
        // create the trigger time
        // subscribe the md
        // update state
    }

    void AR1::onStop(uint64_t cur_micro) {

    }

    void AR1::onOneSecond(uint64_t cur_micro) {
        // if trigger time
        // update the state
        // forecast
        // pop
        // setPosition
    }

    std::string AR1::onDump(uint64_t cur_micro) {
        // dump the state book/bar/curpos
        // dump the param coef/barsec/st/et/maxpos
    }

    bool AR1::update() {
        // update snap
        // update bar
    }

    bool AR1::forecsst(AR1Forecast& fcst ) {
        // mul coef
    }

    int AR1::pop(const AR1Forecast& fcst) {
    }
}
