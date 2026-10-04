#ifndef CONFIG_H
#define CONFIG_H
/* PC port settings: shinobi.ini next to the executable (not in the Saturn game).
 *
 * The parser is plain C (no SDL): key bindings are kept as names (SDL scancode names for
 * the keyboard, SDL GameController button/axis names for pads) and resolved by input.c.
 * Built with -DCONFIG_NO_SDL (or without SDL headers) only the SDL helpers
 * (Config_LoadOrCreate, Config_BaseDir) are left out, so tests/test_config.c runs anywhere.
 *
 * Format: "[section]" headers, "key = value" lines, ';' or '#' comments (whole line, or
 * after whitespace in a value). Values may be "quoted" (needed for names containing ; # ,).
 * Binding values are comma-separated lists of up to CFG_MAX_BINDS names; "none" or empty =
 * unbound. Unknown keys/sections and bad values only print a warning (default kept). */

#define CFG_NBUTTONS   13      /* Saturn pad: A B C X Y Z L R Start Up Down Left Right */
#define CFG_MAX_BINDS  4
#define CFG_NAME_LEN   32

enum {
    CFG_BTN_A, CFG_BTN_B, CFG_BTN_C, CFG_BTN_X, CFG_BTN_Y, CFG_BTN_Z, CFG_BTN_L, CFG_BTN_R,
    CFG_BTN_START, CFG_BTN_UP, CFG_BTN_DOWN, CFG_BTN_LEFT, CFG_BTN_RIGHT
};

typedef struct {
    int  n;                                     /* 0 = unbound */
    char name[CFG_MAX_BINDS][CFG_NAME_LEN];
} CfgBinding;

typedef struct {
    /* [video] */
    int scale;            /* window = scale x 320x224, 1..6 */
    int fullscreen;       /* desktop fullscreen at startup */
    int integer_scaling;  /* whole zoom factors only */
    int vsync;
    int bilinear;         /* smooth upscaling */
    int aspect43;         /* 0 = square pixels (320x224), 1 = 4:3 like a TV */
    int crt;              /* CRT look (crt.c): 0 off, 1 scanlines, 2 crt (F10 cycles) */
    int crt_strength;     /* 0..100 */
    int crt_mask;         /* crt mode phosphor mask: 0 none, 1 aperture grille, 2 slot */
    int crt_curvature;    /* crt mode screen curvature 0..100 */
    char renderer[16];    /* SDL render driver (SDL_HINT_RENDER_DRIVER), "" = auto */
    /* [audio] 0..100 */
    int master_volume, music_volume, sfx_volume;
    /* [misc] */
    int screenshot_png;   /* 1 = PNG, 0 = BMP */
    /* [keyboard] (port 1) / [controller] (both pads) */
    CfgBinding keys[CFG_NBUTTONS];
    CfgBinding pad[CFG_NBUTTONS];
    int stick_as_dpad;    /* left stick drives the D-pad */
    int deadzone;         /* % of the stick travel, 5..95 */
} Config;

extern Config g_config;                         /* filled by Config_LoadOrCreate */
/* g_config, set to the defaults first if nothing initialised it yet. */
const Config *Config_Get(void);
extern const char *const g_cfgButtonNames[CFG_NBUTTONS];   /* "A" ... "Right" */

void        Config_SetDefaults(Config *c);
const char *Config_DefaultText(void);           /* the commented default shinobi.ini */
/* Parses ini text over *c (keys not present keep their value). Returns the number of
 * warnings; each one is printed as "[config] <source>:<line>: ..." when source != NULL. */
int         Config_ParseText(Config *c, const char *text, const char *source);
/* 1 = loaded, 0 = file missing (c untouched), -1 = read error. */
int         Config_LoadFile(Config *c, const char *path);
int         Config_WriteDefault(const char *path);   /* 1 = ok */

#if !defined(CONFIG_NO_SDL) && defined(__has_include)
#  if !__has_include(<SDL2/SDL.h>)
#    define CONFIG_NO_SDL 1
#  endif
#endif
#ifndef CONFIG_NO_SDL
/* Directory of the executable (SDL_GetBasePath, ends with a separator), "" if unknown. */
const char *Config_BaseDir(void);
/* Loads <path> (or <exe dir>/shinobi.ini when path is NULL) into g_config; creates the
 * commented default file if it is missing. */
void        Config_LoadOrCreate(const char *path);
#endif

#endif
