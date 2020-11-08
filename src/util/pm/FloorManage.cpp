#include "floor.h"
#include <string>
#include "time_util.h"

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

        FloorManager(const std::string& name) 
        : m_name(name), m_started(false), m_loaded(false), m_should_run(false) {}

            // this loads up the position manager with eod 
            // and start the floor
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
        volatile bool m_started, m_loaded, m_should_run;
        std::string m_recovery_file;

        explicit FloorManager(const std::string& name)
        : m_name(name), 
          m_pm(m_name),
          m_channel(utils::Floor::get().getServer()),
          m_started(false), m_loaded(false), m_should_run(false) 
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
