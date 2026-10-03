#ifndef DIRECTOR_H
#define DIRECTOR_H
/* DIRECTOR.PPB (front-end overlay at 0x06040000), see docs/director.md.
 * Public entry called by the main loop when DIRECTOR is the resident overlay. */
void Director_Entry(void);      /* 0x06040000 */
#endif
