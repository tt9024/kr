#pragma once
#include <Vulcan/Common/FixUtils/FixUtils.h>
#define BUFFER_CHUNK_SIZE 1024
#define ALLOCATE_THRESHOLD 64
// we shouldn't be sending fix message larger than 1K
// #define CHECK_BUFFER_LEN

namespace fixutils {
	class FixMessageBuilder {
	public:
		explicit FixMessageBuilder(const char* version);
		~FixMessageBuilder();
		FixMessageBuilder& resetType(const char* type);
		template<typename SockType>
		void finalizeAndSend(SockType* sock);

		// functions to write various types of tags
		FixMessageBuilder& writeField(unsigned int tag, const char* charBuf, int bufSize);
		FixMessageBuilder& writeField(unsigned int tag, const char* strVal);
		FixMessageBuilder& writeField(unsigned int tag, char ch);
		template<typename IntType>
		FixMessageBuilder& writeIntField(unsigned int tag, IntType intVal);
		template<int decimalPlace=DoubleMaxDecimal-1>
		FixMessageBuilder& writeField(unsigned int tag, double doubleVal);
		template<bool ifWriteMS = true>
		FixMessageBuilder& writeCurrentTime(unsigned int tag);
		FixMessageBuilder& writeString(const char* charBuf, int bufSize);
		// accessor
		const char* getMsg() const { return m_buff; };
		int getMsgLen() const { return m_buffPtr - m_buff; };

	private:
		FixMessageBuilder (const FixMessageBuilder& builder);
		FixMessageBuilder& operator = (const FixMessageBuilder& builder);
		unsigned char getCheckSum();
		void checkAndIncBuff(int reserveSize = ALLOCATE_THRESHOLD);
		char* m_buff;
		char* m_buffPtr;
		char* m_buffEnd;
	};

	FixMessageBuilder::FixMessageBuilder(const char* version)
	{
		m_buff = (char*) malloc(BUFFER_CHUNK_SIZE);
		m_buffPtr = m_buff;
		m_buffEnd = m_buff + BUFFER_CHUNK_SIZE;
		*m_buffPtr++ = '8';
		*m_buffPtr++ = '=';
		memcpy(m_buffPtr, version, FixVersionStringLen);
		m_buffPtr+=FixVersionStringLen;
		*m_buffPtr++ = 1;
		*m_buffPtr++ = '9';
		*m_buffPtr++ = '=';
		m_buffPtr+=LengthValueMaxLen; // leave space for body length
		*m_buffPtr++ = 1;
	}

	FixMessageBuilder::~FixMessageBuilder() {
		if (m_buff) {
			free(m_buff);
			m_buff = NULL;
		}
	}

	FixMessageBuilder& FixMessageBuilder::resetType(const char* type)
	{
		m_buffPtr = m_buff + LengthValueOffset + LengthValueMaxLen + 1; // seek to the type field;
		m_buffPtr += writeField(35, type);
		return *this;
	}

	// fill in body length and compute the checksum
	template<typename SockType>
	inline void FixMessageBuilder::finalizeAndSend(SockType* sock) {
		checkAndIncBuff(8);
		// fill in digits of bodyLen, fixed LengthValueMaxLen
		int bodyLen = (m_buffPtr - m_buff) - (LengthValueOffset + LengthValueMaxLen + 1);
		char *msgPtr = m_buff + LengthValueOffset;
		writeIntFixed<LengthValueMaxLen>(msgPtr, bodyLen);

		// appm_buffPtr checksum as 3 digit number
		unsigned char checkSum = getCheckSum();
		*m_buffPtr++ = '1';
		*m_buffPtr++ = '0';
		*m_buffPtr = '=';
		fixutils::writeIntFixed<CheckSumValLen>(m_buffPtr, checkSum);
		m_buffPtr+=CheckSumValLen;
		*m_buffPtr++ = 1;
		if (sock)
			sock->sendToSocket(m_buff, m_buffPtr-m_buff);
	}

	inline FixMessageBuilder& FixMessageBuilder::writeField(unsigned int tag, const char* charBuf, int bufSize)
	{
		checkAndIncBuff(bufSize+1);
		int bytes = fixutils::writeInt(m_buffPtr, tag);
		m_buffPtr += bytes;
		*m_buffPtr++ = '=';
		bytes += bufSize;
		while(bufSize--) *m_buffPtr++ = *charBuf++;
		*m_buffPtr++ = 1;
		return *this;
	}


	inline FixMessageBuilder& FixMessageBuilder::writeString(const char* charBuf, int bufSize)
	{
		checkAndIncBuff(bufSize+1);
		memcpy(m_buffPtr, charBuf, bufSize);
		m_buffPtr += bufSize;
		return *this;
	}

	inline FixMessageBuilder& FixMessageBuilder::writeField(unsigned int tag, const char* strVal)
	{
		checkAndIncBuff();
		int bytes = fixutils::writeInt(m_buffPtr, tag);
		m_buffPtr += bytes;
		*m_buffPtr++ = '=';
		const char* strStart = strVal;
		while(*strVal) { *m_buffPtr++ = *strVal++;};
		*m_buffPtr++ = 1;
		return *this;
	}

	inline FixMessageBuilder& FixMessageBuilder::writeField(unsigned int tag, char ch)
	{
		checkAndIncBuff();
		int bytes = fixutils::writeInt(m_buffPtr, tag);
		m_buffPtr += bytes;
		*m_buffPtr++ = '=';
		*m_buffPtr++ = ch;
		*m_buffPtr++ = 1;
		return *this;
	}

	template<typename IntType>
	inline FixMessageBuilder& FixMessageBuilder::writeIntField(unsigned int tag, IntType intVal)
	{
		checkAndIncBuff();
		/// currently specialized for int/uint/longlong/ulonglong
		int bytes = fixutils::writeInt(m_buffPtr, tag);
		m_buffPtr += bytes;
		*m_buffPtr++ = '=';
		int bytes2 = writeInt(m_buffPtr, intVal);
		m_buffPtr += bytes2;
		*m_buffPtr++ = 1;
		return *this;
	}

	template<int decimalPlace=DoubleMaxDecimal-1>
	inline FixMessageBuilder& FixMessageBuilder::writeField(unsigned int tag, double doubleVal)
	{
		checkAndIncBuff();
		static_assert((decimalPlace < DoubleMaxDecimal) && (decimalPlace > 0), "writeDoubleWrongSTRLEN");
		int bytes = fixutils::writeInt(m_buffPtr, tag);
		m_buffPtr += bytes;
		*m_buffPtr++ = '=';
		int bytes2 = writeDouble<decimalPlace>(m_buffPtr, doubleVal);
		m_buffPtr += bytes2;
		*m_buffPtr++ = 1;
		return *this;
	}

	template<bool ifWriteMS = true>
	inline FixMessageBuilder& FixMessageBuilder::writeCurrentTime(unsigned int tag)
	{
		checkAndIncBuff();
		int bytes = fixutils::writeInt(m_buffPtr, tag);
		m_buffPtr += bytes;
		*m_buffPtr++ = '=';
		m_buffPtr+= fixutils::getFixSendingTimeMS<ifWriteMS>(m_buffPtr);
		*m_buffPtr++ = 1;
		return *this;
	}

	inline unsigned char FixMessageBuilder::getCheckSum() {
		unsigned char checksum = 0;
		const char* msg = m_buff;
		while (msg != m_buffPtr) {
			checksum += (unsigned char) *msg++;
		}
		return checksum;
	}

	void FixMessageBuilder::checkAndIncBuff(
#ifndef CHECK_BUFFER_LEN
			__UNUSED__
#endif
			int reserveSize) {
#ifdef CHECK_BUFFER_LEN
		if (__builtin_expect((m_buffEnd - m_buffPtr < reserveSize), 0)) {
			int newSize = m_buffEnd - m_buff + BUFFER_CHUNK_SIZE;
			char *newBuff = (char*) malloc(newSize);
			memcpy(newBuff, m_buff, m_buffPtr - m_buff);
			m_buffPtr = newBuff + (m_buffPtr - m_buff);
			free(m_buff);
			m_buff = newBuff;
			m_buffEnd = m_buff + newSize;
		}
#endif
	}
}
