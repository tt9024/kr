#include "ExecutionReport.h"
#include <iostream>
#include <cmath>

const std::string erfname = "/tmp/test_er.csv";
utils::CSVUtil::FileTokens erlines = {
    {"sym1", "algo1","cid1", "eid1", "0","-10","2.1218", "20201004-18:33:02","", "1601850782023138"},
    {"sym1", "algo1","cid1", "eid2", "1","-3" ,"2.1218", "20201004-18:33:02","", "1601850782823033"},
    {"sym1", "algo1","cid2", "eid3", "0","5"  ,"2.1219", "20201004-18:33:08","", "1601850788504031"},
    {"sym1", "algo1","cid2", "eid4", "4","0"  ,"0"     , "20201004-18:33:08","", "1601850788591032"},
    {"sym1", "algo1","cid1", "eid5", "2","-7" ,"2.13"  , "20201004-18:33:09","", "1601850789504031"}
};

bool testER(bool verbose = false) {
    utils::CSVUtil::write_file(erlines, erfname, false);
    const auto& lines = utils::CSVUtil::read_file(erfname);

    std::vector<pm::ExecutionReport> erlist;
    for (const auto& line : lines) {
        erlist.push_back(pm::ExecutionReport::fromCSVLine(line));
    }

    if (verbose) {
        std::cout << "read er from csv: " << std::endl;
        for (const auto& er: erlist) {
            std::cout << er.toString() << std::endl;
        }
    }

    // write back to csv lines
    utils::CSVUtil::FileTokens erlines_derived;
    for (const auto& er : erlist) {
        erlines_derived.push_back(er.toCSVLine());
    }

    // load it differently
    std::vector<pm::ExecutionReport> erlist_derived;
    for (const auto& line: erlines_derived) {
        erlist_derived.emplace_back(
                line[0],
                line[1],
                line[2],
                line[3],
                line[4],
                std::stoi(line[5]),
                std::stod(line[6]),
                line[7],
                line[8],
                std::stoull(line[9]));
    };

    if (verbose) {
        std::cout << "derived from write back: " << std::endl;
        for (const auto& er: erlist_derived) {
            std::cout << er.toString() << std::endl;
        }
    }

    // compare the two
    for (size_t i=0; i<erlist.size(); ++i) {
        std::string difflog;
        if (!erlist[i].compareTo(erlist_derived[i], &difflog)) {
            std::cout << difflog << std::endl;
            return false;
        }
    }
    return true;
}

int main() {
    bool ret = testER(true);
    std::cout << (ret?"Matched!":"Mismatch!") << std::endl;
    return 0;
}

