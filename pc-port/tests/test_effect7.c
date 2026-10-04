/* Effect type 7 (SHINOBI 0x06062578): the Ikazuchi shield flash. Spawned on Sho by
 * Player_TakeDamage (0x06045BEC) when the shield (magic state 0x060252BA: 0x4000 set, kind
 * nibble 0x00F0 clear) absorbs a hit; sprite list 0x060AD2C2 (4 frames, char 0 + 0x480,
 * offset (-29,-72)), drifts 3 px per frame towards Sho's facing, colour bank 0x40, colour
 * calculation 0x7F, drawn on odd frames only, freed when the list ends.
 * S=$(cat tests/SOURCES.txt); gcc -std=c11 -Isrc -Itests tests/test_effect7.c $S -lm && ./a.out ../extracted */
#include <stdio.h>
#include <assert.h>
#include "ram.h"
#include "render.h"
#include "sprite.h"
#include "fx.h"
#include "enemy.h"
#include "input.h"
#include "player.h"

static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
extern RenderSprite g_lastSprite; extern int g_spriteCount;

#define PL      0x060EA2B4u
#define MAGIC   0x060252BAu
#define SHIELD  0x060252BCu
#define SPR     0x060250ACu
#define FRAMES  0x060143EAu
#define LIST    0x060AD2C2u

static int run(int facing_right, int frozen)
{
    Effects_Clear();
    W32(0x060252A0u, 0x01000000u); W32(0x060252A4u, 0x01000000u);         /* camera (256,256) */
    PW32(8, 0x01400000u); PW32(0xC, 0x01600000u);                          /* Sho (320,352) */
    PW16(2, facing_right ? 0x8000 : 0);
    PW16(4, 0); PW16(0x16, 6);
    W16(MAGIC, 0x4000); W16(SHIELD, 2);                                    /* Ikazuchi, 2 hits left */
    W32(0x060EA41Cu, 0x060FFF00u); W16(0x060FFF00u, 0);
    W16(0x0602529Eu, frozen ? 0x8000 : 0);
    Player_TakeDamage(1);
    assert(RS16(PL + 0x16) == 6 && RS16(SHIELD) == 1 && R16(MAGIC) == 0x4000);     /* absorbed */
    assert(R16(0x060FFF00u) == 0x60);                                              /* invulnerable */
    assert(R16(EFFECTS) == 7 && R16(EFFECTS + 2) == ((facing_right ? 0x8000 : 0) | 1));
    assert(RS32(EFFECTS + 8) == 0x01400000 && RS32(EFFECTS + 0xC) == 0x01600000);

    /* expected lifetime from the list: each record {char, ox, oy, ticks, cc} lasts ticks + 1 */
    int life = 0;
    for (uint32_t r = LIST; R16(r) != 0xFFFF; r += 10) life += R16(r + 6) + 1;
    int f, drawn = 0;
    for (f = 0; f < 100 && R16(EFFECTS) == 7; f++) {
        W16(FRAMES, (uint32_t)f);
        int before = g_spriteCount;
        int32_t x = RS32(EFFECTS + 8);
        Effects_Update();
        if (R16(EFFECTS) != 7) break;
        int32_t dx = RS32(EFFECTS + 8) - x;
        assert(dx == (frozen ? 0 : facing_right ? 0x30000 : -0x30000));
        assert(R16(SPR) == 0x480 && RS16(SPR + 2) == (RS32(EFFECTS + 8) - 0x01000000) >> 16 && RS16(SPR + 4) == 96);
        assert(RS16(SPR + 6) == -29 && RS16(SPR + 8) == -72);
        assert(R16(SPR + 0x0A) == 0x40 && R16(SPR + 0x10) == ((facing_right ? 0x8000 : 0) | 0x200) && R32(SPR + 0x18) == 0x7F);
        int d = g_spriteCount - before;
        assert(d == (f & 1));                                              /* odd frames only */
        drawn += d;
    }
    printf("  facing %s%s: %d frames (list: %d), %d drawn, x %d -> %d\n", facing_right ? "right" : "left",
           frozen ? ", frozen" : "", f, life, drawn, 320, (int)(RS32(EFFECTS + 8) >> 16));
    if (frozen) assert(f == 100);                                          /* the list does not advance */
    else assert(f == life && R16(EFFECTS) == 0xFFFF);
    return f;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%s/A.BIN", dir); assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%s/SHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    Char_Register(0x480, 64, 80, 0, 0x60000);
    printf("effect 7 (Ikazuchi shield flash):\n");
    run(1, 0);
    run(0, 0);
    run(1, 1);
    /* shield used up: the last absorbed hit clears the magic */
    Effects_Clear();
    W16(MAGIC, 0x4000); W16(SHIELD, 1); PW16(4, 0); PW16(0x16, 6); W16(0x0602529Eu, 0);
    Player_TakeDamage(1);
    assert(R16(EFFECTS) == 7 && R16(MAGIC) == 0 && RS16(SHIELD) == 0 && RS16(PL + 0x16) == 6);
    /* no shield: no effect, HP lost */
    Effects_Clear();
    Player_TakeDamage(1);
    assert(R16(EFFECTS) == 0xFFFF && (R16(PL + 2) & 8));
    printf("all effect 7 tests OK\n");
    return 0;
}
