#pragma once

#include "csv_util.h"
#include <string>
#include <stdexcept>
#include <memory>

namespace pm {
    /*
    template<int L>
    class String {
    public:
        String();
        explicit String(const std::string& text) {
            snprintf(m_text, L, "%s", text.c_str());
        }
        explicit String(const char* buf, size_t len) {
            if (len > L) {
                throw std::runtime_error(std::to_string(len)+" more than " + std::to_string(L) + " in String()");
            }
            memset(m_text,0,L);
            memcpy(m_text, buf, len);
        }

        char m_text[L];
        operator char*() const {
            return (char*)m_text;
        }
        operator std::string() const {
            return std::string(m_text);
        }
    };
    using SymbolType = String<16>;
    using IDType = String<32>;
    using StatusType = String<4>;
    */

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

