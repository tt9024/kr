#pragma once

#include <unordered_map>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <set>
#include "plcc/PLCC.hpp"
#include "rate_limiter.h"
#include "symbol_map.h"

namespace pm {
    class RiskManager {
    public:
        static RiskManager& get(bool reload=false);
        uint64_t stratMaxPosition (const std::string& strat, const std::string& symbol) const;
        uint64_t symbolMaxPosition(const std::string& symbol) const;
        uint64_t symbolMaxOrdSize(const std::string& symbol) const;

        // The single checking function, including 
        //   * rate check, 
        //   * fat-finger check, 
        //   * position checks on strategy and engine level
        template<typename PositionManager>
        bool checkOrder (const std::string& strat, 
                         const std::string& tradable_symbol, 
                         int64_t ord_qty,
                         const PositionManager& pm);

        bool isPaperTrading(const std::string& strat_name) const;
        static std::string ManualStrategyName();
        static bool isManualStrategy(const std::string& strat_name);

    private:
        RiskManager();
        RiskManager(const RiskManager& rm) = delete;
        void operator = (const RiskManager& rm) = delete;

        void load(const std::string& risk_cfg = "");
        bool rateCheck(const std::string& strat, const std::string& symbol);
        bool oneSecondThrottle(const std::string& strat, const std::string& symbol);

        using MaxPosMap = std::unordered_map<std::string, uint64_t>;
        std::unordered_map<std::string, MaxPosMap> m_strat_max_pos;
        MaxPosMap m_engine_max_pos;
        std::unordered_map<std::string, uint64_t> m_engine_max_ord_size;
        std::unordered_map<std::string, utils::RateLimiter> m_rateLimiter;
        std::set<std::string> m_paper_trading_strats;
        std::unordered_map<std::string, time_t> _logThrottle;
    };
}

// template and inline
namespace pm {

    inline
    RiskManager& RiskManager::get(bool reload) {
        static RiskManager _risk;
        if (reload) {
            _risk.load();
        }
        return _risk;
    }

    inline
    uint64_t RiskManager::stratMaxPosition (const std::string& strat, const std::string& symbol) const {
        return m_strat_max_pos.at(strat).at(symbol);
    }

    inline
    uint64_t RiskManager::symbolMaxPosition(const std::string& symbol) const {
        return m_engine_max_pos.at(symbol);
    }

    inline
    uint64_t RiskManager::symbolMaxOrdSize(const std::string& symbol) const {
        return m_engine_max_ord_size.at(symbol);
    }

    inline
    bool RiskManager::oneSecondThrottle(const std::string& strat, const std::string& symbol) {
        // (approximately) throttle at 1 per second
        time_t utc = utils::TimeUtil::cur_utc();
        const auto key (strat+symbol);
        auto iter = _logThrottle.find(key);
        if (iter != _logThrottle.end()) {
            if (utc == iter->second) {
                return true;
            }
            iter->second = utc;
        } else {
            _logThrottle[key]=utc;
        }
        return false;
    }

    // The single checking function, including 
    //   * rate check, 
    //   * fat-finger check, 
    //   * position checks on strategy and engine level
    template<typename PositionManager>
    bool RiskManager::checkOrder (const std::string& strat, 
                     const std::string& tradable_symbol, 
                     int64_t ord_qty,
                     const PositionManager& pm)
    {
        // bypass manual trade
        if (isManualStrategy(strat)) {
            return true;
        }
        const auto& symbol = utils::SymbolMapReader::get().getByTradable(tradable_symbol)->_symbol;

        // check rate
        if (!rateCheck(strat, symbol)) {
            if (!oneSecondThrottle(strat, tradable_symbol)) {
                logError("Risk Check Failed! Trading too hot, slow down %s on %s!", strat.c_str(), symbol.c_str());
            }
            return false;
        }

        // fat-finger check
        if (m_engine_max_ord_size[symbol] < (uint64_t) std::abs(ord_qty)) {
            logError("Risk Check Failed! Order size (%lld) too big for %s(%s), take it easy %s!",
                    (long long) ord_qty, tradable_symbol.c_str(), symbol.c_str(), strat.c_str());
            return false;
        }

        // position check
        // The check is on the symbol level, instead of the given tradable_symbol.
        // For example, if the tradable_symbol is ESH1, we need to look for
        // the total position of ES in this stratgy, as well as all strategies. 
       
        const auto& tiarr = utils::SymbolMapReader::get().getAllBySymbol(symbol);
        // This is the array of all tradables with same symbol of the given tradable
        // Two quantities needs to be calculated: 
        // 1. the total position for this strategy, used by the strategy risk
        // 2. the total position, used by firm wide risk
        
        int64_t strat_qty = 0, engine_qty = 0, open_qty;
        for (const auto* ti: tiarr) {
            const auto& tradable_symbol = ti->_tradable;

            // for all strategies (including the current open positions)
            engine_qty += pm.getPosition(tradable_symbol, nullptr, nullptr, &open_qty);
            engine_qty += open_qty;

            // for this strategy (including the current open positions)
            open_qty = 0;
            strat_qty += pm.getPosition(strat, tradable_symbol, nullptr, nullptr, &open_qty);
            strat_qty += open_qty;
        }

        // strategy position check
        if (m_strat_max_pos[strat][symbol] < (uint64_t) std::abs(strat_qty + ord_qty)) {
            // we should still allow for reducing
            if ((strat_qty + ord_qty) * ord_qty > 0) {
                logError("Risk Check Failed! Strategy %s max position %lld breached"
                        " by a trade of %lld on top of current position of %lld!", 
                        strat.c_str(), (long long) m_strat_max_pos[strat][symbol],
                        (long long) ord_qty, (long long)strat_qty);
                return false;
            } else {
                logInfo("Risk Check Allowing Reducing: Strategy %s max position %lld breached"
                        " but will be reduced by a trade of %lld on top of current position of %lld!", 
                        strat.c_str(), (long long) m_strat_max_pos[strat][symbol],
                        (long long) ord_qty, (long long)strat_qty);
            }
        }

        // engine level check
        if (m_engine_max_pos[symbol] < (uint64_t) std::abs(engine_qty + ord_qty)) {
            // we should still allow for reducing
            if ((engine_qty + ord_qty) * ord_qty > 0) {
                logError("Risk Check Failed! Engine max position %lld breached"
                        " by %s trading %lld on top of current position of %lld!", 
                        (long long) m_engine_max_pos[symbol],
                        strat.c_str(), (long long) ord_qty, (long long)engine_qty);
                return false;
            } else {
                logInfo("Risk Check Allowing Reducing: Engine max position %lld breached"
                        " but will be reduced by %s trading %lld on top of current position of %lld!", 
                        (long long) m_engine_max_pos[symbol],
                        strat.c_str(), (long long) ord_qty, (long long)engine_qty);

            }
        }

        // good to go!
        return true;
    }

    inline
    bool RiskManager::isPaperTrading(const std::string& strat_name) const {
        return m_paper_trading_strats.count(strat_name);
    }

    inline
    RiskManager::RiskManager() {
        load();
    };
}
