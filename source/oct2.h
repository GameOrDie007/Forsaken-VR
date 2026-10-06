#ifndef OCT2_INCLUDED
#define OCT2_INCLUDED

#include "main.h"

#define MAX_SHORT_LEVEL_NAME 32
extern char ShortLevelNames[MAXLEVELS][MAX_SHORT_LEVEL_NAME];

/* Rectangle in the 2D pass's logical space that HUD elements anchor to. The
   whole screen in flat mode; an inset box under -vr, because the screen edges
   are ~55 degrees off-axis in a headset and anything anchored there cannot be
   read. Call hud_box_update() before using them. Defined in oct2.c. */
extern int hud_box_x0, hud_box_y0, hud_box_x1, hud_box_y1;
void hud_box_update( void );

extern int16_t NewLevelNum;

/* comfort vignette: see oct2.c */
extern int   vr_vignette;
extern float vr_vignette_force;
void  vr_vignette_update( void );

#endif // OCT2_INCLUDED
