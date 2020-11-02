#pragma once

#include "PositionData.h"
#include <memory>
#include <map>
#include <vector>

namesapce pm {
    using PositionMap = std::map<std::string, std::map<std::string, std::shared_ptr<IntraDayPosition> > >;

    class PositionManager {
    public:
        explicit PositionManager(const std::string& name, const std::string& recover_path);
        ~PositionManager() {};

        uint64_t getLoadUtc() const;
        // read the latest positions from eod_csv_file, 
        // return the utc of the latest persistence

        bool loadRecovery(const std::string& recovery_file);
        // loads execution reports from recovery file
        //
        bool reconcile(const std::string& recovery_file, std::string& diff_logs, bool adjust=false) ;
        // reconciles this position with previous eod position updated with recovery_file. 

        void update(const ExecutionReport& er);
        // updates the intraday position from either fill or open
        
        bool persist() const;
        // writes the intraday positions to EoDPosition
        
        int64_t getPosition(const string& algo, const string& symbol, 
                           int64_t* ptr_qty, double* ptr_vap, double* pnl) const ;
        // get position, vap, pnl given algo/symbol

        int64_t getPosition(const string& symbol, double* ptr_vap, double* pnl) const;
        // get total position, vap, pnl given a symbol

        std::vector<std::shared_ptr<const IntraDayPosition> > listPosition(const string* algo=nullptr, const string* symbol=nullptr) const;
        // list all positions given either algo or symbol.  If none were given, list all

        std::vector<std::shared_ptr<const OpenOrder> > listOO(const string* algo=nullptr, const string* symbol=nullptr) const;
        // list all open positions given either algo or symbol, if none were given, list all

        double getPnl(const string* algo=nullptr, 
                      const string* symbol=nullptr) const;
        // get aggregated pnl given algo or symbol, if none given, aggregate all

        void operator=(const PositionManager& pm);
        bool operator==(const PositionManager& pm) const;
        const std::string& getName() const { return m_name;};

    protected:
        const std::string m_name;
        const std::string m_recovery_path;
        PositionMap m_algo_pos;
        PositionMap m_symbol_pos;
        const std::string m_load_timestamp;
        uint64_t m_last_micro;

        std::string loadEoD();
        std::string diff(const PositionManager& pm) const;
        // for each position, check with pm

        std::string eod_csv() const;
        // get name of eod position csv file

        template<typename V, typename MIter>
        void PositionManager::addAllMapVal(V& vec, MIter iter1, MIter iter2) {
            for (; iter1 != iter2, ++iter1) {
                vec.push_back(iter1->second);
            };
        }
    }
};

