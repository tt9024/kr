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

#define getMax(x, y) ((x)>(y)?(x):(y))
#define getMin(x, y) ((x)<(y)?(x):(y))

namespace md {

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
    Price price;  
    Quantity size;
    TSMicro ts_micro;
    Quantity count;
#pragma pack(pop)
    PriceEntry() : price(0), size(0), ts_micro(0), count(0) {};
    PriceEntry(Price p, Quantity s, Quantity cnt, TSMicro ts) : price(p), size(s), ts_micro(ts), count(cnt) {
        if (!ts) {
            ts_micro = utils::TimeUtil::cur_micro();
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
        return price;
    }

    std::string toString() const {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld(%.7lf:%d)", (unsigned long long) ts_micro, getPrice(), size);
        return std::string(buf);
    }
};

class VenueConfig {
public:
    static const VenueConfig& get() {
        static VenueConfig vc;
        return vc;
    }

    int start_hour(const std::string& venue) const { 
        return findValue(venue, 0) % 24;
    }

    int start_min (const std::string& venue) const {
        return findValue(venue, 1);
    }

    int end_hour(const std::string& venue) const {
        return findValue(venue, 2);
    }

    int end_min(const std::string& venue) const {
        return findValue(venue, 3);
    }

    bool isTradingTime(const std::string& venue, const time_t cur_utc) const {
        const auto iter = venue_map.find(venue);
        if (iter == venue_map.end()) {
            logError("Venue not found: %s", venue.c_str());
            throw std::runtime_error("Venue not found!");
        }
        const auto& hm = iter->second;
        return utils::TimeUtil::isTradingTime(cur_utc, hm[0], hm[1], hm[2], hm[3]);
    }

    std::pair<time_t, time_t> startEndUTC(const std::string& venue, const time_t cur_utc, int snap) const {
        // getting the venue's starting and ending time given the current time.  
        // If the current time is a trading time, getting the current trading day's starting time
        // If the current time is not a trading time, 
        //    snap = 0: return 0
        //    snap = 1: return previous trading day's starting time
        //    snap = 2: return next trading day's starting time
        // Note for over-night venues, the trading day of over-night session is the next day.
        // For example, CME starting at 18:00, ends at 17:00.  
        // If current time is 20:00, then trading day is next day, startUTC is today's 18:00.
        // If current time is 17:55pm on Sunday, snap = 2, then trading day is Monday,
        // and startUTC is today's 18:00.
        //
        // Return: a pair of utc time stamp in seconds for starting and ending time of the
        //         trading day.

        const auto iter = venue_map.find(venue);
        if (iter == venue_map.end()) {
            logError("Venue not found: %s", venue.c_str());
            throw std::runtime_error("Venue not found!");
        }
        const auto& hm = iter->second;
        const auto curDay = utils::TimeUtil::tradingDay(cur_utc, hm[0], hm[1], hm[2], hm[3]);
        time_t sutc = utils::TimeUtil::string_to_frac_UTC(curDay.c_str(), 0, "%Y%m%d");
        return std::pair<time_t, time_t>( 
                sutc + hm[0]*3600 + hm[1]*60, 
                sutc + hm[2]*3600 + hm[3]*60
               ) ;
    }

    time_t sessionLength(const std::string& venue) const {
        // return number of seconds in a trading day
        const auto iter = venue_map.find(venue);
        if (iter == venue_map.end()) {
            logError("Venue not found: %s", venue.c_str());
            throw std::runtime_error("Venue not found!");
        }
        const auto& hm = iter->second;
        return hm[2]*3600 + hm[3]*60 - hm[0]*3600 - hm[1]*60;
    }

private:
    std::map<std::string, std::vector<int> > venue_map;
    VenueConfig() {
        const auto cfg = plcc_getString("Venue");
        auto vc = utils::ConfigureReader(cfg.c_str());
        auto vl = vc.getStringArr("VenueList");
        for (const auto& v : vl) {
            auto hm = vc.getStringArr(v.c_str());
            // hm in [ start_hour, start_min, end_hour, end_min ]
            if (hm.size() != 4) {
                logError("Venue Reading Error for %s: wrong size.", v.c_str());
                throw std::runtime_error("Venue Reading Error!");
            }
            int sh = (std::stoi(hm[0]) % 24);
            int sm = std::stoi(hm[1]);
            int eh = (std::stoi(hm[2]) % 24);
            int em = std::stoi(hm[3]);
            if (em < 0 || sm < 0) {
                logError("Venue %s minutes negative!", v.c_str());
                throw std::runtime_error("Venue minutes negative!");
            }

            if (sh > eh) { sh = 24 - sh ; }
            std::vector<int> hv {sh, sm, eh, em};
            venue_map.emplace(v, hv);
        }
    };

    int findValue(const std::string& venue, int offset) const {
        const auto iter = venue_map.find(venue);
        if (iter == venue_map.end()) {
            logError("Venue not found: %s", venue.c_str());
            throw std::runtime_error("Venue not found: " + venue);
        }
        return iter->second[offset];
    }
};


struct BookConfig {
    std::string venue;
    std::string symbol;
    std::string type; // "L1, L2, TbT"

    BookConfig(const std::string& v, const std::string& s, const std::string& bt) :
        venue(v), symbol(s), type(bt) {
        logInfo("BookConfig %s", toString().c_str());
    };

    // construct by venue/symbol and a book type: "L1 or L2"
    BookConfig(const std::string& venu_symbol, const std::string& bt) :
        type(bt) {
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
        // get the current bar file
        return plcc_getString("BarPath")+"/"+
                   venue+"_"+symbol+"_"+std::to_string(barsec)+"S.csv";
    }

    std::string bhfname(int barsec) const {
        // get the previous bar file
        return plcc_getString("HistPath")+"/"+
                   venue+"_"+symbol+"_"+std::to_string(barsec)+"S.csv";
    }

    std::vector<int> barsec_vec() const {
        std::vector<std::string> bsv = plcc_getStringArr("BarSec");
        if (bsv.size() == 0) {
            throw std::runtime_error(std::string("BarSec not found in config"));
        }
        std::vector<int> ret;
        for (const auto& bs: bsv) {
            ret.push_back(std::stoi(bs));
        };
        return ret;
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

    // attr 0: buy, 1 sell
    // qty set as positive for buy, negative for sell
    void addTrade(Price price, Quantity size, int attr) {
        reset();
        type=4;  // trade
        px=price;
        qty=(1-2*attr)*size; // attr: 0: buy, 1: sell
                           // qty : + buy, - sell
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
    int update_type;  // 0: bid, 1: ask, 2: trade
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

    bool addTrade(Price px, Quantity sz, int attr) {
        trade_price=px;
        trade_size=sz;
        return addTrade(attr);
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
        if (__builtin_expect(!isValidTrade() ,0)) {
                return false;
        }
        if (__builtin_expect(!isValidQuote() ,0)) {
                // keep the previous trade_attr
                if (trade_attr == 0) {
                        // take as buy
                        bvol_cum+=trade_size;
                } else if (trade_attr == 1) {
                        // take as sell
                        svol_cum+=trade_size;
                }
        } else {
                        const Price bd=std::abs(getBid()-trade_price);
                        const Price ad=std::abs(getAsk()-trade_price);
                        if (ad>bd) {
                                trade_attr=1;  // sell close to best bid
                                svol_cum+=trade_size;
                        } else {
                                trade_attr=0;
                                bvol_cum+=trade_size;
                        }
        }
        setUpdateType(true, false);
        return true;
    }

    // This is to allow L2 updates to get L1 trade
    // also applies to l2delta
    bool addTrade(int attr) {
        trade_attr = attr;
                if (trade_attr == 0) {
                        // take as buy
                        bvol_cum+=trade_size;
                } else if (trade_attr == 1) {
                        // take as sell
                        svol_cum+=trade_size;
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

    explicit BookL2(const BookConfig& cfg):
            _cfg(cfg), _book(), _avail_level(_book.avail_level)
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

    bool addTrade(Price px, Quantity sz) {
        if (_book.addTrade(px, sz)) {
                _book.l2_delta.addTrade(px,sz, _book.trade_attr);
                return true;
        }
        return false;
     }

    // add trade from another book
    // used for L2 updates gets from L1
    bool addTrade(Price px, Quantity sz, int attr) {
        _book.addTrade(px, sz, attr);
        _book.l2_delta.addTrade(px, sz, attr);
        return true;
     }

    void reset() {
        _book.reset();
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
                int attr = 0;
                Quantity qty = delta->qty;
                if (qty<0) {
                        attr = 1;
                        qty*=-1;
                }
                return this->addTrade(delta->px, qty, attr);
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
        }

        void delPrice(int level, bool is_bid, uint64_t ts_micro) {
            if (__builtin_expect((_bookL2.delPrice(level, is_bid)),1)) {
                                updateQ(ts_micro);
                                return;
            }
        }

        void updPrice(double price, Quantity size, int level, bool is_bid, uint64_t ts_micro) {
            if (__builtin_expect(_bookL2.updPrice(price, size, level, is_bid, ts_micro),1)) {
                                updateQ(ts_micro);
                                return;
            }
        }

        void updBBO(double price, Quantity size, bool is_bid, uint64_t ts_micro) {
            if (__builtin_expect(_bookL2.updBBO(price, size, is_bid, ts_micro), 1)) {
                updateQ(ts_micro);
                return;
            }
        }

        void updBBOPriceOnly(double price, bool is_bid, uint64_t ts_micro) {
            if (_bookL2.updBBOPriceOnly(price, is_bid, ts_micro)) {
                updateQ(ts_micro);
                return;
            }
        }

        void updBBOSizeOnly(Quantity size, bool is_bid, uint64_t ts_micro) {
            if (_bookL2.updBBOSizeOnly(size, is_bid, ts_micro)) {
                updateQ(ts_micro);
                return;
            }
        }

        bool updTrade(double price, Quantity size) {
                if(__builtin_expect(_bookL2.addTrade(price,size),1)) {
                updateQ(utils::TimeUtil::cur_micro());
                return true;
                }
                return false;
        };

        // this is different from updTrade, because the addTrade logic for
        // L2 maybe affected by intra-interval aggregation.  In case the 
        // L1 tick-by-tick is real, L1 trade is more reliable. This is
        // to get a trade direction from L1 book.
        bool updTradeFromL1(double price, Quantity size, const BookDepot& bookL1) {
                if(__builtin_expect(_bookL2.addTrade(price,size, bookL1.trade_attr),1)) {
                    updateQ(utils::TimeUtil::cur_micro());
                    return true;
                }
                return false;
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
                        // state during updates (i.e. update bid and then
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
            {
                int lost_updates = _rq->catchUp();
                logError("venue read queue %s overflow, lost %d updates. Trying to catch up."
                        ,_bq._q_name.c_str(), lost_updates);
                return getNextUpdate(book);
            }
            case utils::QStat_ERROR :
            {
                _rq->advanceToTop();
                logError("venue read queue %s error. Trying to sync."
                        ,_bq._q_name.c_str());
                return getNextUpdate(book);
            }
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
            default:
                logError("getLatestUpdate read queue %s unknown qstat %d, exiting..."
                    ,_bq._q_name.c_str(), (int) stat);
                throw std::runtime_error("BookQ Reader got unknown qstat.");
            }
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
            case utils::QStat_ERROR :
            {
                _rq->advanceToTop();
                logError("venue read queue %s error. Trying to sync."
                    ,_bq._q_name.c_str());
                return false;
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

using BookQType = BookQ<utils::ShmCircularBuffer> ;
using BookQReader = BookQType::Reader;

// query book by venue/symbol and a book type: "L1 or L2"
static inline
bool LatestBook(const std::string& symbol, const std::string& levelStr, BookDepot& myBook) {
    BookConfig bcfg(symbol, levelStr);
    BookQType bq(bcfg, true);
    BookQReader* book_reader = bq.newReader();
    if (!book_reader) {
        logError("Couldn't get book reader!");
        return false;
    }
    bool ret = book_reader->getLatestUpdate(myBook);
    delete book_reader;
    return ret;
}

}  // namespace md
