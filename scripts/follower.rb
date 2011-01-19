#!/usr/bin/env ruby

require "socket"

BUFSIZE = 1024
PORT = 8187

f = File.open(ARGV[0], "rb")
s = TCPSocket.open(ARGV[1], PORT)

while true
  begin
    rdbuf = ''
    buffer = ''
    nread = 0

    f.sysread(BUFSIZE, rdbuf)
    #buffer << rdbuf
    s.write(rdbuf)
  rescue
    sleep(1)
  end
end

s.close
