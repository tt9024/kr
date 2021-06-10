import numpy as np
import datetime
import os
import sys
import traceback
import subprocess
import glob
import pandas

import mts_util
import tickdata_parser as td_parser
import symbol_map

class MTS_REPO :
    """
    Manages the MTS Bar in csv files for securities and dates
    Each file is one day of 1 second bar for the security in directories:
    repo_path/YYYYMMDD/tradable.csv
    """
    def __init__(self, repo_path, symbol_map_obj=None) :
        """
        repo_path: the root path to the MTS repo
        symbol_map_obj: the SymbolMap object for future contract definitions
        """
        self.path = repo_path
        if symbol_map_obj is None :
            symbol_map_obj = symbol_map.SymbolMap()
        self.symbol_map = symbol_map_obj

    def get_file_tradable(self, tradable, yyyymmdd, create_path=False) :
        p = os.path.join(self.path, yyyymmdd)
        if create_path :
            os.system('mkdir -p ' + p + ' > /dev/null 2>&1')
        symn, sym, cont = self.symbol_map.get_symbol_contract_from_tradable(tradable, yyyymmdd)
        return os.path.join(p, sym+'_'+cont+'.csv')

    def get_file_symbol(self, mts_symbol, contract_month, day, create_path=False) :
        """
        getting file via the mts_symbol, i.e. "WTI" and a contract_month, i.e. '202101'
        """
        p = os.path.join(self.path, day)
        if create_path :
            os.system('mkdir -p ' + p + ' > /dev/null 2>&1')
        return os.path.join(p, mts_symbol+'_'+contract_month+'.csv')

    def get_bar(self, tradable, yyyymmdd, barsec = 1, out_csv_file=None) :
        """
        Read given mts bar file, and return generated bar using self.barsec
        """
        mts_bar_file = self.get_file_tradable(tradable, yyyymmdd)
        print ('getting mts bar from %s'%mts_bar_file)
        os.system('gunzip -f ' + mts_bar_file + '.gz > /dev/null 2>&1')
        bar = np.genfromtxt(mts_bar_file, delimiter=',', dtype='float')
        os.system('gzip -f ' + mts_bar_file + ' > /devnull 2>&1')

        if barsec != 1 :
            print ('merge into %d second bars'%barsec)
            bar = td_parser.mergeBar(bar, barsec)
        if out_csv_file is not None:
            print ('writting to %s'%out_csv_file)
            td_parser.saveCSV(bar, out_csv_file)
        return bar

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
        assert self.eh > self.sh, "end hour less or equal to start hour"

    ##
    ## MTS Bar from TickData
    ##
    def fromTickData(self, quote_file_name, trade_file_name, trading_day_YYYYMMDD, time_zone, pxmul, out_csv_file=None):
        """
        Read given quote and trade, and return generated bar using self.barsec
        quote_file_name:      full path to tickdata quote file
        trade_file_name:      full path to tickdata trade file
        trading_day_YYYYMMDD: the trading day, in YYYYMMDD
        out_csv_file:         optional output csv file of the bar
        """
        print ('getting quotes from %s'%quote_file_name)
        quote = td_parser.get_quote_tickdata(quote_file_name, time_zone=time_zone, px_multiplier=pxmul)
        print ('getting trades from %s'%trade_file_name)
        trade = td_parser.get_trade_tickdata(trade_file_name, time_zone=time_zone, px_multiplier=pxmul)
        start_utc, end_utc = self._get_utc(trading_day_YYYYMMDD)
        print ('generating bars on %s'%trading_day_YYYYMMDD)
        bar, colname = td_parser.daily_mts_bar(trade, quote, 1, start_utc, end_utc-start_utc)
        if self.barsec != 1 :
            bar = td_parser.mergeBar(bar, self.barsec)
        if out_csv_file is not None:
            print ('writting to %s'%out_csv_file)
            td_parser.saveCSV(bar, out_csv_file)
        return bar

    def fromTickDataMultiDay(self, start_day, end_day, mts_symbol, tickdata_path, repo_obj, tickdata_map_obj) :
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
        """

        tdi = mts_util.TradingDayIterator(start_day, end_day)
        day = tdi.begin()
        while day <= end_day :
            print ("***\nGenerating for %s"%(day))
            sys.stdout.flush()
            sys.stderr.flush()

            contracts = repo_obj.symbol_map.get_contract_from_symbol(mts_symbol, day)
            if len(contracts) == 0 :
                print ("nothing found on %s"%(day))
            else :
                try :
                    for con in contracts :
                        qfile, tfile, tzone, pxmul = tickdata_map_obj.get_tickdata_file(mts_symbol, con, day)
                        qfile = os.path.join(tickdata_path, 'quote', qfile)
                        tfile = os.path.join(tickdata_path, 'trade', tfile)
                        out_csv_file = repo_obj.get_file_symbol(mts_symbol, con, day, create_path = True)
                        print("%s_%s"%(mts_symbol, con))
                        self.fromTickData(qfile, tfile, day, tzone, pxmul, out_csv_file=out_csv_file)
                except :
                    traceback.print_exc()
            day = tdi.next()

    ##
    ## MTS Bar from LiveTrading
    ##
    def fromMTSLiveData(self, mts_bar_file, trading_day_YYYYMMDD, out_csv_file=None, write_repo=True) :
        """
        Read given mts bar file, and return generated bar using self.barsec
        if write_repo is true, it saves the generated bar to the repo using the
        out_csv_file.  If it is None, then a file name is generated from MTS_REPO
        conventions, with repo path to be mts_live
        """
        # get all the bars from mts_bar_file
        # forward/backward fill upto stuc-eutc
        sutc, eutc = self._get_utc(trading_day_YYYYMMDD)
        bar = None
        file_stat = os.stat(mts_bar_file)
        if file_stat.st_size < 64:
            print("file size (%d) too small, delete %s"%(file_stat.st_size, mts_bar_file))
            os.remove(mts_bar_file)
            return None
        bar = np.genfromtxt(mts_bar_file,delimiter=',',dtype=float)
        if bar[0,0] > sutc+1 :
            # in a unlikely situation where the first bar is missing in bar file,
            # try going to MTS Repo to retrieve previos day's bar
            venue, tradable = self._parse_bar_file(mts_bar_file)
            tdi = mts_util.TradingDayIterator(trading_day_YYYYMMDD, '19700101')
            tdi.begin()
            prev_day = tdi.prev()
            print ('%s first bar missing: try to get (%s:%s) from previous trading day %s'%(mts_bar_file, venue, tradable, prev_day))
            try :
                repo_path = "/home/mts/run/repo/mts_live"
                repo_obj = MTS_REPO(repo_path)
                prev_bar = repo_obj.get_bar(tradable, prev_day)
                print ('got %d bars'%(prev_bar.shape[0]))
                bar = np.vstack((prev_bar[-1:,:], bar))
            except :
                print('failed to get the previous day bar')

                # fix: backfill the first bar
                # Note, this is only allowed if the missing is 
                # less than 15 seconds 
                # TODO - consider removing it
                if bar[0,0] - sutc+1 < 15:
                    print  ('backfill the first bar')
                    bar = np.vstack((bar[0:1,:], bar))
                    bar[0,0] = sutc+1
                else :
                    return None

        dbar = self._normalize(bar, sutc, eutc)
        if write_repo: 
            if out_csv_file is None :
                # generate a file name, mainly for standalone run
                repo_path = "/home/mts/run/repo/mts_live"
                repo_obj = MTS_REPO(repo_path)
                venue, tradable = self._parse_bar_file(mts_bar_file)
                out_csv_file = repo_obj.get_file_tradable(tradable, trading_day_YYYYMMDD, create_path=True)
            print ('writting to %s'%out_csv_file)
            td_parser.saveCSV(dbar, out_csv_file)
        return dbar

    def updateLive(self, trade_day = None, gzip_expired=True) :
        """
        check the bar directory's 1 second bars and write to the repo's mts_live bars
        """
        print ("Update MTS Bar from Live Trading!")
        repo_path = "/home/mts/run/repo/mts_live"
        repo_obj = MTS_REPO(repo_path)
        if trade_day is None :
            tdu = mts_util.TradingDayUtil()
            trade_day = tdu.get_trading_day(snap_forward=False)
        bar_file_glob_str = "/home/mts/run/bar/*_1S.csv"
        fn = glob.glob(bar_file_glob_str)
        for bar_file in fn :
            print ("Processing " + bar_file)
            try :
                venue, tradable = self._parse_bar_file(bar_file)
                try :
                    out_csv_file = repo_obj.get_file_tradable(tradable, trade_day, create_path=True)
                except KeyError as e :
                    print ("tradable %s expired on %s"%(tradable, trade_day))
                    if gzip_expired:
                        os.system("gzip -f \"" + bar_file + "\"")
                        print ("gzip'ed")
                    continue

                self.fromMTSLiveData(bar_file, trade_day, out_csv_file)
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
    
    def _parse_bar_file(self, bar_file) :
        """
        bar_file is supposed to be in the format of 
            /home/mts/run/bar/NYM_CLN1_1S.csv
        return:
            venue and tradable, i.e. NYM, CLN1
        """
        tk = bar_file.split('/')[-1].split('_')
        return tk[0], '_'.join(tk[1:-1])

    def _get_utc(self, trading_day_YYYYMMDD) :
        day_utc = int(datetime.datetime.strptime(trading_day_YYYYMMDD, '%Y%m%d').strftime('%s'))
        sutc = day_utc + self.sh*3600 + self.sm*60
        eutc = day_utc + self.eh*3600 + self.em*60
        return sutc, eutc

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

