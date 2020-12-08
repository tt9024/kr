#pragma once
#include "time_util.h"
#include "plcc/PLCC.hpp"
namespace utils {

    /*
     * This is a continuous rate estimator, used to measure number of 
     * events in a "trailing" or continous time period.  Typically,
     * to check if more than "Count" events have happened in previous
     * "TimeWindow".  
     *
     * The implementation uses a circular buffer of size "Count" to
     * store time stamps of events.  Upon a new event, it checks
     * if the difference between overriding timestamp and current timestamp
     * is more than "TimeWindow".  If so, then the limit has been broken.
     */
    template<int Count>
    class RateLimiter {
    public:
        explicit RateLimiter(time_t TimeWindow_In_Second) 
        :_twnd(TimeWindow_In_Second*1000000LL) {
            memset((char*)_event, 0, sizeof(_event));
            _idx = 0;
        }

        uint64_t check(uint64_t cur_micro, bool ifAdd=true) {
            // this checks if an event happens now would violate the limit
            // return 0 if not, otherwise, micro_seconds before ok
            // In case of good, i.e. return 0, if ifAdd is true, 
            // the cur_micro is counted as an event and added.
            //

            if (__builtin_expect(cur_micro == 0, 0)) {
                cur_micro = TimeUtil::cur_micro();
            };
            auto& ts = _event[_idx%Count];
            int64_t tdiff = cur_micro - ts;
            if (tdiff < _twnd) {
                return (uint64_t) _twnd - tdiff;
            }
            if (ifAdd) {
                ts=cur_micro;
                ++_idx;
            }
            return 0;
        }
    private:
        const int64_t _twnd;
        uint64_t _event[Count];
        uint64_t _idx;
    };
}
