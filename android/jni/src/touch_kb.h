/*
 * touch_kb.h - on-screen keyboard for snibbetracker (Android port).
 *
 * Design rule: DO NOT touch the editor logic. Everything the tracker does is driven by
 * SDL_KEYDOWN / SDL_KEYUP, so this layer only turns finger touches into those events and
 * pushes them with SDL_PushEvent. The editor then behaves exactly as with a real keyboard.
 *
 * The layer is pure C with no Android dependency, so it can be unit-tested on the host.
 * Everything is expressed in "screen pixels" of the window (as SDL reports them).
 */
#ifndef SNIBBE_TOUCH_KB_H
#define SNIBBE_TOUCH_KB_H

#include <SDL.h>
#include <stdbool.h>

#define TKB_MAX_KEYS   128
#define TKB_MAX_FINGERS 10

typedef enum {
    TKB_NORMAL = 0,   /* sends key down on touch, key up on release */
    TKB_STICKY,       /* toggles; applies to the NEXT normal key (Ctrl, Shift) */
    TKB_REPEAT        /* like NORMAL but auto-repeats while held (arrows, +/-) */
} TkbKind;

typedef struct {
    int x, y, w, h;          /* rectangle in window pixels */
    SDL_Keycode sym;         /* key to inject */
    SDL_Scancode scan;       /* matching scancode */
    TkbKind kind;
    char label[8];
    int color_group;         /* 0 white key, 1 black key, 2 command, 3 view, 4 sticky */
    /* runtime state */
    int  finger;             /* finger id holding it, -1 if none */
    bool lit;                /* drawn highlighted */
    Uint32 next_repeat;      /* SDL_GetTicks() of next auto repeat */
} TkbKey;

typedef struct {
    TkbKey keys[TKB_MAX_KEYS];
    int    count;
    bool   visible;
    bool   ctrl_on, shift_on;         /* sticky state */
    int    area_top;                  /* first pixel row used by the keyboard */
    int    win_w, win_h;
} TkbState;

/* Build the layout for a window of the given size. Returns the y where the keyboard begins,
 * i.e. the height available to the tracker picture. */
int  tkb_layout(TkbState *t, int win_w, int win_h);

/* Feed touch events. `fx,fy` are normalized 0..1 as delivered by SDL_FINGER* events. */
bool tkb_finger_down(TkbState *t, int finger, float fx, float fy);
bool tkb_finger_move(TkbState *t, int finger, float fx, float fy);
bool tkb_finger_up  (TkbState *t, int finger, float fx, float fy);

/* Call every frame to generate auto repeats. */
void tkb_tick(TkbState *t, Uint32 now);

/* Returns the index of the key at a window pixel, or -1. */
int  tkb_hit(const TkbState *t, int px, int py);

/* Painting: draws directly with the SDL renderer in window pixel coordinates.
 * The caller must have reset any logical size / viewport before calling. */
void tkb_draw_renderer(const TkbState *t, SDL_Renderer *r);

/* Test hook: when set, events are collected here instead of being pushed to SDL. */
typedef void (*TkbSink)(SDL_Keycode sym, SDL_Scancode scan, Uint16 mod, bool down);
void tkb_set_sink(TkbSink sink);

#endif
