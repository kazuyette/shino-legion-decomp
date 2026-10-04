/* PC port settings (shinobi.ini), see config.h. */
#include "config.h"
#include "u8path.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_NO_SDL
#include <SDL2/SDL.h>
#endif

Config g_config;

const char *const g_cfgButtonNames[CFG_NBUTTONS] = {
    "A", "B", "C", "X", "Y", "Z", "L", "R", "Start", "Up", "Down", "Left", "Right"
};

/* Default bindings, one string per Saturn button (same syntax as the file). */
static const char *const kDefKeys[CFG_NBUTTONS] = {
    "Z", "X", "C", "A", "S", "D", "Q", "W", "Return, Keypad Enter", "Up", "Down", "Left", "Right"
};
static const char *const kDefPad[CFG_NBUTTONS] = {
    "b", "x", "a", "y", "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger",
    "start", "dpup", "dpdown", "dpleft", "dpright"
};

static const char kDefaultText[] =
"; Shinobi Legions PC port - settings\n"
"; This file is created with the default values when it is missing: delete it to get the\n"
"; defaults back. Lines are \"key = value\"; ';' or '#' starts a comment. Unknown keys and\n"
"; bad values are reported in the console and ignored (the default is kept).\n"
"\n"
"[video]\n"
"; Window size = scale x 320x224 (1..6). The window can also be resized freely.\n"
"scale = 3\n"
"; 1 = start in fullscreen (desktop resolution). Alt+Enter or F11 toggles it in game.\n"
"fullscreen = 0\n"
"; 1 = only whole zoom factors (every Saturn pixel the same size, black borders).\n"
"integer_scaling = 0\n"
"; 1 = sync to the monitor refresh (no tearing).\n"
"vsync = 1\n"
"; 1 = smooth (bilinear) upscaling, 0 = sharp pixels.\n"
"bilinear = 0\n"
"; native = square pixels (320x224), 4:3 = stretched to 4:3 like on a TV.\n"
"aspect = native\n"
"; Old TV look: off, scanlines (dark lines between the 224 lines), crt (scanlines + phosphor\n"
"; mask + glow). F10 cycles off / scanlines / crt in game (not saved here).\n"
"crt = off\n"
"; Strength of the scanlines / mask, 0..100.\n"
"crt_strength = 70\n"
"; crt mode phosphor mask: aperture (grille, Trinitron-like), slot (best at 1440p and up), none.\n"
"crt_mask = aperture\n"
"; crt mode screen curvature, 0 (flat) .. 100.\n"
"crt_curvature = 0\n"
"; Graphics backend: auto (SDL's choice: direct3d on Windows), direct3d, direct3d11,\n"
"; direct3d12, opengl, opengles2 or software. The one in use is printed at startup; if it\n"
"; cannot be created the automatic choice is used.\n"
"renderer = auto\n"
"\n"
"[audio]\n"
"; 0..100 (%). Music also covers the movie soundtracks.\n"
"master_volume = 100\n"
"music_volume = 100\n"
"sfx_volume = 100\n"
"\n"
"[misc]\n"
"; F12 saves a 320x224 screenshot in the \"screenshots\" folder next to the game: png or bmp.\n"
"screenshot_format = png\n"
"\n"
"[keyboard]\n"
"; Saturn button = SDL key names (scancodes, US layout positions), comma separated, up to 4.\n"
"; Examples: Z, Space, Return, Left Shift, Right Ctrl, Left Alt, Tab, Keypad 8, F5.\n"
"; Put a name containing ; # or , in quotes (\";\"). none = unbound.\n"
"; Reserved: F1 (debug menu), F10 (CRT filter), F11 / Alt+Enter (fullscreen), F12 (screenshot).\n"
"A = Z\n"
"B = X\n"
"C = C\n"
"X = A\n"
"Y = S\n"
"Z = D\n"
"L = Q\n"
"R = W\n"
"Start = Return, Keypad Enter\n"
"Up = Up\n"
"Down = Down\n"
"Left = Left\n"
"Right = Right\n"
"\n"
"[controller]\n"
"; Applies to controllers 1 and 2 (Xbox names; PlayStation: a = Cross, b = Circle,\n"
"; x = Square, y = Triangle). Buttons: a b x y start leftshoulder rightshoulder\n"
"; leftstick rightstick dpup dpdown dpleft dpright misc1 paddle1..paddle4 touchpad guide;\n"
"; triggers: lefttrigger righttrigger; stick directions: +leftx -leftx +lefty -lefty\n"
"; +rightx -rightx +righty -righty (- = left/up). back opens the debug menu (avoid it).\n"
"A = b\n"
"B = x\n"
"C = a\n"
"X = y\n"
"Y = leftshoulder\n"
"Z = rightshoulder\n"
"L = lefttrigger\n"
"R = righttrigger\n"
"Start = start\n"
"Up = dpup\n"
"Down = dpdown\n"
"Left = dpleft\n"
"Right = dpright\n"
"; 1 = the left stick also works as the D-pad.\n"
"stick_as_dpad = 1\n"
"; Stick dead zone, in % of its travel (5..95).\n"
"deadzone = 37\n";

const char *Config_DefaultText(void) { return kDefaultText; }

/* ------------------------------------------------------------------ helpers */

static int streqi(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}

/* Removes surrounding quotes (after trim). */
static char *unquote(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') { s[n - 1] = 0; return s + 1; }
    return s;
}

/* Cuts an inline comment: ';' or '#' outside quotes, at the start or after whitespace. */
static void strip_comment(char *v)
{
    int inq = 0;
    for (char *p = v; *p; p++) {
        if (*p == '"') inq = !inq;
        else if (!inq && (*p == ';' || *p == '#') && (p == v || isspace((unsigned char)p[-1]))) { *p = 0; return; }
    }
}

typedef struct { const char *src; int line; int warnings; } Ctx;

static void warn(Ctx *x, const char *fmt, ...)
{
    x->warnings++;
    if (!x->src) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[config] %s:%d: ", x->src, x->line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void set_int(Ctx *x, const char *key, const char *v, int *out, int lo, int hi)
{
    char *end;
    long n = strtol(v, &end, 10);
    if (!*v || *end) { warn(x, "%s: \"%s\" is not a number", key, v); return; }
    if (n < lo || n > hi) {
        warn(x, "%s: %ld out of range %d..%d, clamped", key, n, lo, hi);
        n = n < lo ? lo : hi;
    }
    *out = (int)n;
}

static void set_bool(Ctx *x, const char *key, const char *v, int *out)
{
    if (streqi(v, "1") || streqi(v, "true") || streqi(v, "yes") || streqi(v, "on")) *out = 1;
    else if (streqi(v, "0") || streqi(v, "false") || streqi(v, "no") || streqi(v, "off")) *out = 0;
    else warn(x, "%s: \"%s\" is not 0/1", key, v);
}

/* "name, name, ..." -> binding (v has its comment stripped, not trimmed). */
static void set_binding(Ctx *x, const char *key, char *v, CfgBinding *b)
{
    CfgBinding nb;
    memset(&nb, 0, sizeof nb);
    char *p = v;
    for (;;) {
        char *item = p;
        int inq = 0;
        while (*p && (inq || *p != ',')) { if (*p == '"') inq = !inq; p++; }
        int last = *p == 0;
        *p = 0;
        char *name = unquote(trim(item));
        if (*name && !streqi(name, "none")) {
            if (strlen(name) >= CFG_NAME_LEN) warn(x, "%s: name \"%s\" too long, ignored", key, name);
            else if (nb.n >= CFG_MAX_BINDS) warn(x, "%s: more than %d bindings, \"%s\" ignored", key, CFG_MAX_BINDS, name);
            else strcpy(nb.name[nb.n++], name);
        }
        if (last) break;
        p++;
    }
    *b = nb;
}

static int button_index(const char *key)
{
    for (int i = 0; i < CFG_NBUTTONS; i++)
        if (streqi(key, g_cfgButtonNames[i])) return i;
    return -1;
}

/* ------------------------------------------------------------------- public */

void Config_SetDefaults(Config *c)
{
    memset(c, 0, sizeof *c);
    c->scale = 3;
    c->vsync = 1;
    c->crt_strength = 70;
    c->crt_mask = 1;                     /* aperture */
    c->master_volume = c->music_volume = c->sfx_volume = 100;
    c->screenshot_png = 1;
    c->stick_as_dpad = 1;
    c->deadzone = 37;
    Ctx x = { NULL, 0, 0 };
    char tmp[CFG_MAX_BINDS * (CFG_NAME_LEN + 2)];
    for (int i = 0; i < CFG_NBUTTONS; i++) {
        snprintf(tmp, sizeof tmp, "%s", kDefKeys[i]);
        set_binding(&x, g_cfgButtonNames[i], tmp, &c->keys[i]);
        snprintf(tmp, sizeof tmp, "%s", kDefPad[i]);
        set_binding(&x, g_cfgButtonNames[i], tmp, &c->pad[i]);
    }
}

const Config *Config_Get(void)
{
    if (g_config.scale == 0) Config_SetDefaults(&g_config);
    return &g_config;
}

enum { SEC_NONE, SEC_VIDEO, SEC_AUDIO, SEC_MISC, SEC_KEYBOARD, SEC_CONTROLLER, SEC_UNKNOWN };

int Config_ParseText(Config *c, const char *text, const char *source)
{
    Ctx x = { source, 0, 0 };
    size_t len = strlen(text);
    char *buf = (char *)malloc(len + 1);
    if (!buf) return 1;
    memcpy(buf, text, len + 1);
    char *p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;  /* UTF-8 BOM */
    int sec = SEC_NONE;
    while (p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        x.line++;
        char *s = trim(p);
        p = nl ? nl + 1 : NULL;
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (!e) { warn(&x, "missing ']'"); sec = SEC_UNKNOWN; continue; }
            *e = 0;
            char *name = trim(s + 1);
            if (streqi(name, "video")) sec = SEC_VIDEO;
            else if (streqi(name, "audio")) sec = SEC_AUDIO;
            else if (streqi(name, "misc")) sec = SEC_MISC;
            else if (streqi(name, "keyboard")) sec = SEC_KEYBOARD;
            else if (streqi(name, "controller")) sec = SEC_CONTROLLER;
            else { warn(&x, "unknown section [%s] ignored", name); sec = SEC_UNKNOWN; }
            continue;
        }
        if (sec == SEC_UNKNOWN) continue;      /* already reported */
        char *eq = strchr(s, '=');
        if (!eq) { warn(&x, "expected key = value"); continue; }
        *eq = 0;
        char *key = trim(s), *raw = eq + 1;
        strip_comment(raw);
        if (sec == SEC_NONE) { warn(&x, "%s: outside of a [section], ignored", key); continue; }
        int bi = button_index(key);
        int isBinding = bi >= 0 && (sec == SEC_KEYBOARD || sec == SEC_CONTROLLER);
        char *v = isBinding ? raw : unquote(trim(raw));   /* bindings: split first (set_binding) */
        if (sec == SEC_VIDEO) {
            if (streqi(key, "scale")) set_int(&x, key, v, &c->scale, 1, 6);
            else if (streqi(key, "fullscreen")) set_bool(&x, key, v, &c->fullscreen);
            else if (streqi(key, "integer_scaling") || streqi(key, "integer_scale")) set_bool(&x, key, v, &c->integer_scaling);
            else if (streqi(key, "vsync")) set_bool(&x, key, v, &c->vsync);
            else if (streqi(key, "bilinear")) set_bool(&x, key, v, &c->bilinear);
            else if (streqi(key, "aspect")) {
                if (streqi(v, "native") || streqi(v, "square") || streqi(v, "10:7")) c->aspect43 = 0;
                else if (streqi(v, "4:3") || streqi(v, "tv")) c->aspect43 = 1;
                else warn(&x, "aspect: \"%s\" is not native or 4:3", v);
            } else if (streqi(key, "crt")) {
                if (streqi(v, "off") || streqi(v, "none") || streqi(v, "0")) c->crt = 0;
                else if (streqi(v, "scanlines") || streqi(v, "1")) c->crt = 1;
                else if (streqi(v, "crt") || streqi(v, "2")) c->crt = 2;
                else warn(&x, "crt: \"%s\" is not off, scanlines or crt", v);
            } else if (streqi(key, "crt_strength")) set_int(&x, key, v, &c->crt_strength, 0, 100);
            else if (streqi(key, "crt_mask")) {
                if (streqi(v, "none") || streqi(v, "off")) c->crt_mask = 0;
                else if (streqi(v, "aperture") || streqi(v, "grille")) c->crt_mask = 1;
                else if (streqi(v, "slot")) c->crt_mask = 2;
                else warn(&x, "crt_mask: \"%s\" is not none, aperture or slot", v);
            } else if (streqi(key, "crt_curvature")) set_int(&x, key, v, &c->crt_curvature, 0, 100);
            else if (streqi(key, "renderer")) {
                static const char *const names[] = { "auto", "direct3d", "direct3d11", "direct3d12", "opengl",
                                                     "opengles2", "software", "metal" };
                int k, n = (int)(sizeof names / sizeof names[0]);
                for (k = 0; k < n && !streqi(v, names[k]); k++) ;
                if (k == n) warn(&x, "renderer: \"%s\" is not auto, direct3d, direct3d11, direct3d12, opengl, opengles2 or software", v);
                else snprintf(c->renderer, sizeof c->renderer, "%s", k == 0 ? "" : names[k]);
            }
            else warn(&x, "unknown key %s in [video]", key);
        } else if (sec == SEC_AUDIO) {
            if (streqi(key, "master_volume")) set_int(&x, key, v, &c->master_volume, 0, 100);
            else if (streqi(key, "music_volume")) set_int(&x, key, v, &c->music_volume, 0, 100);
            else if (streqi(key, "sfx_volume")) set_int(&x, key, v, &c->sfx_volume, 0, 100);
            else warn(&x, "unknown key %s in [audio]", key);
        } else if (sec == SEC_MISC) {
            if (streqi(key, "screenshot_format")) {
                if (streqi(v, "png")) c->screenshot_png = 1;
                else if (streqi(v, "bmp")) c->screenshot_png = 0;
                else warn(&x, "screenshot_format: \"%s\" is not png or bmp", v);
            } else warn(&x, "unknown key %s in [misc]", key);
        } else if (sec == SEC_KEYBOARD) {
            if (bi >= 0) set_binding(&x, key, raw, &c->keys[bi]);
            else warn(&x, "unknown Saturn button %s in [keyboard]", key);
        } else if (sec == SEC_CONTROLLER) {
            if (bi >= 0) set_binding(&x, key, raw, &c->pad[bi]);
            else if (streqi(key, "stick_as_dpad")) set_bool(&x, key, v, &c->stick_as_dpad);
            else if (streqi(key, "deadzone")) set_int(&x, key, v, &c->deadzone, 5, 95);
            else warn(&x, "unknown key %s in [controller]", key);
        }
    }
    free(buf);
    return x.warnings;
}

int Config_LoadFile(Config *c, const char *path)
{
    FILE *f = u8_fopen(path, "rb");
    if (!f) return 0;
    char *buf = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n >= 0 && n <= (1L << 20) && fseek(f, 0, SEEK_SET) == 0 && (buf = (char *)malloc((size_t)n + 1)) != NULL) {
        size_t got = fread(buf, 1, (size_t)n, f);
        buf[got] = 0;
        for (size_t i = 0; i < got; i++) if (!buf[i]) buf[i] = ' ';   /* stray NULs */
    }
    fclose(f);
    if (!buf) { fprintf(stderr, "[config] cannot read %s\n", path); return -1; }
    Config_ParseText(c, buf, path);
    free(buf);
    return 1;
}

int Config_WriteDefault(const char *path)
{
    FILE *f = u8_fopen(path, "w");          /* text mode: CRLF on Windows */
    if (!f) return 0;
    int ok = fputs(kDefaultText, f) >= 0;
    if (fclose(f) != 0) ok = 0;
    return ok;
}

/* ---------------------------------------------------------------------- SDL */
#ifndef CONFIG_NO_SDL
const char *Config_BaseDir(void)
{
    static char s_base[1024];
    static int s_done;
    if (!s_done) {
        char *b = SDL_GetBasePath();
        if (b && strlen(b) < sizeof s_base) strcpy(s_base, b);
        if (b) SDL_free(b);
        s_done = 1;
    }
    return s_base;
}

void Config_LoadOrCreate(const char *path)
{
    char def[1100];
    if (!path) {
        snprintf(def, sizeof def, "%sshinobi.ini", Config_BaseDir());
        path = def;
    }
    Config_SetDefaults(&g_config);
    int r = Config_LoadFile(&g_config, path);
    if (r > 0) printf("[config] %s loaded\n", path);
    else if (r == 0) {
        if (Config_WriteDefault(path)) printf("[config] %s created with the defaults\n", path);
        else fprintf(stderr, "[config] %s missing and cannot be created: defaults used\n", path);
    }
}
#endif
