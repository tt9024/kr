#include "FloorManager.h"
#include "RiskManager.h"
#include "time_util.h"
#include "md_snap.h"
#include <stdexcept>
#include <atomic>
namespace pm {

    FloorManager& FloorManager::get() {
        static FloorManager mgr("floor");
        return mgr;
    };

    FloorManager::~FloorManager() {};
    void FloorManager::start() {
        // process of startup
        // 1. create posisiotn manager and load eod
        // 2. create channel and start to process er
        // 3. send request for replay
        // 4. wait for done
        // 5. process recovery
        // 6. request open order
        // 7. Mark SoD load done
        // 8. wait for 2 seconds before accepting position requests
        // 9. enter the loop

        if (m_started) {
            logInfo("Floor Manager already started!");
            return;
        }
        m_started = true;
        m_loaded = false;
        m_should_run = true;
        m_eod_pending = false;
        m_loaded_time = 0;
        std::string errstr;
        while (m_should_run && (!requestReplay(m_pm.getLoadUtc(), &errstr))) {
            logError("problem requesting replay: %s, retrying in next 5 seconds", errstr.c_str());
            utils::TimeUtil::micro_sleep(5*1000000);
            continue;
        };
        while (m_should_run && (!requestOpenOrder(&errstr))) {
            logError("problem requesting open orders download: %s, retrying in next 5 seconds", errstr.c_str());
            utils::TimeUtil::micro_sleep(5*1000000);
            continue;
        }

        setInitialSubscriptions();
        while (m_should_run) {
            handleTimedPositionInstructions();
            bool has_message = this->run_one_loop(*this);
            if (! has_message) {
                // idle, don't spin
                // any other tasks could be performed here

                if (scanOpenOrders()) {
                    continue;
                }
                // check loaded time
                if ((!m_loaded) && (m_loaded_time > 0)) {
                    // wait for 2 second before subscribing position requests
                    if (time(nullptr) > m_loaded_time + 1 ) {
                        m_loaded = true;
                        addPositionSubscriptions();
                        logInfo("%s accepting position requests", m_name.c_str());
                    }
                }
                utils::TimeUtil::micro_sleep(1000);
            }
        }
        logInfo("Stop received, exit.");
        m_started = false;
        m_loaded = false;
    }

    FloorManager::FloorManager(const std::string& name)
    : FloorBase(name, true), 
      m_pm(m_name),
      m_started(false), m_loaded(false), m_should_run(false), m_eod_pending(false),
      m_loaded_time(0)
    {}

    void FloorManager::handleMessage(MsgType& msg_in) {
        switch (msg_in.type) {
        case ExecutionReport:
        {
            handleExecutionReport(msg_in);
            break;
        }
        case FloorBase::ExecutionReplayDone: 
        {
            if (!m_eod_pending) {
                // start up recovery
                m_pm.loadRecovery(m_recovery_file);
                logInfo("recovery done!");
                m_loaded_time = time(nullptr);
                clearAllOpenOrders();
            } else {
                std::string difflog;
                if (!m_pm.reconcile(m_recovery_file, difflog, false)) {
                    logError("%s failed to reconcile! \nrecovery_file: %s\ndiff:%s", 
                            m_name.c_str(), m_recovery_file.c_str(), difflog.c_str());
                } else {
                    m_pm.persist();
                    m_pm.resetPnl();
                    logInfo("%s EoD Done!", m_name.c_str());
                }
                m_eod_pending = false;
            }
            break;
        }
        case FloorBase::UserReq :
        {
            handleUserReq(msg_in);
            m_channel->update(m_msgout);
            break;
        }
        case FloorBase::GetPositionReq :
        case FloorBase::SetPositionReq :
        {
            handlePositionReq(msg_in);
            m_channel->update(m_msgout);
            break;
        }
        default:
            logError("%s received a unknown message: %s", 
                    m_name.c_str(), msg_in.toString().c_str());
            break;
        }
    }

    // helpers
    void FloorManager::setInitialSubscriptions() {
        std::set<int> type_set;
        type_set.insert((int)FloorBase::ExecutionReport);
        type_set.insert((int)FloorBase::UserReq);
        type_set.insert((int)FloorBase::ExecutionReplayDone);
        subscribeMsgType(type_set);
    }

    void FloorManager::addPositionSubscriptions() {
        std::set<int> type_set;
        type_set.insert((int)FloorBase::SetPositionReq);
        type_set.insert((int)FloorBase::GetPositionReq);
        subscribeMsgType(type_set);
    }

    void FloorManager::handleExecutionReport(const MsgType& msg) {
        const auto* er((pm::ExecutionReport*)(msg.buf));
        handleExecutionReport(*er);
    }

    void FloorManager::handleExecutionReport(const pm::ExecutionReport& er) {
        m_pm.update(er);
        // remove m_orderMap if clOrdId is no longer open in pm
        const std::string clOrdId (er.m_clOrdId);
        if (!m_pm.getOO(clOrdId)) {
            auto iter = m_orderMap.find(clOrdId);
            if (iter != m_orderMap.end()) {
                logDebug("removed pi %s (clOrdId=%s)from m_orderMap",
                        iter->second->toString().c_str(),
                        clOrdId.c_str());
                m_orderMap.erase(iter);
            }
        }
    }

    void FloorManager::handleUserReq(const MsgType& msg) {
        const char* cmd = msg.buf;
        logInfo("%s got user command: %s", m_name.c_str(), msg.buf);
        m_msgout.type = FloorBase::UserResp;
        m_msgout.ref = msg.ref;
        const std::string helpstr(
                    "Command Line Interface\n"
                    "P algo_name, symbol_name\n\tlist positions (and open orders) of the specified algo and sybmol\n\thave to specify both algo and symbol, leave empty to include all entries\n"
                    "B|S algo_name, symbol, qty, price\n\tenter buy or sell with limit order with given qty/px (qty is positive)\n\tprice string [b|s][+|-][t|s]count, where \n\t\t[b|s]: reference to b (best bid price) or a (best ask price)\n\t\t[+|-]: specifies a delta of plus or minus\n\t\t[t|s]: specifies unit of delta, t (tick) or s (current spread)\n\t\tcount: number of units in delta\n"
                    "C ClOrdId\n\tcancel an order from tag 11, the client order id\n"
                    "R ClOrdId, qty, px\n\tqty positive, same sign with original order\n"
                    "X algo_name, symbol_name, qty [,px_str|twap_str]\n\tset target position by trading if necessary, with an optional\n\tlimit price string (see B|S order). If no price is specified,\n\ttrade aggressively using limit price of the other side\n\ttwap_str can be specified in place of a px_str, in format of Tn[s|m|h]\n\twhere n is a number, 's','m' or 'h' specifies unit of time.\n"
                    "A algo_name, symbol_name, target_position, target_vap\n\tset the position and vap to the given targets with synthetic fills\n"
                    "L \n\treload the risk limit given by the risk.cfg\n"
                    "E \n\tinitiate the reconcile process, if good, persist currrent position to EoD file\n"
                    "D \n\tdump the state of Floor Manager\n"
                    "K \n\tstop the floor message processing and done\n"
                    "M symbol_name, bar_sec, bar_cnt\n\tget the snap and bars (with bar_sec for bar_cnt)\n"
                    // Below commands have message type
                    // "FloorBase::AlgoUserCommand"
                    // They are handled by AlgoThread
                    "@L \n\tlist loaded strategies\n"
                    "@strat_name S\n\tstart strat_name\n"
                    "@strat_name E\n\tstop strat_name\n"
                    "@strat_name D\n\tdump pmarameters and state of strat_name\n"
                    "@state_name R config_file\n\tstop, reload with config_file and start\n"
                    "H\n\tlist of commands supported\n");

        // control messages
        std::string respstr("Ack");
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
                    respstr = m_pm.toString(&tk[0], &tk[1], true);
                }
                break;
            }
            case 'K' : 
            {
                stop();
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
                const char* cmdstr = cmd;
                auto errstr = sendCancelReplaceByString(cmdstr);
                if (errstr.size()>0) {
                    respstr = errstr;
                }
                break;
            }
            case 'A':
            {
                //A position adjustment
                //algo, symbol, tgt_qty, tgt_vap

                const auto& tk(utils::CSVUtil::read_line(cmd+1));
                if (tk.size() != 4) {
                    std::string errstr = "Problem parsing a position adjustment: " + std::string(cmd) + "\nExecpted: algo, symbol, tgt_qty, tgt_vap";
                    logError("%s", errstr.c_str());
                    respstr = errstr;
                    break;
                }

                const std::string& algo (tk[0]);
                const std::string& symbol(tk[1]);
                int64_t tgt_qty = std::stoll(tk[2]), qty;
                double tgt_vap, px;
                if (!md::getPriceByStr(symbol, tk[3].c_str(), tgt_vap)) {
                    respstr = std::string("Cannot parse the price string ") + tk[3];
                    logError("%s", respstr.c_str());
                    break;
                }
                const auto& idp_arr (m_pm.listPosition(&algo, &symbol));
                if (idp_arr.size() != 1) {
                    logInfo("Adjusting a non-existing position - simply adding");
                    qty = tgt_qty;
                    px = tgt_vap;
                } else {
                    qty = idp_arr[0]->tgt_fill(tgt_qty, tgt_vap, &px);
                }
                if (!qty) {
                    logInfo("Position already meets target, nothing to do");
                    break;
                }
                try {
                    // generating a fill and add to the erpersist and update
                    const auto& er (ExecutionReport::genSyntheticFills(symbol, algo, qty, px, "PA"));
                    // append it to the er report
                    utils::CSVUtil::write_line_to_file(er.toCSVLine(), ExecutionReport::ERPersistFile(), true);
                    handleExecutionReport(er);
                } catch (std::exception& e) {
                    respstr = std::string("Failed to ajust position: ") + e.what();
                    logError("%s : %s", cmd, respstr.c_str());
                    break;
                }
                break;
            }
            case 'L' :
            {
                logInfo("reloading the risk manager's configuration");
                RiskManager::get(true);
                break;
            }
            case 'X' :
            {
                // in the format of X algo, symbol, qty [, px_str|twap_str]
                logDebug("set position command received: %s", cmd);
                try {
                    auto pi = std::make_shared<PositionInstruction>(cmd+1);
                    addPositionInstruction(pi);
                } catch (const std::exception& e) {
                    respstr = e.what();
                }
                break;
            }
            default :
                respstr = "not supported (yet?)";
        }
        m_msgout.copyString(respstr);

    }

    void FloorManager::handlePositionReq(const MsgType& msg) {
        // handles both GetPositionReq and SetPositionReq
        m_msgout.ref = msg.ref;
        if (msg.type == FloorBase::GetPositionReq) {
            // expect a FloorBase::PositionRequest
            // the algo_name is allowed to be "ALL", but symbol has to be specified
            // returns another PositionRequest struct populated with
            // two int64_t as (aggregated) qty_done and qty_open in m_msgout.buf

            m_msgout.type = FloorBase::GetPositionResp;
            m_msgout.copyData(msg.buf, msg.data_size);
            FloorBase::PositionRequest* prp = (FloorBase::PositionRequest*)m_msgout.buf;
            prp->qty_done = m_pm.getPosition(prp->algo, prp->symbol, nullptr, nullptr, &(prp->qty_open));
            return;
        }

        if(msg.type == FloorBase::SetPositionReq) {
            // expect a PositionInstruction
            // queue a instruction and ack the request
            m_msgout.type = FloorBase::SetPositionAck;
            auto pip = std::make_shared<PositionInstruction>(*((PositionInstruction*)msg.buf));
            addPositionInstruction(pip);
            m_msgout.copyString("Ack");
        }
    }

    void FloorManager::handlePositionInstructions(const std::vector<std::shared_ptr<PositionInstruction>>& pos_instr_vec) {
        for (const auto& pi : pos_instr_vec) {
            logInfo("Got instruction: %s", pi->toString().c_str());

            switch ( (PositionInstruction::TYPE) pi->type ) {
            case PositionInstruction::MARKET:
            case PositionInstruction::LIMIT:
            case PositionInstruction::PASSIVE:
            case PositionInstruction::TWAP:
                {
                    int64_t done_qty, open_qty, trade_qty;
                    if (pi->type == PositionInstruction::TWAP) {
                        // TWAP qty is already the trade qty
                        trade_qty = pi->qty;
                    } else {
                        // get trade qty based on target qty, i.e. pi->qty
                        done_qty = m_pm.getPosition(pi->algo, pi->symbol, nullptr, nullptr, &(open_qty));
                        trade_qty = pi->qty - (done_qty + open_qty);
                    }

                    // if need to trade
                    if (trade_qty != 0) {
                        // check if we have out contract to be traded out first
                        const auto & outin_vec(m_pm.listOutInPosition(pi->algo, pi->symbol));
                        for (const auto& pos : outin_vec) {
                            const auto& pi_tradable (utils::SymbolMapReader::get().getTradableSymbol(pi->symbol));
                            if (pi_tradable != pos->get_symbol()) {
                                // out contract
                                trade_qty = sendOrder_outContract(trade_qty, pi, *pos);
                                break;
                            }
                        }

                        // in contract
                        sendOrder_InContract(trade_qty, pi);
                    } else {
                        logInfo("Trade qty is zero: done: %lld, open: %lld, tgt_qty: %lld", 
                                (long long) done_qty, 
                                (long long) open_qty,
                                (long long) trade_qty);
                    }
                    break;
                }
            default:
                {
                    logError("unknown instruction type: %s", pi->toString().c_str());
                    break;
                }
            }
        }
    }

    std::vector<std::pair<time_t, long long>> FloorManager::getTWAPSlice(int64_t trade_qty, const std::string tradable_symbol, time_t target_utc, int lot_seconds) const {
        time_t cur_utc = utils::TimeUtil::cur_utc();
        int duration = (int)target_utc - (int)cur_utc;
        std::vector<std::pair<time_t, long long>> vec;

        // check the 1 minute qty, with minimum qty 10
        int lotspermin = utils::SymbolMapReader::get().getByTradable(tradable_symbol)->_lotspermin ;
        if (lot_seconds != 60) {
            lotspermin = int(((double)lotspermin / 60.0) * lot_seconds + 0.5);
        }
        if (lotspermin == 0 || trade_qty == 0) {
            logInfo("lotspermin (%d) or trade_qty(%lld) is zero, nothing to trade",
                    lotspermin, (long long) trade_qty);
            return vec;
        }

        int cnt = duration/lot_seconds;
        if (cnt < 0) {
            cnt = 0;
        }
        int cnt0 = std::abs(trade_qty)/lotspermin;
        if (cnt > cnt0) {
            cnt = cnt0;
        }
        int64_t qty0 = trade_qty;
        int64_t slice = qty0/(cnt+1) + qty0/std::abs(qty0);
        time_t enter_utc = cur_utc;
        while (qty0) {
            if (qty0 * (qty0-slice) < 0) {
                slice = qty0;
            }
            vec.push_back(std::make_pair<time_t, int64_t>(enter_utc+lot_seconds,(long long)slice));
            enter_utc += lot_seconds;
            qty0 -= slice;
        }
        return vec;
    }

    void FloorManager::addPositionInstruction(const std::shared_ptr<PositionInstruction>& pi, time_t cur_utc) {
        if (cur_utc == 0) {
            cur_utc = utils::TimeUtil::cur_utc();
        }
        // handle scheduled execution if needed
        switch ( (PositionInstruction::TYPE) pi->type ) {
            case PositionInstruction::INVALID: 
            {
                logError("ignore the invalid instruction type");
                break;
            }
            case PositionInstruction::TWAP:
            {
                // figure out the trade qty
                int64_t done_qty, open_qty, trade_qty;
                done_qty = m_pm.getPosition(pi->algo, pi->symbol, nullptr, nullptr, &(open_qty));
                trade_qty = pi->qty - (done_qty + open_qty);
                if (trade_qty == 0) {
                    logDebug("TWAP not added (position already done) %s", pi->toString().c_str());
                    break;
                }

                const auto& qty_vec (getTWAPSlice(trade_qty, pi->symbol, pi->target_utc));
                time_t enter_utc = utils::TimeUtil::cur_utc();
                for (const auto& tv: qty_vec) {
                    const auto& target_utc(tv.first);
                    const auto& slice (tv.second);
                    auto pi0 = std::make_shared<PositionInstruction>(*pi);
                    //pi0->type = PositionInstruction::TWAP;
                    pi0->qty = slice;
                    pi0->target_utc = target_utc;
                    m_timedInst[enter_utc].push_back(pi0);
                    logInfo("TWAP adding %s", pi0->toString().c_str());
                    enter_utc = target_utc;
                }
                break;
            }
            default : {
                m_timedInst[cur_utc].push_back(pi);
                break;
            }
        }
    }

    void FloorManager::handleTimedPositionInstructions(time_t cur_utc) {
        if (cur_utc==0) {
            cur_utc = utils::TimeUtil::cur_utc();
        }
        for (auto iter = m_timedInst.begin(); iter != m_timedInst.end();){
            auto due = iter->first;
            if (cur_utc < due) {
                break;
            }
            handlePositionInstructions(iter->second);
            iter = m_timedInst.erase(iter);
        }
    }

    std::string FloorManager::sendOrderByString(const char* bsstr, std::string* clOrdId) {
        // expect bssr to be in format of 
        // B|S algo, symbol, qty, px_str [,clOrdId]
        // if clOrdId is not in bsstr, it is generated
        // and assigned to *clOrdId if not null
        try {
            if ((*bsstr == 'B') || (*bsstr == 'S')) {
                // an order string: B|S algo,symbol,qty,px
                // parse and check the order before sending
                int side = ((*bsstr == 'B')?1:-1);

                const auto& tk(utils::CSVUtil::read_line(bsstr+1));
                std::string ordId;
                if (tk.size() == 4) {
                    ordId = ExecutionReport::genClOrdId();
                    if (clOrdId) {
                        *clOrdId = ordId;
                    }
                } else if (tk.size() == 5) {
                    ordId = tk[4];
                } else {
                    return std::string("Order string has to have 4 or 5 comma delimited fields: algo, symbol, qty, px_str [,clOrdId]. Not accepting the string: ") + std::string(bsstr);
                }

                // get the pi if there were one. We need to
                // do a book keeping here, as the order maybe
                // matched internally and therefore not being sent
                // so we take a copy here and reinsert if needed
                std::shared_ptr<PositionInstruction> pi;
                auto iter = m_orderMap.find(ordId);
                if (iter!=m_orderMap.end()) {
                    pi = iter->second;
                    m_orderMap.erase(iter);
                    logDebug("removing order map %s - %s", ordId.c_str(), pi->toString());
                }

                const std::string& algo(tk[0]);
                const std::string& symbol(utils::SymbolMapReader::get().getTradableSymbol(tk[1]));
                if (!symbol.size()) {
                    return std::string("problem sending order: symbol not found " + tk[1]);
                }

                int qty = std::stod(tk[2]);
                double px;
                if (! md::getPriceByStr(symbol, tk[3].c_str(), px) ) {
                    return std::string("problem parsing price string: ") + std::string(bsstr);
                }
                // format good, do risk check here
                if (!RiskManager::get().checkOrder(algo, symbol, qty * side, m_pm)) {
                    // errors logged in check
                    return std::string("Risk failed for request! ") + std::string(bsstr);
                }

                // match self trade if possible
                qty = matchOpenOrders(algo, symbol, qty*side, &px) * side;
                if (qty) {
                    // recreat the price string in caes needed
                    char ordstr[256];
                    size_t bytes = snprintf(ordstr, sizeof(ordstr), "%c %s, %s, %d, %s, %s", *bsstr, algo.c_str(), symbol.c_str(), qty, PriceCString(px),ordId.c_str());
                    FloorBase::MsgType req(FloorBase::SendOrderReq, ordstr, bytes + 1);
                    FloorBase::MsgType resp;
                    if (!m_channel->requestAndCheckAck(req, resp, 1, FloorBase::SendOrderAck)) {
                        return std::string("problem sending order: ") + std::string(resp.buf);
                    }

                    // save the map
                    if (pi) {
                        logDebug("Adding it back to orderMap at sendOrderByString %s: %s", ordId.c_str(), pi->toString().c_str());
                        m_orderMap[ordId] = pi;
                    }
                }
            } else {
                logError("Order string not starting from B or S: %s", bsstr);
                return "Order string not starting from B or S";
            }
        } catch (const std::exception& e) {
            logError("Exception when send order string %s: %s", bsstr, e.what());
            return std::string("Exception when send order string ") + std::string(bsstr) + " : " + std::string( e.what());
        }
        return "";
    }

    std::string FloorManager::sendCancelReplaceByString(const char* bsstr) {
        // bsstr is one of the following form:
        // C clOrdId 
        // R clOrdId, qty, px
        // where qty must be positive, same sign with orinial order
        // qty and px could be an empty string, which does not change
        //
        // Note however the 'qty' in the final string sending to tpmain 
        // is sign significant, see sendOrder() in CEngine.cpp
        try {
            if ((*bsstr == 'C') || (*bsstr == 'R')) {
                const auto& line = utils::CSVUtil::read_line(std::string(bsstr+1));
                if (((*bsstr == 'C') && (line.size()!= 1)) ||
                    ((*bsstr == 'R') && (line.size()!= 3))) {
                    logError("Error parsing the cancel/eplace order string %s!", bsstr);
                    return std::string("Error parsing cancel/replace ") + std::string(bsstr);
                }
                const auto& oo (m_pm.getOO(line[0]));
                if (!oo) {
                    logError("Cannot find clOrdId %s", line[0].c_str());
                    return std::string("clOrdId not found: ") + line[0];
                }
                const std::string& algo (oo->m_idp->get_algo());
                const std::string& symbol(oo->m_idp->get_symbol());
                int64_t qty = oo->m_open_qty; // sign significant
                double px = oo->m_open_px;

                char ordStr[256];
                size_t bytes = 0;
                if (*bsstr == 'C') {
                    bytes = snprintf(ordStr, sizeof(ordStr), "C %s, %s", line[0].c_str(), algo.c_str());
                } else {
                    if ((line[1] == "") && (line[2] == "")) {
                        logError("Both qty and px are emtpy, nothing to be replaced");
                        return "nothing to be replaced";
                    }
                    if (line[1] != "") {
                        // replace qty
                        qty = std::stoll(line[1]);
                        if (qty < 0) {
                            logError("Replace qty (%lld) must be positive, same side as the original order", (long long)qty);
                            return "replace qty must be positive";
                        }
                        // now make qty to be signed
                        qty *= (oo->m_open_qty>0?1:-1);
                    }
                    if (line[2] != "") {
                        // replace px, parse line[2] as px_str
                        if (!md::getPriceByStr(symbol, line[2].c_str(), px)) {
                            logError("Failed to parse the replacement price string %s", line[2].c_str());
                            return std::string("Failed to parse the replacement price string ") + line[2];
                        }
                    }

                    // if we have anything to be replaced
                    if (__builtin_expect((qty == oo->m_open_qty) && (std::abs(px - oo->m_open_px)<1e-10),0)) {
                        logError("Nothing to be replaced from %s with command %s!", oo->toString().c_str(), bsstr);
                        return "nothing to be replaced!";
                    }

                    if (!RiskManager::get().checkOrder(oo->m_idp->get_algo(), oo->m_idp->get_symbol(), qty, m_pm)) {
                        //errors logged in the function
                        return std::string("Risk failed for request! ") + std::string(bsstr);
                    }

                    const std::string replaceClOrdId = ExecutionReport::genReplaceClOrdId(line[0]);
                    bytes = snprintf(ordStr, sizeof(ordStr), "R %s, %lld, %s, %s, %s, %s", line[0].c_str(), (long long)qty, PriceCString(px), algo.c_str(), symbol.c_str(), replaceClOrdId.c_str());

                    // update m_orderMap if necessary
                    if (qty != 0) {
                        auto iter = m_orderMap.find(line[0]);
                        if (iter != m_orderMap.end()) {
                            m_orderMap[replaceClOrdId] = iter->second;
                            logInfo("Add pi %s to from clOrdId %s to the replaced clOrdId %s",
                                    iter->second->toString().c_str(),
                                    line[0].c_str(),
                                    replaceClOrdId.c_str());
                        }
                    }
                }
                FloorBase::MsgType req(FloorBase::SendOrderReq, ordStr, bytes+1);
                FloorBase::MsgType resp;
                if (!m_channel->requestAndCheckAck(req, resp, 1, FloorBase::SendOrderAck)) {
                    return std::string("problem sending order: ") + std::string(resp.buf);
                }
            } else {
                logError("CancelReplace not starting from C or R: %s", bsstr);
                return "CancelReplace not starting from C or R";
            }
        } catch (const std::exception& e) {
            logError("Exception when send cancel or replace string %s: %s", bsstr, e.what());
            return std::string("Exception when send cancel or replace string ") + std::string(bsstr) + " : " + std::string(e.what());
        }
        return "";
    }

    std::string FloorManager::sendOrder(const bool isBuy, 
            const char* algo, const char* symbol,
            int64_t qty, double px, std::string* clOrdId) {

        char buf[256];
        snprintf(buf, sizeof(buf), "%c %s,%s,%lld,%s%s",
                isBuy?'B':'S', algo, symbol,
                (long long)qty, PriceCString(px), 
                ((clOrdId&&(clOrdId->size()>0))?(std::string(",")+*clOrdId):std::string("")).c_str());
        return sendOrderByString(buf, clOrdId);
    }

    int64_t FloorManager::matchOpenOrders(const std::string& algo, const std::string& symbol, int64_t qty, double* px) {
        // match internal open orders and return the remaining qty after match
        // qty is sign significant, px is the limit price to be matched at
        // return qty remaining, also sign significant
        //
        // For each matched open order, it sends cancel and then
        // generates two synthetic fills on the matched amount.
        const auto& vec (m_pm.matchOO(symbol, qty, px));

        for (const auto& matched_pair : vec) {
            const auto& oo(matched_pair.first);
            const auto& qty0(matched_pair.second); // qty0 has opposite sign
            int64_t cur_qty = oo->m_open_qty;
            double cur_px = oo->m_open_px;

            logInfo("Matching OpenOrder %s. Total wanted: %lld, matched size: %lld", oo->toString().c_str(), (long long)qty, (long long)qty0);
            // cancel/replace it to cur_qty - qty0
            char tstr[128];
            snprintf(tstr, sizeof(tstr), "R %s,%lld,", oo->m_clOrdId, (long long)std::abs(cur_qty-qty0));
            std::string errstr = sendCancelReplaceByString(tstr);
            if (errstr != "") {
                logError("Failed to send replace order %s error: %s", 
                        tstr, errstr.c_str());
                continue;
            }
            // generates two fills and publish them
            const auto& er0 (ExecutionReport::genSyntheticFills(symbol, algo, -qty0, cur_px, "IM0"));
            const auto& er1 (ExecutionReport::genSyntheticFills(oo->m_idp->get_symbol(), oo->m_idp->get_algo(), qty0, cur_px, "IM1"));
            // append it to the er report
            utils::CSVUtil::write_line_to_file(er0.toCSVLine(), ExecutionReport::ERPersistFile(), true);
            utils::CSVUtil::write_line_to_file(er1.toCSVLine(), ExecutionReport::ERPersistFile(), true);
            
            // update position
            handleExecutionReport(er0);
            handleExecutionReport(er1);
            qty += qty0;
        }
        return qty;
    }

    void FloorManager::clearAllOpenOrders() {
        const auto& vec(m_pm.listOO());
        for (const auto& oo: vec) {
            uint64_t qty = oo->m_open_qty;
            if (!qty) {
                continue;
            }
            logInfo("Canceling open order %s", oo->toString().c_str());
            char tstr[128];
            snprintf(tstr, sizeof(tstr), "C %s", oo->m_clOrdId);
            std::string errstr = sendCancelReplaceByString(tstr);
            if (errstr != "") {
                logError("failed to send cancel %s error = %s", 
                        tstr, errstr.c_str());
            }
            m_pm.deleteOO(oo->m_clOrdId);
        }
    }

    bool FloorManager::shouldScan(const std::shared_ptr<const OpenOrder>& oo, bool& peg_passive) const {
        peg_passive = false;
        // do not scan oo if qty is zero
        int64_t qty = oo->m_open_qty;
        if (!qty) {
            return false;
        }

        // do not scan manual order
        if (RiskManager::isManualStrategy(oo->m_idp->get_algo())) {
            return false;
        }

        // if oo in the m_orderMap AND
        // TRADER_WO: do not scan
        // TWAP: peg passive until target_utc
        const auto& clOrdId (oo->m_clOrdId);
        auto iter (m_orderMap.find(clOrdId));
        if (iter != m_orderMap.end()) {
            const auto& pi = iter->second;
            if (pi->type == PositionInstruction::TWAP) {
                const auto cur_utc = utils::TimeUtil::cur_utc();
                if (cur_utc < pi->target_utc) {
                    peg_passive = true;
                }
            } else if ((pi->type == PositionInstruction::TRADER_WO) ||
                       (pi->type == PositionInstruction::LIMIT)) {
                // don't scan LIMIT or Trader_WO orders
                return false;
            }
        }
        return true;
    }

    double FloorManager::getPegPx(const std::shared_ptr<const OpenOrder>& oo, bool peg_passive) const {
        // check if the OO stays on a passive side.
        // if price moved away, then calculate a new price, depending on
        // peg_passive.  If true, then new price be the new passive side
        // otherwise, new price be the new aggressive side
        // return the new price

        double px = oo->m_open_px;
        int64_t qty = oo->m_open_qty;

        const std::string& symbol(oo->m_idp->get_symbol());
        double bidpx, askpx;
        int bidsz, asksz;
        if (!md::getBBO(symbol,  bidpx, bidsz, askpx, asksz)) {
            logInfo("Cannot get bbo for %s, Open Order not scanned %s", symbol.c_str(), oo->toString().c_str());
            return px;
        }

        // get the new passive and aggressve
        double px_passive = qty>0?bidpx:askpx, px_agg = qty>0?askpx:bidpx;
        if (qty*(px_passive-px)<1e-10) {
            // still there
            return px;
        }

        // additional debug
        logInfo("New BBO: %.8f, %.8f, %.8f, %.8f, %lld, %.8f, %s", bidpx, askpx, px_passive, px_agg, (long long)qty, px, peg_passive?"Pass":"Agg");
        return peg_passive?px_passive:px_agg;
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
            bool peg_passive = false;
            if (!shouldScan(oo, peg_passive)) {
                continue;
            }
            double px_now = getPegPx(oo, peg_passive);
            double px_diff = oo->m_open_px - px_now; 
            if (std::abs(px_diff) > 1e-10) {
                ret = true;
                logInfo("Price moved away for %s, new px: %s, px-diff: %s, trading in", oo->toString().c_str(), PriceCString(px_now), PriceCString(px_diff));

                // additional check on the replacement
                logDebug("Price moved away detail: oo_price: %.8f(%s), px_now: %.8f(%s), px_diff: %.8f(%s)",
                        oo->m_open_px, PriceCString(oo->m_open_px), px_now, PriceCString(px_now), px_diff, PriceCString(px_diff));

                char tstr[128];
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
            }
        }
        return ret;
    }

    bool FloorManager::requestReplay(const std::string& loadTime, std::string* errstr) {
        // Note loadTime is a string in *local* time
        // make start/stop time string in *readable* local time
        const std::string endTime = utils::TimeUtil::frac_UTC_to_string(0, 0);

        // compose request 
        m_recovery_file = loadTime+'_'+endTime+"_replay.csv";

#ifdef USE_TT_RECOVERY
        const std::string reqstr = loadTime + "," + m_pm.getRecoveryPath()+"/"+m_recovery_file;
        MsgType msgReq(FloorBase::ExecutionReplayReq, reqstr.c_str(), reqstr.size()+1);
        MsgType msgResp;
        if(!m_channel->requestAndCheckAck(msgReq, msgResp, 60,  FloorBase::ExecutionReplayAck) ) {
            if (errstr)
                *errstr = std::string(msgResp.buf);
            return false;
        }
        return true;
#else
        return loadRecoveryFromFill(loadTime, errstr);
#endif
    }

    bool FloorManager::loadRecoveryFromFill(const std::string& loadTime, std::string* errstr) {
        const std::string endTime = utils::TimeUtil::frac_UTC_to_string(0, 0);
        m_recovery_file = loadTime+'_'+endTime+"_replay.csv";

        if (! pm::ExecutionReport::loadFromPersistence(loadTime, endTime, m_pm.getRecoveryPath()+"/"+m_recovery_file)) {
            if (errstr)
                *errstr = "problem dumping fills to file " + m_pm.getRecoveryPath()+"/"+m_recovery_file + " from " + loadTime + " to " + endTime;
            return false;
        }
        // Recover file is ready, update event
        MsgType msg_in;
        msg_in.type = FloorBase::ExecutionReplayDone;
        handleMessage(msg_in);
        return true;
    }

    bool FloorManager::requestOpenOrder(std::string* errstr) {
        // this should be replayed
        const std::string reqstr = "ALL";
        MsgType msgReq(FloorBase::ExecutionOpenOrderReq, reqstr.c_str(), reqstr.size()+1);
        MsgType msgResp;
        if(!m_channel->requestAndCheckAck(msgReq, msgResp, 3,  FloorBase::ExecutionOpenOrderAck)) {
            if (errstr) {
                *errstr = std::string(msgResp.buf);
            }
            return false;
        }
        return true;
    }

    std::string FloorManager::toString() const {
        char buf[1024];
        size_t cnt = snprintf(buf, sizeof(buf),
                "FloorManager %s [running: %s, stopping: %s, loaded: %s, eod_pending: %s, recovery_file: %s, time_loaded: %s]\n"
                "Position Dump\n",
                m_name.c_str(), 
                m_started?"Y":"N", 
                (!m_should_run)?"Y":"N", 
                m_loaded?"Y":"N", 
                m_eod_pending?"Y":"N",
                m_recovery_file.c_str(), 
                utils::TimeUtil::frac_UTC_to_string(m_loaded_time,0).c_str());

        // dump the m_orderMap
        cnt += snprintf(buf+cnt, sizeof(buf)-cnt, "order map size: %d\n", (int)m_orderMap.size());
        for (const auto& kv:m_orderMap) {
            cnt += snprintf(buf+cnt, sizeof(buf)-cnt, "\t%s:%s\n", kv.first.c_str(),kv.second->toString().c_str());
        }
        return std::string(buf) + m_pm.toString();
    }

    void FloorManager::stop() {
        logInfo("FloorManager %s stop received", m_name.c_str());
        m_should_run = false;
    }

    int64_t FloorManager::sendOrder_outContract(int64_t trade_qty, const std::shared_ptr<PositionInstruction>& pi, const IntraDayPosition& pos) {
        int64_t qty = pos.getPosition(), oqty = pos.getOpenQty();
        qty += oqty;
        if (qty * trade_qty < 0) {
            // we could reduce out-contract position
            if (std::abs(qty) > std::abs(trade_qty)) {
                qty = -trade_qty;
            }
            auto pi_out = std::make_shared<PositionInstruction>(*pi);
            pi_out->type = PositionInstruction::PASSIVE;
            std::strcpy(pi_out->symbol, pos.get_symbol().c_str());
            if (sendOrder_InContract(-qty, pi_out)) {
                trade_qty += qty;
            }
        }
        return trade_qty;
    }

    bool FloorManager::sendOrder_InContract(int64_t trade_qty, const std::shared_ptr<PositionInstruction>& pi) {
        if (__builtin_expect(trade_qty==0, 0)) {
            return true;
        }

        // figure out the price
        const bool isBuy = (trade_qty > 0);
        double px = pi->px;
        if (pi->type == PositionInstruction::MARKET) {
            if (!md::getPriceByStr(pi->symbol, std::string(isBuy?"a+t10":"b-t10").c_str(), px)) {
                logError("Error getting market order price for %s, order of %s not sent", pi->symbol, pi->toString().c_str());
                return false;
            }
        } else if ((pi->type == PositionInstruction::PASSIVE) ||
                   (pi->type == PositionInstruction::TWAP)) {
            if (!md::getPriceByStr(pi->symbol, std::string(isBuy?"b+t0":"a+t0").c_str(), px)) {
                logError("Error getting market order price for %s, order of %s not sent", pi->symbol, pi->toString().c_str());
                return false;
            }
        }

        trade_qty = (isBuy? trade_qty : -trade_qty); // trade_qty is now positive
        std::string clOrdId = ExecutionReport::genClOrdId((int)pi->type, 0);

        // save the map
        logDebug("Adding to orderMap at sendOrder_InContract: %s: %s", clOrdId.c_str(), pi->toString().c_str());
        m_orderMap[clOrdId] = pi;

        const auto& errstr = sendOrder(isBuy, pi->algo, pi->symbol, trade_qty, px, &clOrdId);
        if ((errstr.size() > 0) || (clOrdId.size() == 0)) {
            logError("Error sending order: %s", errstr.c_str());
            return false;
        }
        return true;
    }
};
