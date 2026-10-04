#ifndef CAMERA_H
#define CAMERA_H
#include <stdint.h>
/* Camera follow, ported from SHINOBI 0x060446FC.
 *  0x060252A0/A4  camera world X/Y (16.16) used to place sprites and layers
 *  0x060EA550/554 player's target screen X/Y (starts 92,180; lags/leads)
 *  0x060EA558     horizontal look-ahead speed
 *  0x060EA564/568 target screen X range, 0x060EA55C/560 target screen Y range
 *  0x060EA56C/570 look up/down limits, 0x060EA638 camera lock flags */
#define CAM_X 0x060252A0u
#define CAM_Y 0x060252A4u
void Camera_Update(void);   /* 0x060446FC */
void Scroll_Update(void);   /* 0x060443D8: camera + world bounds + layer positions */
void Zoom_Update(void);     /* 0x0604455A: sprite / NBG0-1 zoom (stays 1.0 in normal play) */
void Layer_FromCamera(int32_t camX, int32_t camY, uint32_t desc);   /* 0x060470A4 */
#endif
