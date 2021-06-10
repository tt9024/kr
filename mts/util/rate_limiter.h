#pragma once
#include "time_util.h"
#include "plcc/PLCC.hpp"
#include "circular_buffer.h"

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
     *
     * It is thread safe.  See the lockless implementation notes below.
     */
    class RateLimiter {
    public:
        explicit RateLimiter(const int count, time_t TimeWindow_In_Second) 
        :_count(count), _twnd(TimeWindow_In_Second*1000000LL),
         _event((long long*)malloc(sizeof(long long)*_count))
        {
            if (!_event) {
                throw std::runtime_error("RateLimit malloc");
            }
            memset((char*)_event, 0, sizeof(int64_t)*_count);
            _idx = 0;
        };
        ~RateLimiter() { free ((void*)_event); _event = NULL; };

        uint64_t checkOnly(uint64_t cur_micro) {
            // this checks if an event happens now would violate the limit
            // return 0 if not, otherwise, micro_seconds before ok
            // In case of good, i.e. return 0

            if (__builtin_expect(cur_micro == 0, 0)) {
                cur_micro = TimeUtil::cur_micro();
            };
            auto& ts = _event[_idx%_count];
            long long tdiff = (long long)cur_micro - ts;
            if (tdiff < _twnd) {
                return (uint64_t) _twnd - tdiff;
            }
            return 0;
        }

        uint64_t check(uint64_t cur_micro=0) {
            // This checks if an event happens now would violate the limit.
            // Return 0 if not, otherwise, a micro_seconds that must be waited.
            // 
            // The function is thread safe.  It works in the following loop:
            // 
            // It first get the current idx and check _event[idx] against
            // cur_micro.  If it's good:
            //    1. CAS on the index to make sure only one thread can update
            //    2. if obtained CAS, update the entry value of the index
            //    3. return
            // otherwise, return number of micro seconds it must wait if
            // the index hasn't been changed.
            // 
            // The loop ends before index advances to a maximum of 
            // circular buffer length.  It is both safe (no two thread
            // updating same entry due to modulo wrap) and valid (from
            // the definition of the maximum of _count events)
            //

            if (__builtin_expect(cur_micro == 0, 0)) {
                cur_micro = TimeUtil::cur_micro();
            };

            long long idx = _idx;
            const long long idx0 = idx;
            while (idx-idx0 < _count) {
                volatile long long* ptr = _event + (idx%_count);
                long long ts = *ptr;
                if (__builtin_expect((long long)cur_micro - ts >= _twnd, 1)) {
                    // good case.  In case multiple thread reading the same
                    // entry value and coming up with the same decision of passing,
                    // only one could update and return, while the others continue to spin
                    long long idx_now = compareAndSwap(&_idx, idx, idx+1);
                    if (__builtin_expect(idx_now == idx, 1)) {
                        // it's now safe to update this entry value
                        // the wrapping case is checked against below
                        // note - we assume a memory fence here, which
                        *ptr = (long long)cur_micro;
                        return 0;
                    }
                } else {
                    // violating case, return wait second without update
                    if (idx == _idx) {
                        // this check is necessary in case multiple
                        // thread checking on the same idx with one
                        // of them passed and updated the entry.
                        // This is to safe guard for false negative of
                        // the competing thread checking on the same idx 
                        // reading the updated entry value.
                        return (uint64_t) (_twnd - (cur_micro - ts));
                    }
                }
                idx = _idx;
            }
            return _twnd;
        }

        RateLimiter(const RateLimiter& rl)
        : _count(rl._count), _twnd(rl._twnd),
          _event((long long*)malloc(sizeof(long long)*_count)),
          _idx(rl._idx)
        {
            if (!_event) {
                throw std::runtime_error("RateLimit malloc");
            }
            memcpy((void*)_event, (void*)rl._event, sizeof(long long)*_count);
        }

    private:
        const int _count;
        const long long _twnd;
        volatile long long* _event;
        volatile long long _idx;
        void operator = (const RateLimiter& ) = delete;
    };
}
