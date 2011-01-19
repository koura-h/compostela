#!/usr/bin/env ruby

require "socket"

PORT = 8187

gs = TCPServer.open(PORT)
socks = [gs]

addr = gs.addr
addr.shift

while true
  nsock = select(socks)
  next if nsock == nil
  for s in nsock[0]
    if s == gs
      socks.push(s.accept)
    else
      if s.eof?
        s.close
        socks.delete(s)
      else
        str = s.gets
        p str
      end
    end
  end
end
