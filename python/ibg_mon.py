#!/usr/bin/python

import subprocess
import time

class IBGatewayMonitor :
    def __init__(self) :
        self.start_cmd = '/cygdrive/c/zfu/IBController/IBControllerGatewayStart.bat'
        self.ibg_proc='IB Gateway'
        self.ibc_proc='IBController'

    def _get_pid(self, running_only) :
        cmd = 'tasklist /v'
        if running_only :
            cmd += ' /FI "STATUS eq RUNNING" '
        ret = subprocess.check_output(cmd, shell=True).split('\n')
        ibg_pid=[]
        ibc_pid=[]
        for r in ret :
            try :
                if self.ibg_proc in r :
                    ibg_pid.append(self._pid(r))
                elif self.ibc_proc in r :
                    ibc_pid.append(self._pid(r))
            except :
                print 'problem getting pid, skipping: ', r
        return ibg_pid, ibc_pid

    def _pid(self, line) :
        # the second non-zero number 
        for l0 in line.split(' ')[1:] :
            if len(l0) > 0 :
                return int(l0)

    def _launch(self) :
        print 'launching ib gateway!'
        subprocess.Popen(self.start_cmd, shell=True)

    def kill(self) :
        cnt = 0
        while cnt < 3 :
            ibg_pid, ibc_pid = self._get_pid(running_only=False)
            if len(ibg_pid) + len(ibc_pid) == 0 :
                return
            print 'killing existing, ibg:{0}, ibc:{1}'.format(ibg_pid, ibc_pid)
            for pid in ibg_pid :
                subprocess.Popen('taskkill /PID ' + str(pid), shell=True)
                time.sleep(1)
            for pid in ibc_pid :
                subprocess.Popen('taskkill /PID ' + str(pid), shell=True)
                time.sleep(1)
            cnt+=1

        print 'problem killing ibg/ibc processes. ibg:{0}, ibc:{1}'.format(ibg_pid, ibc_pid)

    def sustain(self, clean_start=False) :
        if clean_start :
            self.kill() 
            time.sleep(5)
        while True :
            ibg_pid, ibc_pid = self._get_pid(running_only=True)
            if len(ibg_pid) == 1 and len(ibc_pid) == 1 :
                # tight loop
                time.sleep(1)
                continue

            print 'problem with IB Gateway processes, bouncing...'
            self.kill()
            time.sleep(1)

            self._launch()
            time.sleep(10)

if __name__ == '__main__' :
    ibm = IBGatewayMonitor()
    ibm.sustain()
