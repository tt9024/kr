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
#include <set>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

//#define MaxPriceLevels 8
//there is really no need to
//optimize this for now, just leave it for later
//#define PriceMult 1000000

#define getMax(x, y) ((x)>(y)?(x):(y))
#define getMin(x, y) ((x)<(y)?(x):(y))

namespace tp {

typedef double Price;
typedef int32_t Quantity;
typedef uint64_t TSMicro;

static inline
bool  px_equal(Price x, Price y) {
	const Price minpx(1e-10);
	return ((x-y<minpx) && (x-y>-minpx));
}

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

    Price getPrice() const {
        //return is_bid? price:-price;
    	return price;
    }

    std::string toString() const {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld(%.7lf:%d)", (unsigned long long) ts_micro, getPrice(), size);
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
    		   venue+"_"+
			   (isFuture(symbol)?
					   symbol.substr(0,symbol.size()-2):
					   symbol)+
			   "_B"+
			   std::to_string(barsec)+"S.csv";
    }

    std::string L2fname() const {
    	return plcc_getString("BarPath")+"/"+
    		   venue+"_"+
			   (isFuture(symbol)?
					   symbol.substr(0,symbol.size()-2):
					   symbol)+
			   "_L2.bin";
    }

    static inline
	bool isFuture(const std::string& symbol) {
		const size_t n = symbol.size();
		const char m = symbol[n-2];
		const char y = symbol[n-1];
		const char ms[12]={'F','G','H','J','K','M','N','Q','U','V','X','Z'};
		for (char c : ms ) {
			if (c==m) {
				if (std::isdigit(y)) {
					return true;
				}
				break;
			}
		}
		return false;
	}
};

struct L2Delta {
#pragma pack(push, 1)
	char type; // 0: snapshot, 1: new_level, 2: del_level, 3: upd_level, 4: trade
	char side; // 0: bid 1: ask
	short level;
	Quantity qty;
	Price px;
#pragma pack(pop)
	L2Delta() {
		reset();
	}
	void reset() {
		memset(this, 0, sizeof(L2Delta));
	}

    void newPrice(Price price, Quantity size, unsigned int lvl, bool is_bid) {
    	reset();
    	type=1; // new_level
    	side=is_bid?0:1;
    	level=lvl;
    	qty=size;
    	px=price;
    }

    void delPrice(unsigned int lvl, bool is_bid) {
    	reset();
    	type=2; // del_level
    	side=is_bid?0:1;
    	level=lvl;
    }

    void updPrice(Price price, Quantity size, unsigned int lvl, bool is_bid) {
    	reset();
    	type=3; // upd_level
    	side=is_bid?0:1;
    	level=lvl;
    	qty=size;
    	px=price;
    }

    void snapshot() {
    	reset();
    	type=0;  // snapshot, get from the bookdepot
    }

    void addTrade(Price price, Quantity size) {
    	reset();
    	type=4;  // trade
    	px=price;
    	qty=size;
    }

    std::string toString() const {
    	char buf[256];
    	snprintf(buf, sizeof(buf), "%d %d %d %f %d", (int) type, (int) side, (int) level, px, qty);
    	return std::string(buf);
    }

};

#define BookLevel 10
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
    //Price close_px;    // the close price of previous session.
    L2Delta l2_delta;
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

    Price getVWAP(int level, bool isBid, Quantity* q=NULL) const {
    	const int side=isBid?0:1;
    	int lvl = getMin(level, avail_level[side]);
    	Quantity qty = 0;
    	Price px = 0;
    	const PriceEntry* p = pe + side*BookLevel;
    	const PriceEntry* p0 = p + lvl;
    	while (p < p0) {
    		px += p->price * p->size;
    		qty += p->size;
    		++p;
    	}
    	if (q) *q = qty;
    	return px/qty;
    }

    Price getISM(int level) const {
    	Quantity bq, aq;
    	Price bp, ap;
    	bp=getVWAP(level, true, &bq);
    	ap=getVWAP(level, false, &aq);
    	return (bp*aq + ap*bq)/(aq+bq);
    }

    Price getBid(Quantity* size) const {
        if ((avail_level[0] < 1))
            return 0;

        for (int i = 0; i<avail_level[0]; ++i) {
            if (pe[i].size > 0) {
            	*size=pe[i].size;
                return pe[i].getPrice();
            }
        }
        return 0;
    }

    Price getAsk(Quantity* size) const {
        if ((avail_level[1] < 1))
            return 0;
        for (int i = BookLevel; i<BookLevel+avail_level[1]; ++i) {
			if (pe[i].size > 0) {
				*size=pe[i].size;
				return pe[i].getPrice();
			}
        }
        return 0;
    }

    Price getBid() const {
        if ((avail_level[0] < 1))
            return 0;
        for (int i = 0; i<avail_level[0]; ++i) {
            if (pe[i].size > 0) {
                return pe[i].getPrice();
            }
        }
        return 0;
    }

    Price getAsk() const {
        if ((avail_level[1] < 1))
            return 0;
        for (int i = BookLevel; i<BookLevel+avail_level[1]; ++i) {
			if (pe[i].size > 0) {
				return pe[i].getPrice();
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
                return pe[i].getPrice();
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

    // Use it to preserve the quote accounting
    // for self trades (taking liquidity) before next update
    // It's slower than a simple assignment
    // use assignment if not trading too frequently and trading
    // size is small compared with BBO size
    void updateFrom(const BookDepot& book_) {
    	PriceEntry pe[2*BookLevel];
    	updateFromEntry(book_.pe, pe, book_.avail_level[0]);
    	updateFromEntry(book_.pe+BookLevel, pe+BookLevel, book_.avail_level[1]);
    	memcpy(this, &book_, sizeof(BookDepot));
    	memcpy(this->pe, pe, sizeof(pe));
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

     void updateFromEntry( const PriceEntry* from_pe,
    		 PriceEntry* to_pe,
			 int levels) const {
    	 PriceEntry pe_store[BookLevel];
    	 PriceEntry* pe_ = &(pe_store[0]);

    	 const PriceEntry* const to_pe_end = to_pe + BookLevel;
    	 const PriceEntry* const from_pe_end = from_pe + levels;
    	 PriceEntry* to_pe_start = to_pe;
    	 //const PriceEntry* const from_pe_start = from_pe;
    	 while(from_pe != from_pe_end)
    	 {
    		 // search from_pe's price in the remainder of to_pe up to levels
    		 bool found = false;
    		 PriceEntry* pe_ptr = to_pe_start;
    		 while(pe_ptr != to_pe_end)
    		 {
    			 if (__builtin_expect(px_equal(from_pe->price, pe_ptr->price),1))
    			 {
    				 found = true;
    				 to_pe_start = pe_ptr + 1;
    				 break;
    			 }
    			 ++pe_ptr;
    		 }
    		 if (__builtin_expect(found && (from_pe->ts_micro <= pe_ptr->ts_micro),0)) {
    			 logInfo("pe entry not updated from %s to %s",
    					 from_pe->toString().c_str(),
						 pe_ptr->toString().c_str());
    			 memcpy(pe_,pe_ptr, sizeof(PriceEntry));
    		 } else {
    			 memcpy(pe_,from_pe, sizeof(PriceEntry));
    		 }
        	 ++pe_;
        	 ++from_pe;
    	 }
    	 memcpy(to_pe, pe_store, sizeof(PriceEntry)*levels);
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
    //static const unsigned long long MaxMicroSizeFilter=40ULL;
    //uint64_t _last_size_micro;
    //Quantity _last_size;

    explicit BookL2(const BookConfig& cfg):
            _cfg(cfg), _book(), _avail_level(_book.avail_level)
			//_last_size_micro(0), _last_size(0)
    {
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
        _book.l2_delta.newPrice(price, size, level, is_bid);
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
        _book.l2_delta.delPrice(level, is_bid);
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
        _book.l2_delta.updPrice(price, size, level, is_bid);
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
    	_book.l2_delta.type=0;
        int side = is_bid?0:1;
        if (__builtin_expect((0 == _avail_level[side]), 0)) {
            return newPrice(price, size, 0, is_bid, ts_micro);
        }
        return updPrice(price, size, 0, is_bid, ts_micro);
    }

    // price only
    bool updBBOPriceOnly(Price price, bool is_bid, uint64_t ts_micro) {
    	_book.l2_delta.type=0;
        int side = is_bid?0:1;
        if (__builtin_expect((0==_avail_level[side]), 0)) {
            return newPrice(price, 0, 0, is_bid, ts_micro);
        }
        Quantity size = getEntry(0, side)->size;
        return updPrice(price, size, 0, is_bid, ts_micro);
    }

    // size only
    bool updBBOSizeOnly(Quantity size, bool is_bid, uint64_t ts_micro) {
    	_book.l2_delta.type=0;
        int side = is_bid?0:1;
        if (__builtin_expect((0 == _avail_level[side]), 0)) {
            return newPrice(0, size, 0, is_bid, ts_micro);
        }
        Price price = getEntry(0, side)->getPrice();
        return updPrice(price, size, 0, is_bid, ts_micro);
    }

    /*
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

    */


    bool addTrade(Price px, Quantity sz) {
    	_book.l2_delta.addTrade(px,sz);
    	return _book.addTrade(px, sz);
     }

    void reset() {
        _book.reset();
        //_last_size_micro = 0;
        //_last_size = 0;
    }

    void plccLogError(const char* msg, int number) {
        logError("book (%s) update error: "
                "%s, %d",
                _cfg.toString().c_str(), msg, number);
    }
    PriceEntry* getEntry(unsigned int level, int side) const {
        return (PriceEntry*) &(_book.pe[side*BookLevel+level]);
    }

    // this is used by reading from L2 delta file
    bool updFromDelta(const L2Delta* delta, uint64_t ts_micro) {
    	_book.update_ts_micro = ts_micro;
    	switch(delta->type) {
    	case 1: // new_level
    		logDebug("new %s %d %f\n", delta->side==0?"Bid":"Offer",(int)delta->level, delta->px);
    		return this->newPrice(delta->px, delta->qty, delta->level, delta->side==0, ts_micro);
    	case 2: // del_level
    		logDebug("del %s %d\n", delta->side==0?"Bid":"Offer",(int)delta->level);
    		return this->delPrice(delta->level, delta->side==0);
    	case 3: // upd_level
    		logDebug("upd %s %d %f %d\n", delta->side==0?"Bid":"Offer",(int)delta->level, delta->px, delta->qty);
    		return this->updPrice(delta->px, delta->qty, delta->level, delta->side==0, ts_micro);
    	case 4: // trade
    		logDebug("add trade\n");
    		return this->addTrade(delta->px, delta->qty);
    	}
    	return false;
    }
};

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

    BookQ(const BookConfig config, bool readonly, bool init_to_zero=false) :
        _cfg(config), _q_name(_cfg.qname()), _q(_q_name.c_str(), readonly, init_to_zero),
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
        bool _l2_snap;  // only used in updateQ(), true if current
                        // book is not written due to valid check
                        // and so next write should be a snapshot
                        // since the reader may lose delta updates
                        // This is because there could be trainsient
                        // state durint updates (i.e. update bid and then
                        // ask, etc) that is invalid

        friend class BookQ<BufferType>;
        Writer(BookQ& bq) : _bq(bq), _wq(_bq._q.theWriter()),
        		_bookL2(_bq._cfg), _l2_snap(false) {
        	resetBook();
        }

        void updateQ(uint64_t ts_micro) {
        	if (__builtin_expect(_bookL2.isValid(), 1)) {
				if (__builtin_expect(_l2_snap, 0)) {
					// force a snapshot at L2DeltaWriter
					_bookL2._book.l2_delta.type = 0;
					_l2_snap = false;
				}
				_bookL2._book.update_ts_micro = ts_micro;
				_wq.put((char*)&(_bookL2._book));
        	} else {
        		// make sure the next L2 write is a snap
        		// since we may be missing updates
        		_l2_snap = true;
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

        bool getLatestUpdateAndAdvance(BookDepot& book) {
        	if (__builtin_expect(!_rq->advanceToTop(),0)) {
        		return false;
        	}
        	utils::QStatus stat = _rq->copyNextIn((char*)&book);
        	if (__builtin_expect(stat == utils::QStat_OK, 1)) {
        		_rq->advance();
        		return true;
        	}
        	switch (stat) {
        	case utils::QStat_EAGAIN :
        		return false;
        	case utils::QStat_OVERFLOW :
        	{
        		// try again
        		_rq->advanceToTop();
        		stat = _rq->copyNextIn((char*)&book);
        		if (stat != utils::QStat_OK) {
        			return false;
        		}
        		return true;
        	}
        	default :
        		logError("read queue %s unknown qstat %d, existing...", _bq._q_name.c_str(), (int) stat);
        		throw std::runtime_error("BookQ Reader got unknown qstat!");
        	}
        	return false;
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
    		// guard against restart
    		if (bv<0) bv=0;
    		if (sv<0) sv=0;
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

/*
 * Format of L2 Delta writer:
 * PREAMBLE bookdepot [TS_MICRO L2DELTA]
 *
 * Where PREAMBLE is a unique 8-bytes sequence to detect start of a snapshot
 * [TS_MICRO L2DELTA] is repeated and is supposed to be applied to the
 * snapshot until the next snapshot
 */

static const uint64_t SnapshotPreamble = 0xf0f0f0f0f0f0f0f0ULL;
static const int SnapCount  = 1024;

template <template<int, int> class BufferType >
class L2DeltaWriter {
public:
	static const int FlushCount = 1;
	static const uint64_t MaxSnapMicro = 300ULL * 1000000ULL;

	L2DeltaWriter(const BookConfig& bcfg) :
		_bcfg(bcfg),
		_fp(fopen(bcfg.L2fname().c_str(), "ab+")), // barsec=0 -> L2Delta
		_flushCount(0),
		_snapCount(0),
		_nextSnapSec(0),
		_bq(_bcfg,true), _br(_bq.newReader())
	{
		if (!_fp) {
			throw std::runtime_error(
					std::string("cannot open file for L2 delta writer")
			        + bcfg.toString());
		}
		if (!_br) {
			throw std::runtime_error(
					std::string("cannot open shm queue for L2 delta writer ")
			        + bcfg.toString());
		}
	};
	~L2DeltaWriter() {
		if (_fp)
			fclose(_fp);
		_fp=NULL;
		delete _br;
		_br = NULL;
	}

	bool update() {
		BookDepot book;
		if (_br->getNextUpdate(book)) {
			write(book);
			return true;
		}
		return false;
	}
private:
	const BookConfig& _bcfg;
	FILE* _fp;
	int _flushCount;
	int _snapCount;
	uint64_t _nextSnapSec;
	BookQ<BufferType> _bq;
	typename BookQ<BufferType>::Reader* _br;

	void writeSnap(const BookDepot& book) {
		logDebug("write snap\n");
		fwrite(&SnapshotPreamble, sizeof(uint64_t), 1, _fp);
		fwrite(&book, sizeof(BookDepot), 1, _fp);
	}
	void writeDelta(const BookDepot& book) {
		logDebug("write delta: %s\n", book.l2_delta.toString().c_str());
		fwrite(&book.update_ts_micro, sizeof(uint64_t), 1, _fp);
		fwrite(&book.l2_delta, sizeof(book.l2_delta), 1, _fp);
	}

	void write(const BookDepot& book) {
		// just write a timestamp and book.l2detal
		// if a snapshot or _nextSnapSec, write a book
		// with a 8 byte preamble
		if (book.l2_delta.type == 0 || _snapCount == 0 || book.update_ts_micro > _nextSnapSec) {
			// write a snap, reset count
			writeSnap(book);
			_snapCount = SnapCount;
			_nextSnapSec = book.update_ts_micro + MaxSnapMicro;
		} else {
			--_snapCount;
			writeDelta(book);
		}
		if (_flushCount == 0) {
			fflush(_fp);
			_flushCount = FlushCount;
		} else {
			--_flushCount;
		}
	}
};

class L2DeltaReader {
public:
	explicit L2DeltaReader(const BookConfig& bcfg, bool tail = true) :
		_bcfg(bcfg),
		_fname(bcfg.L2fname()),
		_book(bcfg),
		_fp(fopen(_fname.c_str(), "rb")),
		_latest_micro(0),
		_last_pos(0),
		_file_size(0),
		_has_header(false),
		_header(0)
	{
		if (!_fp) {
			throw std::runtime_error(
					std::string("cannot open file for L2 delta reader: ")
			        + _bcfg.toString());
		}
		if (tail) {
			sync();
		}

	}
	~L2DeltaReader() {
		if(_fp)
			fclose(_fp);
		_fp = NULL;
	}
	const BookDepot* readNext() {
		if (!readHeader()) {
			return NULL;
		}
		if (_header == SnapshotPreamble) {
			logDebug("snapshot!\n");
			fread(&_book._book, sizeof(BookDepot), 1, _fp);
			_last_pos += sizeof(BookDepot);
		} else {
			L2Delta* delta = &(_book._book.l2_delta);
			fread(delta, sizeof(L2Delta), 1, _fp);
			_book.updFromDelta(delta, _header);
			_last_pos += sizeof(L2Delta);
		}
		// ready for the next
		_has_header = false;
		return &(_book._book);
	}

private:
	const BookConfig& _bcfg;
	const std::string _fname;
	BookL2 _book;
	FILE* _fp;
	uint64_t _latest_micro;
	uint64_t _last_pos;
	uint64_t _file_size;

	bool _has_header;
	uint64_t _header;

	void sync() {
		_file_size = updFileSize();
		uint64_t seek_point = SnapCount*(sizeof(L2Delta)+sizeof(uint64_t)) + sizeof(uint64_t);
		if (seek_point < _file_size) {
			_last_pos = _file_size - seek_point;
			fseek(_fp, _last_pos, SEEK_SET);
		}
		while(true) {
			_has_header = false;
			if (readHeader()) {
				if (_header == SnapshotPreamble)
					break;
			}
		}
	}

	bool readHeader() {
		if (!_has_header) {
			if (_file_size-_last_pos < sizeof(uint64_t)) {
				_file_size = updFileSize();
				if (_file_size-_last_pos < sizeof(uint64_t)) {
					return false;
				}
			}
			fread(&_header, sizeof(uint64_t), 1, _fp);
			_has_header = true;
			_last_pos += sizeof(uint64_t);
		}
		// make sure we have the content (delta or snapshot)
		if (_header == SnapshotPreamble) {
			if (_file_size-_last_pos >= sizeof(BookDepot)) {
				return true;
			}
		}
		if (_file_size-_last_pos >= sizeof(L2Delta)) {
			return true;
		}
		_file_size = updFileSize();
		return false;
	}

	uint64_t updFileSize() const {
		struct stat fs;
		if (stat(_fname.c_str(), &fs) != 0) {
			logError("error getting file size");
			return 0;
		}
		return fs.st_size;
	}


};

static inline
bool LatestBook(const std::string& symbol, const std::string& levelStr, BookDepot& myBook) {
    BookConfig bcfg(symbol, levelStr);
    BookQ<utils::ShmCircularBuffer> bq(bcfg, true);
    BookQ<utils::ShmCircularBuffer>::Reader* book_reader = bq.newReader();
    if (!book_reader) {
    	logError("Couldn't get book reader!");
    	return false;
    }
    bool ret = book_reader->getLatestUpdate(myBook);
    delete book_reader;
    return ret;
}

}  // namespace tp
