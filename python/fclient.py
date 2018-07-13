# TCP client for floor
# allows for make orders
import numpy as np
import l1
import socket
import datetime

class FloorClient :
    def __init__(self, ip='127.0.0.1', port=9024) :
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.connect((ip, port))
        self.cl=client

    def run(self,cmdline) :
        self.cl.send(cmdline)
        return self.cl.recv(1024);


def getFday(cl_bar_file, fday = None) :
    if fday is None :
        fday = datetime.datetime.now().strftime('%Y%m%d')

    ti = l1.TradingDayIterator(fday)
    assert ti.weekday() == 4, 'not a friday '+ fday

    ti.prev()
    thuday = ti.yyyymmdd()
    cl=np.genfromtxt(cl_bar_file,delimiter=',')
    t0=l1.TradingDayIterator.local_ymd_to_utc(thuday,4,45)
    t1=l1.TradingDayIterator.local_ymd_to_utc(thuday,6,15)
    t2=l1.TradingDayIterator.local_ymd_to_utc(thuday,16,55)
    t3=l1.TradingDayIterator.local_ymd_to_utc(thuday,18,35)
    i0 = np.nonzero(cl[:,0]==t0)[0]
    i1 = np.nonzero(cl[:,0]==t1)[0]  
    i2 = np.nonzero(cl[:,0]==t2)[0] 
    i3 = np.nonzero(cl[:,0]==t3)[0] 
    mid0=cl[i0,2]
    mid1=cl[i1,2] 
    mid2=cl[i2,2] 
    mid3=cl[i3,2]
    lr1 = np.log(mid1)-np.log(mid0) 
    lr2 = np.log(mid3)-np.log(mid2)
    print lr1, lr2
    xmu = [0.000498121848, 0.000272078733]
    xstd = [0.00485678068, 0.00245295458]
    fcst = (lr1-xmu[0])/xstd[0] * 0.00064095 + (lr2-xmu[1])/xstd[1] *0.00117629 + 0.00107083
    print fcst


