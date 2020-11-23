#include "ExecutionReport.h"
#include "time_util.h"
#include <cmath>

namespace pm {
    ExecutionReport::ExecutionReport() {
        memset((void*)this, 0, sizeof(ExecutionReport));
    }

    ExecutionReport::ExecutionReport (
           const std::string& symbol, // exchange symbol
           const std::string& algo,   // fully qualified for position
           const std::string& clOrdId,
           const std::string& execId,
           const std::string& tag39,
           int qty,   // + buy, - sell
           double px,
           const std::string& utcTime, // YYYYMMDD-HH:MM:SS[.sss]
           const std::string& optionalTag, // reserved 
           uint64_t recv_micro
    )
    :  m_qty(qty), m_px(px), 
       m_utc_milli(utils::TimeUtil::string_to_frac_UTC(utcTime.c_str(), 3)),
       m_recv_micro(recv_micro) 
    {
        snprintf(m_symbol, sizeof(m_symbol),"%s", symbol.c_str());
        snprintf(m_algo, sizeof(m_algo), "%s", algo.c_str());
        snprintf(m_clOrdId, sizeof(m_clOrdId), "%s", clOrdId.c_str());
        snprintf(m_execId, sizeof(m_execId), "%s", execId.c_str());
        snprintf(m_tag39, sizeof(m_tag39), "%s", tag39.c_str());
        snprintf(m_optional, sizeof(m_optional), "%s", optionalTag.c_str());
        if (m_recv_micro==0) {
            m_recv_micro=utils::TimeUtil::cur_micro();
        }
    }

    ExecutionReport ExecutionReport::fromCSVLine(const utils::CSVUtil::LineTokens& token_vec) {
        return ExecutionReport(token_vec[0], token_vec[1], token_vec[2], token_vec[3],
                token_vec[4], std::stod(token_vec[5]), std::stod(token_vec[6]),
                token_vec[7], token_vec[8], std::stod(token_vec[9]));
    }

    utils::CSVUtil::LineTokens ExecutionReport::toCSVLine() const {
        std::vector<std::string> token_vec;
        token_vec.push_back(m_symbol);
        token_vec.push_back(m_algo);
        token_vec.push_back(m_clOrdId);
        token_vec.push_back(m_execId);
        token_vec.push_back(m_tag39);
        token_vec.push_back(std::to_string(m_qty));
        token_vec.push_back(std::to_string(m_px));
        token_vec.push_back(utils::TimeUtil::frac_UTC_to_string(m_utc_milli, 3));
        token_vec.push_back(m_optional);
        token_vec.push_back(std::to_string(m_recv_micro));
        return token_vec;
    }

    std::string ExecutionReport::toString() const {
        char buf[256];
        snprintf(buf, sizeof(buf), 
                "Execution Report [symbol=%s, algo=%s, clOrdId=%s, execId=%s, tag39=%s, qty=%d, px=%.7lf, execTime=%s, tag=%s, recvTime=%s]",
                m_symbol, m_algo, m_clOrdId, m_execId, m_tag39, m_qty, m_px, 
                utils::TimeUtil::frac_UTC_to_string(m_utc_milli, 3).c_str(), m_optional, 
                utils::TimeUtil::frac_UTC_to_string(m_recv_micro, 6).c_str());
        return std::string(buf);
    }

    bool ExecutionReport::isFill() const {
        return (m_tag39[0] == '1') || (m_tag39[0] == '2');
    }

    bool ExecutionReport::compareTo(const ExecutionReport& er, std::string* difflog)  const {
        const auto& l1 = toCSVLine();
        const auto& l2 = er.toCSVLine();
        if (l1.size() != l2.size()) {
            if (difflog) {
                *difflog = "size mismatch!";
                return false;
            }
        }
        const size_t px_col = 5;
        for (size_t i=0; (i<l1.size()) && (i!=px_col); ++i) {
            if (l1[i] != l2[i]) {
                if (difflog) {
                    *difflog = "column " + std::to_string(i) + " mismatch!";
                }
                return false;
            }
        }
        if (std::fabs(m_px-er.m_px)>1e-10) {
            if (difflog) {
                *difflog = "column " + std::to_string(px_col) + " mismatch!";
            }
            return false;
        }
        return true;
    }
}
