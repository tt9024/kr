import os
import mts_repo
import forecast

mts_repo_mapping = {'CL':'WTI_N1', 'HG':'HGCopper_N1', 'GC':'Gold_N1', '6J':'JPY_N1'}

def get_md_days(symbol_arr, start_day, end_day, barsec, lpx_dict=None, repo_dir = '/home/mts/run/repo/mts_live_prod'):
    md_dict = {}
    for symbol in symbol_arr :
        lpx = None
        if lpx_dict is not None and symbol in lpx_dict.keys():
            lpx = lpx_dict[symbol]

        md_dict[symbol] = forecast.ForecastModel.md_days_from_mts(mts_repo_mapping[symbol], start_day, end_day, barsec=barsec, lpx=lpx, repo_dir=repo_dir)
    return md_dict

class ARData :
    def __init__(self, start_date, end_date, symbol) :
        self.sd = start_date
        self.ed = end_date
        self.symbol = symbol
        self.data = {'model':symbol}

    def _add_md(self, symbol) :
        import pyres_test as test
        ndays, n, dt, lr, vol, vbs, lpx = test.get_data_ALL(symbol, self.sd, self.ed)
        # save the n, dt, vol, vbs
        self.data[symbol] = {'n':n, 'dt':dt, 'vol':vol,'vbs':vbs}

    def _add_coef(self, std, statelr, vacorr) :
        pass

    def save(self, fname) :
        import dill
        fn = fname+'.dill'
        fn=fn.replace(' ','')
        print 'dumping to ', fn, 'and gzip'
        with open (fn, 'wb') as f :
            dill.dump(self.data,f)
        os.system('gzip -f ' + fn)


class GetData :
    def __init__(self, state_array, param) :
        """
        Pack data to be fed into the Forecast
        It gets raw data (bar and snap), process
        them with indicator functions, and pack
        the data to be feed into the Forecast

        In particular, it implements three interface
        - on_bar(self, k, bar_data)
          called on bar time, bar_data is length m of raw data
          since last bar. Returns length ni of indicators, i.e.,
          lr, vbs, quotes, and state modified, etc, in order.
          It also saves the state for next snap updates.
        - on_snap(self, snap_data)
          called on snap time, snap_data is length m of raw data
          since last bar time. Return length ni of indicators same
          as onBar(). 
        - end_of_day(self, daily_bar_data)
          called on daily update at EoD, to update the state of 
          indicators, such as vbs, and states modified indicators
        """
        self.state_array = copy.deepcopy(state_array)
        self.param = copy.deepcopy(param)
        self.n = param['n']  # bars per day
        self.init_state()

    def init(self) :
        """
        initialize the indicator states for vbs
        vbs['m']: shape n of means
        vbs['d']: shape n of std
        vbs['Li']: shape n of look back index (refer to vbs)
        vbs['rwnd']: shape [roll_day, n] of vbs
        vbs['bh']: the bucket hour
        vbs['decay']: the decay
        """
        self.vbs = self.param['vbs']


    def onBar(self, k, bar_data) :
        """
        k = 0 on 18:00, k = n on 17:00
        n: number of bars
        m: number of inputs
        """
        pass

def _fcst_func(bd, lr) :
    """
    update the 
    """

class BarData0 :
    def __init__(self, param) :
        """
        std, stlr, stlrh, ceof
        This creates object to run the Bar Data in real time
        """
        pass

    def upd(self, lr, k) :
        """
        This updates the lr, vector of m
        k is the bar index, 18:00 is bar 0, 17:00 is bar n
        """
        pass

    def upd_rt(self, lr) :
        """
        update the realtime bar and indicators
        """
        pass

