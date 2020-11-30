#pragma once

#include "FloorBase.h"
#include <set>
#include <vector>

namespace algo {
    class AlgoBase {
    public:
        AlgoBase(const std::string& name, pm::FloorBase& floor);
        virtual ~AlgoBase();

        // functions for getting/setting positions
        bool getPosition(pm::FloorBase::PositionRequest& req);
        bool setPosition(pm::FloorBase::PositionInstruction& instruction);

        // functions for getting price
        bool getBarLine(BarPrice& bp);

        // functions called by AlgoThread
        virtual std::vector<uint64_t> getTriggerTimes(uint64_t cur_micro);
        virtual void onStop(uint64_t cur_micro);
        virtual void onStart(uint64_t cur_micro);
        virtual void onReload(uint64_t cur_micro, const std::string& config_file);
        virtual std::string onDump(uint64_t cur_micro);
        virtual onTrigger(uint64_t cur_micro);
        bool shouldRun() const { return m_should_run;};
        void setTriggers(const std::vector<uint64_t>& tigger_micro);


    protected:
        const std::string m_name;
        std::set<uint64_t> m_trigger_utc_micro;
        pm::FloorBase& m_floor;
        volatile bool m_should_run;
    }
}
