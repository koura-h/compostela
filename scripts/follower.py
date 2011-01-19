#!/usr/bin/env python
import socket
import sys
import time

#from struct import *

PATH_CACHE = "/var/cache/santiago"

HOST = ''
PORT = 7070

MSGSIZE = 512

FILE = ''

# packet from follower to aggregator
# 1. channel no (a.k.a. filename)
# 2. position
# 3. length
# 4. data content

# response from aggregator to follower
# 1. channel no
# 2. position (accepted)

class DirObserver:
    path = None

    def __init__(self, path):
        self.path = path

    def run(self):
        pass

class FileReader:
    pos = 0
    file = None

    def __init__(self, fname):
        self.file = open(fname, 'rb')

    def process(self):
        buf = file.read()
        context.send(self, buf)


class AggregatorConnection
    sock = None
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

def _do_send(s, buf):
    s.send(buf)

def run(s, f):
    while True:
        try:
            buf = f.read()
	    _do_send(s, buf)
        except:
            time.sleep(1)

    s.close()

def main():
    HOST = sys.argv[1]
    FILE = sys.argv[2]

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))

    f = open(FILE, 'rb')

    run(s, f)

if __name__ == '__main__':
    main()
