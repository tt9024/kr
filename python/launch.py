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
RESET_WAIT_SECOND = 30

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

def is_in_daily_trading() :
    dt=datetime.datetime.now()
    wd=dt.weekday()
    if wd < 4:
        #return l1.tradinghour(dt) 
        return dt.hour != 17 or dt.minute < 5
    if wd == 4 :
        return dt.hour < 17 or (dt.hour == 17 and dt.minute < 5)
    if wd == 5 :
        return False
    if wd == 6 :
        return dt.hour >= 18

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
        return dt.hour<18

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
        print 'wait for Sunday open...'
        #reset_network()
        bounce_ibg()
        time.sleep( RESET_WAIT_SECOND )
        dtnow = datetime.datetime.now()
    while dtnow.weekday() == 6 and not should_run() :
        utcnow=l1.TradingDayIterator.local_dt_to_utc(dtnow)
        utcstart=l1.TradingDayIterator.local_ymd_to_utc(dtnow.strftime(\
                '%Y%m%d'), 17, 59, 59)
        while utcnow < utcstart -  RESET_WAIT_SECOND - 10 :
            print 'wait for Sunday open...', utcnow, utcstart, utcstart-utcnow
            #reset_network()
            bounce_ibg()
            time.sleep( RESET_WAIT_SECOND )
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
            utcstart=l1.TradingDayIterator.local_ymd_to_utc(dtnow.strftime(\
                '%Y%m%d'), 17, 59, 59)
            cur_utc = l1.TradingDayIterator.cur_utc()
            while  cur_utc <= utcstart - RESET_WAIT_SECOND - 10 :
                print 'reset network', cur_utc, utcstart
                #reset_network()
                bounce_ibg()
                time.sleep( RESET_WAIT_SECOND )
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
            bar_path = dt.strftime('%Y%m%d')
            remove_logs()
            prev_wk, this_wk = ibbar.move_bar()
            print 'moving bar files to ', this_wk
            print 'previous week was ', prev_wk

if __name__ == "__main__":
    launch_sustain()
