#include "PositionManager.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "csv_util.h"
#include "time_util.h"

#define EoDPositionFile "eod_pos.csv"
#define FillFile "fills.csv"
#define RecoveryPath "recovery"

namespace pm {
    PositionManager::PositionManager(const std::string& name, const std::string& recover_path) :
    m_name(name), m_recovery_path(recover_path.size()==0? RecoveryPath:recover_path),
    m_load_second(loadEoD()), m_last_micro(0)
    {}

    std::string PositionManager::getLoadUtc() const {
        // from YYYYMMDD-HH:MM:SS local
        // see persist()
        return m_load_second;
    }

    std::string PositionManager::eod_csv() const {
        return m_recovery_path + "/" + EoDPositionFile;
    }

    std::string PositionManager::fill_csv() const {
        return m_recovery_path + "/" + FillFile;
    }

    std::string PositionManager::loadEoD() {
        auto line_vec = utils::CSVUtil::read_file(eod_csv());
        size_t line_cnt = line_vec.size();
        if (line_cnt==0) {
            return utils::TimeUtil::frac_UTC_to_string(0,0);
        }

        std::string latest_day = line_vec[line_cnt-1][0];
        // this should be YYYYMMDD,HH:MM:SS the time when 
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
            auto idp = std::make_shared<IntraDayPosition>(token_vec);
            const std::string& symbol = idp->get_symbol();
            const std::string& algo = idp->get_algo();
            // add idp to the AlgoPosition
            if ( (m_algo_pos.find(algo)!=m_algo_pos.end()) &&
                 (m_algo_pos[algo].find(symbol)!=m_algo_pos[algo].end()) ) {
                // we find a duplicated line, failed
                throw std::runtime_error(std::string("PM load failed: duplicated position found: ") + idp->toString());
            }
            m_algo_pos[algo][symbol]=idp;
            m_symbol_pos[symbol][algo]=idp;
        };
        return latest_day;
    };

    void PositionManager::update(const ExecutionReport& er, bool persist_fill) {
        // check for duplicate fill
        // this could happen if the recovery overlaps with real-time
        // fills, or otherwise a previous fill is resent
        if (er.isFill()) {
            if ( haveThisFill(er) ) {
                fprintf(stderr, "warning! duplicated fill not updated: %s\n", er.toString().c_str());
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
            utils::CSVUtil::write_line(er.toCSVLine(), fill_csv(), true);
        }
    }

    bool PositionManager::reconcile(const std::string& recovery_file, std::string& diff_logs, bool adjust) {
        // this loads the latest positions from eod_position file
        // update with the execution reports from the given recovery_file
        // and compare the result with this position
        // return true if match, otherwise, mismatches noted in diff_logs
        // If adjust is set to true, this position becomes the recovery position.
        PositionManager pm ("reconcile", m_recovery_path);
        pm.loadRecovery(recovery_file, false);
        auto diff_log1 = diff(pm);
        auto diff_log2 =  pm.diff(*this);

        if ((diff_log1.size()) == 0 && (diff_log2.size()) == 0) {
            return true;
        }
        diff_logs = diff_log1 + "\n" + diff_log2;
        if (adjust) {
            fprintf(stderr, "Warning, %s is going to copy from %s, all existing open order trackings are lost. Download open order again if needed!\n", m_name.c_str(), pm.m_name.c_str());
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
            fprintf(stderr, "Failed to load recover.  fname: %s, error: %s\npm:%s", 
                    fname.c_str(), e.what(), toString().c_str());
            return false;
        }
        return true;
    }

    bool PositionManager::persist() const {
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
            fprintf(stderr, "Warning: %s\n", warn_str.c_str());
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
        const auto pos = listPosition(&algo, &symbol);
        int64_t qty = 0;
        if (pos.size()==1) {
            qty = pos[0]->getPosition(ptr_vap, pnl);
            if (oqty) {
                *oqty = pos[0]->getOpenQty();
            }
        }
        return qty;
    }

    int64_t PositionManager::getPosition(const std::string& symbol, double* ptr_vap, double* pnl, int64_t* oqty) const {
        // get an aggregated position for the given symbol
        const auto pos = listPosition(nullptr, &symbol);
        int64_t qty = 0;
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
                    const auto& iter2 = iter->second.find(*symbol);
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
                const auto& iter = m_symbol_pos.find(*symbol);
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

    std::vector<std::shared_ptr<const OpenOrder> > PositionManager::listOO(const std::string* algo, const std::string* symbol) const {
        std::vector<std::shared_ptr<const OpenOrder> > vec_oo;
        const auto vec = listPosition(algo, symbol);
        for (const auto& idp: vec) {
            const auto oov = idp->listOO();
            for (const auto& oo:oov) {
                vec_oo.push_back(oo);
            }
        }
        return vec_oo;
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
        fprintf(stderr, "INFO - %s gets all positions from %s", m_name.c_str(), pm.m_name.c_str());
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
        for (const auto& idp:idp_vec) {
            ret += (idp->toString() + " ");
            ret += (idp->dumpOpenOrder() + "\n");
            idp0 += *idp;
        }
        if (summary) {
            double vwap = 0, pnl = 0;
            int64_t qty = idp0.getPosition(&vwap, &pnl);
            ret += ("***Total qty: " + std::to_string(qty) + " avg_px: " + std::to_string(vwap) + " pnl (realized): " + std::to_string(pnl));
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
}

