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
            while (fgets(buf, sizeof(buf)-1, fp));
            if (strlen(buf) > 0 )
                return BarPrice(std::string(buf));
        } catch (const std::exception & e) {
            logError("failed to get last bar price from %s: %s", fn.c_str(), e.what());
        }
        return BarPrice();
    }

    void operator = (const BarReader&) = delete;
};

class BarWriter {
public:

    BarWriter(const BookConfig& bcfg, int64_t cur_micro) 
    : m_bcfg(bcfg), m_bq(bcfg, true), m_br(m_bq.newReader())
    {
        auto bsv = m_bcfg.barsec_vec();
        for (auto bs : bsv) {
            BarInfo binfo;
            binfo.fn = m_bcfg.bfname(bs);
            binfo.fp = fopen(binfo.fn.c_str(), "at");
            binfo.due = (time_t)((int)((cur_micro/1000000LL)/bs+2)*bs);
            m_bar[bs] = binfo;
        }
        checkUpdate();
    }

    bool checkUpdate() {
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
                auto& bar = bitem.second.bar;
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
        while (checkUpdate());

        for (auto& bitem: m_bar) {
            auto& binfo = bitem.second;
            if (!binfo.bar.isValid()) {
                continue;
            }
            const auto& bsec = bitem.first;
            while(cur_sec >= binfo.due) {
                const auto& line = binfo.bar.writeAndRoll(binfo.due);
                fprintf(binfo.fp, "%s\n",line.c_str());
                fflush(binfo.fp);
                binfo.due += bsec;
            }
        }
    }

    ~BarWriter() {
        for (auto& bitem: m_bar) {
            auto& binfo = bitem.second;
            if (binfo.fp) {
                fclose(binfo.fp);
                binfo.fp = nullptr;
            }
        }
    };

private:
        using QType = BookQ<utils::ShmCircularBuffer>;

	const BookConfig m_bcfg;
        QType m_bq;
        std::shared_ptr<typename QType::Reader> m_br;

        struct BarInfo {
            std::string fn;
            FILE* fp;
            time_t due;
            BarPrice bar;
            BarInfo() : fp(nullptr) {};
        };

        std::map<int, BarInfo> m_bar;
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
        m_writers.emplace_back( bcfg, cur_micro);
    }

    void start() {
        m_should_run = true;
        uint64_t cur_micro = TimerType::cur_micro();
        uint64_t next_sec = (cur_micro/1000000ULL + 2ULL) * 1000000ULL;
        const int64_t max_sleep_micro = 1000*50;
        const int64_t min_sleep_micro = 200;
        while (m_should_run) {
            // do update for all
            bool has_update = false;
            for (auto& bw : m_writers) {
                has_update |= bw.checkUpdate();
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
                    bw.onOneSecond((time_t)(next_sec/1000000LL));
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
    std::vector<BarWriter> m_writers;
    volatile bool m_should_run;
};

}
