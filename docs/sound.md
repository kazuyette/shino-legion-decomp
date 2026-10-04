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

## 3. Playing a sound: `Snd_Play(id)` A.BIN 0x06007CDE

```
id < 0          -> -1
id < 0x100      -> table 0x0600EF20[id],         flag 1   (tunes / jingles)
0x100..0x1FF    -> table 0x0600EF50[id - 0x100], flag 0   (system SE)
0x200..         -> table 0x0600F0D0[id - 0x200], flag 0   (stage SE)
entry (8 bytes) = { u16 seq bank, u16 song, u16 priority, u16 0 }
0x06007FA8(id, bank, song, priority, flag): request queue 0x06025178 (17 x 10 bytes,
   count 0x06025244), state 3 = pending; a request for an id already pending is
   dropped; when full, replaces a lower-priority non-tune request.
0x0600863E (scheduler, per frame): starts the requests on the 8 sequence slots
   0x06025138 (8 x 8 bytes, +0 id) through SBL SND_StartSeq 0x06030A82.
```

* `0x06007D60(id, max)`: Snd_Play only while fewer than `max` slots play `id` (used by
  the animation scripts, anim.c: word `0xNiii` = id iii, max N).
* `Snd_StopAll` 0x06007E00: drops pending requests, stops every slot (0x060084BE).
* `0x06007DBA(vol, rate)`: 0x06008154 on the 8 slots (sequence volume fade).

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
(the entry lasts `ticks`; looping tunes have 2: intro length, loop length).
Events (one track, MIDI channels multiplexed; `step` = ticks to the next event):

| byte | event |
|---|---|
| 00-7F | note: channel = low nibble, `key, velocity, gate, step`; bit 6 = gate + 0x100, bit 5 = step + 0x100 |
| Ax | poly pressure `key, value, step` |
| Bx | control change `ctrl, value, step` (only 0x07 volume, 0x0A pan, 0x20 tone bank select are used) |
| Cx | program (voice of the tone bank) `prog, step` |
| Dx | channel pressure `value, step` |
| Ex | pitch bend `value (0x40 = centre), step` (range = voice header byte 0, semitones) |
| 81 hh ll nn | play the `nn` events at data + 0xhhll then come back (prefixes 88-8F not counted, no nesting) |
| 82 ss | loop point (+ step): 1st = loop start, 2nd = jump back to after the 1st |
| 83 | end of track |
| 88 / 89 / 8A / 8B | next gate + 0x200 / 0x800 / 0x1000 / 0x2000 |
| 8C / 8D / 8E / 8F | next step + 0x100 / 0x200 / 0x800 / 0x1000 |

Verified: with these step rules (and the 0x81 rule) the two loop markers of all 22
looping tunes fall exactly on the tempo table's intro / loop lengths. The gate
extensions are by analogy (they do not affect timing).

## 5. Tone bank (SBL TON)

`u16` offsets of the mixer, velocity, PEG and PLFO tables, then `u16` voice offsets up
to the mixer table (`(mixer - 8) / 2` voices). Voice: `u8 bend range, u8 ?, u8 layers - 1,
u8 ?` + 32-byte layers (every voice of this game has one layer, keys 0-127):

| offset | content |
|---|---|
| +00 / +01 | start / end key |
| +02 | SCSP reg 0x00 bits: b6-5 LPCTL (0 off, 1 loop, 2 reverse, 3 alternate), b4 PCM8B, b3-0 SA bits 19-16 (b13 always set) |
| +04 | SA bits 15-0: **byte offset of the PCM in the tone bank** |
| +06 / +08 | LSA / LEA (samples) |
| +0A | reg 0x08: D2R / D1R / EGHOLD / AR (always AR 31, D1R = D2R = 0) |
| +0C | reg 0x0A: LPSLNK / KRS / DL / RR (always RR 31) |
| +0E | reg 0x0C: TL in the low byte (always 0) |
| +10 | reg 0x0E modulation (0) ; +12 / +14 zero |
| +16 | reg 0x14: ISEL / IMXL (DSP send) |
| +18 | DISDL << 5 \| DIPAN (DISDL 0 = only through the DSP / reverb) |
| +19 | base key: the key at which the sample plays at 44.1 kHz (OCT = FNS = 0) |
| +1A | fine tune (signed) |

PCM: 8-bit signed or 16-bit signed big-endian, inside the tone bank (base keys seen:
0x3C, 0x48, 0x52-0x58).

## 6. PC port (`pc-port/src/sfx.c`)

* `resource.c`: kind-2 requests read the .SND and call `Sfx_LoadBankData` (parse +
  install under the audio lock). `Res_LoadStageSoundBank(stage)` = SHINOBI 0x0606BB0C.
  `Res_EnsureSoundBank()` (PC): if an SE is requested before any bank was loaded (title
  after power-on), STAGE7.SND is loaded (system banks 0/1 are identical in every file).
* `player_stubs.c`: `sub_06007CDE` → `Audio_PlaySfx` → sfx hook; `sub_06007D60` (count
  limit) and `sub_06007E00` (stop all) too.
* `audio.c`: `AudioSfxHooks` {play, stop, render}; the mixer adds the SFX output (32-bit)
  to music + movie sound before clipping. Without hooks audio.c behaves as before.
* Engine: 8 sequence slots (same id restarts its slot, otherwise a free slot, otherwise
  the lowest-priority / oldest non-tune slot with priority <= the new one; a new tune
  stops the previous tune), 32 voices (oldest released voice stolen first), linear
  interpolation, SCSP loops, SCSP EG rates (MAME tables, no key rate scaling), volume
  `(cc7/127)^2 * (vel/127)^2 * DISDL * TL`, CC 0x0A pan, pitch
  `2^((key - base + fine/128 + bend * range) / 12)` at 44.1 kHz, master gain 0.4.
* Test: `tests/test_sfx.c` parses the 10 banks, walks every song, plays every id of the
  tables (non-dummy ones must be audible), writes `/tmp/sfx_STAGE7_<id>.wav`.

### Known gaps

* DSP effects (reverb: DISDL-0 layers are played direct at their IMXL level), PEG /
  PLFO / ALFO tables, velocity table, KRS, portamento, mono flag of the driver (audio.c's
  global mono option applies).
* Fine-tune unit (1/128 semitone assumed) and gate extensions 88-8B are inferred.
* Slot scheduler is simplified (no request queue / per-frame start; same-id restarts).
* `0x06007DBA` sequence-volume fades, `SND_StartSeq`/`StopSeq` of the debug sound test,
  `Snd_SetAreaMap` (movie map) are not wired.
