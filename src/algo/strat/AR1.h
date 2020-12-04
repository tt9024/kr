#pragma once

#include "AlgoBase.h"
#include <cmath>

namespace algo {

    class AR1 : public: AlgoBase {

        // create the algo in stopped state, config not read
        AR1(const std::string& name, const std::string& cfg, pm::FloorBase& floor);
        ~AR1();

        void onStop(uint64_t cur_micro) override; 
        void onStart(uint64_t cur_micro) override; 

        // read config, remove all subscriptions, add all subscriptions
        void onReload(uint64_t cur_micro, const std::string& config_file = "") override;

        // check to see what to do for algo
        void onOneSecond(uint64_t cur_micro) override;

        std::string onDump(uint64_t cur_micro) const override; 

    protected:
        struct AR1Param {
            explicit AR1Param(const std::string& cfg);
            AR1Param();
            std::vector<int> _A1Coef;
            int _barsec;
            time_t _start_utc;
            time_t _end_utc;
        }

        struct AR1State {
            md::BookDepot _bookL1;
            std::vector<int> _barHist;
            int _curPos;
        }

        struct AR1Forecast {
            double _logRet;
            double _variance;
        }

        // update state with new market data
        bool update();

        // perform forecast based on current state
        bool forecast(const AR1State& state, AR1Forecast& fcst);

        // position optimization code
        int pop(const AR1Forecast& forecast);

        std::string _cfg;
        AR1Param _param;
        AR1State _state;
        std::vector<time_t> _trigger_time;
        int _next_trigger_idx;
        int _symid;
    }
}
