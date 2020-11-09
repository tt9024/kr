#include "PositionManager.h"
#include "csv_util.h"
#include "time_util.h"
#include <iostream>
#include <cmath>

utils::CSVUtil::FileTokens erlines = {
    {"sym1", "algo1","cid1", "eid1", "0","-10","3.0", "20201004-18:33:02","", "1601850782023138"},
    {"sym1", "algo1","cid1", "eid2", "1","-5" ,"3.0", "20201004-18:33:02","", "1601850782823033"},
    {"sym1", "algo1","cid2", "eid3", "0","5"  ,"2", "20201004-18:33:08","", "1601850788504031"},
    {"sym1", "algo1","cid2", "eid4", "4","0"  ,"0"     , "20201004-18:33:08","", "1601850788591032"},
    {"sym1", "algo1","cid1", "eid5", "2","-5" ,"1.0"  , "20201004-18:33:09","", "1601850789504031"},
    {"sym1", "algo2","cid3", "eid6", "0","5"  ,"3.0", "20201004-18:33:08","", "1601850788504031"},
    {"sym1", "algo2","cid3", "eid7", "1","1"  ,"3.0", "20201004-18:33:08","", "1601850788504031"},
    {"sym2", "algo1","cid4", "eid8", "0","-5"  ,"10.1", "20201004-18:33:08","", "1601850788504031"},
    {"sym2", "algo1","cid4", "eid9", "1","-1"  ,"10.1", "20201004-18:33:08","", "1601850788504031"}
    //,{"sym1", "algo2","cid3", "eid10", "4","0"  ,"0", "20201004-18:33:08","", "1601850788504031"}
};

bool load_save() {
    utils::CSVUtil::FileTokens erlines0;
    utils::CSVUtil::write_file(erlines0, "./eod_pos.csv",false);

    pm::PositionManager pmgr("testpm", ".");
    utils::CSVUtil::write_file(erlines, "recovery.csv",false);
    pmgr.loadRecovery("recovery.csv");

    pmgr.persist();
    pm::PositionManager pmgr2("testpm2", ".");

    std::cout << pmgr.toString() << std::endl;
    std::cout << pmgr2.toString() << std::endl;

    std::string difflog = pmgr.diff(pmgr2);
    if (difflog.size() != 0) {
        std::cout << "save-load mismatch!" << std::endl;
        std::cout << difflog << std::endl;
        return false;
    }

    utils::CSVUtil::FileTokens erlines_update = {
    {"sym1", "algo1","cid5", "eid11", "0","10","3.0", "20201004-18:33:02","", "1601850782023138"},
    {"sym1", "algo1","cid5", "eid12", "1","5" ,"3.0", "20201004-18:33:02","", "1601850782823033"}
    };

    for (const auto& line : erlines_update) {
        pmgr.update(pm::ExecutionReport::fromCSVLine(line));
    }

    // get position of sym1 algo1
    double vap, pnl;
    int64_t oqty;
    int64_t qty = pmgr.getPosition("algo1", "sym1", &vap, &pnl, &oqty);
    if ((qty != -5) || (std::fabs(vap-2.0)>1e-10) || (std::fabs(pnl+5.0)>1e-10) || (oqty != 5)) {
        std::cout << "getPosition algo1 sym1 mismatch!" << std::endl;
        std::cout << pmgr.toString() << std::endl;
        return false;
    }

    // get Position of sym1
    // algo1: short 5 @ 2.0, 
    // algo2: long 1 @ 3.0
    oqty=0;
    qty = pmgr.getPosition("sym1", &vap, &pnl, &oqty);
    if ( (qty!=-4) || (std::fabs(vap-2.0)>1e-10) || (std::fabs(pnl+6)>1e-10) || (oqty != 9) ) {
        std::cout << "getPosition sym1 mismatch!" << std::endl;
        std::cout << pmgr.toString() << std::endl;
        return false;
    }

    // list Positions of algo1
    // sym1: -5, 2.0 -5.0  oo(5, 3.0)
    // sym2: -1  10.1 0.0  oo(-4, 10.1)
    const std::string algo1 ("algo1");
    auto idpvec = pmgr.listPosition(&algo1);
    if ((idpvec.size()!=2) || 
        (idpvec[0]->getPosition() != -5) || 
        (idpvec[1]->getPosition() != -1)) {
        std::cout << "listPoisition mismatch!" << std::endl;
        for (auto& idp:idpvec) {
            std::cout << idp->toString() << std::endl;
        }
        return false;
    }

    // 
    // list OO (2)
    
    auto oovec = pmgr.listOO(&algo1);
    if ( (oovec.size()!= 2)  ||
         (oovec[0]->m_open_qty != 5) ||
         (oovec[1]->m_open_qty != -4) ||
         std::fabs((oovec[1]->m_open_px - 10.1) > 1e-10) ) {
        std::cout << "listOO mismatch!" << std::endl;
        for (auto& oo : oovec) {
            std::cout << oo->toString() << std::endl;
        }
        return false;
    }

    // reconcile
    utils::CSVUtil::FileTokens erlines_update2 = {
    {"sym1", "algo1","cid5", "eid12", "1","5" ,"3.0", "20201004-18:33:02","", "1601850782823033"}, // a duplicate fill
    {"sym2", "algo1","cid4", "eid13", "2","-4","10.1", "20201004-18:33:02","", "1601850782023138"}
    };

    utils::CSVUtil::write_file(erlines_update, "./recovery.csv",false);
    utils::CSVUtil::write_file(erlines_update2,"./recovery.csv",true);


    bool ret=pmgr.reconcile("recovery.csv", difflog, true);
    std::cout << "reconcile diff: " << difflog << std::endl;
    if (ret) {
        std::cout << "reconcile mismatch!" << std::endl;
        return false;
    }

    // list Positions of algo1
    // sym1: -5, 2.0 -5.0  oo(5, 3.0)
    // sym2: -5  10.1 0.0  oo()
    idpvec = pmgr.listPosition(&algo1);
    if ((idpvec.size()!=2) || 
        (idpvec[0]->getPosition() != -5) || 
        (idpvec[1]->getPosition() != -5)) {
        std::cout << "reconciled listPoisition mismatch!" << std::endl;
        for (auto& idp:idpvec) {
            std::cout << idp->toString() << std::endl;
        }
        return false;
    }

    // 
    // list OO (1)
    
    oovec = pmgr.listOO(&algo1);
    if ( (oovec.size()!= 1)  ||
         (oovec[0]->m_open_qty != 5) ||
         std::fabs((oovec[0]->m_open_px - 3.0) > 1e-10) ) {
        std::cout << "listOO mismatch!" << std::endl;
        for (auto& oo : oovec) {
            std::cout << oo->toString() << std::endl;
        }
        return false;
    }

    // get pnl
    pnl = pmgr.getPnl(&algo1);
    if (std::fabs(pnl+5.0)>1e-10) {
        std::cout << "reconcile pnl mismatch! " << pnl << std::endl;
        return false;
    }


    idpvec = pmgr.listPosition();
    if (idpvec.size()!= 3) {
        std::cout << "reconcile position mismatch! " << std::endl;
        for (auto& idp : idpvec) {
            std::cout << idp->toString() << std::endl;
        }
        return false;
    }
    return true;
};

int main() {
    if (load_save()) {
        std::cout << "all good!" << std::endl;
    }
    return 0;
}
