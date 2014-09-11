#pragma once
#include <Misc/utils/Config.h>
#include <stdint.h>
#include <sstream>
#include <endian.h>
#include <time.h>
#include <vector>
#include <stdexcept>

#define FieldTerminator '\01'
#define MaxNumberLen 18  // max strlen for a decimal number
#define FixVersionStringLen 7 // FIX.4.3
#define LengthValueMaxLen 5  // max char for 9= value
#define CheckSumValLen 3
#define LengthValueOffset (FixVersionStringLen+5)  // offset of 9=
#define ChecksumFieldLen (CheckSumValLen+4)   // 10=xxx|
#define DoubleMaxDecimal 8

#if __BYTE_ORDER != __LITTLE_ENDIAN
#error "machine is not little endian"
#endif

namespace fixutils {
	struct FixUtilConfig {
		static std::string get_version_string(const CConfig& config) {
			std::string str;
			config.get("PROTOCOL_VERSION", &str, true);
			if (str.length() + 4 != FixVersionStringLen) {
				throw std::invalid_argument("protocol version length error");
			}
			return "FIX." + str;
		}
		static std::string get_sender_comp(const CConfig& config) {
			std::string str;
			config.get("m_senderCompID", &str, true);
			return str;
		}
		static std::string get_target_comp(const CConfig& config) {
			std::string str;
			config.get("m_targetCompID", &str, true);
			return str;
		}
		static std::string get_sender_sub_comp(const CConfig& config) {
			std::string str;
			config.get("m_senderSubCompID", &str);
			return str;
		}
		static std::string get_target_sub_comp(const CConfig& config) {
			std::string str;
			config.get("m_targetSubCompID", &str);
			return str;
		}
		static std::string get_username(const CConfig& config) {
			std::string str;
			config.get("USERNAME", &str);
			return str;
		}
		static std::string get_password(const CConfig& config) {
			std::string str;
			config.get("PASSWORD", &str);
			return str;
		}
		static unsigned int get_seq_in(const CConfig& config) {
			unsigned int val = -1;
			config.get("fromExchangeToEngine", &val);
			return val;
		}
		static unsigned int get_seq_out(const CConfig& config) {
			unsigned int val = -1;
			config.get("fromEngineToExchange", &val);
			return val;
		}

	};

	inline unsigned char getCheckSum(const char* msg, int size) {
		unsigned char checksum = 0;
		const char* const msg_end = msg + size;
		while (msg != msg_end) {
			checksum += (unsigned char) *msg++;
		}
		return checksum;
	}

	inline std::string dumpFixMsg(const char* msg, int size) {
		// change all the '\01' to '|'
		std::ostringstream oss;
		for (int i=0; i<size; ++i) {
			unsigned char ch = msg[i];
			oss <<  ((ch == 1) ? '1' : ch);
		}
		return oss.str();
	}

	inline int writeField(char* msg, unsigned int tag, const char* charBuf, int bufSize)
	{
		int bytes = writeInt(msg, tag);
		msg += bytes;
		*msg++ = '=';
		bytes += bufSize;
		while(bufSize--) *msg++ = *charBuf++;
		*msg = 1;
		return bytes + 2;
	}

	inline int writeField(char* msg, unsigned int tag, const char* strVal)
	{
		int bytes = writeInt(msg, tag);
		msg += bytes;
		*msg++ = '=';
		const char* strStart = strVal;
		while(*strVal) { *msg++ = *strVal++;};
		*msg = 1;
		return bytes + (strVal - strStart) + 2;
	}

	inline int writeField(char* msg, unsigned int tag, char ch)
	{
		int bytes = writeInt(msg, tag);
		msg += bytes;
		*msg++ = '=';
		*msg++ = ch;
		*msg = 1;
		return bytes + 3;
	}

	template<typename IntType>
	inline int writeIntField(char* msg, unsigned int tag, IntType intVal)
	{
		/// currently specialized for int/uint/longlong/ulonglong
		int bytes = writeInt(msg, tag);
		msg += bytes;
		*msg++ = '=';
		int bytes2 = writeInt(msg, intVal);
		msg += bytes2;
		*msg = 1;
		return bytes + bytes2 + 2;
	}

	template<int decimalPlace=DoubleMaxDecimal-1>
	inline int writeField(char* msg, unsigned int tag, double doubleVal)
	{
		static_assert((decimalPlace < DoubleMaxDecimal) && (decimalPlace > 0), "writeDoubleWrongSTRLEN");
		int bytes = writeInt(msg, tag);
		msg += bytes;
		*msg++ = '=';
		int bytes2 = writeDouble<decimalPlace>(msg, doubleVal);
		msg += bytes2;
		*msg = 1;
		return bytes + bytes2 + 2;
	}

	template<int decimal=DoubleMaxDecimal-1>
	inline int writeDouble(char* dst, double val)
	{
		static const double decimalMul[DoubleMaxDecimal] = {0.0,10.0,100.0,1000.0,10000.0,100000.0,1000000.0,10000000.0};
		static_assert((decimal < DoubleMaxDecimal) && (decimal > 0), "writeDoubleWrongSTRLEN");
		int bytes = 0;
		if (val < 0)
		{
			*dst++ = '-';
			val = -val;
			++bytes;
		}
		unsigned long long int_part = (unsigned long long) val;
		unsigned long long decimal_part = (unsigned long long)((val - (double)int_part) * decimalMul[decimal]);
		bytes += writeInt(dst, int_part);
		dst += bytes;
		*dst++ = '.';
		bytes += writeInt(dst, decimal_part);
		return bytes+1;
	}

	inline int writeInt(char* dst, int n)
	{
		int bytes = 0;
		if (n > 0)
		{
			return writeInt(dst, (unsigned int) n);
		}
		*dst++ = '-';
		return writeInt(dst, (unsigned int) (-n)) + 1;
	}

	inline int writeInt(char* dst, unsigned int n)
	{
		// optimize for writing small numbers such as tag values
		if (n < 10)
		{
			*dst = n+'0';
			return 1;
		}
		else if (n < 100)
		{
			// 2 digits
			*dst++ = n/10 + '0';
			*dst= n%10 +'0';
			return 2;
		}
		else if (n < 1000)
		{
			// 3 digits
			*dst++ = n/100 + '0';
			n = n%100;
			*dst++= n/10 + '0';
			*dst = n%10 + '0';
			return 3;
		}
		return writeInt(dst, (unsigned long long)n);
	}


	inline int writeInt(char* dst, long long n)
	{
		int bytes = 0;
		if (n > 0)
		{
			return writeInt(dst, (unsigned long long) n);
		};
		*dst++ = '-';
		return writeInt(dst, (unsigned long long) (-n)) + 1;
	}

	inline int writeInt(char* dst, unsigned long long n)
	{
		char buf[32];
		char *ptr = buf+32;
		do
		{
			*--ptr = n % 10 + '0';
		} while (n /= 10);
		int bytes = 32 - (ptr - buf);
		memcpy(dst, ptr, bytes);
		return bytes;
	}

	template<int Len>
	inline void writeIntFixed(char* msgBuf, unsigned int val)
	{
		msgBuf+=Len;
		for (int i=0; i<Len; ++i)
		{
			*--msgBuf = ((val % 10) + '0');
			val /= 10;
		};
	}

	template<int numberLen=MaxNumberLen-1>
	inline double getDouble(const char* source, bool& success)
	{
	    static const double mul[MaxNumberLen] = {
	            .1,
	            .01,
	            .001,
	            .0001,
	            .00001,
	            .000001,
	            .0000001,
	            .00000001,
	            .000000001,
	            .0000000001,
	            .00000000001,
	            .000000000001,
	            .0000000000001,
	            .00000000000001,
	            .000000000000001,
	            .0000000000000001,
	            .00000000000000001,
	            .000000000000000001};

	    static_assert((numberLen < MaxNumberLen) && (numberLen > 0), "getDoubleWrongSTRLEN");
	    long long intNum1 = 0, intNum2 = 0;
	    long long *intPtr = &intNum1;
	    int dotPos = 0, strPos = -1;
	    bool hasFractions = false;

	    // check sign
	    int sign = 1;
	    bool hasSign = false;
	    if (*source == '-' || *source == '+')
	    {
	    	if (*source == '-')
	    	{
	    		sign = -1;
	    	}
	    	++strPos;
	    	hasSign = true;
	    }

	    // loop through the string
	    char ch;
	    while (++strPos < numberLen)
	    {
	        ch = source[strPos];
	        if (ch == '.')
	        {
	            if (hasFractions)
	            {
	            	success = false;
	                return 0;
	            }
	            intPtr = &intNum2;
	            hasFractions = true;
	            dotPos = strPos;
	            continue;
	        }
	        int digit = ch - '0';
	        if ((digit >= 0) && (digit <= 9))
	        {
	            *intPtr = *intPtr * 10 + digit;
	        } else {
	            break;
	        }
	    }
	    if (strPos == 0 || (hasSign && (strPos == 1)))
	    {
	    	// no digits were found
	    	success = false;
	        return 0;
	    }
	    success = true;
	    if (!hasFractions || (strPos == dotPos + 1))
	    {
	        return (double)(intNum1 * sign);
	    }
	    int mulIdx = strPos - dotPos - 2;
	    if (sign == 1)
	    {
	        return intNum1 + mul[mulIdx]* intNum2;
	    };
		return -intNum1 - mul[mulIdx]* intNum2;
	}

	template<int numberLen = MaxNumberLen-1>
	inline long long getInt(const char* source, bool& success)
	{
		static_assert((numberLen < MaxNumberLen) && (MaxNumberLen > 0), "getIntWrongSTRLEN");
	    long long target = 0;
	    int sign = 1;
	    const char* const endPtr = source + numberLen;
	    while ((*source == ' ') && (source < endPtr)) ++source;  // skipping leading spaces
	    if ((source < endPtr) && ((*source == '-') || (*source == '+')))
	    {
	        if (*source == '-')
	        {
	            sign = -1;
	        }
	        ++source;
	    }
	    int digits = 0;
	    while (source < endPtr)
	    {
	    	int digit = *source - '0';
	        if ((digit >= 0) && (digit <= 9))
	        {
	            target = target * 10 + digit;
	            ++digits;
	        } else {
	            break;
	        }
	        ++source;
	    }
	    if (!digits)
	    {
	    	success = false;
	    	return 0;
	    }
	    success = true;
	    return target * sign;
	}

	inline int getUnsignedIntUnknownLen(const char* msg, unsigned int& val) {
		val = 0;
		char ch;
		int idx;
		for (idx = 0; idx < 7; ++idx) {
			ch = msg[idx];
			if ((ch >= '0') && (ch <= '9')) {
				val = val*10 + (ch - '0');
				continue;
			}
			return idx;
		}
		uint64_t longVal = val;
		for (; idx < 11; ++idx) {
			ch = msg[idx];
			if ((ch >= '0') && (ch <= '9')) {
				longVal = longVal*10 + (ch - '0');
				continue;
			}
			if (longVal <= 0xffffffffULL) {
				val = (unsigned int) longVal;
				return idx;
			};
			return -1;
		}
		return -1;
	}

	inline int getMsgSize(const char* buf, int bufLen, const char*& msgStart, unsigned int& bodyLen, char* type) const {
		if (bufLen < LengthValueOffset + LengthValueMaxLen)
			return 0;
		int fieldSize = getUnsignedIntUnknownLen(buf + LengthValueOffset, bodyLen);
		if (__builtin_expect((fieldSize <= 0), 0)) {
			// got a wrong size here, it's a unrecoverable error
			// the session should probably be closed
			return -1;
		}
		unsigned int msgSize = LengthValueOffset + fieldSize + 1 + bodyLen + ChecksumFieldLen;
		if ( msgSize <= bufLen) {
			// get type as well
			int typeOffset = LengthValueOffset + fieldSize + 1 + 3 ; // adding '35='
			// read in type
			int typeLen = 0;
			while (buf[typeOffset] != 1)
			{
				type[typeLen++] = buf[typeOffset++];
			}
			type[typeLen] = 0; // terminating NULL
			msgStart = buf + LengthValueOffset + fieldSize + 1 + 3 + typeLen + 1;
			bodyLen -= (3 + typeLen + 1);
			return msgSize;
		}
		return 0;
	}

	template<bool ifWriteMS = true>
	inline int getFixSendingTimeMS(char* msgBuf)
	{
		/// YYYYMMDD-hh:mm:ss.sss
		timeval gmt_tv;
		gettimeofday(&gmt_tv, NULL);
		char* msgStart = msgBuf;
		msgBuf += formatSendingTime(msgBuf, gmt_tv.tv_sec);
		if (ifWriteMS)
		{
			*msgBuf++='.';
			writeIntFixed<3>(msgBuf, gmt_tv.tv_usec/1000);
			msgBuf+=3;
		}
		return msgBuf-msgStart;
	}

	inline int formatSendingTime(char* msgBuf, time_t tv_sec)
	{
		/// YYYYMMDD-hh:mm:ss.sss
		tm gmt_tm;
		gmtime_r(&tv_sec, &gmt_tm);

		writeIntFixed<4>(msgBuf, (gmt_tm.tm_year + 1900));
		msgBuf+=4;
		writeIntFixed<2>(msgBuf, (gmt_tm.tm_mon + 1));
		msgBuf+=2;
		writeIntFixed<2>(msgBuf, gmt_tm.tm_mday);
		msgBuf+=2;
		*msgBuf++='-';
		writeIntFixed<2>(msgBuf, gmt_tm.tm_hour);
		msgBuf+=2;
		*msgBuf++=':';
		writeIntFixed<2>(msgBuf, gmt_tm.tm_min);
		msgBuf+=2;
		*msgBuf++=':';
		writeIntFixed<2>(msgBuf, gmt_tm.tm_sec);
		msgBuf+=2;
		return 17;
	}


#define Default_HB_Interval 30
	inline const char* skipToTypeVal(const char* msg)
	{
		return msg + (LengthValueOffset + LengthValueMaxLen + 1 + 3);
	}

	inline int getBodyLen(int size)
	{
		return size - (LengthValueOffset + LengthValueMaxLen + 1 + CheckSumValLen);
	}

	template<int bytes=2>
	inline unsigned long long StringToInt(const char* strVal)
	{
		static_assert((bytes <= 8) && (bytes > 0), "StringToInt Bytes Error");
		unsigned long long val = strVal[0];
		for (int i=1; i<bytes; ++i)
		{
			val <<= 8;
			val |= strVal[i];
		}
		return val;
	}

#define HeartBeatType "0"
#define TestRequestType "1"
#define ResendReqType "2"
#define SessionRejectType "3"
#define SequenceResetType "4"
#define LogoutType "5"
#define LogonType "A"
#define GetTypeShort(type) (*((unsigned short*)type))

#define HeartBeatTypeShort GetTypeShort(HeartBeatType)
#define TestRequestTypeShort GetTypeShort(TestRequestType)
#define ResendReqTypeShort GetTypeShort(ResendReqType)
#define SessionRejectTypeShort GetTypeShort(SessionRejectType)
#define SequenceResetTypeShort GetTypeShort(SequenceResetType)
#define LogoutTypeShort GetTypeShort(LogoutType)
#define LogonTypeShort GetTypeShort(LogonType)
};
