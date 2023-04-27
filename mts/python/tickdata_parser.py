import numpy as np
import datetime
import os
import mts_util
import copy
import traceback

def get_trade_tickdata(file_name, time_zone='US/Eastern', px_multiplier=1.0) :
    """
    The trade file has the following format
    01/31/2021,18:00:04.281,51.89,1,E,0,,51.89
    in the format of
    date, time, filtered_px, volume, flag, condition, exclude, raw_px

    We are going to use the raw_px instead of filterd_px
    where flag = 'E'
    return array of [utc_milli, trd_px, trd_sz]

    Note, the last 4 columns only available after 7/1/2011, so use the 
    filtered_px if needed.
    """
    try :
        do_gz = False
        if file_name[-3:] == '.gz' :
            os.system('gunzip -f ' + file_name)
            file_name = file_name[:-3]
            do_gz = True

        d = np.genfromtxt(file_name, delimiter=',', dtype='|S64')
        n,m = d.shape

        dflag = d[:,4].astype(str)
        ix = np.nonzero(dflag == 'E')[0]
        if len(ix) > 0 :
            d = d[ix,:]
        sz = d[:,3].astype(int)

        pxcol = 7 if m > 7 else 2
        px = d[:,pxcol].astype(float)*px_multiplier

        dt0 = ''
        utcs0 = 0.0

        utc=[]
        for d0 in d:
            dts=d0[0].decode() + '-' + d0[1].decode()
            if '.' not in dts: 
                dts += '.000'

            dts0 = dts[:-10]
            sec0 = int(dts[-9:-7])*60 + int(dts[-6:-4])
            sec0 += float(dts[-3:])/1000.0
            if dt0 != dts0 :
                dt = datetime.datetime.strptime(dts0+":00:00.000", '%m/%d/%Y-%H:%M:%S.%f')
                utcs0 = mts_util.TradingDayUtil.dt_to_utc(dt, time_zone)
                dt0 = dts0
            utcs = utcs0 + sec0
            utc.append(utcs)

        trd = np.vstack(((np.array(utc)*1000).astype(int), px, sz)).T

        """
        if do_gz :
            os.system('gzip ' + file_name)
        """
        return trd
    except :
        print ('problem getting trade from ',file_name)
        #traceback.print_exc()
        return None

def get_quote_tickdata(file_name, time_zone='US/Eastern', px_multiplier=1.0) :
    """
    The quote file has the following format
    02/17/2021,18:00:00.000,61.71,13,61.71,11,E,
    in the format of 
    date, time, bp, bsz, ap, asz, flag, condition

    We are going to read only flag = 'E'
    return array of [utc_milli, bp, bsz, ap, asz]
    """
    try :
        do_gz = False
        if file_name[-3:] == '.gz' :
            os.system('gunzip -f ' + file_name)
            file_name = file_name[:-3]
            do_gz = True

        d = np.genfromtxt(file_name, delimiter=',', dtype='|S64')
        dflag = d[:,-2].astype(str)
        ix = np.nonzero(dflag=='E')[0]
        d = d[ix,:]

        # check bp/ap emtpy
        for c in [2,3,4,5] :
            ix = np.nonzero(d[:,c]!=b'')[0]
            d = d[ix,:]

        bp = d[:,2].astype(float)*px_multiplier
        bsz = d[:,3].astype(int)
        ap = d[:,4].astype(float)*px_multiplier
        asz = d[:,5].astype(int)

        dt0 = ''
        utcs0 = 0.0
        
        utc=[]
        for d0 in d:
            dts=d0[0].decode() + '-' + d0[1].decode()

            dts0 = dts[:-10]
            sec0 = int(dts[-9:-7])*60 + int(dts[-6:-4])
            sec0 += float(dts[-3:])/1000.0
            if dt0 != dts0 :
                dt = datetime.datetime.strptime(dts0+":00:00.000", '%m/%d/%Y-%H:%M:%S.%f')
                utcs0 = mts_util.TradingDayUtil.dt_to_utc(dt, time_zone)
                dt0 = dts0
            utcs = utcs0 + sec0
            utc.append(utcs)

        quote = np.vstack(( (np.array(utc)*1000).astype(int), bp, bsz, ap, asz)).T

        """
        if do_gz :
            os.system('gzip ' + file_name)
        """
        return quote
    except :
        print ('problem getting quote from ', file_name)
        #traceback.print_exc()
        return None

def _quote_from_trade(trd_bar) :
    """
    this is for the old days where only trades are avaliable
    trd_bar: [utc, px, sz]
    """
    mid_px = trd_bar[:,1]
    tick_sz = mid_px.mean()*0.0001
    n = len(mid_px)
    return np.vstack((trd_bar[:,0], mid_px-tick_sz, np.ones(n), mid_px+tick_sz, np.ones(n))).T

def sample_OHLC(midpx, midutc, butc) :
    """
    both midutc and butc are integer and are in same unit, i.e. milliseconds
    butc should include the start of first bar, i.e. len(butc) = bars + 1
    return [butc, open, high, low, close]
    """
    mp = midpx.copy()
    mutc = midutc.copy()
    rmidx = np.nonzero(np.abs(mp[1:]-mp[:-1])<1e-10)[0]
    if rmidx.size > 0 :
        rmidx += 1;
        mp = np.delete(mp, rmidx)
        mutc = np.delete(mutc, rmidx)

    nbars = len(butc) - 1
    qix = np.clip(np.searchsorted(mutc,butc+0.001)-1,0,1e+10).astype(int)
    ticks = qix[1:] - qix[:-1] + 1
    mt = np.max(ticks)
    px = np.tile(mp[qix[:-1]],(mt,1)).T
    px[:,-1] = mp[qix[1:]]
    for ix in np.arange(mt-2)+1 :
        p = mp[np.clip(qix[:-1]+ix,0,qix[1:])]
        px[:,ix]=p
    return np.vstack((px[:,0],np.max(px,axis=1),np.min(px,axis=1),px[:,-1])).T

def sample_bbo(quote, butc, trade=None) :
    """
    quote is returned from get_quote_tickdata(), shape [nticks, 5], 
    where ts is first column, as int of 1000*utc
    butc also in milli-seconds, length [nbars + 1] vector, as the time of 
         start of the first bar to the end of last bar, i.e. len(butc) = bars + 1
    if trade is not none, the bid/ask diffs are removed with the trade quantity.
    """

    # remove crossed or zero size quote
    ixg = np.nonzero(quote[:,3]-quote[:,1]>1e-10)[0]
    quote0 = quote[ixg,:]
    ixg = np.nonzero(np.abs(quote0[:,2]*quote0[:,4])>1e-10)[0]
    quote0 = quote0[ixg,:]
    ts, bp, bsz, ap, asz = quote0.T

    bdif = np.r_[0, bsz[1:] - bsz[:-1]]
    adif = np.r_[0, asz[1:] - asz[:-1]]

    # accounting for the trades, note this is the best
    # effort, as the time is in milliseconds
    if trade is not None:
        tst, pxt, szt = trade.copy().T
        tix = np.clip(np.searchsorted(ts, tst, side='left'),0, len(ts)-1).astype(int)

        # demand exact match on tix
        tix0 = np.nonzero(np.abs(ts[tix] - tst) > 0.1)[0]
        tix = np.delete(tix, tix0)
        pxt = np.delete(pxt, tix0)
        szt = np.delete(szt, tix0)

        bix = np.nonzero(np.abs(bp[tix]-pxt)<1e-10)[0]
        bdif[tix[bix]] += szt[bix]
        aix = np.nonzero(np.abs(ap[tix]-pxt)<1e-10)[0]
        adif[tix[aix]] += szt[aix]

    # nzix is index into the new ticks with a different mid px
    bnzix = np.nonzero( np.abs(bp[1:] - bp[:-1]) > 1e-10 )[0] + 1
    anzix = np.nonzero( np.abs(ap[1:] - ap[:-1]) > 1e-10 )[0] + 1

    # total bsz change and asz change in the bar
    bdif[bnzix] = 0
    adif[anzix] = 0

    qix = np.clip(np.searchsorted(ts,butc+0.1)-1,0,1e+10).astype(int)
    vdifc = np.cumsum(np.vstack((bdif, adif)), axis=1)
    vdifx = vdifc[:,qix[1:]] - vdifc[:,qix[:-1]]

    # bsz, asz, spd, need time weighted avg. 

    # time weighted avg is more complicated, it has to
    # observe boundary of a bar, even when there were no
    # changes upon them. The following adds "artificial"
    # ticks on bar time, so to allow such calclation

    spd = ap - bp

    # time weighted avg of book pressure and spread
    val = np.vstack((bsz, asz, spd))

    # add bar open/closing times to the ts, if not there yet
    tsix = np.nonzero(np.abs(butc - ts[qix]) > 1e-8)[0]
    butc_to_add = butc[tsix]
    qtix = qix[tsix]  # the value at butc[tsix]

    ts0 = np.r_[ts, butc_to_add]
    val = np.hstack((val, val[:, qtix]))
    # sort
    ix0 = np.argsort(ts0, kind='stable')
    ts0 = ts0[ix0]
    val = val[:,ix0]

    # redo the ts
    qix0 = np.clip(np.searchsorted(ts0.astype(int),butc.astype(int), side='right')-1,0,1e+10).astype(int)

    dt = ts0[1:] - ts0[:-1]
    valc = np.hstack((np.zeros((val.shape[0],1)), np.cumsum(val[:,:-1]*dt, axis=1)))
    vx = (valc[:,qix0[1:]] - valc[:,qix0[:-1]])/(butc[1:]-butc[:-1])

    col_name = ['avg_bsz', 'avg_asz', 'avg_spd', 'tot_bsz_dif', 'tot_asz_diff']
    return np.vstack((vx, vdifx)).T, col_name

def daily_mts_bar(trd0, quote0, barsec, start_utc, bar_cnt, extended_fields = False) :
    """
    write a daily bar with the trd and quote returned from quote and trade file
    barsec: bar period
    start_utc: starting time of the quote, bar_first_utc is start_utc plus barsec
    bar_cnt: the number of bars to be written

    return: 2-d array with each row being a bar line and columns as:
    BarTime: the utc of the bar generation time
    Open/High/Close/Low: OHCL
    TotalVolume: total trading volume
    LastPrice: the latest trade price seen so far
    LastPriceTime: the time of latest trade, with precision given
    VolumeImbalance: Buy trade size minus Sell trade size within this bar
    """

    if trd0 is None and quote0 is None :
        raise RuntimeError("trade and quote not found!")
    if quote0 is None :
        quote0 = _quote_from_trade(trd0)

    trd = trd0.copy()
    quote = quote0.copy()

    # remove all the crossed ticks
    crix = np.nonzero(quote[:,3]-quote[:,1]>1e-8)[0]
    if len(crix) > 0 :
        quote = quote[crix,:]

    bar_first_utc = start_utc + barsec
    butc = np.arange(bar_cnt)*barsec+bar_first_utc

    # get OHLC
    bp = quote[:,1]
    ap = quote[:,3]
    ohlc = sample_OHLC((bp+ap)/2, quote[:,0], np.r_[start_utc,butc]*1000)

    # match trades with quote
    # find trade sign by matching trades with quotes

    # Since tickdata has 100 millisecond conflated, it is
    # therefore approximation. We look at the previous bpx/apx
    # of the matching time stamp of trade time onto quote.
    # if the trade price in doesn't touch any bpx/apx, look at
    # previous different bpx/apx to make desicion
    tix = np.clip(np.searchsorted(quote[:,0], trd[:,0], side='left') -2 ,0,quote.shape[0]-1).astype(int)

    # make sure the tpx equals either bpx/apx
    bpx = quote[tix, 1]
    apx = quote[tix, 3]
    tpx = trd[:,1]
    nzix = np.nonzero(np.min(np.abs(np.vstack((bpx,apx))-tpx),axis=0)>1e-8)[0]
    mpx = (quote[:,1] + quote[:,3])/2
    max_cnt = 100
    cnt = 0
    while len(nzix) > 0 and cnt < max_cnt:
        mpx0 = mpx[tix[nzix]]
        tix[nzix] = np.clip(tix[nzix]-1,0,1e+10)
        mpx1 = mpx[tix[nzix]]
        nzix_ix = np.nonzero(np.abs(mpx0-mpx1)>1e-8)[0]
        if len(nzix_ix) == 0 or np.max(nzix_ix) < 1:
            break
        nzix = nzix[nzix_ix]
        cnt += 1

    tpx = trd[:,1].copy()
    tsz = trd[:,2].copy()
    sellix = np.nonzero(np.abs(tpx-quote[tix, 1]) < np.abs(tpx-quote[tix,3]))[0]
    tsz[sellix]=tsz[sellix]*-1

    #tix matches latest trade on the bar second
    tix = np.clip(np.searchsorted(trd[:,0],butc*1000-0.001)-1,0,1e+10).astype(int)
    last_px = tpx[tix]
    last_px_time = trd[tix,0]
    volc = np.cumsum(np.abs(tsz))[tix]
    vbsc = np.cumsum(tsz)[tix]
    tvol = volc-np.r_[0, volc[:-1]]
    tvbs = vbsc-np.r_[0, vbsc[:-1]]

    ixname = {'BarTime':0, 'Open':1, 'High':2, 'Low':3, 'Close':4, 'TotalVolume':5, 'LastPx':6, 'LastPxTime':7, 'VolumImbalance':8}
    flds = np.vstack((butc, ohlc.T, tvol, last_px, last_px_time, tvbs)).T

    if extended_fields:
        #efld, colname = sample_bbo(quote0.copy(), np.r_[start_utc,butc].astype(int)*1000)
        efld, colname = sample_bbo(quote0.copy(), np.r_[start_utc,butc].astype(int)*1000, trade=trd0.copy())
        next_field = 9
        for i, col in enumerate(colname) :
            ixname[col] = next_field + i
        flds = np.hstack((flds, efld))

    return flds, ixname

def mergeBar(MtsBar, barsec) :
    """
    MtsBar shape supposed to be 1-second bars with shape [nsecs, mcols]
    base_columes: {'BarTime':0, 'Open':1, 'High':2, 'Low':3, 'Close':4, 'TotalVolume':5, 'LastPx':6, 'LastPxTime':7, 'VolumImbalance':8}
    ext_colums: 'avg_bsz', 'avg_asz', 'avg_spd', 'tot_bsz_dif', 'tot_asz_diff'
    """
    n,m = MtsBar.shape
    assert(n//barsec*barsec==n), 'barsec not a multiple of total bars'
    assert(MtsBar[-1,0]-MtsBar[0,0]==n-1), 'MtsBar not in 1-second period'
    ix = np.arange(0,n,barsec)
    ## getting the ohlc
    oix = np.arange(0,n,barsec)
    cix = np.r_[oix[1:]-1,n-1]
    bt=MtsBar[cix,0]
    o=MtsBar[oix,1]
    h=np.max(MtsBar[:,2].reshape(n//barsec,barsec),axis=1)
    l=np.min(MtsBar[:,3].reshape(n//barsec,barsec),axis=1)
    c=MtsBar[cix,4]

    tvc=np.cumsum(MtsBar[:,5])
    tv=tvc[cix]-np.r_[0,tvc[cix[:-1]]]
    lpx=MtsBar[cix,6]
    lpxt=MtsBar[cix,7]
    tvbc=np.cumsum(MtsBar[:,8])
    tvb=tvbc[cix]-np.r_[0,tvbc[cix[:-1]]]

    flds = np.vstack((bt,o,h,l,c,tv,lpx,lpxt,tvb)).T
    m_base = flds.shape[1]
    if m > m_base :
        # extended
        # 'avg_bsz', 'avg_asz', 'avg_spd', 'tot_bsz_dif', 'tot_asz_diff'
        m_ext = 5
        assert m == m_base + m_ext, 'unknown field number'
        eflds = MtsBar[:,m_base:m_base+m_ext].T.reshape((m_ext,n//barsec,barsec))
        # get avg for the first 3 fields
        eflds0 = np.mean(eflds[:3,:,:],axis=2)
        # get sum for the tot_diff
        eflds1 = np.sum(eflds[3:,:,:],axis=2)
        flds = np.hstack((flds,eflds0.T,eflds1.T))

    return flds

def saveCSV(bar, file_name, do_gzip = True):
    assert (len(bar.shape)==2), 'bar needs to be a 2-dimensional array'
    assert bar.shape[1] in [9, 14], 'unknown bar shape!'
    fmt = ['%d','%.8g','%.8g','%.8g','%.8g','%d','%.8g','%d','%d']
    if bar.shape[1] == 14:
        fmt += ['%.1g', '%.1g', '%.8f', '%d','%d']
    np.savetxt(file_name, bar, delimiter = ',', fmt = fmt)
    if do_gzip :
        os.system('gzip -f ' + file_name)

cme_month_code = ['F', 'G', 'H', 'J', 'K', 'M', 'N', 'Q', 'U', 'V', 'X', 'Z']

class TickdataMap :
    def __init__(self, symbol_map_obj) :
        self.symbol_map = symbol_map_obj

    def get_tickdata_file(self, mts_symbol_no_N, contract_ym, day_ymd, add_prev_day=False) :

        # figure out the tickdata symbols
        tmap = self.symbol_map.get_tradable_map(day_ymd, mts_key = True, mts_symbols = [ mts_symbol_no_N ], add_prev_day=add_prev_day)
        found = False
        tdsym = None
        for k in tmap.keys() :
            if tmap[k]["symbol"] == mts_symbol_no_N :
                tdsym = tmap[k]["tickdata_id"]
                tzone = tmap[k]["tickdata_timezone"]
                pxmul = float(tmap[k]["tickdata_px_multiplier"])
                if tmap[k]["contract_month"] == contract_ym :
                    found = True
                    break
        if tdsym is None :
            raise RuntimeError(mts_symbol_no_N + " not defined for contract month " + contract_ym + " on the day of " + day_ymd)

        # there could be a contract that is N6 or N12, which is 
        # more than symbol_map's maxN, typically 2. In this case,
        # construct a tickdata_id by 
        # asset code + month_code + yy
        # timezone and pxmul are defined at asset level
        if not found:
            contract_month = int(contract_ym[-2:])
            contract_yy = contract_ym[2:4]
            tdsym = tdsym[:-3] + cme_month_code[contract_month-1] + contract_yy
        day_append = '_' + day_ymd[:4] + '_' + day_ymd[4:6] + '_' + day_ymd[6:8]
        qfile = tdsym + day_append + '_Q.asc.gz'
        tfile = tdsym + day_append + '.asc.gz'
        return qfile, tfile, tzone, pxmul

    def get_td_monthly_file(self, td_symbol, month_ym, tickdata_future_path, extract_to_path = None) :
        path = os.path.join(tickdata_future_path, td_symbol[0], td_symbol)
        tfname = month_ym[:4]+'_'+month_ym[4:]+'_'+td_symbol
        qfname = tfname + '_Q'

        tfile = os.path.join(path, tfname)
        qfile = os.path.join(path, 'QUOTES', qfname)

        if extract_to_path is not None :
            # do the extraction
            qpath = os.path.join(extract_to_path, 'quote')
            tpath = os.path.join(extract_to_path, 'trade')
            for p, f in zip ([qpath, tpath], [qfile, tfile]) :
                os.system('rm -fR ' + p + ' > /dev/null 2>&1')
                os.system('mkdir -p ' + p + ' > /dev/null 2>&1')
                os.system('unzip -o ' + f + ' -d ' + p)

        return qfile, tfile

    def _get_td_by_mts(self, mts_symbol_no_N, day_ymd) :
        tmap = self.symbol_map.get_tradable_map(day_ymd, mts_key = True, mts_symbols = [ mts_symbol_no_N ])
        for k in tmap.keys() :
            if tmap[k]["symbol"] == mts_symbol_no_N :
                tdsym = tmap[k]["tickdata_id"]
                return tdsym[:-3]
        raise RuntimeError(mts_symbol_no_N + " not recognized from symbol map ")

    def get_td_by_mts(self, mts_symbol_no_N) :
        try :
            return self._get_td_by_mts(mts_symbol_no_N, '20210104')
        except :
            pass
        return self._get_td_by_mts(mts_symbol_no_N, '20210201')

