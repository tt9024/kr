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

   static std::string frac_UTC_to_string(uint64_t utc_frac_mul, int frac_decimals=0, const char* fmt_str="%Y%m%d-%H:%M:%S") {
       char buf[32];
       frac_UTC_to_string(utc_frac_mul, buf, sizeof(buf), frac_decimals, fmt_str);
       return std::string(buf);
   }
    

   static struct tm int_to_tm_UTC(time_t ts) {
      struct tm*ptm = localtime(&ts);
      return *ptm;
   };

   static bool isTradingTime_TM(struct tm tm_data) {
      if (tm_data.tm_wday == 6) {
         return false;
      };

      if (tm_data.tm_wday == 5) {
         if (tm_data.tm_hour >= 17) {
             return false;
         };
      };

      if (tm_data.tm_wday == 0) {
         if (tm_data.tm_hour < 17) {
            return false;
         };
      };
      
      return true;
   };

   static bool isTradingTime_UTC(time_t ts) {
      return isTradingTime_TM(int_to_tm_UTC(ts));
   };

   static int int_to_string_second_UTC(time_t sec, char*char_buf, int buf_size) {
     struct tm *tmsec = localtime(&sec);
     return strftime(char_buf, buf_size, "%Y%m%d,%H:%M:%S", tmsec);
   }

   static uint64_t cur_micro() {
       struct timeval tv;
       gettimeofday(&tv, NULL);
       return ((uint64_t)tv.tv_sec)*1000000ULL + tv.tv_usec;
   };
};

}
