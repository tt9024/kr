#pragma once
#include <memory>
#include <string.h>
#include <stdlib.h>
#include <stdexcept>

/** This is for multi-threaded environment.
 * Goal is to achieve multi-write, multi-read
 * lock-free read/write. Each put/take is
 * also wait-free except in unlikely situation
 * where writer can spin on bursty concurrent
 * write operations.
 */

namespace utils {
	typedef long long QPos;

	inline QPos fetchAndAdd(QPos* pos, int val) {
		return __sync_fetch_and_add(pos, val);
	}
	inline QPos AddAndfetch(QPos* pos, int val) {
		return __sync_add_and_fetch(pos, val);
	}
	inline QPos compareAndSwap(QPos* pos, QPos old_pos, QPos new_pos) {
		return __sync_val_compare_and_swap(pos, old_pos, new_pos);
	}

	template<bool isInbound>
	inline void CopyBytes(char* buf1, char* buf2, int bytes) {};
	template<>
	inline void CopyBytes<true>(char* buf1, char* buf2, int bytes) {
			memcpy(buf1, buf2, bytes);
	}
	template<>
	inline void CopyBytes<false>(char* buf1, char* buf2, int bytes) {
			memcpy(buf2, buf1, bytes);
	}

	// The circular buffer stores the content in the first QLen bytes
	// and the header information at the end
	template<int QLen, int HeaderBytes>
	class CircularBuffer {
	public:
		CircularBuffer() : m_buffer((char*)malloc(QLen + HeaderBytes)) {
			static_assert(((QLen-1)&(QLen)) == 0, "CircularBuffer: qlen not power of 2");
			if (!m_buffer)
				throw std::runtime_error("CircularBuffer: malloc failed");
			// zero out everything
			memset((void*)m_buffer, 0, QLen + HeaderBytes);
		}

		~CircularBuffer() {
			free((void*)m_buffer);
			m_buffer = NULL;
		}
		char* getHeaderStart() const { return m_buffer + QLen; };
		char* getBufferStart() const { return m_buffer; };

		// this will break if msg len are larger than QLen
		// this should be assured by the caller. Otherwise
		// behavior is unspecified
		// it returns the pointer of the next write location
		// within the circular buffer
		template<bool ifInbound>
		void copyBytes(QPos start_pos, char* content, int bytes) volatile {
			int s_pos = start_pos & QMask;
			int e_pos = (start_pos + bytes) & QMask;
			if (e_pos < s_pos) {
				int len1 = QLen - s_pos;
				CopyBytes<ifInbound>((char*) (m_buffer + s_pos), content, len1);
				content += len1;
				bytes -= len1;
				s_pos = 0;
			}
			CopyBytes<ifInbound>((char*) (m_buffer + s_pos), content, bytes);
		}

		// check if the contents of bytes starting from start_pos would
		// cross the boundary.  If not, buffer will be the starting pointer
		bool wouldCrossBoundary(const QPos start_pos, int bytes, char*& buffer) const volatile {
			QPos s_pos = (start_pos & QMask);
			buffer = m_buffer + s_pos;
			return s_pos < ((start_pos + bytes) & QMask);
		}

	private:
		volatile char* const m_buffer;
		static const int QMask = QLen - 1;
	};

	enum QStatus {
		QStat_OK,
		QStat_EAGAIN,
		QStat_OVERFLOW,
		QStat_ERROR
	};

	// This is a fixed size, lossy single writer multiple reader queue.
	// The writer does not check the overflow on readers, and it always
	// writes to queue without blocking. Overflow can be detected at
	// the reader side and can be somewhat avoided by increasing buffer size.
	// The reader has to check queue status on each read.
	// Both QLen and DataLen has to be power of 2.
	template<int QLen, int DataLen>
	class SwQueue
	{
	public:
		class Writer;
		class Reader;
		SwQueue() : m_writer(*this) {};
		~SwQueue() {};
		Reader* newReader() {
			// caller is responsible to delete this object
			return new Reader(*this);
		}
		// the only writer
		Writer& theWriter() {
			return m_writer;
		};

	private:
		volatile QPos *getPtrReadyBytes() const { return (volatile QPos*) m_buffer.getHeaderStart(); } ;
		static const int HeaderLen = 64;   // one 64-bit counter for next writer position
		CircularBuffer<QLen, HeaderLen> m_buffer;
		Writer m_writer;
		// in case the compiler uses c++03
		friend class Reader;
		friend class Writer;

	public:
		// only one writer will have shared access to the queue
		class Writer {
		public:
			// just put the content to the queue, slow readers
			// could get overflow.  C++ ensures the order of operation on
			// two volatile pointers are honored.
			void putNoSpin(const char* content) {
				m_buffer->copyBytes<true>(*m_ready_bytes, content, DataLen);
				*m_ready_bytes += DataLen;
			}

		private:
			explicit Writer(const SwQueue<QLen, DataLen>& queue)
			: m_buffer(&queue.m_buffer),
			  m_ready_bytes(queue.getPtrReadyBytes())
			{
				*m_ready_bytes = 0;
			};
			explicit Writer(const Writer& writer);
			volatile CircularBuffer<QLen, SwQueue<QLen, DataLen>::HeaderLen>* const m_buffer;
			volatile QPos* const m_ready_bytes;
			// for private constructor access
			friend class SwQueue<QLen, DataLen>;
		};


		// Readers share access to the queue,
		// has it's own read position
		class Reader {
		public:
			QStatus copyNextIn(char* buffer) {
				QPos pos = *m_ready_bytes;
				long long bytes = (long long) (pos - m_pos);
				if (bytes <= 0) {
					return QStat_EAGAIN;
				}
				if (__builtin_expect((bytes > QLen - DataLen), 0)) {
					return QStat_OVERFLOW;
				}
				m_buffer->copyBytes<false>(m_pos, buffer, DataLen);
				return QStat_OK;
			}

			// this doesn't copy the bytes
			QStatus takeNextPtr(char*& buffer) {
				QPos pos = *m_ready_bytes;
				long long bytes = (long long)(pos - m_pos);
				if (bytes <= 0) {
					return QStat_EAGAIN;
				}
				if (__builtin_expect((bytes > QLen - DataLen), 0)) {
					return QStat_OVERFLOW;
				}
				if (__builtin_expect(m_buffer->wouldCrossBoundary(m_pos, DataLen, buffer), 0))
				{
					// we cannot just return the pointer to the content since the
					// content would cross circular boundary.
					// Copy the bytes locally and return;
					m_buffer->copyBytes<false>(m_pos, m_localBuffer, DataLen);
					buffer = m_localBuffer;
				}
				return QStat_OK;
			}

			void seekToTop() {
				QPos pos = *m_ready_bytes;
				if (__builtin_expect((pos == 0), 0))
					m_pos = 0;
				m_pos = pos - DataLen;
			}

			void seekToBottom() {
				QPos pos = *m_ready_bytes;
				QPos pos_lowest = (pos > QLen)? (pos - QLen) : 0;
				int ready_bytes = pos - pos_lowest;
				while (ready_bytes >= DataLen) {
					ready_bytes -= DataLen;
				}
				m_pos = pos_lowest + ready_bytes;
			}

			void advance() { m_pos += DataLen; };
			QPos getPos() const { return m_pos; };
			void setPos(QPos pos) { m_pos = pos; };

		private:
			explicit Reader(const SwQueue<QLen, DataLen>& queue)
			: m_buffer(&queue.m_buffer),
			  m_ready_bytes(queue.getPtrReadyBytes),
			  m_pos(0) { seekToBottom(); }
			Reader(const Reader&);
			volatile CircularBuffer<QLen, SwQueue<QLen, DataLen>::HeaderLen>* const m_buffer;
			const volatile QPos* const m_ready_bytes;
			QPos m_pos;
			char m_localBuffer [DataLen];
			// for private constructor access
			friend class SwQueue<QLen, DataLen>;
		};
	};

	// This is a fixed size, lossless single writer multiple reader queue.
	// The writer keeps track of all readers and spin on any overflow.
	// So readers don't need to check for overflow.
	// Both QLen and DataLen has to be power of 2.
	template<int QLen, int DataLen>
	class SwQueueLossless
	{
	public:
		class Writer;
		class Reader;
		SwQueueLossless() : m_writer(*this), m_numReaders(0) {};
		~SwQueueLossless() {};
		Reader* newReader() {
			// caller is responsible to delete this object
			if (m_numReaders >= MaxReaders)
				throw std::runtime_error("too many Reader created!");
			if (!(m_readers[m_numReaders++] = new Reader(*this)))
				throw std::runtime_error("Reader failed to create!");
			return m_readers[m_numReaders-1];
		}
		// the only writer
		Writer& theWriter() {
			return m_writer;
		};

	private:
		volatile QPos *getPtrReadyBytes() const { return (volatile QPos*) m_buffer.getHeaderStart(); } ;
		static const int HeaderLen = 64;   // one 64-bit counter for next writer position
		static const int MaxReaders = 16;
		CircularBuffer<QLen, HeaderLen> m_buffer;
		Writer m_writer;
		volatile Reader* m_readers[MaxReaders];
		int m_numReaders;
		// in case the compiler uses c++03
		friend class Reader;
		friend class Writer;

	public:
		// only one writer will have shared access to the queue
		class Writer {
		public:
			void putSpin(const char* content) {
				QPos pos = *m_ready_bytes;
				for (int i=0; i<m_queue.m_numReaders; ++i) {
					while (__builtin_expect((pos - m_readers[i]->getPos() > QLen - DataLen), 0)) {}
				}
				m_buffer->copyBytes<true>(*m_ready_bytes, content, DataLen);
				*m_ready_bytes += DataLen;
			}

		private:
			explicit Writer(const SwQueueLossless<QLen, DataLen>& queue)
			: m_queue(queue),
			  m_buffer(&queue.m_buffer),
			  m_ready_bytes(queue.getPtrReadyBytes())
			{
				*m_ready_bytes = 0;
			};
			explicit Writer(const Writer& writer);
			SwQueueLossless<QLen, DataLen>& m_queue;
			volatile CircularBuffer<QLen, SwQueueLossless<QLen, DataLen>::HeaderLen>* const m_buffer;
			volatile QPos* const m_ready_bytes;
			// for private constructor access
			friend class SwQueueLossless<QLen, DataLen>;
		};

		// Readers share access to the queue,
		// has it's own read position, made visible to the writer
		// No overflow check is performed as the writer will wait
		// on slowest reader for queue full
		class Reader {
		public:
			QStatus copyNextIn(char* buffer) {
				QPos wPos = *m_ready_bytes;
				QPos rPos = m_pos;
				if (wPos <= rPos) {
					return QStat_EAGAIN;
				}
				m_buffer->copyBytes<false>(rPos, buffer, DataLen);
				return QStat_OK;
			}

			// this doesn't copy the bytes
			QStatus takeNextPtr(char*& buffer) {
				QPos wPos = *m_ready_bytes;
				QPos rPos = m_pos;
				if (wPos <= rPos) {
					return QStat_EAGAIN;
				}
				if (__builtin_expect(m_buffer->wouldCrossBoundary(rPos, DataLen, buffer), 0))
				{
					// we cannot just return the pointer to the content since the
					// content would cross circular boundary.
					// Copy the bytes locally and return;
					m_buffer->copyBytes<false>(rPos, m_localBuffer, DataLen);
					buffer = m_localBuffer;
				}
				return QStat_OK;
			}

			void seekToTop() {
				QPos pos = *m_ready_bytes;
				if (__builtin_expect((pos == 0), 0))
					m_pos = 0;
				m_pos = pos - DataLen;
			}

			void seekToBottom() {
				QPos pos = *m_ready_bytes;
				QPos pos_lowest = (pos > QLen)? (pos - QLen) : 0;
				int ready_bytes = pos - pos_lowest;
				while (ready_bytes >= DataLen) {
					ready_bytes -= DataLen;
				}
				m_pos = pos_lowest + ready_bytes;
			}

			void advance() { m_pos += DataLen; };
			QPos getPos() const volatile { return m_pos; };
			QPos getPosVolatile() const volatile { return m_pos; };
			void setPos(QPos pos) { m_pos = pos; };

		private:
			explicit Reader(const SwQueue<QLen, DataLen>& queue)
			: m_buffer(&queue.m_buffer),
			  m_ready_bytes(queue.getPtrReadyBytes),
			  m_pos(0) { seekToBottom(); }
			Reader(const Reader&);
			volatile CircularBuffer<QLen, SwQueue<QLen, DataLen>::HeaderLen>* const m_buffer;
			const volatile QPos* const m_ready_bytes;
			volatile QPos m_pos;
			char m_localBuffer [DataLen];
			// for private constructor access
			friend class SwQueue<QLen, DataLen>;
		};
	};

	// the multi-writer queue, data can be uneuqal length
	// the three counters, write_pos, sync_pos and dirty_count
	// are stored in circular buffer as well
	// the MaxBurstSize is the maximum number of bytes that
	// can be dirty but not sync'ed to be readable
	template<int QLen>
	class MwQueue
	{
	public:
		class Reader;
		class Writer;
		MwQueue() {};
		~MwQueue() {};
		Reader* newReader() {
			// the caller responsible for deleting the instance
			return new Reader(*this);
		}
		Writer* newWriterer() {
			// the caller responsible for deleting the instance
			return new Writer(*this);
		}
		volatile QPos *getPtrPosWrite() const { return (volatile QPos*) m_buffer.getHeaderStart() ; };
		volatile QPos *getPtrPosDirty() const { return (volatile QPos*) (m_buffer.getHeaderStart() + sizeof(QPos)) ; };
		volatile QPos *getPtrReadyBytes() const { return (volatile QPos*) (m_buffer.getHeaderStart() + 64); } ;

	private:
		static const int HeaderLen = 128;   // three 64-bit counters, occupying 2 cache lines
										    // one for writers - write + dirty, one for read
										    // also aligning to the cache line
		CircularBuffer<QLen, HeaderLen> m_buffer;

	public:
		// each reader will have have shared access to the queue,
		// has it's own read position
		class Reader {
		public:
			explicit Reader(const MwQueue<QLen>& queue)
			: m_buffer(queue.m_buffer),
			  m_pos_write(queue.getPtrPosWrite()),
			  m_ready_bytes(queue.getPtrReadyBytes),
			  m_pos(0),
			  m_localBuffer(NULL),
			  m_localBufferSize(0) {};

			QStatus copyNextIn(char* buffer, int& bytes) const;
			// this doesn't copy the bytes in most cases, except the content
			// go across the circular buffer boundary
			QStatus takeNextPtr(char*& buffer, int& bytes) const;
			QStatus seekToTop();
			void advanceReadPos(int bytes);
			void reset() { m_pos = 0; };
			~Reader() {
				if (m_localBuffer) {
					free(m_localBuffer); m_localBuffer = NULL ;
				};
			};
		private:
			const CircularBuffer<QLen, MwQueue::HeaderLen>& m_buffer;
			const volatile QPos* const m_pos_write;	// readonly volatile pointer to a ever changing memory location
			const volatile QPos* const m_ready_bytes;
			QPos m_pos;
			mutable char* m_localBuffer;
			mutable char m_localBufferSize;
		};

		// each writer will have shared access to the queue
		class Writer {
		public:
			explicit Writer(const MwQueue<QLen>& queue)
			: m_buffer(queue.m_buffer),
			  m_pos_write(queue.getPtrPosWrite()),
			  m_pos_dirty(queue.getPtrPosDirty()),
			  m_ready_bytes(queue.getPtrReadyBytes())
			{
				reset();
			};
			void reset() { *m_pos_write = 0; *m_pos_dirty = 0; *m_ready_bytes = 0;};
			void put(const char* content, int bytes);

		private:
			CircularBuffer<QLen, MwQueue::HeaderLen>& m_buffer;
			volatile QPos* const m_pos_write;
			volatile QPos* const m_pos_dirty;
			volatile QPos* const m_ready_bytes;
			QPos getWritePos(int bytes);
			void finalizeWrite(int bytes);
		};
	};


	// it first get the write pos, write a size, write the content
	// and check if it detects a synchronous point and update read
	template<int QLen>
	inline
	void MwQueue<QLen>::Writer::put(const char* content, int bytes) {
		int total_bytes = bytes + sizeof(int);
		QPos pos = getWritePos(total_bytes);
		m_buffer.copyBytes<true>(pos, (char*) &bytes, sizeof(int));
		m_buffer.copyBytes<true>(pos + sizeof(int), (char*) content, bytes);
		finalizeWrite(total_bytes);
	}

	template<int QLen>
	inline
	QPos MwQueue<QLen>::Writer::getWritePos(int bytes) {
		if (__builtin_expect(((int)(*m_pos_write - *m_ready_bytes) >= QLen/2), 0)) {
			while (1) {
				// spin for too many unfinished write
				QPos pos = *m_pos_write;
				while (__builtin_expect(((int)(pos - *m_ready_bytes) >= QLen/2), 0)) {
					pos = *m_pos_write;
				} ;
				// multiple writers could be released here, release the first one
				// and put the rest back to spin
				QPos newPos = pos + bytes;
				if (compareAndSwap(m_pos_write, pos, newPos) == pos) {
					return pos;
				}
			}
		}
		// normal case
		return fetchAndAdd(m_pos_write, bytes);
	}

	template<int QLen>
	inline
	void MwQueue<QLen>::Writer::finalizeWrite(int bytes) {
		QPos dirty = AddAndfetch(m_pos_dirty, bytes);
		if (dirty == *m_pos_write) {
			// detected a sync point
			// ready bytes are at least dirty bytes
			// try to update the read position
			QPos prev_ready_bytes = *m_ready_bytes;
			while (prev_ready_bytes < dirty) {
				// multiple writers may be updating ready_bytes
				// take the hight dirty value
				prev_ready_bytes = compareAndSwap(m_ready_bytes, prev_ready_bytes, dirty);
			}
		}
	}

	template<int QLen>
	inline
	void MwQueue<QLen>::Reader::advanceReadPos(int bytes) {
		m_pos += (bytes + sizeof(int));
	};

	template<int QLen>
	inline
	QStatus MwQueue<QLen>::Reader::takeNextPtr(char*& buffer, int& bytes) const {
		if (__builtin_expect((*m_pos_write - m_pos >= QLen), 0)) {
			return QStat_OVERFLOW;
		}
		QPos unread_bytes = *m_ready_bytes - m_pos;
		if (unread_bytes == 0)
			return QStat_EAGAIN;
		// get size
		m_buffer.copyBytes<false>(m_pos, (char*)&bytes, sizeof(int));
		QPos data_pos = m_pos + sizeof(int);
		if (__builtin_expect(m_buffer.wouldCrossBoundary(data_pos, bytes, buffer), 0))
		{
			// we cannot just return the pointer to the content since the
			// content would cross circular boundary.
			// Copy the bytes locally and return;
			if (__builtin_expect((!m_localBuffer || (m_localBufferSize<bytes)), 0)) {
				if (m_localBuffer) {
					free( m_localBuffer );
				}
				m_localBuffer = (char*) malloc(bytes);
				m_localBufferSize = bytes;
			}
			m_buffer.copyBytes<false>(data_pos, m_localBuffer, bytes);
			buffer = m_localBuffer;
		}
		return QStat_OK;
	};

	template<int QLen>
	inline
	QStatus MwQueue<QLen>::Reader::copyNextIn(char* buffer, int& bytes) const {
		if (__builtin_expect((*m_pos_write - m_pos >= QLen), 0)) {
			return QStat_OVERFLOW;
		}
		QPos unread_bytes = *m_ready_bytes - m_pos;
		if (unread_bytes == 0)
			return QStat_EAGAIN;
		// get size
		m_buffer.copyBytes<false>(m_pos, (char*)&bytes, sizeof(int));
		m_buffer.copyBytes<false>(m_pos + sizeof(int), buffer, bytes);
		return QStat_OK;
	}

	// it traverse all the way to top, and get the position of top update,
	template<int QLen>
	inline
	QStatus MwQueue<QLen>::Reader::seekToTop() {
		if (__builtin_expect((*m_pos_write - m_pos >= QLen), 0)) {
			return QStat_OVERFLOW;
		}
		int unread_bytes = *m_ready_bytes - m_pos;
		if (unread_bytes == 0)
			return QStat_EAGAIN;

		if (__builtin_expect((unread_bytes < QLen), 1)) {
			QPos pos = m_pos;
			int bytes;
			while (unread_bytes > 0) {
				m_pos = pos;
				m_buffer.copyBytes<false>(pos, (char*)&bytes, sizeof(int));
				pos += (sizeof(int) + bytes);
				unread_bytes -= (sizeof(int) + bytes);
			}
			return QStat_OK;
		}
		return QStat_OVERFLOW;
	}
}
