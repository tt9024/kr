# -*- coding: utf-8 -*-
"""
Created on Wed Mar 16 12:26:03 2022

@author: jian
"""

import numpy as np
import pandas as pd
import datetime as dt
import statsmodels.api as sm
import copy
#%%
def takeOutWeekends(foo, FridayEndTime = dt.time(17, 51), SundayStartTime = dt.time(18,00)):
    # works not on minute day, but on resampled (eg. hourly) data which introduce nan's
    foo.index = pd.to_datetime(foo.index)
    foo = foo[~((foo.index.dayofweek == 4) & (foo.index.time >= FridayEndTime))]
    foo = foo[~(foo.index.dayofweek == 5)]
    foo = foo[~((foo.index.dayofweek == 6) & (foo.index.time <= SundayStartTime))]
    return foo

def corrFromZero(x,y):
    result = np.dot(x,y)
    norm_x = np.sqrt(x.dot(x))
    norm_y = np.sqrt(y.dot(y))
    return result/norm_x/norm_y

def corrFromZero_mat(x, df):
    # x is an array with two columns
    x1 = df.loc[x.index].iloc[:,0]
    x2 = df.loc[x.index].iloc[:,1]
    result = np.dot(x1,x2)
    norm_x = np.sqrt(x1.dot(x1))
    norm_y = np.sqrt(x2.dot(x2))
    return result/norm_x/norm_y
 
def autoCorrFromZero(x):    
    result= np.dot(x[:-1], x[1:]) 
    return result / np.sqrt(x[:-1].dot(x[:-1])) / np.sqrt(x[1:].dot(x[1:]))
    
def corrFromZero_df(x):
    result = np.dot(x.iloc[:0],x.iloc[:1])
    norm_x = np.sqrt(x.iloc[:0].dot(x.iloc[:0]))
    norm_y = np.sqrt(x.iloc[:1].dot(x.iloc[:1]))
    return result/norm_x/norm_y    


def getCondRegrStats(regrdf, gridpoint=[0,1,2,3]):
    # df has two columns,first is dep, 2nd is
    # kind of ugly    
    # regrdf = pnl_z
    result = []  
    foo = regrdf.loc[regrdf.iloc[:,1] >= gridpoint[3],:]
    x = foo.iloc[:,1]
    x = sm.add_constant(x)
    regrmodel = sm.OLS(foo.iloc[:,0], x).fit()
    result.append([foo.iloc[:,0].mean(axis=0), regrmodel.params[1], regrmodel.tvalues[1]])
    
    foo = regrdf.loc[(regrdf.iloc[:,1] >= gridpoint[2]) & (regrdf.iloc[:,1] < gridpoint[3]),:]
    x = foo.iloc[:,1]
    x = sm.add_constant(x)
    regrmodel = sm.OLS(foo.iloc[:,0], x).fit()
    result.append([foo.iloc[:,0].mean(axis=0), regrmodel.params[1], regrmodel.tvalues[1]])

    foo = regrdf.loc[(regrdf.iloc[:,1] >= gridpoint[1]) & (regrdf.iloc[:,1] < gridpoint[2]),:]
    x = foo.iloc[:,1]
    x = sm.add_constant(x)
    regrmodel = sm.OLS(foo.iloc[:,0], x).fit()
    result.append([foo.iloc[:,0].mean(axis=0), regrmodel.params[1], regrmodel.tvalues[1]])

    foo = regrdf.loc[(regrdf.iloc[:,1] >= gridpoint[0]) & (regrdf.iloc[:,1] < gridpoint[1]),:]
    x = foo.iloc[:,1]
    x = sm.add_constant(x)
    regrmodel = sm.OLS(foo.iloc[:,0], x).fit()
    result.append([foo.iloc[:,0].mean(axis=0), regrmodel.params[1], regrmodel.tvalues[1]])

    foo = regrdf.loc[(regrdf.iloc[:,1] >= -gridpoint[1]) & (regrdf.iloc[:,1] < gridpoint[0]),:]
    x = foo.iloc[:,1]
    x = sm.add_constant(x)
    regrmodel = sm.OLS(foo.iloc[:,0], x).fit()
    result.append([foo.iloc[:,0].mean(axis=0), regrmodel.params[1], regrmodel.tvalues[1]])

    foo = regrdf.loc[(regrdf.iloc[:,1] >= -gridpoint[2]) & (regrdf.iloc[:,1] < -gridpoint[1]),:]
    x = foo.iloc[:,1]
    x = sm.add_constant(x)
    regrmodel = sm.OLS(foo.iloc[:,0], x).fit()
    result.append([foo.iloc[:,0].mean(axis=0), regrmodel.params[1], regrmodel.tvalues[1]])

    foo = regrdf.loc[(regrdf.iloc[:,1] >= -gridpoint[3]) & (regrdf.iloc[:,1] < -gridpoint[2]),:]
    x = foo.iloc[:,1]
    x = sm.add_constant(x)
    regrmodel = sm.OLS(foo.iloc[:,0], x).fit()
    result.append([foo.iloc[:,0].mean(axis=0), regrmodel.params[1], regrmodel.tvalues[1]])

    foo = regrdf.loc[(regrdf.iloc[:,1] < -gridpoint[3]),:]
    x = foo.iloc[:,1]
    x = sm.add_constant(x)
    regrmodel = sm.OLS(foo.iloc[:,0], x).fit()
    result.append([foo.iloc[:,0].mean(axis=0), regrmodel.params[1], regrmodel.tvalues[1]])

    result_df = pd.DataFrame(result)
    result_df.columns=['avg_y', 'beta','tvalue']
    result_df.index = ['>= '+str(gridpoint[3]),\
                       '[' + str(gridpoint[2]) + ', ' + str(gridpoint[3]) + ')',\
                       '[' + str(gridpoint[1]) + ', ' + str(gridpoint[2]) + ')',\
                       '[' + str(gridpoint[0]) + ', ' + str(gridpoint[1]) + ')',\
                       '[-' + str(gridpoint[1]) + ', ' + str(gridpoint[0]) + ')',\
                       '[-' + str(gridpoint[2]) + ', -' + str(gridpoint[1]) + ')',\
                       '[-' + str(gridpoint[3]) + ', -' + str(gridpoint[2]) + ')',\
                       '< -' + str(gridpoint[3])]
    return result_df
        
def getNormNets(x, lb):
    # x is a pd.dataframe
    x_mag = abs(x)
    rollmeanx = x_mag.rolling(250, min_periods=125).mean()
    x_adj = x * 0.5 / rollmeanx
    return x_adj

def getMaxDD(dailyRtn):
    # dataframe indexed by datetime
    x = dailyRtn.to_numpy()
    eqty = x.cumsum()
    DD = np.maximum.accumulate(eqty) - eqty
    maxDD = np.max(DD)
    i = np.argmax(np.maximum.accumulate(eqty) - eqty)
    j = np.argmax(eqty[:i])
    return [maxDD, dailyRtn.index[j], dailyRtn.index[i]]
    #return [maxDD, dailyRtn.index[j].strftime('%Y-%m-%d'), dailyRtn.index[i].strftime('%Y-%m-%d')]

def lwma(df, n=10):
    wts = np.arange(1,n+1)
    ans = df.rolling(n).apply(lambda x: np.dot(x, wts)/wts.sum(), raw=True)
    return ans

def lwmstd(df, n=10):
    wts = np.arange(1,n+1)
    lwavg = df.rolling(n).apply(lambda x: np.dot(x, wts)/wts.sum(), raw=True)
    ans = df.rolling(n).apply(lambda x: np.dot(np.power(x,2), wts)/wts.sum(), raw=True)    
    ans2 = ans - lwavg**2
    ans2 = ans2 * n / (n-1)
    ans2 = ans2**(0.5)
    return ans2

def lwmRWI(df, n=10):
    wts = np.arange(1,n+1)
    lwavg = df.rolling(n).apply(lambda x: np.dot(x, wts)/wts.sum(), raw=True)
    ans = df.rolling(n).apply(lambda x: np.dot(np.power(x,2), wts)/wts.sum(), raw=True)    
    ans2 = ans - lwavg**2
    ans2 = ans2 * n / (n-1)
    ans2 = ans2**(0.5)
    return lwavg / ans2

def lwmRWI_s(foo):
    foo = np.array(foo)
    foo = foo.flatten()
    n = len(foo)
    wts = np.arange(1,n+1)
    lwavg = np.dot(foo, wts)/wts.sum()
    ans = np.dot(np.power(foo,2), wts)/wts.sum()    
    ans2 = ans - lwavg**2
    ans2 = ans2 * n / (n-1)
    ans2 = ans2**(0.5)
    return lwavg / ans2


def useColAsDtIndex(df, colName):
    df = df.rename(columns={colName:'Index'})
    df['Index'] = pd.to_datetime(df['Index'])
    df.set_index('Index', inplace=True)
    return df
    
def getSmoothDailyRtn(df, lb, useLogPrice=True):
    # appropriate for index or FX, use something else for futures
    if useLogPrice:
        df = np.log(df)
    pxdiff = df.diff()
    result = pxdiff.ewm(span=lb, min_periods=lb, adjust=False, ignore_na=True).mean()
    return result 

def getNormRtn(df, winLb):
    # df are price series, not pxdifff, use px diff (appropriate for fut),  not pctrtn    
    pxdiff = df.diff()
    normrtn = pxdiff / pxdiff.rolling(winLb).std()
    normrtn = normrtn.dropna()
    normrtn = pd.DataFrame(normrtn)
    return normrtn
# def calcQV
# def calcVelocity

def zeroOutStringOfNets(foo, window_size):
    # for example 4 for 4 days for daily model  
    foo = pd.DataFrame(foo)
    foo.rename(columns={foo.columns[0]: 'current'}, inplace=True)
    foo_adj = foo.copy()
    for i in range(0, window_size):
        foo['netlag' + str(i+1)] = foo['current'].shift(i+1)
    stringFlag = np.sign(foo['current']) == np.sign(foo.iloc[:,1:].sum(axis=1))
    foo_adj.loc[stringFlag,'current'] = foo_adj.loc[stringFlag,'current'].apply(
        lambda x: 0 if (x != 0) else x)
    return foo_adj

def replaceAndFill(foo_orig, window_size):
    foo = foo_orig.copy() 
    foo = foo.replace(0, np.nan)
    foo = foo.ffill(limit = window_size)
    foo = foo.replace(np.nan, 0)
    return foo

def applyTimeStops(foo, window_size):
    # for example 4 for 4 days for daily model  
    # foo = copy.deepcopy(net)
    foo.columns = ['current']
    origNanIdx = foo['current'].isna()
    foo['current'] = foo['current'].replace(0, np.nan)
    foo['current'] = foo['current'].ffill(limit = window_size-1)
    foo['current'] = foo['current'].replace(np.nan, 0)
    foo.loc[origNanIdx,'current'] = np.nan
    return foo

def calcRollingWindlowHiloRange(foo, Hilo_lb):
    # foo = copy.deepcopy(df)
    rollingHi = foo.rolling(Hilo_lb, min_periods=round(Hilo_lb/2)).max()
    rollingLo = foo.rolling(Hilo_lb, min_periods=round(Hilo_lb/2)).min()
    return pd.DataFrame(rollingHi - rollingLo)
    
def getQVforPos(qv_24h_df, eval_time, qv_avg = 3, floor_lb = 250, floor_quantile = 0.25, ffill_limit=3):
    qv_use = qv_24h_df[qv_24h_df.index.strftime("%H:%M:%S") == eval_time] 
        # remove Saturdays, most Fridays and Sundays should not be nans
    qv_use = qv_use[~(qv_use.index.dayofweek == 5)] 
    #chk = qv_smooth[qv_smooth.isna()]
    qv_floor = qv_use.rolling(floor_lb, min_periods = 100).quantile(0.25)
    qv_smooth = qv_use.rolling(qv_avg, min_periods = 1).mean()
    result = pd.concat([qv_floor, qv_smooth]).max(level=0)
    result = result.ffill(limit = ffill_limit)
    return result

def calcVOP(mkt, p_mult, p_ref, p_fx):
    # p_ref = pydcs['ref']['markets'].copy()
    # p_fx = pydcs['fx']['spot']
    currency = p_ref.loc[p_ref.index == mkt,'currency'].item()
    # always get EURUSD, for time grid

    if mkt == 'SPX':
        p_mult['SPX_N1'] = p_mult['SPX_N1'].replace(250, 50)
    if currency == "USD":
        vop = p_mult[mkt+'_N1']
    else:
        spot_fx = p_fx[currency]
        vop = spot_fx * p_mult[mkt+'_N1']
    return vop

def applyMaxContract(df_pos, df_max_ctr):
    # df_pos, df_max_ctr should have the number of order of columns 
    # df_pos = copy.deepcopy(bt_pos)
    # df_max_ctr = copy.deepcopy(max_ctr)   
    pos_adj = pd.DataFrame(index=df_pos.index)
    for i in range(0, len(df_pos.columns)):
        pos_use = pd.DataFrame(df_pos.iloc[:,i])
        max_ctr_use = pd.DataFrame(df_max_ctr.iloc[:,i])
        mkt_name = pos_use.columns[0]
        max_ctr_use.rename(columns={max_ctr_use.columns[0]: 'max_ctr'}, inplace=True)
        foo = pos_use.join(max_ctr_use)
        foo['max_ctr'] = foo['max_ctr'].ffill()
        foo['new_pos'] = np.sign(foo.iloc[:,0]) * np.minimum(foo.iloc[:,0].abs(), foo['max_ctr'])
        pos_adj[mkt_name] = foo['new_pos']
    return pos_adj
    
def getPerfSum(port_results, CAPITAL):
    # port_results = copy.deepcopy(port)
    port_rtn = 252 * port_results['pnl'].mean() / CAPITAL
    net_rtn = 252 * port_results['net'].mean() / CAPITAL    
    port_vol = np.sqrt(252) * port_results['pnl'].std() / CAPITAL 
    net_vol = np.sqrt(252) * port_results['net'].std() / CAPITAL 
    port_sr = port_rtn / port_vol
    net_sr = net_rtn / net_vol    
    maxDD_list = getMaxDD(port_results['net'])
    maxDD = maxDD_list[0] / CAPITAL
    maxDDasvol = maxDD/net_vol
    skewness = port_results['net'].skew()
    skewness10 = port_results['net'].rolling(10, min_periods = 2).mean().skew()
    kurtosis = port_results['net'].kurtosis()
    
   #
    #print('port_rtn: ', str(round(port_rtn, 3)))
    #print('net_rtn :', str(round(net_vol, 3)))     #, port_vol, net_vol, port_sr, net_sr, maxDD, maxDDdur.days])
    print(round(port_rtn, 3))
    print(round(net_rtn, 3)) 
    print(round(port_vol, 3)) 
    print(round(net_vol, 3))
    print(round(port_sr, 3))
    print(round(net_sr, 3))
    print(round(maxDD, 3)) 
    print(round(maxDDasvol, 1))
    print(round(skewness, 3))
    print(round(skewness10, 3))
    print(round(kurtosis, 3))
    #print(maxDDdur.days) 
    
    
def getHitWLratio(foo):
    # foo ia 2 column array, first being sign of sign, second the price change, p/l
    n_trades = foo.shape[0]
    win_idx = (np.sign(foo[:,0]) == np.sign(foo[:,1]))
    hit_rate = len(foo[win_idx,:]) / n_trades 
    pl_win = np.mean(foo[win_idx,0] * foo[win_idx,1]) 
    pl_loss = np.mean(foo[~win_idx,0] * foo[~win_idx,1]) 
    wl_ratio = -pl_win / pl_loss 
    return np.array([n_trades, hit_rate, wl_ratio])
    
    
def dailyStoploss(df_series):
    # array has three columns for closes, stoplevel and signal/pos    
    # foo = chk['1999-11-04':"1999-11-10"].to_numpy()
    foo = df_series.to_numpy()
    px_t = 0
    stop_t = 1
    pos_t = 2
    s = np.sign(foo[:, pos_t])
    stopflag = s * foo[:, px_t] < s * foo[:, stop_t]
    firststop = np.where(stopflag)[0][0]
    return firststop

def getCorr2EquityLine(eq1, eq2):
    # both need to be dfs with datetimeindex to be aligned
    # eq1 = eqty_sig1.copy()
    # eq2 = eqty2.copy()
    mergedEq = pd.merge(eq1.diff(), eq2.diff(), how = 'inner', left_index=True, right_index=True)
    mergedEq = mergedEq.replace(np.nan, 0)
    return np.corrcoef(mergedEq.iloc[:,0].to_numpy(), mergedEq.iloc[:,1].to_numpy())
    
def calcDailyPos(signal,close,close_lb,vop,cmtcap,typology='ma'):
    pxchg = close.diff()
    if typology =='ma':
        dailyvol = pxchg.rolling(close_lb, min_periods=5).std()
    elif typology == 'lma':
        dailyvol = lwmstd(pxchg, close_lb)
# =============================================================================
#     dailyvol_usd = pd.merge(dailyvol, vop, how='left', left_index=True, \
#                                 right_index=True)      
# =============================================================================
    return (signal * cmtcap / (2 * dailyvol * vop)).round()
  



  
def calcIntradayPos(signal,close,close_lb,vop,cmtcap,grid_size):
    # grid size in minutes
    pxchg = close.diff()
    dailyvol = np.sqrt(24 * 60/grid_size)* pxchg.rolling(close_lb, min_periods=5).std()
# =============================================================================
#     dailyvol_usd = pd.merge(dailyvol, vop, how='left', left_index=True, \
#                                 right_index=True)      
# =============================================================================
    return (signal * cmtcap / (2 * dailyvol * vop)).round()

def calcpl(pos, close, fillpx, vop, ticksize, cost = 0):
    order = pos.diff()
    pl = (pos.shift(1) * close.diff() + order * (close - fillpx)) * vop
    costs = np.abs(order) * cost * ticksize * vop
    return pl - costs


def calcPEMAsignal(df, lb, normLb):
    pema = df - df.ewm(span=lb, min_periods = 2).mean()
    signal = pema / pema.rolling(normLb, min_periods = 5).std()
    return np.arctan(signal / 3)

        



