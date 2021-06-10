#include "symbol_map.h"
#include "plcc/PLCC.hpp"
#include <set>

#define SymbolMapConfig "SymbolMap"

namespace utils {

    TradableInfo::TradableInfo()
    : _tick_size(0), _point_value(0), _px_multiplier(0), _N(0), 
    _expiration_days(0), _lotspermin(10), _subscribed(false) {}

    TradableInfo::TradableInfo(
            const std::string& tradable,
            const std::string& symbol,
            const std::string& exch_symbol,
            const std::string& mts_contract,
            const std::string& mts_symbol,
            const std::string& venue,
            const std::string& type,
            const std::string& contract_month,
            const std::string& tt_security_id,
            const std::string& tt_venue,
            const std::string& currency,
            const std::string& expiration_date,
            double tick_size,
            double point_value,
            double px_multiplier,
            int N,
            int expiration_days,
            int lotspermin,
            bool subscribed) 
    : _tradable(tradable), _symbol(symbol), _exch_symbol(exch_symbol),
      _mts_contract(mts_contract), _mts_symbol(mts_symbol), 
      _venue(venue), _type(type), _contract_month(contract_month),
      _tt_security_id(tt_security_id), _tt_venue(tt_venue), _currency(currency),
      _expiration_date(expiration_date), _tick_size(tick_size),
      _point_value(point_value), _px_multiplier(px_multiplier), _N(N),
      _expiration_days(expiration_days), _lotspermin(lotspermin), _subscribed(subscribed) {};

    std::string TradableInfo::toString() const {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %.7lf, %.7lf, %.7lf, %d, %d, %d, %d",
                _tradable.c_str(), _symbol.c_str(), _exch_symbol.c_str(), _mts_contract.c_str(), _mts_symbol.c_str(),
                _venue.c_str(), _type.c_str(), _contract_month.c_str(),
                _tt_security_id.c_str(), _tt_venue.c_str(), _currency.c_str(), _expiration_date.c_str(),
                _tick_size, _point_value, _px_multiplier,
                _N, _expiration_days, _lotspermin,
                (int)_subscribed);
        return std::string(buf);
    }

    SymbolMapReader::SymbolMapReader(const std::string& cfg_file)
    : m_cfg_file(((cfg_file == "") ? plcc_getString(SymbolMapConfig) : cfg_file) )
    {
        reload();
    }

    void SymbolMapReader::reload() {
        try {
            const utils::ConfigureReader cfg (m_cfg_file.c_str());
            const auto& tradables = cfg.getReader("tradable");
            clear();
            auto keys = tradables.listKeys();
            for (const auto& k:keys) {
                TradableInfo* ti = new TradableInfo(k, tradables.getReader(k));
                m_tradable[k] = ti;
                m_tradable_by_symbol[ti->_symbol].push_back(ti);
                m_tradable_by_mts[ti->_mts_symbol] = ti;
                m_tradable_by_ttsecid[ti->_tt_security_id] = ti;
                m_tradable_by_mts_contract[ti->_mts_contract] = ti;
            }

            // setup N0/N1 map
            for (const auto& kv : m_tradable) {
                const auto* ti(kv.second);
                if (ti->_N == 1) {
                    // if this is N1, see if N0 exists
                    const auto& tivec(getAllByTradable(kv.first));
                    for (const auto* ti0 : tivec) {
                        if (ti0->_N == 0) {
                            m_n0_by_n1[kv.first]=ti0;
                            break;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            logError("Error reload the symbol map: %s", e.what());
            throw e;
        }
    }

    const SymbolMapReader& SymbolMapReader::get(bool reload) {
        static SymbolMapReader reader;
        if (reload) {
            reader.reload();
        }
        return reader;
    }

    const SymbolMapReader SymbolMapReader::getFile(const std::string& file) {
        return SymbolMapReader(file);
    }

    void SymbolMapReader::clear() {
        for (const auto& t: m_tradable) {
            delete t.second;
        }
        m_tradable.clear();
        m_tradable_by_symbol.clear();
        m_tradable_by_mts.clear();
        m_tradable_by_ttsecid.clear();
    }

    SymbolMapReader::~SymbolMapReader() {
        clear();
    }

    const TradableInfo* SymbolMapReader::getByMtsSymbol(const std::string& mts_symbol, bool return_null) const {
        const auto iter = m_tradable_by_mts.find(mts_symbol);
        if (iter == m_tradable_by_mts.end()) {
            if (return_null) return nullptr;
            logError("No tradable found for MTS symbol: %s!", mts_symbol.c_str());
            throw std::runtime_error("tradable not found for mts symbol!");
        }
        return iter->second;
    }

    const std::vector<const TradableInfo*> SymbolMapReader::getAllBySymbol(const std::string& symbol) const {
        const auto iter = m_tradable_by_symbol.find(symbol);
        if (iter == m_tradable_by_symbol.end()) {
            logError("No tradable found for symbol: %s!", symbol.c_str());
            throw std::runtime_error("tradable not found for mts symbol!");
        }
        return iter->second;
    }

    const TradableInfo* SymbolMapReader::getByTradable(const std::string& tradable_symbol, bool return_null) const {
        const auto iter = m_tradable.find(tradable_symbol);
        if (iter == m_tradable.end()) {
            if (return_null) return nullptr;
            logError("No tradable found for tradable symbol: %s!", tradable_symbol.c_str());
            throw std::runtime_error("tradable not found for tradable symbol!");
        }
        return iter->second;
    }

    const TradableInfo* SymbolMapReader::getByTTSecId(const std::string& tt_security_id, bool return_null) const {
        const auto iter = m_tradable_by_ttsecid.find(tt_security_id);
        if (iter == m_tradable_by_ttsecid.end()) {
            if (return_null) return nullptr;
            logError("No tradable found for TT Security ID: %s!", tt_security_id.c_str());
            throw std::runtime_error(std::string("tradable not found for tt security id ") + tt_security_id);
        }
        return iter->second;
    }

    const TradableInfo* SymbolMapReader::getByMtsContract(const std::string& mts_contract, bool return_null) const {
        const auto iter = m_tradable_by_mts_contract.find(mts_contract);
        if (iter == m_tradable_by_mts_contract.end()) {
            if (return_null) return nullptr;
            logError("No tradable found for mts contract: %s!", mts_contract.c_str());
            throw std::runtime_error(std::string("tradable not found for mts contract ") + mts_contract);
        }
        return iter->second;
    }

    const std::vector<const TradableInfo*> SymbolMapReader::getAllByMtsVenue(const std::vector<std::string>& mts_venue, int max_n) const {
        std::set<std::string> vs (mts_venue.begin(), mts_venue.end());
        std::vector<const TradableInfo*> vec;
        for (const auto& kv:m_tradable) {
            if (vs.find(kv.second->_venue) != vs.end()) {
                if (kv.second->_N <= max_n) {
                    vec.push_back(kv.second);
                }
            }
        }
        return vec;
    }

    const std::vector<std::string> SymbolMapReader::getSubscriptions() const {
        bool found;
        std::vector<std::string> ret;

        // read the max_n
        int max_n = plcc_getInt("MaxN");
        const auto& va = plcc_getStringArr("MTSVenue", &found, std::vector<std::string>());
        const auto& sa = plcc_getStringArr("MTSSymbol", &found, std::vector<std::string>());

        const std::set<std::string> vas(va.begin(), va.end());
        const std::set<std::string> sas(sa.begin(), sa.end());

        for (const auto& kv:m_tradable) {
            const auto* t (kv.second);
            if (t->_N>max_n)
                continue;
            if ((vas.find(t->_venue)==vas.end()) && (sas.find(t->_symbol)==sas.end())) {
                continue;
            }
            ret.push_back(t->_mts_symbol);
        }
        return ret;
    }

    std::string SymbolMapReader::getTradableSymbol(const std::string& symbol) const {
        const auto* ti (getByTradable(symbol, true));
        if (__builtin_expect(!ti, 0)) {
            ti = getByMtsSymbol(symbol, true);
            if (!ti) {
                ti = getByTTSecId(symbol, true);
                if (!ti) {
                    ti = getByMtsContract(symbol, true);
                    if (!ti) {
                        logError("%s not found in symbol map under tradable, MTS Symbol, or TT Sec_Id!", symbol.c_str());
                        return "";
                    }
                }
            }
        }
        return ti->_tradable;
    }

    SymbolMapWriter::SymbolMapWriter() {};
    SymbolMapWriter::SymbolMapWriter(const std::string& cfg_file) {
        addTradable(SymbolMapReader::getFile(cfg_file));
    }
    SymbolMapWriter::~SymbolMapWriter() {
        clear();
    }

    void SymbolMapWriter::clear() {
        for (const auto&kv : m_tradable) {
            delete kv.second;
        }
        m_tradable.clear();
    }

    void SymbolMapWriter::addTradable(const TradableInfo* tradable) {
        m_tradable[tradable->_tradable] = tradable;
    }

    void SymbolMapWriter::addTradable(
            const std::string& tradable,
            const std::string& symbol,
            const std::string& exch_symbol,
            const std::string& mts_contract,
            const std::string& mts_symbol,
            const std::string& venue,
            const std::string& type,
            const std::string& contract_month,
            const std::string& tt_security_id, 
            const std::string& tt_venue,
            const std::string& currency,
            const std::string& expiration_date,
            double tick_size,
            double point_value,
            double px_multiplier,
            int N,
            int expiration_days,
            int lotspermin,
            bool subscribed) {
        addTradable(new TradableInfo(tradable, symbol, exch_symbol, mts_contract, mts_symbol, venue, type, contract_month, tt_security_id, tt_venue, currency, expiration_date, tick_size, point_value, px_multiplier, N, expiration_days, lotspermin, subscribed));
    }

    void SymbolMapWriter::addTradable(const SymbolMapReader& reader) {
        const auto& trd (reader.listAllTradable());
        for (const auto* t : trd) {
            addTradable(new TradableInfo(*t));
        }
    }

    void SymbolMapWriter::addTradable(const SymbolMapWriter& writer) {
        if (&writer == this) {
            return;
        }
        for (const auto& t : writer.m_tradable) {
            addTradable(new TradableInfo(*t.second));
        }
    }

    void SymbolMapWriter::delTradable(const std::string& tradable) {
        auto iter(m_tradable.find(tradable)) ; 
        if (iter != m_tradable.end()) {
            m_tradable.erase(iter);
        }
    }

    void SymbolMapWriter::toConfigFile(const std::string& cfg_file) const {
        try {
            utils::ConfigureReader cfg;
            auto* cfg_value = cfg.set("tradable");
            for (const auto& kv: m_tradable) {
                kv.second->toKeyValue(*cfg_value);
            }
            cfg.toFile(((cfg_file == "")? plcc_getString(SymbolMapConfig) : cfg_file).c_str());
        } catch (const std::exception& e) {
            logError("Error writing the symbol map: %s", e.what());
            throw e;
        }
    }
}
