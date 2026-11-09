# Routes

Scripted input (`GCN_INPUT`, see gcn-recomp `docs/diagnostics.md`) keyed by presented
frame. Start from a blank card (`rm saves/memcard_a.raw`) for the first-run flow.

## Blank card to a Grand Prix race

```sh
rm -f saves/memcard_a.raw
GCN_INPUT="3400:START:5,3500:A:5,4200:A:5,4400:A:5,4600:A:5,4800:A:5,5000:A:5,5200:A:5,\
5700:A:5,5900:A:5,6100:A:5,6300:A:5,6500:A:4000" ./build/mkdd --fast
```

| Frame | Press | What it does |
|---|---|---|
| 3400 | Start | title screen (the opening movie ends at about 3200) |
| 3500 | A | "Would you like to create data ... in Slot A?" → Yes; the save is written |
| 4200 | A | Start Game |
| 4400 | A | How many players? → 1 |
| 4600, 4800 | A ×2 | Grand Prix, 50cc |
| 5000, 5200 | A ×2 | Mario and Luigi, the kart |
| 5700-6300 | A ×4 | OK, Mushroom Cup, OK; the course loads |
| 6500 | A held | accelerate from the countdown (START at about 6750) |

Luigi Circuit: the flyover, the countdown and the race with seven CPU racers. With no
steering the kart runs into the wall at the first turn and stays there.

The input log of every run (`saves/inputs/<timestamp>/`) replays it exactly with
`--replay=<dir>`, as long as the build has not changed what the game executes.
