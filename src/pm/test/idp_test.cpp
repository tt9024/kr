#include "ExecutionReport.h"
#include "PositionData.h"
#include "csv_util.h"
#include "time_util.h"
#include <cmath>
#include <iostream>
#include "gtest/gtest.h"

utils::CSVUtil::FileTokens erlines = {
    {"sym1", "algo1","cid1", "eid1", "0","-10","2.1218", "20201004-18:33:02","", "1601850782023138"},
    {"sym1", "algo1","cid1", "eid2", "1","-3" ,"2.1218", "20201004-18:33:02","", "1601850782823033"},
    {"sym1", "algo1","cid2", "eid3", "0","5"  ,"2.1219", "20201004-18:33:08","", "1601850788504031"},
    {"sym1", "algo1","cid2", "eid4", "4","0"  ,"0"     , "20201004-18:33:08","", "1601850788591032"},
    {"sym1", "algo1","cid1", "eid5", "2","-7" ,"2.13"  , "20201004-18:33:09","", "1601850789504031"},
    {"sym1", "algo1","cid3", "eid6", "0","5"  ,"2.1219", "20201004-18:33:08","", "1601850788504031"}
};

bool check(const pm::IntraDayPosition& idp, int64_t qty, double vap) {
    pm::IntraDayPosition idp2("sym1","algo1",qty,vap);
    std::string difflog = idp.diff(idp2);
    if(difflog.size()>0) {
        std::cout << difflog << std::endl;
        return false;
    }
    return true;
};

TEST (IDPTest, LoadSave) {
    // creat initial idp from list a er
    pm::IntraDayPosition idp("sym1","algo1");
    for (const auto& line : erlines) {
        idp.update(pm::ExecutionReport::fromCSVLine(line));

        //std::cout << "update from " << line << std::endl;
        //std::cout << idp.toString() << std::endl;

    }

    double vapref = 2.1275399999999998;
    EXPECT_TRUE(check(idp, -10, vapref)) << "loaded idp from erlines, mismatch!";

    auto oolist = idp.listOO();
    EXPECT_EQ(oolist.size(), 1) << "oolist size mismatch! " << idp.dumpOpenOrder();

    EXPECT_FALSE((oolist[0]->m_open_qty != 5) || (std::fabs(oolist[0]->m_open_px - 2.1219)>1e-10)) << "oolist mismatch: " << idp.dumpOpenOrder();

    // save to csv and load from csv
    auto posline = idp.toCSVLine();
    pm::IntraDayPosition* idp_newday_ptr = new pm::IntraDayPosition(posline);
    pm::IntraDayPosition& idp_newday(*idp_newday_ptr);

    // update with more execution reports
    utils::CSVUtil::FileTokens erlines_update = {
        {"sym1", "algo1","cid1", "eid1", "0","10","3.0", "20201004-18:33:02","", "1601850782023138"},
        {"sym1", "algo1","cid1", "eid2", "1","1" ,"3.0", "20201004-18:33:02","", "1601850782823033"},
        {"sym1", "algo1","cid2", "eid3", "0","5"  ,"2.1219", "20201004-18:33:08","", "1601850788504031"}
    };

    for (const auto& line: erlines_update) {
        idp_newday.update(pm::ExecutionReport::fromCSVLine(line));
        idp.update(pm::ExecutionReport::fromCSVLine(line));
    };

    std::cout << "update from new day position with erlines_update, got" << std::endl;
    std::cout << idp_newday.toString() << std::endl;
    std::cout << idp_newday.dumpOpenOrder() << std::endl;

    // check the result after the updates
    int64_t qty;
    double vap, pnl;
    qty = idp_newday.getPosition(&vap, &pnl);

    EXPECT_FALSE ((qty != -9) || (std::fabs(vap-vapref)>1e-10) || (std::fabs(pnl-(vapref-3.0))>1e-10)) << "position update mismatch with reference!";

    oolist = idp_newday.listOO();
    EXPECT_FALSE ( (oolist.size()!=2) ||
         (oolist[0]->m_open_qty!=9) || (std::fabs(oolist[0]->m_open_px- 3.0) > 1e-10) ||
         (oolist[1]->m_open_qty!=5) || (std::fabs(oolist[1]->m_open_px-2.1219)>1e-10) ) <<  "oolist mismatch!" << std::endl;

    idp_newday = idp; // idp has one more oo at cid3

    {
        // compare the constructor from csv vs from copy constructor
        auto eodline = idp_newday.toCSVLine();
        pm::IntraDayPosition idpload(eodline);  // idpload without oo, pnl
        pm::IntraDayPosition* idpcopy_ptr = new pm::IntraDayPosition(idp_newday);
        pm::IntraDayPosition& idpcopy = *idpcopy_ptr;

        std::cout << "new day loaded: " << idpload.toString() << std::endl;
        std::cout << "new day copy: " << idpcopy.toString() << std::endl;

        std::string difflog = idpload.diff(idpcopy);
        EXPECT_FALSE (difflog.size()>0) << "load mismatch with copy!" << std::endl << difflog << std::endl;

        double pnlm2m = idpcopy.getMtmPnl(3.1);
        double pnlref = (vapref-3.0 + 9*(vapref-3.1));
        EXPECT_FALSE (std::fabs(pnlm2m-pnlref)) << "expect Mtm pnl "<< pnlref << " got " << pnlm2m;

        // update idpcopy with the coverings, but 
        utils::CSVUtil::FileTokens erlines_cover = {
            {"sym1", "algo1","cid1", "eid5", "2","9" ,"2.13"  , "20201004-18:33:09","", "1601850789504031"},
            {"sym1", "algo1","cid2", "eid4", "4","0"  ,"0"     , "20201004-18:33:08","", "1601850788591032"}
        };

        for (const auto& line: erlines_cover) {
            //idpload will have oo not found warning
            idpload.update(pm::ExecutionReport::fromCSVLine(line));
            idpcopy.update(pm::ExecutionReport::fromCSVLine(line));
        }

        qty = idpcopy.getPosition(&vap, &pnl);
        pnlref = vapref-3.0 + 9*(vapref-2.13);
        EXPECT_FALSE ( (qty!=0) || (std::fabs(pnl - pnlref)>1e-10) ) << "qty or pnl mismatch after cover qty: " << qty << " pnl: " << pnl << "(" << pnlref;

        EXPECT_FALSE(std::fabs(pnlref - idpcopy.getRealizedPnl())>1e-10) << "realized pnl wrong after cover pnl: " << pnl << " pnlref " << pnlref;

        EXPECT_FALSE (idpload.hasPosition() || idpcopy.hasPosition()) << "hasPosition() returned true after cover";

        EXPECT_FALSE (idpcopy.listOO().size() != 1) << "copied idp OO size wrong ";

        EXPECT_FALSE (idp.listOO().size() != 1) << "original idp OO size wrong ";

        // test construct
        pm::IntraDayPosition idptest2("sym1","algo1",10, (3.0+9*2.13)/10.0,10,vapref);

        difflog = idpcopy.diff(idptest2, true);
        EXPECT_FALSE (difflog.size() > 0) << "constructed idp mismatch the final cover: " << difflog;

        // test the oo updaet on copy would be reflected on original
        utils::CSVUtil::FileTokens erlines_oo = {
            {"sym1", "algo1","cid3", "eid7", "3","5"  ,"2.1219", "20201004-18:33:08","", "1601850788504031"},
            {"sym1", "algo1","cid4", "eid8", "0","1"  ,"2.1", "20201004-18:33:08","", "1601850788504031"}
        };
        for (const auto& line: erlines_oo) {
            idp.update(pm::ExecutionReport::fromCSVLine(line));
        }
        delete idpcopy_ptr;
    };

    // check the validity of idp after deleting idp_newday
    delete idp_newday_ptr;

    oolist = idp.listOO();
    EXPECT_FALSE (oolist.size()!=1) << "oolist size mismatch! " << idp.dumpOpenOrder();
    EXPECT_FALSE  ((oolist[0]->m_open_qty != 1) || (std::fabs(oolist[0]->m_open_px - 2.1)>1e-10)) << "oolist mismatch: " << idp.dumpOpenOrder();
    EXPECT_FALSE (oolist[0].use_count() != 2) << "open order reference count mismatch! " << oolist[0].use_count();
};

TEST (IDPTest, AddLoad) {
    pm::IntraDayPosition idp("sym1","algo1");
    utils::CSVUtil::FileTokens erlines_update1 = {
        {"sym1", "algo1","cid1", "eid1", "0","10","3.0", "20201004-18:33:02","", "1601850782023138"},
        {"sym1", "algo1","cid1", "eid2", "1","1" ,"3.0", "20201004-18:33:02","", "1601850782823033"},
    };
    for (const auto& line : erlines_update1) {
        idp.update(pm::ExecutionReport::fromCSVLine(line));
    }

    pm::IntraDayPosition* idp_newday_ptr = new pm::IntraDayPosition(idp);
    pm::IntraDayPosition& idp_newday(*idp_newday_ptr);

    // update with more execution reports
    utils::CSVUtil::FileTokens erlines_update2 = {
        {"sym1", "algo1","cid2", "eid3", "0","-10","3.0", "20201004-18:33:02","", "1601850782023138"},
        {"sym1", "algo1","cid2", "eid4", "1","-3" ,"3.0", "20201004-18:33:02","", "1601850782823033"},
        {"sym1", "algo1","cid3", "eid5", "0","5"  ,"4.0", "20201004-18:33:08","", "1601850788504031"}
    };
    for (const auto& line: erlines_update2) {
        idp_newday.update(pm::ExecutionReport::fromCSVLine(line));
    };
    idp+=idp_newday;
    delete idp_newday_ptr;

    EXPECT_FALSE (idp.getPosition() != -1) << "add_copy position mismatch! " << std::endl << idp.toString() << "\n" << idp.dumpOpenOrder();

    auto oovec = idp.listOO();
    EXPECT_FALSE (oovec.size() != 3) << "add_copy oo list size mismatch! 3 expected " << std::endl << idp.toString() << "\n" << idp.dumpOpenOrder();

    EXPECT_FALSE ((oovec[0]->m_open_qty != 9) ||
        (oovec[1]->m_open_qty != -7) ||
        (oovec[2]->m_open_qty != 5)) << "add_copy oo open qty mistmach!" << std::endl << idp.toString() << "\n" << idp.dumpOpenOrder() ;
};


int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
