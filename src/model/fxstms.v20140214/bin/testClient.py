#!/usr/bin/env python 

import sys
import socket 

host = 'localhost' 
port = 8888 

###receiving buffer size
size = 4096

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM) 
s.connect((host,port)) 

inputFn = str(sys.argv[1])
file = open(inputFn, 'r')

###query model id
s.send('modelID\n') 
result = s.recv(size)
result = result.strip()
print result

###send data and receive decision
for line in file:
    s.send(line) 
    result = s.recv(size) 
    result = result.strip()
    print result

###shut down serevr
###s.send('exit\n') 

s.close() 

