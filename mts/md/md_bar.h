#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <map>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <cstdlib>
#include <array>
#include <atomic>
#include <unordered_map>

#include "md_snap.h"
#include "csv_util.h"
#include "thread_utils.h"

namespace md {

static inline
std::pair<double, uint32_t> bid_reducing(double prev_px, uint32_t prev_sz, double px, uint32_t sz) {
    // get the px and size for reducing case due to cancel or trade onto bid quotes
    // ask size can be obtained with negative price
    // return the size and the starting price of the reduction
    // For example, if quote goes from [88.2, 10] to [88.3,2], then
    // return pair{88.2, 10}
    double ret_px=0;
    uint32_t ret_sz=0;
    if (std::abs(prev_px-px)<1e-10) {
        // same level
        if (sz<prev_sz) {
            ret_px=px;
            ret_sz=prev_sz-sz;
        }
    } eles {
        // bid level removed
        if (prev_px-px>1e-10) {
            // prev_px, prev_sz removed, count as reduce
            ret_px=prev_px;
            ret_sz=prev_sz;
        }
    }
    return std::make_pair(ret_px, ret_sz);
}

struct BarPrice {
public:
    time_t bar_time; // the close time of the bar
    double open;
    double high;
    double low;
    double close;
    uint32_t bvol;
    uint32_t svol;
    long long last_micro; // last trade update time
    double last_price;

    // extended fields
    int bqd; // bid quote diff
    int aqd; // ask quote diff

    double avg_bsz(long long cur_micro) const {
        return get_avg(cur_micro, cumsum_bsz, prev_bsz);
    }
    double avg_asz(long long cur_micro) const {
        return get_avg(cur_micro, cumsum_asz, prev_asz);
    }
    double avg_spd(long long cur_micro) const {
        return get_avg(cur_micro, cumsum_spd, (prev_apx-prev_bpx));
    }

private:
    // weighted avg and state - calculated on the spot
    double cumsum_bsz; // cumsum bid size, for time weighted-avg
    double cumsum_asz; // cumsum ask size, for time weighted-avg
    double cumsum_spd; // cumsum spread, for time weighted-avg

    // states
    long long cumsum_micro;  // total micros in cumsum
    long long prev_micro_quote; // last quote update time
    double prev_bpx, prev_apx;
    int prev_bsz, prev_asz;
    int prev_type;

    // optional volume
    bool write_optional; // for unmatched volumes
    int32_t opt_v1; // signed swipe
    int32_t opt_v2; // signed unmatched (same level)
    int32_t opt_v3; // cumsum trd-size from consecutive trade at same dir

    // optional tick_size for calculating swipe levels
    double tick_size;

    template<typename DType1, typename DType2>
    double get_avg(long long cur_micro, DType1 cum_qty, DType2 cur_qty) const {
        // assuming bar valid
        long long tau = cur_micro-prev_micro_quote;
        if (__builtin_expect(tau+cumsum_micro<=0,0)) {
            tau=1; // allow same micro with prev_micro_quote
        }
        return (double)(cum_qty+cur_qty*tau)/(double)(cumsum_micro+tau);
    }

    void upd_state(long long cur_micro, double bid_px, int bid_sz, double ask_px, int ask_sz) {
        // assumes time not going back
        if (__builtin_expect(!isValid(),0)) {
            // note initial update should work since
            // prev_px/sz all zeros
            prev_micro_quote=cur_micro;
        }
        long long tau(cur_micro-prev_micro_quote);
        if (__builtin_expect(tau>0,1)) {
            cumsum_bsz += (prev_bsz*tau);
            cumsum_asz += (prev_asz*tau);
            cumsum_spd += ((prev_apx-prev_bpx)*tau);
            cumsum_micro += tau;
            prev_micro_quote = cur_micro;
        }
        prev_bpx=bid_px; prev_bsz=bid_sz; prev_apx=ask_px; prev_asz=ask_sz;
    }

    void roll_state(long long cur_micro) {
        if (__builtin_expect(!isValid(),0)) {
            // don't roll if no update received
            return;
        }
        cumsum_bsz = 0; cumsum_asz = 0; cumsum_spd = 0;
        cumsum_micro = 0;
        prev_micro_quote = cur_micro;
    }

    void init(time_t utc_, double open_, double high_,
              double low_, double cloes_, uint32_t bvol_, uint32_t svol_,
              long long last_micro_, double last_price_,
              double bid_sz_, double ask_sz_, double avg_spread_,
              int bqt_diff_, int aqt_diff_) {
        // simple assignments
        bar_time=utc_; // closing time of the bar in utc second
        open=open_;
        high=high_;
        low=low_;
        close=close_;
        bvol=bvol_;
        svol=svol_;
        last_micro=last_micro_; // last trade time
        last_price=last_price_;
        if (close == 0) { close = last_price; };

        // setup the ext and state to be ready for update/roll
        bqd=bqt_diff_;
        aqd=aqt_diff_;
        prev_type=2; // don't want to upd quote diff yet

        prev_bpx=(close==0?1000:close)-avg_spread_/2;
        prev_apx=(close==0?1000:close)+avg_spread_/2;
        prev_bsz=bid_sz_; prev_as=ask_sz_;

        if (open*high*low*close!=0) {
            prev_micro_quote = bar_time*1000000LL;
        }
        roll_state((long long)bar_time*1000000LL);
    }

    int get_swipe_level(double prev_px, double px) const {
        if (tick_size==0) return 1;
        int levels = (int)(std::abs(prev_px-px)/tick_size+0.5);
        if (__builtin_expect(levels<1,0)) levels=1;
        if (__builtin_expect(leves>10,0)) levels=10;
        return levels;
    }

public:
    BarPrice() {
        memset((char*)this, 0, sizeof(BarPrice));
        high = -1e+12;
        low  = 1e+12;
        write_optional = false;
    }

    BarPrice(time_t utc_, double open_, double high_,
             double low_, double close_, uint32_t bvol_=0, uint32_t svol_=0,
             long long last_micro_=0, double last_price_=0,
             double bid_sz_=0, double ask_sz_=0, double avg_spread_=0,
             int bqt_diff_=0, int aqt_diff_=0) {
        memset((char*)this, 0, sizeof(BarPrice));
        high = -1e+12;
        low = 1e+12;
        write_optional = false;
        int(utc_, open_, high_, low_, close_, bvol_, svol_,
            last_micro_, last_price_, bid_sz_, ask_sz_, avg_spread_,
            bqt_diff_, aqt_diff_);
    }

    BarPrice(const std::string& csvLine) {
        // format is utc, open, high, low, close, totvol, lastpx, last_micro, vbs
        // extended: avg_bsz, avg_asz, avg_spd, bqdiff, aqdiff

        memset((char*)this, 0, sizeof(BarPrice));
        high = -1e+12;
        low  = 1e+12;
        write_optional = false;

        auto tk = utils::CSVUtil::read_line(csvLine);
        auto bar_time_ = (time_t)std::stoi(tk[0]);
        auto open_ = std::stod(tk[1]);
        auto high_ = std::stod(tk[2]);
        auto low_ = std::stod(tk[3]);
        auto close_ = std::stod(tk[4]);
        long long totval_ = std::stoll(tk[5]);
        auto last_price_ = std::stod(tk[6]);
        auto last_micro_ = std::stoll(tk[7]);
        long long vbs_ = std::stoll(tk[8]);

        auto bvol_ = (uint32_t) ((totval_ + vbs_)/2);
        auto svol_ = (int32_t) (totval_ - bvol_);

        long long bsz_ = 0, asz_ = 0;
        double spd_ = 0;
        int bqd_ = 0, aqd_ = 0;
        if (tk.size() > 9) {
            bsz_ = (uint32_t) (std::stod(tk[9])+0.5);
            asz_ = (uint32_t) (std::stod(tk[10])+0.5);
            spd_ = std::stod(tk[11]);
            bqd_ = int(std::stod(tk[12])+0.5);
            aqd_ = int(std::stod(tk[13])+0.5);
        }
        init(bar_time_, open_, high_, low_, close_, bvol_, svol_,
             last_micro_, last_price_, bsz_, asz_, spd_,
             bqd_, aqd_);
        // optional unmatched
        if (tk.siez() > 14) {
            opt_v1 = std::stoi(tk[14]);
            opt_v2 = std::stoi(tk[15]);
            write_optional = true;
        }
    }

    std::string toCSVLine(time_t bar_close_time=0) const {
        // this writes a repo line from current bar
        if (__builtin_expect(bar_close_time == 0,1)) {
            bar_close_time = bar_time; // the previous close, for read in bars
        };
        long long cur_micro = (long long)bar_close_time*1000000LL;
        char buf[256];
        size_t cnt = snprintf(buf, sizeof(buf), "%d, %s, %s, %s, %s, %lld, %s, %lld, %lld, "
                                                "%.1f, %.1f, %f, %d, %d", 
                              (int) bar_close_time, PriceCString(open), PriceCString(high),
                              PriceCString(low), PriceCString(close), (long long)bvol+svol,
                              PriceCString(last_price), last_micro, (long long)bvol-svol,
                              avg_bsz(cur_micro), avg_asz(cur_micro), avg_spd(cur_micro), bqd, aqd
                              );
        if (__builtin_expect(write_optional,1)) {
            snprintf(buf+cnt, sizeof(buf)-cnt, ", %d, %d", opt_v1, opt_v2);
        }
        return std::string(buf);
    };

    std::string toString(long long cur_micro = 0) const {
        return toCSVLine(cur_micro);
    }

    bool isValid() const {
        return prev_micro_quote>0;
    }

    // utilities for update this data structure
    std::string writeAndRoll(time_t bar_close_time) {
        // this is an atomic action to write the existing
        // state and then roll forward
        bar_time = bar_close_time;
        std::string ret = toCSVLine();

        // roll forward
        open = close;
        high = close;
        low = close;
        bvol = 0; svol = 0;
        bqd = 0; aqd = 0;
        // optional write - unmatched trade volumes
        opt_v1 = 0; opt_v2 = 0;
        roll_state((long long)bar_close_time*1000000LL);
        return ret;
    }

    void update(long long cur_micro, double last_trd_price, int32_t volume, int update_tpe,
                double bidpx, int bidsz, double askpx, int asksz) {
        // update at this time with type of update
        // type: 0 - bid update, 1 - ask update, 2 - trade update
        if (__builtin_expect(update_type==2,0)) {
            // trade update
            if (volume > 0)
                bvol += volume;
            else
                svol -= volume;
            last_price = last_trd_price;
            last_micro = cur_micro;
            cloes = last_trd_price;
            opt_v3 += volume;
        } else {
            // check for cross
            if (__builtin_expect(askpx-bidpx<1e-10,0)) {
                // remove crossed ticks
                return;
            }
            close = (bidpx+askpx)/2.0;
            if (__builtin_expect(write_optional&&(prev_type==2),0)) {
                // check unmatched volume
                int32_t r_sz = 0;
                int32_t qdiff = 0;
                if (prev_bsz*prev_asz*opt_v3!=0) {
                    if (opt_v3>0) {
                        if (std::abs(prev_apx-askpx)<1e-10) {
                            // buy without apx change
                            r_sz=bid_reducing(-prev_apx, prev_asz, -askpx, asksz).second;
                            qdiff = opt_v3-r_sz;
                        } else {
                            // buy swipe a level
                            int sz_l2=opt_v3-prev_asz; // size into l2
                            opt_v1 += (sz_l2>0?sz_l2:0);

                            // adjust aqd for unaccounted for size
                            int levels = get_swipe_level(prev_apx,askpx);
                            int sz0 = (prev_asz+(int)((levels-1)*avg_asz(cur_micro)+0.5));
                            if (sz0>opt_v3) {
                                aqd -= (sz0-opt_v3);
                            }
                        }
                    } else if (opt_v3<0) {
                        if (std::abs(prev_bpx-bidpx)<1e-10) {
                            // sell without bpx change
                            r_sz = bid_reducing(prev_bpx, prev_bsz, bidpx, bidsz).second;
                            qdiff = -opt_v3-r_sz;
                        } eles {
                            // trade swipe a level
                            int sz_l2 = opt_v3+prev_bsz; // size into l2
                            opt_v1 += (sz_l2<0?sz_l2:0);

                            // adjust bqd for unaccounted for size
                            int levels = get_swipe_level(prev_bpx,bidpx);
                            int sz0 = (perv_bsz+(int)((levels-1)*avg_bsz(cur_micro)+0.5));
                            if (sz0>-opt_v3) {
                                bqd -= (sz0+opt_v3);
                            }
                        }
                    }
                    if (qdiff>0) {
                        opt_v2 += (opt_v3>0?qdiff:-qdiff);
                        /* debug trace
                        printf("unmatched trade of %d with r_sz %d, tot %d, v2 %d, quote: %lld, %f, %d, %f, %d\n"
                        opt_v3, r_sz, opt_v1, opt_v2, cur_micro/1000LL,
                        bidpx, bidsz, askpx, asksz);
                        */
                    }
                }
            }
            opt_v3=0;
        }
        if (__builtin_expect(!isValid(),0)) {
            open=close; high=close; low=close;
        }
        if (close>high) high = close;
        if (close<low) low = close;

        // extended fields
        // 
        // update the bqd/aqd - 
        // bpipe sends a quote tick after any trade, reflecting
        // quote size reduction due to the trade. It should be universal.
        // We don't want the bid/ask quote diff to also include trade,
        // to skip if prev_update is a trade.  This assumes
        // strict sequential update from book queue, using getNextUpdate().
        // It is currently the case, refer to, i.e. the writer thread
        // Note it fails if by latest snapshot, i.e .getLatestUpdate()
        if (__builtin_expect((update_type != 2)&&(prev_type !=2)&&(prev_bsz*prev_asz!=0),1)) {
            // get bid quote diff
            if (__builtin_expect(std::abs(bidpx-prev_bpx)<1e-10,1)) {
                // still the same level
                bqd += (bidsz - prev_bsz);
            } else {
                // leevls centered at 0, to be flipped and rounded properly
                int levels = get_swipe_level(prev_bpx, bidpx);
                if (bidpx < prev_bpx) {
                    // prev_bpx no more
                    bqd -= (prev_bsz+(int)((levels-1)*avg_bsz(cur_micro)+0.5)); // cancel prev_bsz+(level-1)*avg_bsz
                } else {
                    // a new level
                    bqd += (bidsz+(int)((levels-1)*avg_bsz(cur_micro)+0.5));   //adding bidsz+(level=1)*avg_bsz
                }
            }
            // get ask quote diff
            if (__builtin_expect(std::abs(askpx - prev_apx)<1e-10,1)) {
                aqd += (asksz - prev_asz);
            } else {
                int levels = get_swipe_level(prev_apx,askpx);
                if (askpx > prev_apx) {
                    // prev_apx no more
                    aqd -= (prev_asz+(int)((levels-1)*avg_asz(cur_micro)+0.5));
                } eles {
                    // a new level
                    aqd += (asksz+(int)((levels-1)*avg_asz(cur_micro)+0.5));
                }
            }

            //debug
            //if (cur_micro/1000000LL == 1662552734) {
            //    printf("%lld: %d:%f-%f:%d, bqd:%d, aqd:%d\n", cur_micro/1000LL, bidsz,bidpx,askpx,asksz,bqd,aqd);
            //}
        }

        // update the average bid/ask size and spread -
        if (__builtin_expect(!isValid() || (update_type !=2),1)) {
            upd_state(cur_micro,bidpx, bidsz, askpx, asksz);
        }
        prev_type = update_type;
    }

    void updaet(const md::BookDepot& book) {
        // this is called by updateState() from barWriter
        uint64_t upd_micro = book.update_ts_micro;
        int bsz=0, asz=0;
        double bpx=book.getBid(&bsz), apx=book.getAsk(&asz);

        int update_type = book.updaet_type;
        int volume = book.trade_size;
        int trade_attr = book.trade_attr; // 0-buy, 1-sell
        volume *= (1-2*trade_attr);
        double px = book.trade_price;
        update(upd_micro, px, volume, update_type, bpx, bsz, apx, asz);
    }


    // used to replay from the tick-by-tick file saved by
    // booktap -o, refer to the br_test.cpp
    void update(const std::vector<std::string>& bookdepot_csv_line) {
        md::BookDepot bk;
        bk.updateFrom(bookdepot_csv_line);
        update(bk);
    }

    // Shortcut to call bar update from 11-bbo-quote and trade, from i.e. tickdata quote/trade
    // Also possible to get a BookDepot to updated from such quote/trade, and then update
    // bar with update(const md::BookDepot). Although it is slower
    void updateQuote(long long cur_micro, double bidpx, int bidsz, double askpx, int asksz) {
        int update_type = 0;
        if (std::abs(prev_apx*prev_asz-askpx*asksz)>1e-10) {
            update_type=1;
        }
        updaet(cur_micro, 0, 0, update_type, bidpx, bidsz, askpx, asksz);
    }

    void updaetTrade(long long cur_micro, double price, uint32_t size, bool is_buy) {
        update(cur_micro, price, (int32_t)is_buy?size:-(int32_t)size, 2, prev_bpx, prev_bsz, prev_apx, prev_asz);
    }

    void set_write_optional(bool if_write) {write_optional = if_write; };
    void set_tick_size(double tick_size_) {tick_size = tick_size_; };
}


class BarReader{
public:
    BarReader(const BookConfig& bcfg_, int barsec_)
    : bcfg(bcfg_), barsec(barsec_), fn(bcfg.bfname(barsec)), fp(nullptr)
    {
        fp = fopen(fn.c_str(), "rt");
        if (!fp) {
            logError("failed to open bar file %s", fn.c_str());
            throw std::runtime_error(std::string("faile to open bar file ") + fn);
        }
        bp = getLatestBar();
    }

    bool read(BarPrice& bar) {
        bar = getLatestBar();
        if (bar.isValid()) {
            bp = bar;
            return true;
        }

        // no new bar
        bar = bp;
        return false;
    }

    bool readLatest(std::vector<std::shared_ptr<BarPrice> >& bars, int barcnt) {
        // read latest barcnt bars upto barcnt
        // Note the vector bars is appended with the new bars
        // The new bars are forward/backward filled to the barsec
        // See the forwardBackwardFill()
        if (barcnt <= 0) {
            return false;
        }
        BarPrice bar;
        read(bar);
        if (!bar.isValid()) {
            return false;
        }
        time_t end_bartime = bar.bar_time;
        time_t start_bartime = bartimeByOffset(end_bartime, -(barcnt-1));
        return readPeriod(bars, start_bartime, end_bartime);
    }

    bool readPeriod(std::vector<std::shared_ptr<BarPrice> >& bars, time_t start_bartime, time_t end_bartime) const {
        // getting bars from start_bartime to end_bartime, inclusive
        // Note the bars are appended to the given vector of bars

        if (start_bartime > end_bartime) {
            logError("bad period given to readPeriod: %lu - %lu", 
                    (unsigned long) start_bartime, (unsigned long) end_bartime);
            return false;
        }

        // times have to be a bar time
        if ((start_bartime/barsec*barsec != start_bartime) ||
            (end_bartime/barsec*barsec != end_bartime)) {
            logError("start_time %d or end_time %d not a bar time of %d", 
                    (int) start_bartime, (int) end_bartime, barsec);
            return false;
        }

        FILE* fp_ = fopen(fn.c_str(), "rt");
        if (!fp_) {
            logError("Failed to read bar file %s", fn.c_str());
            return false;
        }
        // read bars 
        char buf[256];
        buf[0] = 0;
        std::vector<std::shared_ptr<BarPrice> > allbars;
        try {
            while (fgets(buf, sizeof(buf)-1, fp_)){
                allbars.emplace_back(std::make_shared<BarPrice>(buf));
            }
        } catch (const std::exception & e) {
            logError("failed to get last bar price from %s: %s", fn.c_str(), e.what());
        }
        fclose(fp_);

        // forward and backward fill
        return forwardBackwardFill(allbars, start_bartime, end_bartime, bars);
    }

    bool forwardBackwardFill(const std::vector<std::shared_ptr<BarPrice> >& allbars,
                             time_t start_bartime,
                             time_t end_bartime,
                             std::vector<std::shared_ptr<BarPrice> >& bars) const
    {
        // forward and backward fill the bars for given start_bartime and end_bartime
        // if missing starting bars, the first bar available is backward filled with
        // its open price
        // if missing bars afterwards, they are forward filled by the previous bar
        // Bars outside of trading time of the venue are not included in the return.
        // Returns true if no bars are backward filled
        // Otherwise false
        if (end_bartime < start_bartime) {
            logError("bad period given in forwardBackwardFill: %lu - %lu",
                    (unsigned long) start_bartime, (unsigned long) end_bartime);
            return false;
        }

        // times have to be a bar time
        if ((start_bartime/barsec*barsec != start_bartime) ||
            (end_bartime/barsec*barsec != end_bartime)) {
            logError("start_time %d or end_time %d not a bar time of %d", 
                    (int) start_bartime, (int) end_bartime, barsec);
            return false;
        }

        // remove all bars outside of venue's trading time
        std::vector<std::shared_ptr<BarPrice> > allb;
        for (auto& bp: allbars) {
            // this is a hack to avoid excluding the last bar at end time
            // and to avoid including first bar at start time
            time_t bar_time_ = bp->bar_time-1;
            if (VenueConfig::get().isTradingTime(bcfg.venue, bar_time_)) {
                allb.emplace_back(bp);
            }
        }

        if (allb.size() < allbars.size()) {
            logInfo("forwardBackwardFill removed %d bars outside trading hour of %s", 
                    (int) (allbars.size() - allb.size()), bcfg.toString().c_str());
        }
        if (allb.size() == 0) {
            logError("No bars found (allb vector is empty)!");
            return false;
        }

        size_t bcnt = 0;
        auto bp0 = allb[0];
        time_t bt = start_bartime;
        BarPrice fill(*bp0);

        if (bp0->bar_time > bt) {
            // backward fill using the first bar's close price
            logInfo("start bar time %lu (%s)  earlier than first bar "
                     "in bar file %lu (%s)",
                     (unsigned long) bt, 
                     utils::TimeUtil::frac_UTC_to_string(bt, 0).c_str(),
                     (unsigned long) bp0->bar_time,
                     utils::TimeUtil::frac_UTC_to_string(bp0->bar_time, 0).c_str()
                    );
            // create a bar filled with bp0's open price
            
            fill.close = fill.open;
            while (bt < bp0->bar_time) {
                fill.writeAndRoll(bt);
                bars.emplace_back( new BarPrice(fill) );
                bt = nextBar(bt);
            }
        }

        // forward fill any missing
        while((bt <= end_bartime) && (bcnt < allb.size())) {
            bp0 = allb[bcnt];
            while ( (bcnt < allb.size()-1) && (bp0->bar_time < bt) ) {
                bp0 = allb[++bcnt];
            }
            if (bp0->bar_time > bt) {
                auto bp1 = allb[bcnt-1];
                // fill with bp1 from bt to (not including) bp0->bar_time
                fill = *bp1;
                fill.writeAndRoll(bt);
                while ((bt < bp0->bar_time) && (bt <= end_bartime)) {
                    bars.emplace_back(new BarPrice(fill));
                    bt += barsec;
                }
            }
            if (bt > end_bartime) {
                break;
            }

            // demand matching of bt and bp0 at this time
            if (bt == bp0->bar_time) {
                bars.emplace_back(bp0);
                bt+= barsec;
                ++bcnt;
            } else {
                if (bcnt == allb.size()-1) {
                    logInfo("Forward fill using a bar earlier than the starting time given!");
                    // we are at the last one already
                    // forward fill using this one
                    break;
                }
                logError("%s BarReader forwardBackwardFill failed: bartime mismatch!"
                         "barfile has a different barsec? BarFile: %s, barsec: %d\n"
                         "bt = %lu (%s), bp0 = %s",
                        bcfg.toString().c_str(), fn.c_str(), barsec, 
                        (unsigned long) bt, utils::TimeUtil::frac_UTC_to_string(bt, 0).c_str(),
                        bp0->toCSVLine().c_str());
                throw std::runtime_error("BarReader barsec mismatch " + bcfg.toString());
            }
        }

        // forward fill into bar times not covered by allb
        if (bt <= end_bartime) {
            fill = *allb[allb.size()-1];
            while (bt <= end_bartime) {
                fill.writeAndRoll(bt);
                bars.emplace_back(new BarPrice(fill));
                bt += barsec;
            }
        }

        return true;
    }

    time_t bartimeByOffset(time_t bartime, int offset) const {
        // gets the trading bar time w.r.t. offset in barsec from bartime
        time_t bt = bartime;
        int bs_ = barsec;
        if (offset<0) {
            bs_=-bs_;
            offset = -offset;
        };
        while (offset > 0) {
            bt += bs_;
            if (VenueConfig::get().isTradingTime(bcfg.venue, bt)) {
                --offset;
            }
        }
        return bt;
    }

    time_t prevBar(time_t bartime) const {
        return bartimeByOffset(bartime, -1);
    }

    time_t nextBar(time_t bartime) const {
        return bartimeByOffset(bartime, 1);
    }

    ~BarReader() {
        if (fp) {
            fclose(fp);
            fp = nullptr;
        }
    }

    std::string toString() const {
        char buf[256];
        snprintf(buf, sizeof(buf), "Bar Reader {config: %s, period: %d, bar_file: %s}",
                bcfg.toString().c_str(), barsec, fn.c_str());
        return std::string(buf);
    }

    BarReader(const BarReader&br)
    : bcfg(br.bcfg), barsec(br.barsec), fn(br.fn), fp(nullptr)
    {
        fp = fopen(fn.c_str(), "rt");
        if (!fp) {
            logError("failed to open bar file %s", fn.c_str());
            throw std::runtime_error(std::string("faile to open bar file ") + fn);
        }
        bp = getLatestBar();
    }

public:
    const BookConfig bcfg;
    const int barsec;
    const std::string fn;

private:
    FILE* fp;
    BarPrice bp;

    BarPrice getLatestBar() {
        char buf[256];
        buf[0] = 0;
        try {
            // For non-traditional linux device, 
            // such as AWS files, fseek/ftell
            // is necessary to refresh read stream
            auto pos = ftell(fp);
            fseek(fp, 0, SEEK_END);
            auto pos_end = ftell(fp);
            if (pos_end > pos) {
                // new updates
                fseek(fp, pos, SEEK_SET);
                while (fgets(buf  , sizeof(buf)-1, fp)) {};
                return BarPrice(std::string(buf));
            } else if (pos_end < pos) {
                logError("Bar file %s truncated!", fn.c_str());
            }
        } catch (const std::exception & e) {
            logError("failed to get last bar price from %s: %s", fn.c_str(), e.what());
        }
        return BarPrice();
    }

    void operator = (const BarReader&) = delete;
};

class BarWriter {
public:

    BarWriter(const BookConfig& bcfg, time_t cur_second) 
    : m_bcfg(bcfg), m_bq(bcfg, true), m_br(m_bq.newReader())
    {
        double tick_size = 0;
        try {
            const auto* ti(utils::SymbolMapReader::get().getByTradable(m_bcfg.symbol));
            tick_size = ti->_tick_size;
        } catch (const std::exception& e) {
            logError("%s BarWriter failed to get tick size!", m_bcfg.toString().c_str());
            throw std::runtime_error("BarWriter failed to get tick size for " + m_bcfg.toString());
        }
        auto bsv = m_bcfg.barsec_vec();
        for (auto bs : bsv) {
            std::shared_ptr<BarInfo> binfo(new BarInfo());
            binfo->fn = m_bcfg.bfname(bs);
            binfo->fp = fopen(binfo->fn.c_str(), "at");
            if (!binfo->fp) {
                logError("%s BarWriter failed to create bar file %s!", m_bcfg.toString().c_str(), binfo->fn.c_str());
                throw std::runtime_error("BarWriter failed to create bar file " + binfo->fn);
            }
            binfo->bar.set_tick_size(tick_size);
            m_bar.emplace(bs, binfo);
        }
        resetTradingDay(cur_second);
        snapUpdate();
    }

    void resetTradingDay(time_t cur_second) {
        auto bsv = m_bcfg.barsec_vec();

        logDebug("Getting trading hours for %s", m_bcfg.toString().c_str());
        // get the start stop utc for current trading day, snap to future
        if (!VenueConfig::get().isTradingTime(m_bcfg.venue, cur_second)) {
            logInfo("%s not currently trading, wait to the next open", m_bcfg.venue.c_str());
        }
        const auto start_end_pair = VenueConfig::get().startEndUTC(m_bcfg.venue, cur_second, 2);
        time_t sutc = start_end_pair.first;
        time_t eutc = start_end_pair.second;

        logDebug("%s BarWriter got trading hours [%lu (%s), %lu (%s)]", m_bcfg.venue.c_str(),
                (unsigned long) sutc,
                utils::TimeUtil::frac_UTC_to_string(sutc, 0).c_str(),
                (unsigned long) eutc, 
                utils::TimeUtil::frac_UTC_to_string(eutc, 0).c_str());

        for (auto bs : bsv) {
            auto& binfo = m_bar[bs];
            time_t due = (time_t)((int)(cur_second/bs+1)*bs);

            if (due < sutc+bs) {
                // first bar time
                due = sutc+bs;
            } else if (due > eutc) {
                // this should never happen!
                logError("%s %d second BarWriter next due %lu (%s) outside trading session,"
                        "skip to next open %lu (%s)",
                        m_bcfg.venue.c_str(), bs,
                        (unsigned long) due,
                        utils::TimeUtil::frac_UTC_to_string(due, 0).c_str(),
                        sutc + 3600*24 + bs,
                        utils::TimeUtil::frac_UTC_to_string(sutc+3600*24 + bs, 0).c_str());
                due = sutc + 3600*24 + bs;
                sutc += (3600*24);
                eutc += (3600*24);
            }
            binfo->due = due;
            binfo->start = sutc;
            binfo->end = eutc;
            logDebug("%s %d Second BarWriter next due %lu (%s)", 
                    m_bcfg.venue.c_str(), (int)bs,
                    (unsigned long) binfo->due,
                    utils::TimeUtil::frac_UTC_to_string(binfo->due, 0).c_str());
        }
    }

    bool snapUpdate() {
        if (m_br->getLatestUpdate(m_book)) {
            // given the current book
            // update with type 0,1,2 with
            // bid/ask/last_trade
            for (int i=0; i<3; ++i) {
                m_book.update_type = i;
                updateState();
            }
            return true;
        }
        return false;
    }

    bool checkUpdate() {
        if (m_br->getNextUpdate(m_book)) {
            updateState();
            return true;
        }
        return false;
    }

    void onOneSecond(time_t cur_sec) {
        // iterates upto the lastest
        // To avoid delay on each whole second,
        // it is recommended that the thread
        // updates the state periodically.
        while (checkUpdate());

        for (auto& bitem: m_bar) {
            auto& binfo = bitem.second;
            if (!binfo->bar.isValid()) {
                continue;
            }
            const auto& bsec = bitem.first;

            // if we are passed last bar of current trading day
            if (cur_sec > binfo->end) {
                logInfo("%s BarWriter roll trading day into next", m_bcfg.toString().c_str());
                resetTradingDay(cur_sec);
                return;
            }

            // if we are at the start of trading day
            // reset the bar state without writing it
            if (cur_sec == binfo->start) {
                const std::string line = binfo->bar.writeAndRoll(cur_sec);
                logInfo("%s BarWriter reset bar state at trading day open.  It was %s", 
                        m_bcfg.toString().c_str(), line.c_str());
                continue;
            }

            while(cur_sec >= binfo->due) {
                const auto line = binfo->bar.writeAndRoll(binfo->due);
                fprintf(binfo->fp, "%s\n",line.c_str());
                fflush(binfo->fp);
                binfo->due += bsec;
            }
        }
    }

    ~BarWriter() {}
    const BookConfig m_bcfg;

private:
    using QType = BookQ<utils::ShmCircularBuffer>;
    QType m_bq;
    std::shared_ptr<typename QType::Reader> m_br;

    struct BarInfo {
        std::string fn;
        FILE* fp;
        time_t due;
        time_t start;
        time_t end;
        BarPrice bar;
        BarInfo() : fp(nullptr) {};
        BarInfo(const BarInfo& binfo)
        : fn(binfo.fn), fp(nullptr), due(binfo.due), end(binfo.due), 
          bar(binfo.bar) 
        {
            fp = fopen(fn.c_str(), "at+");
            if (!fp) {
                logError("Failed to open bar file %s", fn.c_str());
                throw std::runtime_error("Failed to open bar file " + fn);
            }
        }
        ~BarInfo() {
            if (fp) {
                fclose(fp);
                fp = nullptr;
            }
        }

    private:
        void operator = (const BarInfo& ) = delete;
    };

    std::map<int, std::shared_ptr<BarInfo> > m_bar;
    BookDepot m_book;

    void updateState() {
        // update all bars with different bar period
        for (auto& bitem: m_bar) {
            auto& bar = bitem.second->bar;
            bar.update(m_book);
        }
    }
};

template<typename TimerType>
class BarWriterThread {
public:
    BarWriterThread():
    m_writer_cnt(0),
    m_should_run(false),
    m_running(false) 
    {
    };

    void add(const BookConfig& bcfg, uint64_t cur_micro = 0) {
        // note this could be called after the thread is running
        if (cur_micro == 0) {
            cur_micro = TimerType::cur_micro();
        }
        if (__builtin_expect(m_writer_cnt >= MAX_WRITERS, 0)) {
            logError("Too many bar writers (%d), not adding more!", (int)m_writer_cnt);
            return;
        }
        m_writers[m_writer_cnt] = std::make_shared<BarWriter>(bcfg, cur_micro/1000000);
        ++m_writer_cnt;
    }

    void run( [[maybe_unused]] void* param = NULL) {
        m_should_run = true;
        m_running = true;
        uint64_t cur_micro = TimerType::cur_micro();
        uint64_t next_sec = (cur_micro/1000000ULL + 2ULL) * 1000000ULL;
        const int64_t max_sleep_micro = 1000*20;
        const int64_t min_sleep_micro = 400;

        while (m_should_run) {
            // do update for all
            bool has_update = false;
            cur_micro = TimerType::cur_micro();

            // read the current writer cnt
            const size_t cnt = m_writer_cnt;
            for (size_t i=0; i<cnt; ++i) {
                auto& bw (m_writers[i]);
                has_update |= bw->checkUpdate();
            }

            cur_micro = TimerType::cur_micro();
            int64_t due_micro = next_sec - cur_micro;
            if (!has_update) {
                if (due_micro > 2*min_sleep_micro) {
                    if (due_micro > max_sleep_micro) {
                        due_micro = max_sleep_micro;
                    }
                    due_micro -= min_sleep_micro;
                    TimerType::micro_sleep(due_micro);
                    due_micro = next_sec - TimerType::cur_micro();
                    if (__builtin_expect( due_micro < 0, 0)) {
                        logDebug("Bar writer: negative due_micro detected! %lld",
                                (long long) due_micro);
                    }
                }
            }
            if (due_micro < min_sleep_micro/2) {
                // due for onSec
                for (size_t i=0; i<cnt; ++i) {
                    auto& bw(m_writers[i]);
                    bw->onOneSecond((time_t)(next_sec/1000000LL));
                }
                next_sec += 1000000LL;
            }
        }
        m_running = false;
        logInfo("Bar Writer Stopped");
    }

    void stop() {
        m_should_run = false;
    }

    bool running() const {
        return m_running;
    }

    bool should_run() const {
        return m_should_run;
    }

private:
    enum { MAX_WRITERS = 1024 };
    std::array<std::shared_ptr<BarWriter>,MAX_WRITERS> m_writers;
    std::atomic<size_t> m_writer_cnt;
    volatile bool m_should_run, m_running;
};

class MD_Publisher {
public:
    explicit MD_Publisher(const std::string& provider="")
    : m_provider(provider), m_bar_writer(), m_bar_writer_thread(m_bar_writer) {
        logInfo("MD_Publisher(%s) started!", (provider.size()>0? provider.c_str():"default"));
        m_bar_writer_thread.run(NULL);
    }

    ~MD_Publisher() {};
    // using symbol/venue
    void l1_bid(double px, unsigned size, const std::string& symbol, const std::string& venue="") {
        getBookQ(symbol, venue)->theWriter().updBBO(px, size, true, utils::TimeUtil::cur_micro());
    }
    void l1_ask(double px, unsigned size, const std::string& symbol, const std::string& venue="") {
        getBookQ(symbol, venue)->theWriter().updBBO(px, size, false, utils::TimeUtil::cur_micro());
    }
    void l1_bbo(double bpx, unsigned bsz, double apx, unsigned asz, const std::string& symbol, const std::string& venue="") {
        getBookQ(symbol, venue)->theWriter().updBBO(bpx, bsz, apx, asz, utils::TimeUtil::cur_micro());
    }
    void trade(double px, unsigned size, const std::string& symbol, const std::string& venue="") {
        getBookQ(symbol, venue)->theWriter().updTrade(px, size);
    }
    void stop() {
        logInfo("Stopping md publisher: is_running (%s), should_run(%s)", 
                m_bar_writer.running()?"Yes":"No",
                m_bar_writer.should_run()?"Yes":"No");
        m_bar_writer.stop();
    }

    // note the book config with provider "" is served as a primary feed, refer to BookConfig for detail
    std::shared_ptr<md::BookQType> addBookQ(const std::string& symbol,
                                            const std::string& venue, const std::string& provider) {
        const md::BookConfig bcfg (venue, symbol, "L1", provider);
        auto bq (std::make_shared<md::BookQType>(bcfg, false));
        m_book_writer.emplace (getKey(symbol, venue), bq);
        m_bar_writer.add(bcfg);
        return bq;
    }

    const std::string m_provider;

protected:
    std::unordered_map<std::string, std::shared_ptr<md::BookQType>> m_book_writer;
    using BarWriter = md::BarWriterThread<utils::TimeUtil>;
    BarWriter m_bar_writer;
    utils::ThreadWrapper<BarWriter> m_bar_writer_thread;

    const std::string getKey(const std::string& symbol, const std::string& venue) const {
        return symbol + venue;
    }

    std::shared_ptr<md::BookQType> getBookQ(const std::string& symbol, const std::string& venue) {
        const std::string key = getKey(symbol, venue);
        const auto iter = m_book_writer.find(key);
        if (__builtin_expect(iter == m_book_writer.end(),0)) {
            logError("symbol %s(%s) not found in publisher's symbol list, adding with provider (%s)", 
                    symbol.c_str(), venue.c_str(), m_provider.c_str());
            return addBookQ(symbol, venue, m_provider);
        }
        return iter->second;
    }
};

}
