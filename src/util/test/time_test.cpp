#include "time_util.h"
#include "stdio.h"
#include <string>

int main() {
    const char* ts1[] = {
        "20201101-02:00:00", 
        "20200308-03:00:00.0",
        "20200101-20:16:32.123",
        "20201030-20:16:32.124678"};

    int dec1[] = {0,1,3,6};
    char buf[32];
    for (int i=0; i<4; ++i) {
        auto ts = ts1[i];
        auto dec = dec1[i];
        uint64_t utc = utils::TimeUtil::string_to_frac_UTC(ts, dec);
        size_t bytes = utils::TimeUtil::frac_UTC_to_string(utc, buf, sizeof(buf), dec);
        printf("%s %lld %s %d\n", ts, (long long)utc, buf, (int)strcmp(ts, buf));
    }

    int dec2[] = {1,0,5,3};
    const char* ts2[] = {
        "20201101-02:00:00.0", 
        "20200308-03:00:00",
        "20200101-20:16:32.12300",
        "20201030-20:16:32.125"};

    for (int i=0; i<4; ++i) {
        auto ts = ts1[i];
        auto dec = dec2[i];
        auto tsr = ts2[i];
        uint64_t utc = utils::TimeUtil::string_to_frac_UTC(ts, dec);
        size_t bytes = utils::TimeUtil::frac_UTC_to_string(utc, buf, sizeof(buf), dec);
        printf("%s %lld %s %d\n", ts, (long long)utc, buf, (int)strcmp(tsr, buf));
    }


    uint64_t cur_micro = utils::TimeUtil::cur_time_micro();
    utils::TimeUtil::frac_UTC_to_string(cur_micro, buf, sizeof(buf), 6, "%Y%m%d%H%M%S");

    time_t cur_utc;
    utils::TimeUtil::string_to_int_second_UTC_Packed(cur_utc, buf, sizeof(buf));

    printf("%lld, %s, %d, %d\n", (long long)cur_micro, buf, (int)cur_utc, (int) (cur_utc - cur_micro/1000000));

    return 0;
}
