#!/usr/bin/python

import subprocess
import os
import datetime
import signal
import sys
import time
import ibbar
import l1
import traceback
import glob
import numpy as np

_should_run = True
def signal_handler(signal, frame) :
    print 'got signal ', signal
    _should_run = False
    kill_all()
    sys.exit(1)

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

procs=['bin/tpib.exe','bin/tickrec.exe','bin/tickrecL2.exe','python/ibg_mon.py','bin/floor.exe']
#procs=['bin/tpib.exe','bin/tickrec.exe','bin/tickrecL2.exe','bin/floor.exe']
cfg=ibbar.CFG_FILE
proc_map={}

### data machine settings
USER='bfu'
DATA_MACHINE = 'huan'
BAR_PATH  = '/home/bfu/kisco/bar'
HIST_PATH = '/home/bfu/kisco/hist'
REPO_PATH = '/home/bfu/kisco/repo'

GET_HIST_LOG = '/cygdrive/e/ib/kisco/gethist.log'
GET_HIST_LOG_SIZE = [0, 0, 0] # prev_sz, prev_utc, prev_bounce_utc
RESET_WAIT_SECOND_Current = 150
RESET_WAIT_SECOND_Max = 180
RESET_WAIT_SECOND_Decay = 0.8
GET_HIST_LOG_SIZE_INC = 200

def hist_updated( check_sec = 3 ) :
    global GET_HIST_LOG_SIZE
    global RESET_WAIT_SECOND_Current
    global RESET_WAIT_SECOND_Max
    global RESET_WAIT_SECOND_Decay 
    global GET_HIST_LOG_SIZE_INC 

    dtnow = datetime.datetime.now()
    cur_utc=l1.TradingDayIterator.local_dt_to_utc(dtnow)
    prev_sz, prev_utc, prev_bounce_utc = GET_HIST_LOG_SIZE;

    if cur_utc - prev_bounce_utc > RESET_WAIT_SECOND_Current :
        GET_HIST_LOG_SIZE = [ prev_sz, cur_utc, cur_utc ]
        print "reset!"
        RESET_WAIT_SECOND_Current = RESET_WAIT_SECOND_Max/2
        return False

    if cur_utc - prev_utc < check_sec:
        return True

    try :
        cur_sz = os.stat(GET_HIST_LOG).st_size;
    except :
        # fall back to maximum wait if
        # gethist is not found, i.e. during the
        # week, or run by ipython
        cur_sz = prev_sz + GET_HIST_LOG_SIZE_INC 

    size_delta = cur_sz - prev_sz
    #if size_delta == 0 and RESET_WAIT_SECOND_Current == RESET_WAIT_SECOND_Max/2:
    if size_delta == 0 :
        # no updates for the first check, need to report
        #print "no updates for the first run"
        print "no updates"
        GET_HIST_LOG_SIZE = [ cur_sz, cur_utc, cur_utc ]
        RESET_WAIT_SECOND_Current = RESET_WAIT_SECOND_Max/2
        return False

    if size_delta >= GET_HIST_LOG_SIZE_INC :
        # got updates, increase reset linearly upto max
        RESET_WAIT_SECOND_Current = min(RESET_WAIT_SECOND_Current + (size_delta//GET_HIST_LOG_SIZE_INC)* check_sec + 1, RESET_WAIT_SECOND_Max)
        print "size delta ", size_delta, " increase to ", RESET_WAIT_SECOND_Current
    else :
        # got stuck into some slow updates, try resetting
        decay = RESET_WAIT_SECOND_Decay * np.clip(float(size_delta)/float(GET_HIST_LOG_SIZE_INC), 0.4, 1)
        RESET_WAIT_SECOND_Current *= decay
        print "size delta ", size_delta, " decrease to ", RESET_WAIT_SECOND_Current
    GET_HIST_LOG_SIZE = [ cur_sz, cur_utc, prev_bounce_utc]
    return True

class TPMon :
    def __init__(self, stale_sec=60) :
        self.stale_sec=stale_sec
        self.bar_path=ibbar.read_cfg('BarPath')
        self.fn = glob.glob(self.bar_path+'/*L2*bin')
        self.fs = self.upd()
        self.ts = datetime.datetime.now()

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
            if s2 > s1 :
                ret=True
                break

        self.ts = datetime.datetime.now()
        self.fs = fsnow
        return ret

def reset_network() :
    os.system('netsh interface set interface "Ethernet 2" admin=disable')
    time.sleep(0.001)
    os.system('netsh interface set interface "Ethernet 2" admin=enable')

def bounce_ibg() :
    import ibg_mon
    ibm = ibg_mon.IBGatewayMonitor()
    ibm.kill()
    ibm._launch()


# ==================
# the following 3 functions control start/stop time
# =================
def is_in_daily_trading() :
    dt=datetime.datetime.now()
    wd=dt.weekday()
    if wd < 4:
        #return l1.tradinghour(dt) 
        return dt.hour != 17 or dt.minute < 5 or dt.minute > 55
    if wd == 4 :
        return dt.hour < 17 or (dt.hour == 17 and dt.minute < 5)
    if wd == 5 :
        return False
    if wd == 6 :
        return dt.hour >= 18 or (dt.hour==17 and dt.minute > 55)

def is_weekend() :
    dt=datetime.datetime.now()
    wd=dt.weekday()
    if wd < 4 :
        return False
    if wd == 5 :
        return True
    if wd==4 :
        return dt.hour>17 or (dt.hour == 17 and dt.minute >= 5)
    if wd==6 :
        return dt.hour<17 or (dt.hour == 17 and dt.minute <= 55)

def get_utcstart() :
    dtnow=datetime.datetime.now()
    return l1.TradingDayIterator.local_ymd_to_utc(dtnow.strftime(\
            '%Y%m%d'), 17, 55, 59)

def should_run() :
    return (not is_weekend()) and _should_run

def remove_logs() :
    print 'removing log files in ./log/'
    os.system('rm -fR log/*hist*.txt')
    os.system('rm -fR log/*tpib*.txt')
    os.system('rm -fR log/*l2*.txt')
    os.system('rm -fR log/*book*.txt')
    os.system('rm -fR log/*tickrec*.txt')
    print 'removing log files in /cygdrive/c/Jts/*.20*.log'
    os.system('rm -fR /cygdrive/c/Jts/*.20*.log')
    os.system('rm -fR /cygdrive/c/Jts/dhmeniwux/*.20*.log')

def is_pid_alive(pid) :
    pids = [pid for pid in os.listdir('/proc') if pid.isdigit()]
    return pid in pids

def is_proc_alive(proc) :
    return proc.poll() is None

def kill_proc(proc) :
    try :
        pid = proc.pid
        while is_proc_alive(proc) :
            print 'sending sigint'
            proc.send_signal(signal.SIGINT)
            time.sleep(1)
            if is_pid_alive(pid) :
                print 'sending sigterm'
                proc.send_signal(signal.SIGTERM)
                time.sleep(2)
                if is_pid_alive(pid) :
                    print 'sending sigkill'
                    proc.send_signal(signal.SIGKILL)  #kill -9
                    time.sleep(5)
    except Exception as e :
        print ('Exception in Kill: ' + str(e))
        traceback.print_exc()
        return 0
    return 1

def kill_p(p) :
    if p in proc_map.keys() :
        proc=proc_map[p]
        if is_proc_alive(proc) :
            print p, '(', proc.pid, ') is alive, killing'
            kill_proc(proc)

def kill_all() :
    for p in proc_map.keys() :
        kill_p(p)

def launch(p) :
    kill_p(p)
    print 'launching ', p, ' ', datetime.datetime.now()
    proc_map[p]=subprocess.Popen(p,shell=True)

def launch_sustain() :
    alive = False
    dtnow = datetime.datetime.now()
    while not should_run() and dtnow.weekday() != 6 :
        #print 'wait for Sunday open...'
        #reset_network()
        time.sleep(1)
        if not hist_updated() :
            print 'bouncing while wait for Sunday open...'
            bounce_ibg()
            time.sleep( 15 )
            if not hist_updated() :
                time.sleep(15)

        dtnow = datetime.datetime.now()
    while dtnow.weekday() == 6 and not should_run() :
        utcnow=l1.TradingDayIterator.local_dt_to_utc(dtnow)
        utcstart=get_utcstart()
        while utcnow < utcstart -  RESET_WAIT_SECOND_Max - 10 :
            #print 'wait for Sunday open...', utcnow, utcstart, utcstart-utcnow
            #reset_network()
            time.sleep(1)
            if not hist_updated() :
                print 'bouncing while wait for open...', utcnow, utcstart, utcstart-utcnow
                bounce_ibg()
                time.sleep(15)
                if not hist_updated() :
                    time.sleep(15)

            utcnow = l1.TradingDayIterator.cur_utc()

        print 'getting on-line, updating roll ', datetime.datetime.now()
        ibbar.update_ib_config(cfg_file=cfg)
        utcnow = l1.TradingDayIterator.cur_utc()
        if utcstart > utcnow and not is_in_daily_trading() :
            time.sleep(utcstart-utcnow)

        utcnow = l1.TradingDayIterator.cur_utc()
        print 'spining for start', utcnow
        while not is_in_daily_trading() :
            utcnow = l1.TradingDayIterator.cur_utc()
            #time.sleep( float((1000000-utcnow.microsecond)/1000)/1000.0 )
        print 'starting on', utcnow
        alive = True

    tpm = TPMon()
    while should_run() : 
        if is_in_daily_trading() :
            if not alive :
                print 'getting on-line, updating roll ', datetime.datetime.now()
                ibbar.update_ib_config(cfg_file=cfg)
                alive = True
            # poll and sustain
            for p in procs :
                if (p not in proc_map.keys()) or (not is_proc_alive(proc_map[p])) :
                    launch(p)
            time.sleep(1)
            if not tpm.check() :
                # All L2 repo hasn't been updated for 1 min
                # exit the process and retry in outer (while [ 1 ]) loop
                print 'stale detected, exit!'
                _should_run = False
                kill_all()
                alive=False
                sys.exit(1)
            continue
        else :
            if alive :
                print 'getting off-line, killing all ', datetime.datetime.now()
                kill_all()
                alive = False
            # do one hour of reset
            dtnow = datetime.datetime.now()
            utcstart=get_utcstart()
            cur_utc = l1.TradingDayIterator.cur_utc()
            while  cur_utc <= utcstart - RESET_WAIT_SECOND_Max - 10 :
                print 'reset network', cur_utc, utcstart
                #reset_network()
                time.sleep(10)
                if not hist_updated() :
                    print 'boucing in between trading days ', cur_utc, utcstart
                    bounce_ibg()
                    time.sleep(15)
                    if not hist_updated() :
                        time.sleep(15)
                cur_utc = l1.TradingDayIterator.cur_utc()
            print 'getting on-line, updating roll ', datetime.datetime.now()
            ibbar.update_ib_config(cfg_file=cfg)
            cur_utc = l1.TradingDayIterator.cur_utc()
            if utcstart > cur_utc :
                time.sleep(utcstart-cur_utc)
            cur_utc = l1.TradingDayIterator.cur_utc()
            print 'spinning for start', cur_utc
            while cur_utc <= utcstart :
                cur_utc = l1.TradingDayIterator.cur_utc()
            alive = True
            tpm = TPMon()
    
    print 'stopped ' , datetime.datetime.now()
    kill_all()
    if is_weekend() :
        # only do it on friday close
        dt=datetime.datetime.now()
        wd=dt.weekday()
        if wd == 4 :
            remove_logs()
            # edrive
            prev_wk, this_wk = ibbar.move_bar(rsync_dir_list=['/cygdrive/e/ib/kisco/bar'])
            bar_path = ibbar.read_cfg('BarPath')
            #os.system('scp -r ' + bar_path + '/'+this_wk + ' ' + USER+'@'+DATA_MACHINE+':'+BAR_PATH)

            print 'moving bar files to ', this_wk
            print 'previous week was ', prev_wk

            #import IB_hist
            #IB_hist.weekly_get_ingest(rsync_dir_list=['/cygdrive/e/ib/kisco/hist'])
            eday = dt.strftime('%Y%m%d')
            tdi = l1.TradingDayIterator(eday)
            sday = tdi.prev_n_trade_day(5).yyyymmdd()
            #ibbar.weekly_get_hist(sday, eday)
            os.system("nohup python/ibbar.py " + sday + " " + eday + " >> " + GET_HIST_LOG + " 2>&1 &")
            print "started nohup python/ibbar.py " + sday + " " + eday + " >> " + GET_HIST_LOG + " &", datetime.datetime.now()
            time.sleep( 30 )

if __name__ == "__main__":
    launch_sustain()
