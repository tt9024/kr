#include "AlgoThread.h"
#include "plcc/PLCC.hpp"
#include "strat/AR1.h"

namespace algo {
    AlgoThread::AlgoThread(const std::string& inst, const std::string& cfg)
    : FloorBase(inst, true), m_inst(inst), m_floor(inst+"_client", false), m_should_run(false) {
        auto vc = utils::ConfigureReader(cfg.c_str());
        auto al = vc.getStringArr("RunList");
        for (const auto& a : al) {
            auto ac = vc.getString(a);
            addAlgo(a, ac);
        }
        logInfo("AlgoThread %s created!", inst.c_str());
    }

    void AlgoThread::reload(const std::string& algo, const std::string& cfg) {
        logInfo("Reloading algo %s %s", algo.c_str(), ((cfg.size()==0)?"":(std::string("(cfg:")+cfg+")").c_str()));
        m_algo_map[algo]->onReload(TimerType::cur_micro(), cfg);
    }

    void AlgoThread::reload(const std::string& algo ) {
        reload(algo, "");
    }

    void AlgoThread::reloadAll() {
        for (auto& am : m_algo_map) {
            reload(am.first);
        }
    }

    void AlgoThread::stop(const std::string& algo) {
        logInfo("Stopping algo %s", algo.c_str());
        m_algo_map[algo]->onStop(TimerType::cur_micro());
        m_algo_map[algo]->setShouldRun(false);
    }

    void AlgoThread::stopAll() {
        for (auto& am : m_algo_map) {
            stop(am.first);
        }
    }

    void AlgoThread::start(const std::string& algo) {
        logInfo("Startng algo %s", algo.c_str());
        m_algo_map[algo]->setShouldRun(true);
        m_algo_map[algo]->onStart(TimerType::cur_micro());
    }

    void AlgoThread::startAll() {
        for (auto& am : m_algo_map) {
            start(am.first);
        }
    }

    void AlgoThread::run() {
        logInfo("AlgoThread %s is running", m_name.c_str());
        m_should_run = true;
        setSubscriptions();
        const int max_sleep_micro = 50000;
        uint64_t cur_micro = TimerType::cur_micro();
        uint64_t next_micro = (cur_micro / 1000000 + 2)*1000000;
        while (m_should_run) {
            while( this->run_one_loop(*this) ) {
            };
            // call onOneSecond 
            cur_micro = TimerType::cur_micro();
            int64_t diff_time = next_micro - cur_micro;
            if (diff_time < 500) {
                while (diff_time > 10) {
                    cur_micro = TimerType::cur_micro();
                    diff_time = next_micro - cur_micro;
                }
                for (auto& am : m_algo_map) {
                    auto& ap = am.second;
                    if (ap->shouldRun()) {
                        ap->onOneSecond(cur_micro);
                    }
                }
                next_micro += 1000000ULL;
            } else {
                // sleep upto max_sleep_micro
                if (diff_time > max_sleep_micro) {
                    diff_time = max_sleep_micro;
                }
                TimeType::micro_sleep(diff_time);
            }
        }
    }

    void AlgoThread::handleMessage(const MsgType& msg_in) {
        // the algo command is expected to be
        // 'strat_name S' : start 
        // 'strat_name R config_file' : reload
        // 'strat_name E' : stop 
        // 'strat_name D' : dump

        switch (msg_in.type) {
            case FloorBase::AlgoUserCommand :
            {
                std::string respstr("Ack");
                const char* cmd = msg_in.buf;
                const auto tk = utils::CSVUtil::read_line(cmd, delimiter=' ');
                const auto& sn(tk[0]);
                const auto& c(tk[1]);

                if (m_algo_map.find(sn) == m_algo_map.end()) {
                    logError("Strategy %s not found!", sn.c_str());
                    respstr = "Strategy " + sn + " not found!";
                    break;
                } else {
                    switch (c[0]) {
                    case 'S':
                    {
                        logInfo("Starting strategy %s", sn.c_str());
                        start(sn);
                        break;
                    }
                    case 'E':
                    {
                        logInfo("Stopping strategy %s", sn.c_str());
                        stop(sn);
                        break;
                    }
                    case 'R':
                    {
                        const auto& cfg_file(tk[2]);
                        logInfo("Reload strategy %s with config %s", 
                                sn.c_str(), cfg_file.c_str());
                        reload(sn, cfg_file);
                        break;
                    }
                    case 'D':
                    {
                        auto dumpstr = m_algo_map[sn]->toString();
                        logInfo("Dump %s: %s", sn.c_str(), dumpstr.c_str());
                        respstr = dumpstr;
                        break;
                    }
                    default :
                        logError("Unknown strategy command: %s", cmd);
                        respstr = "Unknown strategy command: " + std::string(cmd);
                    }
                }
                m_msgout.type = FloorBase::AlgoUserCommandResp;
                m_msgout.ref = msg_in.ref;
                m_msgout.copyString(respstr);
                m_channel->update(m_msgout);
            }
            default :
                logError("Unknown message type received: %s", msg_in.toString().c_str());
        }
    }

    void AlgoThread::addAlgo(const std::string& name, const std::string& objname, const std::string& cfg) {
        std::shared_ptr<AlgoBase> algp;
        
        // create algo based on object name
        if (objname == "AR1") {
            algp = std::make_shared<AR1>(name, cfg, m_floor);
        } else {
            logError("Unknown object name (%s) when createing strategy %s", objname.c_str(), name.c_str());
        }
        m_algo_map.emplace(name, algp);
    }

    void AlgoThread::setSubscriptions() {
        std::set<int> type_set;
        type_set.insert((int)FloorBase::AlgoUserCommand);
        subscribeMsgType(type_set);
    }
}
