#pragma once

#include "PositionData.h"
#include <unorderd_map>
#include <vector>

namesapce pm {
    using AlgoPosition = std::unordered_map<std::string, std::unordered_map<std::string, IntraDayPosition*> >;
    using SymbolPosition = std::unordered_map<std::string, IntraDayPosition*>;

    class PositionManager {
    public:
        explicit PositionManager(const std::string& recover_path);
        // loads last EoDPosition, setup all intraday positions
        
        void update(const ExecutionReport& er);
        // updates the intraday position from either fill or open
        
        bool reconcile() const;
        // reconciles with recovery
        
        bool persist() const;
        // writes the intraday positions to EoDPosition
        
        int64_t getPosition(const string& algo, const string& symbol, double* ptr_vap, double* pnl) const ;
        int64_t getPosition(const string& symbol, double* ptr_vap, double* pnl) const;

        double getPnl(const string& algo) const;

        bool operator==(const PositionManager& pm) const;
    protected:
        const string& m_recovery_path;
        AlgoPosition m_algo_pos;
        SymbolPosition m_symbol_pos;

        // go to recovery path, load latest csv lines 
        void load(const std::string& eod_csv_file);
    }
}


