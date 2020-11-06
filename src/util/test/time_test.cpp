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


    uint64_t cur_micro = utils::TimeUtil::cur_micro();
    utils::TimeUtil::frac_UTC_to_string(cur_micro, buf, sizeof(buf), 6, "%Y%m%d%H%M%S");

    std::string ct_milli = utils::TimeUtil::frac_UTC_to_string(0, 3, "%Y%m%d%H%M%S");
    std::string ct_sec = utils::TimeUtil::frac_UTC_to_string(0, 0, "%Y%m%d%H%M%S");
    std::string ct_nano = utils::TimeUtil::frac_UTC_to_string(0, 9, "%Y%m%d%H%M%S");

    uint64_t ct_milli_ts = utils::TimeUtil::string_to_frac_UTC(ct_milli.c_str(), 3, "%Y%m%d%H%M%S");
    uint64_t ct_sec_ts = utils::TimeUtil::string_to_frac_UTC(ct_sec.c_str(), 0, "%Y%m%d%H%M%S");
    uint64_t ct_nano_ts = utils::TimeUtil::string_to_frac_UTC(ct_nano.c_str(), 9, "%Y%m%d%H%M%S");

    // demand they be less tha 1000 micro
    if ((-cur_micro + ct_milli_ts*1000ULL  < 1000000ULL) ||
        (-cur_micro + ct_sec_ts*1000000ULL < 1000000ULL) ||
        (-cur_micro + ct_nano_ts/1000ULL   < 1000000ULL)) {
        printf("matched!\n");
    }
    printf("%s, %s, %s, %s\n", buf, ct_milli.c_str(), ct_sec.c_str(), ct_nano.c_str());
    return 0;
}
