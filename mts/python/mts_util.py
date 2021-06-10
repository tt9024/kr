import numpy as np
import datetime
import os
import traceback
import subprocess

class TradingDayIterator :
    def __init__(self, start_yyyymmdd, end_yyyymmdd) :
        """
        Both start and end days are inclusive. 
        """
        self.start = start_yyyymmdd
        self.end = end_yyyymmdd
        self.day = None
        self.dt = None

    def begin(self) :
        self.dt = datetime.datetime.strptime(self.start, "%Y%m%d")
        if self.start <= self.end :
            self._next(1)
        else :
            self._next(-1)
        return self.day

    def end(self) :
        return self.end

    def next(self) :
        self.dt = datetime.datetime.strptime(self.day, "%Y%m%d") + datetime.timedelta(1)
        self._next(1)
        return self.day

    def prev(self) :
        self.dt = datetime.datetime.strptime(self.day, "%Y%m%d") + datetime.timedelta(-1)
        self._next(-1)
        return self.day

    def _next(self, delta = 1) :
        while self.dt.weekday() > 4 or TradingDayIterator.is_holiday(self.dt) :
            self.dt += datetime.timedelta(delta)
        self.day = self.dt.strftime("%Y%m%d")

    @staticmethod
    def is_holiday(dt) :
        """
        New year and Chrismas day, to be extended
        """
        d = dt.month * 100 + dt.day
        if d == 101 or d == 1225 :
            return True
        return False

class TradingDayUtil :
    def __init__(self, shour=-6, smin=0, ehour=17, emin=0) :
        """
        shour: the starting hour of trading time, modulo 24 hour
        smin: the starting minute of trading time, relative to starting hour
              it could be negative
        ehour: the ending hour of trading time, ehour has to be positive
        emin: the ending minute of trading time, relative to ending hour
              it could be negative
        """
        assert (ehour > 0)
        self.tmstart = shour*60 + smin
        self.tmend =   ehour*60 + emin
        self.trd_len = self.tmend - self.tmstart

    def is_trading_time(self, dt) :
        """
        returns (True, day_yyyymmdd) if dt is a trading time, also returns a trading day in YYYYMMDD
                Otherwise, return (False, None)
        """
        tm = dt.hour*60 + dt.minute
        day = None
        if dt.weekday() == 5 :
            # Saturday
            return False, None
        elif dt.weekday() == 4 :
            # Friday
            if tm >= self.tmend :
                return False, None
        elif dt.weekday() == 6 :
            # Sunday
            if not ((self.tmstart < 0) and (tm >= (self.tmstart % (24*60)))) :
                return False, None
        else :
            # Monday to Thursday
            if not ((tm - self.tmstart)%(24*60) < self.trd_len) :
                return False, None
 
        dt_day = dt
        # trading day tomorrow if during over night session
        if dt.hour*60 + dt.minute >= self.tmend :
            dt_day += datetime.timedelta(1)

        if TradingDayIterator.is_holiday(dt_day):
            # holiday
            return False, None

        day = dt_day.strftime('%Y%m%d')
        return True, day

    def get_trading_day(self, yyyymmdd_hh_mm_ss = None, snap_forward=True) :
        """
        yyyymmdd_hh_mm_ss : in format of yyyymmdd-hh:mm:ss, use current time if None
        snap_forward : if the given time is not a trading time, look for the next
                       trading day in future.  Other look back to the previous one
        return the current trading day.

        Note Daily trading time defined in is_trading_time()
        """

        if yyyymmdd_hh_mm_ss is None :
            yyyymmdd_hh_mm_ss = datetime.datetime.now().strftime('%Y%m%d-%H:%M:%S')
        dt = datetime.datetime.strptime(yyyymmdd_hh_mm_ss, '%Y%m%d-%H:%M:%S')
        dtsign = 1 if snap_forward else -1

        is_trading, trading_day = self.is_trading_time(dt)
        while not is_trading :
            dt += datetime.timedelta(hours = dtsign)
            is_trading, trading_day = self.is_trading_time(dt)

        return trading_day

    @staticmethod
    def dt_to_utc(dt, dt_tz, machine_tz = 'US/Eastern') :
        """
        input: 
        dt: a datetime object in time zone of dt_tz, a fraction of second maybe given
        machine_ta: the timezone that the local machine uses when
                    running the dt.strftime('%s')
        return:
        a float: utc (with fraction of second if given) of dt in dt_tz
        """
        import pytz
        dtz = pytz.timezone(dt_tz)
        ttz = pytz.timezone(machine_tz)
        ddt = dtz.localize(dt)
        tdt = ttz.localize(dt)
        df = (ddt-tdt).total_seconds()
        lutc = float(dt.strftime('%s.%f'))
        return lutc+df

    @staticmethod
    def contract_month_code(month) :
        code = [ 'F','G','H','J','K','M','N','Q','U','V','X','Z']
        m = int(month)
        if m < 1 or m > 12 :
            raise RuntimeError("month out of range " + str(month))
        return code[m-1]

class MTS_Config :
    def __init__(self, cfg_file, cfgupd = 'bin/cfgupd') :
        self.cfg_file = cfg_file
        self.cfgupd = cfgupd

    def _run(self, action, key = '', value = '') :
        return subprocess.check_output([self.cfgupd, action, self.cfg_file, key, value]).decode().strip()

    def _csvLineToList(self, csv_str, type_func=str) :
        ret = []
        if len(csv_str) > 0 :
            val = csv_str.strip().split(',')
            for v in val: 
                ret.append(type_func(v.strip()))
        return ret

    def get(self, key, type_func=str) :
        """
        type_func specifies the return type, i.e. int, float, str, etc
        """
        return type_func(self._run('get', key))

    def getArr(self, key, type_func=str) :
        val = self._run('getArr', key)
        return self._csvLineToList(val, type_func=type_func)

    def listRootKeys(self) :
        val = self._run('list')
        return self._csvLineToList(val, type_func=str)

    def listSubKeys(self, key) :
        val = self._run('list', key)
        return self._csvLineToList(val, type_func=str)

    def arrayLen(self, key) :
        return self._run('count', key)

    def set(self, key, value) :
        self._run('set', key, str(value))

    def setArr(self, key, value_list) :
        val = ''
        if len(value_list) > 0 :
            val = str(value_list[0])
            for v in value_list[1:] :
                val = val + ', ' + str(v)
        self._run('setArr', key, val)

class MTS_Logger :
    def __init__(self, module, logger_file = None) :
        if logger_file is None :
            # get it from the config/main.cfg
            cfg = MTS_Config('config/main.cfg')
            logger_path = cfg.get('Logger')
            trade_day = TradingDayUtil().get_trading_day(snap_forward=True)
            logger_file = logger_path + '_' + trade_day + '.txt'

        self.lf = logger_file
        self.module = module

    def _log(self, level, module, logstr) :
        dt = datetime.datetime.now()
        line = dt.strftime('%Y%m%d-%H:%M:%S.%f') + ','+level+','+module+','+logstr+'\n'
        with open(self.lf,'a') as f :
            f.write(line)

    def logInfo(self, logstr) :
        self._log('INF', self.module, logstr)

    def logError(self, logstr) :
        self._log('ERR', self.module, logstr)

class RiskConfig :
    def __init__(self, cfg_file = None) :
        if cfg_file is None :
            cfg_file = 'config/risk.cfg'
        self.cfg = cfg_file

    def writeDefaults(self, strat_list, strat_pos = 500.0, strat_rate_limit = [3, 60], mts_pos = 1000, mts_order = 1000):
        # get all the symbols from symbol_map
        import symbol_map
        sym_list = symbol_map.SymbolMap().list_symbol()
        with open (self.cfg, "w") as f:
            f.write("strat = {\n")
            for strat in strat_list:
                f.write("    " + strat + " = {\n")
                f.write("        PaperTrading = 1\n")
                f.write("        Symbols = {\n")
                for sym in sym_list:
                    f.write("            " + sym + " = {\n")
                    f.write("                position = " + str(strat_pos) + "\n")
                    f.write("                rate_limit = " + str(strat_rate_limit) + "\n")
                    f.write("            }\n")
                f.write("        }\n")
                f.write("    }\n")
            f.write("}\n")

            f.write("mts = {\n")
            for sym in sym_list:
                f.write("    " + sym + " = {\n")
                f.write("        position = " + str(mts_pos) + "\n")
                f.write("        order_size = " + str(mts_order) + "\n")
                f.write("    }\n")
            f.write("}\n")

class StratUtil :
    @staticmethod
    def strat_code_prefix(pod_id=None, mts_code = '7000') :
        if pod_id is not None and len(pod_id) > 0 :
            return '-'.join([pod_id, mts_code, '']) 
        return '-'.join([mts_code,''])

    @staticmethod
    def strat_code(sub_strat_code, pod_id=None, mts_code = '7000') :
        return StratUtil.strat_code_prefix(pod_id=pod_id, mts_code=mts_code)+sub_strat_code

