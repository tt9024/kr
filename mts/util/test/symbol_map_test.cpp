#include "symbol_map.h"
#include "stdio.h"
#include <string>
#include "gtest/gtest.h"
#include <cstdlib>

class SymFixture : public testing::Test {
public:
    SymFixture ():
    _cfg_str ( \
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
        "        expiration_date = 2020-01-20\n"
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
        "    BRN FMG0020! = {\n"
        "        symbol = Brent\n"
        "        exch_symbol = R\n"
        "        venue = IFEU\n"
        "        mts_contract = Brent_202002\n"
        "        tick_size = 0.010000000000\n"
        "        point_value = 1000.0\n"
        "        px_multiplier = 1.000000000000\n"
        "        type = FUT\n"
        "        contract_month = 202002\n"
        "        tt_security_id = 3971113080253836648\n"
        "        tt_venue = ICE_L\n"
        "        currency = USD\n"
        "        expiration_date = 2020-02-20\n"
        "        mts_symbol = Brent_N0\n"
        "        N = 0\n"
        "        expiration_days = 5\n"
        "    }\n"
        "}"),
    _fn("/tmp/cfg_test.cfg") 
    {}

    void writeCfg() const {
        FILE* fp = fopen(_fn, "w");
        fprintf(fp, "%s\n", _cfg_str);
        fclose(fp);
    }

    template<typename T>
    void testReader(const T& sr) const {
        const auto* trd = sr.getByMtsSymbol("WTI_N1");
        EXPECT_STREQ(trd->_tradable.c_str(), "CLH0");
        EXPECT_STREQ(trd->_mts_contract.c_str(), "CL_202003");
        EXPECT_STREQ(trd->_symbol.c_str(), "WTI");
        EXPECT_STREQ(trd->_exch_symbol.c_str(), "CL");
        EXPECT_STREQ(trd->_contract_month.c_str(), "202003");
        EXPECT_STREQ(trd->_tt_security_id.c_str(), "3971113080253836647");
        EXPECT_STREQ(trd->_currency.c_str(), "USD");
        EXPECT_STREQ(trd->_expiration_date.c_str(), "2020-03-20");
        EXPECT_STREQ(trd->_type.c_str(), "FUT");
        EXPECT_DOUBLE_EQ(trd->_px_multiplier, 0.01);

        trd = sr.getByTradable("BRN FMG0020!");
        EXPECT_STREQ(trd->_tradable.c_str(), "BRN FMG0020!");
        EXPECT_STREQ(trd->_mts_contract.c_str(), "Brent_202002");
        EXPECT_STREQ(trd->_symbol.c_str(), "Brent");
        EXPECT_STREQ(trd->_exch_symbol.c_str(), "R");
        EXPECT_STREQ(trd->_venue.c_str(), "IFEU");
        EXPECT_STREQ(trd->_mts_symbol.c_str(), "Brent_N0");
        EXPECT_STREQ(trd->_contract_month.c_str(), "202002");
        EXPECT_DOUBLE_EQ(trd->_point_value, 1000.0);
        EXPECT_DOUBLE_EQ(trd->_tick_size, 0.01);
        EXPECT_EQ(trd->_expiration_days, 5);
        EXPECT_EQ(trd->_N, 0);

        const auto& tv = sr.getAllBySymbol("WTI");
        EXPECT_EQ(tv.size(), 2);
        EXPECT_STREQ(tv[0]->_tradable.c_str(), "CLG0");
        EXPECT_STREQ(tv[1]->_tradable.c_str(), "CLH0");

        const auto& tv2 = sr.getAllByTradable("BRN FMG0020!");
        EXPECT_EQ(tv2.size(), 1);
        EXPECT_STREQ(tv2[0]->_mts_symbol.c_str(), "Brent_N0");
    }

    const char* _cfg_str;
    const char* _fn;
};

TEST_F (SymFixture, test) {
    // load the file and write to it
    writeCfg();
    const auto& sr (utils::SymbolMapReader::getFile(_fn));
    testReader(sr);

    // update it and write and load it and compare
    //
    utils::SymbolMapWriter writer(_fn);
    writer.toConfigFile(_fn);
    const auto& sr2 = utils::SymbolMapReader::getFile(_fn);
    testReader(sr2);

    writer.clear();
    writer.addTradable(sr2);
    writer.toConfigFile(_fn);
    const auto& sr3 = utils::SymbolMapReader::getFile(_fn);
    testReader(sr3);

    utils::SymbolMapWriter writer2;
    writer2.addTradable(sr2.getByMtsSymbol("Brent_N0"));
    writer2.addTradable(writer);
    writer2.toConfigFile(_fn);
    const auto& sr4 = utils::SymbolMapReader::getFile(_fn);
    testReader(sr4);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

