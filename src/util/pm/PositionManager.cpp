#include "PositionManager.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <exception>

namespace pm {
    void PositionManager::load(const std::string& eod_csv_file) {
        std::string line;
        std::vector<std::string> pos_vec;
        uint64_t latest_day = 0;
        {
            std::ifstream eod_file(eod_csv_file);
            while (std::getline(eod_file, line)) {
                // read the first column as saving time
                std::istringstream iss(line);
                std::string utc_time;
                std::getline(iss >> std::skipws, utc_time, ',');
                uint64_t ut = (uint64_t)atoll(utc_time.c_str());
                if (ut > latest_day) {
                    pos_vec.clear();
                    latest_day = ut;
                }
                pos_vec.push_back(line);
            }
        };
        // populate positions
        for (const auto& line: pos_vec) {
            IntraDayPosition* idp = new IntraDayPosition(line);
            const std::string& symbol = idp->m_symbol;
            const std::string& algo = idp->m_algo;
            // add idp to the AlgoPosition
            if ( (m_algo_pos.find(algo)!=m_algo_pos.end()) &&
                 (m_algo_pos[algo].find(symbol)!=m_algo_pos[algo].end()) ) {
                // we find a duplicated line, failed
                throw std::runtime_error(std::string("PM load failed: duplicated position line found: ") + line);
            }
            m_algo_pos[algo][symbol]=idp;
            if (m_symbol_pos.find(symbol)==m_symbol_pos.end()) {
                m_symbol_Pos[symbol]=new IntraDayPosition();
            }
            (*m_symbol_pos[symbol])+=(*idp);
        };
    };

    void PositionManager::update(const ExecutionReport& er) {
    }
    
