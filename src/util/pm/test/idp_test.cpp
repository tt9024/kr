#include "PositionData.h"
#include "time_util.h"

utils::CSVUtil::FileTokens erlines = {
    {"sym1", "algo1","cid1", "eid1", "0","-10","2.1218", "20201004-18:33:02","", "1601850782023138"},
    {"sym1", "algo1","cid1", "eid2", "1","-3" ,"2.1218", "20201004-18:33:02","", "1601850782823033"},
    {"sym1", "algo1","cid2", "eid3", "0","5"  ,"2.1219", "20201004-18:33:08","", "1601850788504031"},
    {"sym1", "algo1","cid2", "eid4", "4","0"  ,"0"     , "20201004-18:33:08","", "1601850788591032"},
    {"sym1", "algo1","cid1", "eid5", "2","-7" ,"2.13"  , "20201004-18:33:09","", "1601850789504031"}
};

bool check(const pm::IntraDayPosition& idp, int64_t qty, double vap) {
    pm::IntraDayPosition idp2("sym1","algo1",qty,vap);
    std::string difflog = idp.diff(idp2);
    if(difflog.size()>0) {
        std::cout << difflog << std::endl;
        return false;
    }
    return true;
}

bool load_save() {
    pm::IntraDayPosition idp("sym1","algo1");
    for (const auto& line : erlines) {
        idp.update(pm::ExecutionReport::fromCSVLine(line));
    }

    double vapref = 2.1205399999999996;
    if (!check(idp, -10, vapref)) {
        std::cout << "loaded idp from erlines, mismatch!" << std::endl;
        return false;
    }

    auto oolist = idp.listOO();
    if (oolist.size()!=0) {
        std::cout << "oolist not empty! " << idp.dumpOpenOrder() << std::endl;
        return false;
    }

    auto posline = idp.toCSVLine();
    pm::IntraDayPosition idp_newday(posline);

    utils::CSVUtil::FileTokens erlines_update = {
        {"sym1", "algo1","cid1", "eid1", "0","10","3.0", "20201004-18:33:02","", "1601850782023138"},
        {"sym1", "algo1","cid1", "eid2", "1","1" ,"3.0", "20201004-18:33:02","", "1601850782823033"},
        {"sym1", "algo1","cid2", "eid3", "0","5"  ,"2.1219", "20201004-18:33:08","", "1601850788504031"}
    };

    for (const auto& line: erlines_update) {
        idp_newday.update(pm::ExecutionReport::fromCSVLine(line));
    };

    std::cout << "update from new day position with erlines_update, got" << std::endl;
    std::cout << idp_newday.toString() << std::endl;
    std::cout << idp_newday.dumpOpenOrder() << std::endl;

    int64_t qty;
    double vap, pnl;
    qty = idp_newday.getPosition(&vap, &pnl);

    if ((qty != 9) || (std::fabs(vap-vapref)>1e-10) || (std::fabs(pnl-(vapref-3.0))>1e-10)) {
        std::cout << "position update mismatch with reference!" << std::endl;
        return false;
    }

    oolist = idp_newday.listOO();
    if ( (oolist.size()!=2) || 
         (oolist[0]->m_open_qty!=9) || (std::fabs(oolist[0]->m_open_px- 3.0) > 1e-10) ||
         (oolist[1]->m_open_qty!=5) || (std::fabs(oolist[1]->m_open_px-2.1219)>1e-10)) {
        std::cout << "oolist mismatch!" << std::endl;
        return false;
    }

    auto eodline = idp_newday.toDSVLine();

    pm::IntraDayPosition idpload(eodline);
    pm::IntraDayPosition idpcopy = idp_newday;
    std::cout << "new day loaded: " << idpload.toString() << std::endl;
    std::cout << "new day copy: " << idpcopy.toString() << std::endl;

    std::string difflog idpload.diff(idpcopy);
    if (difflog.size()>0) {
        std::cout << "load mismatch with copy!" << std::endl;
        std::cout << difflog << std::endl;
        return false;
    }

    double pnlm2m = idpload.getMtmPnl(3.1);
    double pnlref = (vapref-3.0 + 9*(vapref-3.1));
    if (std::fabs(pnlm2m-pnlref)) {
        std::cout << "expect Mtm pnl "<< pnlref << " got " << pnlm2m << std::endl;
        return false;
    }

    utils::CSVUtil::FileTokens erlines_cover = {
        {"sym1", "algo1","cid1", "eid5", "2","9" ,"2.13"  , "20201004-18:33:09","", "1601850789504031"},
        {"sym1", "algo1","cid2", "eid4", "4","0"  ,"0"     , "20201004-18:33:08","", "1601850788591032"}
    };

    for (const auto& line: erlines_cover) {
        idpload.update(pm::ExecutionReport::fromCSVLine(line));
    }

    qty = idpload.getPosition(&vap, &pnl);
    pnlref = vapref-3.0 + 9*(vapref-2.13);
    if ( (qty!=0) || (std::fabs(pnl - pnlref)>1e-10) ) {
        std::cout << "qty or pnl mismatch after cover qty: " << qty << " pnl: " << pnl << "(" << pnlref << std::endl;;
        return false;
    }

    if (std::fabs(pnlref - idpload.getRealizedPnl())>1e-10) {
        std::cout << "realized pnl wrong after cover pnl: " << pnl << " pnlref " << pnlref << std::endl;
        return false;
    }

    if (idpload.hasPosition()) {
        std::cout << "hasPosition() returned true after cover" << std::endl;
        return false;
    }

    std::cout << "all good!" << std::endl;
    return true;
}

int main() {
    load_save();
    return 0;
}
