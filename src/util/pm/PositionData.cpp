#include "PositionData.h"
#include "time_util.h"
#include <cmath>
#include <cstdlib>

namespace pm {

    IntraDayPosition::IntraDayPosition() { resetPositionUnsafe(0,0,0); };

    IntraDayPosition::IntraDayPosition (
            const std::string& symbol,
            const std::string& algo,
            int64_t qty,
            double vap,
            int64_t last_micro 
    )
    : m_algo(algo), m_symbol(symbol) 
    {
        resetPositionUnsafe(qty, vap, last_micro);
    }

    IntraDayPosition::IntraDayPosition (
            const std::string& symbol,
            const std::string& algo,
            int64_t qty_long,
            double vap_long,
            int64_t qty_short,
            double vap_short,
            int64_t last_micro
    )
    : m_algo(algo), m_symbol(symbol), 
      m_qty_long(qty_long), m_vap_long(vap_long),
      m_qty_short(qty_short), m_vap_short(vap_short),
      m_last_micro(last_micro) 
    {
        if (m_last_micro==0) 
            m_last_micro=utils::TimeUtil::cur_micro();
    };

    IntraDayPosition::IntraDayPosition(const utils::CSVUtil::LineTokens& tokens) {
        // token sequence: algo, symbol, qty, vap, pnl, last_utc, read from a csv file
        m_algo = tokens[0];
        m_symbol = tokens[1];
        int64_t qty = std::stoll(tokens[2]);
        double vap = std::stod(tokens[3]);
        double pnl = std::stod(tokens[4]);
        m_last_micro = std::stoll(tokens[5]);
        resetPositionUnsafe(qty, vap, m_last_micro);
    }

    IntraDayPosition::~IntraDayPosition() {}

    void IntraDayPosition::update(const ExecutionReport& er) {
        switch(er.m_tag39[0]) {
        case '0': // new
        {
            addOO(er);
            break;
        }

        case '1': // Partial Fill
        case '2': // Fill
        {
            int64_t qty = (int64_t) er.m_qty;
            double px = er.m_px;

            // add fill
            addFill(qty, px, er.m_recv_micro);

            // update open order
            updateOO(er.m_clOrdId, qty);
            break;
        }

        case '3': // done for day
        case '4': // cancel
        case '5': // replaced
        case '7': // stopped
        case 'C': // Expired
        {
            deleteOO(er.m_clOrdId);
            break;
        }

        case '8': // rejected
        case 'A': // pending new
        case 'E': // pending replace
        case '6': // pending cancel
            break;

        default : // everything else
            break;
        }
    }

    void IntraDayPosition::resetPositionUnsafe(int64_t qty, double vap, uint64_t last_utc) {
        // reset the position.  
        if (qty>0) {
            m_qty_long = qty;
            m_qty_short = 0;
            m_vap_long = vap;
            m_vap_short = 0;
        } else {
            m_qty_long = 0;
            m_qty_short = -qty;
            m_vap_long = 0;
            m_vap_short = vap;
        }
        m_last_micro=last_utc;
        if (m_last_micro==0) 
            m_last_micro=utils::TimeUtil::cur_micro();
    }

    void IntraDayPosition::operator+=(const IntraDayPosition& idp) {
        addFill(idp.m_qty_long, idp.m_vap_long, idp.m_last_micro);
        addFill(-idp.m_qty_short, idp.m_vap_short, idp.m_last_micro);
        for (const auto& me : idp.listOO()) {
            if (m_oo.find(me->m_clOrdId) == m_oo.end()) {
                auto iter = idp.m_oo.find(me->m_clOrdId);
                m_oo[me->m_clOrdId] = iter->second;
            }
        }
    }

    IntraDayPosition IntraDayPosition::operator+(const IntraDayPosition& idp) {
        *this+=idp;
        return *this;
    }

    // this aggregates the two positions
    std::string IntraDayPosition::diff(const IntraDayPosition& idp, bool check_pnl) const 
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
                "qty: "+std::to_string(qty1) + " != " + std::to_string(qty2) + "\n");

        // check vap
        if (qty1 || qty2) {
            ret += (std::string((std::fabs(vap1-vap2)<1e-10)?"":
                    "vap: "+std::to_string(vap1) + " != " + std::to_string(vap2) + " diff(" + std::to_string(std::fabs(vap1-vap2)) + ")\n"));
        }

        if (check_pnl) {
            // check pnl
            ret += (std::string((std::fabs(pnl1-pnl2)<1e-10)?"":
                    "pnl: " + std::to_string(pnl1) + " != " + std::to_string(pnl2) + "\n"));
        }

        if (ret.size()>0) {
            ret = m_algo+":"+m_symbol+" "+ret;
        }
        return ret;
    }

    bool IntraDayPosition::operator==(const IntraDayPosition& idp) const {
        return diff(idp, false).size()==0;
    };

    utils::CSVUtil::LineTokens IntraDayPosition::toCSVLine() const {
        // same token sequence as above
        utils::CSVUtil::LineTokens vec;
        double vap, pnl;
        int64_t qty = getPosition(&vap, &pnl);
        vec.push_back(m_algo);
        vec.push_back(m_symbol);
        vec.push_back(std::to_string(qty));
        vec.push_back(std::to_string(vap));
        vec.push_back(std::to_string(pnl));
        vec.push_back(std::to_string(m_last_micro));
        return vec;
    }

    std::string IntraDayPosition::toString() const {
        double vap, pnl;
        int64_t qty = getPosition(&vap, &pnl);

        char buf[256];
        size_t bytes = snprintf(buf, sizeof(buf), "%s:%s qty=%lld, vap=%.7lf, pnl=%.3lf, last_updated=", 
                m_algo.c_str(), m_symbol.c_str(), 
                (long long) qty, vap, pnl);
        bytes += utils::TimeUtil::frac_UTC_to_string(m_last_micro, buf+bytes, sizeof(buf)-bytes,6);
        bytes += snprintf(buf+bytes, sizeof(bytes)-bytes, " DETAIL(lqty=%lld, lvap=%.7lf, sqty=%lld, svap=%.7lf)", 
                (long long) m_qty_long, m_vap_long,
                (long long) m_qty_short, m_vap_short);
        return std::string(buf);
    }

    std::string IntraDayPosition::dumpOpenOrder() const {
        std::string ret(m_algo+":"+m_symbol+" "+std::to_string(m_oo.size())+" open orders");
        auto oovec (listOO());
        for (const auto& oo:oovec) {
            ret += "\n";
            ret += oo->toString();
        }
        return ret;
    }

    bool IntraDayPosition::hasPosition() const {
        return m_qty_long != m_qty_short;
    }

    int64_t IntraDayPosition::getPosition(double* ptr_vap, double* ptr_pnl) const {
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

    int64_t IntraDayPosition::getOpenQty() const {
        int64_t qty = 0;
        for(auto iter=m_oo.begin(); iter!=m_oo.end(); ++iter) {
            qty += iter->second->m_open_qty;
        }
        return qty;
    }

    std::vector<std::shared_ptr<const OpenOrder> > IntraDayPosition::listOO() const {
        std::vector<std::shared_ptr<const OpenOrder> > vec;
        for (auto iter=m_oo.begin(); iter!=m_oo.end(); ++iter) {
            if (iter->second->m_open_qty) {
                vec.push_back(iter->second);
            }
        };
        return vec;
    }

    double IntraDayPosition::getRealizedPnl() const {
        double pnl;
        getPosition(nullptr, &pnl);
        return pnl;
    }
    
    double IntraDayPosition::getMtmPnl(double ref_px) const {
        double vap, pnl;
        int64_t m_qty = getPosition(&vap, &pnl);
        return pnl + m_qty*(ref_px-vap);
    }

    void IntraDayPosition::addOO(const ExecutionReport& er) {
        auto oop_iter = m_oo.find(er.m_clOrdId);
        if (oop_iter!=m_oo.end()) {
            const auto& oop = oop_iter->second;
            fprintf(stderr, "Warning! Recived a new on existing clOrdId, overwriting.\nIncoming new: %s, existing open order %s\n", er.toString().c_str(), oop->toString().c_str());
            oop_iter->second = std::make_shared<OpenOrder>(er);
        } else {
            m_oo.emplace(er.m_clOrdId, std::make_shared<OpenOrder>(er));
        }
    }

    void IntraDayPosition::deleteOO(const char* clOrdId) {
        auto iter = m_oo.find(clOrdId);
        if (iter != m_oo.end()) {
            iter->second->m_open_qty=0;
            m_oo.erase(iter);
        } else {
            fprintf(stderr, "Warning! delete a nonexisting open order! clOrdId: %s\n", clOrdId);
        }
    }

    void IntraDayPosition::updateOO(const char* clOrdId, int64_t qty) {
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

    void IntraDayPosition::addFill(int64_t qty, double px, uint64_t utc_micro) {
        int64_t* qtyp;
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
        if (*qtyp == 0) {
            *vapp = 0;
        } else {
            *vapp = vap/(*qtyp);
        }
        m_last_micro = utc_micro;
    }


    OpenOrder::OpenOrder() 
    : m_open_qty(0), m_open_px(0), m_open_micro(0)
    {
        memset(m_clOrdId, 0, sizeof(m_clOrdId));
    }

    OpenOrder::OpenOrder(const ExecutionReport& er)
    : m_open_qty(er.m_qty), m_open_px(er.m_px), m_open_micro(er.m_recv_micro)
    {
        memcpy(m_clOrdId, er.m_clOrdId, sizeof(IDType));
    }

    std::string OpenOrder::toString() const {
        char buf[256];
        size_t bytes = snprintf(buf, sizeof(buf), 
                "OpenOrder(clOrdId=%s,%s,open_qty=%lld,open_px=%.7lf,open_time=%s)",
                m_clOrdId, m_open_qty>0?"Buy":"Sell",std::llabs(m_open_qty), m_open_px,
                utils::TimeUtil::frac_UTC_to_string(m_open_micro,6).c_str());
        return std::string(buf);
    }
}
