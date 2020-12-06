#include "csv_util.h"
#include <iostream>
#include "gtest/gtest.h"
#include <cstdio>
#include <stdio.h>

class CSVFixture : public testing::Test {
public:
    CSVFixture ():
    _lines( {"a, 1, 2.0, , ", 
             "b, 2, 0.2, a ,c "
            } 
          ),
    _ftr( { {"a", "1", "2.0", "", ""}, 
            {"b", "2", "0.2", "a", "c"}
         }
       )
    { 
        _tmpfile = "/tmp/csv_test";
    }

    void TearDown() {
        if (_tmpfile.size()>0) {
            remove (_tmpfile.c_str());
        }
    }

protected:
    std::string _tmpfile;
    std::vector<std::string> _lines;
    utils::CSVUtil::FileTokens _ftr;
};

TEST_F (CSVFixture, Read) {
    std::ofstream filecsv(_tmpfile);
    for (const auto l : _lines) {
        filecsv << l << std::endl;
    }
    auto ft = utils::CSVUtil::read_file(_tmpfile);
    EXPECT_EQ (ft, _ftr);

    // debug
    {
        for (const auto& l: ft) {
            for (const auto& t: l) {
                std::cout << "[" << t <<"]";
            }
            std::cout << std::endl;
        }
    }

    // append
    utils::CSVUtil::write_file(ft, _tmpfile, true);
    auto ft2 = utils::CSVUtil::read_file(_tmpfile);
    EXPECT_EQ(ft2[0], ft2[2]);
    EXPECT_EQ(ft2[1], ft2[3]);

};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

