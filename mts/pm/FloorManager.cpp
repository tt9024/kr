#include "FloorManager.h"
#include "RiskMonitor.h"
#include "time_util.h"
#include "md_bar.h"
#include "instance_limiter.h"
#include <stdexcept>
#include <atomic>

namespace pm {
    FloorManager& FloorManager::get() {
        static FloorManager mgr("FloorManager");
        return mgr;
    };

    FloorManager::FloorManager(const std::string& name)
    : FloorCPR<FloorManager>(name) {
    };

    FloorManager::~FloorManager() {
    };

    void FloorManager::start_derived() {
        utils::OnlyMe::get().add_name(m_name);
    };

    void FloorManager::shutdown_derived() {
    };

    std::string FloorManager::toString_derived() const {
        return "" ; 
    };

    void FloorManager::addPositionSubscriptions_derived() {
        std::set<int> type_set;
        // no set only get
        // TODO - allow set, to check for the existence and uniqueness
        // of algo+symbol+type
        //type_set.insert((int)FloorBase::SetPositionReq);
        type_set.insert((int)FloorBase::GetPositionReq);
        subscribeMsgType(type_set);
    };

    void FloorManager::run_loop_derived() {
        static int check_utc = 0;
        static const int check_interval = 5;

        utils::TimeUtil::micro_sleep(IdleSleepMicro);
        auto cur_utc = utils::TimeUtil::cur_utc();
        if (__builtin_expect(cur_utc>check_utc, 0)) {
            check_utc = cur_utc + check_interval;
            if (!utils::OnlyMe::get().check()) {
                logError("other %s detected, exiting!", m_name.c_str());
                stop();
            }
        }
        scanOpenOrders();
    }

    void FloorManager::handleExecutionReport_derived(const pm::ExecutionReport& er) {
        // extra handling after receiving an er
    };

    bool FloorManager::handleUserReq_derived(const MsgType& msg, std::string& respstr) {
        const char* cmd = msg.buf;
        logDebug("%s got user command: %s", m_name.c_str(), msg.buf);
        const std::string helpstr(
                    "Command Line Interface\n"
                    "P algo_name, symbol\n\tlist positions (and open orders) of the specified algo and symbol\n\thave to specify both algo and symbol, leave empty to include all entries\n"
                    "B|S algo_name, symbol, qty, price\n\tenter buy or sell with limit order with given qty/px (qty is positive)\n\tprice string [b|s][+|-][t|s]count, where \n\t\t[b|s]: reference to b (best bid price) or a (best ask price)\n\t\t[+|-]: specifies a delta of plus or minus\n\t\t[t|s]: specifies unit of delta, t (tick) or s (current spread)\n\t\tcount: number of units in delta\n"
                    "C [ClOrdId]\n\tcancel an order from tag 11, the client order id\n\tIf no ClOrdId is provided, cancel all open orders\n"
                    "R ClOrdId, qty, px\n\tqty positive, same sign with original order\n"
                    "X algo_name, symbol, qty [,px_str|twap_str]\n\tset target position by trading if necessary, with an optional\n\tlimit price string (see B|S order). If no price is specified,\n\ttrade aggressively using limit price of the other side\n\ttwap_str can be specified in place of a px_str, in format of Tn[s|m|h]\n\twhere n is a number, 's','m' or 'h' specifies unit of time.\n"
                    "A algo_name, symbol, target_position, target_vap\n\tset the position and vap to the given targets with synthetic fills\n"
                    "E \n\tinitiate the reconcile process, if good, persist currrent position to EoD file\n"
                    "D \n\tdump the state of Floor Manager\n"
                    "F \n\tdump the state of Floor Trader\n"
                    "K \n\tstop the floor message processing and done\n"
                    "M [symbol]\n\tget the snap and bars (with bar_sec for bar_cnt), empty symbol matches all symbols\n"
                    "Z algo, market_list [,ON|OFF]\n\tset the paused for algo and list of markets, a ':' delimitered string, to be ON or OFF if given, otherwise gets paused status\n\tempty algo or symbol matches all\n\tmarket_list doesn't specify contract, i.e. WTI:Brent"
                    // Below commands have message type
                    // "FloorBase::AlgoUserCommand"
                    // They are handled by AlgoThread
                    "@L \n\tlist loaded strategies\n"
                    "@strat_name S\n\tstart strat_name\n"
                    "@strat_name E\n\tstop strat_name, specify '*' for all strategies\n"
                    "@strat_name D\n\tdump pmarameters and state of strat_name\n"
                    "@state_name R config_file\n\tstop, reload with config_file and start\n"
                    "H\n\tlist of commands supported\n");

        // control messages
        // handled by CPR: 'K', 'A', 'Z', 'L'
        // User cmd of 'X' is converted to "PositionReq' by flr, so handled at
        // handlePositionReq_derived
        //
        switch (cmd[0]) {
            case 'H' : 
            {
                respstr = helpstr;
                break;
            };
            case 'P':
            {
                // get position or open order
                auto tk = utils::CSVUtil::read_line(cmd+1);
                if (tk.size()!=2) {
                    respstr = std::string("Failed to parse Algo or Symbol name: ")+ std::string(cmd)+ "\n"+ helpstr;
                } else {
                    respstr = getPM().toString(&tk[0], &tk[1], true);
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
                    m_eod_pending = true;
                    std::string errstr;
                    if (!requestReplay(pmr.getLoadUtc(), &errstr)) {
                        m_eod_pending = false;
                        respstr = "problem requesting replay: " + errstr;
                    }
                }
                break;
            }
            case 'B':
            case 'S':
            {
                // send a req and return (qty is positive)
                //"B|S algo_name, symbol, qty, price_str 
                logInfo("%s got user command: %s", m_name.c_str(), msg.buf);
                const char* bsstr = cmd;
                auto errstr = sendOrderByString(bsstr);
                if (errstr.size()>0) {
                    respstr = errstr;
                }
                break;
            }
            case 'C':
            case 'R':
            {
                // send a cancel/replace (qty is positive)
                // C|R ClOrdId [, qty, px] - qty positive, same sign
                logInfo("%s got user command: %s", m_name.c_str(), msg.buf);
                const char* cmdstr = cmd;
                auto errstr = sendCancelReplaceByString(cmdstr);
                if (errstr.size()>0) {
                    respstr = errstr;
                }
                break;
            }
            case 'M' :
            {
                //M symbol
                respstr = handleUserReqMD(cmd);
                break;
            }
            // handled by FloorTrader, no need to response here
            case 'F' :
            {
                return false;
            }
            default :
                respstr = "not supported (yet?)";
        }
        return true;
    }

    bool FloorManager::handlePositionReq_derived(const MsgType& msg, MsgType& msg_out) {
        // handles both GetPositionReq and SetPositionReq
        if (msg.type == FloorBase::GetPositionReq) {
            // expect a FloorBase::PositionRequest
            // the algo_name is allowed to be "ALL", but symbol has to be specified
            // returns another PositionRequest struct populated with
            // two int64_t as (aggregated) qty_done and qty_open in m_msgout.buf

            m_msgout.ref = msg.ref;
            m_msgout.type = FloorBase::GetPositionResp;
            m_msgout.copyData(msg.buf, msg.data_size);
            FloorBase::PositionRequest* prp = (FloorBase::PositionRequest*)m_msgout.buf;
            prp->qty_done = getPM().getPosition(prp->algo, prp->symbol, nullptr, nullptr, &(prp->qty_open));
            return true;
        }
        return false;
    }

    std::string FloorManager::handleUserReqMD(const char* cmd) {
        //M symbol
        std::string respstr;
        const auto& tk (utils::CSVUtil::read_line(cmd+1));
        std::vector<std::string> sym;
        if ((tk.size() == 0 ) || (tk[0] == "")) {
            sym = utils::SymbolMapReader::get().getPrimarySubscriptions(1);
        } else {
            sym.push_back(tk[0]);
        }
        // output the results
        char buf[1024*64];
        size_t bcnt = 0;
        bcnt = snprintf(buf, sizeof(buf), "%-16s  %-13s  %s\n"
               "---------------------------------------------------\n", 
               "symbol", "quote (mid)", "updated (sec ago)");
        std::map<std::string, int> stale_symbols;
        const int stale_secs = 60;
        for (const auto& s: sym) {
            md::BookDepot book;
            bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, "%-16s  ", s.c_str());
            bool ret = false;
            try {
                ret = md::LatestBook(s, "L1", book);
            } catch (const std::exception& e) {
            }
            if (ret) {
                double mid = book.getMid();
                int secs_ago = (int)((utils::TimeUtil::cur_micro() - book.update_ts_micro)/1000000ULL);
                bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, "%-13s  %d",
                        PriceCString(mid), secs_ago);
                if (secs_ago > stale_secs) {
                    bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, " (stale?) ");
                    stale_symbols[s] = secs_ago;
                }
            } else {
                bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, "%-13s  %s", "N/A", "N/A");
                stale_symbols[s] = -1;
            }
            bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, "\n");
        }
        bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, "===\nSummary: ");
        if (stale_symbols.size() == 0) {
            bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, "All Good!");
        } else {
            int sz = (int)stale_symbols.size();
            bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, "Found %d %s (updated %d seconds ago):\n", sz, sz>1?"warnings":"warning", stale_secs);
            for (const auto& ss : stale_symbols) {
                bcnt += snprintf(buf+bcnt, sizeof(buf)-bcnt, "\t%-16s (%s)\n",
                        ss.first.c_str(), 
                        (ss.second > 0? std::to_string(ss.second).c_str():"N/A"));
            }
        }
        buf[bcnt-1] = 0;
        respstr = std::string(buf);
        return respstr;
    }

    // open order scan, only scan PASSIVE or MARKET orders
    bool FloorManager::shouldScan(const std::shared_ptr<const OpenOrder>& oo, bool& peg_passive) const {
        peg_passive = false;
        // do not scan oo if qty is zero
        int64_t qty = oo->m_open_qty;
        if (!qty) {
            return false;
        }

        // do not scan manual order
        if (risk::Monitor::get().config().isManualStrategy(oo->m_idp->get_algo())) {
            return false;
        }

        // only scan PI type of
        // PASSIVE or MARKET
        // TWAP/VWAP/TRADER_WO orders are not scanned 
        // here by FM
        const auto& clOrdId (oo->m_clOrdId);
        auto iter (m_orderMap.find(clOrdId));
        if (iter != m_orderMap.end()) {
            const auto& pi = iter->second;
            const int64_t cur_micro = utils::TimeUtil::cur_micro();
            if (cur_micro - (int64_t)oo->m_open_micro > 100000LL) {
                if (pi->type == PositionInstruction::PASSIVE) {
                    peg_passive = true;
                    return true;
                }
                if (pi->type == PositionInstruction::MARKET) {
                    peg_passive = false;
                    return true;
                }
            }
        }
        return false;
    }

    bool FloorManager::scanOpenOrders() {
        static uint64_t last_scan_micro = 0;
        auto cur_micro = utils::TimeUtil::cur_micro();
        if ((int64_t) (cur_micro - last_scan_micro) < (int64_t) ScanIntervalMicro) {
            return false;
        }
        last_scan_micro = cur_micro;

        // trade out open orders with less 
        bool ret = false;
        const auto& vec(m_pm.listOO());
        for (const auto& oo: vec) {
            //logDebug("scanning oo: %s", oo->toString().c_str());
            bool peg_passive = false;
            if (!shouldScan(oo, peg_passive)) {
                continue;
            }
            double px_now = getPegPxBP(oo, peg_passive, MaxPegTickDiff, 0.5);
            double px_diff = oo->m_open_px - px_now; 
            if (std::abs(px_diff) > 1e-10) {
                ret = true;
                logDebug("Price moved away for %s, new px: %s, px-diff: %s, trading in", oo->toString().c_str(), PriceCString(px_now), PriceCString(px_diff));

                // additional check on the replacement
                logDebug("Price moved away detail: oo_price: %.8f(%s), px_now: %.8f(%s), px_diff: %.8f(%s)",
                        oo->m_open_px, PriceCString(oo->m_open_px), px_now, PriceCString(px_now), px_diff, PriceCString(px_diff));

                char tstr[256];
                snprintf(tstr, sizeof(tstr), "R %s,,%s", oo->m_clOrdId, PriceCString(px_now));
                std::string errstr = sendCancelReplaceByString(tstr);
                if (errstr != "") {
                    logError("failed to send replace order %s error = %s", 
                            tstr, errstr.c_str());
                    continue;
                }
                // In case the cancel is rejected, the fill 
                // should be in the way to be applied without oo
                // and orderMap entry, which is fine
                m_pm.deleteOO(oo->m_clOrdId);
                if (!peg_passive) {
                    // avoid burst and wait for rtt
                    last_scan_micro = last_scan_micro - ScanIntervalMicro + PegAggIntervalMilli*1000;
                };
                break;
            }
        }
        return ret;
    }
}
