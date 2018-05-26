import l1
import os
import datetime

ven_sym_map={'NYM':['CL','NG','HO','RB','GC','SI','HG'],'CME':['ES','6A','6C','6E','6B','6J','6N','6R','6Z','6M'],'CBT':['ZB','ZN','ZF','ZC'],'EUX':['FDX','STXE','FGBX','FGBL','FGBS','FGBM']}

barsec_dur={1:1800, 2:3600, 10:14400, 30:28800, 60:60*60*24,300:60*60*24}
ib_sym_special=['6A','6C','6E','6B','6J','6N','6R','6Z','6M','ZC']
def ibvenue(symbol) :
    for k,v in ven_sym_map.items() :
        if symbol in v :
            return k
    raise ValueError('venue not found for ' + symbol)

def ibfn(symbol,barsec,sday,eday) :
    return symbol+'_'+sday+'_'+eday+'_'+str(barsec)+'S'

def ibfc(sym,day) :
    fc=l1.FC(sym,day)
    if sym in ib_sym_special :
        fc=sym+fc[-2:]
    return ibvenue(sym)+'/'+fc

def get_ib_future(symbol_list, start_date, end_date, barsec, ibclient='bin/histclient.exe', clp='IB',mock_run=False) :
    cid = 100
    step_sec=barsec_dur[barsec]
    for symbol in symbol_list :
        bar_dir = symbol
        os.system(' mkdir -p ' + bar_dir)
        ti = l1.TradingDayIterator(start_date)
        day=ti.yyyymmdd()
        venue=ibvenue(symbol)
        while day <= end_date :
            fc=l1.FC(symbol, day)
            sday=day
            while day <= end_date :
                ti.next()
                day=ti.yyyymmdd()
                fc0=l1.FC(symbol, day)
                if fc != fc0 :
                    break
            eday=day
            fn=bar_dir+'/'+ibfn(fc,barsec,sday,eday)
            if symbol in ib_sym_special :
                fc = symbol+fc[-2:]
            sym=venue+'/'+fc

            #for ext in ['_qt.csv','_trd.csv']:
            for ext in ['_qt.csv'] :
                fn0=fn+ext
                os.system( 'rm -f ' + fn0 + ' > /dev/null 2>&1')
                os.system( 'rm -f ' + fn0 + '.gz' + ' > /dev/null 2>&1')
            
            # get all days with the same contract, saving to the same file
            tic=l1.TradingDayIterator(sday)
            d0=tic.yyyymmdd()
            try :
                while d0 <= eday and d0 <= end_date :
                    # get for day d0
                    utc0=tic.to_local_utc(-6,0,0)
                    utc1=tic.to_local_utc(17,0,0)
                    while utc0 < utc1 :
                        # get for quote and trade for end_time as utc
                        utc0+=step_sec
                        eday_str=datetime.datetime.fromtimestamp(utc0).strftime('%Y%m%d %H:%M:%S')
                        #for ist, ext in zip (['0', '1'], ['_qt.csv','_trd.csv']):
                        for ist, ext in zip (['0'], ['_qt.csv']):
                            fn0=fn+ext
                            cmdline=ibclient + ' ' + str(cid) + ' ' + sym + ' ' + '\"'+eday_str+'\"' + ' ' + str(barsec) + ' ' + fn0 + ' ' + ist + ' ' + clp
                            print 'running ', cmdline
                            if not mock_run :
                                os.system( cmdline )
                                #os.system ('gzip ' + fn0)
                                os.system( 'sleep 2' )
                    tic.next()
                    d0=tic.yyyymmdd()
            except (KeyboardInterrupt, SystemExit) :
                print 'stop ...'
                return
            except :
                traceback.print_exc()

def ib_bar_by_file(fn, skip_header) :
    """
    using l1.py's write_daily_bar()interface, need to collect all bars in format of
    bar0=[utc, utc_lt, b['open'],b['high'],b['low'],b['close'],b['vwap'],b['volume'],b['bvol'],b['svol']]
    """
    qt_raw=np.genfromtxt(fn+'_qt.csv',delimiter=',',usecols=[0,1,2,3,4])
    trd_raw=np.genfromtxt(fn+'_trd.csv',delimiter=',',usecols=[0,1,2,3,4,5,6,7])