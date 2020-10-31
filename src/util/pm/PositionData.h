#pragma once

#include <string>
#include <stdio.h>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <vector>
#include <cmath>
#include <sstream>
#include "time_util.h"

namespace pm {
    template<int L>
    class String {
    public:
        String();
        explicit String(const std::string& text);
        explicit String(const char* buf, size_t len);
        char m_text[L];
    };

    // CSV Utils 
    // TODO move to a header file
    std::vector<std::string> csv_line_tokenizer(const std::string& line, char delimiter = ',') {
        std::istringstream iss(csv_line);
        std::vector<std::string> vec;
        std::string token;
        while (getline(iss >> std::skipws, token, delimiter)) {
            vec.push_back(token);
        }
        return vec;
    }

    std::vector< std::vector<std::string> > csv_file_tokenizer(const std::string& csv_file, char delimiter = ',', int skip_head_lines = 0) {
        std::string line;
        std::vector<std::vector<std::string> > vec;
        try {
            std::ifstream csv(csv_file);
            while (std::getline(csv, line)) {
                const auto v = csv_line_tokenizer(line, delimiter);
                if (v.size()>0) {
                    vec.push_back(v);
                }
            }
        } catch(const exception& e) {
            fprintf(stderr, "Error getting csv file %s: %s\n", csv_file.c_str(), e.what());
        }
        return vec;
    }

    void csv_write_line(const std::vector<std::string>& token_vec, std::ofstream& csvfile, char delimiter = ',') {
        if (token_vec.size()==0) 
            return;
        csvfile << token_vec[0];
        for (size_t i=1; i<token_vec.size();++i) {
            csvfile << delimiter << token_vec[i];
        }
        csvfile << std::endl;
    }

    bool csv_write_file(const std::vector<std::vector<std::string> > & line_vec, const std::string& filename, delimiter=',') {
        std::ofstream csvfile
        try {
             csvfile.open(filename, std::ios_base::app);
             for (const auto& line : line_vec) {
                 csv_write_line(line, csvfile, delimiter);
             }
             return true;
        } catch (const std::exception e) {
            fprintf(stderr, "csv file %s write failed: %s\n", filename.c_str(), e.what());
        } catch (...) {
            fprintf(stderr, "csv file %s write failed for unknown reason\n", filename.c_str());
        };
        return false;
    }

    using SymbolType = String<16>;
    using IDType = String<32>;
    using StatusType = String<4>;

    class ExecutionReport {
    public:
        ExecutionReport() {
            memset((void*)this, 0, sizeof(ExecutionReport));
        }

        ExecutionReport(
                const std::string& symbol, // exchange symbol
                const std::string& algo,   // fully qualified for position
                const std::string& clOrdId,
                const std::string& execId,
                const std::string& tag39,
                int qty,   // + buy, - sell
                double px,
                const std::string& utcTime, // YYYYMMDD-HH:MM:SS[.sss]
                const std::string& optionalTag // reserved 
                ) :
            m_qty(qty), m_px(px) {
                snprintf(m_symbol, sizeof(m_symbol),"%s", symbol.c_str());
                snprintf(m_algo, sizeof(m_algo), "%s", algo.c_str());
                snprintf(m_clOrdId, sizeof(m_clOrdId), "%s", clOrdId.c_str());
                snprintf(m_execId, sizeof(m_execId), "%s", execId.c_str());
                snprintf(m_tag39, sizeof(m_tag39), "%s", tag39.c_str());
                
                optionalTag
            }

        explicit ExecutionReport(const std::string& csv_line) {
            std::istringstream iss(csv_line);


        }

        std::string toCSV() const {
        }
        

        SymbolType m_symbol;
        IDType m_algo;
        IDType m_clOrdId;
        IDType m_execId;
        StatusType m_tag39;
        int m_qty;   // + buy, - sell
        double m_px;
        uint64_t m_utc_milli;
        IDType m_optional;
    };

    /*
     * Used for summarize intraday position 
     * for admin's purpose, not used for now.
     */
    /*
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
        uint64_t m_utc;
    };
    */

    struct OpenOrder;

    class IntraDayPosition {
    public:
        IntraDayPosition(); // do init
        IntraDayPosition(
                const std::string& symbol,
                const std::string& algo,
                int64_t qty_long,
                double vap_long,
                int64_t qty_short,
                double vap_short,
                int64_t last_utc
                );
        explicit IntraDayPosition(const std::vector<std::string>& tokens) {
            // token sequence: algo, symbol, qty, vap, pnl, last_utc, read from a csv file
            m_algo = tokens[1];
            m_symbol = tokens[2];
            int64_t qty = std::stoll(tokens[3]);
            double vap = std::stod(tokens[4]);
            double pnl = std::stod(tokens[5]);
            m_last_utc = std::stoll(tokens[6]);

            if (qty>0) {
                m_qty_long = qty;
                m_qty_short = 0;
                m_vap_long = vap;
                m_vap_short = 0;
            } else {
                m_qty_short = qty;
                m_qty_long = 0;
                m_vap_short = vap;
                m_vap_long = 0;
            }
        }

        ~IntraDayPosition() {
            for(auto& iter=m_oo.begin(); iter!=m_oo.end();++iter) {
                if (iter->second) {
                    delete (iter->second);
                    iter->second=nullptr;
                }
            }
        }

        void update(const ExecutionReport& er) {
            switch(er.m_tag39[0]) {
            case '0':
               // new
                addOO(er);
                break;

            case '1': 
                // Partial Fill
                // fall through
            case '2':
                // Fill
                int64_t qty = (int64_t) er.m_qty;
                double px = er.m_px;
                uint64_t utc_milli = er.m_utc_milli;
                // add fill
                addFill(qty, px, utc_milli);

                // update open order
                updateOO(er.m_clOrdId, qty);
                break;

            case '3': 
                // done for day
            case '4':
                // cancel
            case '5':
                // replaced
            case '7':
                // stopped
            case 'C':
                // Expired
                deleteOO(er.m_clOrdId);
                break;

            case '8':
                // rejected
            case 'A':
                // pending new
            case 'E':
                // pending replace
            case '6':
                // pending cancel
                break;

            default :
                // everything else
                break;
            }
        }

        void addPosition(int64_t qty, double px, bool is_long);
        IntraDayPosition& operator+(const IntraDayPosition& idp);
        // this aggregates the two positions
        
        std::string diff(const IntraDayPosition& idp) const 
        // finds difference with the given idp, 
        // return "" in case no difference is found 
        {
            // compare qty, vap and pnl
            int64_t qty1, qty2;
            double vap1, vap2, pnl1, pnl2;
            qty1=getPosition(&vap1, &pnl1);
            qty2=idp.getPosition(&vap2, &pnl2);

            // check position
            std::string ret((qty1==qty2)?"":
                    "qty: "+std::to_string(qty1) + " != " + std::to_string(qty1) + "\n");

            // check vap
            if (qty1 || qty2) {
                ret += std::string((std::fabs(vap1-vap2)<1e-10)?"":
                        "vap: "+std::to_string(vap1) + " != " + std::to_string(vap1) + "\n");
            }
            // check pnl
            ret += std::string((std::fabs(pnl1-pnl2)<1e-10)?"":
                    "pnl: " + std::to_string(pnl1) + " != " + std::to_string(pnl2) + "\n");

            if (ret.size()>0) {
                ret = m_algo+":"+m_symbol+" "+ret;
            }
            return ret;
        }

        bool operator==(const IntraDayPosition& idp) const {
            return diff(idp).size()==0;
        };

        std::vector<std::string> toStringVec() const {
            // same token sequence as above
            std::vector<std::string> vec;
            double vap, pnl;
            int64_t qty = getPosition(&vap, &pnl);
            vec.push_back(m_algo);
            vec.push_back(m_symbol);
            vec.push_back(std::to_string(qty));
            vec.push_back(std::to_string(vap));
            vec.push_back(std::to_string(pnl));
            vec.push_back(std::to_string(m_last_utc));
            return vec;
        }

        std::string toString() const {
            double vap, pnl;
            int64_t qty = getPosition(&vap, &pnl);

            char buf[256];
            size_t bytes = snprintf(buf, sizeof(buf), "%s:%s qty=%lld, vap=%.7lf, pnl=%.3lf, last_updated=", 
                    m_algo.c_str(), m_symbol.c_str(), 
                    (long long) qty, vap, pnl);
            bytes += utils::TimeUtil::int_to_string_second_UTC(m_last_utc, buf+bytes, sizeof(buf)-bytes);
            bytes += snprintf(buf+bytes, sizeof(bytes)-bytes, "-- DETAIL(lqty=%lld, lvap=%.7lf, sqty=%lld, svap=%.7lf)", 
                    (long long) m_qty_log, m_vap_long,
                    (long long) m_qty_short, m_vap_short);
            return std::string(buf);
        }

        std::string dumpOpenOrder() const {
            std::string ret(m_algo+":"+m_symbol+" "+std::to_string(m_oo.size())+" open orders");
            for (const auto iter=m_oo.begin(); iter!=m_oo.end(); ++iter) {
                ret += "\n";
                ret += iter->second->toString();
            }
            return ret;
        }

        bool hasPosition() const {
            return m_qty_long || m_qty_short;
        }

        int64_t getPosition(double* ptr_vap = nullptr, double* ptr_pnl = nullptr) const {
            int64_t qty = m_qty_long - m_qty_short;
            if (ptr_vap || ptr_pnl) {
                double vap, pnl;
                if (qty>0) {
                    vap = m_vap_long;
                    pnl = m_qty_short*(m_vap_short-m_vap_long);
                } else {
                    vap = m_vap_short;
                    pnl = m_qty_long*(m_vap_short-m_vap_long);
                }
                if (ptr_vap) *ptr_vap=vap;
                if (ptr_pnl) *ptr_pnl=pnl;
            }
            return qty;
        }

        int64_t getOpenQty() const {
            int64_t qty = 0;
            for(const auto iter=m_oo.begin(); iter!=m_oo.end(); ++iter) {
                qty += iter->second->m_open_qty;
            }
            return qty;
        }

        std::vector<const OpenOrder*const> listOO() const {
            std::vector<const OpenOrder* const> vec;
            for (const auto iter=m_oo.begin(); iter!=m_oo.end(); ++iter) {
                vec.push_back(iter->second);
            };
            return vec;
        }

        double getRealizedPnl() const {
            double pnl;
            getPosition(nullptr, &pnl);
            return pnl;
        }
        
        double getMtmPnl(double ref_px) const {
            double vap, pnl;
            int64_t m_qty = getPosition(&vap, &pnl);
            return pnl + m_qty*(ref_px-vap);
        }

    protected:
        std::string m_algo;
        std::string m_symbol;
        int64_t m_qty_long;
        double m_vap_long;
        int64_t m_qty_short;
        double m_vap_short;
        uint64_t m_last_utc;
        std::map<std::string, const OpenOrder* const> m_oo;

        void addOO(const ExecutionReport& er) {
            OpenOrder* oop = m_oo[er.m_clOrdId];
            if (oop) {
                fprintf(stderr, "ERR! new on existing clOrdId! This report %s, existing open order %s\n", er.toString().c_str(), oop->toString().c_str());
                delete oop;
            }
            m_oo[er.m_clOrdId]=new OpenOrder(er);
        }

        void deleteOO(const char* clOrdId) {
            auto iter = m_oo.find(clOrdId);
            if (iter != m_oo.end()) {
                delete iter->second;
                m_oo.erase(iter);
            } else {
                fprintf(stderr, "Warning! delete a nonexisting open order! clOrdId: %s\n", clOrdId);
            }
        }

        void updateOO(const char* clOrdId, int64_t qty) {
            auto iter = m_oo.find(clOrdId);
            if (iter != m_oo.end()) {
                iter->second->m_open_qty-=qty;
                if (iter->second->m_open_qty == 0) {
                    deleteOO(clOrdId);
                }
            } else {
                fprintf(stderr, "Warning! update a nonexisting open order! clOrdId: %s, qty: %lld\n", clOrdId, (long long)qty);
            }
        }

        void addFill(int64_t qty, double px, uint64_t utc_milli) {
            uint64_t* qtyp;
            double* vapp;
            if (qty>0) {
                qtyp = &m_qty_long;
                vapp = &m_vap_long;
            } else {
                qty = -qty;
                qtyp = &m_qty_short;
                vapp = &m_vap_short;
            }
            double vap = (*qtyp) * (*vapp) + qty*px;
            *qtyp += qty;
            *vapp = vap/(*qtyp);
    };

    struct OpenOrder {
        IDType m_clOrdId;
        //IDType m_execId;  // in case clOrdId is not unique (?)
        int64_t m_open_qty; // + buy - sell, from er
        double m_open_px;
        uint64_t m_open_utc_milli;
        OpenOrder() : m_open_qty(0), m_open_px(0), m_open_utc_milli(0)
        {
            memset(m_clOrdId, 0, sizeof(m_clOrdId));
        };
        OpenOrder(const ExecutionReport& er) : m_open_qty(er.m_qty), m_open_px(er.m_px), m_open_utc_milli(m_utc_milli)
        {
            memcpy(m_clOrdId, er.m_clOrdId, sizeof(IDType));
        };
        std::string toString() const {
            char buf[256];
            size_t bytes = snprintf(buf, sizeof(buf), 
                    "OpenOrder(clOrdId=%s,%s,open_qty=%lld,open_px=%.7llf,open_time=", m_clOrdId, m_open_qty>0?"Buy":"Sell",std::abs(m_open_qty), m_open_px);
            bytes += utils::TimeUtil::int_to_string_second_UTC((int)(m_open_utc_milli/1000), buf+bytes, sizeof(buf)-bytes);
            bytes += snprintf(buf+bytes, sizeof(buf)-bytes, 
                    ".%d)", (int)(m_open_utc_milli%1000));
            return std::string(buf);
        }
    }
}
