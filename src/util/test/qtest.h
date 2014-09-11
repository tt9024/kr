#pragma once

#include "queue.h"
#include "thread_utils.h"
#include "time_util.h"
#include <vector>

using namespace utils;

struct ItemType {
    uint64_t _seq;
    uint64_t _ts;
};

template<typename WriterType>
class QueueWriter {
public:
    QueueWriter(int id, WriterType& wrt, int wait_micro) :
        _id(id), _num(0), _writer(wrt), _wait_micro(wait_micro), _should_run(false) {};

    void run(void* data_len_ptr) {
        int len = *((int*)data_len_ptr);
        ItemType* buf = (ItemType*)malloc(len*sizeof(ItemType));
        _should_run = true;
        while (_should_run) {
            uint64_t now = TimeUtil::cur_time_micro();
            ++_num;
            for (int i=0; i<len; ++i) {
                buf[i]._seq = _num;
                buf[i]._ts = now;
            }
            _writer.put((char*)buf);

            if ((_num & 0xffff) == 0 ) {
                printf("writer wrote %llu\n", _num);
            }
            now += _wait_micro;
            while (TimeUtil::cur_time_micro() < now);
        }
        printf("writer %d stopped.", _id);
    }

    void stop() {
        _should_run = false;
    }

private:
    const int _id;
    uint64_t _num;
    WriterType& _writer;
    int _wait_micro;
    bool _should_run;
};

template<typename ReaderType>
class QueueReader {
public:
    QueueReader(int id, ReaderType& rdr) :
        _id(id), _reader(rdr), _lat(0), _cnt(0), _should_run(false) {};

    void run(void* data_len_ptr) {
        int len = *((int*)data_len_ptr);
        ItemType* buf = (ItemType*)malloc(len*sizeof(ItemType));
        uint64_t num = 0;
        _lat = _cnt = 0;
        _should_run = true;
        while (_should_run) {
            utils::QStatus qstatus = _reader.copyNextIn((char*)buf);
            if (qstatus == utils::QStat_OK) {
                if (!checkContent(buf, len)) {
                    printf("reader %d found error at %llu, seek to top.\n", _id, (unsigned long long) num);
                    _reader.seekToTop();
                    continue;
                }
                ItemType *iptr = buf + (num%len);
                uint64_t new_num = iptr->_seq;
                if (num)
                {
                    if (num != new_num-1) {
                        printf("reader %d found gap of %lld, seek to top.\n", _id, (long long) (new_num - num - 1));
                        _reader.seekToTop();
                        num = 0;
                        continue;
                    }
                } else {
                    printf("reader %d started with %llu", _id, (unsigned long long) new_num);
                }
                num = new_num;
                uint64_t now = TimeUtil::cur_time_micro();
                _lat += (now - iptr->_ts);
                _cnt += 1;

                if ((_cnt & 0xffff) == 0) {
                    printf("reader %d latency: %llu (micro)\n", _id, (unsigned long long) _lat/_cnt);
                    _lat = 0;
                    _cnt = 0;
                }

                _reader.advance();

            } else {
                if (qstatus != QStat_EAGAIN) {
                    printf("reader %d got qstatus %s.\n", _id, QStatStr(qstatus));
                    _reader.seekToTop();
                }
            }
        }
        printf("reader %d Stopped.\n", _id);
    }

    void stop() {
        _should_run = false;
    }

private:
    const int _id;
    ReaderType& _reader;
    bool checkContent(ItemType* buf, int len) {
        for (int i=1; i<len; ++i) {
            if (memcmp(buf+i, buf, sizeof(ItemType)))
                return false;
        }
        return true;
    }
    uint64_t _lat, _cnt;
    bool _should_run;
};

#define ITEMLEN sizeof(ItemType)  /* seq + timestamp */
#define ITEMCNT 16
#define DATALEN (ITEMLEN*ITEMCNT) /* this represents the size of content, i.e. dob snapshot */
#define QLEN (1024*32*DATALEN)

