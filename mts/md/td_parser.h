#include <stdio.h>
#include <string>
#include <utility>
#include <functional>
#include <tuple>
#include "md_bar.h"

namespace md {

class BarsFileWriter {
public:
    BarsFileWriter (const std::string& file): _fp(0) {
        _fp = fopen(file.c_str(), "wt");
        if (!_fp) throw std::runtime_error("failed to open" + file + " for writing!");
    }

    void emplace_back(const std::string& line) {
        fprintf(_fp, "%s\n", line.c_str());
    }

    ~BarsFileWriter() {
        if (_fp) {
            fclose(_fp);
            _fp = nullptr;
        }
    }
private:
    FILE* _fp;
};

class NullWriter {
public:
    NullWriter() {};
    void emplace_back(const std::string& line) {
    }
};

template<typename DType>
struct Array2D {
    // thin layer for c-style 2D array
    // no boundary limit is checked
    Array2D(DType* d, size_t rows, size_t cols):
        _d(d), _N(rows), _M(cols) {};
    const DType* operator[](int i) const {
        return &_d[i*_M];
    }
    DType* operator[](int i) {
        return &_d[i*_M];
    }
    double* _d;
    size_t _N, _M;
};

class TickData2Bar {
public:
    TickData2Bar(const std::string& quote_csv,
                 const std::string& trade_csv,
                 time_t start_utc,
                 time_t end_utc,
                 int barsec,
                 double tick_size=0):
        _quote_csv(quote_csv), _trade_csv(trade_csv),
        _start_utc(start_utc), _end_utc(end_utc),
        _barsec(barsec), _tick_size(tick_size) {}

    bool parse(std::vector<std::string>& bars) {
        return get_bar(_quote_csv, _trade_csv, 
                       _start_utc, _end_utc, _barsec, bars,
                       (NullWriter*)nullptr);
    };

    bool parse(const std::string& out_csv_file) {
        BarsFileWriter bars(out_csv_file);
        return get_bar(_quote_csv, _trade_csv,
                       _start_utc, _end_utc, _barsec, bars,
                       (NullWriter*)nullptr);
    }

    template<typename QuoteType, typename TradeType>
    bool parseArray(const QuoteType& q, const TradeType& t,
                    size_t qlen, size_t tlen,
                    const std::string& out_csv_file) {
        BarsFileWriter bars(out_csv_file);
        return get_bar_array(q,t,qlen,tlen, _start_utc, _end_utc, _barsec, bars, (NullWriter*)nullptr);
    }

    bool parseDoubleArray(const double* q, const double* t,
                          size_t qlen, size_t tlen,
                          const std::string& out_csv_file) {
        BarsFileWriter bars(out_csv_file);
        Array2D<double> qa((double*)q, qlen, 5);
        Array2D<double> ta((double*)t, tlen, 3);
        return get_bar_array(qa, ta, qlen, tlen, _start_utc, _end_utc, _barsec, bars, (NullWriter*)nullptr);
    }

    // getting the tick-by-tick dump from quote/trade csv
    bool tickDump(std::vector<std::string>& tickdump) {
        NullWriter bars;
        md::BookDepot book;
        return get_bar(_quote_csv, _trade_csv,
                       _start_utc, _end_utc, _barsec,
                       bars, &tickdump, &book);
    };

    bool tickDump(const std::string& tickdump_file) {
        NullWriter bars;
        BarsFileWriter tickdump(tickdump_file);
        md::BookDepot book;
        return get_bar(_quote_csv, _trade_csv,
                       _start_utc, _end_utc, _barsec,
                       bars, &tickdump, &book);
    };

private:
    static const int cts = 0, cbp = 1, cbs = 2, cap = 3, cas = 4, // quote colums
                     cpx = 1, csz = 2; // trade colums
    static const int quote_cols = 5, trade_cols = 3;

    std::string _quote_csv, _trade_csv;
    time_t _start_utc, _end_utc; // fist bar close at start_utc+barsec
    int _barsec;
    double _tick_size;

    template<typename BarDumpType>
    time_t check_roll(long long cur_micro,
                      md::BarPrice& bar,
                      time_t bar_due, 
                      int barsec,
                      BarDumpType& bars) {
        // assuming the bar hasn't been updated at cur_micro
        // roll bar upto latest bar_due, less or equal to cur_micro
        time_t cur_sec = (time_t) (cur_micro/1000000LL);
        for (; bar_due<=cur_sec; bar_due+=barsec) {
            bars.emplace_back(bar.writeAndRoll(bar_due));
        }
        return bar_due;
    }

    template<typename QuoteType, typename BarDumpType, typename TickDumpType>
    time_t update_quote(const QuoteType& q,
                        md::BarPrice& bp,
                        time_t bar_due,
                        int barsec,
                        BarDumpType& bars,
                        TickDumpType* tickdump=nullptr,
                        md::BookDepot* bookp=nullptr) {
        long long cur_micro = ((long long)(q[cts]+0.5))*1000LL; //q[cts] in milli
        double bpx = q[cbp], apx=q[cap];
        int bsz=(int)(q[cbs]+0.5), asz=(int)(q[cas]+0.5);
        bar_due = check_roll(cur_micro, bp, bar_due, barsec, bars);
        bp.updateQuote(cur_micro, bpx, bsz, apx, asz);
        if (__builtin_expect(tickdump && bookp, 0)) {
            bookp->updateFromQuote(cur_micro, bpx, bsz, apx, asz);
            tickdump->emplace_back(bookp->toCSV());
        }
        return bar_due;
    }

    template<typename TradeType, typename BarDumpType, typename TickDumpType>
    time_t update_trade(const TradeType& t,
                        bool is_buy,
                        md::BarPrice& bp,
                        time_t bar_due,
                        int barsec,
                        BarDumpType& bars,
                        TickDumpType* tickdump=nullptr,
                        md::BookDepot* bookp=nullptr) {
        long long cur_micro = ((long long)(t[cts]+0.5))*1000LL; //q[cts] in milli
        double tpx=t[cpx];
        uint32_t tsz = (uint32_t)(t[csz]+0.5);
        bar_due = check_roll(cur_micro, bp, bar_due, barsec, bars);
        bp.updateTrade(cur_micro, tpx, tsz, is_buy);
        if (__builtin_expect(tickdump && bookp,0)) {
            bookp->updateFromTrade(cur_micro, tpx, tsz);
            tickdump->emplace_back(bookp->toCSV());
        }
        return bar_due;
    };

    template<typename QuoteType>
    std::tuple<double, uint32_t, double, uint32_t> get_reducing(
            const QuoteType& prev_quote,
            const QuoteType& this_quote) {
        // check if this quote is a "reducing" quote, if so,
        // get the reducing price, size and side (is_bid_side)
        // input: vector of 5 elements [utc, bpx, bz, apx, asz] all double
        // return: tuple<bid_side_reducing<px,sz>, ask_side_reducing<px,sz>>
        const auto [bpx,bsz] = bid_reducing(prev_quote[cbp], (uint32_t)(prev_quote[cbs]+0.5), this_quote[cbp], (uint32_t)(this_quote[cbs]+0.5));
        const auto [apx,asz] = bid_reducing(-prev_quote[cap],(uint32_t)(prev_quote[cas]+0.5),-this_quote[cap], (uint32_t)(this_quote[cas]+0.5));
        return {bpx,bsz,-apx,asz};
    }

    // QuoteType could be double** or double(*)[5], or vector<vector<double>>
    // TradeType could be double** or double(*)[3], or vector<vector<double>>
    // BarDumptype could be a vector<std::string> BarWriter or NullWriter
    template<typename QuoteType, typename TradeType, typename BarDumpType, typename TickDumpType>
    bool get_bar_array(const QuoteType& q,
                       const TradeType& t,
                       size_t qlen,
                       size_t tlen,
                       const time_t start_utc,
                       const time_t end_utc,
                       const int barsec,
                       BarDumpType& bars,
                       TickDumpType* tickdump,
                       md::BookDepot* bookp=nullptr) {

        size_t tix=0, qix=0;
        // intialize the first qix and tix
        // remove any tix earlier than qix
        md::BarPrice bp;
        bp.set_write_optional(true);
        bp.set_tick_size(_tick_size);
        time_t bar_utc = start_utc+barsec; // first bar close time

        // adjust qlen, tlen, qix, tix according to end_milli and start_milli
        long long start_milli=start_utc*1000LL;
        long long end_milli=end_utc*1000LL;
        size_t ix=qlen-1; for (; (q[ix][cts]>end_milli) && (ix>qix); --ix); qlen=ix+1;
        ix=tlen-1;        for (; (t[ix][cts]>end_milli) && (ix>tix); --ix); tlen=ix+1;
        ix=0;             for (; (q[ix][cts]<start_milli)&&(ix<qlen);++ix); qix=ix>0?ix-1:0;
        ix=0;             for (; (t[ix][cts]<start_milli)&&(ix<tlen);++ix); tix=ix;

        // check there are ticks
        if (__builtin_expect(qix>=qlen-1,0)) {
            printf("no quote ticks found between [%d,%d]\n",(int)start_utc,(int)end_utc);
            return false;
        }
        // update first quote to initialize the bar
        bar_utc=update_quote(q[qix],bp,bar_utc,barsec,bars,tickdump,bookp);

        // skip any trades before qix
        while ((tix<tlen)&&(t[tix][cts] < q[qix][cts])) {
            ++tix;
        }
        int qix_upd = ++qix; // last updated qix
        bool is_buy=false; // save the previous trade dir for swipe
        while ((tix<tlen) && (qix<qlen)) {
            /* verbose
            if (__builtin_expect((tix%1000==0)||(qix%1000==0),0)) {
            printf(qix(%d/%d),tix(%d/%d)\n",(int)qix, (int)qlen,(int)tix, (int)tlen);
            } */

            // update all qix before tix, if any
            if (q[qix][cts]<t[tix][cts]) {
                bar_utc = update_quote(q[qix],bp,bar_utc,barsec,bars,tickdump,bookp);
                qix_upd = ++qix;
                continue;
            }
            // match trade with quote at the same milli-sec
            int qix0 = -1; // an initial insertion point for the trade
            uint32_t r_sz=0;
            const double tpx=t[tix][cpx], tsz=t[tix][csz];
            while ((qix<qlen) && (q[qix][cts]==t[tix][cts])) {
                const auto [br_px,br_sz,ar_px,ar_sz]=get_reducing(q[qix-1],q[qix]);
                // set r_sz from quote reducing size match with tpx
                // the tpx is same or better, for case of swipe
                //
                if ((br_sz!=0) && (tpx-br_px<1e-10)) {
                    r_sz=br_sz;
                    is_buy=false; // sell
                } else {
                    if ((ar_sz!=0)&&(ar_px-tpx<1e-10)) {
                        r_sz=ar_sz;
                        is_buy=true; // buy
                    }
                }
                // if reducing at the tpx
                //      set qix0 if qix0 is -1
                //      if match sz, 
                //          set qix0 and break
                if (r_sz != 0) {
                    // save the initial reducing ix, in case no match
                    // found, this will be used as the best match
                    qix0 = (qix0==-1?qix:qix0);
                    if (r_sz==tsz) {
                        // exact match!
                        qix0 = qix;
                        break;
                    }
                }
                ++qix;
            }
            // if qix0 is -1, case of no matching time/px found before or at t_utc,
            // update the trade and then update all quotes before or at it
            if (__builtin_expect(r_sz==0,0)) {
                qix0=qix;
                // decide the trade direction
                double bdiff=std::abs(tpx-q[qix-1][cbp]),adiff=std::abs(tpx-q[qix-1][cap]);
                if (bdiff>adiff+1e-10) {
                    is_buy=true;
                } else {
                    if (bdiff<adiff-1e-10) {
                        is_buy=false;
                    }
                } // else - use the previous direction
                // update trade first, it's likely that it is a swipe
                bar_utc=update_trade(t[tix],is_buy,bp,bar_utc,barsec,bars,tickdump,bookp);
                ++tix;
                for (; qix_upd<qix0; ++qix_upd) {
                    bar_utc=update_quote(q[qix_upd],bp,bar_utc,barsec,bars,tickdump,bookp);
                }
            } else {
                // if there were match
                // update from qix_upd+1 to qix0-1
                // and then update the trade at tix
                // take care of swipe
                // then update the quote at qix0
                for (; qix_upd<qix0; ++qix_upd) {
                    bar_utc=update_quote(q[qix_upd],bp,bar_utc,barsec,bars,tickdump,bookp);
                }
                bar_utc=update_trade(t[tix],is_buy,bp,bar_utc,barsec,bars,tickdump,bookp);
                ++tix;

                // check in case of swipe -
                // update all tix that is 
                // 1. same milli
                // 2. increasing tpx
                // 3. strictly less than new quote px
                //
                const double pmul = is_buy?1.0:-1.0;
                auto new_px=q[qix0][is_buy?cap:cbp];
                if (__builtin_expect((new_px-tpx)*pmul>1e-10,0)) {
                    // swipe case
                    auto ts0=t[tix-1][cts];
                    new_px*=pmul;
                    auto tpx0 = tpx*pmul;
                    while ((tix<tlen)&&(t[tix][cts]==ts0)) {
                        auto tpx1=t[tix][cpx]*pmul;
                        if ((tpx1>tpx0-1e-10)&&(tpx1<new_px-1e-10)){
                            // apply this if 
                            // this trade at px that are same or more aggressive with tpx0
                            // but is not at the level of the new quote
                            bar_utc=update_trade(t[tix],is_buy,bp,bar_utc,barsec,bars,tickdump,bookp);
                            ++tix;
                            tpx0=tpx1;
                        } else {
                            break;
                        }
                    }
                }
                qix=qix0;
                if (tsz==r_sz) {
                    // update quote at qix0 if there were exact matching
                    bar_utc=update_quote(q[qix_upd++],bp,bar_utc,barsec,bars,tickdump,bookp);
                    qix=qix0+1;
                }
            }
        }
        // update remaining any tix/qix
        for (; qix<qlen; ++qix) {
            bar_utc = update_quote(q[qix],bp,bar_utc,barsec,bars,tickdump, bookp);
        }
        for (; tix<tlen; ++tix) {
            qix=qlen-1;
            double tpx=t[tix][cpx];
            // decide on a trade direction
            double bdiff=std::abs(tpx-q[qix][cbp]), adiff=std::abs(q[qix][cap]-tpx);
            if (bdiff>adiff+1e-10) {
                is_buy=true;
            } else {
                if (bdiff<adiff-1e-10) {
                    is_buy=false;
                }
            }
            bar_utc=update_trade(t[tix],is_buy,bp,bar_utc,barsec,bars,tickdump,bookp);
        }

        // last bar
        if (bar_utc<=end_utc) {
            check_roll(end_utc*1000000LL,bp,bar_utc,barsec,bars);
        }
        return true;
    };

    template<typename BarDumpType, typename TickDumpType>
    bool get_bar(const std::string& quote_csv,
                 const std::string& trade_csv,
                 time_t start_utc,
                 time_t end_utc,
                 int barsec,
                 BarDumpType& bars,
                 TickDumpType* tickdump,
                 md::BookDepot* bookp=nullptr) {
        const auto& qs(utils::CSVUtil::read_file(quote_csv));
        const auto& ts(utils::CSVUtil::read_file(trade_csv));

        // note:q/t type is not double**, but double(*)[N]
        auto q=new double[qs.size()][quote_cols];
        auto t=new double[ts.size()][trade_cols];

        // populate the q/t
        for (size_t i=0; i<qs.size(); ++i) {
            for (int k=0; k<quote_cols; ++k) {
                q[i][k]=std::stod(qs[i][k]);
            }
        }
        // populate the q/t
        for (size_t i=0; i<ts.size(); ++i) {
            for (int k=0; k<trade_cols; ++k) {
                t[i][k]=std::stod(ts[i][k]);
            }
        }
        bool ret=get_bar_array(q,t,qs.size(),ts.size(),start_utc,end_utc,barsec,bars,tickdump,bookp);
        delete []q;
        delete []t;
        return ret;
    }

};
}


