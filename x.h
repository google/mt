#ifndef MT_X_H
#define MT_X_H

extern "C" {
#include <X11/Xlib.h>
}

/* X modifiers */
#define XK_ANY_MOD UINT_MAX
#define XK_NO_MOD 0
#define XK_SWITCH_MOD (1 << 13)

void draw(void);
void drawregion(int, int, int, int);
void run(void);

void xbell(int);
void xclipcopy(void);
void xclippaste(void);
void xhints(void);
void xinit(void);
void xloadcols(void);
int xsetcolorname(int, const char *);
void xloadfonts(const char *, double);
void xsetenv(void);
void xsettitle(const char *);
void xsetpointermotion(int);
void xseturgency(int);
void xunloadfonts(void);
void xresize(int, int);
void xselpaste(void);
unsigned long xwinid(void);
void xsetsel(char *, Time);

#endif
