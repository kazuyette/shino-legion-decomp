# Ninjutsu (magic): SHINOBI.PPB

This page covers the ported code in `pc-port/src/ninjutsu.c` (effects, magic) and `pc-port/src/player_stubs.c` (player sub-routines). The test is `pc-port/tests/test_ninjutsu.c`.

## RAM

| address | type | meaning |
|---|---|---|
| 0x060252BA | u16 | MAGIC flags. Low nibble = kind (0..6). 0x00F0 = armed sub-kind (0 shield, 0x40 kind 4). 0x2000 = cast requested / running. 0x4000 = armed. 0x8000 = effect started. |
| 0x060252BC | s16 | points of the armed magic (6 when armed) |
| 0x060252BE | s16 | ninjutsu available (scroll pickup `fx.c` kind 5 sets 1; the HUD icon). The normal cast uses it up. |
| 0x060EA43C | u16 | MAGIC saved at the cast (0xFFFF = none), used to re-arm a running shield / kind 4 after kind 2 |
| 0x060F4BF0 | block | effect: +0 state, +2 step timer, +4 frame counter, +6 step, +8 u32 step-duration table, +0xC.. variables |
| 0x060F4CA8 | u16 | graphics request (2/5/6), handled the frame after the start |
| 0x060F4CAC | u32 | step callback (original code address; dispatched by a switch on PC) |
| 0x060F4CB0 | 3 x s32 | colour offsets (0x06025100) saved at the start; the end fades back to them |
| 0x060EA32C | 2 x 0x78 | player sub-objects: kind-4 sword flashes (`PlayerFx_*`) |
| 0x060EA446 / 0x060EA546 | 64 x {x,y} / idx | position history ring (written by Player_Draw), used for afterimages |

## Casting

The 4 action selectors start the ninjutsu. These are stand 0x060411FC → 0x15, crouch 0x06041B3C → 0x2C, air 0x06041E48 → 0x3E and posture 4 0x060424A6 → 0x4A. A selector starts it when `MAGIC & 0x2000` is set (forced by game events), or when `0x060252BE != 0` and X is pressed. It plays sound 0x116 and freezes the world (`0x0602529E |= 0x8000`). The X cast sets `MAGIC = (MAGIC & 0xFFF0) | 0x2002`, so the normal ninjutsu is kind 2.

**Player_NinjutsuExec (0x060437C0)** runs while the action is 0x15 / 0x2C / 0x3E / 0x4A:
1. On the pose's first frame it saves MAGIC (if armed) to 0x060EA43C. It also opens the picture file: ZIRYU.PBC (kind 2), BISYAMON.PBC (kind 4) or NYORAI.PBC (kind 6).
2. When the pose animation ends and no effect is running, it calls `Ninjutsu_Start(MAGIC)` (0x060634A4) and sets `MAGIC |= 0x8000`, `&= ~0x4000`. It also frees the sub-objects (0x060468FC).
3. When the effect is over (state 0 again), it clears the MAGIC flags 0xA000 and puts Sho back to action 0. It gives 0x60 frames of invulnerability (`*0x060EA41C`), sets vel Y = 0, unfreezes the world and calls `Magic_Apply` (0x06045D1E). Finally it turns the NBG3 colour offset back on (CLOFEN bit 3, shadow dirty).

## Effects (0x06063554, called every game frame after the stage hooks)

`Ninjutsu_Start` saves the colour offsets and resets the fader. It then starts the effect for the kind and plays its sound:

| kind | start | state | sound | picture | step table |
|---|---|---|---|---|---|
| 0 | 0x06063B6C | 7 burst | 0x11A, 0x10D | Sho's own frame | 0x060AE674 |
| 2 | 0x06063642 | 2 ZIRYU | 0x118 | ZIRYU.PBC (0x0606369C) | 0x060AE488 |
| 4 | 0x0606399C | 6 BISYAMON | 0x11B, 0x10E | BISYAMON.PBC (0x06063A6C) | 0x060AE5D4 |
| 6 | 0x0606379C | 5 NYORAI | 0x11C | NYORAI.PBC + opens KAZUMA2.PBC (0x06063870) | 0x060AE498 |

On the next frame, the request 0x060F4CA8 loads the picture. It is decompressed to VDP1 0x25C6A740 (chars 0x480.., list at 0x060D806C / 0x060D81A8 / 0x060D833C), with palette 0x680 (bank 0x68). A fade-out to grey or black (mask 0x48) then dims the stage.

Each frame, the step's duration is checked (`0x06063CB8`; 0xFFFF = the handler advances itself), then the frame counter and the step timer go up. The state handler then runs, and every handler calls `Fade_HoldFlash` first. Sprites are drawn as frame 0 of an animation from the effect tables (0x06063C40), through the shared sprite struct 0x060250AC.

* **State 2, ZIRYU (0x06065DAE):** the dragon's head fades in over 3 x 8 frames (colour-calc levels 3, 2, 1). The body then zooms in over 0x50 frames (scale = (t/80)², drawn every other frame), followed by the body and the coil. In the strike phase (0x5A frames), lightning hits 4 places and a white flash comes every 32 frames (fade 0x1D0, 0xA0000). Sounds are 0x109, 0x10A and 0x117 (every 15 frames). The end is white with a fade back (speed 0x40000), about 322 frames from the X press.
* **State 6, BISYAMON (0x06066252):** the god descends at the camera position of the cast, with 20 flame particles (up to 4 new ones a frame, tables 0x060AE5E0.. / 0x060AE616..). Up to 3 mirrored afterimage pairs then move out, followed by a rotating flash that zooms in and whites out once it is taller than 9.0. Sounds are 0x10E and 0x10F.
* **State 7, burst (0x06063CFC / 0x06063DE4):** copies of Sho's current frame fly out in 8 directions, in 3 rings x 4 colour levels, coming back by 10 a frame. When the last one is in, the screen goes white and fades in (0x109). It waits for the fade, then fades back.
* **State 5, NYORAI (0x0606429C):** this is the 9-1 boss finish. Step 0 is 0x0605F0E6: the six collected orbs pulse for 0x81 frames, then fly into Sho one by one, each tinting him (sound 0x106). After the last one, NYORAIBG.CHR is loaded to VDP2 and NBG2's page is filled (0x0606F8D8). The rest of the scene:
  * new layer priorities (0x06064F4E), with colours 0x2C0-0x2FF cycling every frame (0x06064C78)
  * the statue grows (sound 0x22C), its face fades in, and the halo pulses
  * 14 spirits gather and then orbit in two 3D rings (0x06065158; tilt angles from the tables 0x060AE4E2..0x060AE53E)
  * the NBG2 line-scroll wave grows with the rumble (sounds 0x22E/0x22F/0x230/0x231/0x232)
  * colour flashes, then a flicker through the colour table 0x060AE544
  * the end (0x234) fades back with speed 0x30000

## Magic_Apply (0x06045D1E), once the effect is over

| kind | effect |
|---|---|
| 0 | shield: `MAGIC = (MAGIC & 0xFF0F) \| 0x4000`, 6 points, char 0x480 from SHOU.CHR (0x06044D08(0xAB, 0x480, 0x66000)). Player_TakeDamage (0x06045BEC) absorbs hits with it. Afterimages are drawn (0x06044280). |
| 1 | `Enemies_MagicDamage(8, 1)`: on-screen enemies without +0x12 & 0x8000 |
| 2 | `Enemies_MagicDamage(8, 0)`: every on-screen enemy. Uses the scroll (`0x060252BE = 0`) and re-arms the saved shield / kind 4 (MAGIC = saved & 0xDFFF, graphics reloaded by 0x06045E86). |
| 3 | HP = HP max |
| 4 | `MAGIC = (MAGIC & 0xFF0F) \| 0x4040`, 6 points, graphics 0x06046968. Sword power 4 (0x06045BA6), run special (B+C while running, 2 points). Each attack's anim event 2 spawns a sword flash in 0x060EA32C (0x06046782 → 0x06046858; updated by 0x060465B8 after Player_Tick; grows 0xC0 frames, hits while its colour level is ≤ 2, sound 0x119). |
| 6 | the first object of type 0x1F (9-1 boss) gets HP 0x8001 and the hit flag |

**Enemies_MagicDamage (0x06049768)** skips three kinds of enemy: free slots, those with +0x12 & 0x4000 (invulnerable) and those already hit (+2 & 8). It damages an enemy only if it is within 192 px horizontally and 160 px vertically of the screen centre: +0x16 -= dmg and +2 |= 8. The enemy's own handler then plays the hurt or death sequence.

## Player sub-routines (player_stubs.c, formerly stubs)

| address | name / role |
|---|---|
| A.BIN 0x0600A604 | `__modsi3` (r1 % r0, signed; % 0 = 0). Callers fixed: the idle fidget (idle counter % 0x060EA444, player.c), the soldier blink (frame % 4, enemy.c). |
| A.BIN 0x0600CEF4 | `__umodsi3`: the next fidget period `0x20 + 0x60 * ((rand >> 8) % 5)`. Player_Init now seeds 0x060EA444 as 0x06044FD8 does. |
| 0x06040F80 | debug free move (cheat key + RT+Z) |
| 0x060410F8 | on hit: clears +4 & 0x4000 and +2 & 0x1000; cry 0x11D (hurt) / 0x11E (killed) |
| 0x060424A6 | posture-4 (hanging) selector: hit/death, knock-off, ninjutsu 0x4A, guard, shuriken 0x19, slashes 0x18/0x48/0x49, let go (down+jump), climb out (up+jump), hand-over-hand 0x11, fidgets 0x41/0x42 |
| 0x06042B90 | command buffer 0x060EA432 (up, up+side, side → ACT_COMMAND_Q 0x25) |
| 0x06042C88 | 8-shuriken fan (table 0x060A8A2C, sound 0x101, costs 8) |
| 0x06042E24 | damaging terrain (+0x10 & 0x20) → Player_TakeDamage(1) when not invulnerable |
| 0x06043A44 | action 0x1C (third combo slash) step forward / recover |
| 0x06044280 | 6 afterimages from the position ring (shield armed or run special, every other frame) |
| 0x0604592E / 0x060459A0 | the sword box (kind 2) touching a wall tile (flags & 0xC) at its bottom edge spawns a spark (effect 5) and plays 0x105. It also sets +4 \|= 0x20, which ends the slash. |

## Not ported / approximations

* 0x06008154 (sequence slot volume) and 0x060084BE (stop slot n) have no slot table on PC. The volume calls are logged. Stops are done by id: slot 0 = the stage tune 0x060F4D32, then 0x231 / 0x232.
* The debug ninjutsu select (pad 2 cheat, 0x0606B94C) is not ported.
* Of 0x06044E18 (Player_InitForStage), only the magic part is ported (`Ninjutsu_PlayerStageInit`: ring, sub-objects, magic graphics).
