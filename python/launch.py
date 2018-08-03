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

_should_run = True
def signal_handler(signal, frame) :
    print 'got signal ', signal
    _should_run = False
    kill_all()
    sys.exit(1)

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

procs=['bin/tpib.exe','bin/tickrec.exe','bin/tickrecL2.exe']
cfg='config/main.cfg'
proc_map={}

def is_in_daily_trading() :
    dt=datetime.datetime.now()
    wd=dt.weekday()
    if wd < 4:
        #return l1.tradinghour(dt) 
        return dt.hour != 17 or dt.minute < 55
    if wd == 4 :
        return dt.hour <= 17
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
        return dt.hour>=17
    if wd==6 :
        return dt.hour<18

def should_run() :
    return (not is_weekend()) and _should_run

def move_bars(bar_path) :
    print 'moving bar files to bar/' + bar_path
    os.system('mkdir -p bar/' + bar_path)
    os.system('mv bar/*.csv bar/' + bar_path)
    os.system('mv bar/*.bin bar/' + bar_path)

def remove_logs() :
    print 'removing log files in ./log/'
    os.system('rm -fR log/*')
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
    if not should_run() and dtnow.weekday() != 6 :
        print 'launched in a bad time with should_run() false, doing nothing'
        return
    while dtnow.weekday() == 6 and not should_run() :
        utcnow=l1.TradingDayIterator.local_dt_to_utc(dtnow)
        utcstart=l1.TradingDayIterator.local_ymd_to_utc(dtnow.strftime(\
                '%Y%m%d'), 17, 59, 59)
        print 'wait for Sunday open...', utcnow, utcstart, utcstart-utcnow
        time.sleep(utcstart-utcnow)
        dtnow = datetime.datetime.now()
        utcnow=l1.TradingDayIterator.local_dt_to_utc(dtnow)
        print 'spining for start', utcnow
        while utcnow <= utcstart :
            dtnow = datetime.datetime.now()
            utcnow=l1.TradingDayIterator.local_dt_to_utc(dtnow)
            #time.sleep( float((1000000-utcnow.microsecond)/1000)/1000.0 )
        print 'starting on', utcnow
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
        else :
            if alive :
                print 'getting off-line, killing all ', datetime.datetime.now()
                kill_all()
                alive = False
        time.sleep(1)
    
    print 'stopped ' , datetime.datetime.now()
    kill_all()
    if is_weekend() :
        # only do it on friday close
        dt=datetime.datetime.now()
        wd=dt.weekday()
        if wd == 4 :
            bar_path = dt.strftime('%Y%m%d')
            print 'moving bar files to ', bar_path
            move_bars(bar_path)
            #remove_logs()

if __name__ == "__main__":
    launch_sustain()
