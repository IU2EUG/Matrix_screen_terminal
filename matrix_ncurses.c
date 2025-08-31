/*
 * matrix_ncurses.c — Matrix "digital rain" screensaver for terminal (UTF‑8)
 *
 * Build (Ubuntu):
 *   sudo apt-get install build-essential libncursesw5-dev
 *   gcc -O2 -Wall -Wextra -std=c11 matrix_ncurses.c -o matrix -lncursesw
 *
 * Run:
 *   ./matrix                # default settings
 *   ./matrix --speed 1.2 --density 0.35 --bold --no-fade
 *   ./matrix --help
 *
 * Notes:
 *  - Uses wide characters (ncursesw) and UTF‑8 katakana set.
 *  - Handles terminal resize at runtime.
 *  - Press 'q' or ESC to quit. Press 'p' to pause/resume.
 */

#define _XOPEN_SOURCE 700
#include <locale.h>
#include <ncursesw/ncurses.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (int)(sizeof(a)/sizeof((a)[0]))
#endif

// Random helper
static inline uint32_t rnd32(void){
  static uint64_t s = 0;
  if(!s){ s = ((uint64_t)time(NULL) ^ (uintptr_t)&s) | 1ULL; }
  s ^= s << 7; s ^= s >> 9; s ^= s << 8; // xorshift-like
  return (uint32_t)s;
}
static inline int irand(int n){ return (int)(rnd32() % (uint32_t)n); }
static inline double drand(void){ return (rnd32() / (double)UINT32_MAX); }

// Katakana (and a few Latin) characters for the rain
static const wchar_t KATAKANA[] = {
  L'ｦ',L'ｧ',L'ｨ',L'ｩ',L'ｪ',L'ｫ',L'ｬ',L'ｭ',L'ｮ',L'ｯ',L'ｱ',L'ｲ',L'ｳ',L'ｴ',L'ｵ',
  L'ｶ',L'ｷ',L'ｸ',L'ｹ',L'ｺ',L'ｻ',L'ｼ',L'ｽ',L'ｾ',L'ｿ',L'ﾀ',L'ﾁ',L'ﾂ',L'ﾃ',L'ﾄ',
  L'ﾅ',L'ﾆ',L'ﾇ',L'ﾈ',L'ﾉ',L'ﾊ',L'ﾋ',L'ﾌ',L'ﾍ',L'ﾎ',L'ﾏ',L'ﾐ',L'ﾑ',L'ﾒ',L'ﾓ',
  L'ﾔ',L'ﾕ',L'ﾖ',L'ﾗ',L'ﾘ',L'ﾙ',L'ﾚ',L'ﾛ',L'ﾜ',L'ﾝ',L'0',L'1',L'2',L'3',L'4',L'5',L'6',L'7',L'8',L'9'
};

typedef struct {
  int head_y;        // current head row (can be negative before entering)
  int tail;          // tail length in rows
  double speed;      // rows per frame
  bool active;       // whether this column is currently raining
} Column;

static void usage(const char *prog){
  fprintf(stderr,
    "Usage: %s [--speed f] [--density 0..1] [--bold] [--no-fade] [--fps N]\n",
    prog);
}

int main(int argc, char **argv){
  // Defaults
  double speed_mul = 1.0;   // global speed multiplier
  double density   = 0.25;  // fraction of columns active
  bool bold_head   = false;
  bool fade_trail  = true;
  int target_fps   = 60;

  // Parse args (very lightweight)
  for(int i=1;i<argc;i++){
    if(!strcmp(argv[i],"--speed") && i+1<argc){ speed_mul = atof(argv[++i]); }
    else if(!strcmp(argv[i],"--density") && i+1<argc){ density = atof(argv[++i]); if(density<0) density=0; if(density>1) density=1; }
    else if(!strcmp(argv[i],"--bold")){ bold_head = true; }
    else if(!strcmp(argv[i],"--no-fade")){ fade_trail = false; }
    else if(!strcmp(argv[i],"--fps") && i+1<argc){ target_fps = atoi(argv[++i]); if(target_fps<10) target_fps=10; if(target_fps>240) target_fps=240; }
    else if(!strcmp(argv[i],"--help")){ usage(argv[0]); return 0; }
    else { usage(argv[0]); return 1; }
  }

  setlocale(LC_ALL, ""); // enable UTF‑8/wide chars

  initscr();
  start_color();
  use_default_colors();
  noecho(); curs_set(0); keypad(stdscr, TRUE); nodelay(stdscr, TRUE);
  // Define color pairs: 1 = dim green, 2 = bright green, 3 = white head
  init_pair(1, COLOR_GREEN, -1);
  init_pair(2, COLOR_GREEN, -1);
  init_pair(3, COLOR_WHITE, -1);

  int maxy, maxx; getmaxyx(stdscr, maxy, maxx);
  Column *cols = calloc((size_t)maxx, sizeof(Column));
  if(!cols){ endwin(); fprintf(stderr, "OOM\n"); return 1; }

  // Initialize columns
  for(int x=0;x<maxx;x++){
    cols[x].active = drand() < density;
    cols[x].tail = 5 + irand(20);
    cols[x].speed = (0.4 + drand()*1.2) * speed_mul; // rows per frame
    cols[x].head_y = -irand(maxy); // start above the screen
  }

  bool paused=false;
  const double frame_time = 1.0 / (double)target_fps;
  struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (long)(frame_time*1e9);

  while(1){
    // Handle input
    int ch = getch();
    if(ch=='q' || ch==27 /*ESC*/){ break; }
    if(ch=='p' || ch=='P'){ paused=!paused; }

    // Handle resize
    int ny,nx; getmaxyx(stdscr, ny, nx);
    if(nx != maxx || ny != maxy){
      Column *ncols = calloc((size_t)nx, sizeof(Column));
      if(!ncols){ break; }
      // Transfer as many as possible
      for(int x=0; x<nx && x<maxx; x++){ ncols[x] = cols[x]; }
      // Init new columns
      for(int x=maxx; x<nx; x++){
        ncols[x].active = drand() < density;
        ncols[x].tail = 5 + irand(20);
        ncols[x].speed = (0.4 + drand()*1.2) * speed_mul;
        ncols[x].head_y = -irand(ny);
      }
      free(cols); cols=ncols; maxx=nx; maxy=ny; clear();
    }

    if(!paused){
      // Occasionally toggle columns to reach target density
      for(int x=0;x<maxx;x++){
        if(cols[x].active){
          // advance head
          cols[x].head_y += cols[x].speed < 1.0 ? (drand()<cols[x].speed ? 1 : 0) : (int)cols[x].speed;
          if(cols[x].head_y - cols[x].tail > maxy){
            // reset column when tail fully off-screen
            cols[x].active = drand() < density; // maybe go idle
            cols[x].head_y = -irand(maxy);
            cols[x].tail = 5 + irand(20);
            cols[x].speed = (0.4 + drand()*1.2) * speed_mul;
          }
        } else if(drand() < density/200.0){
          // randomly start new rain
          cols[x].active = true; cols[x].head_y = -irand(maxy); cols[x].tail = 5 + irand(20);
          cols[x].speed = (0.4 + drand()*1.2) * speed_mul;
        }
      }

      // Draw
      // Fade entire screen slightly (simple approach using spaces + A_DIM)
      if(fade_trail){
        attron(COLOR_PAIR(1)); attron(A_DIM);
        for(int y=0;y<maxy;y++){
          for(int x=0;x<maxx;x++){
            // draw space with dim green to create a fading overlay
            mvaddch(y,x,' ');
          }
        }
        attroff(A_DIM); attroff(COLOR_PAIR(1));
      } else {
        // Without fade, clear only occasionally to avoid smear
        erase();
      }

      for(int x=0;x<maxx;x++){
        if(!cols[x].active) continue;
        int head = cols[x].head_y;
        int tail_start = head - cols[x].tail;
        for(int y=tail_start; y<=head; y++){
          if(y<0 || y>=maxy) continue;
          wchar_t wc = KATAKANA[irand(ARRAY_SIZE(KATAKANA))];
          if(y==head){
            // Head: bright
            if(bold_head) attron(A_BOLD); else attroff(A_BOLD);
            attron(COLOR_PAIR(3));
            mvaddnwstr(y, x, &wc, 1);
            attroff(COLOR_PAIR(3));
            if(bold_head) attroff(A_BOLD);
          } else {
            // Trail: normal green, dim if close to tail start
            int dist = head - y;
            if(dist > cols[x].tail-2) attron(A_DIM); else attroff(A_DIM);
            attron(COLOR_PAIR(2));
            mvaddnwstr(y, x, &wc, 1);
            attroff(COLOR_PAIR(2));
            if(dist > cols[x].tail-2) attroff(A_DIM);
          }
        }
      }
    }

    // Present
    refresh();
    nanosleep(&ts, NULL);
  }

  endwin();
  free(cols);
  return 0;
}
