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

#include "md_snap.h"
#include "csv_util.h"

namespace md {

struct BarPrice {
public:
    time_t bar_time;
    double open;
    double high;
    double low;
    double close;
    uint32_t bvol;
    uint32_t svol;
    long long last_micro;
    BarPrice() {
        memset((char*)this, 0, sizeof(BarPrice));
        high = -1e+12;
        low = 1e+12;
    }
    BarPrice(time_t utc_, double open_, double high_,
             double low_, double close_, uint32_t bvol_ = 0, uint32_t svol_=0)
    : bar_time(utc_), open(open_), high(high_), low(low_), close(close_),
      bvol(bvol_), svol(svol_), last_micro(utc_*1000000LL) {}

    BarPrice(const std::string& csvLine) {
        // format is utc, open, high, low, close, bvol, svol, last_micro
        auto tk = utils::CSVUtil::read_line(csvLine);
        bar_time = (time_t)std::stoi(tk[0]);
        open = std::stod(tk[1]);
        high = std::stod(tk[2]);
        low = std::stod(tk[3]);
        close = std::stod(tk[4]);
        bvol = std::stoul(tk[5]);
        svol = std::stoul(tk[6]);
        last_micro = std::stoll(tk[7]);
    }

    std::string toCSVLine() const {
        char buf[256];
        snprintf(buf, sizeof(buf), "%d, %.7lf, %.7lf, %.7lf, %.7lf, %lu, %lu, %lld",
                (int) bar_time, open, high, 
                low, close, (unsigned long)bvol, (unsigned long)svol, last_micro);
        return std::string(buf);
    };

    bool isValid() const {
        return last_micro>0;
    }

    // utilities for update this data structure
    std::string writeAndRoll(time_t bartime) {
        // this is an atomic action to write the
        // existing state and then roll forward
        bar_time = bartime;
        std::string ret = toCSVLine();
        // roll forward
        open = close;
        high = close;
        low = close;
        bvol = 0;
        svol = 0;
        return ret;
    }

    void update(long long cur_micro, double price, int32_t volume, int update_type) {
        // update at this time with type of update
        // type: 0 - bid update, 1 - ask update, 2 - trade update

        /* this check is not necessary
        if (cur_micro == 0) {
            logError("bar update with zero timestamp!");
            return;
        }
        */

    	switch (update_type) {
    	case 2 : 
            {
    		// trade update
                if (volume > 0) 
                    bvol += volume;
                else 
                    svol -= volume;
                // fall through for pricea
            }
        case 0:
        case 1:
            {
               close = price;
               if (price > high)
                   high = price;
               if (price < low) 
                   low = price;
               break;
            };
    	default :
    	    logError("unknown book update type %d", update_type);
    	}
        if (__builtin_expect(last_micro==0, 0)) {
            // the very first update
            open = price;
        }
        last_micro = cur_micro;
    }

    std::string toString() const {
        return toCSVLine();
    }
};

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
            while ( (bp0->bar_time < bt) && (bcnt < allb.size()) ) {
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
        char buf[2][256];
        buf[0][0] = 0;
        buf[1][0] = 0;
        int i = 0;
        try {
            while (fgets(buf[i]  , sizeof(buf[i])-1, fp)) {
                i = (i+1)%2 ; 
            };
            i = (i+1)%2;
            if (strlen(buf[i]) > 0)
                return BarPrice(std::string(buf[i]));
        } catch (const std::exception & e) {
            logError("failed to get last bar price from %s: %s", fn.c_str(), e.what());
        }
        return BarPrice();
    }

    void operator = (const BarReader&) = delete;
};

class BarWriter {
public:

    BarWriter(const BookConfig& bcfg, int64_t cur_second) 
    : m_bcfg(bcfg), m_bq(bcfg, true), m_br(m_bq.newReader())
    {
        auto bsv = m_bcfg.barsec_vec();
        for (auto bs : bsv) {
            std::shared_ptr<BarInfo> binfo(new BarInfo());
            binfo->fn = m_bcfg.bfname(bs);
            binfo->fp = fopen(binfo->fn.c_str(), "at");
            if (!binfo->fp) {
                logError("%s BarWriter failed to create bar file %s!", m_bcfg.toString().c_str(), binfo->fn.c_str());
                throw std::runtime_error("BarWriter failed to create bar file " + binfo->fn);
            }
            m_bar.emplace(bs, binfo);
        }
        resetTradingDay(cur_second);
        checkUpdate(cur_second);
    }

    void resetTradingDay(time_t cur_second) {
        auto bsv = m_bcfg.barsec_vec();

        // get the start stop utc for current trading day, snap to future
        if (!VenueConfig::get().isTradingTime(m_bcfg.venue, cur_second)) {
            logInfo("%s not currently trading, wait to the next open", m_bcfg.venue.c_str());
        }
        const auto start_end_pair = VenueConfig::get().startEndUTC(m_bcfg.venue, cur_second, 2);
        time_t sutc = start_end_pair.first;
        time_t eutc = start_end_pair.second;

        logInfo("%s BarWriter got trading hours [%lu (%s), %lu (%s)]", m_bcfg.venue.c_str(),
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
                logError("%s %d second BarWriter next due %ld (%s) outside trading session,"
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
            logInfo("%s %d Second BarWriter next due %lu (%s)", m_bcfg.venue.c_str(),
                    (unsigned long) binfo->due,
                    utils::TimeUtil::frac_UTC_to_string(binfo->due, 0).c_str());
        }
    }

    bool checkUpdate(time_t cur_second) {
        if (m_br->getNextUpdate(m_book)) {
            // this check is enforced at publisher
            /*
            if (!m_book.isValid()) {
                return false;
            }
            */

            // got an new snap price, update the bar state
            uint64_t upd_micro = m_book.update_ts_micro;
            double px = m_book.getMid();
            int update_type = m_book.update_type;
            int volume = m_book.trade_size;
            int trade_attr = m_book.trade_attr; // 0-buy, 1-sell
            volume *= (1-2*trade_attr);
            if (update_type == 2) {
                px = m_book.trade_price;
            }

            // update all bars with different bar period
            for (auto& bitem: m_bar) {
                /*
                if (__builtin_expect( cur_second < bitem.second->start, 0)) {
                    // TODO - review this logic. 
                    // Disable it if we need price updates 
                    // outside of trading hours.
                    continue;
                }
                */
                auto& bar = bitem.second->bar;
                bar.update(upd_micro, px, volume, update_type);
            }
            return true;
        }
        return false;
    }

    void onOneSecond(time_t cur_sec) {
        // iterates upto the lastest
        // To avoid delay on each whole second,
        // it is recommended that the thread
        // updates the state periodically.
        while (checkUpdate(cur_sec));

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

private:
        using QType = BookQ<utils::ShmCircularBuffer>;

	const BookConfig m_bcfg;
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
};

template<typename TimerType>
class BarWriterThread {
public:
    BarWriterThread() {};

    void add(const BookConfig& bcfg, uint64_t cur_micro = 0) {
        if (cur_micro == 0) {
            cur_micro = TimerType::cur_micro();
        }
        m_writers.emplace_back(std::make_shared<BarWriter>(bcfg, cur_micro/1000000));
    }

    void start() {
        m_should_run = true;
        uint64_t cur_micro = TimerType::cur_micro();
        uint64_t next_sec = (cur_micro/1000000ULL + 2ULL) * 1000000ULL;
        const int64_t max_sleep_micro = 1000*50;
        const int64_t min_sleep_micro = 400;
        while (m_should_run) {
            // do update for all
            bool has_update = false;
            cur_micro = TimerType::cur_micro();
            for (auto& bw : m_writers) {
                has_update |= bw->checkUpdate(cur_micro/1000000ULL);
            }
            cur_micro = TimerType::cur_micro();
            int64_t due_micro = next_sec - cur_micro;
            if (!has_update) {
                if (due_micro > min_sleep_micro) {
                    if (due_micro > max_sleep_micro) {
                        due_micro = max_sleep_micro;
                    }
                    due_micro -= min_sleep_micro;
                    TimerType::micro_sleep(due_micro);
                    due_micro = next_sec - TimerType::cur_micro();
                    if (__builtin_expect( due_micro < 0, 0)) {
                        logError("Bar writer: negative due_micro detected! %lld",
                                (long long) due_micro);
                    }
                }
            }
            if (due_micro < min_sleep_micro/2) {
                // due for onSec
                for (auto& bw : m_writers) {
                    bw->onOneSecond((time_t)(next_sec/1000000LL));
                }
                next_sec += 1000000LL;
            }
        }
        logInfo("Bar Writer Stopped");
    }

    void stop() {
        m_should_run = false;
    }

private:
    std::vector<std::shared_ptr<BarWriter> >m_writers;
    volatile bool m_should_run;
};

}
