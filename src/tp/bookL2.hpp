#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <unordered_map>

#include "plcc/PLCC.hpp"
#include "asset/security.hpp"

#include "time_util.h"
#include "queue.h"  // needed for SwQueue for BookQ

//#define MaxPriceLevels 8
//there is really no need to
//optimize this for now, just leave it for later
//#define PriceMult 1000000

namespace tp {

typedef int32_t Price;
typedef int32_t Quantity;
typedef uint64_t TSMicro;

struct PriceEntry {
#pragma pack(push,1)
    Price price;  // price is integer - pip adjusted and side signed (bid+ ask-)
    Quantity size;
    TSMicro ts_micro;
#pragma pack(pop)
    PriceEntry() : price(0), size(0), ts_micro(0) {};
    PriceEntry(Price p, Quantity s, TSMicro ts=0) : price(p), size(s), ts_micro(ts) {
        if (!ts) {
            ts_micro = utils::TimeUtil::cur_time_gmt_micro();
        }
    };
    void reset() {
        price = 0; size = 0;ts_micro=0;
    }

    Price getPrice(bool is_bid) const {
        return is_bid? price:-price;
    }

    double getPriceDouble(utils::eSecurity secid, bool is_bid) const {
        return pxToDouble(secid, getPrice(is_bid));
    }

    std::string toString(utils::eSecurity secid=utils::TotalSecurity, bool is_bid = true) const {
        double px = (secid >= utils::TotalSecurity)? (double) price : getPriceDouble(secid, is_bid);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.7lf,%lld,%llu", px, (long long)size, (unsigned long long) ts_micro);
        return std::string(buf);
    }
};

struct LastTrade {
    Price price;
    Quantity size;
    TSMicro ts_micro;
    uint16_t secid;
    uint16_t tags;
    uint32_t reserved;
};

struct BookConfig {
    std::string venue;
    std::string symbol;
    BookConfig(const std::string v, const std::string s) :
        venue(v), symbol(s) {};
};

#define BookLevel 8
struct BookDepot {
#pragma pack(push,1)
    // this structure needs to be aligned
    uint64_t update_ts_micro;  // this can be obtained from pe's ts
    // bid first, ask second
    PriceEntry pe[2*BookLevel];

    uint16_t security_id;
    uint16_t reserved;
    uint8_t update_level; // direct offset, no bid/ask adjustment
    uint8_t update_side;  // 0 is bid, 1 is ask
    uint8_t avail_level[2];
#pragma pack(pop)

    BookDepot() {
        memset(this, 0, sizeof(BookDepot));
    }
    explicit BookDepot(utils::eSecurity secid) {
        memset(this, 0, sizeof(BookDepot));
        security_id = secid;
    }
    BookDepot& operator = (const BookDepot& book) {
        if (&book == this) {
            return *this;
        }
        memcpy (this, &book, sizeof(BookDepot));
        return *this;
    }

    Price getBid() const {
        if ((avail_level[0] < 1))
            return 0;

        for (size_t i = 0; i<avail_level[0]; ++i) {
            if (pe[i].size > 0)
                return pe[i].getPrice(true);
        }
        return 0;
    }

    Price getAsk() const {
        if ((avail_level[1] < 1))
            return 0;
        for (size_t i = 0; i<avail_level[1]; ++i) {
            if (pe[BookLevel + i].size > 0)
                return pe[i].getPrice(false);
        }
        return 0;
    }

    Price getBestPrice(bool is_bid) const {
        return is_bid?getBid():getAsk();
    }

    Price getMid() const {
        if ((avail_level[0] < 1) || (avail_level[1] < 1))
            return 0;
        Price bid = pe[0].getPrice(true);
        Price ask = pe[BookLevel].getPrice(false);
        return (bid+ask)/2;
    }

    double getBidDouble() const {
        return pxToDouble((utils::eSecurity)security_id, getBid());
    }

    double getAskDouble() const {
        return pxToDouble((utils::eSecurity)security_id, getAsk());
    }

    double getMidDouble() const {
        return pxToDouble((utils::eSecurity)security_id, getMid());
    }

    std::string toString() const {
        char buf[1024];
        int n = 0;
        n += snprintf(buf+n, sizeof(buf)-n, "ID(%d) UpdateLevel(%s:%d) Time(%llu) ",
                (int)security_id, update_side==0?"BID":"ASK",
                (int) update_level, (unsigned long long) update_ts_micro);
        for (int s = 0; s < 2; ++s)
        {
            int levels = avail_level[s];
            n += snprintf(buf+n, sizeof(buf)-n, " %s(%d) [ ", s==0?"Bid":"Ask", levels);
            const PriceEntry* pe_ = &(pe[s*BookLevel]);
            for (int i = 0; i<levels; ++i) {
                if (pe_->size) {
                    n += snprintf(buf+n, sizeof(buf)-n, " (%d)%s ", i, pe_->toString((utils::eSecurity)security_id, s==0).c_str());
                }
                ++pe_;
            }
            n += snprintf(buf+n, sizeof(buf)-n, " ] ");
        }
        return std::string(buf);
    }
};

struct BookL2 {
    // BookDepot is a piece of memory storing normalized L2 book update.
    // The updates shows venue/asset/level/ts of updates, as well as the
    // the full snapshot of "BookLevel" prices on each side.
    // BookL2 is a wrapper of BookDepot, mainly for
    // updating methods from TP.  The reader
    // should be sufficient refering to BookDepot
    // to obtain all necessary updates.

    const BookConfig _cfg;
    const utils::eSecurity _sec_id;
    BookDepot _book;
    uint8_t* const _avail_level;

    explicit BookL2(const BookConfig& cfg):
            _cfg(cfg) ,_sec_id(utils::SecMappings::instance().getSecId(_cfg.symbol.c_str())),
            _book(_sec_id), _avail_level(_book.avail_level){
        reset();
    }

    std::string toString() const {
        char buf[1024];
        int n = 0;
        n += snprintf(buf+n, sizeof(buf)-n, "Book %s %s { %s }", _cfg.symbol.c_str(), _cfg.venue.c_str(), _book.toString().c_str());
        return std::string(buf);
    }

    // interface implementation for L2 book
    // this doesn't check for error values, such as level more than BookLevel
    bool newPrice(Price price, Quantity size, unsigned int level, bool is_bid, uint64_t ts_micro) {
        int side = is_bid?0:1;
        unsigned int levels = (unsigned int) _avail_level[side];
        // checking on the level
        if (__builtin_expect(((level>levels)  || (levels >= BookLevel)), 0)) {
            plccLogError("new price wrong level", level);
            return false;
            //throw new std::runtime_error("error!");
        }

        _book.update_level = level;
        _book.update_side = side;
        // move subsequent levels down
        PriceEntry* pe = getEntry(level, side);
        if (levels > level) {
            memmove(pe+1, pe, (levels - level)*sizeof(PriceEntry));
        };

        pe->price = is_bid?price : -price;
        pe->size = size;
        pe->ts_micro = ts_micro;

        ++(_avail_level[side]);
        return true;
    }

    bool delPrice(unsigned int level, bool is_bid) {
        int side = is_bid?0:1;
        if (__builtin_expect((level>=(unsigned int)_avail_level[side]), 0)) {
            plccLogError("del price wrong level", level);
            return false;
            //throw new std::runtime_error("error!");
        }

        _book.update_level = level;
        _book.update_side = side;
        // move subsequent levels up
        unsigned int levels = _avail_level[side];
        if (levels > level + 1) {
            PriceEntry* pe = getEntry(level, side);
            memmove(pe, pe+1, (levels-level-1)*sizeof(PriceEntry));
        };
        --(_avail_level[side]);
        return true;
    }

    bool updPrice(Price price, Quantity size, unsigned int level, bool is_bid, uint64_t ts_micro) {
        int side = is_bid?0:1;
        if (__builtin_expect((level>=(unsigned int)_avail_level[side]), 0)) {
            plccLogError("update price wrong level", level);
            return false;
            //throw new std::runtime_error("error!");
        }

        _book.update_level = level;
        _book.update_side = side;
        PriceEntry* pe = getEntry(level, side);

        // check if the update is necessary
        // Should this be a performance hit?
        pe->ts_micro = ts_micro;
        if (!is_bid) price = -price;
        if (__builtin_expect(((pe->price == price) && (pe->size == size)), 0)) {
            return false;
        }
        pe->price = price;
        pe->size = size;
        return true;
    }


    bool updBBO(Price price, Quantity size, bool is_bid, uint64_t ts_micro) {
        int side = is_bid?0:1;
        if (__builtin_expect((0 == _avail_level[side]), 0)) {
            return newPrice(price, size, 0, is_bid, ts_micro);
        }
        return updPrice(price, size, 0, is_bid, ts_micro);
    }

    // price only
    bool updBBOPriceOnly(Price price, bool is_bid, uint64_t ts_micro) {
        int side = is_bid?0:1;
        if (__builtin_expect((0==_avail_level[side]), 0)) {
            return newPrice(price, 0, 0, is_bid, ts_micro);
        }
        Quantity size = getEntry(0, side)->size;
        return updPrice(price, size, 0, is_bid, ts_micro);
    }

    // size only
    bool updBBOSizeOnly(Quantity size, bool is_bid, uint64_t ts_micro) {
        int side = is_bid?0:1;
        if (__builtin_expect((0 == _avail_level[side]), 0)) {
            return newPrice(0, size, 0, is_bid, ts_micro);
        }
        Price price = getEntry(0, side)->price;
        price = is_bid?price:-price;
        return updPrice(price, size, 0, is_bid, ts_micro);
    }

    void reset() {
        memset(&_book, sizeof(BookDepot), 0);
        _book.security_id = (uint16_t) _sec_id;
        _avail_level[0] = _avail_level[1] = 0;
    }

    void plccLogError(const char* msg, int number) {
        logError("book (%s %s) update error: "
                "%s, %d, book dump: %s",
                _cfg.symbol.c_str(), _cfg.venue.c_str(), msg, number, toString().c_str());
    }
    PriceEntry* getEntry(unsigned int level, int side) const {
        return (PriceEntry*) &(_book.pe[side*BookLevel+level]);
    }
};


// TODO - an even better way is to use
// seqlock to always provide the latest
// market data (skipping all the previous)
// writer and reader share one memory location
struct BookL2_SeqLock {
};

// TODO : Add Last Trade queue and add add writer/reader functions
//        just add a last Trade SwQueue, writer implements a function
//        that write a LastTrade structure to the queue.  Reader will
//        add a LastTrade reference to the getUpdate() to also check that.
// could be shm queue, or heap queue
// this is defined per-venue basis, i.e. each venue
// has one BookQ, covering all securities from that venue
// It always uses normalized level 2 format
// The writer and the readers has their own Books and Queues
template <template<int, int> class BufferType >
class BookQ {
public:
    static const int BookLen = sizeof(BookDepot);
    static const int QLen = (1024*BookLen);
    const std::vector<BookConfig> _cfg;
    const std::string _q_name;
    class Writer;
    class Reader;

    BookQ(const char* q_name, bool readonly, const std::vector<BookConfig>* config = NULL) :
        _cfg(config? *config : std::vector<BookConfig>()), _q_name(q_name), _q(q_name, readonly, true),
        _writer(readonly? NULL:new Writer(*this))
    {
        logInfo("BookQ %s started %s with %d configs.", q_name, readonly?"ReadOnly":"ReadWrite", (int)_cfg.size());
    };

    // This is to enforce that for SwQueue, at most one writer should
    // be created for each BookQ
    typedef utils::SwQueue<QLen, BookLen, BufferType> QType;
    Writer& theWriter() {
        if (!_writer)
            throw std::runtime_error("BookQ writer instance NULL");

        return *_writer;
    }

    Reader* newReader() {
        return new Reader(*this);
    }

    ~BookQ() {
        if (_writer) {
            delete _writer;
            _writer = NULL;
        }
    }
private:
    QType _q;
    Writer* _writer;
    friend class Writer;
    friend class Reader;

public:

    // Writer uses BookType interface of new|del|upd|Price()
    // and getL2(), it always writes L2 entries
    class Writer {
    public:
        // no checking on NULL pointer of book is performed
        // TP will ensure secid is valid. constructor of writer
        // will ensure book is not NULL
        void newPrice(utils::eSecurity secid, double price, Quantity size, int level, bool is_bid, uint64_t ts_micro) {
            BookL2* book = getBook(secid);
            if (__builtin_expect(book->newPrice(pxToInt(secid, price), size, level, is_bid, ts_micro), 1)) {
                updateQ(secid, ts_micro);
                return;
            }
            //logError("BookQ newPrice() security not updated %d", (int)secid);
        }

        void delPrice(utils::eSecurity secid, int level, bool is_bid, uint64_t ts_micro) {
            BookL2* book = getBook(secid);
            if (__builtin_expect((book->delPrice(level, is_bid)),1)) {
                updateQ(secid, ts_micro);
                return;
            }
            //logError("BookQ delPrice() security not updated %d", (int)secid);
        }

        void updPrice(utils::eSecurity secid, double price, Quantity size, int level, bool is_bid, uint64_t ts_micro) {
            BookL2* book = getBook(secid);
            if (__builtin_expect(book->updPrice(pxToInt(secid, price), size, level, is_bid, ts_micro),1)) {
                updateQ(secid, ts_micro);
                return;
            }
            //logError("BookQ updPrice() security not updated %d", (int)secid);
        }

        void updBBO(utils::eSecurity secid, double price, Quantity size, bool is_bid, uint64_t ts_micro) {
            BookL2* book = getBook(secid);
            if (__builtin_expect(book->updBBO(pxToInt(secid, price), size, is_bid, ts_micro), 1)) {
                updateQ(secid, ts_micro);
                return;
            }
            //logError("BookQ updBBO security not updated %d", (int) secid);
        }

        void updBBOPriceOnly(utils::eSecurity secid, double price, bool is_bid, uint64_t ts_micro) {
            BookL2* book = getBook(secid);
            if (book->updBBOPriceOnly(pxToInt(secid, price), is_bid, ts_micro)) {
                updateQ(secid, ts_micro);
                return;
            }
            //logDebug("BookQ updBBOPriceOnly security not updated %d", (int) secid);
        }

        void updBBOSizeOnly(utils::eSecurity secid, Quantity size, bool is_bid, uint64_t ts_micro) {
            BookL2* book = getBook(secid);
            if (book->updBBOSizeOnly(size, is_bid, ts_micro)) {
                updateQ(secid, ts_micro);
                return;
            }
            //logDebug("BookQ updBBOSizeOnly security not updated %d", (int) secid);
        }

        void resetALLBooks() {
            for (int i=0; i<utils::TotalSecurity; ++i) {
                resetBook((utils::eSecurity)i);
            }
        }

        void resetBook(utils::eSecurity secid) {
            if (_books[(int)secid]) {
                _books[(int)secid]->reset();
                updateQ(secid, utils::TimeUtil::cur_time_gmt_micro());
            }
        }

        BookL2* getBook(utils::eSecurity secid) const {
            return _books[(int)secid];
        }

        void addTrade(utils::eSecurity secid, double price, Quantity size, int attr){
            // TODO
            // implement this
        };

        const std::vector<BookConfig> & getBookConfig() const {
            return _bq._cfg;
        }

        ~Writer() {
            for (int i = 0; i< utils::TotalSecurity; ++i) {
                if (_books[i]) {
                    delete _books[i];
                    _books[i] = NULL;
                }
            }
        }
    private:
        BookQ& _bq;
        typename BookQ::QType::Writer& _wq;  // the writer's queue
        BookL2* _books[utils::TotalSecurity]; // all possible books

        friend class BookQ<BufferType>;
        Writer(BookQ& bq) : _bq(bq), _wq(_bq._q.theWriter()) {
            memset(_books, 0, sizeof(BookL2*) * utils::TotalSecurity);
            // create books according to the config
            for (size_t i = 0; i<_bq._cfg.size(); ++i) {
                const std::string& sym(_bq._cfg[i].symbol);
                utils::eSecurity sec = utils::SecMappings::instance().getSecId(sym.c_str());
                _books[(int)sec] = new BookL2(_bq._cfg[i]);
            }
        }

        void updateQ(utils::eSecurity secid, uint64_t ts_micro) {
            _books[(int)secid]->_book.update_ts_micro = ts_micro;
            _wq.put((char*)&(_books[(int)secid]->_book));
        }
    };

    // reader always assumes a normalized BookL2
    class Reader {
    public:
        bool getNextUpdate(BookDepot& book) {
            utils::QStatus stat = _rq->copyNextIn((char*)&book);
            switch (stat) {
            case utils::QStat_OK :
                _rq->advance();
                return true;
            case utils::QStat_EAGAIN :
                return false;
            case utils::QStat_OVERFLOW :
                int lost_updates = _rq->catchUp();
                logError("venue read queue %s overflow, lost %d updates. Trying to catch up."
                        ,_bq._q_name.c_str(), lost_updates);
                return getNextUpdate(book);
            }
            logError("getNextUpdate read queue %s unknown qstat %d, exiting..."
                    ,_bq._q_name.c_str(), (int) stat);
            throw std::runtime_error("BookQ Reader got unknown qstat.");
        }

        bool getLatestUpdate(BookDepot& book) {
            _rq->seekToTop();
            utils::QStatus stat = _rq->copyNextIn((char*)&book);
            switch (stat) {
            case utils::QStat_OK :
                return true;
            case utils::QStat_EAGAIN :
                return false;
            }
            logError("getLatestUpdate read queue %s unknown qstat %d, exiting..."
                    ,_bq._q_name.c_str(), (int) stat);
            throw std::runtime_error("BookQ Reader got unknown qstat.");
        }

        ~Reader() {
            delete _rq;
            _rq = NULL;
        }

    private:
        BookQ& _bq;
        typename BookQ::QType::Reader* _rq;  // the reader's queue
        friend class BookQ<BufferType>;
        Reader(BookQ& bq) : _bq(bq), _rq(_bq._q.newReader())
        {
        }

    };

};

struct BarLine {
    const int bar_sz;  // in seconds
    time_t next_update_ts;
    double open_px, max_px, min_px, close_px;

    explicit BarLine(int bar_size_seconds): bar_sz(bar_size_seconds), next_update_ts(0),
            open_px(-1), max_px(-10000000), min_px(10000000), close_px(-1) {
    }

    // new price update
    void update(const BookDepot& book) {
        close_px = book.getMidDouble();
        if (close_px > max_px) {
            max_px = close_px;
        }
        if (close_px < min_px) {
            min_px = close_px;
        }
        if (__builtin_expect((next_update_ts == 0), 0)) {
            next_update_ts = book.update_ts_micro/1000000;
            reset();
        }
    }

    // returns true if reset is due
    bool oneSecond(time_t ts, bool reset_if_due, char* buf = NULL, int* buf_len = NULL) {
        if (__builtin_expect((next_update_ts == 0), 0)) {
            return false;
        }

        if (ts >= next_update_ts) {
            if ((buf != NULL) && (buf_len != NULL)) {
                 *buf_len = toString(next_update_ts - bar_sz, buf, *buf_len);
            }
            if (reset_if_due)
                reset();
            return true;
        }
        return false;
    }

    int toString(time_t ts, char* buf, int buf_len) const {
        return snprintf(buf, buf_len, "%u %.7f %.7f %.7f %.7f\n", (unsigned int)ts,
                open_px, max_px, min_px, close_px);
    }

    void reset() {
        open_px = close_px;
        max_px = close_px;
        min_px = close_px;
        next_update_ts += bar_sz;
    }

};

}
