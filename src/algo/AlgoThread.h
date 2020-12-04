#pragma once

#include "AlgoBase.h"
#include "FloorBase.h"
#include "time_util.h"

namespace algo {

    class AlgoThread :public pm::FloorBase {
    public:

        // create algos from the given config, add to algo map
        AlgoThread(const std::string& inst, const std::string& cfg);

        // call onReload to force read of config
        void reload(const std::string& alg); 
        void reload(const std::string& alg, const std::string& cfg);
        void reloadAll();

        // call algo's onStop(), set should_run false
        // so it won't run onOneSecond()
        void stop( const std::string& alg);
        void stopAll();

        // set should_run true, call algo's onStart()
        void start( const std::string& alg);
        void startAll();

        // the main thread
        // 1. check user command and process
        // 2. check onOneSecond()
        void run();

        // to be called by run_one_loop
        void handleMessage(const MsgType& msg_in);

        using TimerType = utils::TimeUtil;

    private:
        const std::string m_inst;
        std::map<std::string, std::shared_ptr<AlgoBase> > m_algo_map;
        pm::FloorBase m_floor;
        volatile bool m_should_run;

        // create algo and add to the map
        void addAlgo(const std::string& name, 
                     const std::string& class_name, 
                     const std::string& cfg);
        void setSubscriptions();
    };

}
