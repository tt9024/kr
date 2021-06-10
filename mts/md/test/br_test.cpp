#include "md_bar.h"
#include "gtest/gtest.h"
#include <fstream>

const char* CFGFile = "/tmp/main.cfg";
const char* VENUEFile = "/tmp/venue.cfg";

class BRFixture : public testing::Test {
public:
    BRFixture ():
    _bar("0, 40.0, 40.1, 39.9, 40.05, 0, 0, 0, 0"),
    _bcfg("NYM", "CLF1", "L1"),
    _barsec(5)
    {
        {
            // write the main config to /tmp/main.cfg
            std::ofstream ofs;
            ofs.open (CFGFile, std::ofstream::out | std::ofstream::trunc);
            ofs << "Logger = /tmp/log" << std::endl;
            ofs << "BarPath = /tmp" << std::endl;
            ofs << "HistPath = /tmp" << std::endl;
            ofs << "BarSec = [ 5 ]" << std::endl;
            ofs << "SymbolMap = " << VENUEFile << std::endl;
        }
        {
            // write the venue config to /tmp/venue.cfg
            std::ofstream ofs;
            ofs.open (VENUEFile, std::ofstream::out | std::ofstream::trunc);
            ofs << "venue = {" << std::endl;
            ofs << "NYM   = { hours = [ -6, 0, 17, 0 ]   } " << std::endl;
            ofs << "ICE   = { hours = [ -4, 30, 17, 15 ] } " << std::endl;
            ofs << "}" << std::endl;
        }

        // set the config path NOW!
        utils::PLCC::setConfigPath(CFGFile);

        {
            // clear the bar file
            std::ofstream ofs;
            ofs.open(_bcfg.bfname(_barsec), std::ofstream::out | std::ofstream::trunc);
        }
    }

    void writeBar(time_t t, double px ) {
        _bar.update((long long)t*1000000LL, px, 0, 0);
        std::string line = _bar.writeAndRoll(t);
        {
            std::ofstream ofs;
            ofs.open(_bcfg.bfname(_barsec), std::ofstream::out | std::ofstream::app);
            ofs << line << std::endl;
        }
    }

protected: 
    md::BarPrice _bar;
    md::BookConfig _bcfg;
    int _barsec;
};

TEST_F (BRFixture, ReadBackwardForward) {

    // having the bar time as  { -20, -10, -5, 0, 3605, 3615}
    std::vector<int> bt { -20, -10, -5, 0, 3605};
    time_t utc_now = (time_t)utils::TimeUtil::string_to_frac_UTC("20201207-17:00:00",0);
    double px = 40.01;
    for (auto& b : bt) {
        px+=0.1;
        writeBar(utc_now + b, px);
    }
    md::BarReader br (_bcfg, _barsec);

    // read latest bar
    md::BarPrice bp;
    EXPECT_FALSE(br.read(bp));
    px+=0.1;
    writeBar(utc_now + 3615, px);
    EXPECT_TRUE(br.read(bp));
    EXPECT_DOUBLE_EQ(bp.close, px);
    EXPECT_EQ(bp.bar_time, utc_now + 3615);

    // read latest 2 bars, 3610 is forward filled by 3605
    std::vector<std::shared_ptr<md::BarPrice> > barHist;
    EXPECT_TRUE(br.readLatest(barHist, 2));
    EXPECT_EQ(bp.bar_time, barHist[1]->bar_time);
    EXPECT_DOUBLE_EQ(bp.close, barHist[1]->close);
    EXPECT_EQ(utc_now + 3610, barHist[0]->bar_time);
    EXPECT_DOUBLE_EQ(px-0.1, barHist[0]->close);

    // read -15 TO 0 as history, -15 is forward filled by -20
    EXPECT_TRUE(br.readPeriod(barHist, utc_now-15, utc_now));
    EXPECT_EQ(barHist.size(), 6);
    EXPECT_EQ(barHist[5]->bar_time, utc_now);
    EXPECT_EQ(barHist[4]->bar_time, utc_now-5);
    EXPECT_EQ(barHist[3]->bar_time, utc_now-10);
    EXPECT_EQ(barHist[2]->bar_time, utc_now-15);
    EXPECT_DOUBLE_EQ(barHist[5]->close, px-0.2);
    EXPECT_DOUBLE_EQ(barHist[2]->close, px-0.5);
    
    // read -25 TO -15, -25 backward filled
    EXPECT_TRUE(br.readPeriod(barHist, utc_now-25, utc_now-15));
    EXPECT_EQ(barHist.size(), 9);
    EXPECT_EQ(barHist[8]->bar_time, utc_now-15);
    EXPECT_EQ(barHist[7]->bar_time, utc_now-20);
    EXPECT_EQ(barHist[6]->bar_time, utc_now-25);
    EXPECT_DOUBLE_EQ(barHist[8]->close, px-0.5);
    EXPECT_DOUBLE_EQ(barHist[7]->close, px-0.5);
    EXPECT_DOUBLE_EQ(barHist[6]->close, px-0.5);

    // read 10 to 20, 10 forward filled from 5, 20 by 15
    EXPECT_TRUE(br.readPeriod(barHist, utc_now+3610, utc_now+3620));
    EXPECT_EQ(barHist.size(), 12);
    EXPECT_EQ(barHist[11]->bar_time, utc_now+3620);
    EXPECT_EQ(barHist[10]->bar_time, utc_now+3615);
    EXPECT_EQ(barHist[9]->bar_time, utc_now+3610);
    EXPECT_DOUBLE_EQ(barHist[11]->close, px);
    EXPECT_DOUBLE_EQ(barHist[10]->close, px);
    EXPECT_DOUBLE_EQ(barHist[9]->close, px-0.1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
