#pragma once

#include "PositionData.h"
#include <unorderd_map>
#include <vector>

namesapce pm {
    using PositionMap = std::unordered_map<std::string, std::unordered_map<std::string, IntraDayPosition*> >;

    class PositionManager {
    public:
        explicit PositionManager(const std::string& name, const std::string& recover_path) :
        m_name(name), m_recovery_path(recovery_path),
        m_load_timestamp(loadEoD()), m_last_utc(0) 
        {}

        ~PositionManager() { delAll() ;};

        uint64_t getLoadUtc() const { 
            // TODO - get to the utc
            // from YYYYMMDD-HH:MM:SS local
            return 0;
        };

        // read the latest positions from eod_csv_file, 
        // return the utc of the latest persistence

        bool loadRecovery(const std::string& recovery_file);
        // loads execution reports from recovery file

        void update(const ExecutionReport& er);
        // updates the intraday position from either fill or open
        
        bool reconcile(const std::string& recovery_file, std::string& diff_logs, bool adjust=false) ;
        // reconciles this position with previous eod position updated with recovery_file. 

        bool persist() const;
        // writes the intraday positions to EoDPosition
        
        int64_t getPosition(const string& algo, const string& symbol, int64_t* ptr_qty, double* ptr_vap, double* pnl) const ;
        int64_t getPosition(const string& symbol, double* ptr_vap, double* pnl) const;

        std::vector<const IntraDayPosition* const> listPosition(const string* algo=nullptr, const string* symbol=nullptr) const;

        std::vector<const OpenOrder* const> listOO(const string* algo=nullptr, const string* symbol=nullptr) const;

        double getPnl(const string* algo=nullptr, 
                      const string* symbol=nullptr) const;

        bool operator==(const PositionManager& pm) const;
        const std::string& getName() const { return m_name;};
    protected:
        const std::string m_name;
        const std::string m_recovery_path;
        PositionMap m_algo_pos;
        PositionMap m_symbol_pos;
        const std::string m_load_timestamp;
        uint64_t m_last_utc;

        std::string loadEoD();
        std::string diff(const PositionManager& pm) const;
        // for each position, check with pm

        void delAll() {
            for (auto& iter = m_algo_pos.begin(); 
                 iter != m_algo_pos.end();
                 ++i) {
                for (auto& iter2 = iter->second.begin();
                     iter2 != iter->second.end();
                     ++iter2) {
                    if (iter2->second) {
                        free (iter2->second);
                        iter2->second=nullptr;
                    }
                }
            }
            m_algo_pos.clear();
            m_symbol_pos.clear();
        };

        void movePosition(PositionManager& pm) {
            delAll();
            m_algo_pos = pm.m_algo_pos;
            m_symbol_pos = pm.m_symbol_pos;
            pm.m_algo_pos.clear();
            pm.m_symbol_pos.clear();
        } ; 

        std::string eod_csv() const;
        // get name of eod position csv file
    }

}


