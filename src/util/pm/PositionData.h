#pragma once

#include <string>
#include <stdio.h>
#include <cstdint>
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
        int m_qty;
        double m_px;
        IDType m_utc;
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
            // token sequence: algo, symbol, qty, vap, pnl, last_utc
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

        void update(const ExecutionReport& er);
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

        bool hasPosition() const {
            return m_qty_long || m_qty_short;
        }

        int64_t getPosition(double* ptr_vap = nullptr, double* ptr_pnl = nullptr) const;
        int64_t getOpenPosition() const;

        std::vector<const OpenOrder*const> listOO() const;

        double getRealizedPnl() const;
        double getMtmPnl(double ref_px) const;

    protected:
        std::string m_algo;
        std::string m_symbol;
        int64_t m_qty_long;
        double m_vap_long;
        int64_t m_qty_short;
        double m_vap_short;
        uint64_t m_last_utc;
        std::map<std::string, const OpenOrder* const> m_oo;
    };

    struct OpenOrder {
        IDType m_clOrdId;
        IDType m_execId;
        int64_t m_open_qty;
        double m_open_px;
        uint64_t m_open_utc;
    }

}
