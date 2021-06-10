#!/usr/bin/python3

import subprocess
import os
import datetime
import signal
import sys
import time
import symbol_map
import traceback
import daily_update
import glob

_should_run = True

def printfl(text) :
    dt = datetime.datetime.now().strftime("%Y%m%d-%H:%M:%S")
    print(str(dt) + " " + str(text))
    sys.stdout.flush()
    sys.stderr.flush()

def signal_handler(signal, frame) :
    printfl( 'got signal '+ str(signal))
    _should_run = False
    kill_all()
    sys.exit(1)

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

procs=['bin/tpmain','bin/floor','bin/strat_run', 'python/strategy/hics_calh']
flr = 'bin/flr'
proc_map={}
RESET_WAIT_SECOND = 80

### data machine settings
BAR_PATH  = 'bar'
PYTHON_PATH = 'python'

### the object for performing daily roll/update tasts
### needs to be called approximately every couple seconds
daily_update_object = daily_update.DailyUpdate()

class TPMon :
    def __init__(self, stale_sec=60, close_hours = [ 17 ], open_hour = 18, open_minute = 0) :
        self.stale_sec=stale_sec
        self.bar_path=BAR_PATH
        self.fn = glob.glob(self.bar_path+'/*CME*.csv')
        self.fs = self.upd()
        self.ts = datetime.datetime.now()
        self.close_hours = close_hours
        self.open_hour = open_hour
        self.open_minute = open_minute

    def upd(self) :
        fs = []
        for f in self.fn :
            fs.append(os.stat(f).st_size)
        return fs

    def check(self) :
        ss = (datetime.datetime.now()-self.ts).seconds
        if ss < self.stale_sec :
            return True

        fsnow = self.upd()
        ret=False
        for s1, s2 in zip(self.fs, fsnow) :
            if s2 != s1 :
                ret=True
                break

        self.ts = datetime.datetime.now()
        self.fs = fsnow

        if not ret:
            # override during off hour and first check after open
            if self.ts.hour in self.close_hours:
                ret = True
            elif (self.ts.hour == self.open_hour) and (self.ts.minute*60 + self.ts.second <= self.open_minute*60 + self.stale_sec + 5):
                ret = True

        return ret

# ==================
# the following 3 functions control start/stop time
# =================
def is_weekend() :
    dt=datetime.datetime.now()
    wd=dt.weekday()
    if wd < 4 :
        return False
    if wd == 5 :
        return True
    if wd==4 :
        return dt.hour>17 or (dt.hour == 17 and dt.minute >= 1)
    if wd==6 :
        return dt.hour<17 or (dt.hour == 17 and dt.minute <= 55)

def is_in_daily_trading() :
    if is_weekend() :
        return False
    dt=datetime.datetime.now()
    wd=dt.weekday()
    if wd < 4:
        # mon-thursday
        return dt.hour != 17 or dt.minute < 1 or dt.minute > 55
    return True

def get_utcstart() :
    ymd = datetime.datetime.now().strftime('%Y%m%d')
    hms = "17:55:59"
    dt = datetime.datetime.strptime(ymd+ "-" + hms, '%Y%m%d-%H:%M:%S')
    return int(dt.strftime('%s'))

def get_cur_utc() :
    return int(datetime.datetime.now().strftime('%s'))

def update_config(trd_day = "") :
    """
    trd_day is in "YYYY-MM-DD", as the trading day.  
    If not given, infer from the current time, i.e. if 
    current time is later than 17:00, use tomorrow, 
    otherwise use today.
    """
    try :
        if trd_day is None or len(trd_day) == 0 :
            dtnow = datetime.datetime.now()
            utc = int(dtnow.strftime('%s'))
            if dtnow.hour >= 17 :
                # for the next trading day
                utc += 24*3600
            trd_day = datetime.datetime.fromtimestamp(utc).strftime('%Y-%m-%d')
        cfg_file = 'config/symbol'
        cfg_path = 'config'
        printfl ("updating symbol map for trading day " + trd_day)
        t, v = symbol_map.update_symbol_map(trd_day, cfg_file)
        symbol_map.writeToConfigure(t,v,cfg_path)
    except :
        traceback.format_exc()
        raise "failed to update symbol map!"

def should_run() :
    return (not is_weekend()) and _should_run

def weekly_cleanups() :
    # weekly clean up jobs to be performed here
    # this is run while system is still up
    daily_update_object.onWeekendEoD()
    pass

def eod_reconcile() :
    printfl ('run reconcile.  Sending requestion: ' + flr + ' E')
    ret = subprocess.check_output([flr, "E"])
    printfl (ret)
    printfl ('waiting for EoD reconcile')
    time.sleep(15)

def is_pid_alive(pid) :
    pids = [pid for pid in os.listdir('/proc') if pid.isdigit()]
    return pid in pids

def is_proc_alive(proc) :
    return proc.poll() is None

def kill_proc(proc) :
    try :
        pid = proc.pid
        while is_proc_alive(proc) :
            printfl ('sending sigint')
            proc.send_signal(signal.SIGINT)
            time.sleep(5)
            if is_proc_alive(proc) :
                printfl ('sending sigterm')
                proc.send_signal(signal.SIGTERM)
                time.sleep(3)
                if is_proc_alive(proc) :
                    printfl ('sending sigkill')
                    proc.send_signal(signal.SIGKILL)  #kill -9
                    time.sleep(2)
    except Exception as e :
        printfl ('Exception in Kill: ' + str(e))
        traceback.print_exc()
        return 0
    return 1

def kill_p(p) :
    if p in proc_map.keys() :
        printfl ("killing " + str(p))
        p0 = p.split('/')[-1]
        os.system("pkill " + p0)
        time.sleep(1)
        proc=proc_map[p]
        if is_proc_alive(proc) :
            printfl ( p + '(' + str(proc.pid) +  ') is alive, killing')
            kill_proc(proc)

def kill_all() :
    for p in proc_map.keys() :
        kill_p(p)

def launch(p) :
    kill_p(p)
    printfl ('launching ' + p + ' ' + str(datetime.datetime.now()))
    #proc_map[p]=subprocess.Popen(p,shell=True)
    proc_map[p]=subprocess.Popen([p])

def launch_sustain() :
    printfl("launching at " + str(datetime.datetime.now()))

    alive = False
    dtnow = datetime.datetime.now()
    while not should_run() and dtnow.weekday() != 6 :
        # Friday night or Saturday
        printfl ('wait for Sunday open...')
        time.sleep( RESET_WAIT_SECOND )
        dtnow = datetime.datetime.now()
    while dtnow.weekday() == 6 and not should_run() :
        # Sunday
        # make sure we start on time
        utcnow=get_cur_utc()
        utcstart=get_utcstart()
        while utcnow < utcstart -  RESET_WAIT_SECOND - 10 :
            printfl ('wait for Sunday open...' +  str(utcnow) + " " + str(utcstart) + " " + str(utcstart-utcnow))
            time.sleep( RESET_WAIT_SECOND )
            utcnow=get_cur_utc()

        printfl ('getting on-line, updating roll ' + str(datetime.datetime.now()))
        utcnow=get_cur_utc()
        if utcstart > utcnow and not is_in_daily_trading() :
            time.sleep(utcstart-utcnow)

        utcnow=get_cur_utc()
        printfl ('spining for start ' +  str(utcnow))
        while not is_in_daily_trading() :
            utcnow=get_cur_utc()
            time.sleep(1)
            #time.sleep( float((1000000-utcnow.microsecond)/1000)/1000.0 )
        printfl ('starting on ' +  str(utcnow))

    while should_run() : 
        # Mon to Fri
        if is_in_daily_trading() :
            if not alive :
                printfl ('getting on-line, updating roll ' + str(datetime.datetime.now()))
                update_config()
                alive = True
                tpm = TPMon()
            # poll and sustain
            for p in procs :
                if (p not in proc_map.keys()) or (not is_proc_alive(proc_map[p])) :
                    launch(p)

            time.sleep(1)
            # check market data, bounce if stale detected
            if not tpm.check():
                # the market data hasn't been updated for a while, 
                # exit the process and retry in outer (while [ 1 ]) loop
                printfl ('stale detected, exit!')
                _should_run = False
                kill_all()
                alive=False
                sys.exit(1)
        else :
            if alive :
                # getting offline after 5pm
                eod_reconcile()
                printfl ('getting off-line...')
                kill_all()
                alive = False
            time.sleep(1)

        # perform daily update/roll 
        daily_update_object.onOneSecond()
        sys.stdout.flush()
        sys.stderr.flush()
    
    printfl ('stopped ' + str(datetime.datetime.now()))
    if is_weekend() :
        # only do it on friday close
        dt=datetime.datetime.now()
        wd=dt.weekday()
        if wd == 4 :
            eod_reconcile()
            weekly_cleanups()

    kill_all()

    sys.stdout.flush()
    sys.stderr.flush()

if __name__ == "__main__":
    sys.path.append(PYTHON_PATH)
    launch_sustain()
    sys.exit()
