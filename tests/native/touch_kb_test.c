#include "touch_kb.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* record every injected event as text so the test can compare sequences */
static char logbuf[4096]; static int logn;
static void sink(SDL_Keycode sym, SDL_Scancode scan, Uint16 mod, bool down){
  (void)scan;
  const char *nm = SDL_GetKeyName(sym);
  logn += snprintf(logbuf+logn, sizeof(logbuf)-logn, "%s%s ", down?"+":"-", nm);
}
static int fails=0;
#define CHECK(cond,msg) do{ if(!(cond)){printf("FAIL: %s\n",msg); fails++;} else printf("ok   : %s\n",msg);}while(0)
static void clear(void){ logbuf[0]=0; logn=0; }

static TkbState T;
static void center(int idx, float *fx, float *fy){
  TkbKey *k=&T.keys[idx]; *fx=(k->x+k->w/2)/(float)T.win_w; *fy=(k->y+k->h/2)/(float)T.win_h; }
static int find(SDL_Keycode s, int group_hint){
  for(int i=0;i<T.count;i++) if(T.keys[i].sym==s && (group_hint<0||T.keys[i].color_group==group_hint)) return i; return -1; }
static void tap(int idx,int finger){ float x,y; center(idx,&x,&y); tkb_finger_down(&T,finger,x,y); tkb_finger_up(&T,finger,x,y); }

int main(void){
  SDL_Init(0);
  tkb_set_sink(sink);

  /* 1) layout on the real phone size from the screenshot: 1520x720 */
  int top = tkb_layout(&T,1520,720);
  printf("layout: keys=%d area_top=%d (screen 1520x720)\n", T.count, top);
  CHECK(T.count>=60 && T.count<=TKB_MAX_KEYS, "key count sane");
  CHECK(top>300 && top<600, "keyboard leaves a usable top area");

  /* 2) no key overlaps another, and all stay on screen */
  int overlap=0, off=0;
  for(int i=0;i<T.count;i++){
    TkbKey*a=&T.keys[i];
    if(a->x<0||a->y<0||a->x+a->w>T.win_w||a->y+a->h>T.win_h) off++;
    for(int j=i+1;j<T.count;j++){ TkbKey*b=&T.keys[j];
      if(a->x<b->x+b->w && b->x<a->x+a->w && a->y<b->y+b->h && b->y<a->y+a->h) overlap++; } }
  CHECK(overlap==0,"no overlapping keys"); CHECK(off==0,"all keys inside the screen");

  /* 3) touch target size (finger friendly). 1520px wide phone ~ 6.4in => aim >= 44px */
  int minw=99999,minh=99999; for(int i=0;i<T.count;i++){ if(T.keys[i].w<minw)minw=T.keys[i].w; if(T.keys[i].h<minh)minh=T.keys[i].h; }
  printf("smallest key: %dx%d px\n",minw,minh);
  CHECK(minh>=44,"smallest key height >= 44px"); CHECK(minw>=44,"smallest key width >= 44px");

  /* 4) plain note */
  clear(); tap(find(SDLK_z,0),1);
  printf("   z    -> %s\n",logbuf); CHECK(strcmp(logbuf,"+Z -Z ")==0,"plain tap sends down then up");

  /* 5) sticky Ctrl then S : must be  +Left Ctrl +S -S -Left Ctrl  and Ctrl must clear */
  clear(); tap(find(SDLK_LCTRL,4),2); CHECK(logn==0,"arming Ctrl sends nothing yet"); CHECK(T.ctrl_on,"Ctrl is armed");
  tap(find(SDLK_s,1),3);
  printf("   ctrl+s -> %s\n",logbuf);
  CHECK(strcmp(logbuf,"+Left Ctrl +S -S -Left Ctrl ")==0,"Ctrl+S order is modifier-first, released last");
  CHECK(!T.ctrl_on,"Ctrl auto-clears after one key");
  clear(); tap(find(SDLK_s,1),3); CHECK(strcmp(logbuf,"+S -S ")==0,"next key has no modifier");

  /* 6) toggling Ctrl twice cancels it */
  clear(); tap(find(SDLK_LCTRL,4),2); tap(find(SDLK_LCTRL,4),2); CHECK(!T.ctrl_on,"Ctrl toggles off on second tap");
  tap(find(SDLK_c,0),3); CHECK(strcmp(logbuf,"+C -C ")==0,"cancelled Ctrl sends nothing extra");

  /* 7) Shift + arrow (selection) */
  clear(); tap(find(SDLK_LSHIFT,4),2); tap(find(SDLK_DOWN,2),3);
  printf("   shift+down -> %s\n",logbuf);
  CHECK(strcmp(logbuf,"+Left Shift +Down -Down -Left Shift ")==0,"Shift+Down order");

  /* 8) Ctrl+Shift together */
  clear(); tap(find(SDLK_LCTRL,4),2); tap(find(SDLK_LSHIFT,4),2); tap(find(SDLK_c,0),3);
  printf("   ctrl+shift+c -> %s\n",logbuf);
  CHECK(strstr(logbuf,"+Left Ctrl")&&strstr(logbuf,"+Left Shift")&&strstr(logbuf,"-Left Ctrl")&&strstr(logbuf,"-Left Shift"),"both modifiers sent and released");

  /* 9) slide off a key -> released (no stuck note) */
  clear(); { float x,y; int z=find(SDLK_z,0); center(z,&x,&y); tkb_finger_down(&T,5,x,y);
             tkb_finger_move(&T,5,0.999f,0.001f); tkb_finger_up(&T,5,0.999f,0.001f); }
  printf("   slide off z -> %s\n",logbuf); CHECK(strcmp(logbuf,"+Z -Z ")==0,"sliding off releases exactly once");

  /* 10) two fingers on two piano keys at the same time (chord) */
  clear(); { float x1,y1,x2,y2; center(find(SDLK_z,0),&x1,&y1); center(find(SDLK_c,0),&x2,&y2);
             tkb_finger_down(&T,6,x1,y1); tkb_finger_down(&T,7,x2,y2); tkb_finger_up(&T,6,x1,y1); tkb_finger_up(&T,7,x2,y2); }
  printf("   chord -> %s\n",logbuf); CHECK(strcmp(logbuf,"+Z +C -Z -C ")==0,"multi-touch chord works");

  /* 11) touch above the keyboard is not consumed (lets the tracker treat it as a normal touch) */
  CHECK(!tkb_finger_down(&T,8,0.5f,0.1f),"touch on the tracker area is not consumed");

  /* 12) auto-repeat on arrows: hold, tick past 400ms, expect extra pairs */
  clear(); { float x,y; int a=find(SDLK_RIGHT,2); center(a,&x,&y); tkb_finger_down(&T,9,x,y);
             T.keys[a].next_repeat = 0; tkb_tick(&T, 1000); tkb_tick(&T, 1000); tkb_finger_up(&T,9,x,y); }
  printf("   held Right -> %s\n",logbuf); CHECK(strstr(logbuf,"-Right +Right")!=NULL,"held arrow repeats");

  /* 13) every key used by the tracker has a working key on screen */
  const SDL_Keycode need[] = {SDLK_F1,SDLK_F2,SDLK_F3,SDLK_F4,SDLK_F5,SDLK_F6,SDLK_F7,SDLK_F8,SDLK_F9,
    SDLK_RETURN,SDLK_SPACE,SDLK_TAB,SDLK_ESCAPE,SDLK_BACKSPACE,SDLK_HOME,SDLK_END,
    SDLK_LEFT,SDLK_RIGHT,SDLK_UP,SDLK_DOWN,SDLK_LCTRL,SDLK_LSHIFT,SDLK_MINUS,SDLK_EQUALS};
  int miss=0; for(unsigned i=0;i<sizeof(need)/sizeof(need[0]);i++) if(find(need[i],-1)<0){printf("   missing %s\n",SDL_GetKeyName(need[i])); miss++;}
  CHECK(miss==0,"all control keys the tracker uses are present");


  /* ---- toggle button ---- */
  { int x,y,w,h; tkb_layout(&T,1465,720); tkb_toggle_rect(&T,&x,&y,&w,&h);
    printf("toggle rect: x=%d y=%d w=%d h=%d\n",x,y,w,h);
    CHECK(w>=44&&h>=44,"toggle is finger sized");
    CHECK(x>=0&&y>=0&&x+w<=T.win_w&&y+h<=T.win_h,"toggle is on screen");
    /* it must not sit on top of any key, otherwise a key tap could hit the toggle */
    int ov=0; for(int i=0;i<T.count;i++){TkbKey*k=&T.keys[i];
      if(x<k->x+k->w&&k->x<x+w&&y<k->y+k->h&&k->y<y+h) ov++; }
    CHECK(ov==0,"toggle does not overlap any key");
    /* and not over the tracker picture area corner used by the LOG button (top-right) */
    CHECK(x+w < T.win_w/2,"toggle is on the left half (LOG is top-right)");

    CHECK(T.visible && tkb_available_height(&T)==T.area_top,"shown: tracker height = area_top");
    float fx=(x+w/2)/(float)T.win_w, fy=(y+h/2)/(float)T.win_h;
    CHECK(tkb_toggle_finger_down(&T,fx,fy),"tap on toggle is consumed");
    CHECK(!T.visible && tkb_available_height(&T)==T.win_h,"hidden: tracker gets the full height");
    CHECK(!tkb_finger_down(&T,1,0.035f,0.874f),"hidden keyboard ignores touches on former key positions");
    CHECK(tkb_toggle_finger_down(&T,fx,fy) && T.visible,"second tap shows it again");
    CHECK(!tkb_toggle_finger_down(&T,0.5f,0.1f),"tap elsewhere is not the toggle");

    /* hiding while a key is held must release it (no stuck note) and drop armed modifiers */
    clear(); tkb_finger_down(&T,3,0.035f,0.874f);            /* hold z */
    tkb_finger_down(&T,4,(T.keys[find(SDLK_LCTRL,4)].x+5)/(float)T.win_w,(T.keys[find(SDLK_LCTRL,4)].y+5)/(float)T.win_h); /* arm ctrl */
    CHECK(T.ctrl_on,"ctrl armed before hiding");
    tkb_toggle_finger_down(&T,fx,fy);
    printf("   hide while holding z -> %s\n",logbuf);
    CHECK(strstr(logbuf,"-Z")!=NULL,"held key is released when hiding");
    CHECK(!T.ctrl_on && !T.shift_on,"armed modifiers are cleared when hiding");
  }

  /* ---- regression: modifier armed AFTER a key went down must not be released with it ---- */
  { tkb_layout(&T,1465,720); clear();
    float zx,zy,cx,cy; center(find(SDLK_z,0),&zx,&zy); center(find(SDLK_LCTRL,4),&cx,&cy);
    tkb_finger_down(&T,1,zx,zy);          /* hold z, no modifier yet */
    tkb_finger_down(&T,2,cx,cy);          /* arm Ctrl with a second finger */
    tkb_finger_up(&T,2,cx,cy);
    tkb_finger_up(&T,1,zx,zy);            /* release z */
    printf("   z held, ctrl armed, z released -> %s\n",logbuf);
    CHECK(strcmp(logbuf,"+Z -Z ")==0,"no orphan Ctrl key-up when Ctrl was armed after the key went down");
    CHECK(T.ctrl_on,"Ctrl is still armed for the NEXT key");
    clear(); tap(find(SDLK_s,1),3);
    printf("   next key -> %s\n",logbuf);
    CHECK(strcmp(logbuf,"+Left Ctrl +S -S -Left Ctrl ")==0,"armed Ctrl applies to the next key and is released with it");
    CHECK(!T.ctrl_on,"and clears afterwards");
  }
  /* ---- Ctrl armed, then key HELD: Ctrl+key auto-repeat should keep Ctrl ---- */
  { tkb_layout(&T,1465,720); clear();
    float rx,ry,cx,cy; center(find(SDLK_RIGHT,2),&rx,&ry); center(find(SDLK_LCTRL,4),&cx,&cy);
    tkb_finger_down(&T,1,cx,cy); tkb_finger_up(&T,1,cx,cy);      /* arm ctrl */
    tkb_finger_down(&T,2,rx,ry);                                 /* hold Right with ctrl */
    int a=find(SDLK_RIGHT,2); T.keys[a].next_repeat=0; tkb_tick(&T,1000); tkb_tick(&T,2000); tkb_tick(&T,3000);
    tkb_finger_up(&T,2,rx,ry);
    printf("   ctrl + held Right (1 repeat) -> %s\n",logbuf);
    { int rights=0,withctrl=0; char *q=logbuf; while((q=strstr(q,"+Right"))){ rights++; if(q-logbuf>=11 && strncmp(q-11,"+Left Ctrl ",11)==0) withctrl++; q+=6; }
      printf("   +Right events=%d, of which directly preceded by +Left Ctrl=%d\n",rights,withctrl);
      CHECK(rights>=2 && withctrl==rights,"every auto-repeat of Ctrl+Right carries Ctrl"); }
    /* every Ctrl down must be matched by exactly one Ctrl up */
    int dn=0,up=0; for(char*p=logbuf;(p=strstr(p,"Left Ctrl"));p+=9){ if(*(p-1)==43) dn++; else up++; }
    printf("   ctrl downs=%d ups=%d\n",dn,up);
    CHECK(dn==up,"Ctrl downs and ups are balanced across auto-repeat");
    CHECK(!T.ctrl_on,"Ctrl is not left armed");
  }
  printf("\n%s (%d failure%s)\n", fails?"FAILED":"ALL PASSED", fails, fails==1?"":"s");
  return fails?1:0;
}
