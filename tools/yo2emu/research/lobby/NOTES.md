# Lobby and rooms: what response each request really expects

Static analysis of the client (Ghidra) plus recorded sessions. The client itself is not modified.

Files:

| File | What it is |
|---|---|
| `NOTES.md` | This document: conventions, full table, dangers, merging |
| `auto_overrides.json` | Corrections to `auto_replies.json` (same format + optional `hex` field) |
| `proposal.py` | Handlers for the priority messages (`python proposal.py --test`: ast + sizes against the protocol table, 63 responses, 0 wrong) |
| `../../ghidra/a3_*.py` | Ghidra scripts: `a3_range_callers.py`, `a3_callsites.py`, `a3_disasm.py` |

The "protocol table" mentioned below is a table of message ids, names and sizes extracted from the client with Ghidra
(`ghidra/protocol_dump.py`); it is not included.

## 1. How the client decides whether a response is "success"

This is what you need to know before reading the table (all checked in the code):

- **Wait flags.** The sender sets `CNetLobby+off = 1` ("wait" column) and something waits for it to become 2. It is set
  to 2 by the handler of the response message (or by the dispatcher itself, `FUN_006ac020`, for the `..._END`
  messages). If no message sets that flag, the request hangs.
- **Result table.** Almost all `...RESULT` messages read an s32 into `CNetLobby+0x7e0+4k` (initialised to -150 = "has
  not arrived"). It is read with:
  - `FUN_006adfb0(&out, k)`: returns 1 if something other than -150 arrived and copies it; otherwise 0 and out = 0.
  - `FUN_006d3990(k)`: checks the flag `+0x1c+k == 2` and then does the same. Used by the scenes for server notices
    (MATCHOFFER, FRIENDOFFER...).
  - `FUN_006a5b90(k)`: only the flag `+0x1c+k`.
- **Convention: 0 = success, any other value = error.** The lobby task (`FUN_006d1840`, "CNetLobbyTask") does at every
  wait `if (result == 0) state 0x59 (ok) else state 0x58 (error, +0x88=3, +0x8c=code)` and the scene draws the error
  with the code. Confirmed exceptions:
  - `DUELROOMLISTSTART` 0x4282 and `NPCINFOLIST_START` 0x4231: s32 **>= 0** (it is the number of entries; < 0 =
    error).
  - `REQTRADERESULT` 0x7103: error **only if == -493**.
  - `REQMATCHRESULT` 0x4301 and `SELECTCARDPACKRESULT` 0x4803: the state machine reports "success" with any value that
    arrives; the REQMATCH scene then requires 0.
  - `FRIENDNOTICE` 0x4515: s32 **1** = accepted.
  - `TEAMLISTRESULT_BEGIN` 0x4564: s32 = **number of members** (0 = no team).
  - `GETMAILCOUNTRESULT` 0x4621: the handler uses > 0 for the mail notice, but the task treats != 0 as an error →
    send 0.
- **Messages missing from the protocol table** (they are the `default:` of a dispatcher `switch`; checked in the
  disassembly, `CMP EAX,id / JZ`):
  - **0x4309** CANCELREQMATCHRESULT → `FUN_006bb470` (s32, +0x2f, k=19)
  - **0x4501** GETPIDBYPNAMERESULT → `FUN_006bcf10` (s32; if 0, u32 pid; +0x41, k=37)
  - **0x4800** CARDPACKLISTNOTICE → `FUN_006bbe20` (loop of u16 pack id; +0x39)
  - **0x7130** TRADELIMIT → `FUN_006bfb20`
- **0x7000 is an envelope**: every trade send uses id 0x7000 with a **u16 subcommand** in front (0x7101 TRADESTART,
  0x7102 REQTRADE...). Responses come back with their own id (0x7103...). `gen_auto` merged them into one.
- **0x3112** (unnamed, `FUN_006b9480`) = notice of a player in the block: u32 pid, u8 op (0 = leaves, 1 = enters with a
  BLOCKPLAYERLIST entry of 36 B + a LIST2 entry of 45 B, 0x67-0x6a = state changes). Can be used to put "players"
  (NPCs) into the room.

List of kinds (k → message): 1 AUTH, 2 PSELECT, 4 ENTERBLOCK, 5-8 BPL1-4, 9 PLAYERINFO 0x4160, 10 0x5340, 11 0x4171,
12 0x4173, 13 DUELROOMLISTSTART, 14 CREATEROOM, 15 DELETEROOM, 16 REQMATCH, 19 CANCELREQMATCHRESULT, 21 DUELABORT,
22 REQWATCH, 30 SELECTCARDPACK, 32 RESETAUTOWAIT, 33 REQFRIEND, 36 FRIENDSEARCH 0x4521, 37 GETPIDBYPNAME, 38 SENDMAIL,
39 RCVMAILSTART, 40 MAILCOUNT, 42 ADDBLACK, 43 DELBLACK, 49 MILEAGE, 52 REQTRADE, 54 TRADEOFFERREPLY, 58 ADDCARD,
59 DELETECARD, 72 PACK_OPEN, 74 PRESENT_PACK, 75 DPAUTH, 94 USBDPAUTH, 95 SD_GET, 97 TPCARDGET, 98 STARTUP_PRESENTGET,
100 NPCINFO, 102 SHOPRESULTEND, 103 PURCHASE, 104 TEAMCREATEEND, 105 TEAMDISBAND, 108 TEAMLIST_END, 109 BPL5,
110 UPDATE_PLAYERTITLE. Kinds that no message fills (50, 51, 56, 62, 64-69: trade) stay at -150 and the task accepts
them as good.

## 2. Full table (everything the client can send to the lobby/rooms; excluding 0x6xxx)

Classification: **OK0** = works with zeros · **≠0** = needs a non-zero value · **DATA** = needs real data for the game
to make sense · **WRONG** = the pairing produced by `gen_auto` is wrong · **RUN** = already answered by the server's
own handlers · **DEAD** = the sender has no callers in the exe (should never arrive).
Verification (second pass over every non-zero or data value, re-reading the handler): **C** = CONFIRMED,
**P** = PROBABLE, **D** = DOUBTFUL.

### Connection, player and block

| Request | Name | Wait | Correct response(s) | Class | Correct value / notes | Evidence |
|---|---|---|---|---|---|---|
| 0x3001 | REQCCODE | +0x1c | 0x3002 CHALLENGE (16 B) | RUN | — | `FUN_006cb940`, `FUN_006b7e50` |
| 0x3003 | (auth) | +0x1d | 0x3004 AUTHRESULT s32 | RUN | 0 | `FUN_006cb990`; task case 7 `FUN_006adfb0(k=1)` |
| 0x0005 | heartbeat | — | 0x0004 (lobby) / 0x3113 (5731) | RUN | — | `FUN_006bfc80` |
| 0x4100 | PSELECT | (+0x1e, nobody waits) | 0x4101 PSELECTRESULT 133 B | RUN, **≠0** | pid ≠ 0 (with 0 the client closes). auto = zeros: dangerous without the server handler | `FUN_006b7ef0` |
| 0x4150 | REQPLAYERINFO | +0x25 | 0x4160: s32 0, u32 pid, name16, statistics (133 B) | DEAD, DATA | 0 + pid + name (proposal) — **P** | `FUN_006b9b50`, k=9 |
| 0x4170 | REGAVATARINFO | +0x27 | 0x4171 s32 | RUN | 0 | `FUN_006ba340` k=11; task case 0x10 |
| 0x4172 | REGPLAYERCOMMENT | +0x28 | 0x4173 s32 | RUN | 0 | `FUN_006ba380` k=12 |
| 0x4174 | UPDATE_PLAYERTITLE | +0x8a | 0x4175 s32 | OK0 | 0 = success — **C** | `FUN_00ac7bf0`: `PUSH 0x6e; TEST EAX; JZ` |
| 0x4200 | REQBLOCKLIST | +0x1f | 0x4201 | RUN | — | `FUN_006b8580` |
| 0x4202 | ENTERBLOCK | +0x20 | 0x4203 s32 + 3×u8 | RUN | s32 0 (`AL==1 && ==0`) — C | task case 0xc |
| 0x4210 | REQBLOCKPLAYERLIST | +0x21 | 0x4211 s32 + 0x4212×N + 0x4213 | RUN | — | |
| 0x4214/18/1c/21 | REQBLOCKPLAYERLIST2-5 | +0x22/23/24/89 | START s32 0 + END | RUN (OK0) | LIST2 = profile data (45 B), LIST3/4 = 32 B (comment/title?), LIST5 = doll appearance (u32 pid, u32, 16 B parts, 16 B) | `FUN_006b8d10/8fc0/9100/9250` |
| 0x4230 | (NPCINFOLIST) | +0x80 | 0x4231 s32 ≥0 + 0x4232×N + 0x4233 | RUN, **WRONG** | gen_auto: 0x421d/0x421f (the client's log string says "REQBLOCKPLAYERLIST4" by mistake) — C | `FUN_006cbf10`; task case 0x4a `JGE` |
| 0x5300 / 0x5310 | position / action | — | echo to the block | RUN | — | |
| 0x5320 | REQAVATARINFO | +0x26 | 0x5340 | RUN | for NPC pids the NPC's name should be sent (currently always the player's) | `FUN_006b9a10` |
| 0x3603 | MATCHINGSVR_REQPLAYERSEARCH | +0x40 | 0x3604: u32 ?, u32 n, n×(u32 pid, 5×u8) | **WRONG** (no response → hangs) | n=0 is valid; with friendly NPCs, their pids ("online" mark) — format C, meaning of the 5 bytes D (see §0x3604) | `FUN_006ccfa0` (friends), `FUN_006cd550` (team), `FUN_006bcdd0` |

### Duel rooms

| Request | Name | Wait | Response(s) | Class | Correct value / notes | Evidence |
|---|---|---|---|---|---|---|
| 0x4280 | REQDUELROOMLIST (s32 -1) | +0x29 | 0x4282 START s32 n + 0x4284 DATA (19 B/room) + 0x4286 END | RUN (empty), **DATA** | s32 = number of rooms (≥0; <0 = error) — C. Room: u32 roomid, u32 pid P1, u32 pid P2, u8 mstat, u8 dstyle, u8 wmode, u8 password, 3×u8 — C (format), P (meaning) | `FUN_006ba410`, `FUN_006ba4f0`, `FUN_006ba6a0`, `FUN_006ba740`, log string "index roomid pid_P1 pid_P2 mstat dstyle wmode password" |
| — | (notice) | — | 0x3113 UPDATE_DUELROOMINFO (19 B) | push | P1 = P2 = 0 → deletes the room with that roomid | `FUN_006ba7b0` |
| 0x4288 | REQCREATEROOM (28 B) | +0x2a | 0x428a s32 | RUN (NPC), **DATA** (own room) | 0 = created — C. Data: dstyle, wmode, mstat, password?, 16 B, 4×u8, u32 opponent | `FUN_006b1bb0` (log "m_dstyle,m_wmode,m_mstat"), `FUN_006b19b0` case 3 |
| 0x4290 | REQDELETEROOM | +0x2b | 0x4292 s32 (+ deletion 0x3113) | OK0 | 0 — C | `FUN_006b2fa0`: `PUSH 0xf; CMP EAX,EBX; JZ` |
| 0x4300 | REQMATCH (u32 roomid, 16 B) | +0x2c | 0x4301 s32, then 0x4306 DUELSERVERNOTICE | **DATA** | 0 = accepted → the scene starts waiting for the duel; ≠0 error (-494 has its own dialog) — C | `FUN_006b69f0` (first checks the room is in its list), `FUN_0086d270` (`+0xa4 == 0`) |
| 0x4303 | MATCHOFFERREPLY (s32 1/0) | — | none; if 1 → 0x4306 | push | — | `FUN_006cc8e0`; task cases 0x11/0x12 |
| — | (notice to the room host) | — | 0x4302 MATCHOFFER: u32 pid, name16, u8, u8, u16, u16, 16 B (parts) | push | sets +0x2d → scenes `FUN_00863ec0`/`FUN_00871130` (`FUN_006d3990(0x11)`) — format C, fields P | `FUN_006bae90` |
| 0x4304 | CANCELREQMATCH | +0x2f | **0x4309** s32 | **WRONG** | gen_auto: 0x4305 (no data, does not fill k=19; works by chance). 0 — C | `FUN_006b54b0` `PUSH 0x13; TEST; JZ` |
| 0x42a0 | RESETAUTOWAIT | +0x3c | 0x42a2 s32 | OK0 | 0 — C. Implemented (§10) | task case 0x14 (k=0x20) |
| 0x4320 | REQWATCH (u32 roomid) | +0x32 | 0x4321 s32, ip15, u16 | **≠0** | zeros = "success" with an empty ip. Without a spectator: 1 — P. See §8 | task case 0x16 (k=0x16) |
| 0x430a | AUTOMATCHREPLY | — | — | DEAD | — (§10) | `FUN_006cc960` |
| — | notices | — | 0x4306 DUELSERVERNOTICE (ip15, u16), 0x4308 OFFERTIMEOUT, 0x4310 DUELABORT s32, 0x43a0 AUTODUELSTART | push | 0x4306 sets +0x30 (task case 0x4b) | |

### Friends, ignore list, chat

| Request | Name | Wait | Response(s) | Class | Correct value / notes | Evidence |
|---|---|---|---|---|---|---|
| 0x4510 | REQFRIEND (u32 pid) | +0x3d | 0x4511 s32; then 0x4515 FRIENDNOTICE | OK0 + **DATA** | 0x4511: 0 — C. 0x4515: s32 **1** = accepted, u32 pid, name16 → the client stores it in its local list — C | task case 0x18 (k=0x21); `FUN_006bc9c0` (log "…から返答が来たので自分のフレンドリストに登録", `== 1`) |
| 0x4513 | FRIENDOFFERREPLY (u32 pid, s32) | — | none | push | (previous notice: 0x4512 FRIENDOFFER u32 pid, name16, u8) | task case 0x1a |
| 0x4500 | GETPIDBYPNAME (name16) | +0x41 | **0x4501** s32 + u32 pid | **WRONG** (no response → hangs) | found: 0 + pid ≠ 0; otherwise: 1 (or pid 0) → "does not exist" dialog — C | `FUN_006bcf10`; `FUN_00a903f0` checks +0x1b88 |
| 0x4530 | REQADDBLACK | +0x46 | 0x4531 s32 | OK0 | 0 — C | task case 0x26, `FUN_00a903f0` k=0x2a |
| 0x4540 | REQDELBLACK | +0x47 | 0x4541 s32 | OK0 | 0 — C | task case 0x28 |
| 0x4400 | CHAT: u8 type, u8, u32 target, u32, u32, text | — | 0x4402 CHATNOTICE to everyone (also to the speaker): u8, u8 channel, u32 pid, u32 language, name16, string | **DATA** | type 0/1/2 = room, 3 = whisper (u32 = target pid), 5 = team — P (from the PUSHes in `FUN_00a77c70`). language 0 = Latin. Ignored in My Room | `FUN_006cca30`, `FUN_006bb780` |
| 0x4410/24/28 | chat rooms | +0x33/34/37 | — | DEAD | nobody sets those flags | |

### Mail

| Request | Name | Wait | Response(s) | Class | Correct value / notes | Evidence |
|---|---|---|---|---|---|---|
| 0x4600 | SENDMAIL: u32 pid, subject32, body512, 64 B | +0x42 | 0x4601 s32 | OK0 + DATA | 0 — P (convention; the scene `FUN_00a2b680` was not read in depth) | `FUN_006be9c0` |
| 0x4610 | RCVMAIL | +0x43 | 0x4611 s32 0 + 0x4612×N (653 B, one per message) + 0x4613 | RUN (empty), DATA | START ≠0 closes the wait without a list; the task requires 0 — C. Entry: u32 sender pid, name16, u32 recipient pid, name16, s32, subject32, body512, 64 B, u8 — P | `FUN_006bea70`, `FUN_006beac0`, task case 0x24 |
| 0x4620 | GETMAILCOUNT | +0x44 | 0x4621 s32 | OK0 | **always 0** (>0 turns on the notice but the task treats it as an error) — C | `FUN_006bea20`, task case 0x22 |
| — | notice | — | 0x4603 SHORTMAILNOTICE (no data) | push | turns on "new mail" (+0x1ba4) — C | `FUN_006bea00` |

### Teams

| Request | Name | Wait | Response(s) | Class | Correct value / notes | Evidence |
|---|---|---|---|---|---|---|
| 0x4550 | TEAMCREATE (name16) | +0x84 | 0x4551 u32 + **0x4566** s32 | **WRONG** (hangs without 0x4566) | 0x4551: 0 = created (≠0 "チーム作成失敗:err=%d") — C; 0x4566: 0 | `FUN_006bcfa0`, `FUN_006bd020` (sets +0x84) |
| 0x4552 | TEAMDISBAND | +0x85 | 0x4553 s32 (+ 0x4560) | DEAD, D | the handler does not set +0x85=2 | `FUN_006bd220` |
| 0x4558 | TEAMLIST | +0x88 | 0x4564 BEGIN (s32 number of members, u32 id, name16, 16 B) + 0x4559×N (u32 pid, name16, u8) + 0x4565 END s32 | RUN, **WRONG** | gen_auto: 0x4559. A 4 B BEGIN is too short (the reader expects 40) — C | `FUN_006be3e0` (log "チームリスト開始 チーム名:%s, チーム員数:%d"), `FUN_006be6e0` |
| 0x4554 | TEAMJOIN (u32) | — | (to the leader: 0x4567 TEAMJOIN_APPY; to everyone: 0x4561 TEAMJOIN_NOTICE) | push, D | | |
| 0x4556 / 0x4568 / 0x4569 | BANISH / JOIN_REPLY / LEAVE | — | 0x4562 / 0x4561 / 0x4570 | push, D | | |

### Packs, gift cards, DUELPASS, shop

| Request | Name | Wait | Response(s) | Class | Correct value / notes | Evidence |
|---|---|---|---|---|---|---|
| 0x4901 | RECOVERY_PACK_LIST | +0x39 | **0x4800** loop of u16 pack id | **WRONG** (hangs) | empty is valid; with pack ids, the list to choose from — C | `FUN_006bbe20`; scene `FUN_00850d90` case 5 |
| 0x4802 | SELECTCARDPACK (u16 pack) | +0x3a | 0x4803 s32 + (u16 card, u8 new)×N | OK0, DATA | any value ≠ -150 = arrived; each card is added to the client's trunk (+0x18ed) — C | `FUN_006bc0f0`, `FUN_008444c0` case 3 |
| 0x4910 | REQGUNSLINGCARD | (+0x68, nobody sets it) | **0x4803** | **WRONG** (hangs) | the scene waits for +0x3a — C | `FUN_008444c0` cases 0/3 |
| 0x4915 | REQCHAMPSCARDGET | (+0x79, nobody) | 0x4803 | OK (override) | — C | `FUN_00850d90` case 0xd |
| 0x4902 | PURCHASE_PACK_OPEN | — | 0x4903 | RUN | | |
| 0x4904 | PRESENT_PACK_OPEN | +0x66 | 0x4905 s32 + cards | DEAD | | |
| 0x5204 | ITEMGET | +0x7f | 0x5205 | RUN | | |
| 0x5208 | REQSHOPITEM | +0x81 | 0x5213 + 0x5209×N + 0x5214 | RUN, WRONG | gen_auto only 0x5214 | |
| 0x5210 | REQSHOPRESULT | (+0x83) | 0x5212 + **0x5211** (+0x4140) | RUN, **WRONG** | gen_auto: 0x5216 (sets +0x83); the scene `FUN_00afd0d0` waits for +0x82 = 0x5211 (`FUN_006a5b90(0x66)`) — C | |
| 0x5400 | SD_GET_REQUEST | +0x7b | 0x5401 | RUN | | |
| 0x5200 / 0x5202 | MILEAGE | +0x4a / +0x4c | (0x5201 sets +0x4b, 0x5203 +0x4d) | DEAD | flags do not match | |
| 0x8101 | REQDPAUTH (16 B) | +0x67 | 0x8102 s32, u16, u8, u8, u16 | OK0 | 0 — P (see `research/persistencia/NOTES.md` §10) | `FUN_00a86470`; k=0x4b |
| 0x8203 | REQUSBDPAUTH | +0x7a | 0x8204 | **not dead** (§10) | s32 1 = error | `FUN_00a894c0` |
| 0x8301 | REQTPPRESENTINFO | +0x7c | 0x8302 u16 | OK0 | — C | `FUN_00850d90` case 0x15 |
| 0x8303 | REQTPCARDGET (u8, u16) | +0x7d | 0x8304 s32, u8, u16 | OK0 | 0 — C | `FUN_00850d90` case 0x16 |
| 0x8401 | (STARTUP_PRESENTGET) | +0x7e | **0x8402** s32, u8 n, n×u16 | override corrected | gen_auto: 0x8304 (sets +0x7d) — C. Sender: presentEntry.cpp (§10) | `FUN_006cef30`, `FUN_00850d90` case 0x1a |

### Trade (envelope 0x7000, u16 subcommand)

| Sub | Name | Wait | Response | Class | Value | Evidence |
|---|---|---|---|---|---|---|
| 0x7102 | REQTRADE (u32 pid) | +0x50 | 0x7103 s32 | **WRONG**, **≠0** | **-493** = rejection (the only error); anything else = accepted and keeps waiting for the other — C | task case 0x2a `(r != -0x1ed) + 0x58` |
| 0x7101 | TRADESTART | +0x4f | 0x7133 | WRONG | — C | `FUN_006bfc60` |
| 0x7105 | TRADEOFFERREPLY (s32) | +0x52 | 0x7105 s32 0 | WRONG | 0 — C | task case 0x2c |
| 0x7108 | CANCELREQTRADE | +0x54 | 0x7109 | WRONG | — C | |
| 0x7113 / 0x7114 | ADDCARD / DELETECARD (u16) | +0x56 / +0x57 | 0x7113 / 0x7114 s32 | WRONG | 0 (-1010 has special handling in `FUN_008fac30`) — C | |
| 0x7117 | SETTRADESTATUS (u8) | +0x5a | 0x7117 u8 | WRONG | — C | |
| 0x7119 | TRADELASTCONFIRM | +0x5c | 0x7120 u8 (+ 0x7121 TRADEEND) | (the only correct one) | — P | |
| 0x7112 | TRADEUSEREXIT | +0x5e | 0x7112 u32 | WRONG | — C | |
| 0x7122 | ARRIVETRADECARD | — | 0x7122 | WRONG | — P | |
| 0x7128 / 0x7132 | TRADECLEAR / TRADEREADY | +0x61 / +0x4e | none known | D | no handler sets those flags | |

### Tournaments

| Request | Name | Wait | Response | Class | Notes |
|---|---|---|---|---|---|
| 0x5100 | TOUR_INFO | +0x69 | 0x5100 (u32, 100 B, u32, s32, s32, u32×3) | OK0, D | echo with data; zeros = "no tournament" (scene `FUN_0099aed0` not read) |
| 0x5107 | TOUR_REQENTRY | +0x70 | 0x5108 START + 0x5109×N + 0x5110 END | OK0 | auto sends only END (valid: END sets +0x70); the overrides add START — P |
| 0x5111..0x5115 | LIST/ENDRESULT/TIMER/PCOUNTER/STATUS | own | echo with the same id | OK0, D | |

## 3. What the client requested in recorded sessions

Lobby/room requests observed: 0x4100, 0x4170, 0x4172 (very frequent), 0x4200, 0x4202, 0x4210-0x4221, 0x4230, 0x4280,
0x4288, 0x4510, 0x4530, 0x4540, 0x4558, 0x4610, 0x4902, 0x4915, 0x5204, 0x5208, 0x5210, 0x5300 (most frequent),
0x5310, 0x5320, 0x5400.

- Answered by the automatic replies only a few times: 0x4288 (before it had a handler), 0x4510, 0x4530, 0x4540
  (against the fake player "Yugi", pid 100001). The three friend/ignore requests returned 0 and the client carried on:
  correct.
- "No response": all the requests made before `gen_auto` existed (0x4100, 0x4170, 0x4200, 0x4610, 0x4902, 0x4915...),
  all answered now.
- Message boxes: only "Runtime Error / abnormal program termination", from the duel, not the lobby.
- Never requested at the time of the review: own rooms (0x4288 with opponent 0), REQMATCH, chat, sent mail, teams,
  trade, search by name. Exactly the ones `auto_replies` answers wrongly or not at all.

## 4. The most dangerous entries of `auto_replies.json`

1. **0x7000 → 0x7120 for the whole trade.** It is an envelope with a subcommand; gen_auto kept the last sender.
   REQTRADE waits for +0x50 forever and, in addition, a TRADELASTCONFIRMRESULT arrives out of context.
2. **Certain hangs (no response or the flag is not set):** 0x4500 GETPIDBYPNAME (add friend/ignore by name, mail),
   0x3603 online search (friends window), 0x4901 pack list, 0x4910 GUNSLING cards, 0x4550 create team (0x4566
   missing), 0x8401 (sends 0x8304).
3. **0x4320 REQWATCH with zeros = success with an empty ip and port 0**: the client tries to connect to nothing.
4. **0x4100 PSELECT with zeros = pid 0**, which closes the client (covered by the server handler; if it ever fails,
   the automatic reply breaks it).
5. Wrong pairings covered by the server handlers: 0x4230 (sends player lists instead of NPCs), 0x4558 (0x4559 without
   END), 0x5208/0x5210 (not the ones the shop expects).
6. A trap for anyone who "improves" values: **GETMAILCOUNT > 0 and RCVMAILLISTSTART ≠ 0 are errors** for the lobby
   task.

## 5. Merging with `auto_replies.json`

`auto_overrides.json` has the same format as `auto_replies.json` (`"0xREQUEST": [{"id", "name", "fmt"}]`) plus:
- `hex` (optional): exact response data (if missing, zeros of the size of `fmt`);
- `why`: evidence (ignored by the server);
- keys starting with `_` (not requests): `_doc` and `_sub` (responses of the 0x7000 envelope per subcommand).

Minimal loading logic (entries with only `fmt` already work when merged):

```python
# after loading AUTO:
try:
    OV = json.load(open(os.path.join(HERE, "research", "lobby", "auto_overrides.json"), encoding="utf-8"))
    AUTO.update({k: v for k, v in OV.items() if k.startswith("0x")})   # 0x7000 stays [] (no response) unless _sub is used
except OSError:
    OV = {}

# in auto_reply(c, msg_id), when sending each r:
c.send(int(r["id"], 16), bytes.fromhex(r["hex"]) if r.get("hex") else bytes(n))
```

For 0x7000 the subcommand is needed (auto_reply does not receive `data`): either use `proposal.py` (`h_trade`), or in
the lobby handler: `reps = OV["_sub"]["0x7000"].get("0x%04x" % struct.unpack(">H", data[:2])[0], [])`.

Better still: fix `gen_auto.py` so that it (a) looks up the dispatcher via the `dispatcher` of the response message and
not via the id range (0x3603→0x3604), (b) prefers as response the message that **sets the flag to 2** over the one that
matches by name (0x4550, 0x5210, 0x8401), (c) knows about the 0x7000 envelope, and (d) adds by hand the 4 `default`
cases (0x4309, 0x4501, 0x4800, 0x7130).

## 6. `proposal.py`: what it does

It hooks in with `LP.install(globals())` and `if LP.handle(c, msg_id, param, data): return` at the start of the lobby
handler (details in its header). It returns False for whatever it does not handle, including REQCREATEROOM with
opponent ≠ 0 (the direct duel against an NPC is unchanged).

- **Rooms**: each gate room shows 6 open rooms owned by famous NPCs (P1 = 100000 + id). Entering one (REQMATCH) →
  0x4301 0 + DUELSERVERNOTICE with `DUEL["rival"]` = that NPC, and the duel continues on the duel server (5760). Creating
  an own room → 0x428a 0 + 0x3113; after 8 s an NPC challenges (MATCHOFFER); accept → room occupied +
  DUELSERVERNOTICE; refuse → another NPC later (see `lobby.NPC_CHALLENGE` in `yo2srv/BANDERAS.md`). Delete room →
  0x4292 + deletion 0x3113. Cancel → 0x4309. Spectator → §8.
- **Friends**: REQFRIEND to an NPC → 0x4511 0 and after 1.5 s FRIENDNOTICE 1 (the client stores it in its local
  list). GETPIDBYPNAME looks up players and NPCs by name. Ignore list stored. Online search: NPCs appear "online".
- **Chat**: CHATNOTICE echo to the speaker (P: the client was not seen drawing its own message when sending it; if it
  appears duplicated, remove the echo); whisper to an NPC → it answers.
- **Mail**: SENDMAIL stores; to an NPC → it answers with another mail + 0x4603; RCVMAIL returns the player's mails
  (max. 50); GETMAILCOUNT always 0.
- **Teams**: create (0x4551 0 + 0x4566 0), list with members, disband, leave.
- **Packs**: 0x4901 → 0x4800 with packs 1-10; SELECTCARDPACK / GUNSLING / CHAMPS → 0x4803 with 5 real cards
  (`shop_purchase`). Note: the client adds them to its trunk; storing them server-side is the persistence module's job.
- **Trade**: REQTRADE rejected (-493); the other subcommands are echoed so that nothing hangs.
- Persistence: if the server provides `load_state/save_state`, mail, friends, ignore list and team are stored in
  `state.json["lobby"]`.

To be checked live (items marked P/D): that the room list draws the name of the NPC host (it may require putting the
NPC into the block with 0x3112 or BLOCKPLAYERLIST), that the MATCHOFFER challenge shows the dialog, and that
DUELSERVERNOTICE after REQMATCH leads to the duel without AUTODUELSTART.

### Chat: speech bubbles over the character? — NOT in the original
- 0x4402 CHATNOTICE -> `FUN_006bb780`: reads u8, u8 channel, u32 pid, u32 language, name16, text; builds the line with
  the language prefix (1 "<LJ>", 3 "<LK>", others "<LA>"), channel 3 (whisper) uses another colour (+0x358c instead of
  +0x355c), checks the ignore list (`FUN_0085d1a0`) and ONLY puts it into the queue +0x3554/+0x3558. — C
- That queue is emptied only by the chat window (`FUN_00a75760` via `FUN_006af100`), which applies the word filter
  `data/etc/taboo*.txt` and writes into the window; it does not touch `CChibiAvatar*` or `CPlChibiAvatar`. — C
- The client's bubbles (`CWindowModelBalloon`, `CWindowTaskBalloonFrame`, `fuki_win_*.png`, "Fuki Error!") belong to
  the half-body avatar (`src/Game/Avatar/yo2WaistUpAvatar.cpp`) and to NPC dialogs (`CFukiDictionary`,
  `data/Avatar/npc/fuki_index.csv`, `npc_fuki_button_%d`); they are called from NPC/duel scenes, not from the chat. — C
→ In the original, player chat only appeared in the chat window, with no bubble over the doll.

### Chat: Enter that "does nothing" — CLIENT anti-flood, 1:1; GMs are exempt
Observed live: with a NEW character, Enter did not send the chat; with an older character holding the GAME MASTER
title it did. What `ui_chat_send` (`FUN_00a77c70`, CChatWindow[0]) checks when Enter is pressed, in order:
- The text box (`CWindowTaskInput`) always lets Enter through: `+0x3fd = 1` when the window is created (0x5f3262) and
  the key handler (`FUN_005c4d10`, key 0xd) sets `+0x3fe = 1` and calls the window. — C
- Empty text or only spaces: nothing. "@" is sent doubled ("@@"). — C
- **Chat ban**: bit 0x20 of the player's `+0x1214` (2nd u32 of 0x3004 AUTHRESULT) -> text 4581 (0x11e5) "You have a
  Chat ban." (0xa7823d). The server's 0x3004 sends `1 | (admin_flags & 0x60)`: off unless a moderation sanction is
  set. — C
- **Minimum delay between messages PER TAB** (0xa78b30-0xa78b50, `switch` 0xa78b29): `now - last < interval` and the
  equipped title (record `+0x112`, via `lobby_slot_record_get(+0x18ec)`) is **not** 2 (GAME MASTER) -> exits through
  0xa79238 WITHOUT a message or sound (C; the text stays in the box: P, clearing only happens on the send path).
  Intervals (window constructor, 0xa768a7-0xa768e4, `EBX = 0x1e`, `EDI = 3`): tab 0 general **30 s**, 1 regional
  **5 s**, 2 trade **30 s**, 3 and 4 (whisper / duel) no delay (the table says 3 s but those cases do not check it),
  5 team 1 s. Last-sent times are 0 when the window opens (the first message always goes through). — C
- Nothing about DUELPASS, level or number of duels: the only readers of `+0x1248` are the DUELPASS window,
  0x4903/0x8102/0x8204 and the room challenges (`FUN_00863750`, `lobby_map_scene`). — C
→ A character with the GAME MASTER title (id 2; `research/mundo`: GM_ACCOUNTS gives bit 226 to GM accounts) can write
without delay. A normal player, in "General chat", only one line every 30 s, as in the original. It is not a field the
server sends wrongly: left untouched (changing it would require patching the client).

### Restrictions while a room is open — the client does it SILENTLY; already 1:1
- **"I own a room" mark** = byte `+0xd8b` of the lobby network task (`DAT_012711ac+0xc6c`; `+0xd8c` = pointer to the
  room). It is set to 1 when a room from 0x4284 (`FUN_006ba4f0`) or from 0x3113 (`FUN_006ba7b0`) carries **P1 = my
  pid** (pid of the current slot, `+0x124c + slot*0x1a8 + 0xc`). It is cleared ONLY by 0x4282 (list start,
  `FUN_006ba410`) and by 0x4292 = 0 (`FUN_006b2fa0` case 3). A deletion 0x3113 (P1=P2=0) removes the room from the
  list but does **not** clear the mark. — C. It is not the presence "state 100" (that is what OTHERS see on the card).
- **Map menu bar**: 3 buttons (slot 0 = MAIN "Menu", 1 = DUEL, 2 = MOVE; `FUN_00a7f870` from 0x856790, texts
  `MENUBAR_MENU_*`). In the map scene (`FUN_009ff900`, 0xa0071a): if `+0xd8b != 0` it jumps to 0xa00bb5, which
  **clears the clicks of MAIN and MOVE** (`FUN_00a7fb00(0)` and `(2)`) and returns: no dialog, no sound. — C
  - Everything reachable from the main menu (My Room, shop, deck, profile, mail…) is thus inaccessible. — C (block) /
    P (menu content)
  - Changing map: the only path is MOVE → `CWindowModelMove` (`FUN_00a07600`, state 6 → 7 → ENTERBLOCK). Silently
    blocked. — C
  - Text 5092 "You can not move from map to map while waiting for a Duel." EXISTS in `yo2text_*` but **no code in
    yo2.exe 080912 uses it** (sweep of all operands: the only 0x13e4 is a `cmp` in the duel at 0x718e92). — C.
    Leftover from another version. An optional client-side notice that uses it exists behind a switch, off by default:
    see "Notice when pressing MOVE with an open room".
- **DUEL button with an own room** (`FUN_00863ec0`, 0x8648e9 → 0x86547f): takes CTRL_DUEL_MENU (0x10) and opens the
  YesNo `m_DuelDel` with text **5252 (0x1484) "Cancel the Duel you created?"**. Yes (4000, `FUN_00863190`) → state 0xe
  → 0x4290 REQDELETEROOM. While it is open, the map goes to its state 5. — C
- **Notice icons**: with an own room the event windows (`CWindowModelGoEvent`) do not open, nor notice 1 (mail,
  CTRL_D_MAIL, 0x865f98) nor notice 7. — C (condition) / P (which icon each number is)
- **Another player's card** (`FUN_00893ff0`): with an own room the buttons 0 Challenge, 1 Friend request, 3 Trade,
  5/6 Ignore, 8 Watch duel, 9 "Go to the same Area", 10 Invite to team are NOT added (texts 0x13d0.., 0x148c-0x148e,
  0x14c7, in that order at 0x892536..0x892b58). Chat and D-mail remain. — C
- **Server**: nothing to add. The mark is set by the server's 0x3113 with P1 = pid (h_createroom) and cleared by
  0x4292 0 (h_deleteroom), as in the original. There is no server error response in any of these blocks (the client
  never asks). — C
- When returning to the map with the mark set (scene, state 2, `FUN_00863750`): if DUELPASS (`+0x1248`, goes up 150 at
  a time) < 3 (1 if the room type is 3) the client deletes the room by itself (state 0xe → 0x4290); otherwise it
  refreshes the lists (0x44/0x45). This indicates that in the original the room **survived the duel** and the room host
  kept waiting. — C (client) / P (server). See §9 "The room after the duel".

### Notice when pressing MOVE with an open room — OUR ADDITION, not 1:1, behind a switch and OFF by default
It is client-side (no message reaches the server on that click): a Frida hook. The canonical code is a launcher hook
(not included); a literal copy sits at the end of `net_hook.js`.
- **Turn on:** a file `yo2-aviso-sala.txt` (may be empty) next to `yo2.exe` — checked on every click, no relaunch
  needed — or `YO2_AVISO_SALA=1` in the game's environment; `YO2_AVISO_SALA=0` turns it off even if the file exists.
  Logged as `[cli] aviso_sala {...}` in the server log, or `[aviso_sala] {...}` in the launcher log (`ev`: `cargado`
  (loaded), `aviso_abierto` (notice opened), `clic_bloqueado` (click blocked) with the reason, `error`).
- **Where:** Interceptor at **0xa00c21** (bytes `8b f0 6a 00 8b ce` checked before hooking), inside
  0xa00bb5-0xa00c35 right before `FUN_00a7fb00(0)`/`(2)`: there `eax` = menu bar (`CWindowModelMenu`, from
  `FUN_00a564e0` = dyncast of `[CGameMng+0x4a4]`) and `esi` = map scene. — C
- **MOVE and not MAIN:** the click of each slot is the byte `bar+0x3cc+slot` (`FUN_00a7f9d0` reads it, `FUN_00a7fb00`
  sets it to 0 if `[bar+0x3a4+4·slot]` exists); at 0xa00c21 it has not been cleared yet → slot 2 = MOVE. — C. Since
  0xa00bb5 is also reached from other scene states (`FUN_00863660/6e0/720`), the hook only acts if the mark `+0xd8b` is
  set, and only with `[0x12bc618]` (interface mode) = 0. — C
- **Window:** the same `CWindowModelDialogOk` the client opens when DUEL is pressed with an invalid deck (m_pDeckErr,
  `FUN_00863ec0` 0x864d98-0x86547a, text 0x17a3): `FUN_005e79f0(parent=dyncast([scene+0x78], CElement), 420.0, 280.0,
  0xfa2, 1)` (cdecl) → yo2TaskPtr `scene+0x150/+0x154` (`FUN_00628fa0`) → `FUN_005df6d0(1)` → callback
  `{0xbee8a0, scene, 0x40fa88}` with `FUN_005df480` → text with `TextMng[0x1271840]->vtable[1](wstring, 5092)` and
  `window->vtable[0x30]` (text in the client language). When OK is pressed, `FUN_008633c0` sets `scene+0xc4 = 1` and
  `FUN_00863ec0` itself closes it on the next frame (0x865dc8-0x865f2c). — C (static). That it displays and closes
  correctly in the game: **P** (only verified that the script loads and hooks into a suspended `yo2.exe`).
  It does not open if there is already a window at +0x154 or a Yes/No at +0x1d8. This window was chosen rather than the
  server-error one (`CWindowModelDialogError`, m_pErrDlg +0x148, 0x11c9 in `FUN_00884b70`) because that one is driven by
  the lobby state machine (states 0x33→0x34→0x30, `FUN_00879700`) and opening it from outside would change the lobby
  state. — C/P
- **MAIN ("Menu")**: no notice (the client has no dedicated text). Options: nothing (as the original) / reuse 5092
  (speaks of "moving from map to map": not a perfect fit) / another existing text. It is one line in `AVISOS` of the
  hook (and its copy in `net_hook.js`).
- **NPC with an open room** (observed live in auto-duel): blocked **by the client, silently**. When an avatar on the
  map is clicked, `FUN_00ada5b0` sends those of type 2 (`CNpcChibiAvatar`) to `FUN_0089f790(npc, -1, -1)` (start
  talking to the NPC); that function, at 0x89f7c0-0x89f7df, if `+0xd8b != 0` calls `FUN_0089cc50` (closes controls
  0x13 and 0x14 and releases the NPC selected in the scene) and returns, with no text or sound. — C. It also exits if
  `+0x9ed == 8` or if `FUN_00407b3a(1)`. — C. What the player sees (the doll walks towards the NPC and nothing
  happens): P. No notice added.

### Entering/leaving a map as seen by others — each AREA is a BLOCK; flag `hub.AREA_BLOCKS` (on)
Symptoms observed live (before the fix): player B saw player A leave the Academy "walking through the walls", come
back "from the other side of the map", and a player in the Shop saw another player who was in the Academy, and vice
versa.
- **What the player crossing a door sends** — C: `FUN_00a01510` computes their position in the new area
  (GetInitPos(destination, origin) ± 30 at random), sends it in **0x5300** (`FUN_006cec70`) and then **0x4202 [block,
  type≠0]** (`FUN_006d3d50`; 1 = door, 2 = go to a player). Recorded example: `0x5300 00f7 04d7` (247,1239) and
  `GetInitPos(5<-4) pos(250,1250)`, then `0x4202 00 01`. The destination area is NOT in the message: the **block** is.
- **Where the block comes from** — C: BLOCKLIST 0x4201 (`FUN_006b8580`) = u16 max. and entries u16, u8 → +4 (**area**),
  u8 → +8 (room type; the HUD reads it with `FUN_00a08ba0(+0x9ef)`, index: out of range = abort), u8 → +9 (**block
  id**). When crossing, the client looks for the entry with +4 == destination area and takes its +9
  (0x9fc0b7-0x9fc112); if there is none, it keeps the old block. "Go to a player" (`FUN_009faf70`) does the reverse:
  the other's block (reg+0x131) → entry → area +4. 0x4203 (`FUN_006b8840`): u32 result, u8 → **+0x9ef own block**,
  u8 → +0x9f0 initial area, u8.
- **Who is drawn** — C: every frame `FUN_00a02ad0` creates the doll of each player in the list with **reg+0x131 ==
  +0x9ef** and position ≠ (0,0) (at reg+0x15c/+0x15e, dir +0x160: it pops up there) and **immediately removes** those
  that no longer qualify (`FUN_009d2e10`); the own doll likewise (`FUN_00a024e0`). reg+0x131 = last u8 of LIST1
  (struct+0x67, `FUN_006b8c30`) and byte 5 of 0x3112 (also for a pid already present). The room list carries
  EVERYONE (the HUD counts those in the same block separately). 0x5300 (`FUN_006c2130`) only updates the record and
  queues the step; it does not create dolls.
- **Cause** — C: the BLOCKLIST had a single entry (area = language, id 0) and 0x4203/LIST1/0x3112 were sent with
  block 0: everybody in block 0 of any area. On leaving, B received the 0x5300 with the position in the new area and
  walked there with nothing removing them; on returning, they walked from their last Shop position to the door; and
  each one saw the other on the wrong map.
- **Fix** (`pvp_hub.AREA_BLOCKS`, on by default; `YO2_AREA_BLOCKS=0` turns it off) — P (server; the client part is C):
  BLOCKLIST with one entry per area of the room's world (Domino 0,1,2,3,9; Academy 4,5,6,7; id = index), 0x4203 with
  the block of the entry area (or the one the client requests on an area change), LIST1 and 0x3112 with each player's
  block, and on an area change 0x3112 op 1 to the others with the new block and the position just sent: those in the
  area being left lose the player at once and those in the destination area see them appear at the door. 0x5300 is
  still forwarded to the whole room (the client ignores players of another block); whether the original filtered them
  by block — D. A 1-2 frame step towards the new position remains before disappearing (0x5300 arrives before 0x4202,
  as in the original). Covered by `fake_client.py pvp`.

## 7. Forbidden/Limited list: 0x5600 LIMITATIONLIST and 0x5601 UNRELEASELIST — flag `lobby.LIMIT_LIST` (on)

- **Nobody requests them**: they go through the lobby dispatcher (`FUN_006ac020`) without a wait flag; they are server
  notices. The tables are global and **nothing clears them** (only these two handlers write them). — C
- **0x5600** (`FUN_006c31e0`, disassembly 0x6c31e0-0x6c326c): loop until the end of the data of `u32 index, u32 A,
  u32 B, u8 copies` → `FUN_0073c1e0(index, A, B, copies)`: 12 B table at 0x1295038 (+0 = **B**, +4 = A, +8 = copies)
  and count = index + 1 (`FUN_0073c220`). Max. 4096 entries. — C
  - Lookup (`FUN_0073c290`): by **+0 = B**, returns the copies; **3 if absent**. B = **Konami id** (≥ 3000): callers
    pass `FUN_0073bc50(YO2 id)` = `card_prop[id] & 0x3fff`; ids < 3000 → 0 (`FUN_0073c190`). The other lookup, by +4
    (`FUN_0073c2e0`), has no callers. — C. A = YO2 id by analogy with 0x5601 — P (nobody reads it).
  - Bit 0x800 of the player's `+0x1214` (2nd u32 of 0x3004 AUTHRESULT, `FUN_0068f480`) → everything 3 (no limits). The
    server's 0x3004 sends 1: bit off. — C
  - Users (`FUN_00716c10`, 14 sites): deck editor (`FUN_00907660`: does not allow adding if copies ≥ limit;
    `FUN_009702c0/00970a80/009716e0/009735d0`), trimming when copying recipes (`FUN_00811210`), forbidden/limited icon
    on the card sheet (`FUN_00946870`, `switch` by limit) and `FUN_00a12f80` (limit + 1). — C (calls) / P (which
    screen each one is)
  - The 080912 client does **not** read `card_max.bin` (no string with that name in the exe): without 0x5600, 3 copies
    of everything. — C
- **0x5601** (`FUN_006c32a0`): `u32 index, u32 A, u32 B` → 8 B table at 0x128d030 (+0 = B, +4 = A), count at
  0x1295030. `FUN_0073c330` looks up the **Konami id** at +0 and `FUN_0073c380` the **YO2 id** at +4 (0 = present).
  Use: the card draw of `FUN_0079a540` repeats while it draws a listed card (with option 0x15) and `FUN_007689d0`
  (lists of all engine cards: declaring a card?) skips listed cards. — C (format and use) / P (name: "unreleased").
  **Which cards the original sent: D** → `UNRELEASED = ()` and it is not sent.
- **Server** (`research/lobby/proposal.py`, `LIMIT_LIST`, `YO2_LIMIT_LIST`): when 0x4100 PSELECT arrives (before the
  0x4101) it sends 0x5600 with the 149 cards of `research/ia/cards_yo2.csv` (generated by
  `research/ia/build_ia_tables.py`) with `limit` 0/1/2 (= 2008 `card_max.bin`), sorted by YO2 id, `index` from 0,
  A = YO2 id, B = Konami id, in messages of ≤ 1024 B (78 entries; currently 2 messages). The 47 with 65535 (gods,
  tokens) are not sent (the u8 cannot hold it). When the original sent it: P (after PSELECT; the timing does not
  matter because nothing clears it).
- Tests: `proposal.py --test` (format, indices, Pot of Greed 4844 = 0, Mirror Force 4887 = 1) and `fake_client.py pvp`.

## 8. Watching other players' duels (spectator) — flag `lobby.WATCH` (on) (`proposal.WATCH` + `pvp_match.PVP_WATCH`)

Client (all C unless stated):
1. **Button 8 "Watch duel"** on the card (`FUN_00893ff0`): no own room (+0xd8b = 0), flag 0x40 of the card and the
   other player's state 0x67..0x6a or 0x98 (and +0x4e8 ≠ 6). It moves the scene to state 0x3b with
   `+0xd8 = FUN_0086b7a0()` = **pid of the player on the card** (the same field 0x4300 REQMATCH sends).
2. **0x4320 REQWATCH** `u32 pid` (`FUN_0087c900` → task 0x15/0x16, `FUN_006d3f30`). **0x4321** (`FUN_006bbda0`):
   `s32` → +0x838; if 0: `ip 15 B` → +0xd78 and `u16 port` → +0xd88 (the same fields as 0x4306); +0x32 = 2. ≠ 0 →
   scene to state 0x4a (error). Error code of the original: D.
3. Scene 0x41 → task 0x52/0x53: connects to the duel server at +0xd78/+0xd88 (like a duel, state 0x4d) and, once
   connected (0x54), **0x6700 without data** (`FUN_006926d0`): ND+0x676 |= 0x20 (**spectator mode**), +0x22 = 1. It
   does **not** send 0x6001 or a deck. Waits for +0x22 = 2 (0x55) → scene 0x42: `FUN_00794d90(7 or 8)`.
4. **0x6006 DUEL_OTHERDECKINFO_WATCH** (`FUN_00692470`), 147 B: twice (player 0 and 1, ND+0x54c + k·0x94)
   `u16 deck cards (+0x18), u16 extra (+0x1c), u8 (+0x11, D), 16 B name (+0), 3 × 16 B avatar parts per world
   (+0x28/+0x39/+0x4a), 4 B country (+0x6c)`; and `u8` → ND+0x67c: 4 → mode 8, other → mode 7.
   Modes 7/8 = spectator (`FUN_00790530`); 10/11 = replay. With 7/8 **both** decks are dummies with those sizes
   (`FUN_00795300`). **It carries no life points**: `FUN_0071fe10` reads them from ND+0x570/+0x604, which only 0x600c
   sets (risk D: without a previous duel in that client session, the value is unchecked).
5. Rock-paper-scissors in spectator mode (`FUN_007c76b0`): does not send 0x6100/0x6102; in state 2 it waits (after
   0x3c frames) for **ND+0x677 == 1** and takes the hands from **+0x66a0 (player 0) / +0x66a1 (player 1)**; in state 7
   it waits again for +0x677 == 1, takes **+0x66a2** as who goes first, sets +0x677 = 0 and sends **0x6702**. When
   leaving the scene it sends **0x6703** without data (0x7c556a; nobody answers it). On a tie it would go back to
   state 2 with the same hands: the hands that decided must be sent.
6. **0x6701 WATCH_JSEEN** (`FUN_00692660`): `u8 → +0x66a0, u8 → +0x66a1, u8 → +0x66a2`; +0x677 = 1.
7. Duel: the spectator cannot play (`FUN_007911b0/007912c0/00791350` return 0 in mode 7/8) and the executor ignores
   0x1a. After each command (`FUN_00720fd0`), if spectating and the 0x6202 queue is empty (ND+0xb8f8 < 0) →
   `FUN_00692720`: sends **0x6702** and sets +0x677 = 2; with +0x677 == 2 it does not repeat until 10 s have passed
   (then it resets it to 0). Every **0x6202 sets +0x677 = 0** (`FUN_00692580`) and 0x6701 sets it to 1: both allow it
   to ask again immediately. The queue has 512 slots and **overflow is not checked**. → the original served **on
   demand**, one batch per 0x6702 (P).
8. Engine commands 0x4e/0x4f (`FUN_0072a3e0`/`FUN_0072a6b0`, lockstep §3): only in spectator/replay; generated by the
   client engine, not by the server (C); what they draw: D.

Server (P except formats):
- `proposal.h_watch`: with `WATCH`, if the pid has an unfinished PvP (`PVP.watch_request`), 0x4321 = 0 +
  `duel_ip_for` + duel port and it is registered (client IP → match, 60 s). Otherwise 0x4321 = 1.
- `pvp_match.Matches.route`: a **0x6700** from that IP attaches the connection to the match as a spectator
  (`watch_attach`, without pid or session). In the match thread: 0x6006 (`watch_info`: real deck sizes, names, parts
  as in 0x600c, "ESP", mode 3) and, as soon as it is known who goes first, **0x6701 (hand 0, hand 1, absolute first
  player)** (that it is an absolute index: P). Each **0x6702** → next batch (≤ `WATCH_BATCH` = 64) of the 0x6202
  **sent to player 0** (recorded by `_RecTx`, like the replay, even if `PVP_DUELLOG_RECORD` is off); if there is
  nothing new it stays pending and goes out as soon as player 0 receives more. No 0x6202 goes out before its first
  0x6702 (the one from the rock-paper-scissors scene). 0x0005 → 0x0004; 0x6703 is ignored.
- Player 0's view (with their hand visible, like the replay from that side): what the official spectator saw — D.
- `WATCH_SEND_PREP` (`YO2_WATCH_PREP=1`, **ours, not 1:1**): a 0x600c (player 0's view) before the 0x6006 so that it
  has life points. Off.
- Not done (D): the room's "Watch" option (one of the 3 u8 `x` of 0x4288/0x3113) is not checked (unknown which byte
  and which values); presence state of the spectator (0x69/0x6a?); what happens at the end (it only receives player
  0's 0x6202, which include the end); 0x6610 SELDUELLIST_DATA (list of duels to watch).
- Tests: `fake_client.py pvp`: a third client C (127.0.0.4) asks to watch B during the whole duel: 0x4321 = 0, 0x6006
  of 147 B, 0x6701 = 00 01 00 and the same 0x6202 as A up to the 0x4d.

## 9. The room after the duel — flag `lobby.ROOM_REOPEN` (on)

Client (C):
- When returning to the map after a duel, the scene (`FUN_00863750` state 2) checks the own-room mark (+0xd8b): if
  DUELPASS (+0x1248) < 3 (< 1 with room type 3) **it deletes the room itself** (state 0xe → 0x4290); otherwise (and
  also without a room) it goes to state 0x44 → `FUN_0087e640` → task 0x41..0x49: **0x4210, 0x4214, 0x4218, 0x421c,
  0x4221 (LIST1..5), 0x4280 REQDUELROOMLIST** (except in My Room) and 0x4230.
- 0x4282 clears the mark and every 0x4284 entry with **P1 = my pid** sets it again (`FUN_006ba4f0`): **P2 and mstat do
  not matter**. After the duel the room is still "own" (blocks Menu/Move) as long as the server lists it.
- For someone else to challenge it, it needs ("Restrictions while a room is open", `FUN_0088add0` and `_challenge`):
  host state 100..0x65 and a room with dstyle 3 **and free**.
- Conclusion: in the original the room **survived the duel** and the room host went back to waiting (otherwise the
  automatic deletion by DUELPASS would be pointless). — C (client) / P (server). mstat: the client sends 0 when
  creating it (`m_mstat`, all recorded sessions); the "with opponent" 1 is ours — P.

Server (`proposal._room_reopen`, P): with `ROOM_REOPEN`, when 0x4210 arrives from someone whose room has P2 ≠ 0 and
**no** PvP reserved or running (`PVP.by_pid`): P2 = 0, mstat = the creation one (`mstat0`), 0x3113 to the room host and the
block, and state 100 again (`hub.set_state`, after the 0 that pvp_hub sets when returning from the duel). If the
opponent was an NPC, the NPC challenge is scheduled again as at creation. The following 0x4280 already brings the free
room: the room host keeps waiting (Menu/Move silently blocked, as in the original) and the others can challenge them; to
leave, "Duel" → "Cancel the Duel you created?" → 0x4290. Nothing changes for the challenger (no room).
- Tests: `proposal.py --test` (room freed only once; not touched while the duel is running) and `fake_client.py pvp`
  (A sees 0x3113 with P2 = 0 and B's state 100; B's list brings their free room).
- Open (D): whether the original freed it at the end of the duel (0x4800) or when returning to the map, and whether it
  sent anything else.

### 9b. The state others see travels in LIST1, not LIST2 — flags `hub.LIST1_STATE` and `hub.LIST2_DUEL_23` (on)

Observed bug: after a second PvP duel, the challenger no longer saw the room host in blue and could not challenge
them, although the server state was correct (free room: 0x3113 and 0x4284 with P2 = 0; host in state 100 via 0x3112
op 1 and LIST2 = 100). After the first PvP it had worked: the challenger used what the 0x3112 carried. The difference:
after the second one, the challenger requested the lists again (0x4172×2, 0x5204, 0x4210..0x4280: menus / return to
the map).
- **Client (C)**: reg+0x130 (the state that draws the blue colour, the icon, "Challenge", the pose) is written only by
  **LIST1** (`FUN_006b8ae0` → `FUN_006b8b70`: **byte 34** of the 36 B entry goes to struct+0x66 and byte 35 to +0x67;
  `FUN_006b8c30` → +0x130/+0x131) and by **0x3112** (`FUN_006b9480`). LIST2 (`FUN_006b8d10` → `FUN_006b8eb0`) **does
  not touch it**: parts, x, y, dir and, only if the existing +0x130 is 0x67..0x6a, the opponent's 23 B. 0x4211 clears
  the list.
- **Cause**: the server's LIST1 carried 0 in byte 34: every list refresh left EVERYONE else at 0 until the next 0x3112
  (no blue or "Challenge"; and no "dueling" pose for someone who enters the map during a duel).
- **Fix** (P: that the original sent the state there; it is the only place that keeps it): `LIST1_STATE` →
  `Hub.list1_entry` puts the session state in byte 34 if it is not 0 (0 stays as is). The player's own entry still
  carries 0: it does not affect what the others see.
- **LIST2 "dueling"** (C): after 0x67..0x6a the client reads **23 B** (`FUN_006b8de0`: 16 + u8 + 3×u16, same as
  0x3112); sending 25 shifts the next entry by 2 B and, after the last one, a ghost entry with buffer leftovers
  appears. `LIST2_DUEL_23` → 23 (`fake_client.list2` likewise).
- Original (P/D): the room survived the duel and the room host kept waiting (§9); with the state in LIST1 the others see
  them "Waiting" even if they enter or return to the map later. Whether it was freed at 0x4800 or on returning to the
  map: D.
- A player's `s.duel` (e.g. `{'rival': 66, …}`) is intentionally stale (their last NPC duel): `duel_state()` creates it
  and only `duel_begin` (NPC) changes it; it is read by 0x600c / NPC deck / `npc_watch`, all in NPC duels after
  `duel_begin`. PvP (`pvp_match`, `PVP.by_pid`) and presence (`s.state`) do not use it.

## 2008 world: titles, "dueling" against an NPC, find a friend

Code: `research/mundo/proposal.py` (self-test). Confidence: C = client (+ source) · P = probable · D = doubtful.

### B) Titles (称号, PBIN type 10)

- **What they are (C):** 112 titles, PBIN bits 152-255 and 408-511 (`yo2pbinmap.dat`: type 10, subtype 7600+n = text
  of the description, column 6 = 9100+n = name). Id ↔ bit table in the exe at 0xc98d50. The record stores the **id**
  (+0x112, FUN_0068fc50 → text); 0x4174 carries the **bit**.
- **How one is chosen (C, implemented in persistence):** the profile lists the titles whose bit is set; choosing one
  sends `0x4174 UPDATE_PLAYERTITLE` (u32 bit, waits for +0x8a) → `0x4175` s32 (0 = valid: the client stores
  FUN_0068fe10(bit) in +0x112). `PP.on_title` stores the id in `rec.title`; it appears in LIST1 (+0x112) for the
  others when entering the block. There is no message to tell the block about the change live (D: the others see it
  when they re-enter).
- **How they were earned (client descriptions, texts 7600-7711; C the rule, D the numbers):**

| Bits | Titles | How (game description) | Emulator |
|---|---|---|---|
| 224 | Beginner Duelist | the first one | always selectable |
| 225-227 | KONAMI / GAME MASTER / JUMP FESTA | staff, GM, event | not granted (C: not applicable; GM see `research/mundo/NOTES.md`) |
| 228-234, 494-511 | Trialist, Gunslinger, Y.O.C., TP, Evolution, Anniversary… | tournaments, streaks, rankings | tournaments module |
| 235-246 | Summoner / Fusioner / Trap Artist / Spell Caster (bronze-silver-gold) | "many" high-level monsters, fusions, traps, spells | `TITLE_RULES` (thresholds D) |
| 247-249 | Never Gives Up | comebacks | same |
| 250-254 | Bronze Duelist … ultimate | duels won ("you are no longer a beginner"…) | same |
| 255 | Surrenders in one turn | surrendering right away | same |
| 152 / 153 | The Kaiser Afro / The Pompadour King | "true lover of the Afro / of the pompadour" | `hair_titles`: with the Afro Lv4 part (bits 13/265) or リーゼント Lv3 (bit 9). The Lv1 parts came from NPCs (missbomb/dancer "afro Lv1", bankara "pompadour Lv1", DC) — P |
| 154-156 | Card Enthusiast / Collector / Hoarder | collection | `TITLE_RULES` (% of cards, D) |
| 157-158 | Seasoned Veteran / Legendary Hero | weekend trials / championship | tournaments |
| 159 | Everyone's Friend | friendship with every character | npcreglas/persistence (all the friendship ones) |
| 160-174 | Friendship with each famous character | level 3 complete ("24 wins → title") | npcreglas `friendship` |
| 175, 408-421 | Defeat by each famous character | losing 20 times | npcreglas |
| 422-431, 480-484 | "X FAN" / eternal friendship | 200 wins | npcreglas |
| 485-493 | Egyptian gods, Exodia, Destiny Board, Vennominaga, Final Countdown, annihilate | summon / win with those effects | not done (needs data from the duel engine, D) |

- **Gap found:** achievement titles were only checked at the end of an **NPC** duel, and with
  `NPC_RULES_2008["no_record"]` not even then: nobody would earn "Bronze Duelist" etc. PvP duels do add `duels_won`
  (`pvp_match.pvp_record`). → `pvp_titles`.
- **Flags** (`mundo.pvp_titles`, `mundo.hair_titles`, `mundo.title_owned`, all on by default; environment variable
  `YO2_MUNDO`, e.g. `YO2_MUNDO=pvp_titles,hair_titles,title_owned`):
  `pvp_titles` = after every PvP duel, `check_titles` (persistence `TITLE_RULES`) and anything new stays pending
  "any"; it is delivered with the ITEMGET(1,0) the client sends when returning to the map (observed in recorded
  sessions: 0x5204 00 01 after the 0x4140) — P (rule) / D (thresholds). Note: with the "all3" trunk (3 copies of
  everything) the first PvP grants the 3 collector titles (154-156) at once. `hair_titles` = 152/153 when entering a
  map if the player has the part — P. `title_owned` = 0x4174 with a bit the profile does not own → 0x4175 = 1 (224 is
  always valid) — P (the client only offers its own; this is a server-side defence).

### C) "Dueling" pose when the opponent is an NPC

- **What the client draws (C):** FUN_009d3820 (doll refresh) sets the duel animation (+0x350 = 4, then 5/6) for anyone
  with reg+0x130 in **0x67..0x6a**; outside that range it removes it. It does not check who the opponent is: nothing
  is different for an NPC. States come only from the server (0x3112 op = state, or LIST2); the client does not set its
  own. FUN_0088d170 (card icon): 100/0x97 → 2 (room), 0x65 → 7, 0x66 → 3, 0x67..0x6a/**0x98** → 1 (dueling); 0x98
  gives the icon and the "Watch" option but **not** the pose or the opponent data (0x3112 only reads the 23 extra B
  with 0x67..0x6a). FUN_00880000 counts 100/0x65 (rooms), 0x66+0x69/0x6a and 0x67..0x6a (dueling) for the duel button
  (CWindowTaskButtonDuel).
- **The 23 B after the state (C):** FUN_006b9480 → reg+0x162 name16, +0x173 u8, +0x174 u16, +0x176 u16, +0x178 u16;
  the card (FUN_0089b290) draws them in the "busy" variant just like the player's own (+0xa4 LV, +0xb2 wins, +0xba
  losses, +0x112 title → FUN_0068fc50). That is: **opponent's name, LV, W, L and title**. Sending zeros shows "Lv0,
  Beginner Duelist". → `pvp_hub.RIVAL_CARD` (**on**, C correction; `YO2_RIVAL_CARD=0` turns it off): in a PvP duel,
  `_room_state` passes the opponent's session and `Hub.rival_card` takes LV/W/L/title from their LIST1.
- **During an NPC duel** the client stays in the block (keepalive 0x0005 on 5744) and, on returning, sends 0x5204 type 1
  and 0x4210, which already sets the state back to 1 (pvp_hub). So with `NPC_DUEL_STATE` the mechanism works end to
  end: 0x3112 op 0x67 + NPC name (LV/W/L/title 0: an NPC has no record) → pose for the others; on return → op 1.
- **Confirmed (C) by a period gameplay video: when a duel against an NPC starts, other players see the player in the
  "dueling" pose.** `hub.NPC_DUEL_STATE` (on; `YO2_NPC_DUEL_STATE=0` turns it off) is therefore 1:1. Whether that duel
  could be watched (C2) is NOT confirmed.
- **When it is set:** not at the 0x4288 (pvp_hub) nor at `duel_begin`, but **when the duel starts** (first 0x61fe of
  the duel connection, `npc_watch.py`), so it does not appear if the challenge is refused (`dp_npc_gate`, −612, runs
  before pvp_hub) or if the duel never starts. It is removed (op 1) **as soon as the duel ends** for any reason and,
  as a fallback, when returning to the map (0x4202/0x4210, and 0x5204 only for this pose). With the pose, "Watch"
  appears on the card: see C2.

### C2) Spectating duels against NPCs — OUR ADDITION (not 1:1)

There is no evidence that the original allowed watching a duel against an NPC (nor that it did not). Everything is in
`npc_watch.py` + small hooks; the client side follows the same path as the PvP spectator (§8, formats C), the server
side is P.

- **When it can be watched:** from the start of the duel (0x61fe; the same condition as the pose) until it ends.
  Before (challenge accepted, rock-paper-scissors) or after, 0x4321 = 1 (error).
- **0x4320** → `research/lobby` `h_watch`: PvP first (`PVP.watch_request`); otherwise `NW.watch_request` (running NPC
  duel of that pid) → 0x4321 s32 0 + `duel_ip_for` + duel port, and IP → duel is registered (`WATCH_REQ_TTL` = 60 s
  from pvp_match). A PvP watch granted to that IP deletes its NPC request (`NW.forget`). Everything is under `WATCH`
  (`YO2_WATCH`, on) and `NPC_WATCH` (`YO2_NPC_WATCH`, on; =0 turns off NPC recording and Watch).
- **0x6700** → the duel router calls `NW.on_duel_msg` BEFORE pvp_match: if that IP has an NPC request, the connection
  becomes a spectator (`c.npc_watch`; `c.pvp` = `_Proxy` so that `on_conn_closed` does not count it as abandoning a
  duel, and for the reservation) and receives: 0x600c only with `WATCH_SEND_PREP` (off, as in PvP), **0x6006** (147 B
  like `pvp_match.watch_info`: player 0 = the human with their deck size, name and parts; player 1 = the NPC with its
  official deck, name and parts as in 0x600c; "ESP"; mode 3) and **0x6701** (human's hand, NPC's hand, who goes first:
  absolute, 0 = the human). Each **0x6702** → the next batch (≤ `WATCH_BATCH` = 64) of 0x6202. 0x0005 → 0x0004;
  0x0003/0x6703/0x6700 are ignored; anything else (moves, 0x6001…) never reaches the duel.
- **What is shown = what the player has seen since the start** (including what already happened when joining midway):
  the player's duel connection carries its own INSTANCE `send` (not the Conn class one) that calls the usual
  `Conn.send` and then records each 0x6202 (and the NPC's hand from 0x6101). It is armed at the first 0x61fe (the
  0x6101 hand is recorded before that). The "Do you surrender?" dialog (`DFM.surrender_confirm_msgs`, while open) is
  not recorded: it belongs to the player only. The duelfin end (DUEL_END + 0x4d) goes through the same `c.send`: the
  spectator receives it too.
- **End:** 0x4d to the player (win, loss, surrender, draw, duelcore crash: all go through duelfin `finish`) → duel
  over, pose removed. If the player drops, opens another duel connection, or their ocg session is "over" without a 0x4d
  for 30 s, the watchdog (thread `npc-mirar`, every 1 s, only while there is something to watch) **cuts it**: the
  spectators receive the end of a loss by disconnection as seen from the player (`DFM.end_duel_msgs(1,
  WHY_DISCONNECT)`, only the 0x6202) and the pose is removed. A finished duel is kept 10 s (no spectators) or 120 s
  (with spectators) and then forgotten.
- **Isolation:** the spectator has no session (`c.sess` None); it does not touch profile, DP, prizes, friendship,
  NPCBIN, record or the player's time; its traffic uses its own queue (`Conn.post`). Recording never raises (a failure
  only goes to the `[npcmirar]` log). Verified: the spectator pays no DP and its `rec` does not change; the player
  pays as usual.
- **Duel reservation** (`research/seguridad_duelo`, "vigilar" = monitor mode): the spectator does NOT send 0x6001 (C,
  §8 item 3) and the 0x4321 creates no reservation, as in PvP. If an NPC spectator connection sends 0x6001,
  `duel_reserva_6001` treats it like the PvP one (`spectator=getattr(c, "pvp", None) is not None`, because of the
  `_Proxy`): "would reject … spectator connection" in monitor mode and rejection in "on" mode. In monitor mode, as in
  PvP, that 0x6001 is let through (`HUB.attach`): that is the risk monitor mode already carries for anyone; the
  message itself is stopped by `on_duel_msg` (it never reaches the duel).
- **Limits:** duels already running when the module is hot-loaded cannot be watched (armed at 0x61fe). Max. 16
  spectators per duel. What the real client draws in spectator mode with an NPC (portrait, NPC texts): not tested live
  (D).
- **Tests:** `python npc_watch.py --test` (no network); `research/lobby/proposal.py --test` (h_watch with NPC);
  `python fake_client.py npcmirar` (no pose or Watch if the duel does not start; pose 0x67 + NPC name with LV/W/L/title
  0 at the start; Watch midway → 0x6006, 0x6701 = 00 01 00 and the same 0x6202 as the player from the beginning
  without the "Do you surrender?"; at the end op 1 and Watch = 1; player dropped with a spectator → the spectator
  receives the end and op 1; spectator's DP untouched).

### D) Find a friend by name and other social items

- **0x4520 REQFRIENDSEARCH: the 080912 client never sends it (C).** There is no sender in the protocol table, no
  immediate 0x4520 in the exe, and 0x4521 only appears in the dispatcher (`cmp eax, 0x4521`). Its handler FUN_006bcd40
  reads s32 → +0x870 and then pairs (u32, u16) that it **discards**, and sets +0x40 = 2: a leftover of an older
  version. Nothing to do.
- **The real search is 0x4500 GETPIDBYPNAME** (friends, ignore list and mail by name → 0x4501 s32 + u32 pid). Finding
  only the player themself and the NPCs would make adding **another player** by name return "does not exist". →
  `PIDNAME_PLAYERS` in `research/lobby/proposal.py` (**on**, C; `YO2_PIDNAME_PLAYERS=0` turns it off): searches
  (case-insensitive) among the hub sessions and, failing that, among the accounts in state.json (pid = 1000 +
  4·index + slot, the one the hub assigns them; offline = P). A friend request to an offline player stays "sent"
  (there is no notice queue, D).
- **0x4563 TEAMCREATE_NOTICE: empty handler** (FUN_006bd070 = `return`). Sending it changes nothing (C); not sent.
- Tests: lobby `--test`; `fake_client.py pvp`.

## 10. Previously missing messages — `proposal.py` (section "Tanda mensajes que faltaban"), `selftest_mensajes()`

The messages outside the duel that had no specific handler. Confidence: **C** = read in the client code ·
**P** = deduced · **D** = doubtful. Tools: decompiled client and, for what the decompilation lacks (0x8ef050,
0xa894c0), Ghidra `ghidra/decomp_at.py`. Absolute references (vtables) were searched in every section of the exe, not
only in code.

### Implemented

| Message | Who sends it and when | Format / what the client does | Server | Flag | Conf. |
|---|---|---|---|---|---|
| 0x42a0 RESETAUTOWAIT → **0x42a2** | lobby task, state 0x13 (`FUN_006d3ef0` ← scene `FUN_0087e810` step 3, with the queues at `0x12bb6b0` empty). Observed live | 0x42a2 s32 (`FUN_006bc230`, +0x3c=2, k=0x20): 0 → scene 0xb; other → 0x4a with the code | s32 0 | `RESETAUTOWAIT` **on** (`YO2_RESETAUTOWAIT=0` turns it off) | C |
| 0x8203 REQUSBDPAUTH → **0x8204** | window `CWindowModelDuelpassUSB` (`FUN_00a894c0`, jump at 0xa8a8a6), only if the USB server answered 0x8202 = 0. **Not dead code** | 0x8204 (`FUN_006c1710`): s32; **only if 0** reads s8 + 3×(u16 card, u8) + u8, adds the cards to the trunk and grants the DUELPASS. An automatic reply of 9 zero bytes = "success" with short data | s32 1 (error; nothing else is read) | `USBDPAUTH_REFUSE` **on** (`YO2_USBDPAUTH_REFUSE=0`) | C |
| 0x8301 REQTPPRESENTINFO → **0x8302** | login scene `FUN_00850d90` case 0x14 (only with `tp_rank` 1-1000 or PBIN bits 0x240/0x242) | u16 = TP prize card → CNetLobby+0x548 ("TP present victory card:%d"), +0x7c=2 → case 0x15 | u16 `TP_PRESENT_CARD` (0) | `TPPRESENT` off (same 2 B as the automatic reply) | format C, card **D** |
| 0x8401 REQTPCARDGET → **0x8402** STARTUP_PRESENTGET_RESULT | **it does have a caller** (0x8efa38): `Deck\presentEntry.cpp` (`FUN_008ef050`), button 4000 of the Yes/No dialog. Before that, the login scene (case 0x18, 0x852eaa) checks the "Startup Present" PBIN bits 0x30e/0x310/0x312/0x314/0x316 (782-791 in `yo2pbinmap.dat`: 30/40/50/60/10) and keeps the **last** one set | request: u8 n + n×u16 chosen cards (`FUN_006cef30`, +0x7e). 0x8402 (`FUN_006c29c0`): s32 (k=0x62) and **always** u8 n + n×u16 (each card to the trunk; stack of 80 u16 → n ≤ 80). Case 0x1a: s32 0 → sets bit+1 "GET済み" (0x8535a1) | with the flag: validates (right set, "済み" not set, n ≤ the present's number, valid cards), adds to the trunk, sets bit+1 and returns the same list; error = s32 1 + n 0 | `STARTUP_PRESENT` **on** (without it: `auto_overrides`, success without cards) | format C; echoing the list and the cap **P**; the client does not clear the right bit (is it offered again in case 0x18?) **P** |
| **0x4700** MAINTENOTICE (notice, no request) | sent by the server | s32 = **seconds** remaining → +0x1bac, `time()` in +0x1bb0, +0x48=2 (nobody waits) and turns on **icon 0** of the menu bar (`FUN_00a7fb70(0)`). When clicked (`FUN_00a7a610`): texts 5270 "Maintenance will be carried out shortly." + 5271 "Time remaining until the server shuts down:" + `%02d:%02d` = **hours:minutes** (local countdown) | `LP.maintenance_notice(seconds)`: to the lobby connection of every hub session (without a hub: to the last lobby/room connection of each client) | no flag: sent manually only | C |

Notice before a restart, from the server console: `LP.maintenance_notice(600)` → icon on the menu bar; the text shows
the remaining time (hours:minutes). Sending it again reschedules it (overwrites +0x1bac/+0x1bb0). No network message
removes it (`FUN_00a7fbb0(0)` is not called from the network: P).

### Not applicable (in `proposal.NO_APLICA`, with the reason; none has a handler)

| Message | Reason | Conf. |
|---|---|---|
| 0x4410 / 0x4424 / 0x4428 chat rooms | senders `FUN_006ccbf0/006ccc40/006ccd70`: no calls, no jumps and no absolute references. There are no 0x441x/0x442x responses with a handler either | C |
| 0x430a AUTOMATCHREPLY | sender `FUN_006cc960` without calls or references | C |
| 0x4904 / 0x4905 PRESENT_PACK_OPEN | sender `FUN_006ce570` without calls or references; 0x4905 is its response | C |
| 0x5201 / 0x5203 miles | responses to 0x5200/0x5202, whose senders (`FUN_006ce6b0/006ce700`) have no calls (also noted in `research/torneos/proposal.py`). The real miles shop is 0x5208/0x5210 | C |
| 0x5215 / 0x5216 PURCHASE_SHOP_ITEM_RESULT_BEGIN/END | 0x5215 = empty handler; 0x5216 only sets +0x83, which nobody waits for (the shop waits for +0x82 = 0x5211, which the server already sends). That the original sent them around 0x5212 (like 0x5213/0x5214 with the list): P, no effect | C |
| 0x4182 SAVE_DECK | `FUN_006c3340`: discards the u32 and copies 128 + 512 B to CNetLobby+0x74b/+0x54a; no other function reads those fields and it sets no flag | C (search over the whole decompilation) |
| 0x4521 REQFRIENDSEARCHRESULT | the 0x4520 request does not exist (see "2008 world" D); +0x40 is already set by 0x3604 | C |
| 0x4563 TEAMCREATE_NOTICE | handler `FUN_006bd070` = `return` | C |
| 0x7107 CARDLIST / 0x7110 CANCELREQTRADERESULT / 0x7111 TRADEOFFERTIMEOUT | empty handlers (`research/intercambio/NOTES.md` §3) | C |
| 0x7115 MOVEOWNCARD | stores u16 + u8 in +0x1bce/+0x1bd0 and sets +0x58; the trade scene does not look at it. That the original sent it to the mover as a confirmation: P, no effect | C |
| 0x8202 USBCHECKRESULT / 0x8205 USBGETCARDLIST | **not lobby messages**: read by another connection (game+0xc74, `FUN_006d59b0`, dispatcher `FUN_006d6010`) to the **type-6 server** of the server list (`FUN_006d5e50`), to which the client sends 0x8201 with 16 B of the account. It is the "USB Duelpass" product (registry key `usbDuelpass`, `CRegistryManagerUSB`, `CWindowModelDuelpassUSB`), not a DP reader: 0x8205 carries u8 use, s8, 3×u16 cards. Supporting it requires announcing a type-6 server and the DUELPASS economy: out of scope | C |

### How it is tested

- `python research/lobby/proposal.py --test` → prints "prueba mensajes que faltaban…: 0 fallos" (0 failures; with and without flag;
  right, "済み" and cap of the Startup Present; 0x4700 with and without hub).
- `python fake_client.py pvp --offset <n>`. Use a dedicated port offset: another test server running on the same ports
  makes the run fail.

## Gate rooms: names and Beginners' Room

Static reading of the decompiled client, disassembly of `yo2.exe` and the client texts.

**Gate list = `FUN_0082e170`.** Types requested per gate (`this+0x7c`): blue (0xd) → 13, 7, 15, 8; red (0xe) → 14, 1,
9, 16, 17, 10, 20 (C). If the `NoEventLobby` flag of the client config is set (`DAT_0127188c`->vfunc 0xd8 = byte
+0x1c1, key at 0xc8d51c) it only requests 13 (blue) and 14, 1 (red): the rest are "event rooms" (C). For each type it
walks rooms 0-7 and takes the 0x2003 entry with type 3, `b1` = selected language (`this+0x78`), `b2` = type, `b3` =
room number; it draws "%s (%4d/1000)" with the u16 occupancy (C). If none is found, it draws text 4591 "Room not
available" (C).

**Name = `FUN_00a54f90(type, no.)`** (jump table 0xa55810) + suffix A…H by room number 0-7 (C):

| b2 | Text | Name | Gate |
|---|---|---|---|
| 13 | 3065 | Domino City | blue |
| 7 | 3052 | GUNSLINGER Room | blue |
| 15 | 3052 | GUNSLINGER Room (same text as 7) | blue |
| 8 | 3054 | CHAMPIONSHIP Room | blue |
| 14 | 3066 | Duel Academy | red |
| 1 | 3050 | Beginners' Room | red |
| 9 | 3053 | TRIAL Room | red |
| 16 | 3053 | TRIAL Room (same text as 9) | red |
| 17 | 7060 | EVOLUTION MATCH | red |
| 10 | 3055 | ANNIVERSARY Room | red |
| 20 | 7072 | LAST BATTLE | red |
| 12 | 3067 | Your Room (My Room, not shown at the gate) | — |

2-6, 11, 18, 19 → assertion "illegal rank" (C). That is why screenshots show 3 names on the blue gate and 6 on the red
one.

**Language `b1` = `FUN_00a54b80`** (C): 0 Japanese (2001), 1 English (2002), 2 German (2003), 3 Italian (2005),
4 French (2004), 5 Spanish (2006), **6 World (2017), 7 Korean (2016)**. `FUN_0082da70` lists 0-5 and 7 and leaves 6
(World) at the end. Note: the world room is 6, not 7.

**Beginners' Room by level (C, disassembly 0x82e1ba-0x82e1db and 0x82e742-0x82e762):**
`LV = byte reg+0xa4` of the selected slot (`account+0x121c`, slot = `account+0x18ec`, `FUN_006a5bc0`), read **every
time the list is drawn**. Condition to show an entry:
`(LV <= 1 and b2 == 1) or (LV >= 2 and b2 != 1)`.
- LV 0-1: on the red gate **only** the Beginners' Rooms appear; on the blue one nothing appears ("Room not available").
- LV ≥ 2: the Beginners' Room **disappears** from the list; there is no way to choose it from the gate.
- It does not change the name or replace 14 with 1: it **filters by `b2`**. In a test it looked like "the same entry"
  because with LV 1 the first row of the red gate is Beginners and with LV 20 it is Academy (the order is 14, 1, 9…).
- There is no "beginners only / Lv X" text (search in yo2text_s); the filter is silent.
- `reg+0xa4` is written by PSELECTRESULT 0x4101 (offset 26), PLAYERDATAUPDATE 0x4140 (offset 0) and PLAYERLIST 0x3012
  (offset 17) (`research/persistencia`). So raising `rec.lv` and sending 0x4140 is enough without reconnecting
  (observed live: a test character raised to LV 20 entered room (5744, 5)). The value must be written inside `rec`
  (`rec.lv`), not as a top-level `lv` key.

**1:1 proposal (not implemented):** the filtering is done by the client, so the official server needed nothing more
than to send the real LV. Consistent behaviour: (a) announce the type-1 rooms in 0x2003 as now (the client already
hides them at LV ≥ 2); (b) always send the real LV in 0x3012/0x4101/0x4140; (c) **P**: that the server also refuses
entry to the room (port 5745) to an LV ≥ 2 arriving by another route (the official error code has not been seen; do
not invent one). LV 2 is the first level that leaves Beginners. See also `persistencia.START_LEVEL` in
`yo2srv/BANDERAS.md`.

## 0x3604: where each friend is

Bug (fixed): with a PLAYER friend online, the Friend List crashed with "illegal rank" (0x3604 sent with the 5 B at 0).

**Path (C):** `FUN_006bcdd0` reads u32 ?, u32 n and n×(u32 pid, 5 u8) and, for each pid in its 100-entry table
(`obj+0x13b0 + i*0x14`, getter `FUN_006af030`; obj = `[DAT_012711ac+0xc6c]`), sets `+8 = 1` (online) and the bytes at
+0xd, +0xc, +0xe, +0xf, +0x10 (**the 1st and 2nd are swapped**). The friends window (`FUN_00a93920`, which requests
0x3603 with `FUN_006ccfa0`) sets all its rows to state 1 and, at 0xa93c2c, for each online friend calls
`FUN_00a95520(pid, 2, +0xd, +0xc, +0xe, +0xf, +0x10)` → 0x2c B row: state, language, type, room, area, block.
`FUN_00a943b0` draws it: state 0 → text 4029 "Requesting now...", 1 → 5019 "This Player is not online now.",
2 → columns with the 5 values.

| Byte (wire order) | What it is | Drawn by | Out of range |
|---|---|---|---|
| 1st | language `b1` | `FUN_00a54b80` | > 7 → "unknown", no assertion (C) |
| 2nd | room type `b2` | `FUN_00a54f90` | 0, 2-6, 11, 18, 19, > 20 → assertion **"illegal rank"** (C) |
| 3rd | room number `b3` (A-H) | `FUN_00a54f90` | > 7 → assertion **"illegal area"** (C) |
| 4th | map area | `FUN_00a55b30` (texts 0xd2f2b8) | > 9 → drawn as 0, no assertion (C) |
| 5th | block number | `"%s[%d]"` with area and block + 1 | no limit (C) |

Areas (C, texts): 0 Clock Square, 1 Kame Game, 2 Domino Pier, 3 Kaiba Corp., 4 Duel Academy, 5 Shop, 6 Duel Space,
7 Slifer Red Dorm, 8 Your Room, 9 Kaiba Corp. Lab. They match `WORLD_AREAS` in pvp_hub. With type 12 (My Room) the row
uses another, shorter format (`@>16/%s@>230/@0%s`) (C): the client expects friends in My Room.

- All zeros = type 0 → "illegal rank" (C). A friendly NPC sent with zeros triggers the same (C by code).
- **There is no "dueling" field** (C: only state 0/1/2 + the 5 B); a friend in a duel appears in their room (P: the
  original did the same).
- The "[n]" block is the block number within the area (P: with one block per area the server sends 0 → "[1]").

**What `h_online` (proposal.py) sends:** hub session with a live `s.lobby` → in a room (`s.block`): (block language,
`ROOM_KINDS[port]`, 0, `WORLD_AREAS[world][s.ablock]` or the entry area, 0); in My Room
(`s.lobby.port == LOBBY_PORT`): (0, 12, 0, 8, 0); on a room port without a 0x4202 yet: that room with the entry area.
Menus, gate, session with a closed connection (sessions are not deleted on leaving) → **not online**. Friendly NPC →
the room from which the question is asked, or Domino City (13) with your language if asking from My Room (P: safe
value, no data). Final filter: type outside {1,7,8,9,10,12,13,14,15,16,17,20} or room > 7 → not sent.

## Auto-duel — flags `lobby.AUTODUEL` (on) and `lobby.AUTODUEL_MATCH` (on)

Confidence: **C** = read in the client · **P** = deduced · **D** = doubtful.

| What | Where | Conf. |
|---|---|---|
| "Duel Menu" (`FUN_00a38700`/`FUN_00a3a870`): "Auto-duel" (text 5090) sets CNetLobby `+0xa4` (mstat) = 1; "Create Duel" (5091) = 0. "Auto-duel" opens `CWindowModelDuelFind` (`CDuelMenuTask::m_AutoDuel`, `FUN_00a430b0`): type 3/4/6 ("Both"), `+0xbc` = "similar level", `+0xc4` = 1 on accept | decompiled client | C |
| On accept, the scene (`FUN_00863ec0`) checks DUELPASS (error −612) and goes to state 0xc → `FUN_0086bc60` → **0x4288 REQCREATEROOM** with mstat (3rd byte) = **1** and opponent 0xffffffff. There is no dedicated "activate" message. Recorded: `03 01 01 00 … 00 01 01 00 ff ff ff ff` | decompiled client + recorded session | C |
| Deactivate = delete the room (0x4290), like a normal room | scene | P |
| After a duel, with an own room (`+0xd8b`, set by 0x4284/0x3113 with P1 = self) and mstat 1 → state 3 → **0x42a0 RESETAUTOWAIT** (`FUN_0087e810`); 0x42a2 0 → map (0xb). Meaning: "I am waiting for auto-duel again" | decompiled client + recorded session | C (client) / P (meaning) |
| How the client sees ANOTHER player: `+0x130` (LIST2 `+0x66` / 0x3112). **0x66** = "Auto-duel" only: `FUN_00888970` counts 0x66 and 0x69/0x6a in the "Players" of "Auto-duel\nPlayers %d\nWaiting%d\nDuel%d" (5086), 100/0x65 in "Waiting", 0x67-0x6a in "Duel". `FUN_0088d170`: 0x66 → **icon 3** (100 → 2, dueling → 1) in the player list (`FUN_0088ac60`) and on the doll's label (`FUN_009df3d0`). The card (`FUN_0088add0`) only offers "Challenge" with 100..0x65 | decompiled client | C |
| What the server sent: 0x66 while the auto-duel room waits (it is the only value the client treats as "Auto-duel") | — | P |
| Challenging someone looking for auto-duel: −602/−604/−606 → 0x11c9 "This Player is looking for Auto-duels." (`FUN_00884b70`). Which request returned it exactly: P (REQMATCH to their room) | decompiled client | C (text) / P |
| Pairing: 0x430a AUTOMATCHREPLY has no callers (C): the client does not answer. With **0x43a0 AUTODUELSTART** on the map, `FUN_00863ec0` (k=0x1f, 0x8675ae) goes to state 0x3d → `FUN_0087ca70`, the same path as the direct duel against an NPC (verified live); then 0x4306 | decompiled client | C |
| Pairing rules of the original (type, level, block/server, order, mode byte for a match) | not in the client | D |

**Server** (`proposal.py`, "autoduelo" batch):
- `AUTODUEL` (on; `YO2_AUTODUEL=0` turns it off): room with mstat 1 → state **0x66** to the others (0x3112 and LIST2);
  when returning from the duel (0x4210 with `ROOM_REOPEN`, and 0x42a0) 0x66 again; 0x4300 to that room → 0x4301
  **−602**.
- `AUTODUEL_MATCH` (on; `YO2_AUTODUEL_MATCH`): two waiting players in the same block, type 3 (or 3 with 6), and if
  either asked for "similar level", |LV| ≤ `AUTODUEL_LV_DIFF` (5, D) → `PVP.reserve`, 0x3113 with P2, state **0x69**
  (P) and to each one 0x43a0 (opponent, name, LV, title, parts, mode 0) + 0x4306. Match (type 4): not paired (D).
- Unchanged: the NPC that challenges a room if you are alone in the block (0x4302) also challenges the auto-duel room.

## Rooms per language 1:1 — flag `lobby.ROOM_PER_LANG` (off)

Static reading of the decompiled client and its disassembly (`+ 0x9ec]` accesses).
Confidence: **C** = read in the client · **P** = deduced · **D** = doubtful.

| What | Where | Conf. |
|---|---|---|
| 0x2003 entry (48 B, `FUN_0069a6a0`): u32 id, u32 type (1 accounts, 3 lobby), name 16, IP 15, u16 port (+0x2e), u16 occupancy (+0x30), u8 **b1** (+0x32), u8 **b2** (+0x33), u8 **b3** (+0x34). The client itself calls them "rgn", "rank" and "area" (debug strings 0xc9ec3c and 0xc9ec88) | `proto.server_entry`, `FUN_006abe10` | C |
| Gate = `CMyRoom2MapTask` (vtable 0xbee158). State 2: language list (`ui_language_list_build` 0x82da70); on selection, `FUN_0082d9f0` stores the language in +0x78 and draws the rooms (`lobby_door_room_list` 0x82e170: type 3, b1 = +0x78, b2 by gate, b3 0-7). State 3: on choosing a room, `FUN_0082d750` stores b3 in +0x80 and b2 in +0x7c | decompiled client | C |
| On connecting: `FUN_006d3ca0` copies (b1, b2, b3) to CNetLobby +0x9ec/+0x9ed/+0x9ee and `FUN_006abe10` looks in the list for the type-3 entry with those THREE bytes and connects to ITS IP (`FUN_0068ec10`) and ITS port (+0x2e): "接続先ロビーサーバはコレですね！！ rgn=%d,rank=%d,area=%d". If there is none: "そんなサーバはねーずら" and −0x78 | decompiled client | C |
| **No message carries b1 to the server.** +0x9ec is only read by that lookup, the reset (`FUN_006ab5e0`: 0/1/0) and 0x5000 EVENTINFO (`FUN_006bf310`: accepts the event if its "world" is 6 or equals b1). 0x4100 carries the CLIENT language (French 3, Spanish 5), which is not the room's b1 (French 4, Spanish 5) | disassembly | C |
| The official server knew which room each client was in only from where it connected: every (b1, b2, b3) was an entry with its own IP:port (the client calls it a "lobby server"). Spanish and French rooms = different servers/ports | — | P |
| The world room (b1 6, text 2017) is one more room with its own IP:port, where people with the game in any language enter | EVENTINFO treats 6 as "all" | P |
| Whether the original used one port or one machine per room, and which languages/rooms each region had open | — | D |

**Server** (`yo2srv/red/puerta.py`; `YO2_ROOM_PER_LANG=1`; registry `lobby.ROOM_PER_LANG`, origin oficial, vanilla
`False`):
- One port per (language, type): the **world** room (b1 6) stays on the usual ports, `ROOM_PORT_BASE + i`
  (5740-5750); the other 7 languages on `ROOM_LANG_PORT_BASE + band × 11 + i` (band = b1 for 0-5, 6 for Korean 7):
  **5800-5876** (+ `YO2_PORT_OFFSET`). They do not fit in 5740-5750 (11 ports for 88 rooms), nor between 5751 and 5798
  (5760 = duel, 5799 = console). `room_port`, `room_of_port`, `room_ports` and `room_block_key` (in the namespace as
  usual).
- Block key: `(ROOM_PORT_BASE + i, b1 of the room)`. The port of the key is still the "canonical" one, so pvp_hub,
  pvp_match, npc_watch, the admin side and `_room_where`, which derive the type with
  `ROOM_KINDS[key[0] - ROOM_PORT_BASE]`, are unchanged; the language of the key becomes the room's (what `h_online`
  sends in 0x3604 as the friend's b1, C per the §0x3604 table). `block_type` (0x4201/0x4203) also returns the room's b1.
  `_conn_where` (proposal.py) translates the real port of a connection (friend without 0x4202, friendly NPC).
- Gate list: each room announces its port and counts the players of ITS block.
- `comprobar_puertos` checks the ports of each IP in parallel (on Windows a `connect_ex` to a free port takes ~1 s:
  with 93 ports startup took ~95 s).
- Turning it on at runtime without a restart does not work: the language ports are not in HANDLERS and the gate
  announces the usual ones (everyone ends up in the world room). A **restart** is required.

**Interaction with `ROOM_LANG_MERGE`:**

| ROOM_PER_LANG | ROOM_LANG_MERGE | Result |
|---|---|---|
| off | on (default) | one port per type, everyone together (key `(port, 0)`) |
| off | off | one port per type, separated by CLIENT language (earlier behaviour, not 1:1) |
| on | any | 1:1: separated by ROOM; the world room mixes languages. ROOM_LANG_MERGE is not used |

**Launchers:** the tunnel only forwards/listens on the ports returned by `Puertos()` in `launcher/sala/tunel.go`:
5730, 5731, 5732, 5760, 8730, 5740-5750 and 5800-5876 (the host refuses any other with "port not allowed"). A guest
using an older launcher that does not listen on 5800-5876 can only enter the world rooms with the flag on (the client
does not connect). If Tailscale mode is used, its ACL must also allow 5800-5876. The guest listens on 77 more ports on
127.0.0.1.

Tests: `python fake_client.py salas --offset <n>` (own instance with `YO2_ROOM_PER_LANG=1`): A with the game in
Spanish and B in French; same Spanish room = they see each other, Spanish and French = they do not, world = they do,
world and Spanish = they do not, gate counts per room, Spanish Academy with area 5. Unit tests: `yo2srv/tests/test_red.py`
(`TestPuertos`, `TestGate`).

## Two players behind one IP: per-connection numbering (26-10, web)

The client drops every message whose number is not its counter + 1 (`FUN_0068bac0`, per connection). Since 2026-10-06
`Conn.seq` was one counter per (client IP, port) on every port, so that a client's second duel connection continues
its count. On the public web build every player reaches the server through the network gateway from 127.0.0.1, so
two players in one room shared a counter. When B entered a block where A already was, the 0x3112 (and the 0x4181)
the hub posts to A spent B's numbers. B then dropped its own 0x4211 and 0x4171 and stayed in My Room, and A got
numbers out of order too. Fix (`yo2srv/red/conexion.py`): lobby, rooms and accounts count per connection (they always
start at #1). Only the gate and the duel port keep a client's count across connections, keyed by (account, or IP on
the gate, port, the client's next message number). Tests: `test_red.TestMismaIP`, which uses three real TCP
connections from the same 127.0.0.1.
