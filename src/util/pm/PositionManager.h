#pragma once

#include "PositionData.h"
#include <memory>
#include <map>
#include <vector>
#include <unordered_set>

namespace pm {
    using PositionMap = std::map<std::string, std::map<std::string, std::shared_ptr<IntraDayPosition> > >;

    class PositionManager {
    public:
        explicit PositionManager(const std::string& name, const std::string& recover_path = "");
        ~PositionManager() {};

        std::string getLoadUtc() const;
        // read the latest positions from eod_csv_file, 
        // return the utc of the latest persistence

        bool loadRecovery(const std::string& recovery_file, bool persist_fill=true);
        // loads execution reports from recovery file

        bool reconcile(const std::string& recovery_file, std::string& diff_logs, bool adjust=false) ;
        // reconciles this position with previous eod position updated with recovery_file. 

        void update(const ExecutionReport& er, bool persist_fill=true);
        // updates the intraday position from either fill or open
        
        bool persist() const;
        // writes the intraday positions to EoDPosition
        
        int64_t getPosition(const std::string& algo, const std::string& symbol, 
                           double* vap=nullptr, double* pnl=nullptr, int64_t* oqty=nullptr) const ;
        // get position, vap, pnl given algo/symbol
        // set ptr to nullptr if not interested

        int64_t getPosition(const std::string& symbol, 
                           double* vap=nullptr, double* pnl=nullptr, int64_t* oqty=nullptr) const;
        // get total position, vap, pnl given a symbol

        std::vector<std::shared_ptr<const IntraDayPosition> > listPosition(const std::string* ptr_algo=nullptr, const std::string* ptr_symbol=nullptr) const;
        // list all positions given either algo or symbol.  If none were given, list all

        std::vector<std::shared_ptr<const OpenOrder> > listOO(const std::string* ptr_algo=nullptr, const std::string* ptr_symbol=nullptr) const;
        // list all open positions given either algo or symbol, if none were given, list all

        double getPnl(const std::string* ptr_algo=nullptr, 
                      const std::string* ptr_symbol=nullptr) const;
        // get aggregated pnl given algo or symbol, if none given, aggregate all

        std::string diff(const PositionManager& pm) const;
        // for each position, check with pm

        void operator=(const PositionManager& pm);
        bool operator==(const PositionManager& pm) const;
        const std::string& getName() const { return m_name;};
        std::string getRecoveryPath() const { return m_recovery_path; };

        std::string toString(const std::string* ptr_algo=nullptr, const std::string* ptr_symbol=nullptr) const;

    protected:
        const std::string m_name;
        const std::string m_recovery_path;
        PositionMap m_algo_pos;
        PositionMap m_symbol_pos;
        const std::string m_load_second;
        uint64_t m_last_micro;
        std::unordered_set<std::string> m_fill_execid;

        std::string loadEoD();
        // for each position, check with pm

        std::string eod_csv() const;
        // get name of eod position csv file
        
        std::string fill_csv() const;
        // get the name of fill csv file

        template<typename V, typename MIter>
        void addAllMapVal(V& vec, const MIter& iter1, const MIter& iter2) const {
            for (MIter iter=iter1; iter != iter2; ++iter) {
                vec.push_back(iter->second);
            };
        }

        bool haveThisFill(const ExecutionReport& er);
    };
};

