#include "RiskManager.h"

namespace pm {
void RiskManager::load(const std::string& risk_cfg) {
    // clear everything
    m_strat_max_pos.clear();
    m_engine_max_pos.clear();
    m_engine_max_ord_size.clear();

    // get the risk config
    
    std::string cfg = risk_cfg;
    if (risk_cfg.size() == 0) {
        cfg = plcc_getString("Risk");
    }
    logInfo("Using risk config file %s", cfg.c_str());
    const utils::ConfigureReader reader(cfg.c_str());

    // read the strat level risk
    const auto& strats (reader.getReader("strat"));
    const auto& names (strats.listKeys());
    for (const auto& sn : names) {
        const auto& strat(strats.getReader(sn));

        // getting the paper trading settings for the strategy sn
        int paper_trading = strat.get<int>("PaperTrading");
        // 0 - live trading
        // 1 - paper trading
        if (paper_trading) {
            m_paper_trading_strats.insert(sn);
        }

        // getting all the symbols positions for sn
        const auto& symbols(strat.getReader("Symbols"));
        const auto& symbol_list (symbols.listKeys());
        for (const auto& sym : symbol_list) {
            const auto& strat_sym(symbols.getReader(sym));
            const auto pos = strat_sym.get<double>("position");
            m_strat_max_pos[sn][sym] = (uint64_t) (pos+0.5);
            const auto rl = strat_sym.getArr<int>("rate_limit");
            m_rateLimiter.try_emplace(sn+"_"+sym, rl[0], rl[1]);
        }
    }

    // read the engine level risk
    const auto& engine (reader.getReader("mts"));
    const auto& syms (engine.listKeys());
    for (const auto& sym : syms) {
        const auto& srisk (engine.getReader(sym));
        auto iter = m_engine_max_pos.find(sym);
        if (iter != m_engine_max_pos.end()) {
            logError("Firm risk for %s already defined!", sym.c_str());
            throw std::runtime_error("Firm risk for " + sym + " already defined!");
        }
        m_engine_max_pos[sym] = (uint64_t)srisk.get<long long>("position");
        m_engine_max_ord_size[sym] = (uint64_t)srisk.get<long long>("order_size");
    }
}

bool RiskManager::rateCheck(const std::string& strat, const std::string& symbol) {
    auto iter = m_rateLimiter.find(strat+"_"+symbol);
    if (__builtin_expect(iter == m_rateLimiter.end(), 0)) {
        //logError("risk not found for strat %s", strat.c_str());
        return false;
    };
    return (iter->second.check(0) == 0);
}

std::string RiskManager::ManualStrategyName() {
    return "TSC-7000-1";
}

bool RiskManager::isManualStrategy(const std::string& strat_name) {
    return strat_name == ManualStrategyName();
}

}  // namespace pm
