/*
 * touch_kb.c - see touch_kb.h. No Android specific code in here.
 */
#include "touch_kb.h"
#include <string.h>
#include <stdio.h>

static TkbSink g_sink = NULL;
void tkb_set_sink(TkbSink sink) { g_sink = sink; }

/* ------------------------------------------------------------------ injection */

static void inject(TkbState *t, SDL_Keycode sym, SDL_Scancode scan, bool down)
{
    Uint16 mod = 0;
    if (t->ctrl_on)  mod |= KMOD_LCTRL;
    if (t->shift_on) mod |= KMOD_LSHIFT;

    if (g_sink) { g_sink(sym, scan, mod, down); return; }

    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.repeat = 0;
    e.key.keysym.sym = sym;
    e.key.keysym.scancode = scan;
    e.key.keysym.mod = mod;
    SDL_PushEvent(&e);
}

/* A "modified" tap: modifier down -> key down -> key up -> modifier up, in that order.
 * The tracker sets `modifier` on key_lctrl down and clears it on key_lctrl up, so the order
 * matters: releasing the modifier BEFORE the key would lose it. */
static void tap_with_modifiers(TkbState *t, SDL_Keycode sym, SDL_Scancode scan, bool down)
{
    if (down) {
        if (t->ctrl_on)  inject(t, SDLK_LCTRL,  SDL_SCANCODE_LCTRL,  true);
        if (t->shift_on) inject(t, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, true);
        inject(t, sym, scan, true);
    } else {
        inject(t, sym, scan, false);
        if (t->shift_on) inject(t, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, false);
        if (t->ctrl_on)  inject(t, SDLK_LCTRL,  SDL_SCANCODE_LCTRL,  false);
        /* sticky modifiers are one-shot */
        t->ctrl_on = false;
        t->shift_on = false;
    }
}

/* ------------------------------------------------------------------ layout */

static TkbKey *add(TkbState *t, int x, int y, int w, int h,
                   SDL_Keycode sym, SDL_Scancode scan, TkbKind kind,
                   const char *label, int group)
{
    if (t->count >= TKB_MAX_KEYS) return NULL;
    TkbKey *k = &t->keys[t->count++];
    memset(k, 0, sizeof(*k));
    k->x = x; k->y = y; k->w = w; k->h = h;
    k->sym = sym; k->scan = scan; k->kind = kind;
    snprintf(k->label, sizeof(k->label), "%s", label);
    k->color_group = group;
    k->finger = -1;
    return k;
}

/* Row of equal-width keys. */
typedef struct { const char *label; SDL_Keycode sym; SDL_Scancode scan; TkbKind kind; int group; } Def;

static void row(TkbState *t, int x0, int y, int total_w, int h, const Def *d, int n)
{
    int gap = 3;
    int w = (total_w - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++)
        add(t, x0 + i * (w + gap), y, w, h, d[i].sym, d[i].scan, d[i].kind, d[i].label, d[i].group);
}

int tkb_layout(TkbState *t, int win_w, int win_h)
{
    memset(t, 0, sizeof(*t));
    t->visible = true;
    t->win_w = win_w;
    t->win_h = win_h;

    /* Keyboard height: 42% of the screen but never below 4 rows of 44px. */
    int rows = 5;
    int kb_h = win_h * 42 / 100;
    if (kb_h < rows * 44) kb_h = rows * 44;
    int row_h = kb_h / rows;
    int top = win_h - row_h * rows;
    t->area_top = top;

    const int M = 4;                       /* outer margin */
    int W = win_w - 2 * M;

    /* --- row 0: views F1..F9 --- */
    static const Def views[] = {
        {"F1", SDLK_F1, SDL_SCANCODE_F1, TKB_NORMAL, 3}, {"F2", SDLK_F2, SDL_SCANCODE_F2, TKB_NORMAL, 3},
        {"F3", SDLK_F3, SDL_SCANCODE_F3, TKB_NORMAL, 3}, {"F4", SDLK_F4, SDL_SCANCODE_F4, TKB_NORMAL, 3},
        {"F5", SDLK_F5, SDL_SCANCODE_F5, TKB_NORMAL, 3}, {"F6", SDLK_F6, SDL_SCANCODE_F6, TKB_NORMAL, 3},
        {"F7", SDLK_F7, SDL_SCANCODE_F7, TKB_NORMAL, 3}, {"F8", SDLK_F8, SDL_SCANCODE_F8, TKB_NORMAL, 3},
        {"F9", SDLK_F9, SDL_SCANCODE_F9, TKB_NORMAL, 3},
    };
    row(t, M, top, W, row_h - 2, views, 9);

    /* --- row 1: command keys --- */
    static const Def cmd[] = {
        {"Esc", SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, TKB_NORMAL, 2},
        {"Tab", SDLK_TAB, SDL_SCANCODE_TAB, TKB_NORMAL, 2},
        {"Ctrl", SDLK_LCTRL, SDL_SCANCODE_LCTRL, TKB_STICKY, 4},
        {"Shift", SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, TKB_STICKY, 4},
        {"Edit", SDLK_RETURN, SDL_SCANCODE_RETURN, TKB_NORMAL, 2},
        {"Play", SDLK_SPACE, SDL_SCANCODE_SPACE, TKB_NORMAL, 2},
        {"Del", SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, TKB_REPEAT, 2},
        {"Home", SDLK_HOME, SDL_SCANCODE_HOME, TKB_NORMAL, 2},
        {"End", SDLK_END, SDL_SCANCODE_END, TKB_NORMAL, 2},
    };
    row(t, M, top + row_h, W, row_h - 2, cmd, 9);

    /* --- rows 2-3: two piano rows (upper octave over lower octave) --- */
    /* The tracker maps: lower row z s x d c v g b h n j m , l .   upper row q 2 w 3 e r 5 t 6 y 7 u i 9 o 0 p */
    static const Def upper[] = {
        {"q", SDLK_q, SDL_SCANCODE_Q, TKB_NORMAL, 0}, {"2", SDLK_2, SDL_SCANCODE_2, TKB_NORMAL, 1},
        {"w", SDLK_w, SDL_SCANCODE_W, TKB_NORMAL, 0}, {"3", SDLK_3, SDL_SCANCODE_3, TKB_NORMAL, 1},
        {"e", SDLK_e, SDL_SCANCODE_E, TKB_NORMAL, 0}, {"r", SDLK_r, SDL_SCANCODE_R, TKB_NORMAL, 0},
        {"5", SDLK_5, SDL_SCANCODE_5, TKB_NORMAL, 1}, {"t", SDLK_t, SDL_SCANCODE_T, TKB_NORMAL, 0},
        {"6", SDLK_6, SDL_SCANCODE_6, TKB_NORMAL, 1}, {"y", SDLK_y, SDL_SCANCODE_Y, TKB_NORMAL, 0},
        {"7", SDLK_7, SDL_SCANCODE_7, TKB_NORMAL, 1}, {"u", SDLK_u, SDL_SCANCODE_U, TKB_NORMAL, 0},
        {"i", SDLK_i, SDL_SCANCODE_I, TKB_NORMAL, 0}, {"9", SDLK_9, SDL_SCANCODE_9, TKB_NORMAL, 1},
        {"o", SDLK_o, SDL_SCANCODE_O, TKB_NORMAL, 0}, {"0", SDLK_0, SDL_SCANCODE_0, TKB_NORMAL, 1},
        {"p", SDLK_p, SDL_SCANCODE_P, TKB_NORMAL, 0},
    };
    static const Def lower[] = {
        {"z", SDLK_z, SDL_SCANCODE_Z, TKB_NORMAL, 0}, {"s", SDLK_s, SDL_SCANCODE_S, TKB_NORMAL, 1},
        {"x", SDLK_x, SDL_SCANCODE_X, TKB_NORMAL, 0}, {"d", SDLK_d, SDL_SCANCODE_D, TKB_NORMAL, 1},
        {"c", SDLK_c, SDL_SCANCODE_C, TKB_NORMAL, 0}, {"v", SDLK_v, SDL_SCANCODE_V, TKB_NORMAL, 0},
        {"g", SDLK_g, SDL_SCANCODE_G, TKB_NORMAL, 1}, {"b", SDLK_b, SDL_SCANCODE_B, TKB_NORMAL, 0},
        {"h", SDLK_h, SDL_SCANCODE_H, TKB_NORMAL, 1}, {"n", SDLK_n, SDL_SCANCODE_N, TKB_NORMAL, 0},
        {"j", SDLK_j, SDL_SCANCODE_J, TKB_NORMAL, 1}, {"m", SDLK_m, SDL_SCANCODE_M, TKB_NORMAL, 0},
        {",", SDLK_COMMA, SDL_SCANCODE_COMMA, TKB_NORMAL, 0}, {"l", SDLK_l, SDL_SCANCODE_L, TKB_NORMAL, 1},
        {".", SDLK_PERIOD, SDL_SCANCODE_PERIOD, TKB_NORMAL, 0},
    };
    row(t, M, top + 2 * row_h, W, row_h - 2, upper, (int)(sizeof(upper) / sizeof(upper[0])));
    row(t, M, top + 3 * row_h, W, row_h - 2, lower, (int)(sizeof(lower) / sizeof(lower[0])));

    /* --- row 4: arrows, +/-, digits and hex letters used by parameter fields --- */
    static const Def bottom[] = {
        {"<", SDLK_LEFT,  SDL_SCANCODE_LEFT,  TKB_REPEAT, 2},
        {"v", SDLK_DOWN,  SDL_SCANCODE_DOWN,  TKB_REPEAT, 2},
        {"^", SDLK_UP,    SDL_SCANCODE_UP,    TKB_REPEAT, 2},
        {">", SDLK_RIGHT, SDL_SCANCODE_RIGHT, TKB_REPEAT, 2},
        {"-", SDLK_MINUS, SDL_SCANCODE_MINUS, TKB_REPEAT, 2},
        {"+", SDLK_EQUALS, SDL_SCANCODE_EQUALS, TKB_REPEAT, 2},
        {"1", SDLK_1, SDL_SCANCODE_1, TKB_NORMAL, 2},
        {"4", SDLK_4, SDL_SCANCODE_4, TKB_NORMAL, 2},
        {"8", SDLK_8, SDL_SCANCODE_8, TKB_NORMAL, 2},
        {"a", SDLK_a, SDL_SCANCODE_A, TKB_NORMAL, 2},
        {"f", SDLK_f, SDL_SCANCODE_F, TKB_NORMAL, 2},
    };
    row(t, M, top + 4 * row_h, W, row_h - 2, bottom, (int)(sizeof(bottom) / sizeof(bottom[0])));

    return top;
}

/* ------------------------------------------------------------------ touch handling */

int tkb_hit(const TkbState *t, int px, int py)
{
    for (int i = 0; i < t->count; i++) {
        const TkbKey *k = &t->keys[i];
        if (px >= k->x && px < k->x + k->w && py >= k->y && py < k->y + k->h) return i;
    }
    return -1;
}

static void press(TkbState *t, TkbKey *k, int finger)
{
    k->finger = finger;
    k->lit = true;
    if (k->kind == TKB_STICKY) {
        if (k->sym == SDLK_LCTRL)  t->ctrl_on  = !t->ctrl_on;
        if (k->sym == SDLK_LSHIFT) t->shift_on = !t->shift_on;
        k->lit = (k->sym == SDLK_LCTRL) ? t->ctrl_on : t->shift_on;
        return;                          /* nothing injected: it only arms the next key */
    }
    tap_with_modifiers(t, k->sym, k->scan, true);
    if (k->kind == TKB_REPEAT) k->next_repeat = SDL_GetTicks() + 400;
}

static void release(TkbState *t, TkbKey *k)
{
    if (k->kind == TKB_STICKY) {
        k->finger = -1;
        return;                          /* keep the toggled highlight */
    }
    if (k->finger >= 0) {
        tap_with_modifiers(t, k->sym, k->scan, false);
    }
    k->finger = -1;
    k->lit = false;
}

static void sync_sticky_lights(TkbState *t)
{
    for (int i = 0; i < t->count; i++) {
        TkbKey *k = &t->keys[i];
        if (k->kind == TKB_STICKY)
            k->lit = (k->sym == SDLK_LCTRL) ? t->ctrl_on : t->shift_on;
    }
}

bool tkb_finger_down(TkbState *t, int finger, float fx, float fy)
{
    if (!t->visible) return false;
    int px = (int)(fx * t->win_w), py = (int)(fy * t->win_h);
    int i = tkb_hit(t, px, py);
    if (i < 0) return false;             /* not ours: let the caller treat it as a plain touch */
    press(t, &t->keys[i], finger);
    sync_sticky_lights(t);
    return true;
}

bool tkb_finger_move(TkbState *t, int finger, float fx, float fy)
{
    if (!t->visible) return false;
    int px = (int)(fx * t->win_w), py = (int)(fy * t->win_h);
    bool consumed = false;
    for (int i = 0; i < t->count; i++) {
        TkbKey *k = &t->keys[i];
        if (k->finger != finger) continue;
        consumed = true;
        /* sliding off the key releases it (prevents stuck notes) */
        if (px < k->x || px >= k->x + k->w || py < k->y || py >= k->y + k->h) {
            release(t, k);
            sync_sticky_lights(t);
        }
    }
    return consumed;
}

bool tkb_finger_up(TkbState *t, int finger, float fx, float fy)
{
    (void)fx; (void)fy;
    bool consumed = false;
    for (int i = 0; i < t->count; i++) {
        TkbKey *k = &t->keys[i];
        if (k->finger == finger) { release(t, k); consumed = true; }
    }
    sync_sticky_lights(t);
    return consumed;
}

void tkb_tick(TkbState *t, Uint32 now)
{
    for (int i = 0; i < t->count; i++) {
        TkbKey *k = &t->keys[i];
        if (k->kind == TKB_REPEAT && k->finger >= 0 && now >= k->next_repeat) {
            /* repeat = a fresh down/up pair so the editor sees a new key each time */
            tap_with_modifiers(t, k->sym, k->scan, false);
            tap_with_modifiers(t, k->sym, k->scan, true);
            k->next_repeat = now + 90;
        }
    }
}

/* ------------------------------------------------------------------ drawing (SDL renderer) */

static void rect(SDL_Renderer *r, int x, int y, int w, int h, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    SDL_SetRenderDrawColor(r, R, G, B, A);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(r, &rc);
}

/* Tiny 5x7 glyphs for labels, so we need neither SDL_ttf nor a font asset. */
static const unsigned char *glyph(char c);

static void text(SDL_Renderer *r, int cx, int cy, const char *s, int scale,
                 Uint8 R, Uint8 G, Uint8 B)
{
    int n = (int)strlen(s);
    int w = n * 6 * scale - scale;
    int x = cx - w / 2, y = cy - (7 * scale) / 2;
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    for (int i = 0; i < n; i++) {
        const unsigned char *g = glyph(s[i]);
        for (int col = 0; col < 5; col++)
            for (int row_ = 0; row_ < 7; row_++)
                if (g[col] & (1 << row_)) {
                    SDL_Rect p = { x + (i * 6 + col) * scale, y + row_ * scale, scale, scale };
                    SDL_RenderFillRect(r, &p);
                }
    }
}

void tkb_draw_renderer(const TkbState *t, SDL_Renderer *r)
{
    if (!t->visible) return;
    rect(r, 0, t->area_top, t->win_w, t->win_h - t->area_top, 12, 12, 12, 255);

    /* label scale from key height so text stays readable on small phones */
    int scale = 2;
    if (t->count > 0 && t->keys[0].h >= 60) scale = 3;

    for (int i = 0; i < t->count; i++) {
        const TkbKey *k = &t->keys[i];
        Uint8 R = 60, G = 60, B = 60;
        switch (k->color_group) {
        case 0: R = 200; G = 200; B = 200; break;          /* white piano key */
        case 1: R = 70;  G = 70;  B = 90;  break;          /* black piano key */
        case 2: R = 70;  G = 70;  B = 70;  break;          /* command */
        case 3: R = 40;  G = 80;  B = 110; break;          /* views */
        case 4: R = 110; G = 80;  B = 30;  break;          /* sticky */
        }
        if (k->lit) { R = 255; G = 140; B = 0; }
        rect(r, k->x, k->y, k->w, k->h, R, G, B, 255);

        Uint8 tr = (k->color_group == 0 && !k->lit) ? 20 : 240;
        text(r, k->x + k->w / 2, k->y + k->h / 2, k->label, scale, tr, tr, tr);
    }
}

/* Minimal 5x7 font: digits, lowercase/uppercase letters, and the few symbols used.
 * Each glyph is 5 columns; bit n of a column byte = row n (0 = top). */
static const unsigned char G_SPACE[5] = {0,0,0,0,0};
static const unsigned char G_UNK[5]   = {0x7F,0x41,0x41,0x41,0x7F};
static const unsigned char G_DIG[10][5] = {
 {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
 {0x06,0x49,0x49,0x29,0x1E}};
static const unsigned char G_UP[26][5] = {
 {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
 {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
 {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
 {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
 {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
 {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
 {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
 {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
static const unsigned char G_LO[26][5] = {
 {0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
 {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},
 {0x0C,0x52,0x52,0x52,0x3E},{0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},
 {0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},
 {0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
 {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},
 {0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},
 {0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},
 {0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44}};
static const unsigned char G_SYM_MINUS[5]  = {0x08,0x08,0x08,0x08,0x08};
static const unsigned char G_SYM_PLUS[5]   = {0x08,0x08,0x3E,0x08,0x08};
static const unsigned char G_SYM_COMMA[5]  = {0x00,0x50,0x30,0x00,0x00};
static const unsigned char G_SYM_PERIOD[5] = {0x00,0x60,0x60,0x00,0x00};
static const unsigned char G_SYM_LT[5]     = {0x08,0x14,0x22,0x41,0x00};
static const unsigned char G_SYM_GT[5]     = {0x00,0x41,0x22,0x14,0x08};
static const unsigned char G_SYM_UPARR[5]  = {0x04,0x02,0x7F,0x02,0x04};

static const unsigned char *glyph(char c)
{
    if (c >= '0' && c <= '9') return G_DIG[c - '0'];
    if (c >= 'A' && c <= 'Z') return G_UP[c - 'A'];
    if (c >= 'a' && c <= 'z') return G_LO[c - 'a'];
    switch (c) {
    case ' ': return G_SPACE;
    case '-': return G_SYM_MINUS;
    case '+': return G_SYM_PLUS;
    case ',': return G_SYM_COMMA;
    case '.': return G_SYM_PERIOD;
    case '<': return G_SYM_LT;
    case '>': return G_SYM_GT;
    case '^': return G_SYM_UPARR;
    case 'v': return G_LO['v' - 'a'];
    }
    return G_UNK;
}
