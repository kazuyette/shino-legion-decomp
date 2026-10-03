#include "player.h"
#include <stddef.h>

Player   g_player;
uint16_t g_idleFrames;

/* Compile-time check that the struct matches the original offsets. */
_Static_assert(offsetof(Player, action)      == 0x06, "Player.action");
_Static_assert(offsetof(Player, hp)          == 0x16, "Player.hp");
_Static_assert(offsetof(Player, prevAction)  == 0x22, "Player.prevAction");
_Static_assert(offsetof(Player, x)           == 0x08, "Player.x");
_Static_assert(offsetof(Player, y)           == 0x0C, "Player.y");
_Static_assert(offsetof(Player, velX)        == 0x30, "Player.velX");
_Static_assert(offsetof(Player, velY)        == 0x34, "Player.velY");
_Static_assert(offsetof(Player, flags58)     == 0x58, "Player.flags58");
