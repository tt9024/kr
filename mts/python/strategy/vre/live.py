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
import pickle

#MTS & PIckle lib
from mts import mts_repo, mts_util, symbol_map
from PYDCS import load_pkls

#Import Specific function to the model
from model import run_bar
from data import Strat_Data

class Strat_Live :
    def __init__(self, trade_day, strat_data, obj_dict, persist_path='', persist_fn=''):
        self.trade_day = trade_day
        self.data = strat_data
        self.obj_dict = obj_dict
        self.persist_path = persist_path
        self.persist_fn = persist_fn
        self.strat_codes = '' #self.data.strat_config['strat_code']
        self.strat_name = '' #self.data.strat_config['strategy_key']
        self.markets = self.data.markets
        
        #Initialize/Retrieve Session
        #self.logger = mts_util.MTS_Logger(self.strat_name)
        self.livedict = self.init_livedict(trade_day)
        self.state = self.init_state(trade_day)
        self.hist_bars = self.init_hist_bars(trade_day)
        
    
    def init_livedict(self, trade_day) :
        '''
        

        Parameters
        ----------
        trade_day : TYPE
            DESCRIPTION.

        Returns
        -------
        bkabka : TYPE
            DESCRIPTION.

        '''
        
        if 'livedict' in self.obj_dict :
            return self.obj_dict['livedict']
        
        else :
            livedict = collections.defaultdict(dict)
            # utc0, utc1: start/end utc, fixed at 18:00 to 17:00
            print(self.trade_day)
            utc0 = int(datetime.datetime.strptime(self.trade_day, '%Y%m%d').timestamp()) - 6*3600
            utc1 = int(datetime.datetime.strptime(self.trade_day, '%Y%m%d').timestamp()) + 17*3600

            livedict['utc0'] = utc0
            livedict['utc1'] = utc1
            livedict['utc'] = utc0
            ld_mkt = {}
            smap = symbol_map.SymbolMap()
            for mkt in self.markets: 
                symbol = mkt + self.data.contract
                try :
                    bar_file, contract, contract_size, tick_size, start_time, end_time = mts_util.get_symbol_day_detail(symbol, self.trade_day, self.data.barsec, smap=smap)
                    ld_mkt[mkt] = {'bar_file': bar_file, 
                                   'trade_day': self.trade_day, 
                                   'contract_month':contract, 
                                   'contract_size':contract_size, 
                                   'tick_size':tick_size, 
                                   'start_time':start_time, 
                                   'end_time':end_time}
                except :
                    print('%s not a trading day for %s, livedict not updated'%(self.trade_day, mkt))
            livedict['mkt'] = ld_mkt
        return livedict
    

    def init_state(self, trade_day):
        '''
        
        Parameters
        ----------
        trade_day : string 
            today's trade date with format yyyymmdd
        
        Description
        -------
        update self.state : parameters to which we compare the live bar to enter/exist position

        '''
        if 'state'  in self.obj_dict :
            return self.obj_dict['state']
        
        
        path_data = self.data.strat_config['path_data']
        strategy_key = self.data.strat_config['strategy_key']
        liveho_fn = liveho_fn = f'LiveHO_{strategy_key}'
        
        
        with open(os.path.join(path_data, liveho_fn + '.yaml'),'r') as fw:
            stream = yaml.safe_load(fw)
            
        #assert stream['trade_date'] == trade_day, 'Not todays date'
        
        return stream['live_output']
    
    
    def init_hist_bars(self, trade_day) :
        if 'hist_bars' in self.obj_dict :
            return self.obj_dict['hist_bars']
        
        start = datetime.datetime.strptime(trade_day,'%Y%m%d') - datetime.timedelta(days=self.data.strat_config['max_days'])
        start = start.strftime('%Y%m%d')
        self.data.init_historical_bars(self.data.contract,start=start,end=trade_day)
        
    
    def run(self):
        
        self.should_run = True
        utc0 = self.livedict['utc0']
        
        
        # #Match position with mts
        # pos_matched = Strat_Live.match_position(self, self.logger)
        # if not pos_matched: 
        #     self.logger.logInfo('not all Stat positions match with MTS, trade will start with the Strat position!')

        
        while self.should_run:
            dt = datetime.datetime.now()
            cur_utc = int(dt.strftime('%s'))
            last_utc = self.livedict['utc']

            if cur_utc >= last_utc + self.data.barsec:
                # due for bar update
                k = (last_utc - utc0) // self.data.barsec
                barcnt = (cur_utc - last_utc)//self.data.barsec

                # run all open markets in live_dict
                for mkt in self.livedict['mkt'].keys():
                    self.logger.logInfo('checking %s on bar %d, barcnt(%d)'%(mkt, k, barcnt))
                    symbol = mkt + self.data.contract
                    
                    #Skip if not in tradeing hours
                    trd_k0 = self.data.mkt_config['open_time']
                    trd_k1 = self.data.mkt_config['close_time']
                    
                    bfile = self.livedict['mkt'][mkt]['bar_file']
                    bars = mts_util.get_bar_cols(bfile, barcnt, cols=[0,1,2,3,6])
                    
                    self.update_bars(bars)
                   
                    if k + barcnt - 1 < trd_k0 - 2 or k + barcnt - 1 > trd_k1 + 1:
                        continue
                    
                    #Update signal
                    #TODO curr utc
                    cur_utc, px, qty = self.update_live_signal(mkt, contract, bars, cur_utc, k, barcnt) #hist_bars=self.hist_bars[mkt], todays_bars=self.bars[mkt], latest_bars=bars, parquet=self.parquet[mkt], state=self.state[mkt]) #, last_utc, k, barcnt, self.data.barsec, self.logger)
                    # update_live_signal(self, mkt, contract, latest_bars, cur_utc0, k, barcnt) : 
                    if qty is None :
                        continue

                    #Send pos[]
                    tgt_pos = self.state['pos']['qty'][-1] + qty
                    self._set_mts_pos(symbol, tgt_pos)
                    self.logger.logInfo('set position %s to %f'%(symbol, float(tgt_pos)))
                    
                    #Update pos & pnl
                    self._upd_pos(mkt, qty, px)

                # update live_dict
                self.livedict['utc'] += barcnt * self.data.barsec

                # persist
                self.logger.logInfo('persisting')
                self._persist()

            if cur_utc > self.live_dict['utc1']:
                self.shoud_run = False
                break
            else:
                time.sleep(1)

        self.logger.logInfo('exit the loop')
        cur_utc = int(datetime.datetime.now().strftime('%s'))
        if cur_utc > self.live_dict['utc1']:
            #TODO HERE SMG with pos/pnl => add to live_pos/live_pnl files
            
            self.logger.logInfo('done with the day!')
            print('Done with the day!')
    
    
    def update_live_signal(self, mkt, contract, latest_bars, cur_utc0, k, barcnt) : 
        #bars, today_bars, latest_bars, parquet, mkt_state, mkt, last_utc, k, barcnt, barsec, logger):
        '''
        '''
        # take close as lpx
        if latest_bars is None:
            print('failed to get bar for %s'%(mkt))
            return
        
        # update barcnt starting from k
        for k0 in np.arange(barcnt).astype(int):
            cur_k0 = k + k0
            cur_utc0 += self.data.barsec
            
            # find cur_utc0 in bars[:,0]
            ix = np.clip(np.searchsorted(latest_bars[:,0], cur_utc0),0,barcnt-1)
            if latest_bars[ix,0] != cur_utc0:
                pass
                # this bar cur_utc0 not found in the bar file, not started?
                self.logger.logInfo('bar not found at %s(k=%d), using %s: %s'%(\
                        str(datetime.datetime.fromtimestamp(cur_utc0)), cur_k0,\
                        str(datetime.datetime.fromtimestamp(latest_bars[ix,0])), \
                        str(latest_bars[ix,:])))
            
            qty = run_bar(cur_k0, mkt, contract, self.data.hist_bars[mkt], self.data.bars[mkt], 
                          latest_bars, self.state[mkt], self.data.mkt_config[mkt], self.data.strat_config[mkt])
            
            if qty :
                px = latest_bars[ix,4]
                self.state[mkt]['pos'] += qty
                self.state[mkt]['px'] = px
                return cur_utc0, px, qty
            
        return cur_utc0, None, None
        
    def _upd_pos(self, mkt, utc, qty, px) :
        #TODO - review
        prev_qty = self.state[mkt]['pos']['qty'][-1] 
        prev_px = self.state[mkt]['pos']['px'][-1] 
        pnl =  (prev_px - px)*qty
        self.state[mkt]['pos']['utc'].append(utc)
        self.state[mkt]['pos']['qty'].append(prev_qty + qty)
        self.state[mkt]['pos']['px'].append(px)
        self.state[mkt]['pos']['pnl'].append(pnl)
        
    
    def _set_mts_pos(self, symbol, tgt_pos):
        # the trading time not checked here
        for strat in self.strat_codes:
            mts_util.set_target_position(strat, symbol, np.round(tgt_pos), self.logger, twap_minutes = 5)


    def _persist(self, fn_date = ''):
        persist_fn = self.persist_fn+fn_date
        fn = os.path.join(self.persist_path, persist_fn)
        with open(fn,'wb') as fp:
            dill.dump({'state': self.data._persist(self.data.state), 
                       'live_dict': self.live_dict, 
                       'bars': self.data.bars,
                       'hist_bars' : self.data.hist_bars
                       }, fp)

        # backup existing running dump, and start a new one
        if fn_date != '' :
            running_fn = os.path.join(self.persist_path, self.persist_fn)
            os.system('mv %s %s > /dev/null 2>&1'%(running_fn, running_fn+'_'+fn_date))
            os.system('cp %s %s'%(fn, running_fn))
    
    def _dump_pos(self) :
        return 
    
    def shutdown(self):
        print('Strat_Live shutting down')
        self.should_run = False

    @staticmethod
    def match_position(strat_obj, logger=None):
        match = True
        strat_name = strat_obj.strat_name
        for mkt in strat_obj.data.state.keys():
            strat_pos = strat_obj.data.state[mkt]['pnl']['daily_pos'][-1][1] 
            for strat in strat_obj.strat_codes:
                try :
                    pos = mts_util.get_mts_position(strat, mkt, logger=logger)
                    assert pos is not None
                except Exception as e:
                    if logger is not None:
                        logger.logError('failed to get position for %s %s'%(mkt, strat))
                    print('%s %s: failed to get position for %s %s'%(datetime.datetime.now().strftime('%Y%m%d-%H:%M:%S'), strat_name, mkt, strat))
                    continue
                if pos != strat_pos:
                    if logger is not None:
                        logger.logInfo('position mismatch for %s, %s: (strat:%d, mts:%d)'%(strat, mkt, strat_pos, pos))
                    print('%s %s: position mismatch for %s, %s: (strat:%d, mts:%d)'%(datetime.datetime.now().strftime('%Y%m%d-%H:%M:%S'), strat_name, strat, mkt, strat_pos, pos))
                    match = False
        if match:
            if logger is not None:
                logger.logInfo('ALL position Matched!')
            print('%s %s: all positions matched'%(datetime.datetime.now().strftime('%Y%m%d-%H:%M:%S'), strat_name))
        return match


def load_obj(fn) :
    return None
#%%

# if __name__ == '__main__' :
    
#       start_hour = 20
#       end_hour = 17
#       # run one day
#       try :
#           # wait for start_hour
#         while True:
#             dt = datetime.datetime.now()
#             if (dt.weekday() == 4 and dt.hour>= end_hour) or (dt.hour >= end_hour and dt.hour < start_hour):
#                 time.sleep(1)
#             else:
#                 break
             
#         tdu = mts_util.TradingDayUtil()
#         trade_day = tdu.get_trading_day(snap_forward=True)
#         print('kicking off at %s for trading day %s'%(str(dt), trade_day))
#         #If path exists, it means we had a disconnect during the day else we need to initialize everything
#         if os.path.exists('./stratlive_{trd_day}.dill') :
#             obj_dict = load_obj('./stratlive_{trd_day}.dill')
#         else :
#             obj_dict = {}
        
#         strat_data = Strat_Data(trade_day, obj_dict)
#         strat_live = Strat_Live(trade_day, strat_data, obj_dict)
#         strat_live.run()

                 
#       except KeyboardInterrupt as e:
#           print('user interruption')
#       except Exception as e:
#           traceback.print_exc()

#       strat_live.should_run=False
#       time.sleep(3)    
    
    
        


        
