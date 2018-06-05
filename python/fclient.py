# TCP client for floor
# allows for make orders

import socket

class FloorClient :
    def __init__(self, ip='127.0.0.1', port=9024) :
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.connect((ip, port))
        self.cl=client

    def run(self,cmdline) :
        self.cl.send(cmdline)
        return self.cl.recv(1024);



