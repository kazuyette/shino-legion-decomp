/* Stage 1-1 enemy types 0x0003 (leaping ninja) and 0x0004 (guard ninja) on real data. */
#include <stdio.h>
#include <assert.h>
#include "pad.h"
#include "input.h"
#include "player.h"
#include "ram.h"
#include "render.h"
#include "stage.h"
#include "camera.h"
#include "sprite.h"
#include "objects.h"
#include "enemy.h"
#include "fx.h"
static InputPort fake[2];
const InputPort *Input_GetPort(int p) { return &fake[p]; }
static void tick(uint16_t held)
{
    fake[0].connected = 1; fake[0].raw = (uint16_t)~held;
    W16(0x060143EAu, R16(0x060143EAu) + 1u); W16(0x060F4CCCu, R16(0x060F4CCCu) + 1u);
    Pad_Update(2); Scroll_Update(); Game_LatchPad1(P16(P_FLAGS4)); Player_Tick(); Obj_UpdateEnemies(); Obj_UpdateAll(); Projectiles_Update(); Effects_Update(); Items_Update();
}
static void place_sho(uint32_t e, int32_t dx, int32_t dy)
{
    PW16(P_HP, 16);
    PW32(P_X, (uint32_t)(RS32(e + 8) + dx));
    PW32(P_Y, (uint32_t)(RS32(e + 0xC) + dy));
    W32(0x060252A0u, (uint32_t)(P32(P_X) - RS32(0x060EA550u)));
    W32(0x060252A4u, (uint32_t)(P32(P_Y) - RS32(0x060EA554u)));
}
static uint32_t first_of(uint16_t type)
{
    uint32_t e = 0; int32_t bx = 0x7FFFFFFF;
    for (int s = 0; s < OBJ_SLOTS; s++) {
        uint32_t o = OBJ_TABLE + (uint32_t)s * OBJ_SIZE;
        if (R16(o) == type && RS32(o + 8) < bx) { bx = RS32(o + 8); e = o; }
    }
    return e;
}
static void show_acts(const char *who, const int *acts)
{
    printf("%s actions seen:", who);
    for (int i = 0; i < 0x20; i++) if (acts[i]) printf(" %d(%d)", i, acts[i]);
    printf("\n");
}
/* fight e with the sword (B) or shurikens (A) until it is gone; returns frames or -1 */
static int fight(uint32_t e, uint16_t type, int shuriken, int maxf, int *acts, int *hurt)
{
    int16_t hp0 = (int16_t)P16(P_HP);
    for (int i = 0; i < maxf; i++) {
        int32_t dx = RS32(e + 8) - (int32_t)P32(P_X);
        uint16_t held = 0;
        if (shuriken) {
            if (dx > 0) { if (!(P16(2) & 0x8000) || dx > 0x00C00000) held = PAD_RIGHT; }
            else if (dx < 0) { if ((P16(2) & 0x8000) == 0 || dx < -0x00C00000) held = PAD_LEFT; }
            if (!(i & 8)) held = (dx > 0x00C00000 || dx < -0x00C00000) ? held : (uint16_t)((i & 16) ? PAD_A : 0);
        } else {
            if (dx > 0x280000) held = PAD_RIGHT; else if (dx < -0x280000) held = PAD_LEFT;
            else held = (i & 8) ? PAD_B : 0;
        }
        tick(held);
        if (R16(e) != type) { *hurt = hp0 - (int16_t)P16(P_HP); return i; }
        acts[R16(e + 6) & 0x1F]++;
        if ((int16_t)P16(P_HP) < 4) PW16(P_HP, 16), hp0 += 12;          /* keep Sho alive */
    }
    *hurt = hp0 - (int16_t)P16(P_HP);
    return -1;
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../extracted/";
    char p[512];
    snprintf(p, sizeof p, "%sA.BIN", dir);           assert(Ram_LoadImage(p, 0x06004000u));
    snprintf(p, sizeof p, "%sSHINOBI.dec.bin", dir); assert(Ram_LoadImage(p, 0x06040000u));
    snprintf(p, sizeof p, "%sSHOU.CHR", dir);        assert(Ram_LoadImage(p, 0x00200000u));
    Player_Init();
    assert(Stage_Load(dir, 12));
    for (int k = 0; k < 0xC0; k++) {                 /* PC test: no crates/boxes/statues/doors in the way */
        uint32_t ob = 0x060EC65Cu + (uint32_t)k * 0x78;
        if (R16(ob) >= 0x1000 && R16(ob) <= 0x1003) W16(ob, 0xFFFF);
    }
    assert(Enemy_FindSlot(3) == 3 && Enemy_FindSlot(4) == 2);
    for (int t = 3; t <= 4; t++) {
        int s = Enemy_FindSlot((int16_t)t);
        uint16_t cb = R16(0x060A9064u + 2u * (uint32_t)s), bank = R16(0x060A9084u + 2u * (uint32_t)s);
        int pal = 0;
        for (int k = 0; k < 32; k++) pal |= R16(SHADOW_CRAM + 2u * (uint32_t)(bank * 16 + k));
        printf("type %d: slot %d chars %04X (vram %04X) colour bank %d, palette %s\n", t, s, cb,
               R16(CHAR_TABLE + cb * 10u), bank, pal ? "loaded" : "EMPTY");
        assert(R16(CHAR_TABLE + cb * 10u) != 0 && pal);
    }
    W16(0x060252DCu, 1);                            /* no drop-in (0x0606E440 not run here) */
    Stage_SpawnPlayer();
    /* ---- type 3 */
    int n3 = 0;
    for (int s = 0; s < OBJ_SLOTS; s++) n3 += R16(OBJ_TABLE + (uint32_t)s * OBJ_SIZE) == 3;
    uint32_t e = first_of(3);
    assert(e && n3 == 6);
    printf("%d type-3 ninjas, first at %.0f,%.0f item %04X\n", n3, RS32(e + 8) / 65536.0, RS32(e + 0xC) / 65536.0, R16(e + 0x18));
    place_sho(e, -0x00A00000, -0x00100000);
    int acts[0x20] = {0}; int16_t hp0 = (int16_t)P16(P_HP);
    for (int i = 0; i < 360; i++) { tick(0); if (R16(e) == 3) acts[R16(e + 6) & 0x1F]++; }
    show_acts("ninja (Sho idle 6 s)", acts);
    printf("  Sho HP %d -> %d, ninja HP %d, move %d\n", hp0, (int16_t)P16(P_HP), RS16(e + 0x16), RS16(e + 0x4E));
    assert(R16(e) == 3 && !(R16(e + 2) & 1));
    int hurt = 0, sc = RS32(SCORE);
    int f = fight(e, 3, 0, 1200, acts, &hurt);
    show_acts("ninja (sword fight)", acts);
    printf("  sword: killed after %d frames, score +%d, Sho lost %d HP\n", f, RS32(SCORE) - sc, hurt);
    assert(f >= 0 && RS32(SCORE) - sc == 200);
    e = first_of(3);
    assert(e);
    place_sho(e, -0x00A00000, -0x00100000);
    for (int i = 0; i < 0x20; i++) acts[i] = 0;
    sc = RS32(SCORE);
    f = fight(e, 3, 1, 1500, acts, &hurt);
    show_acts("ninja #2 (shurikens)", acts);
    printf("  shurikens: killed after %d frames, score +%d, Sho lost %d HP\n", f, RS32(SCORE) - sc, hurt);
    assert(f >= 0);
    /* ---- type 4: spawned like the trap-door object 0x1003 does (x + 40 px, flags 1) */
    int32_t x = 12336 * 0x10000 + 0x04000000 + 0x00280000, y = 462 * 0x10000 + 0x04000000;
    int slot = Obj_Register(4, x, y, 1, 0xFFFF);
    assert(slot >= 0);
    uint32_t g = OBJ_TABLE + (uint32_t)slot * OBJ_SIZE;
    place_sho(g, -0x00B00000, -0x00080000);
    for (int i = 0; i < 0x20; i++) acts[i] = 0;
    hp0 = (int16_t)P16(P_HP);
    for (int i = 0; i < 360; i++) { tick(0); if (R16(g) == 4) acts[R16(g + 6) & 0x1F]++; }
    show_acts("guard sub1 (Sho idle 6 s)", acts);
    printf("  at %.0f,%.0f home %d, Sho HP %d -> %d\n", RS32(g + 8) / 65536.0, RS32(g + 0xC) / 65536.0,
           RS16(g + 0x48), hp0, (int16_t)P16(P_HP));
    assert(R16(g) == 4 && !(R16(g + 2) & 1) && RS16(g + 0x16) == 2);
    sc = RS32(SCORE);
    f = fight(g, 4, 0, 1500, acts, &hurt);
    show_acts("guard sub1 (sword fight)", acts);
    printf("  sword: killed after %d frames, score +%d, Sho lost %d HP\n", f, RS32(SCORE) - sc, hurt);
    assert(f >= 0 && RS32(SCORE) - sc == 200);
    /* subtype 0 (stage-6 kind) on the same floor */
    slot = Obj_Register(4, x, y, 0, 0xFFFF);
    g = OBJ_TABLE + (uint32_t)slot * OBJ_SIZE;
    place_sho(g, -0x00B00000, -0x00080000);
    for (int i = 0; i < 0x20; i++) acts[i] = 0;
    sc = RS32(SCORE);
    f = fight(g, 4, 1, 1500, acts, &hurt);
    show_acts("guard sub0 (shurikens)", acts);
    printf("  shurikens: killed after %d frames, score +%d, Sho lost %d HP\n", f, RS32(SCORE) - sc, hurt);
    /* subtype 2 */
    slot = Obj_Register(4, x, y, 2, 0xFFFF);
    g = OBJ_TABLE + (uint32_t)slot * OBJ_SIZE;
    place_sho(g, -0x00B00000, -0x00080000);
    for (int i = 0; i < 0x20; i++) acts[i] = 0;
    sc = RS32(SCORE);
    f = fight(g, 4, 0, 1500, acts, &hurt);
    show_acts("guard sub2 (sword)", acts);
    printf("  sword: killed after %d frames, score +%d, Sho lost %d HP\n", f, RS32(SCORE) - sc, hurt);
    printf("type 3/4 tests done\n");
    return 0;
}
