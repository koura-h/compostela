from SocketServer import *

RECV_BUF = 8192
PORT = 7070

class Handler(BaseRequestHandler):
    def handle(self):
        while True:
            ret = self.request.recv(RECV_BUF)
            if len(ret) == 0:
                break
            print ret
        self.request.close()

def main():
    sv = ThreadingTCPServer(('', PORT), Handler)
    sv.serve_forever()

if __name__ == '__main__':
    main()
