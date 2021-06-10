import numpy as np
import datetime
import os
import mts_util
import copy

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

    do_gz = False
    if file_name[-3:] == '.gz' :
        os.system('gunzip -f ' + file_name)
        file_name = file_name[:-3]
        do_gz = True

    d = np.genfromtxt(file_name, delimiter=',', dtype='|S64')
    n,m = d.shape

    dflag = d[:,4].astype(str)
    ix = np.nonzero(dflag=='E')[0]
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

def get_quote_tickdata(file_name, time_zone='US/Eastern', px_multiplier=1.0) :
    """
    The quote file has the following format
    02/17/2021,18:00:00.000,61.71,13,61.71,11,E,
    in the format of 
    date, time, bp, bsz, ap, asz, flag, condition

    We are going to read only flag = 'E'
    return array of [utc_milli, bp, bsz, ap, asz]
    """

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


def daily_mts_bar(trd0, quote0, barsec, start_utc, bar_cnt) :
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

    trd = trd0.copy()
    quote = quote0.copy()
    bar_first_utc = start_utc + barsec
    butc = np.arange(bar_cnt)*barsec+bar_first_utc

    # get OHLC
    bp = quote[:,1]
    ap = quote[:,3]
    ohlc = sample_OHLC((bp+ap)/2, quote[:,0], np.r_[start_utc,butc]*1000)

    # match trades with quote
    # find trade sign by matching trades with quotes
    tix = np.clip(np.searchsorted(quote[:,0], trd[:,0], side='left'),0,quote.shape[0]-1).astype(int)

    # adjust on mismatched ix to be the previous quote
    nzix = np.nonzero(quote[tix,0]!=trd[:,0])[0]
    if len(nzix) > 0 :
        tix[nzix] = np.clip(tix[nzix]-1, 0, 1e+10)

    # in case a matching quote is crossed, use the previous one
    crix = np.nonzero(quote[tix,3]-quote[tix,1]<1e-7)[0]
    if len(crix) > 0 :
        tix[crix] = np.clip(tix[crix]-1, 0, 1e+10)
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
    return np.vstack((butc, ohlc.T, tvol, last_px, last_px_time, tvbs)).T, ixname

def mergeBar(MtsBar, barsec) :
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

    return np.vstack((bt,o,h,l,c,tv,lpx,lpxt,tvb)).T

def saveCSV(bar, file_name, do_gzip = True):
    assert (len(bar.shape)==2), 'bar needs to be a 2-dimensional array'
    assert (bar.shape[1] == 9), 'bar columns not matching'
    np.savetxt(file_name, bar, delimiter = ',', \
               fmt = ['%d','%.8g','%.8g','%.8g','%.8g','%d','%.8g','%d','%d'])
    if do_gzip :
        os.system('gzip -f ' + file_name)

class TickdataMap :
    def __init__(self, symbol_map_obj) :
        self.symbol_map = symbol_map_obj

    def get_tickdata_file(self, mts_symbol_no_N, contract_ym, day_ymd) :

        # figure out the tickdata symbols
        tmap = self.symbol_map.get_tradable_map(day_ymd, mts_key = True, mts_symbols = [ mts_symbol_no_N ])
        tdsym = None
        for k in tmap.keys() :
            if tmap[k]["symbol"] == mts_symbol_no_N :
                if tmap[k]["contract_month"] != contract_ym :
                    continue
                tdsym = tmap[k]["tickdata_id"]
                tzone = tmap[k]["tickdata_timezone"]
                pxmul = float(tmap[k]["tickdata_px_multiplier"])
                break
        if tdsym is None :
            raise RuntimeError(mts_symbol_no_N + " not defined for contract month " + contract_ym + " on the day of " + day_ymd)

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
                os.system('mkdir -p ' + p + ' > /dev/null 2>&1')
                os.system('unzip ' + f + ' -d ' + p)

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

