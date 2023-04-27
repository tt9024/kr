# -*- coding: utf-8 -*-
"""
Created on Tue Mar 29 16:06:58 2022

@author: jian
"""

import os
# os.chdir('P:\\Rsh\\Model1')
import sys
import pickle
import yaml
from datetime import datetime, timedelta
import datetime as dt
from argparse import ArgumentParser
import numpy as np
import pandas as pd
import scipy.stats as scs
from scipy.stats import norm
import copy

import pywt
import matplotlib.pyplot as plt
from utils.utilsGen import *

def getBollinfo(df, lb, numStdev=1.5):

    df.columns = ['close']
    df = df.reset_index()
    df.columns = ['bar_datetime','close']
    bandwidth = df.rolling(lb).std()
    df['center']= df.ewm(span=lb, min_periods=5, adjust=False, ignore_na=True).mean()
    df['bandwidth'] = bandwidth.multiply(numStdev)
    df['adjvol'] = bandwidth * np.sqrt(20/lb)          # vol standardized to 20d lb
    df['upperB'] = df['center'] +  df['bandwidth']
    df['lowerB'] = df['center'] -  df['bandwidth']
    breakUp = df['close'] > df['upperB']
    breakDn = df['close'] < df['lowerB'] 
    df['BO'] = breakUp.astype(int) - breakDn.astype(int) 
    return df.set_index('bar_datetime')

def getBollinfo_asym(df, lb, numStdev=1.5):
    df.columns = ['close']
    bandwidth= df.rolling(lb).std()
    df['center']= df.ewm(span=lb, min_periods=5, adjust=False, ignore_na=True).mean()
    df['bandwidth'] = bandwidth * numStdev
    df['adjvol'] = bandwidth * np.sqrt(20/lb)          # vol standardized to 20d lb
    df['upperB'] = df['center'] + 0.9 * df['bandwidth']
    df['lowerB'] = df['center'] - 1.1 * df['bandwidth']
    breakUp = df['close'] > df['upperB']
    breakDn = df['close'] < df['lowerB'] 
    df['BO'] = breakUp.astype(int) - breakDn.astype(int)      
    return df

def calcqv(df):
    # df is a panda dataframe/series with minute roll-adjusted prices
    # returns 24-hr qv on an hourly grid
    diff5min = df - df.shift(5)
    qv = (diff5min**2).rolling(60, min_periods=35).mean() * 60/5
    
    # resample to hourly    
    qv_hour = qv.resample('60T',closed='right',label='right').last()
    qv_hour = takeOutWeekends(qv_hour)
    qv_hour = pd.DataFrame(qv_hour)
    
    ## fill forward hourly qv from previous days' same hour
    qv_wk = qv_hour.copy()
    qv_wk.index = pd.to_datetime(qv_wk.index)
    qv_wk['date'] = qv_wk.index.date
    qv_wk['hour'] = qv_wk.index.hour
    
    qv_wk = qv_wk.pivot(index = 'date', columns = 'hour')  
    qv_wk = qv_wk.ffill(limit = 4)
    qv_wk = qv_wk.stack(dropna=False).reset_index()                  # index already in datetime
    #chk2['Index'] = chk2['date'] + ' ' +  chk2['hour']
    qv_wk['bar_datetime'] = qv_wk['date'].astype(str) + ' ' + qv_wk['hour'].astype(str) + ':' + '0'
    qv_wk['bar_datetime'] = pd.to_datetime(qv_wk['bar_datetime'], format = '%Y-%m-%d %H:%M')
    
    qv_wk.set_index('bar_datetime', inplace=True)
    
    qv_24h = qv_wk.rolling(24, min_periods = 7).sum()    

    return qv_24h['close']

def calcPMAsignal(df, lb, normLb):
    psma = df - df.rolling(lb, min_periods = 5).mean()
    signal = psma / psma.rolling(normLb, min_periods = 5).std()
    return signal

def calcPEMAsignal(df, lb, normLb):
    pema = df - df.ewm(span=lb, min_periods = 2).mean()
    signal = pema / pema.rolling(normLb, min_periods = 5).std()
    return np.arctan(signal / 3)


def calcMACOsignal(foo, short_lb, long_lb, norm_lb):
    # foo =copy.deepcopy(df)
    maco = foo.rolling(short_lb, min_periods = 1).mean() - foo.rolling(
            long_lb, min_periods = 1).mean()
    signal = maco / maco.rolling(norm_lb, min_periods = 5).std()
    signal = np.tanh(0.5*signal)
    signal = getNormNets(signal, 250) 
    return signal

def calcEmaCOsignal(foo, short_lb, long_lb, norm_lb):
    # foo =copy.deepcopy(df)
    emaco = foo.ewm(span=short_lb, min_periods = 3).mean() - foo.ewm(
            long_lb, min_periods = 3).mean()
    signal = emaco / emaco.rolling(norm_lb, min_periods = 5).std()
    signal = np.tanh(0.5*signal)
    signal = getNormNets(signal, 250) 
    return signal

def getCTFPLsignal(tf_net, normrtn, pl_eval_lb, t_stat_adj, t_stat_threshold):
    # daily for now,
    # both should be single-column dfs
    # pl_eval_lb = int(ee*days_eq)
    # t_stat_adj = pl_t_stat_adj 
    # t_stat_threshold = pl_t_threshold
    tf_sig = tf_net.copy()
    tf_sig.rename(columns={tf_sig.columns[0]: 'sig'}, inplace=True)
    tf_sig['normrtn'] = normrtn
    tf_sig = tf_sig.dropna()
    tf_sig['daily_pl'] = tf_sig['normrtn'] * tf_sig['sig'].shift(1)
    tf_sig['rolling_pl'] = tf_sig['daily_pl'].rolling(pl_eval_lb , min_periods = 10).sum()
    tf_sig['rolling_t'] = tf_sig['rolling_pl'] / tf_sig['rolling_pl'].rolling(250, min_periods = 100).std()
    
    slope_lb = int(pl_eval_lb/10)
    tf_sig['daily_pl'] = tf_sig['daily_pl'].replace(np.nan, 0)
# =============================================================================
#     tf_sig['slope'] = tf_sig['daily_pl'].rolling(slope_lb, min_periods=3).apply(
#               lambda x: np.polyfit(range(len(x)), x, 1)[0], raw=True)
#     tf_sig['entry'] = 1 * ((tf_sig['rolling_t'] > t_stat_threshold) & (
#                     tf_sig['slope'] < slope_thresh))  
# =============================================================================
   
    sidebet = 2 * (norm.cdf(t_stat_adj * tf_sig['rolling_t']) - 0.5)
    sidebet_df = pd.DataFrame(sidebet, columns = ['rolling_t'], index = tf_sig.index) 
    sidebet_df['use_idx'] = 1 * (tf_sig['rolling_t'] > t_stat_threshold)
    #sidebet_df['use_idx'] = tf_sig['entry']
    sidebet_df['tf_sig'] = tf_sig['sig']
    sidebet_df['signal'] = -sidebet_df['rolling_t']*sidebet_df['use_idx']*np.sign(sidebet_df['tf_sig']) 
    return sidebet_df['signal']

def getCPLsignal(tf_net, normrtn, pl_eval_lb, t_stat_adj, t_stat_threshold):
    # daily for now,
    # both should be single-column dfs
    # pl_eval_lb = int(ee*days_eq)
    # t_stat_adj = pl_t_stat_adj 
    # t_stat_threshold = pl_t_threshold
    tf_sig = tf_net.copy()
    tf_sig.rename(columns={tf_sig.columns[0]: 'sig'}, inplace=True)
    tf_sig['normrtn'] = normrtn
    tf_sig = tf_sig.dropna()
    tf_sig['pl'] = tf_sig['normrtn'] * tf_sig['sig'].shift(1)
    tf_sig['rolling_pl'] = tf_sig['pl'].rolling(pl_eval_lb , min_periods = 10).sum()
    tf_sig['rolling_t'] = tf_sig['rolling_pl'] / tf_sig['rolling_pl'].rolling(250, min_periods = 100).std()
    
# =============================================================================
#     slope_lb = int(pl_eval_lb/10)
#     tf_sig['daily_pl'] = tf_sig['daily_pl'].replace(np.nan, 0)
#     tf_sig['slope'] = tf_sig['daily_pl'].rolling(slope_lb, min_periods=3).apply(
#               lambda x: np.polyfit(range(len(x)), x, 1)[0], raw=True)
#     tf_sig['entry'] = 1 * ((tf_sig['rolling_t'] > t_stat_threshold) & (
#                     tf_sig['slope'] < slope_thresh))  
# =============================================================================
   
    sidebet = 2 * (norm.cdf(t_stat_adj * tf_sig['rolling_t']) - 0.5)
    sidebet_df = pd.DataFrame(sidebet, columns = ['rolling_t'], index = tf_sig.index) 
    sidebet_df['use_idx'] = 1 * (tf_sig['rolling_t'] > t_stat_threshold)
    #sidebet_df['use_idx'] = tf_sig['entry']
    sidebet_df['tf_sig'] = tf_sig['sig']
    sidebet_df['signal'] = -sidebet_df['rolling_t']*sidebet_df['use_idx']*sidebet_df['tf_sig'] 
    return sidebet_df['signal']


def calcTFPLsignal(tf_net, normrtn, pl_eval_lb, t_stat_adj, t_stat_threshold):
    # daily for now, the opposite of getCTFPLsignal
    # both should be single-column dfs
    tf_sig = tf_net.copy()
    tf_sig.rename(columns={tf_sig.columns[0]: 'sig'}, inplace=True)
    tf_sig['normrtn'] = normrtn
    tf_sig = tf_sig.dropna()
    tf_sig['daily_pl'] = tf_sig['normrtn'] * tf_sig['sig'].shift(1)
    tf_sig['rolling_pl'] = tf_sig['daily_pl'].rolling(pl_eval_lb , min_periods = 10).sum()
    tf_sig['rolling_t'] = tf_sig['rolling_pl'] / tf_sig['rolling_pl'].rolling(250, min_periods = 100).std()

       
    sidebet = 2 * (norm.cdf(t_stat_adj * tf_sig['rolling_t']) - 0.5)
    sidebet_df = pd.DataFrame(sidebet, columns = ['rolling_t'], index = tf_sig.index) 
    sidebet_df['use_idx'] = 1 * (tf_sig['rolling_t'] < -t_stat_threshold) 
    sidebet_df['tf_sig'] = tf_sig['sig']
    sidebet_df['signal'] = -sidebet_df['rolling_t']*sidebet_df['use_idx']*sidebet_df['tf_sig']
    return sidebet_df['signal']



def getDWTresults(array1D, wavefilter, wavelevel, mode='symmetric'):

    coeff = pywt.wavedec(array1D, wavefilter, mode, level=wavelevel)    

    if wavelevel == 2:    
        
        coeff_wA2 = copy.deepcopy(coeff)
    
# =============================================================================
#         for j in range(0, len(coeff_wA2[1])):
#             coeff_wA2[1][j] = pywt.threshold(coeff_wA2[1][j], 2.0, 'hard')
#         
#         for j in range(0, len(coeff_wA2[2])):
#             coeff_wA2[2][j] = pywt.threshold(coeff_wA2[2][j], 2.0, 'hard')
# =============================================================================

    
            #unl;ike thresholding above, zero out all detail coeffs
        coeff_wA2[1] = np.zeros_like(coeff_wA2[1])
        coeff_wA2[2] = np.zeros_like(coeff_wA2[2])

        wA = pywt.waverec(coeff_wA2, wavefilter, mode)
        wA = wA[0:len(array1D)]

        # get detail
        coeff_wD1 = copy.deepcopy(coeff)
        coeff_wD1[0] = np.zeros_like(coeff_wD1[0])           # wA2 coeff
  
        for j in range(0, len(coeff_wD1[1])):
            coeff_wD1[1][j] = pywt.threshold(coeff_wD1[1][j], 2.0, 'hard')
        
        for j in range(0, len(coeff_wD1[2])):
            coeff_wD1[2][j] = pywt.threshold(coeff_wD1[2][j], 2.0, 'hard')
    
        wD = pywt.waverec(coeff_wD1, wavefilter, mode='symmetric') 
    
    elif wavelevel == 3:
        
        # cA3,cD3,cD2,cD1 = coeff
            # get approximation
        coeff_wA3 = copy.deepcopy(coeff)
    
        for j in range(0, len(coeff_wA3[1])):
            coeff_wA3[1][j] = pywt.threshold(coeff_wA3[1][j], 2.0, 'hard')
        
        for j in range(0, len(coeff_wA3[2])):
            coeff_wA3[2][j] = pywt.threshold(coeff_wA3[2][j], 2.0, 'hard')
    
        for j in range(0, len(coeff_wA3[3])):
            coeff_wA3[3][j] = pywt.threshold(coeff_wA3[3][j], 2.0, 'hard')
    
        wA = pywt.waverec(coeff_wA3, wavefilter, mode)

        # get detail
        coeff_wD1 = copy.deepcopy(coeff)
        coeff_wD1[0] = np.zeros_like(coeff_wD1[0])           # wA2 coeff
  
        for j in range(0, len(coeff_wD1[1])):
            coeff_wD1[1][j] = pywt.threshold(coeff_wD1[1][j], 2.0, 'hard')
        
        for j in range(0, len(coeff_wD1[2])):
            coeff_wD1[2][j] = pywt.threshold(coeff_wD1[2][j], 2.0, 'hard')
    
        for j in range(0, len(coeff_wD1[3])):
            coeff_wD1[3][j] = pywt.threshold(coeff_wD1[3][j], 2.0, 'hard')

        wD = pywt.waverec(coeff_wD1, wavefilter, mode) 

    elif wavelevel == 4:
         
         # cA3,cD3,cD2,cD1 = coeff
             # get approximation
         coeff_wA4 = copy.deepcopy(coeff)
     
         for j in range(0, len(coeff_wA4[1])):
             coeff_wA4[1][j] = pywt.threshold(coeff_wA4[1][j], 2.0, 'hard')
         
         for j in range(0, len(coeff_wA4[2])):
             coeff_wA4[2][j] = pywt.threshold(coeff_wA4[2][j], 2.0, 'hard')
             
         for j in range(0, len(coeff_wA4[3])):
             coeff_wA4[3][j] = pywt.threshold(coeff_wA4[3][j], 2.0, 'hard') 
     
         for j in range(0, len(coeff_wA4[4])):
             coeff_wA4[4][j] = pywt.threshold(coeff_wA4[4][j], 2.0, 'hard')
             
     
         wA = pywt.waverec(coeff_wA4, wavefilter, mode)

         # get detail
         coeff_wD1 = copy.deepcopy(coeff)
         coeff_wD1[0] = np.zeros_like(coeff_wD1[0])           # wA2 coeff
   
         for j in range(0, len(coeff_wD1[1])):
             coeff_wD1[1][j] = pywt.threshold(coeff_wD1[1][j], 2.0, 'hard')
         
         for j in range(0, len(coeff_wD1[2])):
             coeff_wD1[2][j] = pywt.threshold(coeff_wD1[2][j], 2.0, 'hard')
     
         for j in range(0, len(coeff_wD1[3])):
             coeff_wD1[3][j] = pywt.threshold(coeff_wD1[3][j], 2.0, 'hard')

         for j in range(0, len(coeff_wD1[4])):
             coeff_wD1[4][j] = pywt.threshold(coeff_wD1[4][j], 2.0, 'hard')

         wD = pywt.waverec(coeff_wD1, wavefilter, mode) 



    return [wA, wD]
