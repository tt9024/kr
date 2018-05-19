#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>

#include "plcc/PLCC.hpp"
#include "time_util.h"
#include "queue.h"  // needed for SwQueue for BookQ
//#define MaxPriceLevels 8
//there is really no need to
//optimize this for now, just leave it for later
//#define PriceMult 1000000

namespace tp {

typedef double Price;
typedef int32_t Quantity;
typedef uint64_t TSMicro;

struct PriceEntry {
#pragma pack(push,1)
    Price price;  // price is integer - pip adjusted and side signed (bid+ ask-)
    Quantity size;
    TSMicro ts_micro;
    Quantity count;
#pragma pack(pop)
    PriceEntry() : price(0), size(0), ts_micro(0), count(0) {};
    PriceEntry(Price p, Quantity s, Quantity cnt, TSMicro ts) : price(p), size(s), ts_micro(ts), count(cnt) {
        if (!ts) {
            ts_micro = utils::TimeUtil::cur_time_gmt_micro();
        }
    };
    void set(Price px, Quantity sz, TSMicro ts) {
    	price=px;
    	size=sz;
    	ts_micro=ts;
    }

    void reset() {
        price = 0; size = 0;ts_micro=0;count=0;
    }

    Price getPrice(bool is_bid) const {
        //return is_bid? price:-price;
    	return price;
    }

    std::string toString(bool is_bid = true) const {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld(%.7lf:%d)", (unsigned long long) ts_micro, getPrice(is_bid), size);
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

/*
enum BookType {
	L1,
	TbT,
	L2,
	Bar,
	Total_Types
};
*/

struct BookConfig {
    std::string venue;
    std::string symbol;
    std::string type; // "L1, L2, TbT"

    BookConfig(const std::string& v, const std::string& s, const std::string& bt) :
        venue(v), symbol(s), type(bt) {
    	logInfo("BookConfig %s", toString().c_str());
    };

    // construct by venue/symbol
    BookConfig(const std::string& venu_symbol, const std::string& bt) : type(bt) {
    	size_t n = strlen(venu_symbol.c_str());
    	auto pos = venu_symbol.find("/");
    	if (pos == std::string::npos) {
    		logError("venue/symbol cannot be parsed, slash not found: %s", venu_symbol.c_str());
    		throw std::runtime_error(std::string("venue/symbol cannot be parsed!") + venu_symbol);
    	}
    	venue=venu_symbol.substr(0,pos);
    	symbol=venu_symbol.substr(pos+1,n);
    	logInfo("BookConfig %s", toString().c_str());
    }

    std::string qname() const {
    	return venue+"_"+symbol+"_"+type;
    }
    std::string toString() const {
    	return qname();
    }

    std::string bfname(int barsec) const {
    	return plcc_getString("BarPath")+"/"+
    		   venue+"_"+symbol+"_B"+
			   std::to_string(barsec)+"S.csv";
    }
};

#define BookLevel 8
struct BookDepot {
#pragma pack(push,1)
    // this structure needs to be aligned
    uint64_t update_ts_micro;  // this can be obtained from pe's ts
    // bid first, ask second
    int update_level;
    int update_type;
    PriceEntry pe[2*BookLevel];
    int avail_level[2];
    Price trade_price;
    Quantity trade_size;
    int trade_attr;  // buy(0)/sell(1) possible implied
    Quantity bvol_cum; // the cumulative buy volume since tp up
    Quantity svol_cum; // the cummulative sell volume since tp up
#pragma pack(pop)

    BookDepot() {
    	reset();
    }
    BookDepot& operator = (const BookDepot& book) {
        if (&book == this) {
            return *this;
        }
        memcpy (this, &book, sizeof(BookDepot));
        return *this;
    }

    void reset() {
    	memset(this, 0, sizeof(BookDepot));
    }

    Price getBid(Quantity* size=NULL) const {
        if ((avail_level[0] < 1))
            return 0;

        for (int i = 0; i<avail_level[0]; ++i) {
            if (pe[i].size > 0) {
            	if (size) {
            		*size=pe[i].size;
            	}
                return pe[i].getPrice(true);
            }
        }
        return 0;
    }

    Price getAsk(Quantity* size=NULL) const {
        if ((avail_level[1] < 1))
            return 0;
        for (int i = BookLevel; i<BookLevel+avail_level[1]; ++i) {
			if (pe[i].size > 0) {
				if (size) {
					*size=pe[i].size;
				}
				return pe[i].getPrice(false);
			}
        }
        return 0;
    }

    Price getBestPrice(bool isBid) const {
    	int side=isBid?0:1;
        if ((avail_level[side] < 1))
            return 0;

        for (int i = BookLevel*side; i<avail_level[side]+BookLevel*side; ++i) {
            if (pe[i].size > 0)
                return pe[i].getPrice(isBid);
        }
        return 0;
    }

    Price getMid() const {
        if ((avail_level[0] < 1) || (avail_level[1] < 1))
            return 0;
        return (getBid() + getAsk())/2;
    }

    double getBidDouble() const {
    	return getBid();
    }

    double getAskDouble() const {
    	return getAsk();
    }

    double getMidDouble() const {
    	return getMid();
    }

    bool isValidQuote() const {
    	const Price bp=getBid(), ap=getAsk();
    	return (bp*ap!= 0) && (ap>bp);
    }

    bool isValidTrade() const {
    	return trade_price*trade_size != 0;
    }

    bool isValid() const {
    	if (update_type == 2) {
    		// trade
    		return isValidTrade();
    	}
    	return isValidQuote();
    }

    const char* getUpdateType() const {
    	switch (update_type) {
    	case 0 : return "Quote(Bid)";
    	case 1 : return "Quote(Ask)";
    	case 2 : return "Trade";
    	}
    	logError("unknown update type %d", update_type);
    	throw std::runtime_error("unknown update type!");
    }

    void setUpdateType(bool isTrade, bool isBid) {
    	if (isTrade) {
    		update_type=2;
        } else {
        	if (isBid) {
        		update_type=0;
        	} else {
    		    update_type=1;
    	    }
        }
    }

    bool addTrade(Price px, Quantity sz) {
    	trade_price=px;
    	trade_size=sz;
    	return addTrade();
    }

    std::string toString() const {
        char buf[1024];
        int n = 0;
        const char* update_type = getUpdateType();
        n += snprintf(buf+n, sizeof(buf)-n, "%llu (%s-%d)",
        		(unsigned long long)update_ts_micro,update_type,update_level);
		for (int s = 0; s < 2; ++s)
		{
			int levels = avail_level[s];
			n += snprintf(buf+n, sizeof(buf)-n, " %s(%d) [ ", s==0?"Bid":"Ask", levels);
			const PriceEntry* pe_ = &(pe[s*BookLevel]);
			for (int i = 0; i<levels; ++i) {
				if (pe_->size) {
					n += snprintf(buf+n, sizeof(buf)-n, " (%d)%s ", i, pe_->toString().c_str());
				}
				++pe_;
			}
			n += snprintf(buf+n, sizeof(buf)-n, " ] ");
		}
		const char* bs = trade_attr==0?"Buy":"Sell";
		n += snprintf(buf+n, sizeof(buf)-n, "%s %d@%f %d-%d",
				bs, trade_size, trade_price, bvol_cum, svol_cum);
        return std::string(buf);
    }

    std::string prettyPrint() const {
    	char buf[1024];
    	size_t n = snprintf(buf,sizeof(buf), "%lld:%s,upd_lvl(%d-%d:%d)\n",
    			(long long)update_ts_micro,
				getUpdateType(),
				update_level,
				avail_level[0],
				avail_level[1]);
    	if (update_type==2) {
    		// trade
    		n+=snprintf(buf+n,sizeof(buf)-n,"   %s %d@%.7lf %d-%d\n",
    				trade_attr==0?"B":"S",
    			    trade_size,
					trade_price,
					bvol_cum,svol_cum);
    	} else {
    		// quote
    		int lvl=avail_level[0];
    		if (lvl > avail_level[1]) lvl=avail_level[1];
    		for (int i=0;i<lvl;++i) {
    			n+=snprintf(buf+n,sizeof(buf)-n,"\t%d\t%.7lf:%.7lf\t%d\n",
    					pe[i].size,
						pe[i].price,
						pe[i+BookLevel].price,
						pe[i+BookLevel].size);
    		}
    	}
    	return std::string(buf);
    }

private:
    bool addTrade() {
    	// this assumes that the price and size
    	// has been set before this call
    	if (__builtin_expect( (!isValidQuote()) ||
    			              (!isValidTrade()) ,0)) {
    		return false;
    	}
    	const Price bd=std::abs(getBid()-trade_price);
    	const Price ad=std::abs(getAsk()-trade_price);
    	if (ad>bd) {
    		trade_attr=1;  // sell close to best bid
    		svol_cum+=trade_size;
    	} else {
    		trade_attr=0;
    		bvol_cum+=trade_size;
    	}
    	setUpdateType(true, false);
    	return true;
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
    BookDepot _book;
    int* const _avail_level;
    // filters for duplicate trade size updates
    static const unsigned long long MaxMicroSizeFilter=40ULL;
    uint64_t _last_size_micro;
    Quantity _last_size;

    explicit BookL2(const BookConfig& cfg):
            _cfg(cfg), _book(), _avail_level(_book.avail_level),
			_last_size_micro(0), _last_size(0) {
        reset();
    }

    std::string toString() const {
        char buf[1024];
        int n = 0;
        n += snprintf(buf+n, sizeof(buf)-n, "Book %s { %s }", _cfg.toString().c_str(), _book.toString().c_str());
        return std::string(buf);
    }

    bool isValid() const {
    	return _book.isValid();
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
        _book.setUpdateType(false, side==0);
        // move subsequent levels down
        PriceEntry* pe = getEntry(level, side);
        if (levels > level) {
            memmove(pe+1, pe, (levels - level)*sizeof(PriceEntry));
        };

        pe->set(price, size, ts_micro);
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
        _book.setUpdateType(false, side==0);
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

        PriceEntry* pe = getEntry(level, side);
        if (__builtin_expect(((pe->price == price) && (pe->size == size)), 0)) {
            return false;
        }
        _book.update_level = level;
        _book.setUpdateType(false, side==0);
        pe->set(price, size, ts_micro);
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
        Price price = getEntry(0, side)->getPrice(is_bid);
        return updPrice(price, size, 0, is_bid, ts_micro);
    }

    void updTrdPrice(Price px) {
    	_last_size=0;
    	_book.trade_price=px;
    }

    bool updTrdSize(Quantity size) {
    	// THIS CODE IS COMPLETELY BROKEN!!!
    	// duplication, misses, delays all happens without any
    	// certainty. No way to fix it, tried, many times!
    	// So don't use the ticktype of 5/6 (TickPrice or TickSize)
    	// use the RT_VOLUME and get from tickstring()

    	updTrdSizeNoCheck(size);
    	return true;

    	// this is just to filter out duplicate trade size
    	// update
		const uint64_t cur_micro=utils::TimeUtil::cur_time_gmt_micro();
    	if(_last_size==size) {
    		if (cur_micro-_last_size_micro<MaxMicroSizeFilter) {
    			logInfo("duplicate trade size update detected: "
    					"size(%d), lat_micro(%lld)",
    					size, (long long) cur_micro-_last_size_micro);
    			return false;
    		}
    	} else {
    		_last_size=size;
        	_book.trade_size=size;
    	}
    	_last_size_micro=cur_micro;
    	return true;
    }

    void updTrdSizeNoCheck(Quantity size) {
    	_book.trade_size=size;
    }

    bool addTrade(Price px, Quantity sz) {
    	return _book.addTrade(px, sz);
     }

    void reset() {
        _book.reset();
        _last_size_micro = 0;
        _last_size = 0;
    }

    void plccLogError(const char* msg, int number) {
        logError("book (%s) update error: "
                "%s, %d, book dump: %s",
                _cfg.toString().c_str(), msg, number, toString().c_str());
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
    // This is to enforce that for SwQueue, at most one writer should
    // be created for each BookQ
    typedef utils::SwQueue<QLen, BookLen, BufferType> QType;
    const BookConfig _cfg;
    const std::string _q_name;
    class Writer;
    class Reader;

    BookQ(const BookConfig config, bool readonly) :
        _cfg(config), _q_name(_cfg.qname()), _q(_q_name.c_str(), readonly, false),
        _writer(readonly? NULL:new Writer(*this))
    {
        logInfo("BookQ %s started %s configs (%s).",
        		_q_name.c_str(),
				readonly?"ReadOnly":"ReadWrite",
				_cfg.toString().c_str());
    };

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
        void newPrice(double price, Quantity size, int level, bool is_bid, uint64_t ts_micro) {
            if (__builtin_expect(_bookL2.newPrice(price, size, level, is_bid, ts_micro), 1)) {
                updateQ(ts_micro);
                return;
            }
            //logError("BookQ newPrice() security not updated %d", (int)secid);
        }

        void delPrice(int level, bool is_bid, uint64_t ts_micro) {
            if (__builtin_expect((_bookL2.delPrice(level, is_bid)),1)) {
                updateQ(ts_micro);
                return;
            }
            //logError("BookQ delPrice() security not updated %d", (int)secid);
        }

        void updPrice(double price, Quantity size, int level, bool is_bid, uint64_t ts_micro) {
            if (__builtin_expect(_bookL2.updPrice(price, size, level, is_bid, ts_micro),1)) {
                updateQ(ts_micro);
                return;
            }
            //logError("BookQ updPrice() security not updated %d", (int)secid);
        }

        void updBBO(double price, Quantity size, bool is_bid, uint64_t ts_micro) {
            if (__builtin_expect(_bookL2.updBBO(price, size, is_bid, ts_micro), 1)) {
                updateQ(ts_micro);
                return;
            }
            //logError("BookQ updBBO security not updated %d", (int) secid);
        }

        void updBBOPriceOnly(double price, bool is_bid, uint64_t ts_micro) {
            if (_bookL2.updBBOPriceOnly(price, is_bid, ts_micro)) {
            	// wait for the size?
                // updateQ(ts_micro);
                return;
            }
            //logDebug("BookQ updBBOPriceOnly security not updated %d", (int) secid);
        }

        void updBBOSizeOnly(Quantity size, bool is_bid, uint64_t ts_micro) {
            if (_bookL2.updBBOSizeOnly(size, is_bid, ts_micro)) {
                updateQ(ts_micro);
                return;
            }
            //logDebug("BookQ updBBOSizeOnly security not updated %d", (int) secid);
        }
        /*

        void updTrdPrice(double price) {
            _bookL2.updTrdPrice(price);
        }

        bool updTrdSize(Quantity size) {
        	// make it stageful, filter same
        	// updates (same size within 50 micro
        	// updTrdPrice or other size will disable it
            if (_bookL2.updTrdSize(size)) {
            	return addTrade();
            }
            logInfo("last trade update filtered.");
            return true;
        }
        */

        bool updTrade(double price, Quantity size) {
        	if(__builtin_expect(!_bookL2.addTrade(price,size),0)) {
        		return false;
        	}
        	updateQ(utils::TimeUtil::cur_time_gmt_micro());
        	return true;
        };

        void resetBook() {
            _bookL2.reset();
        }

        const BookL2* getBook() const {
            return &_bookL2;
        }

        const std::vector<BookConfig> & getBookConfig() const {
            return _bq._cfg;
        }

        ~Writer() {};
    private:
        BookQ& _bq;
        typename BookQ::QType::Writer& _wq;  // the writer's queue
        BookL2 _bookL2; // the L2 books, each book per queue

        friend class BookQ<BufferType>;
        Writer(BookQ& bq) : _bq(bq), _wq(_bq._q.theWriter()), _bookL2(_bq._cfg) {
        	resetBook();
        }

        void updateQ(uint64_t ts_micro) {
        	if (__builtin_expect(_bookL2.isValid(), 1)) {
				_bookL2._book.update_ts_micro = ts_micro;
				_wq.put((char*)&(_bookL2._book));
        	}
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

class BarLineWriter {
public:
    const std::string bfname;

    explicit BarLineWriter(const char* barfile_name):
    		bfname(barfile_name), bfp(0), bvol(0),svol(0) {
    	bfp=fopen(bfname.c_str(), "at+");
    	if (!bfp) {
    		throw std::runtime_error(std::string("fopen error")+bfname);
    	}
    }

    // new price update
    void update(const BookDepot& book,time_t this_sec) {
    	Quantity bs=0, as=0;
    	Price bp, ap;
    	bp=book.getBid(&bs);
    	ap=book.getAsk(&as);
    	Quantity bv=0,sv=0;
    	if (bvol*svol != 0) {
    		bv=book.bvol_cum-bvol;
    		sv=book.svol_cum-svol;
    	}
    	bvol=book.bvol_cum;
    	svol=book.svol_cum;
    	fprintf(bfp, "%d, %d, %.7lf, %.7lf, %d, %d, %d, %lld\n",
    			(int) this_sec, bs,bp,ap,as,bv,sv,
				(long long) utils::TimeUtil::cur_time_micro());
    }

    void reset() {
    	bvol=0;
    	svol=0;
    }

    void flush() const {
    	fflush(bfp);
    }

    ~BarLineWriter() {
    	if (bfp) {
    		fclose(bfp);
    		bfp=NULL;
    	}
    }

private:
    FILE* bfp;
    Quantity bvol, svol;

};


template <template<int, int> class BufferType >
class BarLine {
public:
	BarLine(const BookConfig& cfg, int bar_sec) :
		bcfg(cfg), barsec(bar_sec),
		bq(cfg,true), br(bq.newReader()),
		bw(cfg.bfname(barsec).c_str()) {
	}
	void update(time_t this_sec) {
		BookDepot book;
		if (br->getLatestUpdate(book)) {
			bw.update(book,this_sec);
		}
	}
	void flush() const {
		bw.flush();
	}
	~BarLine() { delete br ; br=NULL;};
private:
	const BookConfig bcfg;
	const int barsec;
	BookQ<BufferType> bq;
	typename BookQ<BufferType>::Reader* br;
	BarLineWriter bw;
};

}
