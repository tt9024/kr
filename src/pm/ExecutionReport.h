#pragma once

#include "csv_util.h"
#include <string>
#include <stdexcept>
#include <memory>

namespace pm {
    static const int SymbolLen = 16;
    static const int IDLen = 32;
    static const int StatusLen = 4;
    using SymbolType = char[SymbolLen];
    using IDType = char[IDLen];
    using StatusType = char[StatusLen];

    struct ExecutionReport {
        ExecutionReport();
        ExecutionReport (
                const std::string& symbol, // exchange symbol
                const std::string& algo,   // fully qualified for position
                const std::string& clOrdId,
                const std::string& execId,
                const std::string& tag39,
                int qty, // sign significant: + buy, - sell
                double px,
                const std::string& utcTime, // YYYYMMDD-HH:MM:SS[.sss]
                const std::string& optionalTag, // reserved 
                uint64_t recv_micro = 0  // utc when it is received locally
        );

        static ExecutionReport fromCSVLine(const utils::CSVUtil::LineTokens& token_vec);
            // reads from a csv file

        utils::CSVUtil::LineTokens toCSVLine() const;
            // write as a csv line

        SymbolType m_symbol;
        IDType m_algo;
        IDType m_clOrdId;
        IDType m_execId;
        StatusType m_tag39;
        int m_qty;   // + buy, - sell
        double m_px;
        uint64_t m_utc_milli;
        IDType m_optional;
        uint64_t m_recv_micro;

        // helper functions
        bool isFill() const;
        std::string toString() const;
        bool compareTo(const ExecutionReport& er, std::string* difflog=nullptr) const;
    };
}

