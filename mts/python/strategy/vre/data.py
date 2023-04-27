# -*- coding: utf-8 -*-
import numpy as np
import copy
import datetime
import traceback
import os
import dill
import time
import yaml
import scipy.io as sio
import pandas as pd
import collections
import pytz

#MTS & Pickle lib
from utils.utilsRsch import *
from mts import mts_repo, mts_util, symbol_map
from PYDCS import load_pkls
from PyStrat.mcmdb import query_dataframe
from PyStrat.data_intraday import get_intraday_mat_trades


class Strat_Data() :
    def __init__(self, trade_day, obj_dict, backtest=False):
        self.trade_day = trade_day
        self.obj_dict = obj_dict
        
        #get configs
        self._get_config_mkt(backtest)
        self.markets = list(self.mkt_config.keys())
        self._get_config_strat()
        self._get_ix_time
        
        #init data
        self.bars = self.init_bars()
        self.parquet = self.get_parquet()
        self.vop = self.compute_vop()
        
        
        #Markets
        self.nfo = pd.read_sql(
                    '''
                        select *, [name] as idx
                        from mcmdb.futures.v_market
                        order by [name]
                    ''', 'mssql+pymssql://10.60.1.13/MCMDB').set_index('idx')
        self.nfo = self.nfo[~self.nfo['intraday_db_table'].isnull()]
    
    
  
    def _get_config_mkt(self, backtest,  market_cfg_file='./config/markets.yml'):
        '''
        '''
        with open(market_cfg_file, 'r') as f :
            self.mkt_config = yaml.safe_load(f)
        
        #Assert we have the right configuration
        mkt0 = list(self.mkt_config.keys())[0]
        keys = list(self.mkt_config[mkt0].keys())
        keys = [k for k in keys if k[-5] == '_time']
        
        assert len(keys) == 0, "Market config, need to have opening_time, closing_time and eval_time"
        
        
        
        # if live, convert everything to UTC !!
        if not backtest : 
            for mkt in self.mkt_config.keys():
                print(mkt)
                ##TODO assert _time !!!
                for k, v in self.mkt_config[mkt].items() :
                    if k[-5:] == '_time' :
                        tz = pytz.timezone(self.mkt_config[mkt][k.replace('time', 'timezone')])
                        t = datetime.strptime(self.trade_day +' '+self.mkt_config[mkt][k], '%Y%m%d %H:%M:%S')
                        utct = tz.localize(t).astimezone(pytz.UTC)
                        self.mkt_config[mkt][k] = utct.strftime('%H:%M:%S')
                        
    
    def _get_config_strat(self, strat_cfg_file='./config/strategy.yml') :
        '''
        '''
        with open(strat_cfg_file, 'r') as f :
            self.strat_config = yaml.safe_load(f)
        
        self.barsec = self.strat_config['barsec'] 
        self.contract = self.strat_config['contract']
        self.cols = self.strat_config['cols'] 
        self.nday = self.strat_config['days_back']
        self.n = 3600*24//self.barsec
    
    def _get_ix_time(self) :
        for mkt in self.mkt_config.keys() :
            for k, v in self.mkt_config[mkt] :
                if 'time' in k :
                    if v < 2000 :
                        v += 2400
                    
                    ix = (v - 2000)/self.barsec
                    self.mkt_config[mkt][k+'_ix'] = ix


    def init_historical_bars(self, contract='_N1', start=None, end=None) :
        '''
        Parameters
        ----------
        start : string (yyymmdd)
            start date
        end : string (yyymmdd)
            end date

        Returns
        -------
        hist_bars : {mkt : pandas.DataFrame}
            Dictionnary of Historical Bars per market

        '''
        if 'bars' in self.obj_dict :
            hist_bars = self.obj_dict['hist_bars']
        else :
            hist_bars =  collections.defaultdict(dict)
            
            
            # TODO HERE
            for mkt in self.mkt_config.keys():
                try :
                    hist_bars[mkt] = self.get_historic_data(mkt, contract, start, end, False)
                except :
                    pass
                    
        self.hist_bars = hist_bars
    
    def get_historic_data(self, mkt, contract, start=None, end=None, backtest=False) :
        '''
        Parameters
        ----------
        mkt : TYPE
            DESCRIPTION.
        nfo : TYPE
            DESCRIPTION.
        ordinal : TYPE
            DESCRIPTION.
        start : TYPE, optional
            DESCRIPTION. The default is None.
        end : TYPE, optional
            DESCRIPTION. The default is None.

        Returns
        -------
        bar_adj : TYPE
            DESCRIPTION.

        '''

        row = self.nfo[self.nfo['name'] == mkt]
        name = row['intraday_db_table'].iloc[0]
        tz = row['tickdata_timezone'].iloc[0]
        ordinal = contract.replace('_N','').replace('N','')
        
        filename = os.path.join('J:/Intraday/bar1m_trades/', f'{name}__{ordinal}')
        
        mat = sio.loadmat(filename)
        header = [x[0] for x in mat[f'{name}__{ordinal}__header'][0]]
        df = pd.DataFrame(mat[f'{name}__{ordinal}__data'], columns=header)
        
        # Parse datetime and set as index - int to datetime
        df['bar_datetime'] = pd.to_datetime(df['bar_datetime'].astype(int), 
                                            origin='unix',unit='s')
        df['bar_datetime'] = df['bar_datetime'].dt.tz_localize('US/Eastern')
        
        #1 day is 20pm to 7:59pm next day Local Time
        df['day'] = (df['bar_datetime'] + pd.Timedelta(hours=4)).dt.date
        df = df[df['day'].apply(lambda x : x.weekday() < 5)]
        
        
        #Convert to local exchange timezone
        if backtest : 
            df['bar_datetime'] = df['bar_datetime'].dt.tz_convert(
                self.mkt_config[mkt]['open_timezone']).dt.tz_localize(None)
            
        if start : 
            df = df[(df['bar_datetime'] >= pd.to_datetime(start,utc=True)) 
                    & (df['bar_datetime'] <= pd.to_datetime(end,utc=True))]
            
        # Roll adjust prices
        df['close'] = df['bar_close'] + df['diff_adj_factor']
        df['open'] = df['bar_open'] + df['diff_adj_factor']
        df['high'] = df['bar_high'] + df['diff_adj_factor']
        df['low'] = df['bar_low'] + df['diff_adj_factor']
        

        return df[['day','bar_datetime','open', 'high', 'low', 'close']]
    
    def get_holidays(self, mkt) :
        mkt_id = self.nfo['market_id'].loc[mkt]
        df = pd.read_sql(f'''
        select 
          trade_date as idx,
          trade_date,
          market_id,
          is_calendar_holiday as is_holiday
        from mcmdb.futures.v_calendar_data_new c
        where [market_id] = {mkt_id}
        order by trade_date
        ''', 'mssql+pymssql://10.60.1.13/MCMDB' ) 

        df['trade_date'] = df[['trade_date',
                               'is_holiday']].apply(lambda x : x['trade_date'] if x['is_holiday'] == 0 else None, axis=1)
        df['td_prev'] = df['trade_date'].ffill()
        df['td_next'] = df['trade_date'].bfill()
       
        return  df
        
    
    def compute_twap(self, df) :
        '''
        

        Parameters
        ----------
        df : TYPE
            DESCRIPTION.

        Returns
        -------
        TYPE
            DESCRIPTION.

        '''
        df = df.set_index('bar_datetime')
        dftwap = df['close'].rolling(self.strat_config['twap_interval'], min_periods=2).mean()
        dftwap = dftwap.shift(-(self.strat_config['twap_interval'] + self.strat_config['modelrun_lag']))
        dftwap = dftwap.resample(self.strat_config['resample_grid'], closed='right', label='right').last()
        return dftwap.reset_index()
    
    
    def compute_vop(self, backtest=True) :
        vop = {}
        for mkt in self.markets : 
            #Get info from Pickle
            p_ref = self.parquet['main']['ref']['markets'].copy()
            p_mult = self.parquet['main']['fp']['multiplier'].reset_index()[['index', mkt+self.contract]].set_index('index') #.copy()
            p_fx = self.parquet['main']['fx']['spot']

            #Calculate vop
            vopt = calcVOP(mkt, p_mult, p_ref, p_fx).to_frame()         # daily
            # vopt['bar_datetime'] = vopt.index.astype(str)  
            # vopt['bar_datetime'] = pd.to_datetime(vopt['bar_datetime']).dt.tz_localize('US/Eastern')
            
            # if backtest : 
            #     vopt['bar_datetime'] = vopt['bar_datetime'].dt.tz_convert(self.mkt_config[mkt]['open_timezone'])
            
            vop[mkt] = vopt
        return vop
    
    
    def get_parquet(self, fp='J:/pkl/') :
        '''
        Returns
        -------
        pickle : Dictionary 
            Load main parquet file, created everyday eod.

        '''

        pickle = load_pkls(fp) 
        return pickle
    
    
    def init_bars(self) :
        '''
        Returns
        -------
        TYPE
            DESCRIPTION.

        '''
        if 'bars' in self.obj_dict :
            return self.obj_dict['bars']
        else :
            return collections.defaultdict(dict)
    
    def update_bars(self, mkt, bars) :
        '''
        Description
        -------
        update self.bars with latest bars

        '''
        if not self.bars :
            self.bars[mkt] = bars
            
        else :
            self.bars[mkt] =  np.vstack(self.bars[mkt], bars)


if __name__ == '__main__' :
    pass
    #strat_data = Strat_Data('20220911', {})
