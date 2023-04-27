# -*- coding: utf-8 -*-
import pandas as pd
import numpy as np
from utils.utilsRsch import *
import datetime
from pandas.tseries.offsets import BDay
import time

def generate_signal(mkt, contract, bars, twap, pickle, vop, config, param) :
        
    '''
    Parameters
    ----------
    mkt : TYPE
        DESCRIPTION.
    utc : TYPE
        DESCRIPTION.
    bars : TYPE
        DESCRIPTION.
    today_bars : TYPE
        DESCRIPTION.
    pickle : TYPE
        DESCRIPTION.
    config : TYPE
        DESCRIPTION.
    param : TYPE
        DESCRIPTION.

    Returns
    -------
    TYPE
        DESCRIPTION.

    '''

    def get_vre_pos(signal, close, close_lb, vop, cmtcap, typology='ma'):
        ''' 
            Compute Position for VRE if the net is not None
        '''
        pxchg = close.diff()
        if typology =='ma':
            dailyvol = pxchg.rolling(close_lb, min_periods=5).std()
        elif typology == 'lma':
            dailyvol = lwmstd(pxchg, close_lb)
        
        return (signal * cmtcap / (2 * dailyvol * vop)).round()
    
    def get_df_hour(df, hour) :
        df['hour'] = df.index.hour
        df['min'] = df.index.minute
        df = df[(df['hour'] == int(hour[:2])) & (df['min'] == int(hour[3:5]))]
        del df['hour'], df['min']
        return df
    mkt_contract = mkt + contract
    
    # Date transformation
    bars = bars.set_index('bar_datetime')
    twap = twap.set_index('bar_datetime')
    
    
    #Calculate QV
    now = time.time()
    qv = calcqv(bars['close'])
    qv_grid = qv.resample(param['resample_grid']).asfreq().ffill()    
    qv = qv[qv.index.strftime('%H:%M:%S') == config['eval_time']]#.to_frame() 
    
    #Take bars at eval time
    df = get_df_hour(bars,config['eval_time'] )

    #Twap
    twap = get_df_hour(twap,config['eval_time'] )    

    #Vop
    vop = vop.reindex(index=df.index.date)

    
    VREvar_sum = pd.DataFrame(index=df.index)
    stoploss_sum = pd.DataFrame(index=df.index)
    adjvol_sum = pd.DataFrame(index=df.index)
    
    for ll in config['band_lb']:
        for nn in config['band_numstd'] :
            
            VREvar = getBollinfo(df['close'], ll, numStdev = nn).sort_index()
            
            # get day-ago band info
            for k in range(0, VREvar.shape[1]):
                VREvar[VREvar.columns[k] + '_1'] = VREvar.iloc[:,k].shift(1)

            VREvar['band_fastma'] = VREvar['bandwidth'].rolling(param['band_hist_short_lb'], min_periods=2).mean()
            VREvar['band_slowma'] = VREvar['bandwidth'].rolling(param['band_hist_long_lb'], min_periods=2).mean()
      
    
            # bandTightFlag = VREvar['band_fastma'] < band_width_compratio * VREvar['band_slowma']
            expansionFlag = (VREvar['upperB'] >= VREvar['upperB_1']) & (VREvar['lowerB'] <= VREvar['lowerB_1'])
            
            VREvar['net'] = 0
            VREvar.loc[expansionFlag, 'net'] = VREvar.loc[expansionFlag, 'BO']
            VREvar['net'] = zeroOutStringOfNets(VREvar['net'],  param['timeStopPeriod'])
           
            # get stop loss level
            long_idx = (VREvar['net'] > 0)
            short_idx = (VREvar['net'] < 0)
                
            VREvar['stopL'] = 0
            VREvar.loc[long_idx,'stopL'] = VREvar.loc[long_idx,'upperB'] - param['stopL_ratioBandwidth'] * VREvar.loc[long_idx,'band_fastma'] 
            VREvar.loc[short_idx,'stopL'] = VREvar.loc[short_idx,'lowerB'] + param['stopL_ratioBandwidth'] * VREvar.loc[short_idx,'band_fastma']

            VREvar_sum['ll'+str(ll)+'sd'+str(nn)] = VREvar['net']
            stoploss_sum['ll'+str(ll)+'sd'+str(nn)] = VREvar['stopL']
            adjvol_sum['ll'+str(ll)+'sd'+str(nn)] = VREvar['adjvol']
                   
            
    VREvar_sum = VREvar_sum.fillna(0)
    net_stop =  VREvar_sum.mean(axis=1).to_frame()   
    net_stop.rename(columns={net_stop.columns[0]: 'avgnet'}, inplace=True)
    net_stop['net'] = net_stop['avgnet'].mask(np.abs(net_stop['avgnet']) < param['VREavgnet_thresh'], 0)
    
    VREvar_cp = np.abs(VREvar_sum).copy()
    VREvar_cp = VREvar_cp.replace(0, np.nan)
    
    # for setting stops
    stoploss_use = pd.DataFrame(stoploss_sum.values * VREvar_cp.values, columns = stoploss_sum.columns, index = stoploss_sum.index)
    net_stop['stopL'] = stoploss_use.mean(axis = 1) 
    

    # sizing the net
    net_stop['close'] = df['close'] 
    net_stop['qv'] = qv
    net_stop['qv'] = net_stop['qv'].ffill()
    net_stop['noiseratio'] = net_stop['qv'].rolling(param['band_hist_short_lb']).mean().shift(1) / net_stop[
                                    'qv'].rolling(param['normalize_lb'], min_periods=100).mean().shift(1)
    
    net_stop.loc[(net_stop['noiseratio'] > param['noiseratio_thresh']), 'net'] = 0            # truncate the net
    net_stop['net'] = net_stop['net'] * (1 + np.exp(param['unitnet_scale'])) / (
                    1 + np.exp(param['unitnet_scale'] * net_stop['noiseratio']))
    

    ## implementing ts stop loss
    stop_wk = net_stop[['close','stopL','net']]
    del net_stop
    # stop_wk = stop_wk.rename(columns={'net_qvadj':'net'})
    
    # risk is always negative
    stop_wk['risk'] = stop_wk['net'] * (stop_wk['stopL'] - stop_wk['close'])   
    stop_wk.loc[stop_wk['net'] !=0, 'reward'] = - param['riskreward_ratio'] * stop_wk.loc[stop_wk['net'] !=0, 'risk']
    stop_wk['trailL'] = stop_wk['close'] + stop_wk['net'] * stop_wk['reward']
    
    stop_wk['netFF'] = replaceAndFill(stop_wk['net'], param['timeStopPeriod']-1)   # exclude last day
    stop_wk['vop'] = vop #.values
    

    stop_wk['pos'] = get_vre_pos(stop_wk['net'], stop_wk['close'], 20, stop_wk['vop'], param['capital']*param['alloc']*param['leverage'])
    stop_wk['stopL'] = replaceAndFill(stop_wk['stopL'], param['timeStopPeriod'])
    stop_wk['trailL'] = replaceAndFill(stop_wk['trailL'], param['timeStopPeriod'])


    # trailstop only assess at the end of day, should we move to intra-day
    stop_wk['trailIdx'] = 1 * (stop_wk['netFF'].multiply(stop_wk['close']) > 
                          stop_wk['netFF'].multiply(stop_wk['trailL']))
    trailUpdateIdx = ((stop_wk['trailIdx'].rolling(param['timeStopPeriod']).sum() >= 1) & (stop_wk['stopL'] != 0))
    stop_wk.loc[trailUpdateIdx, 'stopL'] = stop_wk.loc[trailUpdateIdx, 'trailL']

    stop_wk['fillpx'] = twap     
    stop_wk.loc[stop_wk['fillpx'].isnull(), 'fillpx'] = stop_wk.loc[stop_wk['fillpx'].isnull(), 'close']
            
     
    stop_wk['trade_date'] = pd.to_datetime(stop_wk.index.date)
    stop_wk['bar_datetime'] = stop_wk.index
    
    #For dailybacktest speedup
    stop_wk['stopL_prevday'] = stop_wk['stopL'].shift()
    return stop_wk



def run_bar(cur_ko, mkt, contract, histbars, todaysbars, latestbars, lastest_state, mkt_config, strat_config):
    '''
    

    Parameters
    ----------
    cur_ko : TYPE
        DESCRIPTION.
    mkt : TYPE
        DESCRIPTION.
    contract : TYPE
        DESCRIPTION.
    histbars : TYPE
        DESCRIPTION.
    todaysbars : TYPE
        DESCRIPTION.
    latestbars : TYPE
        DESCRIPTION.
    lastest_state : TYPE
        DESCRIPTION.
    mkt_config : TYPE
        DESCRIPTION.
    strat_config : TYPE
        DESCRIPTION.

    Returns
    -------
    pos
        position for mts.

    '''
    if cur_ko == mkt_config['eval_time'] :
        bars = pd.concat([histbars, todaysbars])
        sig = generate_signal(mkt, contract, bars, parquet, mkt_config, strat_config)
        sig = sig.iloc[-1]
        
        if sig['pos'].fillna(0) != 0 :
            return pos
        
        else :
            if latest_state['pos'] != 0 and latest['state_entry_date'] - today > 4 :
                pos = -1*latest_state['pos'] 
            
            else :
                return
        
    else :
        if lastest_state['pos'] == 0 :
            return 
        else :
            sign = np.sign(lastest_state['pos'])
            if sign * latestbars['close'] < sign*lastest_state['stopL'] :
                return -1*lastest_state['pos']
            else :
                return 


def run_daily_strat(day, signals, bars, pos) :
    '''
    This function is for backtest, assuming we have computed the signal for everyday on the historical data
    Enter/Exit Position based on todays bars and signals

    Parameters
    ----------
    day : TYPE
        DESCRIPTION.
    signals : TYPE
        DESCRIPTION.
    bars : TYPE
        DESCRIPTION.

    Returns
    -------
    px : TYPE
        DESCRIPTION.
    pos : TYPE
        DESCRIPTION.
    utc : TYPE
        DESCRIPTION.

    '''   
    positions = {'qty' : [], 'px' : [], 'px_twap' : [], 'utc' : [], 'trade_date' : []}
     
    sig = signals[signals['trade_date'] == day]
    
    if sig.shape[0] > 0 :
        #Run stopLoss
        if len(pos['pos']) > 0 and pos['pos'][-1] != 0 :
            p = pos['pos'][-1]
            #Get signal calculated yesterday for today's trading
            sign = np.sign(p)
            stopFlag =  sign * bars['close'] < sign*sig['stopL_prevday'].iloc[0]
            
            bars = bars.loc[stopFlag]
            if bars.shape[0] > 0 :
                positions['px'].append(bars['close'].iloc[0])
                positions['px_twap'].append(bars['close'].iloc[0])
                positions['qty'].append(-1*p)
                positions['utc'].append(bars['bar_datetime'].iloc[0])
                positions['trade_date'].append(day)
        
    
        #Update State and Pos
        # Get signal calculated for today and see if we need to update pos
        if sig['pos'].fillna(0).iloc[0] != 0 :
            positions['px'].append(sig['close'].iloc[0])
            positions['px_twap'].append(sig['fillpx'].iloc[0])
            positions['qty'].append(sig['pos'].iloc[0])
            positions['utc'].append(sig['bar_datetime'].iloc[0])
            positions['trade_date'].append(day)
            
        
        else :
            #Close position if more than 4 days
            if len(pos['trade_date']) > 1 and (day - pos['trade_date'][-1]).days  > 4 : 
                positions['px'].append(sig['close'].iloc[0])
                positions['px_twap'].append(sig['fillpx'].iloc[0])
                positions['qty'].append(-1*pos['pos'][-1])
                positions['utc'].append(sig['bar_datetime'].iloc[0])
                positions['trade_date'].append(day)
            
            #Keep previous position if less than 4 days
            if len(pos['trade_date']) > 1 and (day - pos['trade_date'][-1]).days  <= 4 : 
                positions['px'].append(sig['close'].iloc[0])
                positions['px_twap'].append(sig['fillpx'].iloc[0])
                positions['qty'].append(0)
                positions['utc'].append(sig['bar_datetime'].iloc[0])
                positions['trade_date'].append(day)
        
    return positions
        
 
