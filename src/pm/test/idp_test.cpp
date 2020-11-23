#include "ExecutionReport.h"
#include "PositionData.h"
#include "csv_util.h"
#include "time_util.h"
#include <cmath>
#include <iostream>

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

bool load_save() {
    // creat initial idp from list a er
    pm::IntraDayPosition idp("sym1","algo1");
    for (const auto& line : erlines) {
        idp.update(pm::ExecutionReport::fromCSVLine(line));

        //std::cout << "update from " << line << std::endl;
        //std::cout << idp.toString() << std::endl;

    }

    double vapref = 2.1275399999999998;
    if (!check(idp, -10, vapref)) {
        std::cout << "loaded idp from erlines, mismatch!" << std::endl;
        return false;
    }

    auto oolist = idp.listOO();
    if (oolist.size()!=1) {
        std::cout << "oolist size mismatch! " << idp.dumpOpenOrder() << std::endl;
        return false;
    }
    if ((oolist[0]->m_open_qty != 5) || (std::fabs(oolist[0]->m_open_px - 2.1219)>1e-10)) {
        std::cout << "oolist mismatch: " << idp.dumpOpenOrder() << std::endl;
        return false;
    }

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

    if ((qty != -9) || (std::fabs(vap-vapref)>1e-10) || (std::fabs(pnl-(vapref-3.0))>1e-10)) {
        std::cout << "position update mismatch with reference!" << std::endl;
        return false;
    }

    oolist = idp_newday.listOO();
    if ( (oolist.size()!=2) || 
         (oolist[0]->m_open_qty!=9) || (std::fabs(oolist[0]->m_open_px- 3.0) > 1e-10) ||
         (oolist[1]->m_open_qty!=5) || (std::fabs(oolist[1]->m_open_px-2.1219)>1e-10)
       ) 
    {
        std::cout << "oolist mismatch!" << std::endl;
        return false;
    }

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
        if (difflog.size()>0) {
            std::cout << "load mismatch with copy!" << std::endl;
            std::cout << difflog << std::endl;
            return false;
        }

        double pnlm2m = idpcopy.getMtmPnl(3.1);
        double pnlref = (vapref-3.0 + 9*(vapref-3.1));
        if (std::fabs(pnlm2m-pnlref)) {
            std::cout << "expect Mtm pnl "<< pnlref << " got " << pnlm2m << std::endl;
            return false;
        }

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
        if ( (qty!=0) || (std::fabs(pnl - pnlref)>1e-10) ) {
            std::cout << "qty or pnl mismatch after cover qty: " << qty << " pnl: " << pnl << "(" << pnlref << std::endl;;
            return false;
        }

        if (std::fabs(pnlref - idpcopy.getRealizedPnl())>1e-10) {
            std::cout << "realized pnl wrong after cover pnl: " << pnl << " pnlref " << pnlref << std::endl;
            return false;
        }

        if (idpload.hasPosition() || idpcopy.hasPosition()) {
            std::cout << "hasPosition() returned true after cover" << std::endl;
            return false;
        }

        if (idpcopy.listOO().size() != 1) {
            std::cout << "copied idp OO size wrong " << std::endl;
            return false;
        }

        if (idp.listOO().size() != 1) {
            std::cout << "original idp OO size wrong " << std::endl;
            return false;
        }

        // test construct
        pm::IntraDayPosition idptest2("sym1","algo1",10, (3.0+9*2.13)/10.0,10,vapref);

        difflog = idpcopy.diff(idptest2, true);
        if (difflog.size() > 0) {
            std::cout << "constructed idp mismatch the final cover: " << difflog << std::endl;
            return false;
        }

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
    if (oolist.size()!=1) {
        std::cout << "oolist size mismatch! " << idp.dumpOpenOrder() << std::endl;
        return false;
    }
    if ((oolist[0]->m_open_qty != 1) || (std::fabs(oolist[0]->m_open_px - 2.1)>1e-10)) {
        std::cout << "oolist mismatch: " << idp.dumpOpenOrder() << std::endl;
        return false;
    }
    if (oolist[0].use_count() != 2) {
        std::cout << "open order reference count mismatch! " << oolist[0].use_count() 
            << std::endl;
    }

    return true;
}

bool add_copy() {
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

    if (idp.getPosition() != -1) {
        std::cout << "add_copy position mismatch! " << std::endl;
        std::cout << idp.toString() << "\n" << idp.dumpOpenOrder() << std::endl;
        return false;
    }

    auto oovec = idp.listOO();
    if (oovec.size() != 3) {
        std::cout << "add_copy oo list size mismatch! 3 expected " << std::endl;
        std::cout << idp.toString() << "\n" << idp.dumpOpenOrder() << std::endl;
        return false;
    }

    if ((oovec[0]->m_open_qty != 9) ||
        (oovec[1]->m_open_qty != -7) ||
        (oovec[2]->m_open_qty != 5)) {
        std::cout << "add_copy oo open qty mistmach!" << std::endl;
        std::cout << idp.toString() << "\n" << idp.dumpOpenOrder() << std::endl;
        return false;
    }
    return true;
}


int main() {
    if (load_save() && add_copy()) {
        std::cout << "all good!" << std::endl;
    }
    return 0;
}
