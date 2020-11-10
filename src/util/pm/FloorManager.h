#pragma once

#include "FloorBase.h"
#include "PositionManager.h"

namespace pm {

    class FloorManager: public FloorBase {
        // the FloorManager act as an interface between algo/engine to/from pm and traders
        // It interacts with the following components
        // * TP(Engine) :
        //   - From: gets the execution report
        //   - To  : sends the order 
        // * Algo :
        //   - From: gets the position requests (get + set)
        //   - To  : respond to the position requests
        // * User :
        //   - From: gets the user command (queries + controls)
        //   - To:   response to user command

    public:
        static FloorManager& get();
        ~FloorManager();
        void start();

    protected:
        PositionManager m_pm;
        volatile bool m_started, m_loaded, m_should_run, m_eod_pending;
        time_t m_loaded_time;
        std::string m_recovery_file;

        explicit FloorManager(const std::string& name);
        FloorManager(const FloorManager& mgr) = delete;
        FloorManager& operator=(const FloorManager& mgr) = delete;

        bool run_one_loop();
        void handleMessage(MsgType& msg_in);

        // helpers
        void setInitialSubscriptions();
        void addPositionSubscriptions();
        void handleExecutionReport(const MsgType& msg);
        void handleUserReq(const MsgType& msg);
        void handlePositionReq(const MsgType msg);
        bool requestReplay(const std::string& loadUtc);
        bool requestOpenOrder();

        std::string toString() const;
    };
};
