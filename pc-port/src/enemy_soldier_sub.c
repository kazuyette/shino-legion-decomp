/* Soldier (type 0x0001) subtypes 2..12, SHINOBI jump table 0x0608FC48:
 *  2 → 0x0608FE38   3, 11 → 0x060904C0   4 → 0x06090D72   5 → 0x0608E3E6
 *  6 → 0x0608E8C4   7 → 0x0609126C   8 → 0x06091DE8   9 → 0x060921CC
 *  10 → 0x060927EC  12 → 0x0608F58C (rides a floating platform)
 * Subtypes are implemented in enemy_soldier_a.c / _b.c / _c.c. */
#include "enemy_int.h"
#include "enemy_soldier_sub.h"

void Soldier_Subtype(uint16_t sub)
{
    switch (sub) {
    case 2:  Soldier_Sub02(); break;
    case 3: case 11: Soldier_Sub03(); break;
    case 4:  Soldier_Sub04(); break;
    case 5:  Soldier_Sub05(); break;                    /* enemy_soldier_sub_d.c */
    case 6:  Soldier_Sub06(); break;
    case 7:  Soldier_Sub07(); break;
    case 8:  Soldier_Sub08(); break;
    case 9:  Soldier_Sub09(); break;
    case 10: Soldier_Sub0A(); break;
    case 12: Soldier_Sub0C(); break;
    default: Enemy_Once("soldier subtype", sub); break;
    }
}
