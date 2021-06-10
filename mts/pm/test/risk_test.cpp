#include "RiskManager.h"
#include "stdio.h"
#include <string>
#include "gtest/gtest.h"
#include <cstdlib>
#include <fstream>

class MockPM {
public:
    MockPM(int strat_pos, int sym_pos) : _strat_pos (strat_pos), _sym_pos(sym_pos) {}

    int64_t getPosition(const std::string& strat, const std::string& tradable, void* ptr1, void* ptr2, int64_t* open_qty) const {
        *open_qty = -_strat_pos;
        return 2*_strat_pos;
    }

    int64_t getPosition(const std::string& tradable, void* ptr1, void* ptr2, int64_t* open_qty) const {
        *open_qty = -_sym_pos;
        return 2*_sym_pos;
    }
    uint64_t _strat_pos, _sym_pos;
};

class RiskFixture : public testing::Test {
public:
    RiskFixture ():
    _risk_cfg_str ( \
        "strat = {\n"
        "    CL001 = {\n"
        "        # symbol is a unique symbol in tradables\n"
        "        Symbols = {\n"
        "        WTI = {\n"
        "        \n"
        "            # BaseAllocation\n"
        "            position = 100\n"
        "\n"
        "            # Rate Limit - orders in previous seconds\n"
        "            # [ #Orders, #Seconds]\n"
        "            rate_limit = [ 10 , 10 ]\n"
        "\n"
        "        }\n"
        "        }\n"
        "        PaperTrading = 0\n"
        "    }\n"
        "}\n"
        " \n"
        "# Firm Risk\n"
        "mts = {\n"
        "    WTI = {\n"
        "        position = 500\n"
        "        order_size = 120\n"
        "    }\n"
        "}\n"
    ),
    _sym_cfg_str ( \
        "tradable = {\n"
        "    CLG0 = {\n"
        "        symbol = WTI\n"
        "        exch_symbol = CL\n"
        "        venue = NYM\n"
        "        mts_contract = CL_202002\n"
        "        tick_size = 0.010000000000\n"
        "        point_value = 1000.0\n"
        "        px_multiplier = 0.010000000000\n"
        "        type = FUT\n"
        "        contract_month = 202002\n"
        "        tt_security_id = 3971113080253836646\n"
        "        tt_venue = NYM\n"
        "        currency = USD\n"
        "        expiration_date = 2020-02-20\n"
        "        mts_symbol = WTI_N0\n"
        "        N = 0\n"
        "        expiration_days = 20\n"
        "    }\n"
        "    CLH0 = {\n"
        "        symbol = WTI\n"
        "        exch_symbol = CL\n"
        "        venue = NYM\n"
        "        mts_contract = CL_202003\n"
        "        tick_size = 0.010000000000\n"
        "        point_value = 1000.0\n"
        "        px_multiplier = 0.010000000000\n"
        "        type = FUT\n"
        "        contract_month = 202003\n"
        "        tt_security_id = 3971113080253836647\n"
        "        tt_venue = NYM\n"
        "        currency = USD\n"
        "        expiration_date = 2020-03-20\n"
        "        mts_symbol = WTI_N1\n"
        "        N = 1\n"
        "        expiration_days = 20\n"
        "    }\n"
        "}"),
    _risk_fn("/tmp/risk_test.cfg"),
    _sym_fn("/tmp/risk_test_sym.cfg"),
    _main_cfg_fn("/tmp/main.cfg")
    {
        {
            // write the main config to /tmp/main.cfg
            std::ofstream ofs;
            ofs.open (_main_cfg_fn, std::ofstream::out | std::ofstream::trunc);
            ofs << "SymbolMap = " << _sym_fn << std::endl;
            ofs << "Risk = " << _risk_fn << std::endl;
        }
        // set the config path NOW!
        utils::PLCC::setConfigPath(_main_cfg_fn);
    }

    void writeCfg() const {
        FILE* fp = fopen(_risk_fn, "w");
        fprintf(fp, "%s\n", _risk_cfg_str);
        fclose(fp);
        fp = fopen(_sym_fn, "w");
        fprintf(fp, "%s\n", _sym_cfg_str);
        fclose(fp);
    }

    const char* _risk_cfg_str, *_sym_cfg_str, *_risk_fn, *_sym_fn, *_main_cfg_fn;
};

TEST_F (RiskFixture, test) {
    // load the risk and check for the numbers

    writeCfg();
    auto& risk (pm::RiskManager::get());

    EXPECT_EQ(risk.stratMaxPosition("CL001", "WTI"), 100);
    EXPECT_EQ(risk.symbolMaxPosition("WTI"), 500);
    EXPECT_EQ(risk.symbolMaxOrdSize("WTI"), 120);

    // check for good and bad for both strat and engine level
    // check for fat finger at engine level
    //
    MockPM pm_str(1, 1);
    EXPECT_TRUE(risk.checkOrder("CL001", "CLH0", 98, pm_str));
    EXPECT_TRUE(risk.checkOrder("CL001", "CLG0", -102, pm_str));
    EXPECT_FALSE(risk.checkOrder("CL001", "CLH0", 99, pm_str)); // strat position fails
    EXPECT_FALSE(risk.checkOrder("CL001", "CLG0", -103, pm_str));

    MockPM pm_sym(0, 201);
    EXPECT_TRUE(risk.checkOrder("CL001", "CLG0", 98, pm_sym));
    EXPECT_TRUE(risk.checkOrder("CL001", "CLH0", -100, pm_sym));
    EXPECT_FALSE(risk.checkOrder("CL001", "CLG0", 99, pm_sym)); // engine level position fails

    MockPM pm_fat(-50, -50);
    EXPECT_TRUE(risk.checkOrder("CL001", "CLH0", 120, pm_fat));
    EXPECT_FALSE(risk.checkOrder("CL001", "CLG0", 121, pm_fat)); // fat finger fails

    // now the risk should check
    EXPECT_TRUE(risk.checkOrder("CL001", "CLH0", 1, pm_fat));
    EXPECT_FALSE(risk.checkOrder("CL001", "CLG0", 1, pm_fat)); // rate limit fails
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

