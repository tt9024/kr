#include "floor.h"
#include <string>
#include "time_util.h"
#include "csv_util.h"
#include <map>

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
            m_eod_pending = false;
            while (!requestReplay(m_pm.getLoadUtc())) {
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
                bool has_message = run_one_looop();
                if (! has_message) {
                    // idle, don't spin
                    // any other tasks could be performed here
                    utils::TimeUtil::micro_sleep(1000);
                }
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
        volatile bool m_started, m_loaded, m_should_run, m_eod_pending;
        std::string m_recovery_file;
        MsgType m_msgout;

        explicit FloorManager(const std::string& name)
        : m_name(name), 
          m_pm(m_name),
          m_channel(utils::Floor::get().getServer()),
          m_started(false), m_loaded(false), m_should_run(false), m_eod_pending(false)
        {}

        FloorManager(const FloorManager& mgr) = delete;
        FloorManager& operator=(const FloorManager& mgr) = delete;

        bool run_one_loop() {
            MsgType msg;
            if (nextMessage(msg)) {
                handleMessage(msg);
                return true;
            }
            return false;
        }

        void handleMessage(MsgType& msg_in) {
            switch (msg_in.type) {
            case utils::Floor::ExecutionReport:
            {
                handleExecutionReport(msg_in);
                break;
            }
            case utils::Floor::ExecutionReplayDone: 
            {
                if (!m_eod_pending) {
                    // start up recovery
                    m_pm.loadRecovery(m_recovery_file);
                    fprintf(stderr, "recovery done!\n");
                    m_loaded = true;
                    addPositionSubscriptions();
                } else {
                    std::string difflog;
                    if (!m_pm.reconcile(m_recovery_file, difflog, false)) {
                        fprintf(stderr, "%s failed to reconcile! \nrecovery_file: %s\ndiff:%s\n", 
                                m_name.c_str(), m_recovery_file.c_str(), difflog.c_str());
                    } else {
                        m_pm.persist();
                        fprintf(stderr, "%s EoD Done!\n", m_name.c_str());
                    }
                    m_eod_pending = false;
                }
                break
            }
            case utils::Floor::UserReq :
            {
                handleUserReq(msg_in);
                m_channel.update(m_msgout);
                break;
            }
            case utils::Floor::GetPositionReq :
            case utils::Floor::SetPositionReq :
            {
                handlePositionReq(msg_in);
                m_channel.update(m_msgout);
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
            const char* cmd = msg.buf;
            fprintf(stderr, "%s got user command: %s\n", m_name.c_str(), msg.buf);
            m_msgout.type = utils::Floor::UserResp;
            m_msgout.ref = msg.ref;
            const std::string helpstr(
                        "Command Line Interface\n"
                        "P algo=algo_name,symbol=symbol_name\n\tlist positions (and open orders) of the specified algo and sybmol\n\thave to specify both algo and symbol, ALL is reserved for dumping all entries\n"
                        "B|S instruction\n\tenter buy or sell with instruction string\n"
                        "!A position_line\n\tadjust the position and pnl using the given csv line\n"
                        "!R limit_line\n\tset limit according to the given csv line\n"
                        "!E \n\tinitiate the reconcile process, if good, persist currrent position to EoD file\n"
                        "!D \n\t dump the state of Floor Manager\n"
                        "!!K \n\tstop the message processing and done\n"
                        "H\n\tlist of commands supported\n");

            switch (cmd[0]) {
                case 'H' : 
                {
                    m_msgout.copyString(helpstr);
                    return;
                };

                case 'P':
                {
                    // get position or open order
                    std::map<std::string, std::string> key_map;
                    if (! parseKeyValue(cmd+1, key_map)) {
                        m_msgout.reserve(helpstr.size()+256);
                        m_msgout.data_size = snprintf(m_msgout.buf, m_msgout.buf_capacity, 
                                "Failed to parse Algo or Symbol name: %s\n%s\n", cmd+1, helpstr.c_str()) + 1
                        break;
                    }
                    m_msgout.copyString( pm.toString(&key_map["algo"], &key_map["symbol"]) );
                    break;
                }

                case '!' :
                {
                    // control messages
                    std::string respstr("OK!");
                    switch(cmd[1]) {
                        case '!' : 
                        {
                            if (cmd[2] == 'K') {
                                m_should_run = false;
                            } else {
                                respstr = "Unknow command: " + std::string(cmd, 3) + "\n";
                            }
                            break;
                        }

                        case 'D' :
                        {
                            respstr = toString();
                            break;
                        }
                        case 'E' :
                        {
                            if (m_eod_pending) {
                                respstr = "Already in EoD\n";
                            } else {
                                PositionManager pmr("reconcile");
                                if (!requestReplay(pmr.getLoadUtc())) {
                                    respstr = "problem requesting replay\n";
                                } else {
                                    m_eod_pending = true;
                                }
                            }
                            break;
                        }
                        default :
                            respstr = "not supported (yet)";
                    }
                    m_msgout.copyString(respstr);
                };
                default :
                    m_msgout.copyString("not supported (yet)");
            }
            m_channel.update(m_msgout);
        }


        bool parseKeyValue (const char* cmd, std::map<std::string, std::string>& key_map) const {
            // parse key=val,... replace ALL to empty string
            try {
                key_map.clear();
                auto tokens = utils::CSVUtil::read_line(std::string(cmd));
                for (const auto& tk: tokens) {
                    auto fields = utils::CSVUtil::read_line(tk, delimiter='=');
                    key_map[fields[0]] = ((fields[1]=="ALL") ? "" : fields[1]);
                }
            } catch (const exception& e) {
                fprintf(stderr, "problem parsing user request: %s\n%s\n", cmd, e.what());
                return false;
            }
            return true;
        }

        void handlePositionReq(const MsgType msg) {
            m_msgout.ref = msg.ref;
            std::map<std::string, std::string> key_map;
            if (!parseKeyValue(msg.buf, key_map)) {
                m_msgout.copyString("Parse Error!");
                m_msgout.type = (utils::Floor::EventType) ((int)msg.type + 1);
                return;
            }

            if (msg.type == utils::Floor::GetPositionReq) {
                // expect a string in format of algo=algo_name,symbol=symbol_name
                // the algo_name is allowed to be "ALL", but symbol has to be specified
                // returns two int64_t as qty and oqty.  
                // If algo is "ALL", aggreated qty and oqtya
                m_msgout.type = utils::Floor::GetPositionResp;
                int64_t qty[2];
                memset(qty, 0, sizeof(qty));
                qty[0]=m_pm.getPosition(key_map["algo"], key_map["symbol"], nullptr, nullptr, &(qty[1]));
                m_msgout.copyData((char*)qty, sizeof(qty));
                return;
            }

            if(msg.type == utils::Floor::SetPositionReq) {
                // expect a string in format of algo=algo_name, symbol=symbol_name, pos=+/-number
                m_msgout.type = utils::Floor::SetPositionAck;
                if (key_map.find("algo") == key_map.end() ||
                    key_map.find("symbol") == key_map.end() ||
                    key_map.find("pos") == key_map.end()) {
                    fprintf(stderr, "Setposition parse error: %s\n", msg.buf);
                    m_msgout.copyString(std::string("SetPosition parse error: ") + msg.buf);
                    return ;
                }
                // TODO - add the trader code here
                m_msgout.copyString("OK");
            }
        }

        bool requestReplay(const std::string& loadUtc) {
            m_recovery_file = loadUtc+"_"+utils::TimeUtil::frac_UTC_to_String(0,3)+"_replay.csv";
            std::string reqstr = loadUtc + "," + m_pm.getRecoveryPath()+"/"+m_recovery_file;
            MsgType msgReq(utils::Floor::ExecutionReplayReq, reqstr.c_str(), reqstr.size()+1);
            return sendAndCheckAck(msgReq, m_msgout, 3,  utils::Floor::ExecutionReplayAck);
        }

        bool sendAndCheckAck(const MsgType& msgreq, MsgType& msgresp, int timeout_sec, utils::Floor::EventType type) {
            if (! m_channel.request(msgreq, msgresp, timeout_sec)) {
                fprintf(stderr, "Error sending request: %s\n", msgreq.toString());
                return false;
            }
            if ( (msgresp.type != type) || (strncmp(msgresp.buf, "Ack")!=0)) {
                fprintf(stderr, "Received message without Ack. type=%d, msg=%s\n", 
                        (int) type, msgresp.buf);
                return false;
            }
            return true;
        }
 
        bool  requestOpenOrder() {
            // this should be replayed
            MsgType msgReq(utils::Floor::ExecutionOpenOrderReq, reqstr.c_str(), reqstr.size()+1);
            return sendAndCheckAck(msgReq, m_msgout, 3,  utils::Floor::ExecutionReplayAck);
        }
    }
}


int main(int argc, char** argv) {
    pm::FloorManager fmgr;
    fmgr.start();
    fprintf(stderr, "Floor Manager Existed!\n");
    return 0;
}
