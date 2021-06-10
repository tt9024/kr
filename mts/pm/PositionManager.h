#pragma once

#include "PositionData.h"
#include <memory>
#include <map>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace pm {
    using PositionMap = std::map<std::string, std::map<std::string, std::shared_ptr<IntraDayPosition> > >;

    class PositionManager {
    public:
        explicit PositionManager(const std::string& name, const std::string& recover_path = "");
        ~PositionManager() {};

        std::string getLoadUtc() const;
        // read the latest positions from eod_csv_file, 
        // return the utc of the latest persistence

        bool loadRecovery(const std::string& recovery_file, bool persist_fill=false);
        // loads execution reports from recovery file

        bool reconcile(const std::string& recovery_file, std::string& diff_logs, bool adjust=false) ;
        // reconciles this position with previous eod position updated with recovery_file. 

        void update(const ExecutionReport& er, bool persist_fill=false);
        // updates the intraday position from either fill or open
 
        void deleteOO(const char* clOrdId);
        // delete an open order, not from execution report

        void resetPnl();
        // reset daily pnl to be 0
        
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

        std::vector<std::shared_ptr<const IntraDayPosition> > listOutInPosition(const std::string& ptr_algo, const std::string& ptr_symbol) const;
        // list all in/out positions given algo and symbol.  
        // If out exists, returns vector size of 2 for in and out positions. 
        // If only in is found, return in position. Otherwise, emtpy vector

        std::vector<std::shared_ptr<const OpenOrder> > listOO(const std::string* ptr_algo=nullptr, const std::string* ptr_symbol=nullptr, const bool* ptr_side=nullptr) const;
        // list all open positions given either algo or symbol, if none were given, list all
        // if ptr_side is given, only list buy OO if true, sell OO otherwise.

        std::shared_ptr<const OpenOrder> getOO(const std::string& clOrdId) const;
        // get an open order by clOrdId, return empty pointer if not found
        
        std::vector<std::pair<std::shared_ptr<const OpenOrder>, int64_t> > matchOO(
                const std::string& symbol, 
                int64_t qty, 
                const double* px = nullptr) const;
        // get a list of matching open orders to fill internally
        // qty is sign significant, px is a limit price to be matched
        // return a vector of pair, each with a pointer to the open order
        // and the qty (with sign) to be matched.  Total matched quantity
        // should be less or equal to the given qty.

        double getPnl(const std::string* ptr_algo=nullptr, 
                      const std::string* ptr_symbol=nullptr) const;
        // get aggregated pnl given algo or symbol, if none given, aggregate all

        std::string diff(const PositionManager& pm) const;
        // for each position, check with pm

        void operator=(const PositionManager& pm);
        bool operator==(const PositionManager& pm) const;
        const std::string& getName() const { return m_name;};
        std::string getRecoveryPath() const { return m_recovery_path; };

        std::string toString(const std::string* ptr_algo=nullptr, const std::string* ptr_symbol=nullptr, bool summary=false) const;

        // const utilities 
        std::string eod_csv() const;
        std::string daily_eod_csv() const;
        // get name of eod position csv file
        std::string fill_csv() const;
        // get the name of fill csv file
    protected:
        const std::string m_name;
        const std::string m_recovery_path;
        PositionMap m_algo_pos;
        PositionMap m_symbol_pos;
        const std::string m_load_second;
        uint64_t m_last_micro;
        std::unordered_set<std::string> m_fill_execid;
        std::unordered_map<std::string, std::shared_ptr<const OpenOrder> > m_oo_map;

        std::string loadEoD();
        // for each position, check with pm

        template<typename V, typename MIter>
        void addAllMapVal(V& vec, const MIter& iter1, const MIter& iter2) const {
            for (MIter iter=iter1; iter != iter2; ++iter) {
                vec.push_back(iter->second);
            };
        }

        bool haveThisFill(const ExecutionReport& er);
    };
};

