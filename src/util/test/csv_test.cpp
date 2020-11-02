#include "csv_util.h"
#include <iostream>

int main() {

    const char* lines[] = {
        "a, 1, 2.0, , ",
        "b, 2, 0.2, a ,c "};
    const int line_cnt = 2;
    utils::CSVUtil::FileTokens ftr ={ 
        {"a", "1", "2.0", "", ""}, 
        {"b", "2", "0.2", "a", "c"}
    };

    const char* testfile = "/tmp/testcsv.csv";
    {
        std::ofstream filecsv(testfile);
        for (const auto l : lines) {
            filecsv << l << std::endl;
        };
    };

    // read and write, and compare with re-read
    auto ft = utils::CSVUtil::read_file(testfile);

    bool matched = (ftr == ft);
    std::cout << (matched? "Matched!":"Mismatch!") << std::endl;

    // debug
    {
        for (const auto& l: ft) {
            for (const auto& t: l) {
                std::cout << "[" << t <<"]";
            }
            std::cout << std::endl;
        }
    }

    utils::CSVUtil::write_file(ft, testfile, true);
    auto ft2 = utils::CSVUtil::read_file(testfile);
    matched = (ft2[0] == ft2[2]) && (ft2[1]==ft2[3]);

    std::cout << (matched? "Matched!":"Mismatch!") << std::endl;

    // debug
    {
        for (const auto& l: ft2) {
            for (const auto& t: l) {
                std::cout << "["<< t <<"]";
            }
            std::cout << std::endl;
        }
    }

    return 0;
}
