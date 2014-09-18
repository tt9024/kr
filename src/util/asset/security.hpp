#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdexcept>
#include <unordered_map>

#define getPip(secid) ((double)(utils::SecMappings::instance().getSecurityInfo((utils::eSecurity)secid)->pip))
#define getSymbol(secid) (utils::SecMappings::instance().getSecurityInfo((utils::eSecurity)secid)->symbol)
#define pxToDouble(secid, px) ((double) px / getPip(secid))
#define pxToInt(secid, px)  ((int32_t)(px * getPip(secid) + 0.5))
#define getSymbolID(sym) (utils::SecMappings::instance().getSecId(sym))

namespace utils {

    enum eVenue {
        IB,
        TotalVenues
    };

    enum eCurrency
    {
        AUD,
        CAD,
        CHF,
        EUR,
        GBP,
        JPY,
        MXN,
        NZD,
        USD,
        TotalCCY
    };

    enum eSecurity
    {
        // FX
        AUDJPY,
        AUDNZD,
        AUDUSD,
        CADJPY,
        CHFJPY,
        EURAUD,
        EURCAD,
        EURCHF,
        EURGBP,
        EURJPY,
        EURNZD,
        EURUSD,
        GBPCHF,
        GBPJPY,
        GBPNZD,
        GBPUSD,
        NZDUSD,
        NZDJPY,
        USDCAD,
        USDCHF,
        USDJPY,
        USDMXN,

        // Future
        FUT_CL,
        //FUT_BT,
        FUT_NG,
        FUT_ES,
        //FUT_VX,
        //FUT_DX,

        // Option
        // Equity
        // FixIncome

        TotalSecurity
    };

    enum eSecType
    {
        SECTYPE_FX,
        SECTYPE_FT,
        SECTYPE_EQ,
        SECTYPE_FI,
        SECTYPE_OP,
        TotalType
    };

    struct SecurityInfo {
        eSecurity secid;
        eSecType type;
        const char* symbol; // i.e. EURUSD, CL
        uint32_t pip; // i.e. 100000 for EUR/USD, 100 for E-mini

        // for futures
        const char* contract;  // i.e. U4
        const char* mat_date;  // i.e. 20140812
        uint32_t stk_price; // for options only

        // for FX
        eCurrency base;
        eCurrency quote;   // always USD except FX pairs
    };

    struct CCYInfo {
        eCurrency m_currency;
        const char* m_symbol;
        // the pair linking (indirectly maybe) to USD
        eSecurity m_majorPair;
        bool m_isBase;
    };

    // this is the place to define securities, system restart
    // needed if this is modified
    static const SecurityInfo g_secInfo[] = {
//secid,  type,          symbol,     pip,     contract,  mat_date,   strike_price, base, quote
{ AUDJPY, SECTYPE_FX,    "AUD/JPY",  1000,    "",        "",         0,            AUD, JPY },
{ AUDNZD, SECTYPE_FX,    "AUD/NZD",  100000,  "",        "",         0,            AUD, NZD },
{ AUDUSD, SECTYPE_FX,    "AUD/USD",  100000,  "",        "",         0,            AUD, USD },
{ CADJPY, SECTYPE_FX,    "CAD/JPY",  1000,    "",        "",         0,            CAD, JPY },
{ CHFJPY, SECTYPE_FX,    "CHF/JPY",  1000,    "",        "",         0,            CHF, JPY },
{ EURAUD, SECTYPE_FX,    "EUR/AUD",  100000,  "",        "",         0,            EUR, AUD },
{ EURCAD, SECTYPE_FX,    "EUR/CAD",  100000,  "",        "",         0,            EUR, CAD },
{ EURCHF, SECTYPE_FX,    "EUR/CHF",  100000,  "",        "",         0,            EUR, CHF },
{ EURGBP, SECTYPE_FX,    "EUR/GBP",  100000,  "",        "",         0,            EUR, GBP },
{ EURJPY, SECTYPE_FX,    "EUR/JPY",  1000,    "",        "",         0,            EUR, JPY },
{ EURNZD, SECTYPE_FX,    "EUR/NZD",  100000,  "",        "",         0,            EUR, NZD },
{ EURUSD, SECTYPE_FX,    "EUR/USD",  100000,  "",        "",         0,            EUR, USD },
{ GBPCHF, SECTYPE_FX,    "GBP/CHF",  100000,  "",        "",         0,            GBP, CHF },
{ GBPJPY, SECTYPE_FX,    "GBP/JPY",  1000,    "",        "",         0,            GBP, JPY },
{ GBPNZD, SECTYPE_FX,    "GBP/NZD",  100000,  "",        "",         0,            GBP, NZD },
{ GBPUSD, SECTYPE_FX,    "GBP/USD",  100000,  "",        "",         0,            GBP, USD },
{ NZDUSD, SECTYPE_FX,    "NZD/USD",  100000,  "",        "",         0,            NZD, USD },
{ NZDJPY, SECTYPE_FX,    "NZD/JPY",  1000,    "",        "",         0,            NZD, JPY },
{ USDCAD, SECTYPE_FX,    "USD/CAD",  100000,  "",        "",         0,            USD, CAD },
{ USDCHF, SECTYPE_FX,    "USD/CHF",  100000,  "",        "",         0,            USD, CHF },
{ USDJPY, SECTYPE_FX,    "USD/JPY",  1000,    "",        "",         0,            USD, JPY },
{ USDMXN, SECTYPE_FX,    "USD/MXN",  10000,   "",        "",         0,            USD, MXN },

{ FUT_CL, SECTYPE_FT,    "NYM/CLX4", 100,     "X4",      "20141013", 0,            USD, USD },
{ FUT_NG, SECTYPE_FT,    "NYM/NGX4", 1000,    "X4",      "20141013", 0,            USD, USD },
{ FUT_ES, SECTYPE_FT,    "CME/ESZ4", 100,     "Z4",      "20141213", 0,            USD, USD },
    };

    static const CCYInfo g_ccyInfo[] = {
        {AUD,"AUD",AUDUSD,true},
        {CAD,"CAD",USDCAD,false},
        {CHF,"CHF",USDCHF,false},
        {EUR,"EUR",EURUSD,true},
        {GBP,"GBP",GBPUSD,true},
        {JPY,"JPY",USDJPY,false},
        {MXN,"MXN",USDMXN,false},
        {NZD,"NZD",NZDUSD,true},
        {USD,"USD",EURUSD,false},
    };

    class SecMappings {
    public:
        static SecMappings& instance() {
            static SecMappings mappings;
            return mappings;
        };

        eCurrency getCCY(const char* symbol) const
        {
            int i=0;
            for (; i<TotalCCY; ++i)
            {
                if (strcmp(symbol, g_ccyInfo[i].m_symbol) == 0)
                    break;
            }
            return (eCurrency) i;
        }

        eSecurity getSecId(const std::string& symbol) const
        {
            // hash function will throw exception if not found
            return g_secInfo[getSymbolHash(symbol)].secid;
        };

        const char* getCCYSymbol(const eCurrency ccy) const
        {
            return g_ccyInfo[(int) ccy].m_symbol;
        }

        const SecurityInfo* getSecurityInfo(const eSecurity secid) const {
            return &g_secInfo[secid];
        }

    private:
        eSecurity getSymbolHash(const std::string& symbol) const
        {
            std::unordered_map<std::string, eSecurity>::const_iterator iter = sec_map.find(symbol);
            if (__builtin_expect((iter == sec_map.end()), 0)) {
                char buf[128];
                snprintf(buf, sizeof(buf), "g_secInfo security not found %s",
                        symbol.c_str());
                throw std::runtime_error(buf);
            }
            return iter->second;
        }
        SecMappings() {
            // check the ordering of g_secInfo
            for (unsigned i=0; i<(unsigned)TotalSecurity; ++i)
            {
                if (g_secInfo[i].secid != i) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "g_secInfo out-of-order for %s at %d should be at %d",
                            g_secInfo[i].symbol, (int)i, (int) g_secInfo[i].secid);
                    throw std::runtime_error(buf);
                }

                std::unordered_map<std::string, eSecurity>::iterator iter = sec_map.find(std::string(g_secInfo[i].symbol));
                if (iter != sec_map.end()) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "g_secInfo duplicate security entry of %s at %d",
                            g_secInfo[i].symbol, (int)i);
                    throw std::runtime_error(buf);
                }
                sec_map[std::string(g_secInfo[i].symbol)] = (eSecurity)i;
            }
        }
        SecMappings(const SecMappings& mapping);
        SecMappings& operator = (const SecMappings& mapping);
        std::unordered_map<std::string, eSecurity> sec_map;
    };
}
