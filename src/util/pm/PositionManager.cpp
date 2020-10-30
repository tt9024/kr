#include "PositionManager.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <exception>

#define EoDPositionFile eod_pos.csv

namespace pm {
    std::string PositionManager::eod_csv() const {
        return m_recovery_path + "/" + EoDPositionFile;

    uint64_t PositionManager::loadEoD() {
        auto line_vec = csv_file_tokenizer(eod_csv());
        size_t line_cnt = line_vec.size();
        if (line_cnt==0) {
            return;
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
            IntraDayPosition* idp = new IntraDayPosition(token_vec);
            const std::string& symbol = idp->m_symbol;
            const std::string& algo = idp->m_algo;
            // add idp to the AlgoPosition
            if ( (m_algo_pos.find(algo)!=m_algo_pos.end()) &&
                 (m_algo_pos[algo].find(symbol)!=m_algo_pos[algo].end()) ) {
                // we find a duplicated line, failed
                throw std::runtime_error(std::string("PM load failed: duplicated position line found: ") + line);
            }
            m_algo_pos[algo][symbol]=idp;
            m_symbol_pos[symbol][algo]=idb;
        };
        return latest_day;
    };

    void PositionManager::update(const ExecutionReport& er) {
        // if it's a fill, need to persist
        // update the intra-day position
        IntraDayPosition* idp = m_algo_pos[er.m_algo][er.m_symbol];
        if (!idp) {
            idp = new IntraDayPosition();
            m_algo_pos[er.m_algo][er.m_symbol] = idp;
            m_symbol_pos[er.m_symbol][er.m_algo] = idp;
        }
        idp->update(er);
    }

    bool PositionManager::reconcile(const std::string& recovery_file, std::string& diff_logs, bool adjust) const {
        PositionManager pm ("reconcile", m_recovery_path);
        pm.loadRecovery(recovery_file);
        diff_logs = diff(pm);
        diff_logs += pm.diff(*this);
        if (diff_logs.size() == 0) {
            return true;
        }
        if (adjust) {
            movePosition(pm);
        }
        return false;
    }

    bool PositionManager::persist() const {
        time_t cur_sec = time();
        char buf[32];
        utils::TimeUtil::int_to_string_second_UTC(cur_sec, buf, sizeof(buf));
        std::string eod_timestamp(buf);
        std::vector< std::vector< std::string> > line_vec;
        for (const auto& iter = m_algo_pos.begin();
             iter != m_algo_pos.end();
             ++iter) {
            for (const auto& iter2 = iter->second.begin();
                     iter2 != iter->second.end();
                     ++iter2) {
                std::vector<std::string> token_vec = iter2->second->toStringVec();
                token_vec.insert(token_vec.begin(), eod_timestamp);
                line_vec.push_back(token_vec);
            }
        };
        return csv_write_file(line_vec, eod_csv());
    }

    bool PositionManager::operator==(const PositionManager& pm) const {
        return (diff(pm)+pm.diff(*this)).size()==0;
    }

    std::string diff(const PositionManager& pm) const {
        std::string diff_str;
        if (pm.m_load_timestamp != m_load_timestamp) {
            diff_str = "load utc: " + m_load_timestamp + "(" + m_name+") != " + std::to_string(pm.m_load_timestamp) + "("+pm.m_name+")";
            return false;
        }
        for (const auto& iter = m_algo_pos.begin();
             iter != m_algo_pos.end();
             ++iter) {
            for (const auto& iter2 = iter->second.begin();
                 iter2 != iter->second.end();
                 ++iter2) {

                const auto idpv = pm.listPosition(&iter->first, &iter2->first);
                if (idpv.size()==1) {
                    diff_str+=iter2->second->diff(idpv[0]);
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
}

