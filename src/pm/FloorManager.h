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
        void stop();
        std::string toString() const;

    protected:
        PositionManager m_pm;
        volatile bool m_started, m_loaded, m_should_run, m_eod_pending;
        time_t m_loaded_time;
        std::string m_recovery_file;
        std::vector<FloorBase::PositionInstruction> m_posInstr;

        explicit FloorManager(const std::string& name);
        FloorManager(const FloorManager& mgr) = delete;
        FloorManager& operator=(const FloorManager& mgr) = delete;

        // main handle function from run_one_loop()
        void handleMessage(MsgType& msg_in);

        // specific request handlers
        void setInitialSubscriptions();
        void addPositionSubscriptions();
        void handleExecutionReport(const MsgType& msg);
        void handleUserReq(const MsgType& msg);
        void handlePositionReq(const MsgType& msg);
        void handlePositionInstructions();
        std::string sendOrderByString(const char* bsstr, int size);
        std::string sendOrder(const bool isBuy, const char* algo, 
            const char* symbol, int64_t qty, double px);

        // helpers to send requests
        bool requestReplay(const std::string& loadUtc, std::string* errstr = nullptr);
        bool requestOpenOrder(std::string* errstr=nullptr);

        // this allows for base to call handleMessage
        friend class FloorBase;
    };
};
