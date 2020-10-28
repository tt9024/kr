#pragma once

#include <string>
#include <stdio.h>
#include <cstdint>

namespace pm {
    template<int L>
    class String {
    public:
        String();
        explicit String(const std::string& text);
        explicit String(const char* buf, size_t len);
        char m_text[L];
    };

    using SymbolType = String<16>;
    using IDType = String<32>;
    using StatusType = String<4>;

    class ExecutionReport {
    public:
        ExecutionReport();
        ExecutionReport(
                const std::string& symbol, // exchange symbol
                const std::string& algo,   // fully qualified for position
                const std::string& clOrdId,
                const std::string& execId,
                const std::string& tag39,
                int qty,
                double px,
                const std::string& utcTime,
                const std::string& optionalTag // reserved 
                );
        explicit ExecutionReport(const std::string& csv_line);
        std::string toCSV() const;

        SymbolType m_symbol;
        IDType m_algo;
        IDType m_clOrdId;
        IDType m_execId;
        StatusType m_tag39;
        int m_qty;
        double m_px;
        IDType m_utcTime;
        IDType m_optionalTag;
    };


    class EoDPosition {
    public:
        EoDPosition();
        EoDPosition(
                const std::string& symbol,
                const std::string& algo,
                int64_t qty,
                double vap,
                double pnl,
                uint64_t utcTime
                );

        explicit EoDPosition(const std::string& csv_line);
        std::string toCSV() const;
        // csv format: utc, timeStr, symbol, algo, qty, vap, pnl

        SymbolType m_symbol;
        IDType m_algo;
        int64_t m_qty;
        double m_vap;
        double m_pnl;
        uint64_t m_utcTime;
    };

    class OpenOrder;

    class IntraDayPosition {
    public:
        IntraDayPosition();
        IntraDayPosition(
                const std::string& symbol,
                const std::string& algo,
                int64_t qty_long,
                double vap_long,
                int64_t qty_short,
                double vap_short
                );
        explicit IntraDayPosition(const std::string& eod_csv_line);
        void updateFrom(const ExecutionReport& er);
        void updateFrom(int qty, double vap, bool is_long);
        void updateFrom(const OpenOrder& oo); // update open orders
        const IntraDayPosition& operator+(const IntraDayPosition& idp);
        // this aggregates the two positions

        std::string toEoDCSVLine() const;
        std::string toString() const;

        int64_t getPosition(double* ptr_vap = NULLL) const;
        int64_t getOpenPosition() const;
        double getRealizedPnl() const;
        double getMtmPnl(double ref_px) const;

    protected:
        SymbolType m_symbol;
        IDType m_algo;
        int64_t m_qty_long;
        double m_vap_long;
        int64_t m_qty_short;
        double m_vap_short;

        int64_t m_open_long;
        int64_t m_open_short;

        uint64_t m_lastUtcTime;
    };

    class OpenOrder {
    }

}
