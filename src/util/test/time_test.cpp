#include "time_util.h"
#include "stdio.h"
#include <string>

int main() {
    const char* ts1[] = {
        "20201030-20:16:32", 
        "20201030-20:16:32.0";
        "20201030-20:16:32.123";
        "20201030-20:16:32.124678"};

    int dec1[] = {0,1,3,6};
    char buf[32];
    for (int i=0; i<4; ++i) {
        auto ts = ts1[i];
        auto dec = dec1[i];
        uint64_t utc = utils::TimeUtil::string_to_frac_UTC(ts, buf, sizeof(buf), frac_decimals=dec);
        size_t bytes = utils::TimeUtil::frac_UTC_to_string(buf, sizeof(buf), frac_decimals=dec);
        printf("%s %lld %s %d\n", ts, (long long)utc, buf, (int)strcmp(ts, buf));
    }
    return 0;
}
