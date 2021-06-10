#pragma once

#include "FloorBase.h"
#include "PositionManager.h"
#include "ExecutionReport.h"
#include <unordered_map>

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
        std::map<time_t, std::vector<std::shared_ptr<PositionInstruction>>> m_timedInst;
        std::unordered_map<std::string, std::shared_ptr<PositionInstruction>> m_orderMap;

        explicit FloorManager(const std::string& name);
        FloorManager(const FloorManager& mgr) = delete;
        FloorManager& operator=(const FloorManager& mgr) = delete;

        // main handle function from run_one_loop()
        void handleMessage(MsgType& msg_in);

        // specific request handlers
        void setInitialSubscriptions();
        void addPositionSubscriptions();
        void handleExecutionReport(const MsgType& msg);
        void handleExecutionReport(const pm::ExecutionReport& er);
        void handleUserReq(const MsgType& msg);
        void handlePositionReq(const MsgType& msg);
        void handleTimedPositionInstructions(time_t cur_utc=0);
        void handlePositionInstructions(const std::vector<std::shared_ptr<PositionInstruction>>& pi_vec);

        // send order functions
        // if clOrdId is not nullptr but empty, then generate a clOrdId and assigns to it
        // if clOrdId is not nullptr and non-empty, then uses the given clOrdId
        std::string sendOrder(const bool isBuy, const char* algo, 
                              const char* symbol, int64_t qty, double px, 
                              std::string* clOrdId = nullptr);
        std::string sendOrderByString(const char* bsstr, std::string* clOrdId=nullptr);
        std::string sendCancelReplaceByString(const char* bsstr);

        // trader related handlers
        bool scanOpenOrders();
        void clearAllOpenOrders();
        int64_t matchOpenOrders(const std::string& algo, const std::string& symbol, int64_t qty, double* px=nullptr);

        // helpers to send requests
        bool requestReplay(const std::string& loadUtc, std::string* errstr = nullptr);
        bool requestOpenOrder(std::string* errstr=nullptr);

        // this allows for base to call handleMessage
        friend class FloorBase;

    private:
        enum {
            ScanIntervalMicro = 5000000, // 5 seconds
        };
        bool loadRecoveryFromFill(const std::string& loadUtc, std::string* errstr = nullptr);
        int64_t sendOrder_outContract(int64_t trade_qty, const std::shared_ptr<PositionInstruction>& pi, const IntraDayPosition& pos);
        bool sendOrder_InContract(int64_t trade_qty, const std::shared_ptr<PositionInstruction>& pi);
        void addPositionInstruction(const std::shared_ptr<PositionInstruction>& pos_inst, time_t cur_utc=0);
        double getPegPx(const std::shared_ptr<const OpenOrder>& oo, bool peg_passive) const;

        bool shouldScan(const std::shared_ptr<const OpenOrder>& oo, bool& peg_passive) const;
        std::vector<std::pair<time_t,long long>> getTWAPSlice(int64_t trade_qty, const std::string tradable_symbol, time_t target_utc, int lot_seconds = 60) const;

    };
};
