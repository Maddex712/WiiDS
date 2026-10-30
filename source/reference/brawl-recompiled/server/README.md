# Brawl Recompiled online servers

Online play needs something every copy of the game can reach, to keep the public room list and turn room codes into
games. There are two kinds; the game picks by the address in `[online] server` (`Config.toml`, set on the launcher's
Online page) or the one built into the game.

## The matchmaking Worker (built in, free): `worker/`

A Cloudflare Worker with one Durable Object. No game traffic goes through it: games check in every couple of seconds
while they are in a room or a match, learn their own public address from a STUN server (stun.l.google.com), and the
Worker introduces games that want to reach each other. Both games then send "punch" packets to each other's public and
local addresses until one gets through, and play directly from then on.

It fits easily in Cloudflare's free plan. It can't connect two players whose routers both rewrite ports per destination
(some mobile and "carrier-grade" connections); those players can use Tailscale, or the relay below.

```bash
cd worker
npm install
npx wrangler login     # once, in a browser
npx wrangler deploy    # prints https://brawl-online.<your-subdomain>.workers.dev
npx wrangler dev       # or run it locally at http://127.0.0.1:8787 to test
```

The address it prints is the server address (`https://...`).

## The UDP relay: `brawl_online_server.py`

One small Python program (no dependencies, Python 3.8+) that relays every packet between players, so it works behind
any router. It needs an always-on machine with a public IP and UDP port 52200 open (a VPS, or your own PC with the port
forwarded). Its address is `host:52200`.

```bash
python3 brawl_online_server.py --port 52200
```

`brawl-online.service` runs it under systemd; `Dockerfile` and `fly.toml` run it on Fly.io (which needs a dedicated
IPv4 for UDP, about $2 a month).

A room code is the host's id, so codes only work between players using the same server.

## Making it the built-in server

Put the address in `kDefaultOnlineServer` in `wiicompiled/runtime/src/netplay/net_socket.cpp` and rebuild, so players
don't have to set anything.
