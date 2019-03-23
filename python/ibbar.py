import l1
import os
import datetime
import numpy as np
import traceback
import time
import multiprocessing as mp
import glob

## The global configuration file
## Assuming the current direcctory is the kisco home
CFG_FILE='./config/main.cfg'
IB_CLIENT='bin/histclient.exe'

# the order makes a difference in priority of receiving live update
sym_priority_list=['CL','LCO','ES','6E','6J','NG','ZN','GC','ZC',\
                   'FDX','STXE','6A','6C','6B','6N','ZB','ZF','6R',\
                   '6Z','6M','HO','RB','SI','HG','FGBX','FGBL','FGBS',\
                   'FGBM','LFU','LOU','ZW','ZS','ZM','ZL','HE','LE','PA',\
                   # nybot no permission yet
                   #'CC','CT','SB','KC',\
                   ]
#sym_priority_list_L2=['CL','LCO','ES']
sym_priority_list_L2=['CL','LCO','ES','6E','ZN','GC']

#sym_priority_list_l1_next=['CL', 'LCO', 'GC', 'SI', 'HG', 'ZC', 'NG', 'HO', 'RB', 'ZW','ZS','ZM','ZL']
# adding '6E','6J','ZN',ZF','ZB','6A','6C','6B','6N','6R','6Z','6M'
sym_priority_list_l1_next2=['6E','6J','ZN','ZF','ZB','6A','6C','6B','6N','6R','6Z','6M']
sym_priority_list_l1_next=['CL','LCO', 'GC', 'SI', 'HG', 'ZC', 'NG', 'HO', 'RB', 'ZW','ZS','ZM','ZL'] + sym_priority_list_l1_next2

barsec_dur={1:1800, 5:3600, 10:14400, 30:28800, 60:60*60*24,300:60*60*24}
ib_sym_special=['6A','6C','6E','6B','6J','6N','6R','6Z','6M','ZC']
ib_sym_etf=['EEM','EPI','EWJ','EWZ','EZU','FXI','GDX','ITB','KRE','QQQ','RSX','SPY','UGAZ','USO','VEA','XLE','XLF','XLK','XLU','XOP']
ib_sym_idx=['ATX','HSI','N225']; # to add ['K200', 'AP','TSX','Y','MXY','OMXS30'] , ['VIX'] should be 'VXX'

def ibvenue(symbol) :
    return l1.venue_by_symbol(symbol)

def ibfn(symbol,barsec,sday,eday) :
    return symbol+'_'+sday+'_'+eday+'_'+str(barsec)+'S'

def l1fc(sym,day,next_contract=False) :
    if ibvenue(sym) in l1.future_venues :
        if not next_contract :
            fc=l1.FC(sym,day)
        else :
            fc=l1.FC_next(sym,day)[0]
    else :
        fc = sym
    return fc

def ibfc(sym,day,next_contract=False) :
    fc = l1fc(sym,day,next_contract=next_contract)
    if sym in ib_sym_special :
        fc=sym+fc[-2:]
    return ibvenue(sym)+'/'+fc

def update_ib_config(symlistL1=sym_priority_list + ib_sym_etf + ib_sym_idx, symlistL1next=sym_priority_list_l1_next,symlistL2=sym_priority_list_L2, cfg_file=None) :
    if symlistL1 is None :
        raise ValueError('symlistL1 cannot be None!')

    day=l1.trd_day()
    symL1=[]
    symL1n=[]
    symL2=[]
    for s in symlistL1 :
        symL1.append(ibfc(s,day))
    # the next contracts
    for s in symlistL1next :
        symL1n.append(ibfc(s,day,next_contract=True))
    for s in symlistL2 :
        symL2.append(ibfc(s,day))

    if cfg_file is not None :
        fline=[]
        with open(cfg_file,'r') as f :
            f.seek(0,2)
            sz=f.tell()
            f.seek(0)
            txt=f.read(sz).split('\n')
            for i,t in enumerate(txt) :
                ####
                ## This is very tricky as SubL1 is a substring of SubL1n
                ## So the sequence is important!
                ####
                if t[:6]=='SubL1n' :
                    txt[i]=('SubL1n = ' + '%s'%(symL1n)).replace('\'','')
                elif t[:5]=='SubL1':
                    txt[i]=('SubL1 = ' + '%s'%(symL1)).replace('\'','')
                elif t[:5]=='SubL2' :
                    txt[i]=('SubL2 = ' + '%s'%(symL2)).replace('\'','')
                if txt[i] != '' :
                    txt[i] += '\n'

        with open(cfg_file,'w') as f :
            f.writelines(txt)
    return symL1, symL2, symL1n

def read_cfg(key, cfg_file=CFG_FILE, default_value=None) :
    with open(cfg_file, 'r') as f :
        while True :
            d = f.readline()
            if len(d) == 0 :
                return default_value
            da=d.strip().split('=')
            if da[0].strip() == key.strip() :
                return da[1].strip()

def get_ib_future(symbol_list, start_date, end_date, barsec, ibclient=IB_CLIENT, clp='IB',mock_run=False, getqt=True,gettrd=False, cid=100, start_end_hour = [], next_contract=False, reuse_exist_file=False, verbose=False, num_threads=None, wait_thread=True) :
    bar_path = read_cfg('HistPath')
    if num_threads is not None :
        import _strptime
        n = len(symbol_list)
        k = np.linspace(0, n, num=num_threads+1).astype(int)
        pool=mp.Pool(processes=num_threads)
        res = []
        for i0, i1 in zip(k[:-1], k[1:]) :
            if i1 == i0 :
                continue
            res.append(pool.apply_async(get_ib_future, args=(symbol_list[i0:i1], start_date, end_date, barsec, ibclient, clp, mock_run, getqt,gettrd, cid, start_end_hour, next_contract, reuse_exist_file, verbose, None, True)))
            cid += 1

        fnarr = []
        if wait_thread :
            for r in res :
                fnarr += r.get()
        return fnarr

    step_sec=barsec_dur[barsec]
    fnarr=[]
    for symbol in symbol_list :
        venue=ibvenue(symbol)
        if venue == 'FX' :
            bar_dir = bar_path+'/FX'
        elif venue == 'ETF' :
            bar_dir = bar_path+'/ETF'
        elif venue == 'IDX' :
            bar_dir = bar_path+'/IDX'
        else :
            bar_dir = bar_path+'/'+symbol
        if next_contract :
            bar_dir += '/nc'
        os.system(' mkdir -p ' + bar_dir)

        if len(start_end_hour) != 2 :
            start_hour, end_hour = l1.get_start_end_hour(symbol)
        else :
            start_hour, end_hour = start_end_hour

        ti = l1.TradingDayIterator(start_date)
        day=ti.yyyymmdd()
        eday=day
        while day <= end_date :
            sday=eday
            fc=l1fc(symbol, day)
            fcn=l1fc(symbol,day,next_contract=True)
            while day <= end_date :
                ti.next()
                day=ti.yyyymmdd()
                fc0=l1fc(symbol, day)
                if fc != fc0 :
                    break
                eday=day
            # make sure eday is not more than end_date
            # if end_date was given as a weekend dates
            if (eday > end_date) :
                print 'ending to ', end_date, ' adjust to ',
                ti0=l1.TradingDayIterator(eday)
                eday = ti0.prev().yyyymmdd()
                print eday

            if next_contract :
                fc=fcn
            fn=bar_dir+'/'+ibfn(fc,barsec,sday,eday)
            fnarr.append(fn)

            fext = []
            cext = []
            for gt, ext, ext_str, etp in zip([getqt, gettrd], ['_qt.csv','_trd.csv'], ['quote','trade'], ['0','1']) :
                if not gt :
                    continue
                fn0=fn+ext
                # reuse_exist_file
                try :
                    found=0
                    assert reuse_exist_file
                    for ext0 in ['', '.gz'] :
                        try :
                            if os.stat(fn0+ext0).st_size > 1024:
                                found += 1
                                print 'found existing file: ', fn0+ext0, ' count = ', found
                        except :
                            continue
                    assert found == 1
                    print 'reusing ', fn0, ' for ', ext_str 
                except :
                    print 'getting ', ext_str, ' FILE: ', fn0, ' (found = %d)'%(found)
                    fext.append(ext)
                    cext.append(etp)

            if len(fext) == 0 :
                print 'Nothing to get from %s to %s!'%(sday, eday)
                continue

            if len(fext) == 1 and fext[0] == '_trd.csv' and next_contract and getqt :
                print '!! Next Contract using existing quote only'
                continue
                
            if ibclient is None :
                # here if ibclient is None then
                # don't run it (save time)
                # the caller should except file
                # not found and handle it with zero bar
                print 'Not running ibclient (None)!'
                fnarr.remove(fn)
                continue

            # clean up the existing files
            for ext in fext :
                fn0=fn+ext
                if not mock_run:
                    os.system( 'rm -f ' + fn0 + ' > /dev/null 2>&1')
                    os.system( 'rm -f ' + fn0 + '.gz' + ' > /dev/null 2>&1')
            
            if symbol in ib_sym_special :
                fc = symbol+fc[-2:]
            sym=venue+'/'+fc
            # get all days with the same contract, saving to the same file
            tic=l1.TradingDayIterator(sday)
            d0=tic.yyyymmdd()
            try :
                while d0 <= eday and d0 <= end_date :
                    # get for day d0
                    utc1=tic.to_local_utc(end_hour,0,0)
                    utc0=utc1-(end_hour-start_hour)*3600
                    while utc0 < utc1 :
                        # get for quote and trade for end_time as utc
                        utc0+=step_sec
                        eday_str=datetime.datetime.fromtimestamp(utc0).strftime('%Y%m%d %H:%M:%S')
                        #for ist, ext in zip (['0', '1'], ['_qt.csv','_trd.csv']):
                        for ist, ext in zip (cext, fext) :
                            fn0=fn+ext
                            cmdline=ibclient + ' ' + str(cid) + ' ' + sym + ' ' + '\"'+eday_str+'\"' + ' ' + str(barsec) + ' ' + fn0 + ' ' + ist + ' ' + clp
                            print 'running ', cmdline
                            if not mock_run :
                                os.system( cmdline )
                                time.sleep(2)
                                #os.system( 'sleep 2' 
                    tic.next()
                    d0=tic.yyyymmdd()
            except (KeyboardInterrupt, SystemExit) :
                print 'stop ...'
                return []
            except :
                traceback.print_exc()

    for fn in fnarr :
        for ext in fext :
            fn0=fn+ext
            if not mock_run:
                print 'gzip ', fn0
                os.system('gzip ' + fn0)

    """
    if upd_repo :
        repo_path = read_cfg('RepoPath')
        future_inclusion = ['back' if next_contract else 'front']
        from IB_hist import ingest_all_symbol
        ingest_all_symbol(start_date, end_date, repo_path=repo_path, get_missing=True, sym_list=sym_list, future_inclusion=future_inclusion)
    """

    return fnarr

def ib_bar_by_file(fn, skip_header) :
    """
    using l1.py's write_daily_bar()interface, need to collect all bars in format of
    bar0=[utc, utc_lt, b['open'],b['high'],b['low'],b['close'],b['vwap'],b['volume'],b['bvol'],b['svol']]
    """
    qt_raw=np.genfromtxt(fn+'_qt.csv',delimiter=',',usecols=[0,1,2,3,4])
    trd_raw=np.genfromtxt(fn+'_trd.csv',delimiter=',',usecols=[0,1,2,3,4,5,6,7])

def get_ib(start_date, end_date, barsec=5, ibclient=IB_CLIENT, clp='IB',mock_run=False, cid=213, exclude_list=[], sym_list=None, reuse_exist_file=False, verbose=False, num_threads=None, wait_thread=True) :
    """
    This gets non-future history files, such as FX and ETF
    """
    sym = sym_list
    if sym is None :
        #all_venues = ['ETF'] + l1.fx_venues
        all_venues = l1.fx_venues
        sym = []
        for v in all_venues :
            sym += l1.ven_sym_map[v]

    sym0=[]
    for s in sym :
        if s not in exclude_list :
            sym0.append(s)

    return get_ib_future(sym0, start_date, end_date, barsec, ibclient=ibclient, clp=clp,mock_run=mock_run, getqt=True,gettrd=False, cid=cid, reuse_exist_file=reuse_exist_file, verbose=verbose, num_threads=num_threads, wait_thread=wait_thread)

#################################
# Just because getting IB history bars
# has so many problem!
##################################
def matchts(fromfn, tofn, is_head=True) :
    """
    fromfn is the qt that has either before or afterwards needs to be cut
    against the tofn
    if is_head is True, then trim fromfn for head
    if is_head is False, then trim fromfn for tail
    """
    f0 = np.genfromtxt(fromfn, delimiter=',')
    f1 = np.genfromtxt(tofn, delimiter=',')
    # check head
    if is_head :
        ts0 = f0[0,0]
        ts1 = f1[0,0]
        ix = 0
        if ts0 < ts1 :
            ix = np.searchsorted(f0[:,0], ts1)
        return ix+1

    if not is_head:
        ts0 = f0[-1,0]
        ts1 = f1[-1,0]
        ix = len(f0)
        if ts0 > ts1 :
            ix = np.searchsorted(f0[:,0], ts1)
            if f0[ix,0] == ts1 :
                ix += 1
        return ix

def bar_file_cleanup(sym) :
    """
    clean up the crap of qt/trd mismatch
    This is just one time thing, won't 
    expect to happen. 
    for each sym_dir
    1. move the *_20180125_*_qt.csv to *_20180201_*_qt.csv 
       cut the qt to match the timestamps of trd
    2. move the *_2018*_20180507_1S_qt.csv to *_2018*_20180502_1S_qt.csv
       cut the qt to match the timestamps of trd
    3. move all the 2017 stuffs to kdb_verify
    And hopefully it's only need for once
    """
    import subprocess

    hist_dir = read_cfg('HistPath')
    sym_dir = hist_dir + '/' + sym
    f2017 = glob.glob(sym_dir+'/*2017*')
    if len(f2017) > 0 :
        os.system('mkdir -p '+sym_dir+'/kdb_verify')
        os.system('mv ' + sym_dir + '/*2017* ' + sym_dir + '/kdb_verify')

    f0125qt = glob.glob(sym_dir+'/*_2018012?_*_qt.csv')
    if len(f0125qt) == 1 :
        f0201trd = glob.glob(sym_dir+'/*_20180201_*_trd.csv')[0]
        f0125qt = f0125qt[0]
        # get the first timestamp and match with the line number
        ts_trd = subprocess.check_output("head -n 1 " + f0201trd + " | cut -d ',' -f 1",shell=True)[:-1]
        qt_ln = subprocess.check_output("grep -n " + ts_trd + " " + f0125qt + " | cut -d ':' -f1 | tail -n 1",shell=True)[:-1]
        if len(qt_ln) == 0 :
            print 'manually get the line number  '
            qt_ln = str(matchts(f0125qt, f0201trd,is_head=True))
        print 'trim ', f0125qt, ' got line number ', qt_ln

        # take the qt context on and after the line
        os.system('tail -n +' + qt_ln + ' ' + f0125qt + ' > ' + sym_dir + '/0125qt_tmp')
        os.system('mv ' + f0125qt + ' ' + f0125qt + '.sav')
        os.system('mv ' + sym_dir + '/0125qt_tmp' + ' ' + f0201trd[:-7]+'qt.csv')

        #print 'tail -n +' + qt_ln + ' ' + f0125qt + ' > ' + sym_dir + '/0125qt_tmp'
        #print 'mv ' + f0125qt + ' ' + f0125qt + '.sav'
        #print 'mv ' + sym_dir + '/0125qt_tmp' + ' ' + f0201trd[:-7]+'qt.csv'

    else :
        print sym_dir+'/*_2018012?_*_qt.csv not found', f0125qt

    f0507qt = glob.glob(sym_dir+'/*_20180507_?S_qt.csv')
    if len(f0507qt) == 2 :
        for f in f0507qt :
            if '20180502' in f :
                continue
            else :
                break
        print 'got ', f
        # need 0502 trade
        f0507qt = f
        f0502trd = glob.glob(sym_dir+'/*_20180502_*_trd.csv')[0]
        ts_trd = subprocess.check_output("tail -n 1 " + f0502trd + " | cut -d ',' -f 1",shell=True)[:-1]
        qt_ln = subprocess.check_output("grep -n " + ts_trd + " " + f0507qt + " | cut -d ':' -f1 | head -n 1",shell=True)[:-1]
        if len(qt_ln) == 0 :
            print 'manually get the line number'
            qt_ln = str(matchts(f0507qt, f0502trd,is_head=False))
        print 'trim ', f0507qt, ' got line number ', qt_ln

        # take the qt contend on and before the line
        os.system('head -n ' + qt_ln + ' ' + f0507qt + ' > ' + sym_dir + '/0507qt_tmp')
        os.system('mv ' + f0507qt + ' ' + f0507qt + '.sav')
        os.system('mv ' + sym_dir + '/0507qt_tmp' + ' ' + f0502trd[:-7]+'qt.csv')
    else :
        print sym_dir+'/*_20180507_?S_qt.csv len not 2 ', f0507qt


def get_all_hist(start_day, end_day, type_str, reuse_exist_file=False, verbose=False) :
    """
    type_str = ['future', 'etf', 'fx', 'future2','idx']
    future2 is the next contract
    This is the function to be called at the end of week to ingest all the history so far. 
    """
    dt = datetime.datetime.now()
    cid = dt.month * 31 + dt.day + 300 + dt.second
    bar_sec = 1

    # Using Thread Pool for 
    if type_str == 'future' :
        get_ib_future(sym_priority_list,         start_day, end_day ,bar_sec,mock_run=False,cid=cid+1, getqt=True, gettrd=True, next_contract=False, reuse_exist_file=reuse_exist_file, verbose=verbose, num_threads=4, wait_thread=True)
    elif type_str == 'etf' :
        get_ib_future(ib_sym_etf,                start_day, end_day ,bar_sec,mock_run=False,cid=cid+10, getqt=True, gettrd=True, next_contract=False, reuse_exist_file=reuse_exist_file, verbose=verbose,num_threads=4, wait_thread=True)
    elif type_str == 'fx' :
        get_ib(                                  start_day, end_day,                        cid=cid+20,                                              reuse_exist_file=reuse_exist_file, verbose=verbose,num_threads=4, wait_thread=True)
    elif type_str == 'future2' :
        # future_2 symbols are covered by future and written there
        get_ib_future(sym_priority_list_l1_next, start_day, end_day ,bar_sec,mock_run=False,cid=cid+30, getqt=True, gettrd=True, next_contract=True, reuse_exist_file=reuse_exist_file, verbose=verbose,num_threads=2, wait_thread=True)
    elif type_str == 'idx' :
        get_ib_future(ib_sym_idx,                start_day, end_day, bar_sec,mock_run=False,cid=cid+40, getqt=False, gettrd=True, next_contract=False,reuse_exist_file=reuse_exist_file,verbose=verbose)
    else :
        print 'unknown type_str ' , type_str, ' valid is future, etf, fx, future2'

def get_missing_day(symbol, trd_day_arr, bar_sec, is_front, cid = None, reuse_exist_file=True, reuse_exist_only=False) :
    """
    Couple of options:
    reuse_exist_file: will take the previous daily file
                      and try to reuse it
    reuse_exist_only: will only try to reuse the existing
                      daily file.  If not found, then
                      don't run the ibclient.  This is
                      usually the case for unnecessary
                      days (such as outside of sday/eday
                      of file name).
    """
    ibclient=IB_CLIENT
    if reuse_exist_only :
        ibclient=None

    if cid  is None :
        dt = datetime.datetime.now()
        cid = dt.month * 31 + dt.day + 300 + dt.second 

    fnarr = []
    for day in trd_day_arr :
        if day in l1.bad_days :
            print 'not getting holiday ', day
            continue
        if l1.venue_by_symbol(symbol) == 'FX':
            fnarr += get_ib(day, day, cid=cid+3,sym_list=[symbol],reuse_exist_file=reuse_exist_file, verbose=False, ibclient=ibclient)
        else :
            # future or etf
            next_contract = not is_front
            fnarr += get_ib_future([symbol], day, day ,bar_sec,mock_run=False,cid=cid+1, getqt=True, gettrd=True, next_contract=next_contract,reuse_exist_file=reuse_exist_file, verbose=False,ibclient=ibclient)

    return fnarr

def move_bar(rsync_dir=None) :
    """
    rsync_dir could be /cygdrive/e/ib/kisco/bar
    """
    bar_path = read_cfg('BarPath')
    dt = datetime.datetime.now()
    if dt.weekday() != 4 :
        print 'not a friday!'
        return
    yyyymmdd = dt.strftime('%Y%m%d')
    # getting the previous week
    wk = glob.glob(bar_path+'/2???????')
    wk.sort()
    prev_yyyymmdd = wk[-1].split('/')[-1]
    # make sure it's a friday

    prev_dt = datetime.datetime.strptime(prev_yyyymmdd, '%Y%m%d')
    if prev_dt.weekday() != 4 :
        print 'previous week', prev_yyyymmdd, ' not a friday ', prev_dt
        return

    # move bars
    print 'moving bar files to ', bar_path +'/' + yyyymmdd
    os.system('mkdir -p ' + bar_path+'/'+yyyymmdd)

    for ft in ['csv','bin'] :
        os.system('mv ' + bar_path+'/*.' + ft + ' ' + bar_path+'/' + yyyymmdd)
    os.system('gzip '+bar_path+'/'+yyyymmdd+'/*')

    if rsync_dir is not None and len(rsync_dir) > 0 :
        os.system('rsync -avz ' + bar_path + '/ ' + rsync_dir)
    return prev_yyyymmdd, yyyymmdd

####################
# History Ingestion 
####################

def all_hist_symbols() :
    sym = []
    for v in l1.ven_sym_map.keys() :
        sym += l1.ven_sym_map[v]
    return sym

def ingest_kdb(symbol_list, year_s = 1998, year_e=2018, repo = None) :
    import KDB_hist as kdb
    for symbol in symbol_list :
        print'ingesting ', symbol, ' from KDB.'
        try :
            kdb.gen_bar(symbol, year_s = year_s, year_e = year_e, repo=repo)
        except :
            print 'problem with ', symbol



