#!/usr/bin/python3

import numpy as np
import datetime
import time
import copy
import os
import subprocess
import yaml
import traceback
import sys
import threading

import mts_util
import symbol_map

class HICS_CALH :
    def __init__(self, liveheader_path, strat_name = 'INTRADAY_MTS_HICS_CALH', strat_weight = 'STRATEGY_WEIGHTS.yaml', POD_id = 'TSC', check_interval_sec=300) :
        self.logger =  mts_util.MTS_Logger('HICS_CALH')
        self.lhpath = liveheader_path
        self.strat_name = strat_name
        self.lhname = 'LiveHO_'+ strat_name
        self.pod = POD_id
        self.swname = os.path.join(liveheader_path,strat_weight)
        self.sw = 0.0
        self.check_interval_sec = check_interval_sec

    def reload(self, cur_trade_day=None) :
        # read the strategy weight 
        try :
            with open(self.swname, 'r') as f:
                sw = yaml.safe_load(f)
                strat_name = self.strat_name
                if self.pod is not None and len(self.pod)>0:
                    strat_name += ('_'+self.pod)
                self.sw = float(sw[strat_name])
        except :
            self.logger.logError('failed to get strategy weight for ' + self.strat_name + ', setting weight to 0\n' + traceback.format_exc())
            raise ValueError("strategy weight not found!")

        cfg_file = os.path.join(self.lhpath, self.lhname)+'.cfg'
        cfg = mts_util.MTS_Config(cfg_file)
        trade_day = cfg.get('trade_date')

        if cur_trade_day is None :
            cur_trade_day = mts_util.TradingDayUtil().get_trading_day(snap_forward=True)
        if cur_trade_day != trade_day :
            self.logger.logError("config file " + cfg_file + " has mismatch trading day of " + trade_day + ", not loaded!")
            raise ValueError("trading day mismatch in " + cfg_file)

        # read config
        self.trade_day = trade_day
        self.strat_name = mts_util.StratUtil.strat_code(cfg.get('strategy_code'), pod_id=self.pod)
        symbols = cfg.listSubKeys('live_output')

        # get today's symbol map and check risk positions with model's trading symbols
        mts_cfg = mts_util.MTS_Config('config/main.cfg')
        sym_map = symbol_map.SymbolMap()

        # figure out the subscription list, only run symbols that have been subscribed
        sub_symbol = sym_map.getSubscriptionList(mts_cfg, trade_day)
        symbols0 = list(set(symbols) - (set(symbols) - set(sub_symbol)))

        if len(symbols0) != len(symbols):
            self.logger.logInfo("Removed unsubscribed symbols: " + str(set(symbols) - set(sub_symbol)))
            symbols = symbols0

        # check the risk based on the position, only run symbols that are in risk
        risk_cfg = mts_cfg.get('Risk')
        mts_risk_cfg = mts_util.MTS_Config(risk_cfg)
        
        # make sure the strategy is in the risk
        risk_strat_list = mts_risk_cfg.listSubKeys('strat')
        if self.strat_name not in risk_strat_list:
            self.logger.logError(self.strat_name + " not in risk strategy list " + str(risk_strat_list))
            raise ValueError(self.strat_name + " not in risk strategy list " + str(risk_strat_list))
        risk_key = 'strat.'+self.strat_name+'.Symbols'
        risk_symbols = mts_risk_cfg.listSubKeys(risk_key)

        # ... remove mts_symbols whose symbol not in risk
        smap = sym_map.get_tradable_map(trade_day, mts_key=True)
        symbols0 = []
        for sym in symbols:
            if smap[sym]['symbol'] in risk_symbols:
                symbols0.append(sym)
        if len(symbols0) != len(symbols):
            self.logger.logInfo("Removed symbols not in risk: " + str(set(symbols) - set(risk_symbols)))
            symbols = symbols0

        run = {}
        for sym in symbols :
            run[sym] = {'ix':0, 'hours':[]}
            key = 'live_output.'+sym+'.params'
            hours = cfg.listSubKeys(key)
            utc_list = []
            ts_list = []
            qty_list = []
            for h in hours :
                if h[0] == 'H' and len(h) == 3:
                    # Hxx
                    ts = self.trade_day +'-'+ h[1:]+':00:00'
                    ts_list.append(ts)
                    utc_list.append(int(datetime.datetime.strptime(ts, '%Y%m%d-%H:%M:%S').strftime('%s')))
                    qty0 = cfg.get(key + '.'+h, float) * self.sw
                    qty_list.append(int(abs(qty0) + 0.5)*np.sign(qty0).astype(int))
                elif h[0] == 'T' and len(h) ==5:
                    # T[HH][MM]
                    ts = self.trade_day + '-' + h[1:3] + ':' + h[3:5] + ':00'
                    ts_list.append(ts)
                    utc_list.append(int(datetime.datetime.strptime(ts, '%Y%m%d-%H:%M:%S').strftime('%s')))
                    qty0 = cfg.get(key + '.'+h, float) * self.sw
                    qty_list.append(int(abs(qty0) + 0.5)*np.sign(qty0).astype(int))
            
            if len(utc_list) == 0:
                self.logger.logError(sym + " don't have any hours to run!")
                continue

            # sort with utc in increasing order
            ix = np.argsort(utc_list)
            utc = list(np.array(utc_list)[ix])
            qty = list(np.array(qty_list)[ix])
            ts = list(np.array(ts_list)[ix])
            for utc, qty, ts in zip(np.array(utc_list)[ix], np.array(qty_list)[ix], np.array(ts_list)[ix]):
                run[sym]['hours'].append({'utc':utc, 'ts':ts, 'qty':qty})
            run[sym]['ix'] = len(run[sym]['hours']) -1

        self.run_schedule = copy.deepcopy(run)

    def start(self, cur_trade_day = None, launch_time = [7, 55, 17, 0]):
        # reload the configuration and start the strategy during trading hour
        # cur_trade_day: yyyymmdd, specifies the current trading day. 
        # launch_time: [start_hour, start_minute, end_hour, end_minute], specifies
        #              the trading hour
        #              if not specified, it starts at any time.
        #              When launched by the main mts, default sustain schedule, 
        #              [-6,0,17,0]) applies. 
        #
        # Note if the strategy loaded a configuration file with a different
        # trading day, the start would throw expection. 

        if cur_trade_day is None :
            cur_trade_day = mts_util.TradingDayUtil().get_trading_day(snap_forward=True)


        if launch_time is None or len(launch_time) != 4:
            launch_time = [-6, 0, 17, 0]

        utc = int(datetime.datetime.strptime(cur_trade_day, '%Y%m%d').strftime('%s'))
        utc0 = utc + launch_time[0]*3600 + launch_time[1]*60
        utc1 = utc + launch_time[2]*3600 + launch_time[3]*60
        self.logger.logInfo("waiting for launch time during " + str(datetime.datetime.fromtimestamp(utc0)) + " to " + str(datetime.datetime.fromtimestamp(utc1)))
        while True:
            cur_utc = int(datetime.datetime.now().strftime('%s'))
            if cur_utc >= utc0 and cur_utc < utc1 :
                self.logger.logInfo("within the launch time, kicking off")
                break
            time.sleep(1)

        # try to load the configuration file
        self.reload(cur_trade_day = cur_trade_day)

        # initialize the ix based on current time
        cur_utc = int(datetime.datetime.now().strftime('%s'))
        symbols = self.run_schedule.keys()
        for sym in symbols :
            sym_run = self.run_schedule[sym]
            sym_run['last_run_utc'] = cur_utc
            hours = sym_run['hours']
            for i, h in enumerate(hours):
                if h['utc'] > cur_utc+300:
                    # we take the latest time earlier 
                    sym_run['ix'] = max(i-1,0)
                    break

        self.logger.logInfo("Starting: " + self.dump())
        done = False
        while not done :
            try :
                done = self.onOneSecond()
            except KeyboardInterrupt as e :
                self.logger.logInfo("keyboard interrupt, exiting")
                done = True
            except :
                self.logger.logError("Exception while running: " + traceback.format_exc())
                time.sleep(1)
            time.sleep(0.2)

        self.logger.logInfo("Done for the day, waiting for exit!")
        while True:
            cur_utc = int(datetime.datetime.now().strftime('%s'))
            if cur_utc >= utc1 :
                break
        self.logger.logInfo("Exit!")

    def onOneSecond(self, cur_utc = None) :
        if cur_utc is None :
            cur_utc = int(datetime.datetime.now().strftime('%s'))

        done = True
        for sym in self.run_schedule.keys():
            sym_run = self.run_schedule[sym]
            ix = sym_run['ix']
            hours = sym_run['hours']
            last_run_utc = sym_run['last_run_utc']
            if ix < len(hours):
                done = False
                run_item = hours[ix]
                utc = run_item['utc']
                qty = run_item['qty']
                if cur_utc >= utc:
                    #self.logger.logInfo("running " + str(run_item) + " for " + sym)
                    self._run(sym, qty)
                    sym_run['ix'] += 1
                    sym_run['last_run_utc'] = cur_utc
                elif ix > 0 and cur_utc >= last_run_utc + self.check_interval_sec:
                    run_item = hours[ix-1]
                    utc = run_item['utc']
                    qty = run_item['qty']
                    self._run(sym, qty)
                    sym_run['last_run_utc'] = cur_utc

        return done

    def start_sim(self, cur_utc, step_sec = 300, cur_trade_day = None) :
        # try to load the configuration file
        if cur_trade_day is None :
            yyyymmdd_hh_mm_ss = datetime.datetime.fromtimestamp(cur_utc).strftime('%Y%m%s-%H:%M:%S')
            cur_trade_day = mts_util.TradingDayUtil().get_trading_day(yyyymmdd_hh_mm_ss, snap_forward = True)
        self.reload(cur_trade_day = cur_trade_day)

        # initialize the ix
        symbols = self.run_schedule.keys()
        for sym in symbols :
            sym_run = self.run_schedule[sym]
            hours = sym_run['hours']
            for i, h in enumerate(hours):
                if h['utc'] > cur_utc+300:
                    sym_run['ix'] = max(i-1,0)
                    break

        self.logger.logInfo("Starting: " + self.dump())
        done = False
        while not done :
            done = self.onOneSecond(cur_utc)
            cur_utc += step_sec
            time.sleep(0.1)

        self.logger.logInfo("Done!")

    def dump(self) :
        ret = 'HICS Dump (weight = ' + str(self.sw) + '):\n'
        for sym in self.run_schedule.keys():
            sym_run = self.run_schedule[sym]
            ret += sym + '('+str(sym_run['ix'])+')\n    '+str(sym_run['hours'])+'\n'
        return ret

    def _run(self, symbol, qty):
        # set position for symbol with target position to be qty
        run_str = ['/home/mts/run/bin/flr',  'X',  self.strat_name + ', ' + symbol + ', ' + str(qty) + ', T10m']
        try :
            ret = subprocess.check_output(run_str)
            #self.logger.logInfo("run command " + str(run_str) + " got " + str(ret))
        except subprocess.CalledProcessError as e:
            self.logger.logError("Failed to run command " + str(run_str) + ", return code: " + str(e.returncode) + ", return output: " + str(e.output))


def HICS_Config(cfg_file = '/home/mts/run/config/strat.cfg', model_name = 'HICS_CALH') :
    if cfg_file is None :
        cfg_file = '/home/mts/run/config/strat.cfg'
    # check if there are HICS_CALH from the strat_cfg's PyRunList
    mts_cfg = mts_util.MTS_Config(cfg_file)
    py_runlist = mts_cfg.getArr('PyRunList')
    model_cfg = None
    for py_strat in py_runlist :
        mdl_name, mdl_cfg = mts_cfg.getArr(py_strat)
        if mdl_name == model_name:
            model_cfg = mdl_cfg
            break
    if model_cfg is None :
        raise ValueError("no " + model_name + " found in " + cfg_file + "'s PyRunList: " + str(py_runlist))

    mts_cfg = mts_util.MTS_Config(model_cfg)
    param = {}
    keys = ['POD_id', 'liveheader_path', 'strat_name', 'strat_weight', 'check_interval_sec']
    for k in keys:
        param[k] = mts_cfg.get(k)
    param['launch_time'] = mts_cfg.getArr('launch_time',int)
    return param

def HICS_Config_All_Models(cfg_file = '/home/mts/run/config/strat.cfg') :
    if cfg_file is None :
        cfg_file = '/home/mts/run/config/strat.cfg'
    # check if there are HICS_CALH from the strat_cfg's PyRunList
    mts_cfg = mts_util.MTS_Config(cfg_file)
    py_runlist = mts_cfg.getArr('PyRunList')

    param_list = []
    for py_strat in py_runlist :
        mdl_name, mdl_cfg = mts_cfg.getArr(py_strat)
        param_list.append(HICS_Config(cfg_file=cfg_file, model_name = mdl_name))
    return param_list

def launch_hics_model(param, trading_day = None) :
    """Launch a hics model in a thread. 
    param: the model's parameter returned by HICS_Config
    trading_day: optional yyyymmdd day, None for current trading day

    Return: python thread of the model
    """
    hics = HICS_CALH(liveheader_path    = param['liveheader_path'], \
                     strat_name         = param['strat_name'], \
                     strat_weight       = param['strat_weight'], \
                     POD_id             = param['POD_id'], \
                     check_interval_sec = param['check_interval_sec'])

    thread = threading.Thread(target = hics.start, args = (trading_day, param['launch_time']))
    thread.start()
    return thread

if __name__ == "__main__":
    cfg_file = None
    if len(sys.argv) >= 2 :
        cfg_file = sys.argv[1]
    param_list = HICS_Config_All_Models(cfg_file = cfg_file)
    trading_day = None
    if len(sys.argv) == 3 :
        trading_day = sys.argv[2]

    model_threads = []
    for param in param_list:
        model_threads.append(launch_hics_model(param, trading_day))

    for thread in model_threads :
        thread.join()

