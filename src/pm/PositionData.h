#pragma once

#include <string>
#include <stdio.h>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <vector>
#include <memory>

#include "ExecutionReport.h"
#include "csv_util.h"

namespace pm {
    struct OpenOrder;
        // Forward declaration of open order data structure

    class IntraDayPosition {
    public:
        IntraDayPosition();
            // create an empty intra-day position

        IntraDayPosition(
                const std::string& symbol,
                const std::string& algo,
                int64_t qty = 0,
                double vap = 0,
                int64_t last_micro = 0 
                );
            // create an empty intra-day position, with optional
            // initial values

        IntraDayPosition(
                const std::string& symbol,
                const std::string& algo,
                int64_t qty_long,
                double vap_long,
                int64_t qty_short,
                double vap_short,
                int64_t last_micro = 0
                );
            // create intra-day position with given values

        explicit IntraDayPosition(const utils::CSVUtil::LineTokens& tokens);
            // create intra-day position from a csv line

        ~IntraDayPosition();

        void update(const ExecutionReport& er);
            // update from an execution report


        void resetPositionUnsafe(int64_t qty, double vap, uint64_t last_utc=0);
            // this resets the position to given qty, vap.
            // qty is sign significant; positive is buy, negative is sell
            // there will be only one sided position after this, i.e. 
            // the other side will be zero and the realized pnl will be 0

        void operator+=(const IntraDayPosition& idp);
        IntraDayPosition operator+(const IntraDayPosition& idp);
            // aggregate two intra-day position

        std::string diff(const IntraDayPosition& idp, bool check_pnl=false) const;
            // finds difference with the given idp, 
            // return "" in case no difference is found 
            // set check_pnl to true to also check the realized pnl
            // from the position.  This may differ if one is loaded
            // from eod (with pnl reset)

        bool operator==(const IntraDayPosition& idp) const;
            // compares two positions, in terms of position and vap

        utils::CSVUtil::LineTokens toCSVLine() const;
            // writes the current position to a csv line

        std::string toString() const;
            // writes the current position to a text line

        std::string dumpOpenOrder() const;
            // writes all open orders to multiple text lines

        bool hasPosition() const;
            // check if the position is empty

        int64_t getPosition(double* ptr_vap = nullptr, double* ptr_pnl = nullptr) const;
            // gets the current position, and optionally the volumn weighted price (vap)
            // and the realized pnl (pnl)
            // Note the return value is sign significant, i.e. positive is qty long
            // negative is qty short

        int64_t getOpenQty() const;
            // gets the total open qty. sign significant, i.e. positive is qty long
            // negative is qty short

        std::vector<std::shared_ptr<const OpenOrder> > listOO() const;
            // list all currently open orders

        double getRealizedPnl() const;
            // calculate current realized pnl
        
        double getMtmPnl(double ref_px) const;
            // calculate realized pnl plus mark to market pnl
            // using the given ref_px
        
        std::string get_symbol() const { return m_symbol ; };
        std::string get_algo() const   { return m_algo;    };

    protected:
        std::string m_algo;
        std::string m_symbol;
        int64_t m_qty_long;
        double m_vap_long;
        int64_t m_qty_short;
        double m_vap_short;
        uint64_t m_last_micro;
        std::map<std::string, std::shared_ptr<OpenOrder> > m_oo;

        void addFill(int64_t qty, double px, uint64_t utc_micro);
        void addOO(const ExecutionReport& er);
        void deleteOO(const char* clOrdId);
        void updateOO(const char* clOrdId, int64_t qty);
    };

    struct OpenOrder {
        IDType m_clOrdId;
        //IDType m_execId;  // in case clOrdId is not unique (?)
        int64_t m_open_qty; // + buy - sell, from er
        double m_open_px;
        uint64_t m_open_micro;

        OpenOrder();
            // create an empty open order

        OpenOrder(const ExecutionReport& er);
            // create an open order from execution report

        std::string toString() const;
            // writes this open order to a text line
    };
}
