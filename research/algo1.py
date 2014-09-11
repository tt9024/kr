from corr4 import *

def ifEnterBase(price, cur_tick) :
    # check a 6 point slope
    return True

def ifExitBase(price, cur_tick) :
    return True

def ifExitTrend(price, cur_tick, barsize = 5, slp_exit = -1e-6, lookback=60, dx_exit=4) :
    slp, dx, mse = getTrend(price[:,4], cur_tick, int(lookback/barsize + 0.5), barsize)
    if (slp < slp_exit) or (slp < 0 and dx > dx_exit)  :
        return True

    return False
    
# here we enter if a short term trend is not too negative
# we define short term as 2 minutes
def ifEnterTrend(price, cur_tick, barsize = 5, slp_enter = 1e-7, lookback=120, dx_enter=3) :
    #mse, slp, b = getMSE(price[:,4], cur_tick+1-int(lookback/barsize), cur_tick+1)
    #if slp/barsize >= slp_enter :
    #    return True

    # try not to exit if 300 second trend is strong, helps some trade, but not sure if it makes sense

    slp, dx, mse = getTrend(price[:,4], cur_tick, int(lookback/barsize + 0.5), barsize)
    if slp > slp_enter and dx >= dx_enter :
        return True

    return False

def ifEnterSpeculation(price, cur_tick, barsize = 5, holdtime=900, slp_enter = 1e-6, lookback=600) :
    # collecting data for testing
    print 'if spec, data : ', price[cur_tick+1,0]

    #lookback = holdtime/3
    #slp1, mse1, slp2, seglen = collectData(price, curtick, lookback, barsize)
    #print slp1, mse1, slp2, seglen, 
    
    #lookback = holdtime/15
    #slp1, mse1, slp2, seglen = collectData(price, curtick, lookback, barsize)
    #print slp1, mse1, slp2, seglen, 

    #bars = int(lookback/barsize+0.5)
    #first_tick=cur_tick-bars+1
    #mse, slp, b = getMSE(price[:,4], int(first_tick), cur_tick+1)

    #slp /= barsize
    #if slp > slp_enter :
    #    print 'testing spec success', price[cur_tick,0], slp
    #    return True

    #print 'testing spec failed', price[cur_tick,0], slp 
    lookback = holdtime
    slp1, std1, detrended_variance_pct, last_seg_mse_pct, len3_pct, len_delta, rate_delta_main, rate_delta_prev, cur_val_pct = collectData(price, cur_tick, lookback, barsize)
    return True, [slp1, std1, detrended_variance_pct, last_seg_mse_pct, len3_pct, len_delta, rate_delta_main, rate_delta_prev, cur_val_pct]
    

# state: prev_state(0/1), prev_stoploss, prev_close, global_min_price, enter_tick
def updateStopLoss(price, cur_tick, states, holdtime = 900, barsize = 5, lookback = 300, slp_thresh=1e-6) :
    bars = int(lookback/barsize+0.5)
    first_tick=cur_tick-bars+1
    mse, slp, b = getMSE(price[:,4], int(first_tick), cur_tick+1)
    this_state = 0
    this_close = price[cur_tick,4];
    this_sl = 0

    if slp/barsize >= slp_thresh :
        this_state = 1

    strong_inc=0.5
    weak_inc=0.2
    weak_red=-0.2
    strong_red=-0.7

    if cur_tick - states[4] >= holdtime/barsize :
        strong_inc = 0.5
        weak_inc = 0.5
        weak_red = -0.7
        strong_red = -0.99

    # if both are going strong, increase risk based on delta
    # delta is the diff between this close and previous close
    if this_state == 1 :
        delta = strong_inc
        if states[0] == 0 :
            delta = weak_inc
        this_sl = states[1] + (this_close - states[2])*delta
        if this_close - this_sl < states[3] :
            this_sl = this_close - states[3]
     
    # if otherwise, reduce risk based on loss tolerance
    # loss tolerance is current close minus prev stop loss
    elif this_state == 0 :
        delta = weak_red
        if states[0] == 0 :
            delta = strong_red
        this_sl = this_close - (states[2] - states[1])
        this_sl += this_sl*delta

    states[0] = this_state 
    states[1] = this_sl
    states[2] = this_close

def barSample(data, bars) :
    darr = [] ;
    b = 0
    maxv=minv = data[0]
    for d in data :
        if int(b) % int(bars) == 0 :
            darr.append(d)
        if maxv < d :
            maxv = d;
        elif minv > d :
            minv = d
        b += 1
    return np.array(darr), maxv, minv

def collectData(price, curtick, lookback, barsize, samplebars=1) :
    # we need to 
    # print a slope and mse value 
    price_barsize = barsize
    bars = int(lookback/barsize+0.5)
    first_tick=curtick-bars+1

    thisPrice, maxv, minv = barSample(price[first_tick:curtick+1,4], samplebars)
    barsize *= samplebars
    bars = int(lookback/barsize+0.5)

    #mse, slp, b = getMSE(price[:,4], int(first_tick), curtick+1)
    mse, slp, b = getMSE(thisPrice, 0, bars)
    slp1 = slp/barsize
    mse1 = mse/bars
    std1 = np.std(thisPrice)

    ans=[]
    ans=getSegment(thisPrice, 3, ans)
    last_seg_mse_pct = ans[3,1]/ans[1,1]
    pt1 = ans[3,2] 
    pt2 = ans[3,3]
    pt3 = bars-1 

    len_delta = (pt2-pt1)/(pt3-pt2)
    len3_pct = (pt3-pt2)/float(bars)

    mse, rate1, b = getMSE(thisPrice, int(pt1), int(pt2+1))
    mse, rate2, b = getMSE(thisPrice, int(pt2), int(pt3+1))
    rate1 /= barsize
    rate2 /= barsize
    rate_delta_prev = rate2-rate1
    rate_delta_main = rate2-slp1

    cur_val_pred = rate2*(pt3+2-pt2)*barsize+b
    if maxv <= minv :
        cur_val_pct = 0.5
    else :
        cur_val_pct = (cur_val_pred - minv)/(maxv-minv)
    width1 = maxv-minv

    # todo make the mse1 as a ratio of std1
    detrended_variance_pct = mse1/(std1*std1)
    return slp1 * 1000000, std1 * 10000, detrended_variance_pct * 10, last_seg_mse_pct * 10, len3_pct * 10, len_delta, rate_delta_main * 1000000, rate_delta_prev * 1000000, cur_val_pct * 10

    # collect two additional feature - 2 times slope and pred_pct

    #lookback *=2
    #first_tick = curtick-int(lookback/price_barsize + 0.5) + 1
    #samplebars *= 2
    #thisPrice, maxv, minv = barSample(price[first_tick:curtick+1,4], samplebars)
    #barsize = price_barsize*samplebars
    #bars = int(lookback/barsize + 0.5)
    #mse, slp, b = getMSE(thisPrice, 0, bars)
    #cur_val_pred2 = slp*(bars+1)+b
    #slp2 = slp/barsize
    #slp_delta = slp1-slp2
    #if maxv <= minv :
    #   cur_val_pct2 = 0.5
    #else :
    #    cur_val_pct2 = (cur_val_pred2 - minv)/(maxv-minv)

    #width2 = maxv-minv

    #return slp1 * 1000000, std1 * 10000, detrended_variance_pct * 10, last_seg_mse_pct * 10, len3_pct * 10, len_delta, rate_delta_main * 1000000, rate_delta_prev * 1000000, cur_val_pct * 10, rate2*1000000, slp2*1000000, slp_delta*1000000, cur_val_pct2*10, width1/std1, width2/std1

# return an array of 3 column
# timestamp, ifspec, signal_stength (#signals within)
# for each original buy signal given in 'buy', 
# generates 1 buy signal plus continuous spec signals 
# at an interval of 1/3 of holdtime, after the original holdtime
# until the covertime expires
def processBuyTime(buy, sampleInterval, holdtime, coverTime) :
    newbuy=[]
    idx = 0 ;
    total_buys = buy.shape[0]

    while idx < total_buys :
        sig_start = buy[idx]
        # find out the signal strength of this one
        sig_end = sig_start + coverTime
        idx += 1
        sig_cnt = 1
        while idx < total_buys and buy[idx] < sig_end :
            sig_cnt += 1
            idx += 1
        
        newbuy.append([sig_start, 0, sig_cnt, 0])
        #I need more samples here
        sig_start += holdtime
        #sig_start += sampleInterval
        if idx >= total_buys :
            break;

        idx2 = 1
        while sig_start < sig_end :
            newbuy.append([sig_start, 1, sig_cnt, idx2])
            sig_start += sampleInterval
            idx2 += 1

    return np.array(newbuy)

def labelBuy(price, barsize, curtick, holdtime, stoploss) :
    enter_time = price[curtick, 0]
    enter_price = price[curtick,4]
    endtick = curtick + holdtime/barsize
    curtick += 1
    while curtick < endtick :
        if price[curtick,3] < enter_price - stoploss :
            return price[curtick,3] - enter_price

        cur_price = price[curtick,4]
        curtick+=1

    return cur_price - enter_price
     
def collectSpecFeature(price, barsize,buyTime, holdTime, stoploss) :
    newbuy = processBuyTime(buyTime, holdTime/3, holdTime, holdTime*3)
    buy_idx = 0;
    buy_ts = newbuy[0,0];
    total_buys = newbuy.shape[0]

    pnl1 = 0
    pnl2 = 0
    this_pnl = 0
    spec_data=[]
    prev_pnl = 0
    # seek signal time to be after our first tick
    while buy_ts <= price[0,0] :
        buy_idx += 1
        if buy_idx >= total_buys :
            print 'signal time is later than tick time, exiting...'
            return
        
        buy_ts = newbuy[buy_idx,0]

    while buy_idx < total_buys :
        buy_ts = newbuy[buy_idx,0]
        curtick = np.where(price[:,0]==buy_ts)
        if np.size(curtick) != 1 :
            buy_idx += 1
            buy_ts = newbuy[buy_idx,0]
            continue
        curtick = curtick[0][0]

        if newbuy[buy_idx,1] == 0 :
            this_pnl = labelBuy(price, barsize, curtick, holdTime, stoploss)
            pnl1 += this_pnl
        else :
            # spec trade
            sample1 = collectData(price, curtick, holdTime, barsize, 1)
            sample2 = collectData(price, curtick, 3*holdTime, barsize, 3)
            samples = []
            # look ahead here
            prev_pnl2 = this_pnl  
            prev_pnl = labelBuy(price, barsize, curtick-int(holdTime/barsize + 0.5), holdTime, stoploss)

            this_pnl = labelBuy(price, barsize, curtick, holdTime, stoploss)
            label = 0;
            if this_pnl > 0.0005 :
                label = 1

            samples.append(int(label))
            # for debug
            samples.append(buy_ts)
            samples.append(this_pnl)
            samples.append(prev_pnl2 * 10000)

            samples.append(time.gmtime(buy_ts).tm_hour)  
            samples.append(prev_pnl * 10000)
            samples.append(newbuy[buy_idx,2]) 
            samples.append(newbuy[buy_idx,3])
            for s in sample1 :
                samples.append(s)
            for s in sample2 :
                samples.append(s)
            
            pnl2 += this_pnl
            spec_data.append(samples)

        buy_idx += 1
 
    
    return pnl1, pnl2, np.array(spec_data)

# price are bars, with ts, open, max, min, close.
# ts is the open ts
# 4 states: 
# 0:idle, 
# 1:entry, 
# 2:holding
# 3:exit
# btime is the time of the signal
# return total pnl and trades
# trades has enter_ts, enter_price, exit_ts, exit_price, pnl
def getPNL(price, btime, defaultHoldTime, defaultStoploss, ifPlot=False, ifUseBuyTime=False, barsize = 5, defaultUpdateFreq1=99999, defaultUpdateFreq2=99999) :
    state=0
    ticks = price.shape[0]
    curtick = 0;
    cur_ts = price[curtick+1,0]
    next_buy_idx = 0;
    next_buy_ts = btime[0];
    total_buys = btime.shape[0]
    enter_time = 0
    enter_price = 0;
    pnl = 0
    trades = []
    this_pnl = 0

    spec_data=[]

    # seek signal time to be after our first tick
    while next_buy_ts <= cur_ts :
        next_buy_idx += 1
        if next_buy_idx >= total_buys :
            print 'signal time is later than tick time, exiting...'
            return
        
        next_buy_ts = btime[next_buy_idx]

    tradeMode = 0
    samples = []

    while curtick < ticks - 1 :
        cur_ts = price[curtick + 1,0]
        cur_bar = price[curtick];

        if state == 0:
            if tradeMode == 1:
                tradeMode = 0;
                if this_pnl > 0.0005 :
                    this_sample=[int(1)]
                else :
                    this_sample=[int(0)]
                for i in sample :
                    this_sample.append(i)
                for i in d :
                    this_sample.append(i)

                samples.append(this_sample)

            # trying to enter
            if cur_ts >= next_buy_ts :
                #trade mode is 0: normal, 1: spec, 2: no trade (skip)
                tradeMode = 0

                # need to adjust the curtick, cur_ts and cur_bar
                if cur_ts > next_buy_ts :
                    #if ifUseBuyTime:
                        # this breaks
                        #curtick = np.where(price[:,0] == next_buy_ts)[0][0] - 1;
                        #cur_ts = next_buy_ts
                        #cur_bar = price[curtick]
 
                    # we have a signal during the trade, see if we should run a speculative trade on this signal
                    sample = [this_pnl * 10000, (cur_ts-next_buy_ts)/100]
                    ifEnter, d = ifEnterSpeculation(price, curtick, barsize)
                    if ifEnter :
                        tradeMode = 1
                        #print this_pnl, cur_ts - next_buy_ts
                        print 'Spec Trade on previous signal', cur_ts, next_buy_ts
                    else :
                        tradeMode = 2
                        print 'skipped previous signal', cur_ts, next_buy_ts

                # entering 
                if tradeMode == 0 :
                    print 'entering normal trade'
                    state = 1
                    signal_time = cur_ts
                    stoploss = defaultStoploss
                    holdtime = defaultHoldTime
                    updateFreq1 = defaultUpdateFreq1
                    updateFreq2 = defaultUpdateFreq2

                elif tradeMode == 1 :
                    print 'entering spec trade'
                    state = 1
                    signal_time = cur_ts
                    #stoploss = max(stoploss - 0.0002, 0.0005)
                    stoploss = defaultStoploss 
                    holdtime = defaultHoldTime
                    updateFreq1 = defaultUpdateFreq1 - 50
                    updateFreq2 = defaultUpdateFreq2 - 50
 
                else :
                    print 'skipping signal at ', next_buy_ts
                    state = 0

                next_buy_idx += 1
                if next_buy_idx >= total_buys :
                    break;
                else :
                    next_buy_ts = btime[next_buy_idx]
  
            else :
                curtick += 1
            continue;

        elif state == 1:
            if cur_ts - signal_time >= holdtime :
                state = 0;
            elif ifEnterTrend(price, curtick, barsize) == True :
                state = 2
                enter_price = cur_bar[4]; # use close price of cur bar
                ref_price = enter_price
                enter_time = cur_ts
                states = np.array([1, stoploss, enter_price, enter_price-stoploss, curtick])

            curtick += 1
            continue;

        elif state == 2:
            if cur_ts - enter_time >= holdtime :
                state = 3;
                continue;
            elif ref_price - cur_bar[3] >= stoploss :
                state = 3;
                continue;
            elif int(cur_ts - enter_time) % int(updateFreq1) == 0 :
                updateStopLoss(price, curtick, states, holdtime, barsize) 
                stoploss = states[1]
                ref_price = states[2]

            curtick += 1
            continue;

        elif state == 3:
            if int(cur_ts - enter_time) % int(updateFreq2) == 0 :
                updateStopLoss(price, curtick, states, holdtime, barsize)
                stoploss = states[1]
                ref_price = states[2]

            if ref_price - cur_bar[3] >= stoploss :
                this_pnl = cur_bar[3] - enter_price
                trades.append([enter_time, enter_price, cur_ts, cur_bar[3], this_pnl])
                pnl += this_pnl
                state = 0;
                print 'stop loss', cur_ts, btime[next_buy_idx-1], this_pnl
                print '*** current pnl is ', pnl
                continue;

            if int(cur_ts - enter_time) % int(updateFreq2) == 0 :
                updateStopLoss(price, curtick, states, holdtime, barsize)
                stoploss = states[1]
                ref_price = states[2]

            if ifExitTrend(price, curtick) == True :
                this_pnl = (cur_bar[4] - enter_price)
                trades.append([enter_time, enter_price, cur_ts, cur_bar[4], this_pnl])
                pnl += this_pnl
                state = 0
                print 'exit', cur_ts, this_pnl, enter_time, enter_price, cur_ts, cur_bar[4]
                print '*** current pnl is ', pnl
                continue

            curtick += 1
    
        else :
            print "error in trading state"

    trades=np.array(trades)
    if ifPlot == True :
        fig = pl.figure()
        ax = fig.add_subplot(1,1,1)
        ax.plot(price[:,0], price[:,1], 'b.-', markersize=8, label='5S bar')
        ax.plot(trades[:,0], trades[:,1], 'rx', markersize=12, label='entrace')
        ax.plot(trades[:,2], trades[:,3], 'ro', markersize=12, label='exit')
        ax.legend(loc='best')

    return pnl, np.array(trades), np.array(samples)

def getParam(price, btime, holdtime, stoploss):
    pnl = []
    trade_arr = []
    sample_arr = []
    for t in holdtime :
        for l in stoploss : 
            print '-------------------'
            print 'running for ', t, l
            print '-------------------'

            p, trades, samples = getPNL(price, btime, t, l)
            pnl.append([t, l, p])
            trade_arr.append([t, l, trades])
            sample_arr.append([t, l, samples])

            np.savetxt('np'+'_'+str(int(t))+'_'+str(int(l*100000)), pnl)
            np.savetxt('trades'+'_'+str(int(t))+'_'+str(int(l*100000)), trades)
            np.savetxt('samples'+'_'+str(int(t))+'_'+str(int(l*100000)), samples)

    return np.array(pnl), trade_arr, sample_arr

from matplotlib import cm
from mpl_toolkits.mplot3d import Axes3D

def plotParm3d(X,Y,pnl,xlabel, ylabel, zlabel, title) :
    X, Y = np.meshgrid(Y,X)
    Z = pnl[:,2].reshape(X.shape)

    fig = pl.figure()
    ax = fig.add_subplot(1,1,1,projection='3d')
    surf = ax.plot_surface(X, Y, Z, rstride=1, cstride=1, cmap=cm.coolwarm, linewidth=0, antialiased=False)
    fig.colorbar(surf, shrink=0.5, aspect=10)
    ax.set_xlabel(ylabel)
    ax.set_ylabel(xlabel)
    ax.set_zlabel(zlabel)
    ax.set_title(title)

def normalizeSample(sam, fname) :
    sam1 = np.hstack([sam[:,0:1], sam[:,2:]])
    sam1[:,1] *= 10000
    sam1[:,2] /=100
    sam1[:,3] *= 1000000
    sam1[:,4] *= 10000
    sam1[:,5] *= 10 
    sam1[:,6] *= 10 
    sam1[:,7] *= 10 
    sam1[:,9] *= 1000000
    sam1[:,10] *= 1000000
    sam1[:,11] *= 10
    np.savetxt(fname, sam1, '%i %f %f %f %f %f %f %f %f %f %f %f', delimiter='	')
    return sam1

def loadData() :
    buy = np.loadtxt('/home/zfu/fxstm/fxstms.v20140214/buy.csv')
    p1 = np.loadtxt('/home/zfu/test/data/market_data/IB/FX/5S_Bar/5S_20130701_20140107_EURUSD')
    p2 = np.loadtxt('/home/zfu/test/data/market_data/IB/FX/5S_Bar/5S_20140106_20140131_EURUSD')
    p = np.vstack([p1,p2[86580:]])
    return p, buy

def isTradingTime(ts) :
    tm = time.localtime(ts)
    return (tm.tm_wday <4) or (tm.tm_wday == 4 and tm.tm_hour < 17) or (tm.tm_wday == 6 and tm.tm_hour > 17) ;

### OK, it looks like I have slight edge on the spec trade with
### the current feature set.  Some improvements can come from
### more samples (sample all the time, not only after signal, 
### not make the signal trigger more). 
### I need to better understand the maxent/logistic code and 
### write a C++ code for label and feature extraction for reasons
### of speed and reusability. 
### On the other hand, get to the CCY thing now, targeting for a
### new feature

### this will fill in any missing bars, starting from start_ts
### until end_ts, all inclusive. 
### it skips the week end ts
def reSampleBarData(bar, barsize, start_ts, end_ts) :
    idx = 0;
    barcnt = bar.shape[0]
    while idx < barcnt :
        if bar[idx][0] < start_ts :
            idx += 1;
        else :
            break;

    b = [];
    next_ts = start_ts
    prev_close = bar[idx][1];  # use this open as previous close
    maxv = prev_close
    minv = prev_close
    while idx < barcnt and bar[idx][0] <= end_ts :
        this_bar = bar[idx];
        while this_bar[0] > next_ts :
            if isTradingTime(next_ts) :
                b.append([next_ts, prev_close, maxv, minv, prev_close]);
                maxv = prev_close;
                minv = prev_close;
            next_ts += barsize;
        if this_bar[0] == next_ts :
            if isTradingTime(next_ts) :
                b.append([next_ts, prev_close, maxv, minv, this_bar[4]])
                prev_close = this_bar[4];
                maxv = prev_close
                minv = prev_close
            next_ts += barsize
        else :
            if maxv < this_bar[2] :
                maxv = this_bar[2] ;
            if minv > this_bar[3] :
                minv = this_bar[3]

        idx += 1;

    # append bars if end_ts is not reached
    if idx >= barcnt :
        while next_ts <= end_ts :
            if isTradingTime(next_ts) :
                b.append([next_ts, prev_close, prev_close, prev_close, prev_close])
            next_ts += barsize
    
    b = np.array(b)
    b[:,0] += barsize;  ## since we are using the close price of the bar
    return b

import sys

## 
def getHDBar(fname_pref, start_ts, end_ts, barsize, cp_exclude) :
    cp_list = [];
    cpbar = [];
    total_bars = (end_ts - start_ts)/barsize + 1

    tslist = [];

    for s in symbols:
        # skip the cp in exclude list
        if len(np.where(cp_exclude == s)[0]) == 1 :
            continue;

        try :
            bars = np.loadtxt(fname_pref+s) ;

            print 'read ' + s
            if bars.shape[0] < total_bars*0.5 :
                print s + ' has too few bars, skipping'
                continue;
            cp_list.append(s);
            data = reSampleBarData(bars, barsize, start_ts, end_ts)
            cpbar.append(data[:,4])
            tslist.append(data[:,0])
        except:
            print s + ' has no data found, skipping', sys.exc_info()

    return np.array(cpbar), getCPnumber(np.array(cp_list)), np.array(tslist);

def plotCCYTrade(ts, hd, ccy_cum, buy, cp_number) :
    fig = pl.figure()
    ax = fig.add_subplot(1,1,1)
    ax.plot(ts, hd[cp_number, :],'b.')
    ax2 = ax.twinx()
    c1 = ccy1n[cp_number]
    c2 = ccy2n[cp_number]

    ax2.plot(ts, ccy_cum[:,c1], 'r.', label = ccy1[cp_number])
    ax2.plot(ts, ccy_cum[:,c2], 'g.', label = ccy2[cp_number])

    ax2.legend(loc='best')
    
    ax2.plot(buy[:], np.ones(len(buy)), 'x')

    
