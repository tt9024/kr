#!/usr/bin/env python

# all the currency we care about  
# EURUSD GBPUSD  USDJPY   USDCHF   USDCAD   NZDUSD   AUDUSD   CHFJPY   EURJPY   GBPJPY   NZDJPY   AUDJPY   CADJPY   EURAUD   EURCAD   EURCHF   GBPCHF   EURGBP   NZDCHF   GBPNZD   GBPCAD   AUDCAD  CADCHF USDTRY USDHUF USDZAR

# TODO : set lp list to be:
# ADS ADS2 ALPARI ALPARI2 BARX BARX2 BNP BNP2 BOAML BOAML2 CITI COMMERZ CREDIT_SUISSE CREDIT_SUISSE2 FASTMATCH FSS FSS2 GOLDMAN GOLDMAN2 GTX HENNING_CAREY HOTSPOT JPMORGAN JPMORGAN2 LAVA MORGANSTANLEY MORGANSTANLEY2 SOCGEN UBS UBS2 VIRTU VIRTU2

import datetime
import time
import numpy as np
import pylab as pl
from matplotlib import finance 
from matplotlib.collections import LineCollection

from sklearn import cluster, covariance, manifold
from sklearn.decomposition import PCA
import sklearn.linear_model as lm

from mpl_toolkits.mplot3d import Axes3D
import matplotlib.pyplot as plt

# weekly depth has a format of
# utc_second, utc_string, mid_price_at_close_of_second, first_1M_vwap_bid, first_1M_vwat_bid, first_1M_lpcount_bid, ... , first_1M_vwap_offer, first_1M_vwat_offer, first_1M_lpcount_offer ... 
# the depth is generated on 1M, 5M, 10M, 20M and 40M volume
#
# AN Example:
#
# 1339366577 20120610,18:16:17 79.567000 795510 611774 1 795496 257094 3 795488 142716 6 795413 218593 13 795122 415183 19 795830 26879 1 795850 27091 3 795885 142716 6 795956 338648 13 796049 392070 21

symbols = [ 'EURUSD', 'GBPUSD',  'USDJPY',   'USDCHF',   'USDCAD',   'NZDUSD',   'AUDUSD',   'CHFJPY',   'EURJPY',   'GBPJPY',   'NZDJPY',  'AUDJPY',   'CADJPY',   'EURAUD',   'EURCAD',   'EURCHF',   'GBPCHF',   'EURGBP' ,  'NZDCHF' ,  'GBPNZD',   'GBPCAD',   'AUDCAD' , 'CADCHF', 'NZDCAD'] ;

symbols_array = np.array(symbols);

ccy = [ 'EUR', 'GBP',  'USD',   'CHF',   'CAD',   'NZD',   'AUD',  'JPY'] ;

ccy1 = [ 'EUR', 'GBP',  'USD',   'USD',   'USD',   'NZD',   'AUD',   'CHF',   'EUR',   'GBP',   'NZD',  'AUD',   'CAD',   'EUR',   'EUR',   'EUR',   'GBP',   'EUR' ,  'NZD' ,  'GBP',   'GBP',   'AUD' , 'CAD', 'NZD'] ;

ccy2 = [ 'USD', 'USD',  'JPY',   'CHF',   'CAD',   'USD',   'USD',   'JPY',   'JPY',   'JPY',   'JPY',  'JPY',   'JPY',   'AUD',   'CAD',   'CHF',   'CHF',   'GBP' ,  'CHF' ,  'NZD',   'CAD',   'CAD' , 'CHF', 'CAD'] ;

pips = [  1,  1,   100, 1, 1,   1, 1,  100, 100,  100,   100,   100,  100,    1,    1,    1,  1,    1,    1,    1,  1,    1,   1,  1] ;

markers2 = ['rx', 'ro', 'rs', 'r^', 'r+', 'bx', 'bo', 'bs', 'b^', 'b+', 'yx', 'yo', 'ys', 'y^', 'y+', 'gx', 'go', 'gs', 'g^', 'g+', 'kx', 'ko', 'ks', 'k^', 'k+', 'cx', 'co', 'cs', 'c^', 'c+'] ;

markers = ['rx-', 'ro-', 'rs-', 'r^-', 'r+-', 'bx-', 'bo-', 'bs-', 'b^-', 'b+-', 'yx-', 'yo-', 'ys-', 'y^-', 'y+-', 'gx-', 'go-', 'gs-', 'g^-', 'g+-', 'kx-', 'ko-', 'ks-', 'k^-', 'k+-', 'cx-', 'co-', 'cs-', 'c^-', 'c+-'] ;

markers3 = ['r.', 'bx', 'yo', 'g+', 'k^', 'cs', 'rx', 'b.', 'go'];

datapath = './data';
ccy_array = np.array(ccy);

month_str_array = np.array(['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC']);

def getMonthNumber (month_str) :
    return "%02d" % (np.where(month_str_array == month_str.upper())[0][0] + 1);

# it gets the ccy number (as in ccy_array), from a string, 
# such as 'jpy' returns 7
def getCCYnumber(this_ccy_symbol) :
    return np.where(ccy_array == this_ccy_symbol.upper())[0][0];


def getCPnumber(cp_symbols) :
    cpn = np.empty(len(cp_symbols), dtype = int) ;
    for idx in range (len(cp_symbols)) :
        cpn[idx] = np.where(symbols_array == cp_symbols[idx])[0][0];

    return cpn;

def getCPFromSymbol(cp_symbol) :
    return np.where(symbols_array == cp_symbol)[0][0];

def getCCYarrayFromCParray(cp_array_number) :
    ccy_array_number = np.empty([len(cp_array_number),2], dtype=int) ;
    for idx in range(len(cp_array_number)) :
        ccy_array_number[idx,0] = np.where(ccy_array == ccy1[cp_array_number[idx]])[0][0] ;
        ccy_array_number[idx,1] = np.where(ccy_array == ccy2[cp_array_number[idx]])[0][0] ;

    return ccy_array_number;

def getCCYn() :
    ccy1n = np.empty(len(ccy1), dtype=int);
    ccy2n = np.empty(len(ccy2), dtype=int);
    for idx in range(len(ccy1)) :
        ccy1n[idx] = np.where(ccy_array == ccy1[idx])[0][0];
        ccy2n[idx] = np.where(ccy_array == ccy2[idx])[0][0];
    return ccy1n, ccy2n;

ccy1n, ccy2n = getCCYn();

def getPairFromCCY(ccy1, ccy2) :
    try :
        return np.where(symbols_array == (ccy1 + ccy2))[0][0]; 
    except:
        pass
    try : 
        return np.where(symbols_array == (ccy2 + ccy1))[0][0];
    except:
        raise 


def getThirdPair(pair1, pair2) :
    cp = np.empty(2, dtype='S3');
    cp[0] = ccy1[pair1] ;
    cp[1] = ccy2[pair1] ;
    ccy = ccy1[pair2] ;
    if ccy == cp[0] :
        return  getPairFromCCY(cp[1], ccy2[pair2]);
    if ccy == cp[1] :
        return  getPairFromCCY(cp[0], ccy2[pair2]);
 
    ccy = ccy2[pair2];
    if ccy == cp[0] :
        return  getPairFromCCY(cp[1], ccy1[pair2]);
    if ccy == cp[1] :
        return  getPairFromCCY(cp[0], ccy1[pair2]);

    raise;

def getFileNames(week) :
    fnames = np.array(symbols, dtype='S128');
    for index in range(len(fnames)) :
        fnames[index] = datapath + '/' + week + '_' +  symbols[index] + '.csv' ;

    return fnames;


def find_skiprows(fnames) :
    index = 0;
    skip_rows = np.empty(len(fnames)) ;
    max_time = 0;
    for fname in fnames :
        ndata, _ = getMidFromWeeklyDepth(fname, 0, 1, 1024);
        non_zero_idx = 0 ;
        #skipping leading zero price values if any, 
        #will report error if 1024 of first records all have 0 price
        while ndata[non_zero_idx][1] == 0 :
            non_zero_idx = non_zero_idx + 1 ;
            if non_zero_idx >= 1024 :
                raise Exception('bad data, all zero at first 1024 rows', fname);

        this_time = ndata[non_zero_idx][0];
        skip_rows[index] = this_time ;
        if max_time < this_time :
            max_time = this_time ;

        index = index + 1;

    for i in range(len(fnames)) :
        skip_rows[i] = max_time - skip_rows[i];

    return skip_rows ;

def getMidFromWeeklyDepth(fname, skip_rows, interval_sec, length, tier=1) :
    if tier == 1 :
        # take the 5M VWAP on both side 
        rawdata = np.loadtxt(fname, delimiter=' ', skiprows = int(skip_rows), usecols=(0,2,6,21)) ;
    else :
        # take the 1M VWAP on both side
        rawdata = np.loadtxt(fname, delimiter=' ', skiprows = int(skip_rows), usecols=(0,2,3,18)) ;

    raw_rows = rawdata.shape[0] ;

    ndata = np.empty([length, 4]) ;
    index = 0 ;
    pos = 0 ;

    while pos < length and index < raw_rows :
        ndata[pos] = rawdata[index] ;
        if rawdata[index][1] == 0 :
            print 'warning: got zero price, trying to use previous tick' ;
            ndata[pos][1] = rawdata[index-1][1] ;

        pos = pos + 1 ;
        index = index + interval_sec ;

    #fill the rest to the last if length is not satisfied
    last_pos = pos - 1;
    if last_pos < 0 :
        raise Exception('no usable data', fname) ;
    while pos < length :
        ndata[pos] = ndata[last_pos] ;
        pos = pos + 1;

    return ndata, index+skip_rows ;

def convertToReturn(prices) :
    l = len(prices);
    rt = np.empty([l]) ;
    for i in range(l-1) :
	    rt[i] = (prices[i+1] - prices[i])/prices[i] ;
    rt[l-1] = 0 ;
    return rt ;

# fnames and skip_rows need to have the same length
def getNextHD(week, skip_rows, seconds) :
    hourly_data = np.empty([len(skip_rows), seconds]);
    hourly_bb = np.empty([len(skip_rows), seconds], dtype = float);
    hourly_bs = np.empty([len(skip_rows), seconds], dtype = float);
    hourly_ts = np.empty([len(skip_rows), seconds]);

    new_skip_row = np.array(skip_rows) ;
    fnames = getFileNames(week);
    index = 0 ;
    for fname in fnames :
        ndata, new_skip_row[index] = getMidFromWeeklyDepth(fname, skip_rows[index], 1, seconds) ;
        #hourly_data[index] = convertToReturn(ndata[:, 1]) ;
        hourly_data[index] = ndata[:, 1] ;
        hourly_bb[index] = ndata[:,2]/1000000.0;
        hourly_bs[index] = ndata[:,3]/1000000.0;
        hourly_ts[index] = ndata[:,0];
        index = index + 1 ;

    return hourly_data, new_skip_row, hourly_bb, hourly_bs, hourly_ts;

# fnames and skip_rows need to have the same length
def getNextHourly(week, skip_rows) :
    return getNextHD(week, skip_rows, 3600);

def getFirstHourly(week) : 
    skiprows = find_skiprows(getFileNames(week));
    return getNextHourly(week, skiprows) ;

def getWeekly(week) :
    skiprows = find_skiprows(getFileNames(week));
    return getNextHD(week, skiprows, 3600*24*5) ;

def fixPip(dat) :
    data = np.empty(dat.shape, dtype=float);
    for idx in range(dat.shape[0]) :
        data[idx] = dat[idx]/pips[idx];

    return data;

def normalData(dat) :
    data = fixPip(dat);
    m = data.mean(axis=1);
    #m = 0;
    #s = data.std(axis=1);
    s = 1;
    return ((data.T-m)/s).T;
 
def graphPriceByColumn(data, columes, title, isCCY=0, showLegend=1, bb=[], bs=[]) :
    if columes.shape[0] != columes.size :
        raise 'columes has to be one dimensional array' ;

    fig = pl.figure() ;
    ax = fig.add_subplot(111);

    x = np.array(range(data.shape[1])) ;
    col_tot = columes.size ;
    for col in columes :
        if isCCY == 0 :
            sym = symbols[col];
            marker = markers[col];
        else :
            if isCCY == 1 :
                sym = ccy[col];
                marker = markers3[col];
            else :
                sym = 'col ' + str(col);
                marker = markers3[col];

        ax.plot(x, data[col, :], marker, label=sym);
        if bb != [] :
            ax.plot(x, bb[col, :], 'gx', label = sym+'bb');
            ax.plot(x, bs[col, :], 'r.', label = sym+'bs');

    if showLegend == 1:
        ax.legend(loc="best");

    ax.set_title(title);
    ax.grid(True);
    return ax ;

def graphPriceByName(data, names, title='', isCCY=0) :
    cols = np.empty(names.shape[0], dtype=int);
    idx = 0;
    if isCCY == 0 :
        symbs = symbols_array ;
    else :
        symbs = ccy_array;

    for n in names :
        cols[idx] = np.where(symbs == n)[0][0];
        idx = idx + 1;

    if isCCY == 0 :
        return graphPriceByColumn(normalData(data), cols, title, isCCY);
    else :
        return graphPriceByColumn(data, cols, title, isCCY);

def graphCov(data, cov, plot_threshold=0.5) :
    cov1 = np.triu(cov, k=1);
    rows = cov1.shape[0];
    cols = cov1.shape[1];
    if rows != cols :
        raise 'cov has to be square matrix' ;

    for i in range(rows) :
        for j in range(cols) :
            if abs(cov1[i,j]) > plot_threshold :
                graphPriceByColumn(normalData(data), np.array([i, j]), 'val=%f' % cov1[i,j])

# not to confuse with previous normalData
def normalData_2(data, toReturn = False, toWhiten = True) :
    X = np.array(data);
    if toReturn :
        idx = 0;
        for d in data :
            X[idx, :] = convertToReturn(d) ;
            idx = idx + 1;

    if toWhiten :
        m= X.T.mean(axis=0);
        s = X.T.std(axis=0);
        return ((X.T-m)/s).T;

    return X;

# it's not clear how corr and partial_corr helps identify 
# good regression pairs, and this is time varying too. 
def getCov(data, model = 1, toReturn = False, toWhiten = True, get_par = True) :
    if model == 0 :
        edge_model = covariance.GraphLassoCV() ;
    else :
        if model == 1:
            edge_model = covariance.EmpiricalCovariance() ;
        else :
            if model == 2 :
                edge_model = covariance.MinCovDet() ;
            else:
                raise 'unsupported model';

    # convert to return
    X = np.empty(data.T.shape);
    idx = 0;
    for d in data :
        if toReturn :
            X[:, idx] = convertToReturn(d) ;
        else:
            X[:, idx] = d;

        idx = idx + 1;

    if toWhiten :
        m = X.mean(axis=0);
        s = X.std(axis=0);
        X = (X-m)/s;

    edge_model.fit(X) ;

    cov = edge_model.covariance_ ;

    if not get_par :
        return cov, np.array([]);

    partial_correlations = edge_model.precision_.copy()
    d = 1 / np.sqrt(np.diag(partial_correlations))
    partial_correlations *= d
    partial_correlations *= d[:, np.newaxis]
    return cov, partial_correlations ;

# data is a two dimension 
def graphTimeSeriesData(data, is_ccy=0, toReturn = False) :
    # do we need to whiten it beforehand?
    edge_model = covariance.GraphLassoCV() ;
    #edge_model = covariance.MinCovDet() ;
    #edge_model = covariance.EmpiricalCovariance() ;

    # convert to return
    X = np.empty(data.T.shape);
    idx = 0;
    for d in data :
        if toReturn :
            X[:, idx] = convertToReturn(d) ;
        else:
            X[:, idx] = d;
        idx = idx + 1;
    #normalize
    #X = data.copy().T ;
    m = X.mean(axis=0);
    s = X.std(axis=0);
    X = (X-m)/s;
    #X /= X.std(axis = 0) ;
    edge_model.fit(X) ;

    cov = edge_model.covariance_ ;
    _, labels = cluster.affinity_propagation(cov)
    n_labels = labels.max()

    if is_ccy == 0 :
        names = np.array(symbols) ;
    else :
        names = np.array(ccy);

    for i in range(n_labels + 1): 
        print 'Cluster %i: %s' % ((i + 1), ', '.join(names[labels == i])) ;

    node_position_model = manifold.LocallyLinearEmbedding(n_components=2, eigen_solver='dense', n_neighbors=6)
    embedding = node_position_model.fit_transform(X.T).T

    # compute partial correlation
    partial_correlations = edge_model.precision_.copy()
    d = 1 / np.sqrt(np.diag(partial_correlations))
    partial_correlations *= d
    partial_correlations *= d[:, np.newaxis]
    non_zero = (np.abs(np.triu(partial_correlations, k=1)) > 0.02)

    # Visualization
    pl.figure(1, facecolor='w', figsize=(10, 8))
    pl.clf()
    ax = pl.axes([0., 0., 1., 1.])
    pl.axis('off')

    pl.scatter(embedding[0], embedding[1], s=100 * d ** 2, c=labels, cmap=pl.cm.spectral)

    pl.scatter(embedding[0], embedding[1], s=100 * d ** 2, c=labels, cmap=pl.cm.spectral)
    # Plot the edges
    start_idx, end_idx = np.where(non_zero)
    #a sequence of (*line0*, *line1*, *line2*), where::
    #            linen = (x0, y0), (x1, y1), ... (xm, ym)
    segments = [[embedding[:, start], embedding[:, stop]]
                for start, stop in zip(start_idx, end_idx)]

    values = np.abs(partial_correlations[non_zero])
    lc = LineCollection(segments, zorder=0, cmap=pl.cm.hot_r, norm=pl.Normalize(0, .7 * values.max()))
    lc.set_array(values)
    lc.set_linewidths(15 * values)
    ax.add_collection(lc)

    # Add a label to each node. The challenge here is that we want to
    # position the labels to avoid overlap with other labels
    for index, (name, label, (x, y)) in enumerate(zip(names, labels, embedding.T)):
        dx = x - embedding[0]
        dx[index] = 1
        dy = y - embedding[1]
        dy[index] = 1
        this_dx = dx[np.argmin(np.abs(dy))]
        this_dy = dy[np.argmin(np.abs(dx))]
        if this_dx > 0:
            horizontalalignment = 'left'
            x = x + .002
        else:
            horizontalalignment = 'right'
            x = x - .002
        if this_dy > 0:
            verticalalignment = 'bottom'
            y = y + .002
        else:
            verticalalignment = 'top'
            y = y - .002

        pl.text(x, y, name, size=10,
            horizontalalignment=horizontalalignment,
            verticalalignment=verticalalignment,
            bbox=dict(facecolor='w',
                      edgecolor=pl.cm.spectral(label / float(n_labels)),
                      alpha=.6))

    pl.xlim(embedding[0].min() - .15 * embedding[0].ptp(), embedding[0].max() + .10 * embedding[0].ptp(),)
    pl.ylim(embedding[1].min() - .03 * embedding[1].ptp(), embedding[1].max() + .03 * embedding[1].ptp())

    np.savetxt('cov', cov);
    np.savetxt('par', partial_correlations);

    #m = data.mean(axis=1);
    #s = data.std(axis=1);
    #data = ((data.T-m)/s).T;
    #graphCov(data, cov, 0.6);
    #graphCov(data, partial_correlations, 0.3);
    #graphCov(data, abs(partial_correlations)+ abs(cov), 1)
    
    #pl.show() ;
    return cov, partial_correlations

# all the prev_ccy is an array of all the ccy (currently 8). 
# they should all be initialized to 1
# cp_list is symbol number
# all should be np array
def iterDepth(prev_ccy, curr_cp, cp_list, max_iters = 1000, stop_threshold=0.00001) :
    ccy_list = getCCYarrayFromCParray(cp_list);
    num_cp = len(cp_list);
    curr_diff = 10;
    iters = 1;
    curr_ccy = np.array(prev_ccy, dtype = float);

    while curr_diff > stop_threshold and iters < max_iters :
        prev_ccy_copy = curr_ccy;
        curr_ccy = np.zeros(len(prev_ccy), dtype = float);
        curr_ccy_cnt = np.zeros(len(prev_ccy), dtype = float);

        for cp_idx in range(len(cp_list)) :
            ccy1 = ccy_list[cp_idx][0];
            ccy2 = ccy_list[cp_idx][1];
            curr_ccy[ccy1] += prev_ccy_copy[ccy2] * curr_cp[cp_idx];
            curr_ccy_cnt[ccy1] += 1;
            curr_ccy[ccy2] += prev_ccy_copy[ccy1] / curr_cp[cp_idx];
            curr_ccy_cnt[ccy2] += 1;

        # do we need to smooth from the previous value?
        for ccy_idx in range(len(prev_ccy)) :
            if curr_ccy_cnt[ccy_idx] > 0.0 :
                curr_ccy[ccy_idx] = curr_ccy[ccy_idx]/curr_ccy_cnt[ccy_idx];
            else:
                curr_ccy[ccy_idx] = 1;

        iters += 1;
        curr_diff = max(abs(curr_ccy - prev_ccy_copy));

    curr_ccy /= np.median(curr_ccy)
    return curr_ccy, curr_diff, iters;

# hd is in the shape of row for each cp
def getCCYrate(hd, interval_sec = 1, cp_list = []) :
    data = fixPip(hd);
    total_seconds = hd.shape[1];

    ccy_rate = np.empty([len(ccy), total_seconds/interval_sec], dtype = float);
    ccy_cum_rate = np.empty([len(ccy), total_seconds/interval_sec], dtype = float);
    ccy_rate[:,0] = np.ones(len(ccy), dtype =float);
    ccy_cum_rate[:,0] = np.ones(len(ccy), dtype =float);

    ccy_idx = 1;
    cp_idx = 0;
    if len(cp_list) == 0 :
        cp_list = range(hd.shape[0]);

    while cp_idx < total_seconds-1:
        cp_rate = data[:,cp_idx+interval_sec] / data[:,cp_idx] ;
        ccy_rate[:,ccy_idx], curr_diff, iters = iterDepth(np.ones(len(ccy), dtype=float), cp_rate, cp_list) ;
        #calibrate towards EUR
        #ccy_rate[:,ccy_idx] /= ccy_rate[0, ccy_idx];
        ccy_cum_rate[:,ccy_idx] = ccy_cum_rate[:, ccy_idx-1]*ccy_rate[:, ccy_idx];
        cp_idx += interval_sec;
        ccy_idx += 1;

    return ccy_rate, ccy_cum_rate;

def getSlope(data, lookback=5) :
    slope_val = np.array(data);

    mean_val_x = np.mean(range(lookback));
    x_var = 0.0;
    for x in range(lookback) :
        x_var += (x-mean_val_x)*(x-mean_val_x);

    for row in range(np.shape(data)[0]) :
        for col in range(np.shape(data)[1]) :
            if col < lookback-1 :
                slope_val[row,col] = 0;
                continue;

            mean_val_y = np.mean(data[row,col-(lookback-1):col]);
            slp = 0;
            for x in range(lookback) :
                slp += (x-mean_val_x)*(data[row,col-(lookback-1-x)]-mean_val_y);

            slope_val[row,col] = slp/x_var;

    return slope_val;

def getEWA_2d(data, factor=0.5) :
    val = np.zeros(data.shape);
    for row in range(data.shape[0]) :
        val[row,0] = data[row,0];
        col = 1;
        while col < data.shape[1] :
            val[row,col] = val[row,col-1]*0.5 + data[row, col]*0.5;
            col += 1;

    return val;

# returns an array of cp whose ccy_rate diff exceeds threshold
def getCPDiff(ccy_slope_array) :
    total_cp = len(symbols);
    total_ccy = len(ccy);
    total_seconds = ccy_slope_array.shape[1];
    cp_diff = np.zeros([total_cp, total_seconds]);

    cp_num_trades = np.zeros(total_cp);
    for sec in range(total_seconds) :
        ind = ccy_slope_array[:,sec].argsort();
        arr = ccy_slope_array[ind, sec];
        # find pairs that have diff more than threshold
        for ccy1 in range(total_ccy-1) :
            for ccy2 in np.array(range(total_ccy-1,ccy1,-1)) :
                diff = arr[ccy2] - arr[ccy1] ;
                # make a pair out of ccy1 and ccy2
                try :
                    cp = getPairFromCCY(ccy[ind[ccy1]], ccy[ind[ccy2]]) ;
                    if ind[ccy1] == ccy1n[cp] :
                        cp_diff[cp,sec] = -diff;
                    else:
                        cp_diff[cp,sec] = diff;
                except :
                    pass;

    return cp_diff;

def getPNL(cp, cur_sec, enter_direction, enter_price, bb, bs) :
    if enter_direction == 1:
        cur_price = bb[cp, cur_sec];
        cur_pnl = cur_price - enter_price;
        return cur_price, cur_pnl;

    cur_price = bs[cp, cur_sec];
    cur_pnl = enter_price - cur_price;
    return cur_price, cur_pnl;

# Enter when
# a. spread less than half of max_down
# b. slopes of two ccy have different sign,
# c. their slope have to be at least min_slope
# d. rate diff greater than enter_diff or big_jump detected on one of the ccy
def enterTrade(cp, sec, ccy_slope_array, ccy_rate, hd, bb, bs, enter_diff,
               big_jump, min_slope, max_down, trade_info) :
    spread = bs[cp,sec] - bb[cp,sec];
    if spread >= max_down/2 :
        return 0;

    ccy_1 = ccy1n[cp];
    ccy_2 = ccy2n[cp];
    slope1 = ccy_slope_array[ccy_1, sec];
    slope2 = ccy_slope_array[ccy_2, sec];

    if slope1 * slope2 > 0 :
        return 0;

    if abs(slope1) < min_slope or abs(slope2) < min_slope :
        return 0;
 
    diff = slope1 - slope2;
    if abs(ccy_rate[ccy_1, sec]) < big_jump and abs(ccy_rate[ccy_2, sec]) < big_jump and abs(diff) < enter_diff :
        return 0;

    trade_info[0] = cp;
    trade_info[1] = sec;
    trade_info[2] = diff;
    trade_info[3] = slope1;
    trade_info[4] = slope2;
    trade_info[5] = spread;

    if diff > 0:
        enter_direction = 1;
        enter_price = bs[cp,sec];
    else:
        enter_direction = -1;
        enter_price = bb[cp,sec];

    trade_info[6] = enter_direction;
    trade_info[7] = enter_price;
    return enter_direction;

# Exit when
# 1. exit when cur_price is at loss w.r.t enter price for max_down
# 2. exit when best price hasn't been improved for max_flat
# 3. exit when in exit mood and cur_price is less than best price
# 4. in exit mood if same direction diff less than min_diff
# 5. when same direction diff larger than enter_diff, get out exit mood if in.
# exit reason -
#   1: max_down reached
#   2: max_flat reached
#   3: exit in exit mood
def exitTrade(cp, enter_sec, enter_price, enter_direction, ccy_slope_array,
              ccy_rate, hd, bb, bs, enter_diff, min_diff,
              max_down, max_flat, big_jump, trade_info) :
    cur_sec = enter_sec + 1;
    total_sec = ccy_slope_array.shape[1];
    exit_mood = 0;
    exit_reason = 0;
    best_price = enter_price * enter_direction;
    flat_time = 0;
    ccy_1 = ccy1n[cp];
    ccy_2 = ccy2n[cp];
    cur_slope_diff = cur_price = cur_pnl = 0.0;
    
    while cur_sec < total_sec :
        cur_price, cur_pnl = getPNL(cp, cur_sec, enter_direction, enter_price, bb, bs);
        cur_slope_diff = enter_direction * (ccy_slope_array[ccy_1, cur_sec] - ccy_slope_array[ccy_2, cur_sec]);

        if cur_pnl < -1*max_down :
            exit_reason = 1;
            break;

        cur_price_adj = cur_price * enter_direction;
        if best_price <  cur_price_adj:
            best_price = cur_price_adj;
            flat_time = 0;
        else:
            flat_time += 1;

        if flat_time > max_flat :
            exit_reason = 2;
            break;

        if exit_mood == 0 :
            if cur_slope_diff < min_diff :
                exit_mood = 1;
        else :
            cur_rate_diff = enter_direction * (ccy_rate[ccy_1, cur_sec] - ccy_rate[ccy_2, cur_sec]);
            if cur_slope_diff >= enter_diff or cur_rate_diff >= big_jump :
                exit_mood = 0;

        if exit_mood == 1 and cur_price_adj != best_price :
            exit_reason = 3;
            break;

        cur_sec += 1;

    trade_info[8] = cur_sec;
    trade_info[9] = cur_slope_diff;
    trade_info[10] = cur_price;
    trade_info[11] = cur_pnl;
    trade_info[12] = exit_reason;
    return cur_sec;


def tradeDepth(hd, bb, bs, slope_look_back = 30, max_down=0.0002, enter_diff = 0.000015, big_jump = 0.00001, 
               min_slope = 0.000001, min_diff=0.000003, max_flat = 20) :
    ccy_rate, ccy_cum_rate = getCCYrate(hd);
    ccy_slope_array = getSlope(ccy_cum_rate, slope_look_back);
    total_cp = len(symbols) ;
    total_seconds = hd.shape[1];

    # fill a trade record as
    # enter_cp, enter_time, enter_diff, ccy1_slope, ccy2_slope, enter_spread, enter_direction, enter_price, 
    # exit_time,  exit_diff,  exit_price, pnl, exit_reason

    cp_trade = [];
    trade_info= np.zeros(13);

    for cp in range(total_cp) :
        sec = 0;
        while sec < total_seconds :
            enter_direction = enterTrade(cp, sec, ccy_slope_array, ccy_rate, hd, bb, bs, enter_diff, big_jump, min_slope, max_down, trade_info);
            enter_price = trade_info[7];

            if enter_direction != 0 :
                sec = exitTrade(cp, sec, enter_price, enter_direction, ccy_slope_array,
                                ccy_rate, hd, bb, bs, enter_diff, min_diff, max_down, max_flat,
                                big_jump, trade_info);
                if sec >= total_seconds - 1:
                    break;

                cp_trade.append(trade_info);

            else :
                sec += 1;

    return np.array(cp_trade);

def graphTrade(cp_trade, hd, bb, bs, cp, slope_look_back = 30) :

    total_seconds = hd.shape[1];
    # draw price line first (no normalization or pip fixture), no legend
    ax = graphPriceByColumn(hd, np.array([cp]), symbols[cp], 0, 0) ;

    # draw cp diff

    ccy_rate, ccy_cum_rate = getCCYrate(hd);
    ccy_slope = getSlope(ccy_cum_rate, slope_look_back);
    cp_diff = getCPDiff(ccy_slope);

    ax2 = ax.twinx();
    ax2.plot(range(total_seconds), cp_diff[cp], 'bx');
    ax2.set_ylabel('slope diff');
    
    #get all trades from cp
    total_trades = cp_trade.shape[0];
    for t in range(total_trades) :
        if cp_trade[t, 0] != cp :
            continue;

        enter_time = cp_trade[t, 1];
        enter_direction = cp_trade[t, 6];
        enter_price = cp_trade[t, 7];

        exit_time = cp_trade[t, 8];
        exit_price = cp_trade[t, 10];
        exit_reason = cp_trade[t, 12];

        if enter_direction == 0 or exit_reason == 0 :
            continue;

        if enter_direction == 1 :
            marker = 'g^-' ;
        else :
            market = 'rv-' ;
   
        ax.plot([enter_time, exit_time], [enter_price, exit_price], marker);

    return ax, ax2;

# this function classify each time instance as win or loss, based on
# the strategy given.  The strategy has two parameters: max_pip, max_time.
# It evaluates whether from the given time, it will first hit max_pip up (win)
# or max_pip down (lose) or the best_price hasn't updated for max_time.
# enter_direction is 1 for long, -1 for short
# it will return a classification: 1 or -1, as win or loss and a pnl result
def getClassify(sec, max_pip, max_time, bb, bs, enter_direction) :
    # don't consider crossed situation
    if bb[sec] > bs[sec] :
        return 0, 0;
    pnl = 0;
    classify = 0;

    enter_side = bs;
    exit_side = bb;
    if enter_direction == -1 :
        enter_side = bb;
        exit_side = bs;

    enter_price = enter_side[sec];

    best_price = enter_price;
    best_time = sec;

    sec += 1;
    total_sec = len(bb);
    while sec < total_sec :
        cur_price = exit_side[sec] ;
        pnl = enter_direction * (cur_price - enter_price);
        if pnl >= max_pip :
            return 1, pnl ;

        if pnl <= -1*max_pip :
            return -1, pnl;

        if (cur_price - best_price) * enter_direction > 0 :
            best_price = cur_price;
            best_time = sec;

        if sec - best_time > max_time :
            return 0, pnl;

        sec += 1;

    return 0, pnl;

# it compares ts_data with val within the given range.
# returns ratio of upticks to total ticks
def getIntensityCount(ts_data, ts_start, ts_end, val, poly_smooth_order = 10, tolerate = 0.000001) :
    uptick = 0;
    fitted_data = np.array(ts_data[ts_start:ts_end]);
    if poly_smooth_order > 0:
        fitted_data = polySmooth(ts_data, ts_start, ts_end-ts_start, poly_smooth_order);

    for cur_pos in range(ts_end-ts_start) :
        #if ts_data[cur_pos] > val + tolerate :
        if fitted_data[cur_pos] > val :
            uptick += 1;

    return float(uptick)/float(ts_end-ts_start);

# regime shifting index
# use the diff between polyfit prediction and actual time series
def getTSDiff(ts1, ts2, num) :
    diff = 0.0;
    for idx in range(num) :
        diff += (ts1[idx] - ts2[idx]);

    return diff/float(num);

def polyPredict(ts_data, ts_start, look_back_window, poly_order, predict_window) :
    poly = np.polyfit(np.array(range(look_back_window)), ts_data[ts_start:ts_start+look_back_window], poly_order);
    poly_series = np.poly1d(poly);
    return np.array(poly_series(range(look_back_window, look_back_window+predict_window)));

def polySmooth(ts_data, ts_start, look_back_window, poly_order) :
    poly = np.polyfit(np.array(range(look_back_window)), ts_data[ts_start:ts_start+look_back_window], poly_order);
    poly_series = np.poly1d(poly);
    return np.array(poly_series(range(look_back_window)));

# given a prediction window, it adds the difference between actual and 1,2,3,.. steps predictions
def getRegimeShiftIndex(ts_data, look_back_window, poly_order, predict_window = 1) :
    total_seconds = len(ts_data);

    shift_index = np.zeros(total_seconds, dtype = float);
    pred = np.zeros([predict_window, predict_window], dtype = float);

    for sec in range(predict_window) :
        pred[sec, :] = polyPredict(ts_data, sec, look_back_window, poly_order, predict_window);

    for sec in range(look_back_window + predict_window, total_seconds) :
        total_shift = 0.0;
        for idx in range(predict_window) :
             # total_shift += getTSDiff(pred[(sec - 1 - idx) % predict_window,:], np.array(ts_data[sec-idx:sec+1]), idx+1);
            total_shift += sum(pred[(sec - 1 - idx) % predict_window,0:idx+1] - ts_data[sec-idx:sec+1])/float(idx+1);

        shift_index[sec] = total_shift/float(predict_window);
        pred[sec % predict_window, :] = polyPredict(ts_data, sec-look_back_window + 1, look_back_window, poly_order, predict_window);

    return shift_index;

## graph feature values again price
def graphPriceByFeat(ccy_cum_rate, feat, feat_label, feat_name, cp) :
    total_seconds = ccy_cum_rate.shape[1];
    total_feat = feat.shape[0];

    if feat.shape[1] != total_seconds :
        raise;

    # draw price line first (no normalization or pip fixture), no legend
    ax = graphPriceByColumn(ccy_cum_rate, np.array([ccy1n[cp], ccy2n[cp]]), symbols[cp], 1, 1);

    ax2 = ax.twinx();
    ax2.set_ylabel(feat_name);

    for fe in range(total_feat) :
        ax2.plot(range(total_seconds), feat[fe, :], markers3[fe+3], label = feat_label[fe]);

    ax2.legend(loc="best");
    return ax, ax2;

# cp is the number of currency pair, time_window is in seconds
# it counts 
def graphIntensityFeature(hd, cp, time_window) :
    ccy_rate, ccy_cum_rate = getCCYrate(hd);
    total_seconds = ccy_rate.shape[1];
    feat = np.zeros([2,total_seconds], dtype = float);

    for sec in range(time_window, total_seconds) :
        feat[0, sec] = getIntensityCount(-1*ccy_rate[ccy1n[cp],:], sec-time_window, sec, -1);
        feat[1, sec] = getIntensityCount(1*ccy_rate[ccy2n[cp],:], sec-time_window, sec, 1);

    feat_label = np.array([ccy1[cp] + ' ' +str(time_window) + ' down', ccy2[cp] + ' ' + str(time_window) + ' up']);
    return graphPriceByFeat(ccy_cum_rate, feat, feat_label, 'intensity percent', cp);

def graphSlopeFeature(hd, cp, time_window) :
    ccy_rate, ccy_cum_rate = getCCYrate(hd);
#    ccy_slope = getSlope(ccy_cum_rate, time_window);
    ccy_slope_60 = getSlope(ccy_cum_rate, 60);
    ccy_slope_20 = getSlope(ccy_cum_rate, 20);
    ccy_slope = (ccy_slope_20 + ccy_slope_60)/2.0;

    total_seconds = ccy_rate.shape[1];
    feat = np.zeros([2,total_seconds], dtype = float);

    feat[0, :] = ccy_slope[ccy1n[cp], :];
    feat[1, :] = ccy_slope[ccy2n[cp], :];
#    feat[2, :] = ccy_slope_60[ccy1n[cp], :];
#    feat[3, :] = ccy_slope_60[ccy2n[cp], :];

#    feat_label = np.array([ccy1[cp] + ' 20 slope', ccy2[cp] + ' 20 slope', ccy1[cp] + ' 60 slope', ccy2[cp] + ' 60 slope']);
    feat_label = np.array([ccy1[cp] + ' ' + str(time_window) + ' slope', ccy2[cp] + ' ' + str(time_window) + ' slope']);
    return graphPriceByFeat(ccy_cum_rate, feat, feat_label, 'slope diff', cp);

def getRegimeFeature(ts_data, look_back_window) :
    si1 =  getRegimeShiftIndex(ts_data, look_back_window, 1); 
    si2 =  getRegimeShiftIndex(ts_data, look_back_window, 2); 
    si3 =  getRegimeShiftIndex(ts_data, look_back_window, 3); 

    return (si1+si2+si3)/3.0, np.array(['si']);

def getIntensityFeature(ts_data, time_window) :
    total_seconds = len(ts_data);
    feat = np.zeros([2,total_seconds], dtype = float);

    for sec in range(time_window, total_seconds) :
        feat[0, sec] = getIntensityCount(ts_data, sec-time_window, sec, 1);
        feat[1, sec] = getIntensityCount(-1*ts_data, sec-time_window, sec, -1);

    feat_label = np.array(['tick up', 'tick down']);
    return feat, feat_label;

def graphFeature(ts, features, labels) :
    fig = pl.figure();
    ax = fig.add_subplot(111);

    ax.plot(range(len(ts)), ts, 'c.-', label = 'data');
    ax2 = ax.twinx()

    num_feat = features.shape[0];
    for f in range(num_feat) :
        ax2.plot(range(len(ts)), features[f, :], markers3[f], label=labels[f]) ;

    ax.legend(loc="best");
    ax2.legend(loc="best");
    ax2.grid(True);
    return ax, ax2;

# collection of per-cp feature
# 1. small 20/120 up/down count                         --2
# 2. slope of 20 in 60/300 polyfit                      --2
# 3. pca'ed two lines: slope of 20 in 60/300 polyfit    --4
# 4. short term ccy diff: slope of 
# 4. short term avg spread                              --1
# 5. short term price slope 5 in 60 polyfit             --1

def collectFeatures(sec, hd, bs, bb, ccy_cum_rate,
        short_term_tick_count = 20, mid_term_tick_count = 120,
        ccy_slope_ticks = 20, short_term_ccy_slope = 60, mid_term_ccy_slope = 300,
        pca_slope_ticks = 20, short_term_pca_slope = 60, mid_term_pca_slope = 300,
        short_term_spread_ticks = 20,
        short_term_cp_slope_ticks = 5, short_term_cp_poly_ticks = 60) :

    num_feat = 10;
    feat = np.empty(2*num_feat);

    ###  TO BE IMPLEMENTED
    return 0;

# ratio can be 1, 2, ... , N (integer),
# meaning how many samples in "data" goes to the new data
# if smooth set to true, the old samples will be avg'ed 
# into a new sample
def overSample(data, ratio, smooth) :
    data_len = len(data);
    if data_len < 1 :
        raise "data is empty" ;

    new_data = np.zeros(data_len/ratio) ;
    n = 0;
    k = 0;
    if not smooth :
        for d in data :
            n += 1;
            if n % ratio == 0 :
                new_data[k] = d;
                k += 1;
    else :
        nd = 0.0;
        for d in data :
            nd += d;
            n += 1;
            if n % ratio == 0 :
                new_data[k] = nd/ratio;
                k += 1;
                nd = 0.0;

    return new_data;

# hd is in [features, samples] format
def overSampleHD(hd, ratio, smooth = True) :
    new_hd = np.zeros([np.shape(hd)[0], np.shape(hd)[1]/ratio]) ;
    idx = 0;
    for data in hd :
        new_hd[idx, :] = overSample(hd[idx,:], ratio, smooth)
        idx = idx + 1;

    return new_hd;
       

def testG () :
    from sklearn import linear_model

    X_train = np.c_[.5, 1].T
    y_train = [.5, 1]
    X_test = np.c_[0, 2].T

    np.random.seed(0)

    classifiers = dict(ols=linear_model.LinearRegression(),
                   ridge=linear_model.Ridge(alpha=.1))

    fignum = 1
    for name, clf in classifiers.iteritems():
        fig = pl.figure(fignum, figsize=(4, 3))
        pl.clf()
        ax = pl.axes([.12, .12, .8, .8])

        for _ in range(6):
            this_X = .1 * np.random.normal(size=(2, 1)) + X_train
            clf.fit(this_X, y_train)

            ax.plot(X_test, clf.predict(X_test), color='.5')
            ax.scatter(this_X, y_train, s=3, c='.5', marker='o', zorder=10)

        clf.fit(X_train, y_train)
        ax.plot(X_test, clf.predict(X_test), linewidth=2, color='blue')
        ax.scatter(X_train, y_train, s=30, c='r', marker='+', zorder=10)

        ax.set_xticks(())
        ax.set_yticks(())
        ax.set_ylim((0, 1.6))
        ax.set_xlabel('X')
        ax.set_ylabel('y')
        ax.set_xlim(0, 2)
        fignum += 1

    pl.show()

# data is in ccy_cum_rate format
def plotPCA(data, ratio = 0.9, isCCY = 1, if_whiten = False) :
    pca = PCA(n_components = ratio, whiten = if_whiten);
    pca.fit(data.T);
    print "components: ", pca.components_ ;
    print "explained variance: ", pca.explained_variance_ratio_ ;

    graphPriceByColumn(data, np.array(range(np.shape(data)[0])), 'original data', isCCY);
    y = pca.transform(data.T);
    graphPriceByColumn(y.T, np.array(range(len(pca.explained_variance_ratio_))), 'pca components', 2);
    data_new = pca.inverse_transform(y).T
    graphPriceByColumn(data_new, np.array(range(np.shape(data)[0])), 'estimation of original', isCCY);
    return y, data_new, pca.components_, pca.explained_variance_ratio_ ;

# is keeps fit data with past 1600, and uses the model for the next 120 seconds, 
# it plots the difference between original data and pca inverse transform.
def runningPCAEstimation(data, estimation_period = 1800, update_period = 120, ratio = 0.6, isCCY = 1) :
    pca = PCA(n_components = ratio);
    pca.fit(data[:, 0:estimation_period].T);
    dims = np.shape(data)[0];
    total = np.shape(data)[1];
    est = np.zeros([dims, total]);
    est[:, 0:estimation_period] = pca.inverse_transform(pca.transform(data[:, 0:estimation_period].T)).T;

    idx = estimation_period;
    while idx + update_period <= total :
        y = pca.transform(data[:, idx:idx+update_period].T);
        est[:, idx:idx+update_period] = pca.inverse_transform(y).T;
        idx = idx + update_period;
        pca.fit(data[:, idx-estimation_period:idx].T)

    graphPriceByColumn(data, np.array(range(dims)), 'original data', isCCY);
    graphPriceByColumn(est, np.array(range(dims)), 'estimated data', isCCY);

    diff = data - est;
    graphPriceByColumn(diff, np.array(range(dims)), 'difference', isCCY);

    return est, diff

# outlier removal
# samples that are both outside of "trim_ratio" from parzin window
#                  and more than "std_multi" times std from mean 
# are removed
def purifyData(data, trim_ratio = 0.001, std_multi=5) :
    pdf_, xplot , cdf_, thres, min_, max_ = toPDF(data, 1000, 1-trim_ratio)
    std_ = data.std(axis=0);
    mean_ = data.mean(axis=0);
    std_min = mean_ - std_multi*std_;
    std_max = mean_ + std_multi*std_;

    new_data = []
    for d in data :
        if d > std_min and d > min_ and d < std_max and d < max_ :
            new_data.append(d) ;

    return np.array(new_data)

# data supposed to be a one dimentional array
def toPDF(data, bins=10000, width_threshold=0.9) :
    pdf_ = np.zeros([bins+1], dtype=float) ;
    cdf_ = np.zeros([bins+1], dtype=float) ;
    min_ = float(min(data)) ;
    max_ = float(max(data)) ;
    bsize_ = (max_ - min_)/float(bins);

    xplot = np.array(range(bins+1));
    xplot = xplot * bsize_ + min_ ;

    if bsize_ == 0 :
        bsize_ = 1.0;

    for d in data :
        idx = int((d-min_)/bsize_ + 0.5) ;
        pdf_[ idx ] = pdf_[ idx ] + 1 ;

    pdf_ = pdf_/float(len(data));
    cdf = 0.0;
    idx = 0;
    sbin = -1;
    ebin = -1;
    sthresh = (1.0-width_threshold)/2.0
    ethresh = 1.0-sthresh;

    ######  TODO: outlier removal: numbers that are far away from 
    ######  the center are removed, i.e. 10 times the std
    while idx <= bins :
        cdf = cdf + pdf_[idx] ;
        cdf_[idx] = cdf;
        if sbin == -1 :
            if cdf > sthresh :
                sbin = idx;
        if ebin == -1 : 
            if cdf > ethresh :
                ebin = idx;

        idx = idx + 1;

    #print sbin, ebin, xplot[sbin], xplot[ebin]

    return pdf_, xplot , cdf_,  float(ebin-sbin)/float(bins), xplot[sbin], xplot[ebin]

# data is 1-dimensional numpy arrays
def whitenData (data) :
    return (data - data.mean(axis=0))/data.std(axis=0);

def squashData(data, lb=0.0, ub=1.0, precision=100.0) :
    dmin = data.min(axis=0);
    dmax = data.max(axis=0);
    drange = dmax-dmin;
    trange = ub-lb;

    new_data = data;
    idx = 0;
    for d in data:
        dnew = (d-dmin)/drange * trange + lb;
        new_data[idx] = float(int(dnew *precision))/precision;
        idx = idx + 1;

    return new_data;

# data is in [features, samples] format like hd
# model = 0 is normalize to have mean=0 and std=1
# model = 1 is to squash to range of [lb, up]
def whitenDataHD(data, model = 0, lb=0.0, ub=1.0) :
    idx = 0;
    new_data = data;
    for d in data :
        if model == 0:
            new_data[idx, :] = whitenData(d);
        if model == 1:
            new_data[idx, :] = squashData(d, lb, ub);
            
        idx = idx + 1;

    return new_data

# data1 and data2 are 1-dimensional numpy arrays
def getCorr(data1, data2) :
    return (whitenData(data1) * whitenData(data2)).mean(axis=0) ;

def binarize(data, threshold=0.0) :
    ret = np.zeros(np.shape(data)) ;
    idx = 0;
    for d in data :
        if d >= threshold :
            ret[idx] = 1 ;
        else :
            ret[idx] = 0;
        idx = idx + 1;

    return ret;

def findRegressionDiff(predictor, target, model = 0) :
    if model == 0 :
        reg = lm.LinearRegression();
    if model == 1 :
        reg = lm.BayesianRidge();

    reg.fit(predictor.reshape([len(predictor), 1]), target)
    diff = target - reg.predict(predictor.reshape([len(predictor),1]))
    return diff;

# this has look ahead problem, need to have a running
# version. 
def plotRegressionDiff(dmax, dmin) :
    diff = findRegressionDiff(dmax, dmin, 0);
    pdf_, xplot, cdf_, thres, min_, max_ = toPDF(diff, 100) ;

    #### start to plot
    #### plot the max and min curve and the prediction difference on the second y axis 
    ax = graphPriceByColumn(np.c_[dmax, dmin].T, np.array([0,1]), 'ccy curve', 1)
    ax2 = ax.twinx()
    ax2.plot(range(len(diff)), diff, 'y-.')

    #### plot pdf of the diff
    fig = pl.figure()
    ax = fig.add_subplot(111);
    ax.plot(xplot, pdf_, 'b.')
    ax2 = ax.twinx()
    ax2.plot(xplot, cdf_, 'r-')

    return diff, thres

## data is supposed to be in shape of ccy_cum_rate([ccy, samples])
def plotMaxMinCorr(data) :
    dmax = data.max(axis=0);
    dmin = data.min(axis=0);
    diff, thres = plotRegressionDiff(dmax, dmin)
    return dmax, dmin, diff, thres

# X has shape of [samples, features], Y has the shape of [samples]
# returns the score of the regression, the residual and the object itself
def multiRegress(X, Y) :
    reg = lm.LinearRegression();
    reg.fit(X, Y);
    return  reg.score(X,Y), Y - reg.predict(X), reg;

### time sereis stuff, one dimension
def findACF(data, lags=10) :
    acf = np.zeros(lags);
    idx = 0;
    tot_len = len(data);
    while idx < lags :
        acf[idx] = getCorr(data[0:tot_len-idx], data[idx:tot_len]);
        idx = idx + 1;

    return acf;

def findPACF(data, lags=10) :
    pacf = np.zeros(lags+1);
    tot_len = len(data);
    pacf[1] = getCorr(data[0:tot_len-1], data[1:tot_len]);

    idx = 2;
    while idx <= lags :
        # create a predictor array for regression purpose
        # for X(t-1), X(t-2), ... , X(t-h+1)
        predictors = data[1 : tot_len-idx + 1].reshape([tot_len-idx, 1])
        predictor_idx = 2;

        while predictor_idx < idx :
            predictors = np.c_[predictors, data[predictor_idx:tot_len-idx+predictor_idx]];
            predictor_idx = predictor_idx + 1;

        # create two regressions according the predictors
        score, res1, reg = multiRegress(predictors, data[0:tot_len-idx]);
        score, res2, reg = multiRegress(predictors, data[idx:tot_len]);

        pacf[idx] = getCorr(res1, res2);
        idx = idx + 1;

    return pacf

# returns an array with 1 element less
def diffData(data) :
    new_data = np.empty(len(data)-1)
    idx = 1 ;
    while idx < len(data) :
        new_data[idx-1] = data[idx] - data[idx-1] ;
        idx = idx + 1 ;

    return new_data ;

# generate two AR(1) series 
def genAR(samples, phi=1.0, delta = 0.0) :
    z = np.random.normal(0,5,samples) ;
    x = np.empty(samples);

    x[0] = 0 ;
    idx = 1;
    while idx < samples :
        x[idx] = delta + x[idx-1]*phi + z[idx] ;
  	idx = idx + 1;
 
    return x

def genCoIntAR(data, coef = -0.85, intercept = 0.0) :
    z = np.random.normal(0, 5, samples) ;
    return data*coef + z + intercept ;

## bound is the length of the rope, step_ratio is
## percent of bound for each step. i.e. speed of the
## dog 
## it turns out that the longer the rope and the
## slower the dog, the less co-integrated of these
## two series
def genLeashedDog(data, bound, step_ratio = 0.01) :
    samples = len(data);
    new_data = np.empty(samples)
    z=np.random.normal(0, 1, samples)

    idx = 0;
    cur_diff = 0;
    while idx < samples :
        cur_diff = cur_diff + z[idx]*bound*step_ratio;
        if abs(cur_diff) > bound :
            cur_diff = cur_diff - 2*z[idx]*bound*step_ratio
        new_data[idx] = data[idx] + cur_diff ;
        idx = idx + 1;

    return new_data

## multiple regression: use every other thing to regress this one
## data is in ccy_cum_rate format: [features, samples]
## model: 0 is OLS, 1 is Bayesian Ridge
def plotMultiRegression(data, predictor_idx, cidx, model = 0, if_plot = True) :
    features = len(predictor_idx)
    samples = data.shape[1];

    regressing = np.empty([samples, features]);

    idx = 0;
    reg_idx = 0;
    for d in data :
         if idx == cidx :
             targ = d;
         else :
             if idx in predictor_idx :
                 regressing[:,reg_idx] = d;
                 reg_idx = reg_idx + 1;

         idx = idx + 1;

    if model == 0 :
        reg = lm.LinearRegression();
    if model == 1 :
        reg = lm.BayesianRidge();

    reg.fit(regressing, targ);
    pred_targ = reg.predict(regressing);
    diff = targ - pred_targ;
    pdf_, xplot, cdf_, thres, min_, max_ = toPDF(diff, 100) ;

    if if_plot != True :
        return diff, thres;

    #### start to plot
    #### plot the max and min curve and the prediction difference on the second y axis 
    ax = graphPriceByColumn(np.c_[targ, pred_targ].T, np.array([0,1]), 'ccy curve', 1)
    ax2 = ax.twinx()
    ax2.plot(range(len(diff)), diff, 'y-.')

    #### plot pdf of the diff
    fig = pl.figure()
    ax = fig.add_subplot(111);
    ax.plot(xplot, pdf_, 'b.')
    ax2 = ax.twinx()
    ax2.plot(xplot, cdf_, 'r-')

    return diff, thres

# data is expected in ccy_cum_rate format
# this finds the pair-wise (order significant)
# co-integration residual's PACF's first coeff
def genPACFMatrix(data, model = 0) :
    features = data.shape[0]
    samples = data.shape[1]

    pacf = np.ones([features, features]);
    pdf_shape = np.ones([features, features]);
    for pred in range(features) :
        for targ in range(features) :
            if targ == pred :
                continue;

            diff = findRegressionDiff(data[pred, :], data[targ, :], model);
            pacf[pred,targ] = findPACF(diff, 3)[1]
            pdf_, xplot , cdf_, pdf_shape[pred, targ], min_, max_ = toPDF(diff, 100);

    return pacf, pdf_shape;

# a scoring function to measure the goodness of the regression
# The rule is: given a list of pacf, pdf_shape, pdf_std values, 
# get those index that are :
# 1. within pacf_threshold of the best pacf, w.r.t. range of pacf
# 2. within shape_threshold of the best pdf_shape, w.r.t. range of pdf_shape
# 3. pick the highest index with pdf_std
# pdf_shape is defined as ratio of 90% of center samples range to the total range. 
# the smaller the better
# INPUT:
# pacf, pdf_shape and pdf_std all assumed to be 1 dimensional array, with the same length
# return the index of best element, its pacf, pdf_shape and std
def scoreRegression(pacf, pdf_shape, pdf_std, pacf_threshold=0.1, shape_threshold=0.05) :

    print pacf, pdf_shape, pdf_std

    pacf_min = pacf.min(axis=0)
    pacf_max = pacf_min + (pacf.mean(axis=0) - pacf_min)*2  # in case there are outliners
    pacf_range = pacf_max - pacf_min;

    samples = len(pacf);
    ### first round, accepting those with pacf less than acceptance line
    idx = 0;
    chosen1 = [];
    acceptance_pacf = pacf_min + pacf_range * pacf_threshold;
    while idx < samples :
        if pacf[idx] <= acceptance_pacf :
            chosen1.append(idx);
        idx = idx + 1;

    shape_min = pdf_shape[chosen1[0]];
    shape_max = pdf_shape[chosen1[0]];
    for idx in chosen1 :
        if pdf_shape[idx] > shape_max :
            shape_max = pdf_shape[idx];
        if pdf_shape[idx] < shape_min :
            shape_min = pdf_shape[idx]

    # detail:  set the shape_range to be the original data series's range
    shape_range = pdf_shape.max(axis=0)-pdf_shape.min(axis=0);

    ### second round, accepting those with shape less than acceptance line
    chosen2 = [];
    acceptance_shape = shape_min+shape_range*shape_threshold;
    for idx in chosen1 :
        if pdf_shape[idx] <= acceptance_shape :
            chosen2.append(idx);

    ### third round, pick the one with highest pdf_std
    max_idx = chosen2[0];
    max_std = pdf_std[max_idx];
    for idx in chosen2 :
        if pdf_std[idx] > max_std :
            max_idx = idx ;
            max_std = pdf_std[idx];

    return max_idx, pacf[max_idx], pdf_shape[max_idx], pdf_std[max_idx];

# return a list with indices of all the set bits. For example:
# genBitSet(127, 7) returns [0,1,2,3,4,5,6]
def genBitSet(number, bit_len) :
    mask = 1;
    bits = [];
    for b in range(bit_len) :
        if number & mask != 0 :
            bits.append(b) ;
        mask = 2*mask;
    return bits;

def getArrayElementByIndices(arr, index) :
    elements = [];
    for idx in index :
        elements.append(arr[idx]);

    return elements;

# For all possible combinations, find the best subset of predictors
# according to the scoreRegression 
def findBestRegressionCombo(data, predictors, targ, model = 0, pacf_thres = 0.1, shape_thres=0.05) :
    bit_len = len(predictors) ;
    subsets = 2**bit_len ;
    pacf = np.zeros(subsets) ;
    pdf_shape = np.zeros(subsets) ;
    pdf_std = np.zeros(subsets) ;

    bitset = 1;
    while bitset < subsets :
        indices = getArrayElementByIndices(predictors, genBitSet(bitset, bit_len)) ;
        diff, pdf_shape[bitset] = plotMultiRegression(data, np.array(indices), targ, model, False);
        pacf[bitset] = findPACF(diff)[1];
        pdf_std[bitset] = diff.std(axis=0);
        bitset = bitset + 1;

    max_idx, pacf_max, shape_max, std_max = scoreRegression(pacf[1:subsets], pdf_shape[1:subsets], pdf_std[1:subsets], pacf_thres, shape_thres);
    return getArrayElementByIndices(predictors, genBitSet(max_idx+1, bit_len)), pacf_max, shape_max, std_max;

#def getRunningEstimation(data):

# data is in [features, samples] format
# it finds best regression combo for all the features
def findBestRegressionComboForAll(data, model = 0, pacf_thres=0.1, shape_thres=0.05) :
    features = data.shape[0];
    pacf = np.zeros(features);
    pdf_shape = np.zeros(features);
    pdf_std = np.zeros(features);
    predictors = []

    for targ in range(features) :
        pred, pacf[targ], pdf_shape[targ], pdf_std[targ] = findBestRegressionCombo(data, np.delete(np.array(range(features)), targ, 0), targ, model, pacf_thres, shape_thres);
        predictors.append(pred);

    return predictors, pacf, pdf_shape, pdf_std;

###############################################
# overall thoughts on 5/23/2013
#
# The idea is to use events as guidance for mean reversion or trend following. 
# if there is no fundemental change, then I will do mean reversion on the pair of the outlier one and the one that mispriced most
# if the outlier one's change is a confirmed one, based on historical correlation. 
# if there is an event related change, I will do a trend following on this one, as well as explore the mis-priced 
# based on correlation. 
# in order to achieve this, I need the following components in back testing:
# 1. get the event calender into the testing.  
# 2. profile a base line correlation/structure change of the 8 time series ccy data, this should include tracking a reliable 
#    correlation relationship especially during big changes. 
# 3. detect big changes
###############################################

# this parses the time strings from the event file:
# in the format of:
# str1 = 'Sun Apr 21'
# str2 = '22:45'
# need to construct a time string as 
#       YY-04-21T22:45:00
# if the time string is specified in local time, then the utc is adjusted.
# for example, if the time string is in EDT, GMT-4, then 
#      gmt_offset_hour = -4*3600
def toUTC(str1, str2, year_str, gmt_offset_hour = 0) :
    if str2 == '' :
        str2 = '00:00' ;

    MM = getMonthNumber(str1.split()[1]) ;
    str = year_str + '-' + MM + '-' + str1.split()[2] + 'T' + str2 + ':00' ;
    dt64 = np.datetime64(str);
    return dt64.astype(int)/1000000 + gmt_offset_hour;

# the events come from one big merged file containing rows like:
#   Sun Mar 3,23:30,GMT,AUD,AUD TD Securities Inflation (YoY),Medium,2.4%,,2.5%
# sort is not needed.  the calling can be:
# events = corr.parseEvent('data/cal/merged_events.csv', '2013');
# np.savetext('merged_events', events)
def readEvent(event_file, YY, gmt_offset_hour = 0) :
    raw_events = np.loadtxt(event_file, delimiter=',', usecols=(0,1,3,5), dtype=('str')) ;
    total_row = raw_events.shape[0];
    events = np.zeros([total_row, 3], dtype = int);
    idx2 = 0;

    for idx in range(total_row) :
        events[idx2, 0] = toUTC(raw_events[idx,0], raw_events[idx,1], YY, gmt_offset_hour) ;

        try :
            events[idx2, 1] = getCCYnumber(raw_events[idx, 2]);
        except:
            print raw_events[idx, 2] + ' not in ccy list, skipping...' ;
            continue;

        str = raw_events[idx,3].upper();
        if str == 'LOW' :
            events[idx2, 2] = 0 ;
        elif str == 'MEDIUM' :
            events[idx2, 2] = 1 ;
        elif str == 'HIGH' :
            events[idx2, 2] = 2 ;
        else :
            continue;
            
        idx2 = idx2 + 1;

    event_order = events[0:idx2,0].argsort();
    return np.take(events[0:idx2, :], event_order, 0);

def graphEvent(events, start_time, end_time = -1, onto_ax = 0) :
    size_str = ['small', 'medium', 'large'];
    mark_str = ['bs', 'yo', 'rx'];
    if end_time == -1 :
        end_time = max(events[:,0])+ 1;

    if onto_ax == 0 :
        fig = pl.figure();
        ax = fig.add_subplot(111);
    else :
        ax = onto_ax.twinx();

    for event in events :
        t = event[0];
        if t < start_time or t > end_time :
            continue;
 
        marker = mark_str[int(event[2])];
        ax.plot(t-start_time, event[1], marker, markersize=6.0+event[2]*4);

    ax.set_ylim(-1, len(ccy))

# data is in ccy_cum_rate format
def getCorrMatrixSAD(data, look_back_window, update_period, x_start = 0, plot = False) :
    total_sec = data.shape[1];
    cov, par = getCov(data[:, 0 : look_back_window]);
    idx = look_back_window + update_period;
    diff = [];
    diff2 = [];

    pdiff = [];
    pdiff2 = [];

    cov_org = cov;
    par_org = par;

    x = [];
    while idx < total_sec :
        cov2, par2 = getCov(data[:,idx-look_back_window:idx]);
        diff.append(sum(sum(abs(cov-cov2))));
        diff2.append(sum(sum(abs(cov_org-cov2))));
        cov = cov2 ;

        pdiff.append(sum(sum(abs(par-par2))));
        pdiff2.append(sum(sum(abs(par_org-par2))));
        par = par2;
        x.append(x_start + idx);
        idx = idx + update_period ;

    if plot :
        fig = pl.figure();
        ax = fig.add_subplot(111);
        ax.plot(x, diff2, 'rx');
        ax2 = ax.twinx();
        ax2.plot(x, pdiff2, 'b.');

    return np.array(diff), np.array(diff2), np.array(pdiff), np.array(pdiff2), np.array(x)

def serializeCovMatrix(cov) :
    dim = cov.shape[0];
    ret = [];
    for i in range(dim) :
        for j in range (dim - i - 1) :
            ret.append(cov[i, i+1+j]);

    return np.array(ret);

# correlation time series - I need to study the correlation structure change, this could
# help understanding the persistence of some correlations.  The reason to calculate this
# is to see how individual cp's correlation, but wait a minutes, isn't that just cp price
# movement hd? 
# This function will calculate so called "Parsin Product" correlation matrix on 8 ccy. 
# with 28 correlation (features) per sample at each time. 
# This [N,28] time series will be reduced in features by PCA and then cluster them. 
# The finally output is the 1 dimension array with cluster number at each time. 
# The purpose of this exercise is to visualize how correlation changes w.r.t. 
# the market and events
#
# Parameters:
# data is shape of ccy_cum_rate
# look_back_window is number of sample to look back when estimating corr matrix
# update_period is sample frequency
# x_start is an optional UTC time stamp as offset in result x-array (time stamp)
def getCorrTimeSeries(data, look_back_window, update_period, x_start = 0, model = 0) :
    cov_array = [];
    x = [];
    total_sec = data.shape[1];
    idx = look_back_window;

    while idx < total_sec :
        cov, par = getCov(data[:, idx - look_back_window: idx], 1, False, True, False);
        cov_array.append(serializeCovMatrix(cov));
        x.append(x_start + idx);
        idx = idx + update_period;

    return np.array(x), np.array(cov_array)

#    cov_np_array = np.array(cov_array);
# reducing dimension of cov_array
#    pca = PCA(n_components = 0.9, whiten = False);
#    pca.fit(cov_np_array);

#    y = pca.transform(cov_np_array);9

# now feed to a cluster

#    two_means = cluster.MiniBatchKMeans(n_clusters=2)
#    ap = cluster.AffinityPropagation(damping=.5, max_iter = 200);
#    ap.fit(y);
#    n_clusters = ap.cluster_centers_indices_ ;
#    n_labels = ap.labels_;

#    return n_clusters, n_labels , np.array(x), cov_np_array, pca;
#    return np.array(x), cov_np_array, pca;

def getCorrTimeSeries_non_overlap(data, look_back_window, x_start = 0) :
    cov_array = [];
    x = [];
    total_sec = data.shape[1];
    idx = look_back_window;

    while idx < total_sec :
        cov, par = getCov(data[:, idx - look_back_window: idx], 1, False, True, False);
        cov_array.append(serializeCovMatrix(cov));
        x.append(x_start + idx);
        idx = idx + look_back_window;

    return np.array(x), np.array(cov_array)

def quantize_corr(data, significant_corr = 0.7) :
    ns = data.shape[0];
    fe = data.shape[1];

    for i in range(ns) :
        for j in range(fe) : 
            d = data[i,j] ;
            if d > significant_corr :
                data[i,j] = 1;
            else :
                if d < -1 * significant_corr :
                    data[i,j] = -1 ;
                else :
                    data[i,j] = 0;

    return data 

def plotEvents(ccy, start_sec, end_sec, start_utc, events, ax, marker_color) :
    events1 = events[np.where(events[:,1] == ccy)];
    marker = ['^', 'o', 'x']
    for e in events1 :
        t_sec = e[0] - start_utc ;
        if t_sec >= start_sec and t_sec < end_sec :
            ax.plot(t_sec, 1, marker_color + marker[int(e[2])], markersize=8+8*e[2]) ;

# this plots 1 CP curve vs 2 CCY curve
# start utc is the 
# ts is returned by getWeek
def plotCPvsCCY(hd, cum_ccy_rate, cp_symbol, start_sec = 0, secs = 600, start_utc = -1, events = []) :
    total_samples = hd.shape[1];
    cp = getCPFromSymbol(cp_symbol);
    if secs == -1 :
        end_sec = total_samples;
    else :
        end_sec = start_sec + secs;

    cp_line = hd[cp, start_sec:end_sec];
    ccy_lines = np.vstack((cum_ccy_rate[ccy1n[cp], start_sec:end_sec], cum_ccy_rate[ccy2n[cp], start_sec:end_sec]));
    labels = [ccy[ccy1n[cp]], ccy[ccy2n[cp]]];
    ax, ax2 = graphFeature(cp_line, ccy_lines, labels);
    if start_utc != -1 :
        # go through the events of ccy1
        plotEvents(ccy1n[cp], start_sec, end_sec, start_utc, events, ax2, 'r');
        plotEvents(ccy2n[cp], start_sec, end_sec, start_utc, events, ax2, 'b');

# input str format:
# 20130101 170000??? 
# output: utc epoch time
def getIBTime(str) :
    return int(time.mktime(time.strptime(str[0:15], "%Y%m%d %H%M%S")))

def isMarketClosed(str) :
    tm = time.strptime(str[0:15], "%Y%m%d %H%M%S");
    return tm.tm_wday == 5 or (tm.tm_wday == 4 and tm.tm_hour >= 16) or (tm.tm_wday == 6 and tm.tm_hour < 18)

#read in data from histData in the format of
#20130101 170000;1.320410;1.320410;1.320340;1.320340;0
def readHistData(filename) :
    data = np.loadtxt(filename, delimiter=';', dtype = str);
    histdata = [];
    for d in data : 
        t = getIBTime(d[0])
        if t == 0 :
            continue;
        histdata.append([ getIBTime(d[0]), float(d[1]), float(d[2]), float(d[3]), float(d[4])])

    return np.array(histdata);


# read in tick data and convert to bar
# 20131201 170002123,1.358680,1.358850,0
# output as 
# utc-timestamp open max min close
def readTickData(filename, barsize) :
    data = np.loadtxt(filename, delimiter=',', dtype = str);
    bar = [];
    ts = getIBTime(data[0,0])
    barEnd = ts + barsize
    
    bopen = (float(data[0,1]) + float(data[0,2]))/2.0
    bclose = bopen
    bmax = bmin = bclose;

    for d in data :
        ts = getIBTime(d[0]);
        while ts > barEnd :
            if not isMarketClosed(d[0]) :
                bar.append([barEnd-barsize, bopen, bmax, bmin, bclose]);
            bopen = bclose;
            bmax = bmin = bopen;
            barEnd += barsize
        bclose = (float(d[1]) + float(d[2]))/2.0
        if bmax < bclose :
            bmax = bclose
        else :
            if bmin > bclose :
                bmin = bclose

    return np.array(bar)

def readInTick(filename) :
    tick=[];
    data = np.loadtxt(filename, delimiter=',', dtype = str);
    for d in data :
        tick.append([getIBTime(d[0]) + float(d[0][15:])/1000.0,(float(d[1])+float(d[2]))/2])

    return np.array(tick)

# get one year of tick data from HistData into bar
# assume 1 month a file as 201301.csv
# year is in str, i.e. '2013'
# barsize is in seconds, i.e. 10
def readTickDataYear(year, barsize) :
    file_prefix = '/home/zfu/test/data/IB/EURUSD/tick/DAT_ASCII_EURUSD_T_'
    out_prefix =  '/home/zfu/test/data/IB/EURUSD/tick/DAT_'+str(barsize)+'S_'

    m = int(year + '01')
    endM = m+12
    bar = readTickData(file_prefix + str(m) + '.csv', barsize)
    m = m+1

    while m < endM : 
        b = readTickData(file_prefix + str(m) + '.csv', barsize)
        #np.savetxt(out_prefix+str(m), b)
        bar = np.vstack((bar,b))
        m+=1

    return bar

# return \sum_0^k (x-\bar{x})^2
def getXVec(k) :
    i = 1;
    vec = [];
    vec.append(0)
    
    while i <= k :
        j = 0;
        s = 0;
        m = i/2.0
        while j <= i :
            s += ((j-m)*(j-m))
            j += 1
            
        vec.append(s)
        i += 1
        
    return np.array(vec)

MaxXVec = 1000
xvec=getXVec(MaxXVec)
    
# xvec[k] is \sum_0^k (x-\bar{x})^2
# end not included
def getMSE(data, start, end) :
    num_points = end - start;
    if num_points < 2 :
        return 0, 0

    y = np.array(data[start:end])
    ymean = np.mean(y)
    y = y - ymean
    slp = 0
    xm = (num_points-1)/2.0
    for i in range(end-start) :
        slp += (y[i]* (i-xm))

    slp /= xvec[end-start-1]
    b = -1*slp*(num_points-1)/2.0
    mse = 0;
    idx = 0
    while idx < num_points :
        diff = y[idx] - (b + idx*slp)
        mse += (diff*diff)
        idx += 1
        
    return mse, slp, b+ymean

def segment(data, start, end, k, ans) :
    if ans[start,end,k,0] != 0 :
        return ans[start,end,k, :]

    if k == 1 :
        mse, slp, b = getMSE(data, start, end)
        ans[start, end, 1, 0] = 1
        ans[start, end, 1, 1] = mse
        ans[start, end, 1, 2] = end
        return ans[start,end, 1,:]

    num_points = end-start
    maxk = ans.shape[3]
    if num_points < 3 :
        res = np.zeros(maxk)
        res[0] = 1
        res[2] = end
        return res

    z = start + 1;
    res = segment(data, start+1, end, k-1, ans)
    mse = res[1]
    path = np.array(res[1:])
    path[0] = z

    z = start + 2;
    while z < end-1 :
        res1 = segment(data, start, z+1, 1, ans)
        mse1 = res1[1]
        res2 = segment(data, z, end, k-1, ans)
        mse2 = res2[1]
        if mse1 + mse2 < mse :
            mse = mse1 + mse2
            path[0] = z
            path[1:k] = res2[2:k+1]
        z += 1;

    ans[start, end, k, 0] = 1
    ans[start, end, k, 1] = mse
    ans[start, end, k, 2:k+2] = path[0:k]
    return ans[start, end, k,:]
        
def getSegment(data, maxk, ans) :
    N=data.shape[0]
    ans=np.zeros([N+1,N+1,maxk+1,maxk+2])
    
    global MaxXVec
    if MaxXVec < N :
        MaxXVec = N
        global xvec
        xvec = getXVec(MaxXVec)

    k = 1
    while k <= maxk :
        segment(data,0,N,k,ans)
        k += 1

    return ans[0,N]

def plotTrade(price, trade_time) :
    p = []
    for t in trade_time :
        p.append(price[np.where(price[:,0] == t)][0])

    pl.plot(price[:,0], price[:,1], 'b.-')
    pl.plot(trade_time, np.array(p)[:,1], 'rx', markersize=10)

## this is a tool to segment a time series linearly piece-wise and get the 
## latest line segment to 
def getTrend(data, idx, lookback_bars, barsize = 5, msethresh = 0.35, minSeg=3, maxSegments = 5) :
    maxk = min(int(lookback_bars/minSeg + 0.5), maxSegments)
    ans = []
    ans = getSegment(data[idx+1-lookback_bars:idx+1], maxk, ans)
    k = 1
    mse=ans[1,1]
    while k < maxk :
        this_mse = ans[k,1];
        if this_mse/mse <= msethresh :
            break;
        k += 1

    # we picked smallest k 
    if k == 1 :
        last_seg_start = 0
    else :
        last_seg_start = ans[k, k]
    dx = int(lookback_bars - last_seg_start - 1)
    last_seg_start += (idx + 1 - lookback_bars)

    mse, slp, b = getMSE(data, int(last_seg_start), idx+1)
    return slp/barsize, dx , mse/(dx+1.0)

def printVersionCorr() :
    print 'version 1.0'

def sg(ifShow=0) :
    if ifShow == 0 :
        pl.show(block=False)
    else :
        pl.show()

def processCitiEvents(csvfname, outfname) :
    events = np.genfromtxt(csvfname, delimiter = ',', dtype='str', usecols=(0,1,2))
    ts = []
    
    for e in events[1:] :
        ts.append(np.datetime64(e[0]+' ').astype(int)/1000000)

    
    ts = np.array(ts)
    event_cnt = events.shape[0]-1

    
    np.savetxt(outfname, np.hstack([ts.reshape([event_cnt,1]), np.ones(event_cnt).reshape([event_cnt, 1]), events[1:, 2:], events[1:, 1:2]]), fmt='%s,%s,%s,%s')

    
