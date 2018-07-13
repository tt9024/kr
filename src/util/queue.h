#pragma once
#include <memory>
#include <string.h>
#include <stdlib.h>
#include <stdexcept>

#include "circular_buffer.h"
#include <vector>

/** This is for multi-threaded environment.
 * Goal is to achieve multi-write, multi-read
 * lock-free read/write. Each put/take is
 * also wait-free except in unlikely situation
 * where writer can spin on bursty concurrent
 * write operations.
 */

namespace utils {

    // This is a fixed size, lossy single writer multiple reader queue.
    // The writer does not check the overflow on readers, and it always
    // writes to queue without blocking. Overflow can be detected at
    // the reader side and can be somewhat avoided by increasing buffer size.
    // The reader has to check queue status on each read.
    template<int QLen, int DataLen, template<int, int> class BufferType = CircularBuffer>
    class SwQueue
    {
    public:
        class Writer;
        class Reader;
        // this is for multi-threaded environment using heap
        SwQueue() : m_name(""), m_writer(new Writer(*this)) {};

        // this is for multi-process environment using shm
        explicit SwQueue(const char* shm_name, bool read_only = true, bool init_to_zero = true) :
                m_name(shm_name), m_buffer(shm_name, read_only, init_to_zero),
                m_writer(read_only?NULL:new Writer(*this, init_to_zero)) {};

        ~SwQueue() {
            if (m_writer) {
                delete m_writer;
                m_writer = NULL;
            };
        };

        // the only writer
        Writer& theWriter() {
            if (!m_writer) {
                throw std::runtime_error("SwQueue writer instance NULL!");
            }
            return *m_writer;
        };

        // caller is responsible to delete this object
        Reader* newReader() {
            return new Reader(*this);
        }

    private:
        volatile QPos *getPtrReadyBytes() const { return (volatile QPos*) m_buffer.getHeaderStart(); } ;
        const std::string m_name;
        static const int HeaderLen = 64;   // one 64-bit counter for next writer position
        BufferType<QLen, HeaderLen> m_buffer;
        Writer* m_writer;
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
                m_buffer->template copyBytes<true>(*m_ready_bytes, content, DataLen);
                *m_ready_bytes += DataLen;
            }

            // for interface with the spin writer
            void put(const char* content) {
                putNoSpin(content);
            }

            // for in context writers. It's for performance
            // reasons: the QLen has to be multiple of Datalen
            // you need to getNextWritePtr(), update buffer
            // and when done, advanceWritePtr(), in sequence
            volatile char* getNextWritePtr() const {
                return m_buffer->getBufferPtr(*m_ready_bytes);
            }

            volatile char* advanceWritePtr() volatile {
                return (*m_ready_bytes += DataLen);
            }

        private:
            explicit Writer(SwQueue<QLen, DataLen, BufferType>& queue, bool init_to_zero=true)
            : m_buffer(&queue.m_buffer),
              m_ready_bytes(queue.getPtrReadyBytes())
            {
                if (init_to_zero)
                    *m_ready_bytes = 0;
            };
            explicit Writer(const Writer& writer);
            volatile BufferType<QLen, SwQueue<QLen, DataLen, BufferType>::HeaderLen>* const m_buffer;
            volatile QPos* const m_ready_bytes;
            // for private constructor access
            friend class SwQueue<QLen, DataLen, BufferType>;
        };


        // Readers share access to the queue,
        // has it's own read position
        class Reader {
        public:
            QStatus copyNextIn(char* buffer) {
                QPos pos = *m_ready_bytes;
                long long bytes = (long long) (pos - m_pos);

                if (bytes == 0) {
                    return QStat_EAGAIN;
                }
                if (__builtin_expect((bytes > QLen - DataLen), 0)) {
                    return QStat_OVERFLOW;
                }
                if (__builtin_expect((bytes < 0), 0)) {
                    // writer restart detected
                    seekToTop();
                    return copyNextIn(buffer);
                }
                m_buffer->template copyBytes<false>(m_pos, buffer, DataLen);
                // check overflow after read
                if (__builtin_expect(((*m_ready_bytes - m_pos) > QLen - DataLen), 0)) {
                    return QStat_OVERFLOW;
                }
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
                    m_buffer->template copyBytes<false>(m_pos, m_localBuffer, DataLen);
                    buffer = m_localBuffer;
                }
                return QStat_OK;
            }

            void seekToTop() {
                QPos pos = *m_ready_bytes;
                if (__builtin_expect((pos == 0), 0)) {
                    m_pos = 0;
                    return ;
                }
                m_pos = pos - DataLen;
            }

            int catchUp() {
                QPos pos = *m_ready_bytes;
                QPos prev_pos = m_pos;
                while (pos - m_pos > QLen - DataLen) {
                    m_pos += DataLen;
                }
                return (m_pos - prev_pos)/DataLen;
            }

            bool advanceToTop() {
            	QPos pos = m_pos;
            	seekToTop();
            	if (__builtin_expect(pos == 0, 0)) {
            		return false;
            	}
            	if (__builtin_expect(pos <= m_pos,1)) {
            		return true;
            	}
				if (__builtin_expect( pos == m_pos + DataLen, 0)) {
					m_pos = pos;  // get back for next read
				}
				// note this accounts for loop back
				// where m_pos could be much less than pos
				// next read will reflect
				return false;
            }

            void seekToBottom() {
                QPos pos = *m_ready_bytes;
                m_pos = (pos > (QLen - DataLen))? (pos - QLen - DataLen) : 0;
            }

            void advance() { m_pos += DataLen; };
            void syncPos() { m_pos = *m_ready_bytes;};
            QPos getPos() const { return m_pos; };
            //void setPos(QPos pos) { m_pos = pos; };

        private:
            explicit Reader(SwQueue<QLen, DataLen, BufferType>& queue)
            : m_buffer(&queue.m_buffer),
              m_ready_bytes(queue.getPtrReadyBytes()),
              m_pos(*m_ready_bytes)
            {
                //seekToBottom();
                //seekToTop();
                // initialize m_pos with the existing ready bytes and
                // go from there
                syncPos();
            }
            Reader(const Reader&);
            volatile BufferType<QLen, SwQueue<QLen, DataLen, BufferType>::HeaderLen>* const m_buffer;
            const volatile QPos* const m_ready_bytes;
            QPos m_pos;
            char m_localBuffer [DataLen];
            // for private constructor access
            friend class SwQueue<QLen, DataLen, BufferType>;
        };
    };

    // FIXME  ---
    //     ADD READER POSITION TO THE CIRCULAR QUEUE
    //     TO SUPPORT MULTI-PROCESS SHARED MEMROY
    // ------
    //
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

        Writer* newWriter() {
            return &m_writer;
        }

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
                m_buffer->template copyBytes<true>(*m_ready_bytes, content, DataLen);
                *m_ready_bytes += DataLen;
            }

            void put(const char* content) {
                putSpin(content);
            }

        private:
            explicit Writer(SwQueueLossless<QLen, DataLen>& queue)
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
                m_buffer->template copyBytes<false>(rPos, buffer, DataLen);
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
                    m_buffer->template copyBytes<false>(rPos, m_localBuffer, DataLen);
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
            explicit Reader(SwQueueLossless<QLen, DataLen>& queue)
            : m_buffer(&queue.m_buffer),
              m_ready_bytes(queue.getPtrReadyBytes),
              m_pos(0) { seekToBottom(); }
            Reader(const Reader&);
            volatile CircularBuffer<QLen, SwQueueLossless<QLen, DataLen>::HeaderLen>* const m_buffer;
            const volatile QPos* const m_ready_bytes;
            volatile QPos m_pos;
            char m_localBuffer [DataLen];
            // for private constructor access
            friend class SwQueueLossless<QLen, DataLen>;
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
            explicit Reader(MwQueue<QLen>& queue)
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
            const volatile QPos* const m_pos_write; // readonly volatile pointer to a ever changing memory location
            const volatile QPos* const m_ready_bytes;
            QPos m_pos;
            mutable char* m_localBuffer;
            mutable char m_localBufferSize;
        };

        // each writer will have shared access to the queue
        class Writer {
        public:
            explicit Writer(MwQueue<QLen>& queue)
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
        m_buffer.template copyBytes<true>(pos, (char*) &bytes, sizeof(int));
        m_buffer.template copyBytes<true>(pos + sizeof(int), (char*) content, bytes);
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

    // TODO
    // create a MwQueue for fixed len items, so we don't
    // need to put len in and don't need to check wouldCrossBoundary.
    // Useful for passing normalized items like orders and executions
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
        m_buffer.template copyBytes<false>(m_pos, (char*)&bytes, sizeof(int));
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
            m_buffer.template copyBytes<false>(data_pos, m_localBuffer, bytes);
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
        m_buffer.template copyBytes<false>(m_pos, (char*)&bytes, sizeof(int));
        m_buffer.template copyBytes<false>(m_pos + sizeof(int), buffer, bytes);
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
                m_buffer.template copyBytes<false>(pos, (char*)&bytes, sizeof(int));
                pos += (sizeof(int) + bytes);
                unread_bytes -= (sizeof(int) + bytes);
            }
            return QStat_OK;
        }
        return QStat_OVERFLOW;
    }

    // an equal length version
    template<int QLen, int DataLen, template<int, int> class BufferType = CircularBuffer>
    class MwQueue2
    {
    public:
        class Reader;
        class Writer;

        // this is for multi-threaded environment using heap
        explicit MwQueue2(const char* queue_name = "ThreadMwQueue2") : m_name(queue_name) {
            static_assert(QLen == (QLen/DataLen*DataLen), "QLen has to be a multiple of DataLen");
        };

        // this is for multi-process environment using shm
        MwQueue2(const char* shm_name, bool read_only, bool init_to_zero = true) :
                m_name(shm_name), m_buffer(shm_name, read_only, init_to_zero) {};

        ~MwQueue2() {
            for (size_t i = 0; i<_writersToDelete.size(); ++i) {
                delete (Writer*) (_writersToDelete[i]);
            }
            _writersToDelete.clear();
        };
        Reader* newReader() {
            // the caller responsible for deleting the instance
            return new Reader(*this);
        }
        Writer* newWriter() {
            // the caller responsible for deleting the instance
            return new Writer(*this);
        }

        Writer& theWriter() {
            Writer* wtr = new Writer(*this);
            _writersToDelete.push_back(wtr);
            return *wtr;
        }

        volatile QPos *getPtrPosWrite() const { return (volatile QPos*) m_buffer.getHeaderStart() ; };
        volatile QPos *getPtrPosDirty() const { return (volatile QPos*) (m_buffer.getHeaderStart() + sizeof(QPos)) ; };
        volatile QPos *getPtrReadyBytes() const { return (volatile QPos*) (m_buffer.getHeaderStart() + 64); } ;
        std::vector<Writer*> _writersToDelete;

    private:
        static const int HeaderLen = 128;   // three 64-bit counters, occupying 2 cache lines
                                            // one for writers - write + dirty, one for read
                                            // also aligning to the cache line
        const std::string m_name;
        static const int ItemCount = (QLen / DataLen);
        BufferType<QLen, HeaderLen> m_buffer;

    public:
        // each reader will have have shared access to the queue,
        // has it's own read position
        class Reader {
        public:
            typedef MwQueue2<QLen, DataLen, BufferType> QType;
            explicit Reader(QType& queue)
            : m_buffer(queue.m_buffer),
              m_pos_write(queue.getPtrPosWrite()),
              m_ready_bytes(queue.getPtrReadyBytes()),
              m_pos(0)
              {
                syncPos();
              };

            QStatus copyNextIn(char* buffer) ;
            QStatus copyPosIn(char* buffer, QPos pos);

            // this doesn't check if the earlier writes
            // is also available.
            // It could be the case when this position is
            // ready for read but pos - 1 is not.
            // In this case this random access version will still
            // read pos, but the previous safe version will
            // wait until all writes below pos is ready. i.e.
            // m_ready_bytes >= pos.
            // Should be fine with Quite id, but not good
            // with sequential access of the multiple writers'
            // content such as log lines
            // This random access version still checks for over flows.
            // It should be sufficient for QuoteIDStore
            QStatus copyPosInRandomAccess(char* buffer, QPos pos);


            // this doesn't copy the bytes in most cases,
            // because QLen is a multiple of Datalen,
            // it should never go across the circular buffer boundary
            QStatus takeNextPtr(volatile char*& buffer) ;
            QStatus seekToTop();

            // this just set the reader position to the
            // existing writer position
            void syncPos() { m_pos = *m_ready_bytes; };
            void advance() { m_pos += DataLen; };
            void reset()   { m_pos = 0; };
            QPos getWritePos() const { return *m_ready_bytes;};
            ~Reader() {};
            std::string dump_state() const;
        private:
            const BufferType<QLen, QType::HeaderLen>& m_buffer;
            // readonly volatile pointer to a ever changing memory location
            const volatile QPos* const m_pos_write;
            const volatile QPos* const m_ready_bytes;
            QPos m_pos;
        };

        // each writer will have shared access to the queue
        class Writer {
        public:
            typedef MwQueue2<QLen, DataLen, BufferType> QType;
            explicit Writer(QType& queue)
            : m_buffer(queue.m_buffer),
              m_pos_write(queue.getPtrPosWrite()),
              m_pos_dirty(queue.getPtrPosDirty()),
              m_ready_bytes(queue.getPtrReadyBytes())
            {
            	// Just continue to write on the position
            	// of the existing positions
                // reset();
            };
            void reset() { *m_pos_write = 0; *m_pos_dirty = 0; *m_ready_bytes = 0;};
            QPos put(const char* content);

            // this will fail (and return false) if
            // the immediate write is not possible, i.e.
            // dirty bytes would exceed QLen
            bool putNoSpin(const char* content);
            long long get_pos_write() const {
            	return *m_pos_write;
            }
            std::string dump() const {
            	char buf[1024];
            	snprintf(buf, sizeof(buf), "%lld, %lld, %lld",
            			(long long) *m_pos_write,
						(long long) *m_pos_dirty,
						(long long) *m_ready_bytes);
            	return std::string(buf);
            }

        private:
            BufferType<QLen, QType::HeaderLen>& m_buffer;
            volatile QPos* const m_pos_write;
            volatile QPos* const m_pos_dirty;
            volatile QPos* const m_ready_bytes;
            QPos getWritePos();
            void finalizeWrite();
            static const int SpinThreshold = QLen/4;
        };
    };

    // it first get the write pos, write a size, write the content
    // and check if it detects a synchronous point and update read
    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    QPos MwQueue2<QLen, DataLen, BufferType>::Writer::put(const char* content) {
        QPos pos = getWritePos();
        m_buffer.template copyBytes<true>(pos, (char*) content, DataLen);
        finalizeWrite();
        return pos;
    }

    // it first get the write pos, write a size, write the content
    // and check if it detects a synchronous point and update read
    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    bool MwQueue2<QLen, DataLen, BufferType>::Writer::putNoSpin(const char* content) {
        if (__builtin_expect(((int)(*m_pos_write - *m_ready_bytes) >= SpinThreshold), 0)) {
            return false;
        }
        put(content);
        return true;
    }

    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    QPos MwQueue2<QLen, DataLen, BufferType>::Writer::getWritePos() {
        if (__builtin_expect(((int)(*m_pos_write - *m_ready_bytes) >= SpinThreshold), 0)) {
            while (1) {
                // spin for too many unfinished write
                QPos pos = *m_pos_write;
                while (__builtin_expect(((int)(pos - *m_ready_bytes) >= SpinThreshold), 0)) {
                    pos = *m_pos_write;
                } ;
                // multiple writers could be released here, release the first one
                // and put the rest back to spin
                QPos newPos = pos + DataLen;
                if (compareAndSwap(m_pos_write, pos, newPos) == pos) {
                    return pos;
                }
            }
        }
        // normal case
        return fetchAndAdd(m_pos_write, DataLen);
    }

    // m_pos_dirty is the total bytes written so far
    // m_pos_write is the current write starting position
    // m_ready_bytes is the current point upto where content is readable
    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    void MwQueue2<QLen, DataLen, BufferType>::Writer::finalizeWrite() {
        QPos dirty = AddAndfetch(m_pos_dirty, DataLen);
        QPos pos = *m_pos_write;

        // if the slow writer guard is turned on
        /*
        if (__builtin_expect(dirty > pos, 0)) {
        	while (*m_pos_dirty > pos) {
        	    AddAndfetch(m_pos_dirty, -DataLen);
        	    dirty -= DataLen;
        	}
        }
        */

        if (__builtin_expect( dirty == pos, 1)) {
            // detected a sync point
            // ready bytes are at least dirty bytes
            // try to update the read position
            QPos prev_ready_bytes = *m_ready_bytes;
            while (prev_ready_bytes < dirty) {
                // multiple writers may be updating ready_bytes
                // take the highest dirty value
                prev_ready_bytes = compareAndSwap(m_ready_bytes, prev_ready_bytes, dirty);
            }
        }

        // this is to guard against a slow write
        // blocking in the writing stage
        // Intended for heavily loaded faulty systems
        /*
        const int MAX_CPU_CORES = 128;
        if (__builtin_expect( (pos-dirty)/DataLen > MAX_CPU_CORES ,0)) {
        	AddAndFetch(m_pos_dirty, DataLen);
        	return;
        }
        */

    }

    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    QStatus MwQueue2<QLen, DataLen, BufferType>::Reader::takeNextPtr(volatile char*& buffer) {
        if (__builtin_expect((*m_pos_write - m_pos >= QLen), 0)) {
            return QStat_OVERFLOW;
        }
        QPos unread_bytes = *m_ready_bytes - m_pos;
        if (unread_bytes <= 0) {
            if (__builtin_expect((unread_bytes < 0),0)) {
                syncPos();
            }
            return QStat_EAGAIN;
        }
        // get size
        buffer = m_buffer.getBufferPtr(m_pos);
        return QStat_OK;
    };

    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    QStatus MwQueue2<QLen, DataLen, BufferType>::Reader::copyNextIn(char* buffer) {
        if (__builtin_expect((*m_pos_write - m_pos >= QLen), 0)) {
            return QStat_OVERFLOW;
        }
        QPos unread_bytes = *m_ready_bytes - m_pos;
        if (unread_bytes <= 0) {
            if (__builtin_expect((unread_bytes < 0),0)) {
                syncPos();
            }
            return QStat_EAGAIN;
        }
        // get size
        m_buffer.template CopyBytesFromBuffer(m_pos, buffer, DataLen);
        // check after copy
        if (__builtin_expect((*m_pos_write - m_pos >= QLen), 0)) {
            return QStat_OVERFLOW;
        }
        return QStat_OK;
    }

    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    std::string MwQueue2<QLen, DataLen, BufferType>::Reader::dump_state() const {
    	char buf[1024];
    	snprintf(buf, sizeof(buf), "%lld %lld %lld",
    			(long long) *m_pos_write,
				(long long) *m_ready_bytes,
				(long long) m_pos);
    	return std::string(buf);
    }

    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    QStatus MwQueue2<QLen, DataLen, BufferType>::Reader::copyPosIn(char*buffer, QPos pos) {
    	if (__builtin_expect((*m_pos_write - pos >=QLen),0)) {
    		return QStat_OVERFLOW;
    	}
    	QPos unread_bytes = *m_ready_bytes - pos;
    	if (__builtin_expect( (unread_bytes <= 0 ),0)) {
    		// spin for a short time of not too far
    		if (unread_bytes / DataLen > -10) {
    			int spin = 10000;
    			while (--spin > 0) {
    				unread_bytes = *m_ready_bytes - pos;
    				if (unread_bytes > 0)
    					break;
    			}
    			if (spin <= 0) {
    				return QStat_EAGAIN;
    			}
    		} else {
    		    return QStat_EAGAIN;
    		}
    	}
    	m_buffer.template CopyBytesFromBuffer(pos, buffer, DataLen);
    	// check after copy
    	if (__builtin_expect((*m_pos_write - pos >=QLen),0)) {
    		return QStat_OVERFLOW;
    	}
    	return QStat_OK;
    }

    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
	QStatus MwQueue2<QLen,DataLen,BufferType>::Reader::copyPosInRandomAccess(char*buffer,QPos pos) {
    	if (__builtin_expect((*m_pos_write - pos >=QLen),0)) {
    		return QStat_OVERFLOW;
    	}
    	m_buffer.template CopyBytesFromBuffer(pos, buffer,DataLen);
    	// check after copy
		if (__builtin_expect((*m_pos_write - pos >=QLen),0)) {
			return QStat_OVERFLOW;
		}
    	return QStat_OK;
    }

    // it traverse all the way to top, and get the position of top update,
    template<int QLen, int DataLen, template<int, int> class BufferType>
    inline
    QStatus MwQueue2<QLen, DataLen, BufferType>::Reader::seekToTop() {
        int unread_bytes = *m_ready_bytes - m_pos;
        if (unread_bytes == 0)
            return QStat_EAGAIN;

        m_pos = *m_ready_bytes - DataLen;
        return QStat_OK;
    }

}
