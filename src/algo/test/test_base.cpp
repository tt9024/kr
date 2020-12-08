#include "AlgoBase.h"
#include "gtest/gtest.h"
#include "FloorBase.h"
#include "strat/AR1.h"

TEST(AlgoBase, GetSetPosition) {
    pm::FloorBase m_floor("algobase_test",false);
    pm::FloorBase::MsgType msgreq(pm::FloorBase::GetPositionReq, nullptr, 0), msgresp;
    const pm::FloorBase::PositionRequest pr("CL001", "CLF1", 0, 0);
    msgreq.copyData((const char*)&pr, sizeof(pm::FloorBase::PositionRequest));
    msgresp.copyString("");
    EXPECT_TRUE(m_floor.m_channel->request(msgreq, msgresp));
    std::cout <<((pm::FloorBase::PositionRequest*)msgresp.buf)->qty_done << std::endl;

    /*
    std::string name = "CL001";
    std::string cfg = "config/strat/cl001.cfg"
    std::shared_ptr<algo::AR1> ar1 = std::make_shared<AR1>(name, cfg, m_floor.m_channel, utils::TimerUtil::cur_micro());
    */
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
