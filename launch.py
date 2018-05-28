#!/usr/bin/python

import subprocess
import os
import datetime
import signal
import sys
import time
import ibbar
import l1

_should_run = True
def signal_handler(signal, frame) :
    print 'got signal ', signal
    _should_run = False

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

procs=['bin/tpib','bin/tickrec']
cfg='config/main.cfg'
proc_map={}

def is_in_daily_trading() :
    dt=datetime.datetime.now()
    wd=dt.weekday()
    if wd < 4:
        return l1.tradinghour(dt) 
    if wd == 4 :
        return dt.hour < 17
    if wd == 5 :
        return False
    if wd == 6 :
        return dt.hour >= 18

def is_weekend() :
    dt=datetime.datetime.now()
    wd=dt.weedday()
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

def is_pid_alive(pid) :
    pids = [pid for pid in os.listdir('/proc') if pid.isdigit()]
    return pid in pids

def is_proc_alive(proc) :
    return proc.poll() is None

def kill_proc(proc) :
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
    proc_map[p]=subprocess.Popen(p,shell=True)

def launch_sustain() :
    alive = False
    while should_run() : 
        if is_in_daily_trading() :
            if not alive :
                print 'getting on-line, updating roll'
                ibbar.update_ib_config(cfg_file=cfg)
                alive = True
            # poll and sustain
            for p in procs :
                if (p not in proc_map.keys()) or (not is_proc_alive(proc_map[p])) :
                    launch(p)
        else :
            if alive :
                print 'getting off-line, killing all'
                kill_all()
                alive = False
        time.sleep(1)
    
    print 'stopped ' 
    kill_all()
    #if is_weekend() :
    #    move_bars()

if __name__ == "__main__":
    launch_sustain()

