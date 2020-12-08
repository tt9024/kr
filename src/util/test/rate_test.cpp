#include "rate_limiter.h"
#include "stdio.h"
#include <string>
#include "gtest/gtest.h"
#include <cstdlib>

TEST (RateTest, Check) {
    {
        // normal case 1
        utils::RateLimiter<2> rc(3);
        EXPECT_EQ(rc.check(11000000), 0);
        EXPECT_EQ(rc.check(12000000), 0);
        EXPECT_EQ(rc.check(14000000), 0);
    };

    {
        // normal case 2
        utils::RateLimiter<2> rc(3);
        EXPECT_EQ(rc.check(11000000), 0);
        EXPECT_EQ(rc.check(13000000), 0);
        EXPECT_EQ(rc.check(15000000), 0);
    };

    {
        // bad case 1
        utils::RateLimiter<2> rc(3);
        EXPECT_EQ(rc.check(11000000), 0);
        EXPECT_EQ(rc.check(12000000), 0);
        EXPECT_EQ(rc.check(13000000), 1000000);
    };

    {
        // bad case 2
        utils::RateLimiter<2> rc(3);
        EXPECT_EQ(rc.check(11000000), 0);
        EXPECT_EQ(rc.check(12000000), 0);
        EXPECT_EQ(rc.check(13000000), 1000000);
        EXPECT_EQ(rc.check(13500000), 500000);
        EXPECT_EQ(rc.check(14000000), 0);
    };

}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

