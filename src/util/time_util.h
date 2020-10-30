#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

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

   /**
    * time format in YYYYMMDDHHMMSS
    * converts to UTC longlong
    */
   static void string_to_tm_UTC_Packed(struct tm & tm_data, const char* str_buf, int strlen) {
      char str[16];
      memset((void*)&tm_data, 0, sizeof(struct tm));

      // turn off the day light saving time manipulation
      tm_data.tm_isdst = -1;

      // year
      if (strlen >= 4) {
          memset(str, 0, 16);
          memcpy(str, str_buf, 4);
          tm_data.tm_year = atoi(str) - 1900;
      };

      // month
      if (strlen >= 6) {
          memset(str, 0, 16);
          memcpy(str, str_buf+4, 2);
          tm_data.tm_mon = atoi(str) - 1;
      }

      // day
      if (strlen >= 8) {
          memset(str, 0, 16);
          memcpy(str, str_buf+6, 2);
          tm_data.tm_mday = atoi(str);
      };

      // hour
      if (strlen >= 10) {
          memset(str, 0, 16);
          memcpy(str, str_buf+8, 2);
          tm_data.tm_hour = atoi(str);
      };

      // min
      if (strlen >= 12) {
          memset(str, 0, 16);
          memcpy(str, str_buf+10, 2);
          tm_data.tm_min = atoi(str);
      };

      // sec
      if (strlen >= 14) {
          memset(str, 0, 16);
          memcpy(str, str_buf+12, 2);
          tm_data.tm_sec = atoi(str);
      };
   }

   static void string_to_int_second_UTC_Packed(time_t &ts, const char* str_buf, int strlen) {
         struct tm time_tm;
         string_to_tm_UTC_Packed(time_tm, str_buf, strlen);
         ts = mktime(&time_tm);
         //printf("%s\n", asctime(&time_tm));
   };

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

   static int longlong_to_string_nanos_SOD(unsigned long long ts, char* char_buf) {
     int hour = ts/(60*60*1000000000ULL);
     int len = sprintf(char_buf, "%02d:", hour);
     ts -= (hour*(60*60*1000000000ULL));

     int min = ts/(60*1000000000ULL);
     len += sprintf(char_buf+len, "%02d:", min);
     ts -= (min*(60*1000000000ULL));

     int second = ts/(1000000000ULL);
     len += sprintf(char_buf+len, "%02d.", second);
     ts -= (second*1000000000ULL);

     len += sprintf(char_buf+len, "%06d", (int) ts/1000);
     return len;
   }

   static int long_long_to_string_nanos_SOD(unsigned long ts, unsigned long nano, char* char_buf) {
     int hour = ts/(60*60);
     int len = sprintf(char_buf, "%02d:", hour);
     ts -= (hour*60*60);

     int min = ts/(60);
     len += sprintf(char_buf+len, "%02d:", min);
     ts -= (min*60);

     int second = ts;
     len += sprintf(char_buf+len, "%02d.", second);
     len += sprintf(char_buf+len, "%06d", (int) nano/1000);
     return len;
   }

   static int int_to_string_second_UTC(time_t sec, char*char_buf, int buf_size) {
     struct tm *tmsec = localtime(&sec);
     return strftime(char_buf, buf_size, "%Y%m%d,%H:%M:%S", tmsec);
   }

   static std::string cur_time_to_string_day() {
	   time_t sec=time(NULL);
	   char char_buf[32];
	   int_to_string_day_UTC_Packed(sec,char_buf,sizeof(char_buf)-1);
	   return std::string(char_buf);
   }

   static int int_to_string_second_UTC_Packed(time_t sec, char*char_buf, int buf_size) {
     struct tm *tmsec = localtime(&sec);
     return strftime(char_buf, buf_size, "%Y%m%d%H%M%S", tmsec);
   }

   static int int_to_string_day_UTC_Packed(time_t sec, char*char_buf, int buf_size) {
     struct tm *tmsec = localtime(&sec);
     return strftime(char_buf, buf_size, "%Y%m%d", tmsec);
   }
    
   static int timeval_to_string_nanos_SOD(const struct timeval* tv, char*char_buf, int buf_size) {
     int len = int_to_string_second_UTC(tv->tv_sec, char_buf, buf_size);
     len += snprintf(char_buf+len, buf_size-len, ".%06d", (int)(tv->tv_usec));
     return len;
   };

   static int cur_time_string_nanos_SOD(char* char_buf, int buf_size) {
       struct timeval tv;
       gettimeofday(&tv, NULL);
       return timeval_to_string_nanos_SOD( &tv, char_buf, buf_size);
   };

   static uint64_t cur_time_micro() {
       struct timeval tv;
       gettimeofday(&tv, NULL);
       return ((uint64_t)tv.tv_sec)*1000000ULL + tv.tv_usec;
   };

   static unsigned long long cur_time_gmt_micro() {
       struct timeval tv;
       gettimeofday(&tv, NULL);
       return (unsigned long long)tv.tv_sec*1000000ULL + (unsigned long long) tv.tv_usec;
   }

   static int utc_to_local_ymdh(time_t utc, int*day=NULL, int*month=NULL, int*year=NULL)
   {
	   struct tm t;
	   if (localtime_r(&utc, &t)) {
		   if (day)
			   *day = t.tm_mday;
		   if (month)
			   *month = t.tm_mon;
		   if (year)
			   *year = t.tm_year;
		   return t.tm_hour;
	   }
	   throw std::runtime_error("invalid utc");
   }

};

}
