import l1
import os
import datetime
import numpy as np

ven_sym_map={'NYM':['CL','NG','HO','RB','GC','SI','HG'],'CME':['ES','6A','6C','6E','6B','6J','6N','6R','6Z','6M'],'CBT':['ZB','ZN','ZF','ZC'],'EUX':['FDX','STXE','FGBX','FGBL','FGBS','FGBM'],'FX':['AUD.CAD','AUD.JPY','AUD.NZD','CAD.JPY','EUR.AUD','EUR.CAD','EUR.CHF','EUR.GBP','EUR.JPY','EUR.NOK','EUR.SEK','EUR.TRY','EUR.ZAR','GBP.CHF','GBP.JPY','NOK.SEK','NZD.JPY','EUR.USD','USD.ZAR','USD.TRY','USD.MXN','USD.CNH','XAU.USD','XAG.USD']}
sym_priority_list=['CL','ES','6E','6J','NG','ZN','GC','ZC','FDX','STXE','6A','6C','6B','6N','ZB','ZF','6R','6Z','6M','HO','RB','SI','HG','FGBX','FGBL','FGBS','FGBM']
sym_priority_list_L2=['CL','ES','6E']
barsec_dur={1:1800, 5:3600, 10:14400, 30:28800, 60:60*60*24,300:60*60*24}
ib_sym_special=['6A','6C','6E','6B','6J','6N','6R','6Z','6M','ZC']
future_venues=['NYM','CME','CBT','EUX']
fx_venues=['FX']

def ibvenue(symbol) :
    for k,v in ven_sym_map.items() :
        if symbol in v :
            return k
    raise ValueError('venue not found for ' + symbol)

def ibfn(symbol,barsec,sday,eday) :
    return symbol+'_'+sday+'_'+eday+'_'+str(barsec)+'S'

def l1fc(sym,day) :
    if ibvenue(sym) in future_venues :
        fc=l1.FC(sym,day)
    else :
        fc = sym
    return fc

def ibfc(sym,day) :
    fc = l1fc(sym,day)
    if sym in ib_sym_special :
        fc=sym+fc[-2:]
    return ibvenue(sym)+'/'+fc

def update_ib_config(symlistL1=sym_priority_list,symlistL2=sym_priority_list_L2,day=None, cfg_file=None) :
    if symlistL1 is None :
        # everything in ven_sym_map
        symlistL1 = []
        for k in ven_sym_map.keys() :
            symlistL1+=ven_sym_map[k]

    if day is None :
        day=datetime.datetime.now().strftime('%Y%m%d')
    symL1=[]
    symL2=[]
    for s in symlistL1 :
        symL1.append(ibfc(s,day))
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
                if t[:5]=='SubL1':
                    txt[i]=('SubL1 = ' + '%s'%(symL1)).replace('\'','')
                elif t[:5]=='SubL2' :
                    txt[i]=('SubL2 = ' + '%s'%(symL2)).replace('\'','')
                if txt[i] != '' :
                    txt[i] += '\n'

        with open(cfg_file,'w') as f :
            f.writelines(txt)
    return symL1, symL2

def get_ib_future(symbol_list, start_date, end_date, barsec, ibclient='bin/histclient.exe', clp='IB',mock_run=False, bar_path='hist',getqt=True,gettrd=False, cid=100) :
    step_sec=barsec_dur[barsec]
    for symbol in symbol_list :
        venue=ibvenue(symbol)
        if venue == 'FX' :
            bar_dir = bar_path+'/FX'
        else :
            bar_dir = bar_path+'/'+symbol
        os.system(' mkdir -p ' + bar_dir)
        ti = l1.TradingDayIterator(start_date)
        day=ti.yyyymmdd()
        while day <= end_date :
            fc=l1fc(symbol, day)
            sday=day
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

            fn=bar_dir+'/'+ibfn(fc,barsec,sday,eday)
            if symbol in ib_sym_special :
                fc = symbol+fc[-2:]
            sym=venue+'/'+fc

            fext = []
            cext = []
            if getqt :
                print 'getting quote'
                fext.append('_qt.csv')
                cext.append('0')
            if gettrd :
                print 'getting trade'
                fext.append('_trd.csv')
                cext.append('1')
            #for ext in ['_qt.csv','_trd.csv']:
            for ext in fext :
                fn0=fn+ext
                if not mock_run:
                    os.system( 'rm -f ' + fn0 + ' > /dev/null 2>&1')
                    #os.system( 'rm -f ' + fn0 + '.gz' + ' > /dev/null 2>&1')
            
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
                        for ist, ext in zip (cext, fext) :
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

def get_ib_fx(start_date, end_date, barsec=5, ibclient='bin/histclient.exe', clp='IB',mock_run=False, bar_path='hist', cid=101, exclude_list=[]) :
    sym = []
    for v in fx_venues :
        sym += ven_sym_map[v]

    sym0=[]
    for s in sym :
        if s not in exclude_list :
            sym0.append(s)

    get_ib_future(sym0, start_date, end_date, barsec, ibclient=ibclient, clp=clp,mock_run=mock_run, bar_path=bar_path,getqt=True,gettrd=False, cid=cid)


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

def bar_file_cleanup(sym, hist_dir='hist') :
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
    import glob
    import subprocess

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

