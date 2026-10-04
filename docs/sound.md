# Sound: SCSP banks (.SND), sound effects and sequenced music

How Shinobi Legions drives the Saturn sound CPU (68000 + SCSP, Sega Sound Library
driver) and how the PC port replays it (`pc-port/src/sfx.c`). CD-DA music (TITLE.PCM /
ENDING.PCM) and movie audio are described in `director.md` §3.2 / `pc-port/src/audio.h`.

## 1. Files

| file | content |
|---|---|
| `STAGE1.SND` … `STAGE9.SND`, `KAZUMA.SND` | one sound bank per game stage (STAGEn = file set n, see director.md §4.1: game 1-1 = STAGE7) ; KAZUMA = stage 9-2 |
| A.BIN 0x0600F318 (0x50CE bytes) | the 68000 sound driver (SBL SDDRV), uploaded by `SND_Init` 0x060304B0 |

No separate driver / tone files exist on the disc: every bank is self-contained.

## 2. Loading a bank

`Res_Request(1, kind 2, 0, -1, 0, "STAGEn.SND")` (A.BIN 0x06004A1C). Before the read
0x060309BC / 0x06030A1A / 0x06007BAC stop the driver's sequences. The file is read in
chunks; each chunk is fed to the state machine **0x06004C52** (state 0x0601ABC4):

| state | action |
|---|---|
| 0 | copy the first 0x80 bytes (16 area-map entries) to 0x0601AB44, setting bit 7 of byte 4 of each entry ("transferred"), then `SND_Init({0x0600F318, 0x50CE, 0x0601AB44, 0x80})` + 0x0603065C(0); mono option (0x06025246 == 1) → driver work +3 = 0x80 |
| 1 | read u16 `0x0T0B` (type T, bank B), look up the area map entry with byte 0 = `T<<4 \| B`, destination = 0x25A00000 + its 24-bit address |
| 2 | stream-decompress one word-LZ block there (**0x06004AA0**), back to state 1 at its end |
| 3 | end |

### 2.1 SBL area map (8 bytes per entry, 0xFF ends)

`+0` type<<4 \| bank id (type 0 tone bank, 1 sequence bank, 2 DSP program, 3 DSP work RAM),
`+1` u24 sound-RAM address, `+4` flags (bit 7 = data present), `+5` u24 area size.

Every .SND has the same map (KAZUMA: bank 2 bigger, bank 3 moved):

| id | area | sound RAM | size | content | same in every file? |
|---|---|---|---|---|---|
| 0x10 | seq bank 0 | 0x0B000 | 0x2100 | 31 system SE sequences | yes |
| 0x11 | seq bank 1 | 0x0D100 | 0x0400 | 11 SE (songs 5, 6 empty) | yes |
| 0x12 | seq bank 2 | 0x0D500 | 0x6000 | 4-6 tunes: 0 stage start, 1 stage clear, 2 death, 3 act-1 BGM, 4 act-2 BGM, 5 extra | no |
| 0x13 | seq bank 3 | 0x13500 | 0x1300 | 27-56 stage SE (ids 0x2xx) | no |
| 0x00 | tone bank 0 | 0x14800 | 0x24000 | 19 voices, system SE samples | yes |
| 0x01 | tone bank 1 | 0x38800 | 0x1A000 | 8 voices | yes |
| 0x02 | tone bank 2 | 0x52800 | 0x15000 (KAZUMA 0x25000) | 4-8 music instruments | no |
| 0x03 | tone bank 3 | 0x67800 (KAZUMA 0x77800) | 0x8000 (STAGE3 0xA000) | 6-24 stage SE voices | no |

(Built-in maps in DIRECTOR: 0x060443EE = the same layout, used on the title/menus with
whatever bank is resident; 0x06087124 = movie layout, frees sound RAM for the CPK PCM
buffer at 0x25A60000.)

### 2.2 Streaming word-LZ

Same coding as `lz.c` (A.BIN 0x060091C8): u32 header, then a u16 flag word (MSB first)
before each group of 16 items; bit 1 = literal u16, bit 0 = reference `w`: copy
`(w & 15) + 3` words from `w >> 4` words back (4096-word ring at 0x06018B20, write
position starting at 0xFEE). **Difference:** the streaming decoder's word count is
`(header & 0x7FFFFFFF) + 1` and it stops after the item that reaches it, so it consumes
one item more than `Lz_Decompress` (which uses the header as the exact count). The block
list of every .SND ends exactly at the end of the file with this rule; no reference ever
reaches before the block start.

## 3. Playing a sound: SH-2 request queue and slot scheduler

```
Snd_Play(id) 0x06007CDE:
id < 0          -> -1
id < 0x100      -> table 0x0600EF20[id],         flag 1   (tunes / jingles)
0x100..0x1FF    -> table 0x0600EF50[id - 0x100], flag 0   (system SE)
0x200..         -> table 0x0600F0D0[id - 0x200], flag 0   (stage SE)
entry (8 bytes) = { u16 seq bank, u16 song, u16 priority, u16 0 }
```

**Request queue** 0x06025178: 17 entries of **12** bytes, count 0x06025244.
`+0` kind (0 stop slot, 1 slot volume 127, 2 slot volume, 3 start, 0xFFFF done), `+2` sort
key (= kind), `+4` id, `+6` music flag (kind 3) / slot (kinds 0-2), `+7` volume, `+8` seq
bank / rate, `+9` song, `+10` priority (smaller = more important; tunes 0, SE 4/8/12/16).

| function | queues |
|---|---|
| 0x06007FA8(id, bank, song, prio, flag) | start; dropped if a start with the same id is pending; queue full: replaces the pending non-music start with the largest priority value above the new one |
| 0x06008154(slot, vol, rate) | vol 127 + rate 0 = kind 1 (turns a pending kind 2 of the slot into 1), else kind 2 (updates a pending kind 2; refused while a kind 1 is pending) |
| 0x060084BE(slot) | stop (once per slot; reuses a done entry) |
| 0x06007DBA(vol, rate) | 0x06008154 on every busy slot |
| 0x06007E00 Snd_StopAll | pending starts -> done, 0x060084BE on every busy slot |
| 0x06007D60(id, max) | Snd_Play only while fewer than `max` slots (table 0x06025138) hold `id` |

**Slot table** 0x06025138: 8 x 8 bytes {+0 id (-1 free), +2 priority, +4 driver status}.

**Scheduler 0x0600863E** (once per frame, skipped while 0x06025248 != 0):
1. sorts the queue by kind (0x060089D4), then the starts by priority (0x06008AF0);
2. 0x06007C14: reads the driver status of the 8 slots, frees (id = -1) the finished ones;
3. for slot 0..7: handles the **first** matching entry, then goes to the next slot:
   stop -> SND_StopSeq 0x06030B24 (slot freed); kind 1/2 -> SND_SetSeqVl 0x06030C80;
   start (tunes only on slot 0, sound effects only on slots 1-7) -> 0x06007C84(slot, prio):
   first free slot >= this one, else the first one >= this one with a larger priority value
   (| 0x8000), else -1. Free and equal: SND_StartSeq 0x06030A82 + id/priority stored.
   Steal (0x8000 | this slot): SND_StopSeq now, the request is kept for the next frame.
4. compacts the queue: done entries **and sound-effect starts that found no slot are
   dropped**; tunes, stops and volume requests stay.

So a sound effect of the same priority as everything playing is refused (no stealing), a new
instance of a playing id takes another slot (no restart: 0x06007D60 is the limiter), and a tune
waits for slot 0 (the game calls Snd_StopAll first).

Game users: SHINOBI 0x0607F8C2 = 0x06008154(0, 0, 0x80) (music fade-out, 12.8 s), 0x06084B56 =
0x06008154(0, 127, 0) (restore), 0x060842EA = 0x060084BE(0); ninjutsu 0x06085B68 fades the slot
playing 0x231 (rate 96) and 0x06085BC0 stops it; 0x060A8CD6 (sound test) 0x06007DBA(127 / 0, 0x80).

### 3.1 Id table summary

| ids | seq bank / songs | priority |
|---|---|---|
| 0x000-0x005 | bank 2 songs 0-5 (0 stage start, 1 stage clear, 2 death, 3 act-1 BGM, 4 act-2 BGM, 5 special: SHINOBI 0x0606C970 plays it through 0x0606BB70(5)) | 0 |
| 0x100-0x115, 0x117, 0x119, 0x121-0x127 | bank 0 songs (0x100→0, 0x101→1, 0x102→23, 0x103→2 (prio 12), 0x104→3, 0x105→4, 0x106→5, 0x107→6 extra life (prio 4), 0x108→7 confirm, 0x109→11, 0x10A→12, 0x10B→19, 0x10C→20 cursor, 0x10D→13, 0x10E→15, 0x10F→16, 0x110→10, 0x111→14, 0x112→8, 0x113→27, 0x114→23, 0x115→28, 0x117→29, 0x119→30, 0x121→17, 0x122→18, 0x123→22, 0x124→21, 0x125→24, 0x126→25, 0x127→26) | 16 unless noted |
| 0x116, 0x118, 0x11A-0x120 | bank 1 songs 0, 1, 2, 3, 4, 7, 8, 9, 10 | 8 (0x116-0x11C), 4 (0x11D-0x120) |
| 0x128-0x12F | bank 0 song 0 (unused) | 16 |
| 0x200-0x237 | bank 3 song (id - 0x200) | 16, 8 for 0x205-0x208, 0x222-0x22B, 0x235-0x237 |

Unused stage-SE slots are a one-note dummy sequence (program 0, volume 10, velocity 2).
Game stage → bank file (SHINOBI table 0x060DAFE4, names 0x060DAFBC): indices 0-15 → file
set (idx >> 1) (STAGE7, 2, 3, 4, 5, 6, 1, 8, 9), index 16 (9-1) → STAGE9.SND, 17 (9-2) →
KAZUMA.SND.

### 3.2 Stage start (SHINOBI `Game_LoadStage` 0x0606B6EC)

```
0x0606BB0C(stage): bank = 0x060DAFE4[stage]; if bank != 0x060F4D30: 0x060F4D30 = bank,
                   Res_Request(1, 2, 0, -1, 0, 0x060DAFBC[bank]), 0x060F4D32 = -1
music = (stage == 17) ? 3 : (stage & 1) + 3;  0x060F4D32 = music
Snd_StopAll(); Snd_Play(music)
0x0606BB70(m): 0x060F4D32 = m; Snd_StopAll(); Snd_Play(m)   (only caller: 0x0606C972, m = 5)
```

## 4. Sequence bank (SBL SEQ)

`u16 n` songs, `u32 offset[n]` (from the bank start). Song:
`u16 resolution` (480 ticks / quarter), `u16 tempo count`, `u16 data offset`,
`u16 tempo-loop offset` (8 = entry 0); tempo entries `{u32 ticks, u32 µs per quarter}`
(the entry lasts `ticks`; looping tunes have 2: intro length, loop length; the driver runs the
tempo list on its own timer, independent of the events, and restarts at the loop entry).

Events (one track; **`step` = delay in ticks BEFORE the event**, like MIDI delta times — the
driver emits a fetched event once its step has elapsed, 68K 0x4766; every song's first event has
step 0 and 80 % of the gates fit the *next* note's step, against 28 % for the own step):

| byte | event |
|---|---|
| 00-7F | note: channel = **bits 4-0** (0-31), `key, velocity, gate, step`; bit 6 = gate + 0x100, bit 5 = step + 0x100 |
| A0-BF | `d1, d2, step`: Ax poly pressure (unsupported by the driver), Bx control change; d1 bit 7 = channel + 16 |
| C0-DF | `d1, step`: Cx program, Dx channel pressure (unsupported); d1 bit 7 = channel + 16 |
| E0-FF | `value, step`: pitch bend (value << 7 - 0x2000), bit 7 = channel + 16 (Fx: unsupported) |
| 81 hh ll nn | play the next `nn` counted events at data + 0xhhll, then come back (88-8F, 81, 82 not counted; a nested 81 replaces the return) |
| 82 ss | loop point: `ss` adds to the next event's step; 1st = loop start, 2nd+ = jump there |
| 83 | end of track (the sequence ends once its notes are released) |
| 88 / 89 / 8A / 8B | gate + 0x200 / 0x800 / 0x1000 / 0x2000 (table 0x501E, confirmed) |
| 8C / 8D / 8E / 8F | step + 0x100 / 0x200 / 0x800 / 0x1000 |
| 80, 84-87, 90-9F | error: the sequence stops |

Only CC 0x07, 0x0A, 0x20 and channels 0-15 occur in the game's data; every channel starts with
a bank select (CC 0x20).

## 5. Tone bank (SBL TON)

`u16` offsets of the mixer (18-byte entries: EFSDL/EFPAN of the 16 DSP outputs + 2 CD inputs),
velocity (10-byte entries), PEG (10-byte) and PLFO (4-byte) tables, then `u16` voice offsets up
to the mixer table (`(mixer - 8) / 2` voices). Voice: `u8 mode << 4 | bend range, u8 ?,
u8 layers - 1, s8 volume bias` + 32-byte layers (every voice of this game has one layer, keys
0-127). In all 40 tone banks: mixer = zeros, velocity table = `0A 7F7F08 7F7F08 7F7F08`
(curve = velocity), PEG / PLFO tables empty, voice mode 0 (poly), bias 0:

| offset | content |
|---|---|
| +00 / +01 | start / end key |
| +02 | SCSP reg 0x00 bits: b6-5 LPCTL (0 off, 1 loop, 2 reverse, 3 alternate), b4 PCM8B, b3-0 SA bits 19-16 (b13 always set) |
| +04 | SA bits 15-0: **byte offset of the PCM in the tone bank** |
| +06 / +08 | LSA / LEA (samples) |
| +0A | reg 0x08: D2R / D1R / EGHOLD / AR (always AR 31, D1R = D2R = 0) |
| +0C | reg 0x0A: LPSLNK / KRS / DL / RR (always RR 31) |
| +0E | reg 0x0C high byte (bit 7: PLFOS/ALFOS forced to 0); +0F TL (always 0) |
| +10 | reg 0x0E modulation (MDL/MDXSL/MDYSL, 0); +12 0 |
| +14 | reg 0x12 hardware LFO (LFORE/LFOF/PLFOWS/PLFOS/ALFOWS/ALFOS): 0 in every layer |
| +16 | reg 0x14: ISEL / IMXL (DSP send) |
| +18 | DISDL << 5 \| DIPAN (111 layers have DISDL 0 **and** IMXL 0: silent, KAZUMA stage voices 0-15) |
| +19 | base key: the key at which the sample plays at 44.1 kHz (OCT = FNS = 0) |
| +1A | fine tune (s8, **1/256 semitone**, 68K 0x3120) |
| +1B / +1C | FM partner layers (MDXSL/MDYSL sources) |
| +1D / +1E / +1F | velocity / PEG / PLFO table numbers |

PCM: 8-bit signed or 16-bit signed big-endian, inside the tone bank (base keys seen:
0x3C, 0x48, 0x52-0x58).

## 6. The 68000 driver (A.BIN 0x0600F318, loaded at sound RAM 0)

Read with a 68000 disassembler (vectors: SP 0xA000, PC 0x1000; work RAM at 0x6000).

### 6.1 Timers and order
* Timer B (reg 0x41A = 0x1D4: TBCTL 1, TIMB 0xD4) interrupts every 44 x 2 = **88 samples**
  (501.1 Hz); the driver counts it as 2000 µs. Handler 0x4136: the note table (32 entries x 12
  bytes, 0x7C00) then the 8 sequence slots (0x60 bytes each, 0x7F00).
* Per slot (0x45B2), 8.8 fixed point: `step = 512000 / (µs per quarter / resolution)`
  (both divisions truncated) ticks per interrupt; tempo timer -= step (next tempo entry at
  underflow); event timer -= step, at <= 0 the pending event is emitted and the next ones
  fetched (their step added) while the timer stays <= 0. A note-on enters the note table with
  `gate << 8 + timer`; at <= 0 it sends the note-off of its (channel, key).
* Timer A (TIMA 0xA7, 89 samples): main-loop housekeeping; 0x1134 refreshes the pitch of 8 SCSP
  slots per period (bend applied there, not at key on).
* Messages (status, slot << 5 | channel, d1, d2) go through a 64-entry queue to the main loop
  (0x2180): note on/off, CC, program, bend.

### 6.2 Volume (TL), pan, pitch
* Velocity curve 0x2FD2 (table: segments with types y, y+d<<s, y+d, y+d>>s, y-d>>s, y-d,
  y-d<<s, clamped 0..127); the game's table is the identity.
* Key on 0x3094: `l36 = 2 vcurve (255 - layer TL)`, `level = l36 x (2 x CC7 & 0xFF) >> 16`
  + voice bias (clamped), **TL register = 255 - level** (SCSP TL bits = 0.4 / 0.8 / 1.5 / 3 /
  6 / 12 / 24 / 48 dB). Velocity and channel volume are therefore linear in dB:
  vel = CC7 = 127 -> -1.5 dB; 100 / 100 -> -37.5 dB; each velocity step ~0.74 dB.
  CC 7 (and CC 11) re-applies `l36 x 2 CC7 >> 16` to the sounding notes of the channel.
* Sequence volume: the emitted note-on velocity is reduced by the slot attenuation
  (127 - volume, or the fade position) — playing notes keep their level. Velocity 0 = note off.
* CC 10: `v = value >> 2`, DIPAN = v - 16 (v >= 16, left attenuated = sound to the right) or
  0x10 | 15 - v (right attenuated); 3 dB steps, 0xF = off. It sets channel flag bit 7 (pan used
  instead of the layer DIPAN at key on). **Driver bug**: the sounding notes follow a pan change
  only on channel 0 (0x2972 compares the slot base with the channel struct offset).
* CC 0x20: tone bank (area type 0), channel volume := 127, program := 0. Program change: bend
  := 0, pan flag cleared (unless locked by the host).
* Pitch 0x311A: `x = bend + fine` (1/256 semitone), `i = (x >> 8) + key + 0x60 - base`,
  OCT/semitone from table 0x35D6, FNS = table 0x3696[semitone << 7 | (x & 0xFF) >> 1]
  = floor(1024 (2^(n/1536) - 1)) (all 1536 entries verified). Bend: `(value << 7 - 0x2000)
  x range / 32` with the driver's shift chains (0x2BC0, ranges 0-13).
* Note on 0x2C28: layers whose key range holds the key; a keyed slot with the same
  (sequence, channel, key) is re-used (retrigger), the others come from the free ring (slot
  keyed off longest ago; none free: the oldest keyed). Note off 0x3252: every keyed slot of
  (sequence, channel, key).
* Mono (68K byte 0x483, written by the SH-2 at bank load when 0x06025246 = 1 and by the
  options screen): every DIPAN / EFPAN cleared (centre, both sides at full level), restored
  when back to stereo.

### 6.3 Sequence volume / fade (SND_SetSeqVl, command 05, 0x409C)
`att = 127 - vol`. By slot state (0 idle, 1 play, 2 play + fade, 3 / 4 paused):
rate 0 sets att (and ends a fade); with a rate: idle -> fade from 127 (fade-in of the next
start), playing -> fade from the current att. Every 50 interrupts (~0.1 s) the position moves
by `((|new - old| << 8) & 0x7FFF) / rate`; reaching att 127 **stops the sequence**. The
attenuation stays with the slot (a later tune in the slot is silent until 0x06008154(slot, 127, 0)).

### 6.4 DSP / effects: none
* No area of type 2 (DSP program) or 3 (DSP work) in any .SND map, in DIRECTOR's title map
  0x060443EE (the bytes after its 0xFF terminator, `30 06E000 00 012000`, are a leftover work
  area entry the driver never reads) — only the movie map 0x06087124 lists SBL's default
  areas (programs 0x20-0x24 at 0x16000...), with no data loaded there.
* SND_ChgEfct (command 0x83 -> 0x2A36) and SND_ChgMix (0x87 -> 0x13D8) are not even linked in
  A.BIN (no reference to 0x0603077E / 0x060307F8); CC 0x5B (effect select) and CC 0x10 (mixer)
  are not in the sequences.
* Command 0x0C (0x060309BC, sent before every bank load) and SND_SetMap clear MPRO (0x100800),
  COEF and MADRS; the mixer tables (EFSDL of the 16 DSP outputs) are all zero.
So the SCSP DSP runs an empty program and its outputs are muted: the IMXL sends of the layers
(ISEL 0, IMXL 3-7) go nowhere and **the game has no reverb**; layers with DISDL 0 are silent.

## 7. PC port (`pc-port/src/sfx.c`)

* `resource.c`: kind-2 requests read the .SND and call `Sfx_LoadBankData` (parse + install
  under the audio lock = SND_Init). `Res_LoadStageSoundBank(stage)` = SHINOBI 0x0606BB0C.
  `Res_EnsureSoundBank()` (PC): if an SE is requested before any bank was loaded (title
  after power-on), STAGE7.SND is loaded (system banks 0/1 are identical in every file).
* `player_stubs.c`: `sub_06007CDE` -> `Audio_PlaySfx` -> `Sfx_Play` (queue); `sub_06007D60`
  (count limit on the slot table) and `sub_06007E00` (stop all) too.
* `audio.c`: `AudioSfxHooks` {play, stop, render, mono}; the mixer adds the SFX output after
  the music / movie mono averaging (the engine centres its own slots).
* Engine, sample-exact: SH-2 queue + scheduler (§3) every 735 samples, driver timer B (§6.1)
  every 88, pitch refresh every 89; 32 SCSP slots: 8/16-bit PCM, linear interpolation, LPCTL
  loops, EG with key rate scaling (rate = 2R + OCT + 2 KRS + FNS bit 9, MAME/manual time tables),
  TL / DISDL / DIPAN gains, hardware PLFO/ALFO of reg 0x12 (manual tables; unused by the data).
  Output gain 0.9 (`Sfx_SetVolume`; 1.0 = a full-level slot at full scale): SE 0x122 peaks at
  1.03 x full scale at 1.0.
* API for the remaining callers: `Sfx_SetSeqVolumeLocked` (0x06008154),
  `Sfx_SetAllSeqVolumeLocked` (0x06007DBA), `Sfx_StopSlotLocked` (0x060084BE),
  `Sfx_SlotIdLocked` (0x06025138), `Sfx_StartSeqDirect` / `Sfx_StopSeqDirect` (sound test).
* Test: `tests/test_sfx.c` parses the 10 banks (no DSP area, zero mixers), walks every song,
  plays every id (non-dummy ones audible), checks the pitch maths against the driver's tables
  in A.BIN, the velocity / TL formulas, the queue rules (pending duplicates, slot 0 = tunes,
  priority sort, refusal, steal one frame later), the fade (0x80: att 16 after 1.6 s; stop at
  127), mono (L = R), no clipping over every SE and 8 s of every tune, and the loop timing (two
  loop iterations of 1-1's tune: 10 ms envelope correlation 0.98, 7.37 s intro, 29.50 s loop).
  With a 2nd argument it writes listening WAVs (`Claude outputs/sound_check/`).

### Known gaps
* Voice modes (mono/legato, voice header bits 6-4), the software PEG / PLFO of the driver
  (layer flags never set, tables empty), FM (MDL), sustain (CC 0x40) and pause / continue:
  not used by the game, not emulated.
* The driver's 32-entry note table, 64-entry message queue and error states are emulated
  only where they change the sound (a full note table leaves the note unreleased, as on the
  68K).
* Output level vs the CD-DA music: a full-level slot = full scale x 0.9 (no MAME / hardware
  capture to calibrate against); music / movie mono is the average (the Saturn's centred
  inputs sum, +6 dB).
* `SND_StartSeq`/`StopSeq` of the debug sound test and `Snd_SetAreaMap` (movie map) are not
  wired by director_title.c (API available).
