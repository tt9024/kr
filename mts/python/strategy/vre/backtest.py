# -*- coding: utf-8 -*-
import numpy as np
import datetime
import traceback
import os
import yaml
import pandas as pd
import time
import paramiko
from pandas.tseries.offsets import BDay

#Import Specific function to the model
from mts import symbol_map
from model import run_daily_strat, generate_signal
from data import Strat_Data

from PyStrat.strat_output import liveheader_txt
from PyStrat.strat_output import liveoutput_txt
from PyStrat.strat_output import pnl_txt
from PyStrat.strat_output import positions_txt
from PyStrat.strat_output import positionsts_txt
from PyStrat.strat_output import exelog_txt
from PyStrat.strat_output import legacy_pnl_txt
from PyStrat.strat_output import dict_to_cpp_cfg


class Strat_Backtest :
    def __init__(self, strat_data, trade_day, strategy_key, output_fn):
        self.trade_day = trade_day
        self.data_day = trade_day - BDay(1)
        self.data = strat_data
        self.output_fn = output_fn
        self.strategy_key = strategy_key
        self.markets = self.data.markets
        self.smap  = symbol_map.SymbolMap('U:\\Scripts/mts_data/data',max_N=1)  
        
    
    def compute_signal(self, mkt, bars, twap) :
        '''
        

        Parameters
        ----------
        bars : pandas.DataFrame
            Historical Intraday file

        Returns
        -------
        signal : pandas.DataFrame
            Daily Signal for Next trading day

        '''
        
        return generate_signal(mkt, 
                               self.data.contract, 
                               bars, 
                               twap, 
                               self.data.parquet['main'], 
                               self.data.vop[mkt], 
                               self.data.mkt_config[mkt], 
                               self.data.strat_config
                               )

    
    def _backtest(self) :
        '''
        Description
        ----------
        _backtest function, that runs generate the signals by calling compute_signal,
        run the strategy day by day and store positions and current price to produce outputs

        "Returns"
        -------
        self.positions : Dict (DataFrame)
            self.positions stores all position taken everyday by market
        
        self.signals : Dict
            self.signals stores signals produced everyday for the next trading date
            

        '''
        self.positions = {}
        self.signals = {}
        
        
        for mkt in self.markets :
            # try :
                print(mkt)
                
                #Initialize Positions
                positions = {'qty' : [0], 'pos' : [0], 'px' : [None], 'px_twap' : [None], 
                             'utc' : [None], 'trade_date' : [None], 'market' : [None], 
                             'symbol' : [None], 'currency': [None]}
                
                #Get Bars and Twap and signals
                bars = self.data.get_historic_data(mkt, self.data.contract, backtest=True)
                twap = self.data.compute_twap(bars)
                self.signals[mkt] = self.compute_signal(mkt, bars, twap)
                
                
                #TODO ASSERT barsec == resample_gridsize
                bars_res = bars.set_index('bar_datetime').resample(self.data.strat_config['resample_grid'], closed='right', label='right').last().reset_index()
                
                
                
                
                #Filter only active hours
                opening = self.data.mkt_config[mkt]['open_time']
                opening = datetime.time(int(opening[:2]), int(opening[3:5]))
                closing = self.data.mkt_config[mkt]['close_time']
                closing = datetime.time(int(closing[:2]), int(closing[3:5]))
                bars_res['time'] = bars_res['bar_datetime'].dt.time
                bars_res = bars_res[(bars_res['time']>= opening) & (bars_res['time'] < closing)]
               
                #Run Backtest for everyday
                for day, todays_bars in bars_res.groupby('day') : 
                    day = pd.to_datetime(day)
                    pos = run_daily_strat(day, self.signals[mkt], todays_bars, positions)
                    for k in positions.keys() :
                        l = len(pos['qty'])
                        
                        if l == 0 :
                            if k == 'pos' :
                                positions[k].append(positions[k][-1])
                            
                            elif k == 'qty' :
                                positions[k].append(0)
                                
                            elif k == 'trade_date' :
                                positions[k].append(day)
                                
                            elif k in ['market', 'symbol', 'currency'] :
                                positions[k] = mkt if k == 'market' \
                                        else mkt + self.data.contract if k == 'symbol' \
                                        else 'USD' #TODO self.data.mkt_config['currency']
                            else :
                                positions[k].append(None)
                                
                        for i in range(l) : 
                            if k in ['qty', 'px', 'px_twap']:
                                positions[k].append(pos[k][i])
                            
                            elif k == 'pos' :
                                q = positions[k][-1] + pos['qty'][i] 
                                positions[k].append(q)
                                                       
                            elif k in ['market', 'symbol', 'contract', 'currency'] :
                                positions[k] = mkt if k == 'market' \
                                        else mkt+self.data.contract if k == 'symbol' \
                                        else 'USD' #TODO self.data.mkt_config['currency']
                            else :
                                positions[k].append(pos[k][i])
    
                self._get_positions(mkt, positions)
            # except :
            #     pass
            
    def _get_positions(self, mkt, pos) :
        '''
        

        Parameters
        ----------
        mkt : str
            market on which we are working on
        pos : Dict
            contains all the information relatives to positions taken everyday

        Description
        ----------
        _get_positions function, converts the dictionnary of positions in a DataFrame
        and compute several metrics needed like PnL or vol or contract_code

        '''
        
        pos = pd.DataFrame(pos)
        pos = pos.iloc[1:]
        pos['temp_px'] = pos['px_twap'].ffill()
        pos['Pnl'] = ((pos['px'] - pos['temp_px'])*pos['qty']) + pos['px'].diff()*pos['pos'].shift()
        pos['trade_date'] = pd.to_datetime(pos['trade_date'])
        pos['vol'] = np.sqrt(252) * pos['Pnl'].dropna().rolling(self.data.strat_config['port_lb']).std()
        pos['vol'] = pos['vol'].ffill()
        
        pos = pd.merge(pos, self.data.parquet['main']['ref']['contracts'][['contract_code', 'market', 'roll_date']], left_on=['trade_date', 'market'], right_on=['roll_date', 'market'],how='left')
        pos['contract'] = pos['contract_code'].ffill()
        self.positions[mkt] = pos
    
   
    def compute_portfolio(self, cost=0) :
        '''
        

        Parameters
        ----------
        cost : int, optional
            Associated cost to calculates net PnL. The default is 0.

        Returns
        -------
        port : DataFrame
            DataFame of aggregated Pnl for all the markets

        '''
        #Initialize
        pnls, nets = {}, {}
        
        #Get Dates
        dates = self.data.parquet['main']['ref']['dates']['data']
        dates = pd.date_range(self.data.strat_config['date_from'],dates,freq='B')
        
        for mkt in self.markets : 
            try :
                vop = self.data.vop[mkt]
                vop['trade_date'] = pd.to_datetime(vop.index)
                df = pd.merge(self.positions[mkt], vop, on=['trade_date'], how='left')
                
                df['Pnl'] = df['Pnl']*df[mkt+self.data.contract]
                df['net'] = df['Pnl'] - np.abs(df['pos'].diff())*df[mkt+self.data.contract]*cost#*self.data.mkt_config['ticksize']
                
                df = df.groupby('trade_date')[['Pnl', 'net']].sum()
                pnls[mkt] = df['Pnl'].reindex(dates).fillna(0)
                nets[mkt] = df['net'].reindex(dates).fillna(0)
            except :
                pass
        
        pnls = pd.DataFrame(pnls)
        nets = pd.DataFrame(nets)
        
        port = pd.DataFrame(index=dates)
        port['Pnl'] = pnls.sum(axis=1)
        port['net'] = pnls.sum(axis=1)
        port['vol'] = np.sqrt(252) * port['Pnl'].dropna().rolling(self.data.strat_config['port_lb']).std()
        port['vol'] = port['vol'].ffill()
        port['CumPnl'] = port['Pnl'].cumsum()
        port['Cumnet'] = port['net'].cumsum()
        
        return port
    
    def compute_strats_lo(self) :
        '''
        

        Returns
        -------
        strat_lo : TYPE
            DESCRIPTION.

        '''
        
        strat_lo = pd.DataFrame(index=self.markets,
                    columns=['theme','strategy','sub_strategy','underlying',
                             'contract','volatility','decision'])
        for mkt in self.markets : 
            try :
                # Generate LiveOutput
                mkt_locol = []
                mp_lo = pd.merge(self.positions[mkt].groupby('trade_date')[['market','symbol','contract','currency','vol']].last().reset_index()
                                 ,self.signals[mkt], left_on=['trade_date'], right_on=['trade_date'], how='left')
                

                mp_lo['prev_date'] = mp_lo['trade_date'].shift()
                mp_lo['prev_date'] = mp_lo['prev_date'].dt.strftime('%Y%m%d')
                mp_lo = mp_lo.reset_index(drop=True).set_index('trade_date')
                holidays = self.data.get_holidays(mkt)
                td_valid_or_next = dict(zip(holidays['trade_date'], holidays['td_next']))
                mkt_lo_date = td_valid_or_next[datetime.datetime.strptime(self.data_day, '%Y%m%d')]
                
                for lodt, row in mp_lo.iloc[-5:].iterrows():
                    
                    # Build live output decision
                    lo_dec = {}
                    # lo_dec['data_error'] = int(row['data_error'])
                    lo_dec['trade_date'] = lodt.strftime('%Y%m%d')
                    lo_dec['data_date'] = self.data_day.strftime('%Y%m%d')
                    lo_dec['time_from'] = self.data.mkt_config[mkt]['open_time'].replace(':','')
                    lo_dec['time_to'] = self.data.mkt_config[mkt]['close_time'].replace(':','')
                    lo_dec['contract'] = row['contract']
                    lo_dec['currency'] = row['currency']
                    
                    for sig_key in backtest.signals[mkt].columns :
                        if sig_key != 'trade_date' and 'time' not in sig_key: 
                            lo_dec[sig_key] = row[sig_key]
                    
                    # Convert to string
                    lo_str = "'"+'/'.join([f'{k}:{v}' for k,v in lo_dec.items()])
                    
                    # Fill frame
                    lo_dat = {}
                    lo_dat['trade_date'] = lodt
                    lo_dat['theme'] = self.data.strat_config['theme']
                    lo_dat['strategy'] = self.data.strat_config['strategy']
                    lo_dat['sub_strategy'] = self.data.strat_config['sub_strategy']
                    lo_dat['underlying'] = mkt
                    lo_dat['contract'] = row['contract']
                    lo_dat['volatility'] = row['vol']
                    lo_dat['decision'] = lo_str
                    
                    mkt_locol.append(lo_dat)
                    
                
                mkt_lodf = pd.DataFrame(mkt_locol)
                mkt_lodf = mkt_lodf.set_index('trade_date')
                mkt_lodf = mkt_lodf[['theme','strategy','sub_strategy',
                           'underlying','contract','volatility','decision']]
                
                mkt_lo_data = mkt_lodf.loc[mkt_lo_date]
                
                # # Fix future trades on holidays
                # if mkt_lo_date != self.trade_day:
                #     mkt_lo_data['decision'] = mkt_lo_data['decision'].replace('signal_tf:1','signal_tf:0')
                strat_lo.loc[mkt] = mkt_lo_data
            except :
                pass
            
        return strat_lo
    
    
    def _compute_LiveHeader(self, port) :
        '''
        

        Parameters
        ----------
        port : TYPE
            DESCRIPTION.

        Returns
        -------
        None.

        '''
        # LiveHeader
        out_lh = {}
        out_lh['theme'] = self.data.strat_config['theme']
        out_lh['strategy'] = self.data.strat_config['strategy']
        out_lh['sub_strategy'] = self.data.strat_config['sub_strategy']
        out_lh['data_date'] = self.trade_day.strftime('%Y%m%d')
        out_lh['volatility'] = int(round(port.fillna(-1).loc[self.data_day,'vol'],0))
        # out_lh['volatility'] = None if out_lh['volatility'] == -1 else out_lh['volatility'] 
        out_lh['unit_risk'] = self.data.strat_config['unit_risk']
        out_lh['type'] = self.data.strat_config['type']
        out_lh['vba'] = self.data.strat_config['vba']
        out_lh['target_risk'] = out_lh['volatility']
        self.out_lh = out_lh
    
    def _compute_LiveOutput(self, port, strat_lo) :
        '''
        

        Parameters
        ----------
        port : TYPE
            DESCRIPTION.
        strat_lo : TYPE
            DESCRIPTION.

        Returns
        -------
        None.

        '''
        out_los = []
        for symbol, row in strat_lo.iterrows():
            if str(row['underlying']) != 'nan' : 
                out_los.append({k:str(v) if isinstance(v,str) else str(int(v)) if str(v) != 'nan' else "'Nan" for k, v in row.items()})
        
        self.out_los = out_los
    
    def _compute_LiveHO(self, port, strat_lo):
        '''
        

        Parameters
        ----------
        port : TYPE
            DESCRIPTION.
        strat_lo : TYPE
            DESCRIPTION.

        Returns
        -------
        None.

        '''
        
        # LiveHO - New combined LiveHeader and LiveOutput for MTS
        x_lho = {}
        x_lho['strategy_key'] = self.data.strat_config['strategy']
        x_lho['theme'] = self.data.strat_config['theme']
        x_lho['strategy'] = self.data.strat_config['strategy']
        x_lho['sub_strategy'] = self.data.strat_config['sub_strategy']
        x_lho['strategy_code'] = self.data.strat_config['strategy_code']
        x_lho['type'] = self.data.strat_config['type']
        x_lho['function'] = self.data.strat_config['function']
        x_lho['data_date'] = int(self.data_day.strftime('%Y%m%d'))
        x_lho['trade_date'] = int(self.trade_day.strftime('%Y%m%d'))
        x_lho['unit_risk'] = self.data.strat_config['unit_risk']
        x_lho['target_risk'] = self.out_lh['target_risk']
        x_lho['volatility'] = self.out_lh['volatility']
        
        # Generate liveOutput blocks
        x_lho['live_output'] = {}
        for symbol, row in strat_lo.iterrows():
            print(symbol)
            # Extract parameters from decision string
            try : 
                if isinstance(row['decision'], str) :
                    dec_pm = {k.replace("'",''):v for k,v in [x.split(':') for x in row['decision'].split('/')]}
                
                    # Build dict
                    z_lo = {}
                    z_lo['symbol'] = symbol
                    z_lo['contract'] = int(row['contract'])
                    z_lo['volatility'] = float(row['volatility'])
                   
                    # Parameters
                    z_pm = {}
                    # z_pm['data_error'] = dec_pm['data_error']
                    #z_pm['trade_date'] = int(dec_pm['trade_date'])
                    z_pm['data_date'] = int(dec_pm['data_date'].strftime('%Y%m%d'))
                    # z_pm['signal_tf'] = int(dec_pm['signal_tf'])
                    z_pm['time_from'] = dec_pm['time_from']
                    z_pm['time_to'] = dec_pm['time_to']
                    z_pm['contract'] = str(int(float(dec_pm['contract'])))
                    z_pm['currency'] = dec_pm['currency']
                    
                    for sig_key in dec_pm.keys() :
                        z_pm[sig_key] = dec_pm[sig_key]
                    
                    # Add to live output
                    z_lo['live_output'] = z_pm
                    x_lho['live_output'][symbol] = z_lo
            except :
                pass
        self.x_lho = x_lho
        self.liveho_cpp = dict_to_cpp_cfg(x_lho)
   
    def _compute_PnL(self, port) :
        '''
        

        Parameters
        ----------
        port : TYPE
            DESCRIPTION.

        Returns
        -------
        None.

        '''
        
        dates = self.data.parquet['main']['ref']['dates']['data']
        dates = pd.date_range(self.data.strat_config['legacy_date'],dates,freq='B')
        
        # Pnl
        out_pnl = port['Pnl']
        out_pnl.index.name = 'TradeDate'
        out_pnl.columns = ['Pnl']

        # Legacy pnl
        out_leg_pnl = out_pnl.copy()
        out_leg_pnl = out_leg_pnl.reindex(dates)
        out_leg_pnl.index.name = 'TradeDate'
        self.out_leg_pnl = out_leg_pnl.fillna(0).reset_index().set_index('TradeDate')

    def _compute_Positions(self) :
        '''
        

        Returns
        -------
        None.

        '''
        
        # Positions
        out_pos = []
        out_pos_ts = []
        for mkt in self.markets:
            # try :
            temp = self.positions[mkt].groupby('trade_date')['pos'].last()
            temp.name = mkt
            out_pos.append(temp)
            temp = self.positions[mkt].groupby('trade_date')[['pos', 'qty', 'Pnl', 'symbol','contract']].agg({
                        'pos' : 'last',
                        'qty' : 'sum',
                        'Pnl' : 'sum',
                        'symbol' : 'first',
                        'contract' : 'first'
                        })
            out_pos_ts.append(temp)
            # except :
            #     pass

        out_pos = pd.concat(out_pos,axis=1).fillna(0)
        out_pos.index.name = 'TradeDate'
        self.out_pos = out_pos
        
        
        # PositionsTS        
        out_pos_ts = pd.concat(out_pos_ts,axis=0)
        out_pos_ts = out_pos_ts.reset_index()
        out_pos_ts = out_pos_ts.rename(columns={
                    'trade_date':'TradeDate',
                    'symbol':'Symbol',
                    'contract':'Contract',
                    'qty':'Signal',
                    'pos':'Position',
                    'Pnl':'Pnl'})
        
        out_pos_ts = out_pos_ts[['TradeDate','Symbol','Contract', 'Signal','Position','Pnl']]
        out_pos_ts['Pnl'] = out_pos_ts['Pnl'].round()
        self.out_pos_ts = out_pos_ts
    
    
    def _compute_ExecutionLogs(self) :
        '''
        

        Returns
        -------
        None.

        '''

        exlogs = []
        for mkt in self.markets :
            try :
                df = self.positions[mkt][['utc', 'trade_date', 'symbol', 'contract', 'qty', 'pos', 'px']]
                df['exqty'] = df['pos']
                exlogs.append(df[['utc', 'trade_date', 'symbol', 'contract', 'qty', 'pos','exqty', 'px']])
            except :
                pass
        exe_log = pd.concat(exlogs, axis=0)

        # Execution log df
        exe_log.columns = ['TimeStamp','TradeDate', 'Symbol', 'Contract',
                   'DesiredSignal','DesiredPosition',
                   'ExecutionQuantity','ExecutionPrice']
        
        self.exe_log = exe_log
        
    def _compute_outputs(self, cost) :
        '''
        

        Parameters
        ----------
        cost : TYPE
            DESCRIPTION.

        Returns
        -------
        None.

        '''
        self.port = port = self.compute_portfolio()
        self.strat_lo = strat_lo = self.compute_strats_lo()
        
        self._compute_LiveHeader(port)
        self._compute_LiveOutput(port, strat_lo)
        self._compute_LiveHO(port, strat_lo)
        self._compute_PnL(port)
        self._compute_Positions()
        self._compute_ExecutionLogs()
        
    
    def dump_outputs(self, prod=True) :
        '''
        

        Parameters
        ----------
        prod : TYPE, optional
            DESCRIPTION. The default is True.

        Returns
        -------
        None.

        '''
        path_data = self.data.strat_config['path_data']
        strategy_key = self.data.strat_config['strategy_key']
        liveho_fn = liveho_fn = f'LiveHO_{strategy_key}'
        
        liveheader_txt(self.out_lh, strategy_key, path_data)
        liveoutput_txt(self.out_los, strategy_key, path_data)
        pnl_txt(self.out_leg_pnl, strategy_key, path_data)
        positions_txt(self.out_pos, strategy_key, path_data)
        positionsts_txt(self.out_pos_ts, strategy_key, path_data)
        exelog_txt(self.exe_log, strategy_key, path_data)
        
        #Dump Config/Yaml file
        with open(os.path.join(path_data, liveho_fn + f'.cfg'),'w') as fw:
            fw.write(self.liveho_cpp)  
        with open(os.path.join(path_data, liveho_fn + f'.yaml'),'w') as fw:
            stream = yaml.safe_dump(self.x_lho, sort_keys=False, default_flow_style=False)
            fw.write(stream.replace("'",''))

        if prod:
            prod_loc = self.data.strat_config['prod_loc']
            liveheader_txt(self.out_lh, strategy_key, os.path.join(prod_loc,'LiveHeader'))
            liveoutput_txt(self.out_los, strategy_key, os.path.join(prod_loc,'LiveOutput'))
            pnl_txt(self.out_pnl, strategy_key, os.path.join(prod_loc,'Pnl'))
            positions_txt(self.out_pos, strategy_key, os.path.join(prod_loc,'Positions'))
            positionsts_txt(self.out_pos_ts, strategy_key, os.path.join(prod_loc,'PositionsTS'))
            exelog_txt(self.exe_log, strategy_key, os.path.join(prod_loc,'ExecutionLog'))
            
            #Dump Config/Yaml file
            with open(os.path.join(prod_loc, 'LiveHO', liveho_fn+'.cfg'), 'w') as fw:
                fw.write(self.liveho_cpp)
            with open(os.path.join(prod_loc, 'LiveHO', liveho_fn+'.yaml'), 'w') as fw:
                stream = yaml.safe_dump(self.x_lho, sort_keys=False, default_flow_style=False)
                fw.write(stream.replace("'",''))
        
        
    def scp_states(self) :
        '''
        

        Returns
        -------
        None.

        '''
        print(f'MTS SSH - Start - {datetime.now().isoformat()}')
        
        path_data = self.data.strat_config['path_data']
        strategy_key = self.data.strat_config['strategy_key']
        liveho_fn = liveho_fn = f'LiveHO_{strategy_key}'
        
        liveho_path_yaml = os.path.join(path_data,liveho_fn+'.yaml')
        liveho_path_cpp = os.path.join(path_data,liveho_fn+'.cfg')
        
        for mts_host in self.strat_config['mts_hosts']:

            ssh = paramiko.SSHClient()
            ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            ssh.connect(hostname=mts_host,
                        username=self.strat_config['mts_user'],
                        key_filename=self.strat_config['mts_keyfp'])
        
            sftp = ssh.open_sftp()
            sftp.put(liveho_path_yaml, self.strat_config['mts_path'] + liveho_fn + '.yaml')
            sftp.put(liveho_path_cpp, self.strat_config['mts_path'] + liveho_fn + '.cfg')
            sftp.close()
            
        return 
        
        
        
if __name__ == '__main__' :
    
    date = datetime.datetime.now().date()
    
    strat_data = Strat_Data(date, {}, True)
    backtest = Strat_Backtest(strat_data, date, 'vre', '') 
    backtest._backtest()

    # Compute outputs and dump it
    backtest._compute_outputs(cost=0)
    backtest.dump_outputs(prod=False)
    
    #SCP yam/ cfg
    # backtest.scp_states()
    
    
    
    
 
