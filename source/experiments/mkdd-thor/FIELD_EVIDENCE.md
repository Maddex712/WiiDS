# Lower-screen field evidence

Native profile: USA GM4E01, DOL SHA-1
`e389e50357d4a5076ab3d38672204658f411123d`.

Reference structure names come from doldecomp/mkdd at
`b56cc7677fa23058eff8f38784479c3b4b854060`. The separate retail address pin
comes from mariokartdd-recomp at `bd36eb982ea58c6651584cc48264876d1aba9546`.
Never apply the debug executable's function addresses to retail memory.

| Data | Retail layout | Static evidence |
|---|---|---|
| Race manager | global `803CB7E8` | runtime `src/race.cpp`, decomp's sRaceManager metadata |
| Phase and run state | manager+18 -> director+0C/+30 | upstream retail reader and VR notes; getRacePhase at 8018A1BC |
| Race info | manager+38 | retail constructor 80187980 and result writer 80189A40 |
| Mode/count/players/screens/demo/mirror | info+08/+1C/+1E/+20/+28/+2C | retail constructor plus upstream reader |
| Total laps | manager+2E | constructor and retail result writer |
| Course object | manager+44; Course ID +00 | retail constructor and calcRace at 80189BB0; Course.h |
| Kart checkers | manager+48 + kart*4 | retail result writer and checkKart at 801892F4 |
| Rank/time/lap/finished | checker+88/+84/+2C/+29 | retail result writer, timer 80186D30 and checkKart |
| Lap progress | checker+48 | KartChecker.h; finite 0..1 guard; live verification pending |
| Character identities | info+30+kart*18; character DB pointers +08/+10; DB ID +00 | KartInfo.h; isDefaultCharCombi at 80185470 |
| Target kart | info+114, signed 16-bit | RaceInfo.h; live verification pending |
| Karts | global 803CC588 -> control+A0+kart*4 | upstream retail reader, kartCtrl.h |
| Position/heading/speed | body+110 world matrix, body+444 speed *2.16; body+578 bit200 gates speed to zero | upstream retail race.cpp and VR notes; kartBody.h and KartCtrl::GetCarSpeed |
| Active driver slot | body+5B2 | kartBody.h and KartCtrl::GetKartDriver; live swaps pending |
| Pause | byte 803CB6DC | upstream runtime's retail pause-diff observations |
| Item manager | global 803CBF40 | r13=803D1420 slot -21728 (decimal), as retail getKartEquipItem callers load it (0x80245488); 803CBEC0 misread -5560 as hex. Live-checked on the Thor 2026-11-02 |
| Both held item slots | item manager+2CC+kart*8+rider*4 | retail getKartEquipItem at 80209A18 |
| Item kind/ownership | item+7C/+120/+124 | ItemObj.h; all ownership and range guards required |
| Empty/transition | stock +24C+kart*16+rider*8; 16 is empty | stockItemToKart and delete_stockItemToKart retail functions |
| Roulette | shuffle pointers +35C+kart*8+rider*4, roll flags +06 bits 0/1 | retail IsRollingSlot at 8020B5E8; ItemObjMgr.h |

`verify_fields.py` checks 24 selected address/field displacements against the
exact private executable. It records no executable bytes. It is static evidence;
other fields listed above retain their specified source/observational limits.

The initial inferred course/checker offsets were +48/+4C. Reading the retail
constructor and result writer disproved them: the retail values are +44/+48.
The production reader uses those corrected values.

Every pointer is bounded to the runtime's 24 MiB MEM1 with subtraction-based
overflow checks. A packet is sampled on the guest frame thread and copied under
a mutex. A renderer never reads guest memory. Unknown pointers/values clear
capabilities and use sentinels; unsupported modes clear the entire race state.
Both item slots are marked known together only after ownership validation;
stock transitions and roulette remain unknown rather than falsely empty.

Real races, actual character swaps, item pickup/use, course map alignment,
restart/results and display loss still need live acceptance.
