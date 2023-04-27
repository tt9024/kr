import numpy as np
import datetime
import os
import sys
import traceback
import subprocess
import glob
import pandas
import glob
import copy

import mts_util
import tickdata_parser as td_parser
import symbol_map

MTS_BAR_COL = {'utc':0,'open':1,'high':2,'low':3,'close':4,'vol':5,'lpx':6,'ltm':7,'vbs':8, \
               'absz':9, 'aasz':10, 'aspd':11, 'bdif':12, 'adif':13}

def get_col_ix(col_name_array) :
    cols = []
    for c in col_name_array:
        cols.append(MTS_BAR_COL[c])
    return np.array(cols).astype(int)

class MTS_REPO(object) :
    """
    Manages the MTS Bar in csv files for securities and dates
    Each file is one day of 1 second bar for the security in directories:
    repo_path/YYYYMMDD/tradable.csv
    """
    def __init__(self, repo_path, symbol_map_obj=None, backup_repo_path=None) :
        """
        repo_path: the root path to the MTS repo
        symbol_map_obj: the SymbolMap object for future contract definitions
        """
        self.path = repo_path
        self.backup_path = backup_repo_path
        if symbol_map_obj is None :
            symbol_map_obj = symbol_map.SymbolMap()
        self.symbol_map = symbol_map_obj


    def get_file_symbol(self, mts_symbol, contract_month, day, create_path=False, repo_path = None) :
        """
        getting file via the mts_symbol, i.e. "WTI" and a contract_month, i.e. '202101'
        """
        if repo_path is None: 
            repo_path = self.path
        p = os.path.join(repo_path, day)
        if create_path :
            os.system('mkdir -p ' + p + ' > /dev/null 2>&1')
        return os.path.join(p, mts_symbol+'_'+contract_month+'.csv')

    def get_file_tradable(self, tradable, yyyymmdd, create_path=False, repo_path = None, is_mts_symbol=None, get_holiday=True, check_file_exist=False):
        """
        get a mts bar file name from a 'tradable', could be a mts_symbol or a tradable
        """
        symn, sym, cont = (None,None,None)
        if is_mts_symbol is not None:
            try:
                symn, sym, cont = self.symbol_map.get_symbol_contract_from_tradable(tradable, yyyymmdd, is_mts_symbol=is_mts_symbol, add_prev_day=get_holiday)
            except:
                raise RuntimeError('filed to find bar file for %s on %s'%(tradable, yyyymmdd))
        else :
            try :
                symn, sym, cont = self.symbol_map.get_symbol_contract_from_tradable(tradable, yyyymmdd, is_mts_symbol=True, add_prev_day=get_holiday)
            except :
                symn, sym, cont = self.symbol_map.get_symbol_contract_from_tradable(tradable, yyyymmdd, is_mts_symbol=False, add_prev_day=get_holiday)

        bar_file = self.get_file_symbol(sym, cont, yyyymmdd, create_path=create_path, repo_path=repo_path)
        if check_file_exist:
            # bar_file could have '.gz'
            try :
                fn = glob.glob(bar_file+'*')
                assert os.stat(fn[0]).st_size > 1024
            except:
                try :
                    bar_file = self.get_backup_bar_file(bar_file)
                    fn = glob.glob(bar_file+'*')
                    assert os.stat(fn[0]).st_size > 1024
                except :
                    print( 'no bar file found (or too small) on '+yyyymmdd+ ' for ' + bar_file)
                    raise RuntimeError('no bar files found on '+yyyymmdd+ ' for ' + bar_file)
        return bar_file, sym, cont

    def get_backup_bar_file(self, bar_file) :
        yyyymmdd, sym_cont = bar_file.split('/')[-2:]
        sym, cont = sym_cont.split('.')[0].split('_')
        
        return self.get_file_symbol(sym, cont, yyyymmdd,create_path=False,repo_path=self.backup_path)

    def _get_bar(self, mts_bar_file, barsec = 1, prev_close_px=None, ref_utc=None):
        """
        Read given mts bar file, and return generated bar using self.barsec
        if prev_close_px is given, then it is set as the open of the first bar
        This is to get the over-night return.

        ref_utc: vector of utcs with barsec being barsec
        """
        if ref_utc is not None:
            assert ref_utc[1]-ref_utc[0] == barsec, 'ref_utc barsec mismatch'

        print ('getting mts bar from %s'%mts_bar_file)
        # bar_file is without '.gz', genfromtxt takes care of gzip
        bar = np.genfromtxt(mts_bar_file, delimiter=',', dtype='float')

        # make the previous close as the first open
        # for over-night lr
        if prev_close_px is not None:
            open_ix,high_ix,low_ix = get_col_ix(['open','high','low'])
            bar[0,open_ix]=prev_close_px
            bar[0,high_ix]=max(bar[0,high_ix],prev_close_px)
            bar[0,low_ix] =min(bar[0,low_ix], prev_close_px)

        if barsec != 1 :
            print ('merge into %d second bars'%barsec)
            bar = td_parser.mergeBar(bar, barsec)

        bar = np.array(bar)
        if ref_utc is not None:
            bar = normalize_ref_utc(bar, ref_utc)
        return bar

    def get_bar(self, mts_bar_file, barsec = 1, prev_close_px=None, ref_utc=None):
        bar = np.array([])
        try :
            bar=self._get_bar(mts_bar_file, barsec=barsec, prev_close_px=prev_close_px, ref_utc=ref_utc)
        except Exception as e:
            print(e)
        if len(bar) == 0 and self.backup_path is not None :
            mts_bar_file_backup = self.get_backup_bar_file(mts_bar_file)
            print ('problem getting ', mts_bar_file, ', trying backup ', mts_bar_file_backup)
            bar=self._get_bar(mts_bar_file_backup, barsec=barsec, prev_close_px=prev_close_px, ref_utc=ref_utc)
        if len(bar) == 0:
            raise RuntimeError('no bars found from %s'%(mts_bar_file))
        return bar

    def get_bars(self, tradable, sday, eday, barsec=1, cols = None, out_csv_file = None, ignore_prev = False, is_mts_symbol=None, get_roll_adj=False, remove_zero=False, hours=(-6,0,17,0), get_holiday=False):
        """ 
        tradable could be mts_symbol or a tradable
        both sday, eday are inclusive, in yyyymmdd
        cols an array for columns defined in MTS_BAR_COL
        if get_roll_adj is True, return a second value as a dict of ['days','contracts','roll_adj']
        hours: tuple of 4 integer (start_hour, min, end_hour_min), default to be (-6, 0, 17, 0)
        get_holiday: holidays could have half day of data saved in repo. If set to True, try read if 
                     exists, otherwise, will skip
        remove_zero: will forward fill all '0's, assuming columns are all non-zero values, i.e. utc, px, etc, that cannot be zero,
                     note that 0 could happen in columns such as vol. set it to true if cols shouldn't be 0.
        """
        prev_bar_file = None
        prev_contract = None
        prev_close_px = None
        prev_close_px_adj = None # for keep track of roll adjust
        prev_day = None
        if get_roll_adj:
            days = []
            contracts = []
            roll_adj = []

        if not ignore_prev :
            tdi = mts_util.TradingDayIterator(sday, eday)
            tdi.begin()
            day = tdi.prev()
            prev_cnt = 0
            while True :
                try :
                    # always include half day if previous day is a holiday
                    # this would use the holiday's previous day as the definition on that day
                    prev_bar_file, prev_sym, prev_contract = self.get_file_tradable(tradable, day, is_mts_symbol=is_mts_symbol, get_holiday=True, check_file_exist=True)
                    bar0 = self.get_bar(prev_bar_file, barsec=1)
                    prev_close_px = bar0[-1,get_col_ix(['close'])[0]]
                    prev_day = day
                    break
                except KeyboardInterrupt as e:
                    print('stopped')
                    return None
                except :
                    print ('problem getting prev day ', day, ' bar, try previous one')
                    day = tdi.prev()
                    prev_cnt += 1
                    if prev_cnt > 3 :
                        print ("problem getting previous day for " + sday)
                        break

        tdi = mts_util.TradingDayIterator(sday, eday)
        day = tdi.begin()
        bar = []

        md = MTS_DATA(*hours, barsec=barsec)
        while day <= eday :
            try :
                # in case we rolled today
                bar_file, sym, contract = self.get_file_tradable(tradable, day, is_mts_symbol=is_mts_symbol, get_holiday=get_holiday, check_file_exist=True)
                if prev_contract is not None and contract != prev_contract:
                    # get from that contract
                    try :
                        prev_bar_file = self.get_file_symbol(sym, contract, prev_day)
                        bar0 = self.get_bar(prev_bar_file, barsec=1)
                        prev_close_px = bar0[-1,get_col_ix(['close'])[0]]
                    except :
                        print('failed to get current contract %s from previous day %s, overnight return might be lost'%(contract, prev_day))
                        prev_close_px = None

                prev_day = day
                prev_contract = contract

                if get_roll_adj:
                    # at this point, 
                    # prev_close_px_adj is previous day's close price using previous day's contract
                    # prev_close_px is the previous day's close price using today's contract
                    days.append(day)
                    contracts.append(contract)
                    if prev_close_px_adj is None:
                        print('missed the roll adjust on day ' + day)
                        cur_adj = 0
                    else :
                        # normalize to ticksize
                        if prev_close_px is None:
                            cur_adj=0
                        else:
                            cur_adj = prev_close_px-prev_close_px_adj
                            tick_size = 1e-10
                            if np.abs(cur_adj) >= 1e-10:
                                try :
                                    tick_size = float(self.symbol_map.get_tinfo(tradable, day,is_mts_symbol=True)['tick_size'])
                                except Exception as e:
                                    print('failed to get tick size ' + str(e))
                                print ('got roll adjust ticksize ', tick_size)
                            cur_adj = np.round(cur_adj/tick_size)*tick_size
                    roll_adj.append(cur_adj)

                # in case ref_utc is None, enforce a default start/stop time of 6pm to 5pm, 
                # except the symbol start/stop time is longer than, i.e. Brent, in which
                # case use the longer time period.
                ref_utc = md._get_ref_utc(day)
                bar0 = self.get_bar(bar_file, barsec=barsec, prev_close_px = prev_close_px, ref_utc=ref_utc)
                prev_close_px = bar0[-1,get_col_ix(['close'])[0]]
                prev_close_px_adj = prev_close_px
                if cols is not None:
                    bar0 = bar0[:,get_col_ix(cols)]
                if len(bar) == 0 or bar[-1].shape[1] == bar0.shape[1]:
                    bar.append(bar0)
                else :
                    raise RuntimeError('got bar different column size on %s, new shape %s, previous shape %s'%(day, str(bar0.shape), str(bar[-1].shape)))
            except KeyboardInterrupt as e:
                print ('stopped...')
                return None
            except :
                traceback.print_exc()
                print ('problem getting day ', day, ' skipping...')
            day = tdi.next()

        if len(bar) == 0:
            raise RuntimeError('no bar found from %s to %s for %s'%(sday, eday, tradable))

        if cols is not None :
            cix = get_col_ix(cols)
        else :
            cix = np.arange(bar[0].shape[1]).astype(int)
        bar = self._crop_bars(bar,cix,barsec)  # days may have different bar counts, i.e. LCO sunday

        if remove_zero:
            # assuming bar doesn't have 0 columns, i.e. the utc and price only
            bar = np.array(bar)
            ix = np.nonzero(np.array(bar)==0)
            cnt = len(ix[0])
            if cnt > 0:
                # fill forward
                while len(ix[0]) > 0 and cnt > 0:
                    bar[ix] = bar[ix[0], np.clip(ix[1]-1,0,bar.shape[1]-1).astype(int), ix[2]]
                    ix = np.nonzero(bar==0)
                    cnt -=1
                if len(ix[0]) > 0:
                    raise RuntimeError("cannot forward fill zeros")

        if out_csv_file is not None:
            print ('writting to %s'%out_csv_file)
            td_parser.saveCSV(bar, out_csv_file)

        if not get_roll_adj:
            return bar

        roll_adj_dict = {'days':days, 'contracts':contracts, 'roll_adj':roll_adj}
        return bar, roll_adj_dict

    @staticmethod
    def roll_adj(bar, utc_col, adj_cols, roll_adj_dict):
        """
        inputs;
            bar: shape [ndays, n, nc]
            utc_col: the column index (into nc) for utc, i.e. 0
            adj_cols: index into nc, i.e. [1,2,3,4] for o/h/l/c
            roll_adj_dict: dict of keys('days','contracts', 'roll_adj'), returned by get_bars()
                     days in 'YYYYMMDD', roll_adj is px diff from front to previous contract
        return: 
            bar with price adjusted with the most front contract unchanged,
                second most front adjusted from the front and the third front adjusted 
                from both second and first, etc.
            note bar is adjusted inplace
        """
        dix = 0
        bix = 0
        days = roll_adj_dict['days']
        adj = roll_adj_dict['roll_adj']
        ndays, n, nc = bar.shape
        bar_days = []
        for t in bar[:,-1,utc_col]:
            bar_days.append(datetime.datetime.fromtimestamp(t).strftime('%Y%m%d'))
        bar_days=np.array(bar_days)
        cols = np.array(adj_cols).astype(int)
        for day, diff in zip(days, adj):
            if np.abs(diff) < 1e-10:
                continue
            if day <= bar_days[0] or day > bar_days[-1]:
                continue
            # on the roll day, add to all previous day with 'diff'
            ix = np.clip(np.searchsorted(bar_days, day),0,ndays-1)
            bar[:ix,:,cols]+=diff
        return bar

    def _crop_bars(self, bar, col_ix, barsec) :
        """ make all daily bars in 'bar' same length, taking a minimum of all days
            no fill is performed, just crop.  see normalize() for backfill/forward_fill
            This is mainly for last check on in case the daily bars from repo has different
            shape, i.e. LCO has more bars on Sunday, and the ref_utc was not given.
        col_ix: a list of col index (integer)
        """
        bcnts = []
        for b0 in bar:
            bcnts.append(b0.shape[0])
        bset = set(bcnts)
        if len(bset) > 1 :
            print('different bar counts on different days, taking a smallest one %s'%(str(bset)))
            bc = min(bset)
            bix = np.nonzero(np.array(bcnts).astype(int)==bc)[0][0]
            utc_col = np.nonzero(np.array(col_ix).astype(int) == MTS_BAR_COL['utc'])[0]
            if len(utc_col) != 1:
                raise RuntimeError('cannot crop bars with different daily counts: utc not found cols')
            ucol = utc_col[0]
            utc = bar[bix][:, ucol]

            # crop bars with minimum barcounts, and no later than 5pm 
            # LCO had old bars from 18 - 17, then the new format has either 
            # 19-18 (sunday open) or 20-18.  So have 3 bar counts to deal with
            # all bars then use a minimum of 20 - 17. 
            # 
            # We enforce a utc_start and utc_end on each day, assuming
            # barsec are strictly enforced, i.e. no skipped bars
            day = datetime.datetime.fromtimestamp(utc[-1]).strftime('%Y%m%d')
            utc0 = int(datetime.datetime.strptime(day+'000000','%Y%m%d%H%M%S').strftime('%s'))
            utc1 = int(datetime.datetime.strptime(day+'170000','%Y%m%d%H%M%S').strftime('%s'))
            soffset = utc[0]-utc0
            eoffset = min(utc1,utc[-1])-utc0
            bc = (eoffset-soffset)//barsec+1
            assert (bc-1)*barsec+soffset==eoffset
            utc=np.arange(utc0+soffset,utc0+eoffset+barsec,barsec).astype(int)

            bars = []
            for b0 in bar:
                if b0.shape[0] != bc:
                    utc_b = b0[:,ucol]
                    day = datetime.datetime.fromtimestamp(utc_b[-1]).strftime('%Y%m%d')
                    utc0 = int(datetime.datetime.strptime(day+'000000','%Y%m%d%H%M%S').strftime('%s'))
                    utc1 = np.arange(utc0+soffset,utc0+eoffset+barsec,barsec).astype(int)
                    uix = np.searchsorted(utc_b, utc1)
                    if np.max(np.abs(utc_b[uix]-utc1)) > 0:
                        raise RuntimeError('cannot crop bars with different daily counts: incompatible bars')
                    b0 = b0[uix,:]
                bars.append(b0)
            bar = bars
        return np.array(bar)

    def get_next_trade_day_roll_adjust(self, mts_symbol, this_day, get_holiday=False):
        """
        Gets the next day's trading day, contract, roll_adj
        Note, this_day has to be a weekday. In case this_day 
              is a holiday, it search backwards for a trading day.
        get_holiday: if True, allow the next day to be a holiday, in which case just uses the contracts of this day, and no roll
        """
        tdi = mts_util.TradingDayIterator(this_day)
        tdi.begin()
        d0 = tdi.prev()
        tdi = mts_util.TradingDayIterator(this_day, d0)
        this_day=tdi.begin()
        while True:
            try:
                bar_file, mkt, contract = self.get_file_tradable(mts_symbol, this_day, is_mts_symbol=True, get_holiday=False, check_file_exist=True)
                break
            except:
                pass
            this_day = tdi.prev()

        tdi = mts_util.TradingDayIterator(this_day)
        tdi.begin()

        cnt = 0
        while cnt < 3:
            day = tdi.next()
            try :
                bar_file_n, _, contract_n = self.get_file_tradable(mts_symbol,day,is_mts_symbol=True, get_holiday=get_holiday, check_file_exist=False)
                break
            except :
                pass
            cnt += 1

        if cnt >= 3:
            print('next trade day not found for %s after %s'%(mts_symbol, this_day))
            raise ValueError('next trade day not found!')

        ra = 0.0
        if contract_n != contract:
            try :
                next_bar_file = self.get_file_symbol(mkt, contract_n, this_day)
                bar = self.get_bar(bar_file, barsec=1)
                bar_nc = self.get_bar(next_bar_file, barsec=1)

                # find out the adjust
                cix = get_col_ix(['close'])[0]
                ra = np.median(bar_nc[-3600*1:,cix]-bar[-3600*1:,cix])

                #normalize with tinfo
                tick_size = float(self.symbol_map.get_tinfo(mts_symbol, this_day, is_mts_symbol=True)['tick_size'])
                ra = np.round(ra/tick_size)*tick_size
            except Exception as e:
                print('problem gettting roll adjust from %s to %s, set ra=0:\n%s'%(bar_file, next_bar_file, str(e)))
        return day, contract_n, ra


TDRepoPath = '/home/mts/run/repo/tickdata_prod'
MTSRepoPath = '/home/mts/run/repo/mts_live_prod'

class MTS_REPO_Live (MTS_REPO) :
    def __init__(self, symbol_map_obj=None) :
        super(MTS_REPO_Live, self).__init__(MTSRepoPath, symbol_map_obj=symbol_map_obj, backup_repo_path=TDRepoPath)

class MTS_REPO_TickData (MTS_REPO) :
    def __init__(self, symbol_map_obj=None) :
        super(MTS_REPO_TickData, self).__init__(TDRepoPath, symbol_map_obj=symbol_map_obj, backup_repo_path=MTSRepoPath)

class MTS_DATA :
    def __init__(self, start_hour, start_min, end_hour, end_min, barsec = 1) :
        """
        start_hour:      New York hour of open time of first bar, which is the first bar time minus barsec,
                         signed integer relative to trading day.
                         For example,
                         -6, meaning previous day's 18:00:00, and
                         9,  meaning current day's 09:00:00.
        start_min:       Minute of open time of first bar, which is the first bar time minus barsec,
                         always positive.
                         For example,
                         30, meaning 30 minutes into the start hour
        end_hour:        New York hour of close time of last bar, which is the last bar time, 
                         signed integer relative to trading day.
                         For example,
                         17, meaning current day's 17:00:00
        end_min:         Minute of close time of last bar, which is the last bar time,
                         always positive.  
                         For example,
                         15, meaning 15 minutes into the end hour
        barsec:          The desired bar period
        """
        self.barsec = int(barsec)
        self.sh = int(start_hour)
        self.sm = int(start_min)
        self.eh= int(end_hour)
        self.em = int(end_min)
        if self.sh > self.eh and self.sh > 0:
            self.sh -= 24
        assert self.eh > self.sh, "end hour less or equal to start hour"
        self.venue_str = ''
        self.trade_day = None

    ##
    ## MTS Bar from TickData
    ##
    def fromTickData(self, quote_file_name, trade_file_name, trading_day_YYYYMMDD, time_zone, pxmul, out_csv_file=None, extended_fields=False, overwrite_repo = False):
        """
        Read given quote and trade, and return generated bar using self.barsec
        quote_file_name:      full path to tickdata quote file
        trade_file_name:      full path to tickdata trade file
        trading_day_YYYYMMDD: the trading day, in YYYYMMDD
        out_csv_file:         optional output csv file of the bar
        """

        if not overwrite_repo and out_csv_file is not None :
            # skip this if it exists
            fa = glob.glob(out_csv_file+'*')
            if len(fa) == 1:
                try:
                    if os.stat(fa[0]).st_size > 1000:
                        if extended_fields:
                            # check number of columns to be more than BASE_COLS
                            import subprocess
                            BASE_COLS = 9  # utc,ohlc,vol,lpx,ltm,vbs
                            gz = (fa[0][-3:]=='.gz')
                            cmd_str = '%s %s | head -n 1'%('zcat' if gz else 'cat', fa[0])
                            l = subprocess.check_output(cmd_str, shell=True).decode().strip().replace('\n','')
                            if len(l.split(',')) > BASE_COLS:
                                print ('found ', fa[0], ' not writting')
                                return []
                except Exception as e:
                    print('problem checking the ' + fa[0] + ' overwriting it.  error: ' + str(e))

        print ('getting quotes from %s'%quote_file_name)
        quote = td_parser.get_quote_tickdata(quote_file_name, time_zone=time_zone, px_multiplier=pxmul)
        print ('getting trades from %s'%trade_file_name)
        trade = td_parser.get_trade_tickdata(trade_file_name, time_zone=time_zone, px_multiplier=pxmul)
        start_utc, end_utc = self._get_utc(trading_day_YYYYMMDD)
        print ('generating bars on %s'%trading_day_YYYYMMDD)
        bar, colname = td_parser.daily_mts_bar(trade, quote, 1, start_utc, end_utc-start_utc, extended_fields = extended_fields)
        if self.barsec != 1 :
            bar = td_parser.mergeBar(bar, self.barsec)

        if out_csv_file is not None:
            print ('writting to %s'%out_csv_file)
            td_parser.saveCSV(bar, out_csv_file)
        return bar

    def fromTickDataMultiDay(self, start_day, end_day, mts_symbol, tickdata_path, repo_obj, tickdata_map_obj, extended_fields=False, overwrite_repo=False, include_spread=False, extra_N=[]):
        """
        continuously parse tickdata from start_day to end_day into MTS Bar.
        start_day, end_day: both start and end dyas are inclusive, in format of yyyymmdd
        mts_symbol:  array of mts symbols, in format of WTI
        tickdata_path: path to tickdata trade and quote
                       tickdata is supposed to be organized into
                       tickdata_path/quote/CLH21_2021_01_04_Q.asc.gz
                       tickdata_path/trade/CLH21_2021_01_04.asc.gz
        repo_obj: object of MTS_REPO, including a SymbolMap object
        tickdata_map_obj: object of TickdataMap object
        include_spread:  if true includes spread contracts, i.e. WTI_N1-WTI_N2
        """

        tdi = mts_util.TradingDayIterator(start_day, end_day)
        day = tdi.begin()
        while day <= end_day :
            print ("***\nGenerating for %s"%(day))
            sys.stdout.flush()
            sys.stderr.flush()
            contracts = repo_obj.symbol_map.get_contract_from_symbol(mts_symbol, day, add_prev_day=True, include_spread=include_spread, extra_N=extra_N)
            if len(contracts) == 0 :
                print ("nothing found on %s"%(day))
            else :
                try :
                    for con in contracts :
                        qfile, tfile, tzone, pxmul = tickdata_map_obj.get_tickdata_file(mts_symbol, con, day, add_prev_day=True)
                        qfile = os.path.join(tickdata_path, 'quote', qfile)
                        tfile = os.path.join(tickdata_path, 'trade', tfile)
                        out_csv_file = repo_obj.get_file_symbol(mts_symbol, con, day, create_path = True)
                        print("%s_%s"%(mts_symbol, con))
                        self.fromTickData(qfile, tfile, day, tzone, pxmul, out_csv_file=out_csv_file, extended_fields=extended_fields, overwrite_repo=overwrite_repo)
                except :
                    traceback.print_exc()
            day = tdi.next()

    ##
    ## MTS Bar from LiveTrading
    ##
    def _read_mts_bar_file(self, mts_bar_file, do_fix=False, manual_read=True):
        if not manual_read:
            try :
                bar = np.genfromtxt(mts_bar_file,delimiter=',',dtype=float)
                return bar
            except:
                traceback.print_exc()
                print('problem with the bar file %s, manual read'%(mts_bar_file))

        bar = []
        with open(mts_bar_file,'rt') as fp:
            while True:
                l = fp.readline()
                if len(l) == 0:
                    break
                lb = l.replace('\n','').split(',')
                if len(bar) > 0 :
                    if len(lb) != len(bar[-1]) :
                        print ('read bad line: %s, ignored'%(l))
                        continue
                    if len(lb[0]) != len(bar[-1][0]):
                        print('found a bad time stamp, %s, removed'%(str(lb)))
                        continue
                    if bar[-1][0] >= lb[0] :
                        print('found a time stamp mismatch, prev(%s) - now(%s), remove both'%(str(bar[-1]), str(lb)))
                        bar.pop(-1)
                        continue
                bar.append(lb)
        bar = np.array(bar).astype(float)
        if do_fix:
            tmp_fn = mts_bar_file+'.tmp'
            np.savetxt(tmp_fn, bar, delimiter=',', fmt='%d,%.7f,%.7f,%.7f,%.7f,%d,%.7f,%d,%d')
        return bar

    def fromMTSLiveData(self, mts_bar_file, trading_day_YYYYMMDD, out_csv_file=None, write_repo=True, mts_sym=None, skip_exist=False, repo_obj=None) :
        """
        Read given mts bar file, and return generated bar using self.barsec
        if write_repo is true, it saves the generated bar to the repo using the
        out_csv_file.  If it is None, then a file name is generated from MTS_REPO
        conventions, with repo path to be mts_live
        """
        if skip_exist and out_csv_file is not None:
            try :
                import glob
                fn = glob.glob(out_csv_file+'*')
                if len(fn) == 1:
                    print('found existing file %s'%(fn[0]))
                    if os.stat(fn[0]).st_size > 1024:
                        print('%s already exists, skipping'%(out_csv_file))
                        return None
            except:
                print('out_csv not found, creating it: %s'%(out_csv_file))

        if repo_obj is None:
            repo_path = "/home/mts/run/repo/mts_live"
            repo_obj = MTS_REPO(repo_path)
        # get all the bars from mts_bar_file
        # forward/backward fill upto stuc-eutc
        sutc, eutc = self._get_utc(trading_day_YYYYMMDD,mts_sym=mts_sym, smap=repo_obj.symbol_map)
        bar = None
        file_stat = os.stat(mts_bar_file)
        if file_stat.st_size < 64:
            print("file size (%d) too small, delete %s"%(file_stat.st_size, mts_bar_file))
            os.remove(mts_bar_file)
            return None
        bar = self._read_mts_bar_file(mts_bar_file)
        if bar[0,0] > sutc+1 :
            # in a unlikely situation where the first bar is missing in bar file,
            # try going to MTS Repo to retrieve previos day's bar
            venue, tradable = self._parse_bar_file(mts_bar_file)
            tdi = mts_util.TradingDayIterator(trading_day_YYYYMMDD, '19700101')
            tdi.begin()
            prev_day = tdi.prev()
            print ('%s first bar missing: try to get (%s:%s) from previous trading day %s'%(mts_bar_file, venue, tradable, prev_day))
            try_cnt = 0
            while True:
                try :
                    prev_bar_file, sym, contract = repo_obj.get_file_tradable(tradable, prev_day, get_holiday=True, check_file_exist=True)
                    prev_bar = repo_obj.get_bar(prev_bar_file)
                    print ('got %d bars'%(prev_bar.shape[0]))
                    bar = np.vstack((prev_bar[-1:,:], bar))
                    break
                except :
                    print('failed to get the previous day bar', prev_day)
                    try_cnt += 1
                    if try_cnt < 7 :
                        print ('try again for previous day')
                        prev_day = tdi.prev()
                        continue
                    print ('no previous bar found, using first open as previous close')
                    bar = np.vstack((bar[0:1,:], bar))
                    bar[0,0] = sutc+1
                    break

        dbar = self._normalize(bar, sutc, eutc) # this throws if less than 4 hours of data
        if write_repo: 
            self._write_repo(dbar, trading_day_YYYYMMDD, out_csv_file=out_csv_file, mts_bar_file=mts_bar_file)

        return dbar

    def updateLive(self, trade_day = None, gzip_expired=True, skip_exist=False, venue_str='', main_cfg_fn='/home/mts/run/config/main.cfg') :
        """
        check the bar directory's 1 second bars and write to the repo's mts_live bars
        """
        # generate a symbol map that takes today's main.cfg about symbol/spread subscriptions
        sm_obj = symbol_map.SymbolMap(main_cfg_fn = main_cfg_fn)

        repo_path = "/home/mts/run/repo/mts_live"
        repo_obj = MTS_REPO(repo_path, symbol_map_obj=sm_obj)
        if trade_day is None :
            # today as the trading day, usually it is called at eod of a trade day
            tdu = mts_util.TradingDayUtil()
            trade_day = tdu.get_trading_day(snap_forward=True)
            tdi = mts_util.TradingDayIterator(trade_day)
            tdi.begin()
            trade_day = tdi.prev()
        print ("Update MTS Bar from Live Trading on %s!"%(trade_day))

        bar_file_glob_str = "/home/mts/run/bar/*_1S.csv"
        if venue_str is not None and len(venue_str) > 0:
            bar_file_glob_str = "/home/mts/run/bar/*"+venue_str+"*_1S.csv"

        fn = glob.glob(bar_file_glob_str)
        for bar_file in fn :
            print ("Processing " + bar_file)
            try :
                venue, tradable = self._parse_bar_file(bar_file)
                mts_sym=None
                try :
                    out_csv_file, mts_sym, contract = repo_obj.get_file_tradable(tradable, trade_day, create_path=True, get_holiday=True, check_file_exist=False)
                except KeyError as e :
                    # try the next day in case ingestion started after 18:00. Note this wont'be be run on a Sunday
                    # if the tradeable is for next day, don't gzip it (but don't parse it either)
                    print ("tradable %s not found on %s"%(tradable, trade_day))
                    tdi = mts_util.TradingDayIterator(trade_day)
                    tdi.begin()
                    next_trade_day = tdi.next()
                    try:
                        out_csv_file, mts_sym, contract = repo_obj.get_file_tradable(tradable, next_trade_day, create_path=True, get_holiday=True, check_file_exist=False)
                        print("it is for next trade day %s"%(next_trade_day))
                        if gzip_expired:
                            print('NOT gziped')
                    except KeyError as e:
                        print("it is expired!")
                        if gzip_expired:
                            os.system("gzip -f \"" + bar_file + "\"")
                            print ("gzip'ed")
                    continue

                self.fromMTSLiveData(bar_file, trade_day, out_csv_file=out_csv_file,mts_sym=mts_sym, skip_exist=skip_exist, repo_obj=repo_obj)
            except KeyError as e :
                print ("KeyError: " + str(e))
            except AssertionError as e :
                print (str(e))
            except KeyboardInterrupt as e :
                print("Keyboard Interrupt, exit!")
                break
            except :
                traceback.print_exc()
                print("Bar File " + bar_file + " not processed!")

    def updateLiveVenueDay(self, gzip_expired=True, skip_exist=False):
        self.updateLive(trade_day = self.trade_day, gzip_expired=gzip_expired, skip_exist=skip_exist, venue_str=self.venue_str)

    def _parse_bar_file(self, bar_file) :
        """
        bar_file is supposed to be in the format of 
            /home/mts/run/bar/NYM_CLN1_1S.csv
        return:
            venue and tradable, i.e. NYM, CLN1
        """
        tk = bar_file.split('/')[-1].split('_')
        return tk[0], '_'.join(tk[1:-1])

    def _get_utc(self, trading_day_YYYYMMDD, mts_sym=None, smap=None, add_prev_day=True) :
        """ This is called by the bar writing process.  It gets the daily
        open/close hours from the symbol map to include those bars to the daily MTS bars.
        The number of bars in each day's MTS repo, i.e. the bar file, could be different.

        See _get_utc_alltime() for getting an all inclusive reference utc, that enforce a
        uniform daily bar count, while including all open times historically, upto 24 hours,
        such as Brent (18 to 18)
        """
        day_utc = int(datetime.datetime.strptime(trading_day_YYYYMMDD, '%Y%m%d').strftime('%s'))
        if mts_sym is not None:
            if smap is None:
                smap=symbol_map.SymbolMap(max_N=1)
            sym = mts_sym.split('_')[0] # remove the _Nx part
            tinfo=smap.get_tinfo(sym+'_N1', yyyymmdd = trading_day_YYYYMMDD, is_mts_symbol = True, add_prev_day=add_prev_day)
            sh,sm = np.array(tinfo['start_time'].split(':')).astype(int)[:2]
            eh,em = np.array(tinfo['end_time'].split(':')).astype(int)[:2]
            if sh>=eh:
                sh-=24
        else :
            sh, sm = [self.sh, self.sm]
            eh, em = [self.eh, self.em]
        sutc = day_utc + sh*3600 + sm*60
        eutc = day_utc + eh*3600 + em*60
        return sutc, eutc

    def _get_utc_alltime(self, trading_day_YYYYMMDD, mts_sym):
        """
        This gets start/end utc for mts_symbol for all the time inclusive, 
        i.e. if there were change in open/close time over the years, it gets the most inclusive range
        the purpose is to enforce a uniform shape of daily bars over the years

        It is faster than _get_utc() as it doesn't query symbol_map, but it needs a 'symbol_time_dict'
        to be loaded in the object. This object is separately created and should not be change too often.
        """
        utc0 = int(datetime.datetime.strptime(trading_day_YYYYMMDD).strftime('%s'))
        try :
            sutc,eutc = self.symbol_time_dict[mts_sym]
            return utc0+sutc, utc0+eutc
        except:
            return self._get_utc(trading_day_YYYYMMDD, mts_sym)

    def _get_ref_utc(self, trading_day_YYYYMMDD):
        utc0 = int(datetime.datetime.strptime(trading_day_YYYYMMDD, '%Y%m%d').strftime('%s'))
        sutc = utc0 + self.sh*3600 + self.sm*60
        eutc = utc0 + self.eh*3600 + self.em*60
        return np.arange(sutc,eutc,self.barsec)+self.barsec

    def _normalize(self, bar, sutc, eutc):
        """
        bar: shape (n,m), with m being the cols defined
        return: 
            1 second bar, first bar at sutc+1, last bar at eutc
            forward filled on any missing

        """
        cols = {'utc':0,'open':1,'high':2,'low':3,'close':4,'vol':5,'lastpx':6,'lastmicro':7,'buysell':8}

        # get all the bars from mts_bar_file
        # forward/backward fill upto stuc-eutc
        assert(len(bar.shape)==2), "bar has shape of " + str(bar.shape)
        n,m=bar.shape
        assert(m == len(cols.keys())), "unrecognized bar format: "
        utc = np.arange(sutc,eutc).astype(int) + 1

        butc = bar[:,cols['utc']].astype(int)
        ix0 = np.searchsorted(butc, sutc+0.5)
        ix1 = np.nonzero(butc[ix0:] > eutc)[0]
        if len(ix1) > 0 :
            ix1 = ix1[0] + ix0
        else :
            ix1 = len(butc)

        # check if we have enough updates
        MinimumBarCount = 4*3600 # ensure we have at least 4 hours of data
        assert (ix1-ix0 > MinimumBarCount), "No enough bars recorded in Live, " + str(ix1-ix0)

        bidx = butc[ix0:ix1]-utc[0]
        dbar = np.empty((len(utc),m))
        dbar[:,:] = np.nan
        dbar[bidx,:] = bar[ix0:ix1,:]

        ixnan = np.nonzero(np.isnan(dbar[:,0]))[0]
        if len(ixnan) > 0 :
            print ("filling " + str(len(ixnan)) + " missing bars")

            # The first bar is missing
            if ixnan[0] == 0 :
                # ensure we have some previous bars to look at
                assert (ix0>0), "missing first bar on " + str(datetime.datetime.fromtimestamp(sutc))

                dbar[0,cols['open']:cols['close']+1] = bar[ix0-1,cols['close']]
                dbar[0,cols['lastpx']] = bar[ix0-1,cols['lastpx']]
                dbar[0,cols['lastmicro']] = bar[ix0-1,cols['lastmicro']]

            # forward fill missing bars
            # 1. fill the close
            df = pandas.DataFrame(dbar[:,cols['close']])
            df.fillna(method='ffill',inplace=True)
            # 2. fill the open/h/l
            df = pandas.DataFrame(dbar[:,cols['open']:cols['close']+1])
            df.fillna(method='bfill',axis=1,inplace=True)
            # 3. fill the lastpx/time
            for c in [cols['lastpx'], cols['lastmicro']] :
                df = pandas.DataFrame(dbar[:,c])
                df.fillna(method='ffill',axis=0,inplace=True)
            # 4. fill in zeros for vol and bs
            for c in [cols['vol'], cols['buysell']] :
                dbar[ixnan,c] = 0
            # 5. normalize the utc
            dbar[ixnan,cols['utc']] = utc[ixnan]
        return dbar

    def _write_repo(self, daily_bar_1S, trading_day_YYYYMMDD, out_csv_file = None, tradable=None, mts_bar_file=None, repo_path=None) :
        if out_csv_file is None :
            # generate a file name, mainly for standalone run
            if repo_path is None :
                repo_path = "/home/mts/run/repo/mts_live"
            repo_obj = MTS_REPO(repo_path)
            if tradable is None :
                venue, tradable = self._parse_bar_file(mts_bar_file)
            out_csv_file, mts_sym, contract = repo_obj.get_file_tradable(tradable, trading_day_YYYYMMDD, create_path=True, get_holiday=True, check_file_exist=False)
        print ('writting to %s'%(out_csv_file))
        td_parser.saveCSV(daily_bar_1S, out_csv_file)

    def fromB1S_CSV(self, fn, trading_day_YYYYMMDD):
        """
        getting from 1 second bar with format of
        utc, bsz, bp, ap, asz, bvol, avol, last_micro, bqcnt, aqcnt,_,_,lpx

        Return:
        md_dict format in [lr, vol, vbs, lpx, utc] for each 1-second bar on the trade_day
        """
        utc0, utc1 = self._get_utc(trading_day_YYYYMMDD)
        assert utc1 > utc0+1, 'wrong start/stop time'

        bar = np.genfromtxt(fn, delimiter=',',dtype=float)
        assert len(bar) > 1, 'empty bar file ' + fn
        col = {'utc':0, 'bpx':2,'apx':3,'bvol':5,'svol':6,'last_micro':7,'lpx':12}
        utc = bar[:,col['utc']]

        utcb = np.array([utc0+1,utc1])
        ixb = np.clip(np.searchsorted(utc, utcb),0, len(utc)-1)
        ix0 = ixb[0]
        ix1 = ixb[-1]
        if utc[ix1] > utc1:
            ix1-=1
        assert ix1-ix0>0, 'no bar found during this period ' + fn

        ix1+=1
        ts = utc[ix0:ix1]
        mid = (bar[ix0:ix1,col['bpx']] + bar[ix0:ix1,col['apx']])/2
        bvol = bar[ix0:ix1,col['bvol']]
        svol = bar[ix0:ix1,col['svol']]
        vol = bvol + svol
        vbs = bvol - svol
        last_micro = bar[ix0:ix1,col['last_micro']]
        lpx = bar[ix0:ix1,col['lpx']]

        # getting the previous day's mid if possible
        mid0 = mid[0]
        if ix0>0 :
            mid0 = (bar[ix0-1,col['bpx']]+bar[ix0-1,col['apx']])/2

        open_px = np.r_[mid0, mid[:-1]]
        close_px = mid
        high_px = np.max(np.vstack((open_px,close_px)),axis=0)
        low_px = np.min(np.vstack((open_px,close_px)),axis=0)
        dbar = np.vstack((ts,open_px,high_px,low_px,close_px,vol,lpx,last_micro,vbs)).T
        return self._normalize(dbar, utc0, utc1)

def get_daily_bbo(mts_symbol, start_day, end_day, start_hhmm, end_hhmm, ax_bbo=None, ax_spd=None, bars=None, barsec=300):
    """
    getting the daily avg bbo size and spd
    """
    if bars is None:
        repo = MTS_REPO_TickData()
        bars = repo.get_bars(mts_symbol, start_day, end_day, barsec=barsec, cols = ['utc','lpx','absz','aasz','aspd'])
    ix = []
    for st in [start_hhmm, end_hhmm]:
        assert len(st) == 4, 'time in format of hhmm'
        hh = int(st[:2])
        mm = int(st[2:])
        if hh>=18:
            hh -= 24
        ix.append(((hh+6)*3600 + mm)//barsec)
    ix = np.array(ix).astype(int)
    utc = bars[:,ix[1],0]
    dt = []
    for t in bars[:,ix[1],0]:
        dt.append(datetime.datetime.fromtimestamp(t))
    bsz = np.mean(bars[:,ix[0]:ix[1],2],axis=1)
    asz = np.mean(bars[:,ix[0]:ix[1],3],axis=1)
    asp = np.mean(bars[:,ix[0]:ix[1],4],axis=1)
    if ax_bbo is not None:
        ax_bbo.plot(dt, (bsz+asz)/2, '.-', label=mts_symbol+' avg bid/ask')
    if ax_spd is not None:
        ax_spd.plot(dt, asp, '.-', label=mts_symbol+' avg spread')
    return utc, bsz, asz, asp

#####################################
#  TODO - pack it into holiroll_dict
#####################################
def update_holiday_dict(holiroll_dict, end_day):
    """ holiroll_dict: the holiday and roll dict with format
        {'holiday': [yyyymmdd], 
         'last_day': yyyymmdd
         'rolls': {'roll_day': [yyyymmdd], 'front_contra': [yyyymm]}
    """
    smap=symbol_map.SymbolMap(max_N=1)
    for mkt in holiroll_dict.keys():
        s_day = holiroll_dict[mkt]['last_day']
        tdi = mts_util.TradingDayIterator(s_day)
        tdi.begin()
        day = tdi.next()
        while day <= end_day:
            symbol = mkt + '_N1'
            try :
                tinfo=smap.get_tinfo(symbol, yyyymmdd = day, is_mts_symbol = True)
            except :
                holiroll_dict[mkt]['holiday'].append(day)
            day = tdi.next()
        holiroll_dict[mkt]['last_day'] = end_day
    return holiroll_dict

def get_daily_utc(mts_symbol, barsec, start_day, end_day, holiroll_dict, hours=(-6,0,17,0)):
    symbol = mts_symbol.split('_')[0]+'_N1'
    mkt = symbol.split('_')[0]
    assert mkt in holiroll_dict.keys(), '%s not in holiroll_dict keys %s'%(mkt, str(holiroll_dict.keys()))
    holidays = holiroll_dict[mkt]['holiday']
    last_day = holiroll_dict[mkt]['last_day']
    assert end_day <= last_day, 'holiroll_dict last_day %s less than end_day%s'%(last_day, end_day)

    sh,sm, eh, em = hours
    if sh>eh:
        sh-=24
    shm = sh*3600+sm*60
    ehm = eh*3600+em*60
    assert ehm-shm <= 24*3600, 'hours %s more than 24 hours!'%(str(hours))
    assert (ehm-shm)//barsec*barsec==(ehm-shm), 'hours %s not multiple of barsec %d'%(str(hours), barsec)
    utc0 = []
    tdi = mts_util.TradingDayIterator(start_day)
    day = tdi.begin()
    while day <= end_day:
        if day not in holidays:
            utc0.append(int(datetime.datetime.strptime(day, '%Y%m%d').strftime('%s')))
        day = tdi.next()
    bt = np.arange(shm,ehm,barsec).astype(int)+int(barsec)
    return np.tile(utc0,(len(bt),1)).T+bt

def normalize_ref_utc(bar, ref_utc, bar_cols = None, verbose=True):
    """
    Filling or removing a day bar in 'bar' according to 'ref_utc'.  No aggregation is
    performed; i.e. ref_utc is assumed to have the same barsec with 'bar'.
    called from MTS_DATA's fromMTSLive(), as well as from the repo's get_bars(), 
    where a start/stop to be enforced within a period.

    bar: shape (n,m), with m being the cols defined, one day worth of bars. 
         Note, the ref_utc[0] defines the first bar of a day, any bars
               before ref_utc0 is ignored.  If ref_utc[0] is not found, it
               will be backward filled using the open of first bar
    bar_cols: length m list of column names, each for bar's column, default to be
              all columns, depending on m>9 with extended.  'utc' has to be in.
    return: 
        bar with ref_utc, forward filled on any missing

    NOTE - it is different with _normalize(), which is used by
           repo writer to write 1S bars, and gets first open from
           previous close.
           This, however is used by repo reader, and mainly 
           concerned with filling initial/post bar times to make
           a consistent daily number of bars 'n'.  It works
           in a strictly daily fashion, i.e. doesn't look at
           previous day's close in case first bar missing.
           This is assuming that the first bar's open has already
           updated to reflect overnight-return, therefore, just
           backward fill the first bar.  For example, the
           bar is from 2am to 15pm, we could make it 6pm to 5pm.
           the 2am bar will be backfilled 6pm to 2pm, with other
           fields updated.
    """
    n,m=bar.shape
    if bar_cols is None:
        bar_cols = list(MTS_BAR_COL.keys())[:m]

    assert 'utc' in bar_cols, "utc not in bar_cols for normalize"
    bix = {}
    for i, bc in enumerate(bar_cols):
        bix[bc] = i
    barsec = ref_utc[1]-ref_utc[0]

    # chop bars in bar that are within ref_utc
    butc = bar[:,bix['utc']]
    if butc[0] >= ref_utc[-1] or butc[-1] <= ref_utc[0]:
        raise RuntimeError('no bars detected for ref_utc')
    ix0 = np.clip(np.searchsorted(butc,ref_utc[0]),0,len(butc)-1)
    ix1 = np.searchsorted(butc,ref_utc[-1]+0.001)-1
    if ix1-ix0 <= 0:
        raise RuntimeError('too few bars detected for ref_utc')
    if ix0>0:
        if verbose:
            print('ref_utc0 %s starts %d bars into the daily bar'%(str(datetime.datetime.fromtimestamp(ref_utc[0])), ix0))
    bar=bar[ix0:ix1+1,:]

    if bar[0, bix['utc']] > ref_utc[0]:
        if verbose:
            print('first bar missing, backfilling!')

        # add the first bar with the open
        b0 = bar[0,:].copy()
        # construct b0
        # utc to be ref_utc[0]
        # fill open to  high low close lpx
        # fill 0 to vol, vbs, bdif, adif
        # copy ltm, absz, aasz, aspd  (no change)
        b0[bix['utc']] = ref_utc[0]
        for c0 in ['close','high','low','lpx']:
            if c0 in bar_cols:
                try :
                    px0 = b0[bix['open']]
                except:
                    print('warning, using close price to backfill')
                    px0 = b0[bix['close']]
                b0[bix[c0]]=px0
        for c0 in ['vol', 'vbs', 'bdif', 'adif']:
            if c0 in bar_cols:
                b0[bix[c0]]=0

        if verbose:
            print('adding first row as ' + str(b0))
        bar = np.vstack((b0,bar))
        n+=1

    # get all the bars from mts_bar_file
    # forward/backward fill upto stuc-eutc
    assert(len(bar.shape)==2), "bar has shape of " + str(bar.shape)
    assert m == len(bar_cols), "bar shape " + str(bar.shape) + " mismatch with bar_col: "+ bar_cols

    butc = bar[:,bix['utc']].astype(int)
    ix0 = np.clip(np.searchsorted(butc, ref_utc+0.01)-1,0,len(butc)-1)
    zix = np.nonzero(butc[ix0] == ref_utc)[0]
    dbar = np.empty((len(ref_utc),m))
    dbar[:,:] = np.nan
    dbar[zix,:] = bar[ix0[zix],:]
    ixnan = np.nonzero(np.isnan(dbar[:,bix['utc']]))[0]
    if len(ixnan) > 0 :
        if verbose:
            dt=[]
            for ix in ixnan:
                dt.append(str(datetime.datetime.fromtimestamp(ref_utc[ix])))
            print ("filling " + str(len(ixnan)) + " missing bars "+ str(dt))

        # forward fill close, lpx, ltm, absz, aasz, aspd
        # copy close to open/high/low
        # fill 0 to vol, vbs, bdif, adif

        # 1. fill the close, lpx, ltm, absz, aasz, aspd
        for c0 in ['close', 'lpx', 'ltm', 'absz','aasz','aspd']:
            if c0 in bar_cols:
                df = pandas.DataFrame(dbar[:,bix[c0]])
                df.fillna(method='ffill',inplace=True)

        # 2. copy the open/h/l
        if 'close' in bar_cols:
            for c0 in ['open','high','low']:
                if c0 in bar_cols:
                    dbar[ixnan,bix[c0]] = dbar[ixnan,bix['close']]

        # 3. fill 0 for vol, vbs, bdif, adif
        for c0 in ['vol','vbs','bdif','adif']:
            if c0 in bar_cols:
                dbar[ixnan,bix[c0]] = 0

        # 4. fill in ref_utc
        dbar[:,bix['utc']] = np.array(ref_utc).copy()

    return dbar

