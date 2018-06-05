# TCP client for floor
# allows for make orders

import socket

class FloorClient :
    def __init__(self, ip, port) :
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.connect(ip, port)

