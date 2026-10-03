#include <stdio.h>
#include <assert.h>
#include "pad.h"
#include "input.h"
#include "player.h"
static InputPort fake[2];
const InputPort *Input_GetPort(int p){ return &fake[p]; }
static void frame(uint16_t held){ fake[0].connected=1; fake[0].raw=(uint16_t)~held; Pad_Update(2); }
int main(void){
  frame(0); frame(PAD_A);
  assert(g_padPorts[0].held==PAD_A && g_padPorts[0].pressed==PAD_A);
  frame(PAD_A); assert(g_padPorts[0].pressed==0 && g_padPorts[0].pressed_prev==PAD_A);
  assert(g_padPorts[1].held==0);                         /* port 1 disconnected */
  frame(PAD_A|PAD_B|PAD_C|PAD_START); assert(Sys_CheckSoftReset());
  frame(PAD_A|PAD_B|PAD_C|PAD_START); assert(!Sys_CheckSoftReset()); /* edge only */
  frame(PAD_RIGHT); assert(Game_LatchPad1(0)==0 && g_gameHeld==PAD_RIGHT);
  Game_LatchPad1(PFLAG4_MIRROR_LR); assert(g_gameHeld==PAD_LEFT);
  frame(0); Game_LatchPad1(PFLAG4_MIRROR_LR); assert(g_gameHeld==0);
  Game_LatchPad1(PFLAG4_INPUT_OFF); assert(g_gameHeld==0);
  g_buttonConfig=4; Game_LatchPad1(0); assert(g_actionBtn[0]==PAD_C && g_actionBtn[2]==PAD_A);
  g_demoFlags=0x8000; assert(Game_LatchPad1(0)==2 && g_actionBtn[0]==PAD_A);
  printf("all pad tests OK\n"); return 0;
}
