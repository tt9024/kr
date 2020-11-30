#pragma once

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdexcept>
#include <string>
#include <cmath>

namespace utils {

class TimeUtil {
public:
   static long long tv_diff(const struct timeval *tv1,
                           const struct timeval *tv2) {
      long long tn1 = (tv1->tv_sec - tv2->tv_sec)*1000000LL;
      return tn1+ (tv1->tv_usec - tv2->tv_usec);
   }

   static long long tv_from_now_to(const struct timeval *tv) {
      struct timeval tv_now;
      gettimeofday(&tv_now, NULL);
      return tv_diff(tv, &tv_now);
   }

   static int longlong_to_string_nanos_UTC(unsigned long long ts, char* char_buf, int buf_size) {
     uint32_t sec = ts/1000000000;
     int len = int_to_string_second_UTC(sec, char_buf, buf_size);
     len += snprintf(char_buf+len, buf_size-len, ",%06d", (int) (ts % 1000000000)/1000);
     return len;
   };

   static uint64_t string_to_frac_UTC(const char* str_buf, int frac_decimals=0, const char* fmt_str="%Y%m%d-%H:%M:%S") {
       // expects a UTC time string like YYYYMMDD-HH:MM:SS.ssssss
       // the .ssssss part, if found, is taken as a fraction.
       // returns the utc * frac_mul + fraction * frac_mul,
       // where frac_mul = 10**frac_decimals
       // set frac_decimals to be 0 to get a whole second (fraction dropped, NOT rounded)

       if (frac_decimals<0 || frac_decimals>9) {
           throw std::runtime_error("frac_decimals out-of-range: " + std::to_string(frac_decimals));
       }
       uint64_t frac_mul = 1;
       for (int i=frac_decimals; i>0; --i, frac_mul*=10);
       struct tm time_tm;
       char* p = strptime(str_buf, fmt_str, &time_tm);
       if (!p) {
           return 0;
       }
       time_tm.tm_isdst = -1;
       uint64_t tsf = (uint64_t)mktime(&time_tm) * frac_mul;
       if(*p=='.') {
           double f = std::stod("0"+std::string(p));
           tsf+=(uint64_t)(f*(double)frac_mul);
       }
       return tsf;
   };

   static size_t frac_UTC_to_string(uint64_t utc_frac_mul, char* char_buf, int buf_size, int frac_decimals=0, const char* fmt_str="%Y%m%d-%H:%M:%S") {
       // expects utc_frac_mul = whole_seconds * frac_mul + frac_seconds
       // where frac_mul=10**frac_decimals
       // ret YYYYMMDD-HHMMSS.sss, where 0.sss = frac_seconds/frac_mul
       // set the utc_frac_mul to 0 for the current time

       if (frac_decimals<0 || frac_decimals>9) {
           throw std::runtime_error("frac_decimals out-of-range: " + std::to_string(frac_decimals));
       }
       uint64_t frac_mul = 1;
       for (int i=frac_decimals; i>0; --i, frac_mul*=10);

       if (utc_frac_mul==0) {
           // use current time
           utc_frac_mul = cur_micro();
           int decimal_adj = frac_decimals-6;
           while (decimal_adj > 0) {
               utc_frac_mul*=10;
               --decimal_adj;
           }
           while (decimal_adj < 0) {
               utc_frac_mul/=10;
               ++decimal_adj;
           }
       }

       time_t sec = (time_t) (utc_frac_mul/frac_mul);
       struct tm t;
       size_t bytes = 0;

       if (localtime_r(&sec, &t)) {
           bytes = strftime(char_buf, buf_size, fmt_str, &t);
           double frac = (double)(utc_frac_mul%frac_mul)/(double)frac_mul;
           std::string fmt = "%.0"+std::to_string(frac_decimals)+"lf";
           char fbuf[16];
           snprintf(fbuf, sizeof(fbuf), fmt.c_str(), frac);
           bytes += snprintf(char_buf+bytes, buf_size-bytes, "%s",fbuf+1);
       }
       return bytes;
   };

   static std::string frac_UTC_to_string(uint64_t utc_frac_mul=0, int frac_decimals=0, const char* fmt_str="%Y%m%d-%H:%M:%S") {
       char buf[32];
       frac_UTC_to_string(utc_frac_mul, buf, sizeof(buf), frac_decimals, fmt_str);
       return std::string(buf);
   }
    

   static struct tm int_to_tm_UTC(time_t ts) {
      struct tm*ptm = localtime(&ts);
      return *ptm;
   };
   
   
   static bool isTradingTime_TM(struct tm tm_data, int start_hour, int start_min, int end_hour, int end_min) {
       // trading ends on friday, end_hour, end_min (inclusive, last minute is end_min - 1)
       // If start hour is negative, 
       //      trading starts on Sunday start_hour, start_min inclusive, first minute is start_min)
       // else 
       //      trading starts on Monday. 
      if (tm_data.tm_wday == 6) {
         return false;
      }
      else if (tm_data.tm_wday == 5) {
         if ((tm_data.tm_hour > (end_hour % 24)) ||
               ((tm_data.tm_hour == (end_hour % 24)) &&
                (tm_data.tm_min >= end_min))) {
             return false;
         };
      }
      else if (tm_data.tm_wday == 0) {
          if (start_hour > 0) {
              return false;
          } else {
              // sunday open
              if ((tm_data.tm_hour < (start_hour % 24)) ||
                  ((tm_data.tm_hour == (start_hour % 24)) && 
                   (tm_data.tm_min < start_min))) {
                  return false;
              };
          };
      }
      else if (tm_data.tm_wday == 1) {
          if (start_hour > 0) {
              if ((tm_data.tm_hour < start_hour) ||
                   ((tm_data.tm_hour == start_hour) && 
                    (tm_data.tm_min < start_min))) {
                  return false;
              }
          }
      }
      return true;
   };

   static bool isTradingTime(time_t ts, int start_hour = -7, int start_min = 31, int end_hour = 17, int end_min = 30) {
      return isTradingTime_TM(int_to_tm_UTC(ts, start_hour, start_min, end_hour, end_min));
   };

   static std::string tradingDay(time_t utc_second, 
                            int roll_hour = -7.0, 
                            int roll_min = 30,
                            int day_offset=0) 
   {
       // get the trading day of utc_second as defined by start/end time.
       // Current trading day ends as soon as current hour/min equals roll_hour, roll_min
       // at which time the trading day becomes the next non-weekend calendar day.
       // if day_offset is set, it returns previous or future trading days (non trading
       // days are not counted in the offset days).
       // Note 
       //     1. if the utc_second is not in a weekly trading day , i.e. during a weekend,
       //        it returns a previous trading day (friday).
       //     2. if roll_hour is negative, trading day is next day during over-night.
       //        if roll_hour is positive, trading day is calendar day of utc.

       int start_hour = roll_hour, start_min = roll_min, end_hour = roll_hour, end_min = roll_min;
       while (! isTradingTime(utc_second, start_hour, start_min, end_hour, end_min)) {
           utc_second -= 3600;
       }

       if (day_offset != 0) {
           int offset_sign = std::abs(day_offset)/day_offset;
           do {
               utc_seond += (offset_sign * 3600 * 24);
           } while (!isTradingTime(utc_second, start_hour, start_min, end_hour, end_min));
           return tradingDay(utc_second, roll_hour, roll_min, day_offset - offset_sign);
       }

       time_t utc = utc_second - (utc_second % 60);
       struct tm *tmsec = localtime(&utc);
       if (roll_hour < 0) {
           if (tm_data.tm_hour >= (roll_hour % 24)) {
               if (tm_data.tm_min >= roll_min) {
                   utc += (24*3600);
               }
           }
       }
       return frac_UTC_to_string(utc, 0, "%Y%m%d");
   }

   static int int_to_string_second_UTC(time_t sec, char*char_buf, int buf_size) {
     struct tm *tmsec = localtime(&sec);
     return strftime(char_buf, buf_size, "%Y%m%d,%H:%M:%S", tmsec);
   }

   static uint64_t cur_micro() {
       struct timeval tv;
       gettimeofday(&tv, NULL);
       return ((uint64_t)tv.tv_sec)*1000000ULL + tv.tv_usec;
   };

   static uint64_t micro_sleep(uint64_t micro) {
       struct timespec req, rem;
       req.tv_sec=(time_t)(micro/1000000ULL);
       req.tv_nsec=(long)((micro%1000000ULL)*1000ULL);
       int ret = nanosleep(&req, &rem);
       if (ret != 0)
           return rem.tv_sec*1000000ULL + (uint64_t)rem.tv_nsec/1000ULL;
       return 0;
   }
};

}
