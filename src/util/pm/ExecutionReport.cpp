#include "ExecutionReport.h"

namespace pm {
    ExecutionReport::ExecutionReport() {
        memset((void*)this, 0, sizeof(ExecutionReport));
    }

    ExecutionReport::ExecutionReport(
           const std::string& symbol, // exchange symbol
           const std::string& algo,   // fully qualified for position
           const std::string& clOrdId,
           const std::string& execId,
           const std::string& tag39,
           int qty,   // + buy, - sell
           double px,
           const std::string& utcTime, // YYYYMMDD-HH:MM:SS[.sss]
           const std::string& optionalTag // reserved 
           uint64_t recv_micro = 0;
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
        token_vec.push_back(m_execId);
        token_vec.push_back(m_tag39);
        token_vec.push_back(std::to_string(m_qty));
        token_vec.push_back(std::to_string(m_px));
        token_vec.push_back(utils::TimeUtil::frac_to_String(m_utc_milli, 3));
        token_vec.push_back(m_optional);
        token_vec.push_back(std::to_string(m_recv_micro));
    }

    std::string ExecutionReport::toString() const {
        char buf[256];
        size_t bytes = snprintf(buf, sizeof(buf), 
                "Execution Report [symbol=%s, algo=%s, clOrdId=%s, execId=%s, tag39=%s, qty=%d, px=%.7lf, execTime=%s, tag=%s, recvTime=%s]",
                m_symbol, m_algo, m_clOrdId, m_execId, m_tag39, m_qty, m_px, 
                utils::TimeUtil::frac_to_String(m_utc_milli, 3).c_str(), m_optional, 
                utils::TimeUtil::frac_to_String(m_recv_micro, 6).c_str());
        return std::string(buf);
    }

    bool ExecutionReport::isFill() const {
        return (m_tag39[0] == '1') || (m_tag39[0] == '2');
    }
}
