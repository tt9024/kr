#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <cstdio>

#include <sys/types.h>
#include <sys/stat.h>

#include "plcc/PLCC.hpp"
#include "csv_util.h"
#include "time_util.h"
#include "symbol_map.h"
#include "PositionManager.h"

#define EoDPositionCSVFile "eod_pos"
#define FillCSVFile "fills.csv"

namespace pm {
    PositionManager::PositionManager(const std::string& name, const std::string& recover_path) :
    m_name(name), m_recovery_path(recover_path.size()==0? plcc_getString("RecoveryPath"): recover_path),
    m_load_second(loadEoD()), m_last_micro(0)
    {}

    std::string PositionManager::getLoadUtc() const {
        // from YYYYMMDD-HH:MM:SS local
        // see persist()
        return m_load_second;
    }

    std::string PositionManager::daily_eod_csv() const {
        return m_recovery_path + "/" + EoDPositionCSVFile + "_" + utils::TimeUtil::curTradingDay1() + ".csv";
    }

    std::string PositionManager::eod_csv() const {
        return m_recovery_path + "/" + EoDPositionCSVFile + ".csv";
    }

    std::string PositionManager::fill_csv() const {
        // getting current (snap to previous) trading day 
        const auto utc_second  { (time_t) (utils::TimeUtil::cur_micro() / 1000000ULL) };
        const auto trading_day { utils::TimeUtil::tradingDay(utc_second, -6,0,17,0,0,1) };
        return m_recovery_path + "/" + trading_day+"_"+FillCSVFile;
    }

    std::string PositionManager::loadEoD() {
        // return the latest reconciled time string (in local time)
        // i.e. the latest time in the eod position file
        auto line_vec = utils::CSVUtil::read_file(eod_csv());
        size_t line_cnt = line_vec.size();
        if (line_cnt==0) {
            logInfo("EoD file empty: %s", eod_csv().c_str());

            // get the st_mtime of the eod file instead
            struct stat fileInfo;
            if (stat(eod_csv().c_str(), &fileInfo) != 0) {
                perror("failed to get file info of eod file, creating new!");
                return utils::TimeUtil::frac_UTC_to_string(0,0);
            } else {
                const std::string eod_time = utils::TimeUtil::frac_UTC_to_string(fileInfo.st_mtime,0);
                logInfo("getting modify time as last eod: %s", eod_time.c_str());
                return eod_time;
            }
        }

        std::string latest_day = line_vec[line_cnt-1][0];
        // this should be YYYYMMDD,HH:MM:SS (local time) the time when 
        // positions are reconciled and persisted.
        // Note - all position entries for each persist
        // use the same time stamp 
        //
        for (auto& token_vec: line_vec) {
            if (token_vec[0] != latest_day) {
                continue;
            }
            // remove the first column - the timestamp
            token_vec.erase(token_vec.begin());
            try {
                auto idp = std::make_shared<IntraDayPosition>(token_vec);
                if ( (!idp->hasPosition()) && (!idp->listOO().size())) {
                    logInfo("Empty idp not loaded: %s", idp->toString().c_str());
                    continue;
                }
                auto symbol = idp->get_symbol();
                auto algo = idp->get_algo();
                // add idp to the AlgoPosition
                if ( (m_algo_pos.find(algo)!=m_algo_pos.end()) &&
                     (m_algo_pos[algo].find(symbol)!=m_algo_pos[algo].end()) ) {
                    // we find a duplicated line, failed
                    throw std::runtime_error(std::string("PM load failed: duplicated position found: ") + idp->toString());
                }
                m_algo_pos[algo][symbol]=idp;
                m_symbol_pos[symbol][algo]=idp;
            } catch (const std::exception& e) {
                logError("Failed to read position: %s, please check position!", e.what());
            }
        };
        return latest_day;
    };

    void PositionManager::update(const ExecutionReport& er, bool persist_fill) {
        // check for duplicate fill
        // this could happen if the recovery overlaps with real-time
        // fills, or otherwise a previous fill is resent
        if (er.isFill()) {
            if ( haveThisFill(er) ) {
                logInfo("Warning! duplicated fill not updated: %s", er.toString().c_str());
                return ;
            }
        }

        // update the intra-day position
        auto idp = m_algo_pos[er.m_algo][er.m_symbol];
        if (!idp) {
            idp = std::make_shared<IntraDayPosition>(er.m_symbol, er.m_algo);
            m_algo_pos[er.m_algo][er.m_symbol] = idp;
            m_symbol_pos[er.m_symbol][er.m_algo] = idp;
        }
        idp->update(er);
        m_last_micro = er.m_recv_micro;

        // if it's a fill, need to persist
        if (persist_fill && er.isFill()) {
            utils::CSVUtil::write_line_to_file(er.ExecutionReport::toFillsCSVLine(), fill_csv(), true);
        }
        // update the clOrdId map
        const auto& oo(idp->findOO(er.m_clOrdId));
        if (oo) {
            m_oo_map[er.m_clOrdId] = oo;
        } else {
            m_oo_map.erase(er.m_clOrdId);
        }
    }

    bool PositionManager::reconcile(const std::string& recovery_file, std::string& diff_logs, bool adjust) {
        // this loads the latest positions from eod_position file
        // update with the execution reports from the given recovery_file
        // and compare the result with this position
        // return true if match, otherwise, mismatches noted in diff_logs
        // If adjust is set to true, this position becomes the recovery position.
        PositionManager pm ("reconcile", m_recovery_path);
        // remove the fill_csv() file
        std::remove(fill_csv().c_str());

        pm.loadRecovery(recovery_file, true);
        auto diff_log1 = diff(pm);
        auto diff_log2 =  pm.diff(*this);

        if ((diff_log1.size()) == 0 && (diff_log2.size()) == 0) {
            return true;
        }
        diff_logs = diff_log1 + "\n" + diff_log2;
        if (adjust) {
            logInfo("Warning, %s is going to copy from %s, all existing open order trackings are lost. Download open order again if needed!", m_name.c_str(), pm.m_name.c_str());
            *this=pm;
        }
        return false;
    }

    bool PositionManager::loadRecovery(const std::string& recovery_file, bool persist_fill) {
        const std::string fname = m_recovery_path + "/" + recovery_file;
        try {
            for(auto& line : utils::CSVUtil::read_file(fname)) {
                update(pm::ExecutionReport::fromCSVLine(line), persist_fill);
            }
        } catch (const std::exception& e) {
            logError("Failed to load recover.  fname: %s, error: %s\npm:%s", 
                    fname.c_str(), e.what(), toString().c_str());
            return false;
        }
        return true;
    }

    void PositionManager::resetPnl() {
        for (auto iter = m_algo_pos.begin();
             iter != m_algo_pos.end();
             ++iter) {
            for (auto iter2 = iter->second.begin();
                     iter2 != iter->second.end();
                     ++iter2) {
                iter2->second->resetPnl();
            }
        }
    }

    bool PositionManager::persist() const {
        // write each algo's intra-day position csv line
        // the time string in local time
        std::string eod_timestamp = utils::TimeUtil::frac_UTC_to_string(0,0);

        utils::CSVUtil::FileTokens line_vec;
        for (auto iter = m_algo_pos.begin();
             iter != m_algo_pos.end();
             ++iter) {
            for (auto iter2 = iter->second.begin();
                     iter2 != iter->second.end();
                     ++iter2) {
                auto token_vec = iter2->second->toCSVLine();
                token_vec.insert(token_vec.begin(), eod_timestamp);
                line_vec.push_back(token_vec);
            }
        };
        // try write a daily position file without append
        if (!utils::CSVUtil::write_file(line_vec, daily_eod_csv(), false)) {
            logError("Failed to write daily position file %s", daily_eod_csv().c_str());
        }
        return utils::CSVUtil::write_file(line_vec, eod_csv());
    }

    bool PositionManager::operator==(const PositionManager& pm) const {
        return (diff(pm)+pm.diff(*this)).size()==0;
    }

    std::string PositionManager::diff(const PositionManager& pm) const {
        std::string diff_str;
        if (pm.m_load_second != m_load_second) {
            // print a warning instead of failing it
            std::string warn_str = "load utc: " + m_load_second + "(" + m_name+") != " + pm.m_load_second + "("+pm.m_name+")";
            logInfo("Warning: %s\n", warn_str.c_str());
        }
        for (auto iter = m_algo_pos.begin();
             iter != m_algo_pos.end();
             ++iter) {
            for (auto iter2 = iter->second.begin();
                 iter2 != iter->second.end();
                 ++iter2) {

                const auto idpv = pm.listPosition(&iter->first, &iter2->first);
                if (idpv.size()==1) {
                    diff_str+=iter2->second->diff(*idpv[0]);
                } else {
                    // is this a zero position?
                    if (iter2->second->hasPosition()) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "%s miss %s\n", pm.getName().c_str(), iter2->second->toString().c_str());
                        diff_str+=std::string(buf);
                    }
                }
            }
        }
        return diff_str;
    }

    int64_t PositionManager::getPosition(const std::string& algo, const std::string& symbol, double* ptr_vap, double* pnl, int64_t* oqty) const {
        if (algo.size()==0) {
            return getPosition(symbol, ptr_vap, pnl, oqty);
        }

        int64_t qty = 0;
        if (oqty) {
            *oqty = 0;
        }
        const auto& pos = listOutInPosition(algo, symbol);
        if (pos.size()>0) {
            const IntraDayPosition* idp = pos[0].get();
            IntraDayPosition pd;
            // include N0 if it's a N1 contract
            if (pos.size() > 1) {
                logInfo("out-position detected for %s: [%s]",
                         symbol.c_str(), 
                         pos[0]->toString().c_str());
                pd = *idp;
                pd += (*pos[1]);
                idp = &pd;
            }

            qty = idp->getPosition(ptr_vap, pnl);
            if (oqty) {
                *oqty = idp->getOpenQty();
            }
        }
        return qty;
    }

    int64_t PositionManager::getPosition(const std::string& symbol, double* ptr_vap, double* pnl, int64_t* oqty) const {
        int64_t qty = 0;
        if (oqty) {
            *oqty = 0;
        }
        // get an aggregated position for the given symbol
        const auto pos = listPosition(nullptr, &symbol);
        if(pos.size()>0) {
            IntraDayPosition idp(*pos[0]);
            for (size_t i = 1; i<pos.size(); ++i) {
                idp += (*pos[i]);
            }
            qty = idp.getPosition(ptr_vap, pnl);
            if (oqty) {
                *oqty = idp.getOpenQty();
            }
        }
        return qty;
    }

    std::vector<std::shared_ptr<const IntraDayPosition> > PositionManager::listPosition(const std::string* algo, const std::string* symbol) const {
        std::vector<std::shared_ptr<const IntraDayPosition> > vec;
        if (algo && (*algo).size()) {
            // use the algo map
            const auto iter = m_algo_pos.find(*algo);
            if (iter != m_algo_pos.end()) {
                if (symbol && (*symbol).size()) {
                    // we have a tradable symbol 
                    const std::string& tradable_symbol = utils::SymbolMapReader::get().getTradableSymbol(*symbol);
                    const auto& iter2 = iter->second.find(tradable_symbol);
                    if (iter2 != iter->second.end()) {
                        vec.push_back(iter2->second);
                    }
                } else {
                    addAllMapVal(vec, iter->second.begin(), iter->second.end());
                }
            }
        } else {
            // use the symbol map
            if (symbol && (*symbol).size()) {
                const std::string& tradable_symbol = utils::SymbolMapReader::get().getTradableSymbol(*symbol);
                const auto& iter = m_symbol_pos.find(tradable_symbol);
                if (iter != m_symbol_pos.end()) {
                    addAllMapVal(vec, iter->second.begin(), iter->second.end());
                }
            } else {
                // add all 
                for (auto iter = m_symbol_pos.begin(); iter!= m_symbol_pos.end(); ++iter) {
                    addAllMapVal(vec, iter->second.begin(), iter->second.end());
                }
            }
        }
        return vec;
    }

    std::vector<std::shared_ptr<const IntraDayPosition> > PositionManager::listOutInPosition(const std::string& algo, const std::string& symbol) const {
        auto vec = listPosition(&algo, &symbol);

        // check N0 contract if this is a N1
        try {
            const std::string& tradable_symbol = utils::SymbolMapReader::get().getTradableSymbol(symbol);
            const auto* ti (utils::SymbolMapReader::get().getN0ByN1(tradable_symbol));
            if (ti) {
                // include N0 if exists to the beginning
                const auto& vec_out = listPosition(&algo, &ti->_tradable);
                logInfo("Got out-contract %s (%d out positions, %d in positions)", 
                        ti->toString().c_str(), (int)vec_out.size(), (int)vec.size());

                vec.insert(vec.begin(), vec_out.begin(), vec_out.end());
            }
        } catch (const std::exception& e) {
            logError("problem checking out contract for %s-%s: %s", algo.c_str(), symbol.c_str(), e.what());
        }
        return vec;
    }

    std::vector<std::shared_ptr<const OpenOrder> > PositionManager::listOO(const std::string* algo, const std::string* symbol, const bool* ptr_side) const {
        std::vector<std::shared_ptr<const OpenOrder> > vec_oo;
        const auto vec = listPosition(algo, symbol);
        for (const auto& idp: vec) {
            const auto oov = idp->listOO();
            for (const auto& oo:oov) {
                if (oo->m_open_qty==0) {
                    continue;
                }
                if (ptr_side) {
                    if ( ((*ptr_side) && (oo->m_open_qty < 0)) ||
                         (!(*ptr_side) && (oo->m_open_qty > 0))) 
                    {
                        continue;
                    }
                }
                vec_oo.push_back(oo);
            }
        }
        return vec_oo;
    }

    void PositionManager::deleteOO(const char* clOrdId) {
        if (clOrdId && *clOrdId) {
            auto iter = m_oo_map.find(clOrdId);
            if (iter!=m_oo_map.end()) {
                const std::string& algo(iter->second->m_idp->get_algo());
                const std::string& symbol(iter->second->m_idp->get_symbol());
                m_algo_pos[algo][symbol]->deleteOO(clOrdId);
                m_oo_map.erase(iter);
            }
        } else {
            // delete all open orders
            for (auto iter = m_algo_pos.begin();
                 iter != m_algo_pos.end();
                 ++iter) {
                for (auto iter2 = iter->second.begin();
                     iter2 != iter->second.end();
                     ++iter2) {
                    iter2->second->deleteOO(nullptr);
                }
            }
            m_oo_map.clear();
        }
    }

    double PositionManager::getPnl(const std::string* algo, 
                  const std::string* symbol) const {
        const auto vec = listPosition(algo, symbol);
        double pnl=0;
        for (const auto& idp: vec) {
            double pnl0;
            idp->getPosition(nullptr, &pnl0);
            pnl+=pnl0;
        };
        return pnl;
    }

    void PositionManager::operator=(const PositionManager& pm) {
        logInfo("%s gets all positions from %s", m_name.c_str(), pm.m_name.c_str());
        m_algo_pos = pm.m_algo_pos;
        m_symbol_pos = pm.m_symbol_pos;
        m_last_micro = pm.m_last_micro;
        m_fill_execid = pm.m_fill_execid;
    }

    std::string PositionManager::toString(const std::string* ptr_algo, const std::string* ptr_symbol, bool summary) const {
        std::string ret;
        const auto idp_vec(listPosition(ptr_algo, ptr_symbol));
        if (idp_vec.size() == 0) {
            return "Nothing Found.";
        }
        IntraDayPosition idp0;
        double pnl = 0;
        int64_t qty = 0;
        for (const auto& idp:idp_vec) {
            ret += (idp->toString() + " ");
            ret += "\n";
            ret += (idp->dumpOpenOrder() + "\n");
            double pnl0;
            qty += idp->getPosition(nullptr, &pnl0);
            pnl += pnl0;
            idp0 += *idp;
        }
        if (summary) {
            double vwap = 0;
            idp0.getPosition(&vwap, nullptr);
            ret += ("***Total qty: " + std::to_string(qty) + " avg_px: " + PriceString(vwap) + " pnl (realized): " +  PnlString(pnl));
        }
        return ret;
    }

    bool PositionManager::haveThisFill(const ExecutionReport& er) {
        // assuming this er is a fill, check to see if we have seen this er
        const std::string key = std::string(er.m_execId) + std::string(er.m_clOrdId);
        if (m_fill_execid.find(key) != m_fill_execid.end()) {
            return true;
        }
        m_fill_execid.insert(key);
        return false;
    }

    std::shared_ptr<const OpenOrder> PositionManager::getOO(const std::string& clOrdId) const {
        const auto iter = m_oo_map.find(clOrdId);
        if (iter != m_oo_map.end()) {
            return iter->second;
        }
        return std::shared_ptr<const OpenOrder>();
    }

    std::vector<std::pair<std::shared_ptr<const OpenOrder>, int64_t> > PositionManager::matchOO(const std::string& symbol, int64_t qty, const double* px) const {
        bool isBuy = (qty<0);
        const auto& oolist = listOO(nullptr, &symbol, &isBuy);
        std::vector<std::pair<std::shared_ptr<const OpenOrder>, int64_t> > vec;
        for (const auto& oo : oolist) {
            if (qty == 0) {
                break;
            }
            double px_diff = 0;
            if (*px) {
                px_diff = oo->m_open_px - *px;
            }
            px_diff = isBuy? px_diff:-px_diff;
            if (px_diff > -1e-10) {
                // match this oo
                int64_t qty0 = oo->m_open_qty;
                if (qty0 == 0) {
                    continue;
                }
                if ((qty + qty0)*qty < 0) {
                    // match upto qty
                    qty0 = -qty;
                }
                vec.push_back(std::make_pair(oo, qty0));
                qty += qty0;
            }
        }
        return vec;
    }
}

