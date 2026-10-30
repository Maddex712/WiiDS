#!/usr/bin/env python3
"""Brawl Recompiled's online server: a public room list and a relay, over one UDP port.

Every game registers under its own 24-bit id (kept in UserData/Netplay/online_id) and keeps registering every
couple of seconds. Games never talk to each other directly over the internet: a packet for another game goes to
this server with that game's id in front and comes out at the other game with the sender's id in front, so play
works behind any router without port forwarding. A game hosting a public room lists it in its registration; a game
looking for rooms asks for the list.

    python3 brawl_online_server.py [--port 52200]

Packets (little-endian; ids are 24 bits sent as 3 bytes):
    0xB1 REG     version u16, id u24, listed u8, then if listed: max u8, players u8, name str8, build str8
         -> 0xB2 REGOK public ip u32 (network order), public port u16
    0xB3 FWD     to-id u24, payload      -> to that game: 0xB3 from-id u24, payload
    0xB5 LIST                            -> 0xB6 count u8, then per room: id u24, max u8, players u8, name str8, build str8
"""
import argparse
import asyncio
import socket
import struct
import time

VERSION = 1
CLIENT_TIMEOUT = 15.0      # seconds without a registration before a game is forgotten
MAX_PACKET = 1500
MAX_ROOMS_LISTED = 40


def read_str8(data, pos):
    n = data[pos]
    return data[pos + 1:pos + 1 + n].decode("utf-8", "replace"), pos + 1 + n


def str8(text):
    raw = text.encode("utf-8")[:63]
    return bytes([len(raw)]) + raw


class Server(asyncio.DatagramProtocol):
    def __init__(self):
        self.transport = None
        self.clients = {}   # id -> {"addr": (ip, port), "seen": t, "room": None or (max, players, name, build)}
        self.by_addr = {}   # (ip, port) -> id
        self.forwarded = 0

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        if not data or len(data) > MAX_PACKET:
            return
        kind = data[0]
        try:
            if kind == 0xB1:
                self.register(data, addr)
            elif kind == 0xB3 and len(data) >= 4:
                self.forward(data, addr)
            elif kind == 0xB5:
                self.send_list(addr)
        except (IndexError, struct.error, UnicodeDecodeError):
            pass  # a malformed packet: ignore it

    def register(self, data, addr):
        version = struct.unpack_from("<H", data, 1)[0]
        cid = int.from_bytes(data[3:6], "little")
        if version != VERSION or cid == 0:
            return
        room = None
        if data[6]:
            max_players, players = data[7], data[8]
            name, pos = read_str8(data, 9)
            build, pos = read_str8(data, pos)
            room = (max_players, players, name, build)
        old = self.clients.get(cid)
        if old and old["addr"] != addr:
            self.by_addr.pop(old["addr"], None)
        self.clients[cid] = {"addr": addr, "seen": time.monotonic(), "room": room}
        self.by_addr[addr] = cid
        ip = socket.inet_aton(addr[0])
        self.transport.sendto(bytes([0xB2]) + ip + struct.pack("<H", addr[1]), addr)

    def forward(self, data, addr):
        src = self.by_addr.get(addr)
        dst = self.clients.get(int.from_bytes(data[1:4], "little"))
        if src is None or dst is None:
            return
        self.transport.sendto(bytes([0xB3]) + src.to_bytes(3, "little") + data[4:], dst["addr"])
        self.forwarded += 1

    def send_list(self, addr):
        rooms = [(cid, c["room"]) for cid, c in self.clients.items() if c["room"] is not None]
        rooms = rooms[:MAX_ROOMS_LISTED]
        out = bytearray([0xB6, 0])
        count = 0
        for cid, (max_players, players, name, build) in rooms:
            entry = cid.to_bytes(3, "little") + bytes([max_players, players]) + str8(name) + str8(build)
            if len(out) + len(entry) > 1400:
                break
            out += entry
            count += 1
        out[1] = count
        self.transport.sendto(bytes(out), addr)

    def expire(self):
        now = time.monotonic()
        for cid in [cid for cid, c in self.clients.items() if now - c["seen"] > CLIENT_TIMEOUT]:
            self.by_addr.pop(self.clients[cid]["addr"], None)
            del self.clients[cid]


async def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="0.0.0.0", help="address to listen on (fly.io: fly-global-services)")
    parser.add_argument("--port", type=int, default=52200)
    args = parser.parse_args()
    loop = asyncio.get_running_loop()
    transport, server = await loop.create_datagram_endpoint(Server, local_addr=(args.host, args.port))
    print(f"Brawl Recompiled online server on UDP {args.host}:{args.port}", flush=True)
    last = None
    try:
        while True:
            await asyncio.sleep(5)
            server.expire()
            rooms = sum(1 for c in server.clients.values() if c["room"] is not None)
            now = (len(server.clients), rooms)
            if now != last:  # a line when something changed, not every few seconds
                last = now
                print(f"{now[0]} game(s) online, {now[1]} public room(s), {server.forwarded} packets relayed so far",
                      flush=True)
    finally:
        transport.close()


if __name__ == "__main__":
    asyncio.run(main())
