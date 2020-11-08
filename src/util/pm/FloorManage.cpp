#include "floor.h"
#include <string>
#include "time_util.h"
#include "csv_util.h"

namespace pm {

    class FloorManager {
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
        //
    public:

        static FloorManager& get() {
            static FloorManager mgr("floor");
            return mgr;
        }

        ~FloorManager() {};
        void start() {
            // process of startup
            // 1. create posisiotn manager and load eod
            // 2. create channel and start to process er
            // 3. send request for replay
            // 4. wait for done
            // 5. process recovery
            // 6. request open order
            // 7. wait for 5 second to allow processing pending er
            // 7. Mark SoD load done
            // 8. enter the loop
            //    1. er: update
            //    2. setPositionReq
            //       - send OrderReq
            //    3. getPositonReq
            //       - getPosition and reply
            //    4. User req
            //       - eod persis
            //         - send request for replay
            //         - wait for done
            //         - process recovery
            //         - reconcile
            //         - persist
            //       - dump state (pnl+position)
            //       - stop
            //       - manual trade
            //       - adjust position
            //       - set risk


            if (m_started) {
                fprintf(stderr, "Floor Manager already started!\n");
                return;
            }
            m_started = true;
            m_loaded = false;
            m_should_run = true;
            const std::string loadUtc(pmgr.getLoadUtc());
            m_recovery_file = loadUtc+"_"+utils::TimeUtil::frac_UTC_to_String(0,3)+"_replay.csv";
            while (!requestReply(loadUtc, recovery_file)) {
                fprintf(stderr, "problem requesting replay, retrying in next 5 seconds\n");
                utils::TimeUtil::micro_sleep(5*1000000);
                continue;
            };
            while (!requestOpenOrder(channel)) {
                fprintf(stderr, "problem requesting open orders download, retrying in next 5 seconds\n");
                utils::TimeUtil::micro_sleep(5*1000000);
                continue;
            }

            setInitialSubscriptions();
            while (m_should_run) {
                run_one_looop();
            }
            fprintf(stderr, "Stop received, exit.\n");
            m_started = false;
            m_loaded = false;
        }

    private:
        using MsgType = utils::Floor::Message;
        using ChannelType = utils::Floor::Channel;
        const std::string m_name;
        PositionManager m_pm;
        ChannelType m_channel;
        volatile bool m_started, m_loaded, m_should_run, m_in_reconcile;
        std::string m_recovery_file;

        explicit FloorManager(const std::string& name)
        : m_name(name), 
          m_pm(m_name),
          m_channel(utils::Floor::get().getServer()),
          m_started(false), m_loaded(false), m_should_run(false), m_in_reconcile(false)
        {}

        FloorManager(const FloorManager& mgr) = delete;
        FloorManager& operator=(const FloorManager& mgr) = delete;

        void run_one_loop() {
            MsgType msg;
            if (nextMessage(msg)) {
                handleMessage(msg);
            } else {
                // nothing received
                utils::TimeUtil::micro_sleep(1000);
            }
        }

        void handleMessage(MsgType& msg_in) {
            switch (msg_in.type) {
            case ExecutionReport:
            {
                handleExecutionReport(msg_in);
                break;
            }
            case ExecutionReplayDone: 
            {
                m_pm.loadRecovery(m_recovery_file);
                fprintf(stderr, "recovery done!\n");
                m_loaded = true;
                addPositionSubscriptions();
                break;
            }
            case UserReq :
            {
                MsgType msg_out;
                handleUserReq(msg_in, msg_out);
                m_channel.update(msg_out);
                break;
            }
            case GetPositionReq :
            case SetPositionReq :
            {
                MsgType msg_out;
                handlePositionReq(msg_in, msg_out);
                m_channel.update(msg_out);
                break;
            }
            default:
                fprintf(stderr, "%s received a unknown message: %s\n", 
                        m_name.c_str(), msg_in.toString().c_str());
                break;
            }
        }

        // helpers
        void setInitialSubscriptions() {
            std::set<EventType>& type_set;
            type_set.insert(utils::Floor::ExecutionReport);
            type_set.insert(utils::Floor::UserReq);
            type_set.insert(utils::Floor::ExecutionReplayDone);
            m_channel.addSubscription(type_set);
        }
        void addPositionSubscriptions() {
            type_set.insert(utils::Floor::SetPositionReq);
            type_set.insert(utils::Floor::GetPositionReq);
            m_channel.addSubscription(type_set);
        }

        void handleExecutionReport(const MsgType& msg) {
            m_pm.update(*static_cast<ExecutionReport*>(msg.buf));
        }

        void handleUserReq(const MsgType& msg, MsgType& msg_out) {
            //    User Request:
            //       - "stop"
            //         - stop processing 
            //       - "position"
            //         - output state position
            //       - "pnl"
            //         - output pnl 
            //       - "open"
            //         - output open orders
            //       - "trade" "instruction_string"
            //         - initiate manual trade
            //       - "adjust" "position_line"
            //         - resets the position with give line
            //       - "risk" "limit_line"
            //         - set max position limit
            //       - "eod"
            //         - send request for replay
            //         - wait for done
            //         - process recovery
            //         - reconcile
            //         - persist
            //       - "help"
            //         - output the help string

            const char* cmd = msg.buf;
            fprintf(stderr, "%s got user command: %s\n", m_name.c_str(), msg.buf);

            if (strncmp(cmd, "help", 4)==0) {
                char strbuf[256];
                size_t bytes = snprintf(strbuf, sizeof(strbuf), "Command Line Interface\n"
                        "P algo=algo_name,symbol=symbol_name\n\tlist positions of the specified algo and sybmol\n\thave to specify both algo and symbol, ALL is reserved for dumping all entries\n"
                        "O algo=algo_name,symbol=symbol_name\n\tlist positions of the specified algo and sybmol\n\thave to specify both algo and symbol, ALL is reserved for dumping all entries\n"
                        "B|S instruction\n\tenter buy or sell with instruction string\n"
                        "!A position_line\n\tadjust the position and pnl using the given csv line\n"
                        "!R limit_line\n\tset limit according to the given csv line\n"
                        "!E \n\tinitiate the reconcile process, if good, persist currrent position to EoD file\n"
                        "!K \n\tstop the message processing and done\n"
                        "H\n\tlist of commands supported\n");
                msg_out.type = utils::Floor::UserResp;
                msg_out.ref = msg_in.ref;
                msg_out.copyData(strbuf, bytes+1);
                return;
            }

            if (strncmp(cmd, "stop", 4)==0) {
                m_should_run = false;
                char strbuf[32];
                size_t bytes = snprintf(strbuf, sizeof(strbuf), "OK\n");
                msg_out.type = utils::Floor::UserResp;
                msg_out.ref = msg_in.ref;
                msg_out.copyData(strbuf, bytes+1);
                return;
            }

            if (strcmp(cmd, "dump", 4)==0) {
                std::string dumpstr = runDump(cmd+4);
                msg_out.type = utils::Floor::UserResp;
                msg_out.ref = msg_in.ref;
                msg_out.copyData(strbuf, bytes+1);
                return;
            }
        }

        std::string runDump(const char* cmd) {
            auto tokens = utils::CSVUtil::read_line(std::string(cmd));
            std::string algo, symbol;
            for (const auto& tk: tokens) {
                auto fields = utils::CSVUtil::read_line(tk, delimiter='=');
                if (fields[1] == "ALL"
                if (fields[0] == "algo") algo=fields[1];
                if (fields[0] == "symbol") symbol=fields[1];

            }
            return pm.toString(&algo, &symbol);
        }

        void handlePositionReq(const MsgType msg, MsgType msg_out);
        void handleExecutionReport(const MsgType& msg);

        void requestReplay(const std::string& start_sec, const std::string& recovery_file);
        void requestOpenOrder( const std::string& start_sec); // this should be replayed

    }
}


int main(int argc, char** argv) {
    pm::FloorManager fmgr;
    fmgr.start();
    fprintf(stderr, "Floor Manager Existed!\n");
    return 0;
}
