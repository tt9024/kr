#include <Vulcan/Common/FixUtils/FixUtils.h>
#define InitialFieldSize 32

namespace fixutils {
	template<unsigned char fieldTerminator = FieldTerminator>
	class FixMessageReader {
	public:
		explicit FixMessageReader()
		: m_arrayCapacity(InitialFieldSize) {
			m_fieldArray = (FieldInfo*) malloc(sizeof(FieldInfo)*m_arrayCapacity);
			memset(m_fieldArray, 0, sizeof(FieldInfo)*m_arrayCapacity);
			bind(NULL, 0);
		}

		~FixMessageReader() {
			if (m_fieldArray) {
				free(m_fieldArray);
				m_fieldArray = NULL;
			}
		}

		// this reuses the fieldArray by reseting the cache
		// the capacity is allocated and unchanged
		void bind(const char* bodyStart, unsigned int bodyLen) {
			m_msg = bodyStart;
			m_msgEnd = bodyStart + bodyLen;
			m_msgPtr = m_msg;
			m_len = bodyLen;
			m_fieldCount = 0;
			m_allScaned = false;
			m_errPosition = NULL;
		}

		unsigned int getUnsignedInt(unsigned int tag, bool& ok) {
			ok = false;
			int tagidx = findTag(tag);
			if (tagidx >= 0) {
				return m_fieldArray[tagidx].getUnsignedInt(m_msg, ok);
			}
			return 0;
		}
		long long getLongInt(unsigned int tag, bool& ok) {
			ok = false;
			int tagidx = findTag(tag);
			if (tagidx >= 0) {
				return m_fieldArray[tagidx].getLongInt(m_msg, ok);
			}
			return 0;
		}
		double getDouble(unsigned int tag, bool& ok) {
			ok = false;
			int tagidx = findTag(tag);
			if (tagidx >= 0) {
				return m_fieldArray[tagidx].getDouble(m_msg, ok);
			}
			return 0;
		}
		char getChar(unsigned int tag, bool& ok) {
			ok = false;
			int tagidx = findTag(tag);
			if (tagidx >= 0) {
				return m_fieldArray[tagidx].getChar(m_msg, ok);
			}
			return (char) 0;
		}
		const char* getString(unsigned int tag, unsigned int &strLen, bool& ok) {
			ok = false;
			int tagidx = findTag(tag);
			if (tagidx >= 0) {
				return m_fieldArray[tagidx].getString(m_msg, strLen, ok);
			}
			return NULL;
		}

		// returns NULL if no error
		const char* getErrorPosition() const {
			return m_errPosition;
		}

		std::string dumpCache() {
			std::string retStr;
			for (int i = 0; i < m_fieldCount; ++i) {
				retStr += m_fieldArray[i].toString(m_msg).c_str();
				retStr += ", ";
			}
			return retStr;
		}

	private:
		// helper data structure needed to describe a FIX field
		struct FieldInfo {
			unsigned int tag;
			uint16_t offset_start;
			uint16_t len;
			// wrapper for using FixUtils functions
			unsigned int getUnsignedInt(const char* msgStart, bool& ok) const {
				ok = true;
				unsigned int val = 0;
				if (fixutils::getUnsignedIntUnknownLen(msgStart + offset_start, val) == -1) {
					ok = false;
				}
				return val;
			}
			long long getLongInt(const char* msgStart, bool& ok) const {
				return fixutils::getInt(msgStart+offset_start, ok);
			}
			double getDouble(const char* msgStart, bool& ok) const {
				return fixutils::getDouble(msgStart+offset_start, ok);
			}
			char getChar(const char* msgStart, bool& ok) const {
				ok = (len>0);
				return *(msgStart+offset_start);
			}
			const char* getString(const char* msgStart, unsigned int &strLen, bool& ok) const {
				ok = (len>0);
				strLen = len;
				return msgStart + offset_start;
			}
			std::string toString(const char*msg) const {
				char buf[128];
				int bytes = snprintf(buf, 128, "%u:%d-%d(", tag, (int)offset_start, (int)len);
				for (int i=0; i<len; ++ i) {
					snprintf(buf+bytes+i, 128-bytes-i, "%c", *(msg+offset_start+i));
				}
				snprintf(buf+bytes+len, 128-bytes-len, ")\n");
				return std::string(buf);
			}
		};

		// it first find from cache, if not found
		// it searches the unscreened message
		// It returns the index in the fieldArray if found,
		// -1 if not. In this case, if the tag reading is wrong,
		// it will set m_errorPosition
		int findTag(unsigned int tag) {
			for (int i=0; i<m_fieldCount; ++i) {
				if (m_fieldArray[i].tag == i) {
					return i;
				}
			}
			while (!m_allScaned) {
				if (readOneField() == tag) {
					return m_fieldCount - 1;
				}
			}
			return -1;
		}

		// scan one field from msg and cache it to Fields
		// m_msg should be read accessible up to m_len + 4 bytes
		// this can be ensured by not including checksum(10)
		// in the m_len.
		inline int readOneField() {
			int tag = 0;
			char ch;
			while (m_msgPtr < m_msgEnd) {
				ch = *m_msgPtr ++;
				if ((ch >= '0') && (ch <= '9')) {
					tag = (ch-'0') + tag * 10;
				}
			};
			if (__builtin_expect((tag == 0), 0)) {
				m_allScaned = true;
				if (m_msgPtr < m_msgEnd) {
					m_errPosition = m_msgPtr;
				}
				return 0;
			}
			++m_msgPtr;  // skip the '='
			FieldInfo& info ( getAndIncSlot() );
			info.tag = tag;
			info.offset_start = m_msgPtr-m_msg;
			do {
				register uint32_t val = *((uint32_t*)m_msgPtr);
				if ((unsigned char)(val) == fieldTerminator) {
					break;
				}
				if (((unsigned char)(val >> 8)) == fieldTerminator) {
					m_msgPtr += 1;
					break;
				}
				if (((unsigned char)(val >> 16)) == fieldTerminator) {
					m_msgPtr += 2;
					break;
				}
				if (((unsigned char)(val >> 24)) == fieldTerminator) {
					m_msgPtr += 3;
					break;
				}
				m_msgPtr += 4;
			} while (__builtin_expect((m_msgPtr < m_msgEnd), 1));
			info.len = m_msgPtr - m_msg - info.offset_start;
			++m_msgPtr;
			return tag; // skip the '\01'
		}

		// this is standard std::vector style array management
		// grows exponentially.
		FieldInfo& getAndIncSlot() {
			if (m_fieldCount == m_arrayCapacity) {
				FieldInfo* newArray = (FieldInfo*)malloc(sizeof(FieldInfo)* m_arrayCapacity * 2);
				memcpy(newArray, m_fieldArray, sizeof(FieldInfo)* m_arrayCapacity);
				memset(newArray + m_arrayCapacity, 0, sizeof(FieldInfo)* m_arrayCapacity);
				m_arrayCapacity *= 2;
				free (m_fieldArray);
				m_fieldArray = newArray;
			}
			return m_fieldArray[m_fieldCount++];
		}
		const char* m_msg, m_msgEnd;  // pointing to the start of the fix message
		const char* m_msgPtr;  // next field to be scanned in msg
		unsigned int m_len;  // total msg len
		FieldInfo* m_fieldArray;  // local cache of fields scanned so far
		int m_arrayCapacity;   // current array capacity
		int m_fieldCount;   // current number of fields in array
		bool m_allScaned;   // indicating if all fields are cached
		const char* m_errPosition;   // indicating error tag if encountered

	};

}
