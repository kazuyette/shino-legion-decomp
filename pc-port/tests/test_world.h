/* Synthetic one-page world for logic tests (no game data needed): a solid
 * floor whose top is at world Y = 1024+192, player standing at X = 1024+160. */
#include "ram.h"
#include "collision.h"
#include "player.h"
static void Test_BuildFlatWorld(void)
{
    static const uint16_t shape[16] = {0,0xF,5,0xA,0xC,3,1,2,8,4,9,6,0xB,7,0xE,0xD};
    static const uint16_t pix[4] = {4, 8, 1, 2};
    static const uint16_t types[8] = {0x1F,0x12,0x2,0x10,0x2F,0xF,0,0};
    static const uint16_t push0[8] = {2,1,8,4,6,0xA,9,5}, push1[8] = {8,4,2,1,6,0xA,9,5};
    for (int i = 0; i < 16; i++) W16(0x060A8AE4u + 2u * i, shape[i]);
    for (int i = 0; i < 4; i++)  W16(0x060A8B04u + 2u * i, pix[i]);
    for (int i = 0; i < 8; i++)  { W16(0x060CA700u + 2u * i, types[i]); W16(0x060A8A64u + 2u * i, push0[i]); W16(0x060A8A74u + 2u * i, push1[i]); }
    const uint32_t lay = 0x00280000u, page = 0x00280100u, attr = 0x00281000u;
    Ram_Ptr(lay)[0] = 1; Ram_Ptr(lay)[1] = 1; Ram_Ptr(lay)[2] = 0;
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++) W16(page + (uint32_t)(ty * 32 + tx) * 2, ty >= 12 ? 2 : 0);
    W16(attr + 2, 0x0001);                                    /* tile 1: full solid, type 0 */
    W32(LAYER_DESC + 0x10, 0x04000000u); W32(LAYER_DESC + 0x14, 0x04000000u);
    W32(LAYER_DESC + 0x1C, page); W32(LAYER_DESC + 0x20, lay); W32(LAYER_DESC + 0x28, attr);
    W16(G_COLL_LAYER, 0);
    for (int i = 0; i < 4; i++) W16(0x060EA618u + 0x10 + 2u * i, 0x7FFF);  /* no screen clamp */
    PW32(P_X, (1024u + 160u) << 16);
    PW32(P_Y, (1024u + 192u) << 16);
}
