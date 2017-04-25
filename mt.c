#include "mt.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fontconfig/fontconfig.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include <X11/Xft/Xft.h>
#include <X11/cursorfont.h>

#if defined(__linux)
#include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <libutil.h>
#endif

#include "x.h"

char *argv0;

/* Arbitrary sizes */
#define UTF_INVALID 0xFFFD
#define ESC_BUF_SIZ (128 * UTF_SIZ)
#define ESC_ARG_SIZ 16
#define STR_BUF_SIZ ESC_BUF_SIZ
#define STR_ARG_SIZ ESC_ARG_SIZ

/* macros */
#define NUMMAXLEN(x) ((int)(sizeof(x) * 2.56 + 0.5) + 1)
#define DEFAULT(a, b) (a) = (a) ? (a) : (b)
#define ISCONTROLC0(c) (BETWEEN(c, 0, 0x1f) || (c) == '\177')
#define ISCONTROLC1(c) (BETWEEN(c, 0x80, 0x9f))
#define ISCONTROL(c) (ISCONTROLC0(c) || ISCONTROLC1(c))
#define ISDELIM(u) (utf8strchr(worddelimiters, u) != NULL)

/* constants */
#define ISO14755CMD "dmenu -w %lu -p codepoint: </dev/null"

enum cursor_movement { CURSOR_SAVE, CURSOR_LOAD };

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
typedef struct {
  char buf[ESC_BUF_SIZ]; /* raw string */
  int len;               /* raw string length */
  bool priv;
  int arg[ESC_ARG_SIZ];
  int narg; /* nb of args */
  char mode[2];
} CSIEscape;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
typedef struct {
  char type;             /* ESC type ... */
  char buf[STR_BUF_SIZ]; /* raw string */
  int len;               /* raw string length */
  char *args[STR_ARG_SIZ];
  int narg; /* nb of args */
} STREscape;

typedef struct {
  KeySym k;
  uint mask;
  char *s;
  /* three valued logic variables: 0 indifferent, 1 on, -1 off */
  signed char appkey;    /* application keypad */
  signed char appcursor; /* application cursor */
  signed char crlf;      /* crlf mode          */
} Key;

/* function definitions used in config.h */
static void clipcopy(const Arg *);
static void clippaste(const Arg *);
static void numlock(const Arg *);
static void selpaste(const Arg *);
static void zoom(const Arg *);
static void zoomabs(const Arg *);
static void zoomreset(const Arg *);
static void printsel(const Arg *);
static void printscreen(const Arg *);
static void iso14755(const Arg *);
static void toggleprinter(const Arg *);
static void sendbreak(const Arg *);

/* config.h for applying patches and the configuration. */
#include "config.h"

static void execsh(void);
static void stty(void);
static void sigchld(int);

static void csidump(void);
static void csihandle(void);
static void csiparse(void);
static void csireset(void);
static bool eschandle(uchar);
static void strdump(void);
static void strhandle(void);
static void strparse(void);
static void strreset(void);

static void tprinter(char *, size_t);
static void tdumpsel(void);
static void tdumpline(int);
static void tdump(void);
static void tclearregion(int, int, int, int);
static void tcursor(enum cursor_movement);
static void tdeletechar(int);
static void tdeleteline(int);
static void tinsertblank(int);
static void tinsertblankline(int);
static int tlinelen(int);
static void tmoveto(int, int);
static void tmoveato(int, int);
static void tnewline(bool);
static void tputtab(int);
static void tputc(Rune);
static void treset(void);
static void tresize(int, int);
static void tscrollup(int, int);
static void tscrolldown(int, int);
static void tsetattr(int *, int);
static void tsetchar(Rune, MTGlyph *, int, int);
static void tsetscroll(int, int);
static void tswapscreen(void);
static void tsetmode(bool, bool, int *, int);
static void tfulldirt(void);
static void techo(Rune);
static void tcontrolcode(uchar);
static void tdectest(char);
static void tdefutf8(char);
static int32_t tdefcolor(int *, int *, int);
static void tdeftran(char);
static void tstrsequence(uchar);

static void selscroll(int, int);
static void selsnap(int *, int *, int);

static Rune utf8decodebyte(char, size_t *);
static char utf8encodebyte(Rune, size_t);
static char *utf8strchr(char *s, Rune u);
static size_t utf8validate(Rune *, size_t);

static char *base64dec(const char *);

static ssize_t xwrite(int, const char *, size_t);
static void *xrealloc(void *, size_t);

/* Globals */
TermWindow win;
Term term;
Selection sel;
int cmdfd;
pid_t pid;
char **opt_cmd = NULL;
char *opt_class = NULL;
char *opt_embed = NULL;
char *opt_font = NULL;
char *opt_io = NULL;
char *opt_line = NULL;
char *opt_name = NULL;
char *opt_title = NULL;
int oldbutton = 3; /* button event on startup: 3 = release */

static CSIEscape csiescseq;
static STREscape strescseq;
static int iofd = 1;

char *usedfont = NULL;
double usedfontsize = 0;
double defaultfontsize = 0;

static uchar utfbyte[UTF_SIZ + 1] = {0x80, 0, 0xC0, 0xE0, 0xF0};
static uchar utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static Rune utfmin[UTF_SIZ + 1] = {0, 0, 0x80, 0x800, 0x10000};
static Rune utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

/* config.h array lengths */
size_t colornamelen = LEN(colorname);
size_t mshortcutslen = LEN(mshortcuts);
size_t shortcutslen = LEN(shortcuts);
size_t selmaskslen = LEN(selmasks);

ssize_t xwrite(int fd, const char *s, size_t len) {
  size_t aux = len;

  while (len > 0) {
    ssize_t r = write(fd, s, len);
    if (r < 0)
      return r;
    len -= r;
    s += r;
  }

  return aux;
}

void *xmalloc(size_t len) {
  void *p = malloc(len);

  if (!p)
    die("Out of memory\n");

  return p;
}

void *xrealloc(void *p, size_t len) {
  if ((p = realloc(p, len)) == NULL)
    die("Out of memory\n");

  return p;
}

char *xstrdup(char *s) {
  if ((s = strdup(s)) == NULL)
    die("Out of memory\n");

  return s;
}

size_t utf8decode(char *c, Rune *u, size_t clen) {
  *u = UTF_INVALID;
  if (!clen)
    return 0;
  size_t len;
  Rune udecoded = utf8decodebyte(c[0], &len);
  if (!BETWEEN(len, 1, UTF_SIZ))
    return 1;
  size_t type, i;
  for (i = 1; i < clen && i < len; ++i) {
    udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
    if (type != 0)
      return i;
  }
  if (i < len)
    return 0;
  *u = udecoded;
  utf8validate(u, len);

  return len;
}

Rune utf8decodebyte(char c, size_t *i) {
  for (*i = 0; *i < LEN(utfmask); ++(*i))
    if (((uchar)c & utfmask[*i]) == utfbyte[*i])
      return (uchar)c & ~utfmask[*i];

  return 0;
}

size_t utf8encode(Rune u, char *c) {
  size_t len = utf8validate(&u, 0);
  if (len > UTF_SIZ)
    return 0;

  for (size_t i = len - 1; i != 0; --i) {
    c[i] = utf8encodebyte(u, 0);
    u >>= 6;
  }
  c[0] = utf8encodebyte(u, len);

  return len;
}

char utf8encodebyte(Rune u, size_t i) { return utfbyte[i] | (u & ~utfmask[i]); }

char *utf8strchr(char *s, Rune u) {
  size_t len = strlen(s);
  Rune r;
  for (size_t i = 0, j = 0; i < len; i += j) {
    if (!(j = utf8decode(&s[i], &r, len - i)))
      break;
    if (r == u)
      return &(s[i]);
  }

  return NULL;
}

size_t utf8validate(Rune *u, size_t i) {
  if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
    *u = UTF_INVALID;
  for (i = 1; *u > utfmax[i]; ++i)
    ;

  return i;
}

static const char base64_digits[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  62, 0,  0,  0,  63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 0,  0,  0,  -1, 0,  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,  0,  0,  0,
    0,  0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0};

char *base64dec(const char *src) {
  size_t in_len = strlen(src);
  if (in_len % 4)
    return NULL;

  char *dst = xmalloc(in_len / 4 * 3 + 1);
  char *result = dst;
  while (*src) {
    int a = base64_digits[(unsigned char)*src++];
    int b = base64_digits[(unsigned char)*src++];
    int c = base64_digits[(unsigned char)*src++];
    int d = base64_digits[(unsigned char)*src++];

    *dst++ = (a << 2) | ((b & 0x30) >> 4);
    if (c == -1)
      break;
    *dst++ = ((b & 0x0f) << 4) | ((c & 0x3c) >> 2);
    if (d == -1)
      break;
    *dst++ = ((c & 0x03) << 6) | d;
  }
  *dst = '\0';
  return result;
}

void selinit(void) {
  clock_gettime(CLOCK_MONOTONIC, &sel.tclick1);
  clock_gettime(CLOCK_MONOTONIC, &sel.tclick2);
  sel.mode = SEL_IDLE;
  sel.snap = 0;
  sel.ob.x = -1;
  sel.primary = NULL;
  sel.clipboard = NULL;
}

int x2col(int x) {
  x -= borderpx;
  x /= win.cw;

  return LIMIT(x, 0, term.col - 1);
}

int y2row(int y) {
  y -= borderpx;
  y /= win.ch;

  return LIMIT(y, 0, term.row - 1);
}

int tlinelen(int y) {
  int i = term.col;

  if (term.line[y][i - 1].mode & ATTR_WRAP)
    return i;

  while (i > 0 && term.line[y][i - 1].u == ' ')
    --i;

  return i;
}

void selnormalize(void) {
  if (sel.type == SEL_REGULAR && sel.ob.y != sel.oe.y) {
    sel.nb.x = sel.ob.y < sel.oe.y ? sel.ob.x : sel.oe.x;
    sel.ne.x = sel.ob.y < sel.oe.y ? sel.oe.x : sel.ob.x;
  } else {
    sel.nb.x = MIN(sel.ob.x, sel.oe.x);
    sel.ne.x = MAX(sel.ob.x, sel.oe.x);
  }
  sel.nb.y = MIN(sel.ob.y, sel.oe.y);
  sel.ne.y = MAX(sel.ob.y, sel.oe.y);

  selsnap(&sel.nb.x, &sel.nb.y, -1);
  selsnap(&sel.ne.x, &sel.ne.y, +1);

  /* expand selection over line breaks */
  if (sel.type == SEL_RECTANGULAR)
    return;
  int i = tlinelen(sel.nb.y);
  if (i < sel.nb.x)
    sel.nb.x = i;
  if (tlinelen(sel.ne.y) <= sel.ne.x)
    sel.ne.x = term.col - 1;
}

bool selected(int x, int y) {
  if (sel.mode == SEL_EMPTY)
    return false;

  if (sel.type == SEL_RECTANGULAR)
    return BETWEEN(y, sel.nb.y, sel.ne.y) && BETWEEN(x, sel.nb.x, sel.ne.x);

  return BETWEEN(y, sel.nb.y, sel.ne.y) && (y != sel.nb.y || x >= sel.nb.x) &&
         (y != sel.ne.y || x <= sel.ne.x);
}

void selsnap(int *x, int *y, int direction) {
  switch (sel.snap) {
  case SNAP_WORD: {
    /*
     * Snap around if the word wraps around at the end or
     * beginning of a line.
     */
    MTGlyph *prevgp = &term.line[*y][*x];
    bool prevdelim = ISDELIM(prevgp->u);
    for (;;) {
      int newx = *x + direction;
      int newy = *y;
      if (!BETWEEN(newx, 0, term.col - 1)) {
        newy += direction;
        newx = (newx + term.col) % term.col;
        if (!BETWEEN(newy, 0, term.row - 1))
          break;

        int xt, yt;
        if (direction > 0)
          yt = *y, xt = *x;
        else
          yt = newy, xt = newx;
        if (!(term.line[yt][xt].mode & ATTR_WRAP))
          break;
      }

      if (newx >= tlinelen(newy))
        break;

      MTGlyph *gp = &term.line[newy][newx];
      bool delim = ISDELIM(gp->u);
      if (!(gp->mode & ATTR_WDUMMY) &&
          (delim != prevdelim || (delim && gp->u != prevgp->u)))
        break;

      *x = newx;
      *y = newy;
      prevgp = gp;
      prevdelim = delim;
    }
    break;
  }
  case SNAP_LINE:
    /*
     * Snap around if the the previous line or the current one
     * has set ATTR_WRAP at its end. Then the whole next or
     * previous line will be selected.
     */
    *x = (direction < 0) ? 0 : term.col - 1;
    if (direction < 0) {
      for (; *y > 0; *y += direction) {
        if (!(term.line[*y - 1][term.col - 1].mode & ATTR_WRAP)) {
          break;
        }
      }
    } else if (direction > 0) {
      for (; *y < term.row - 1; *y += direction) {
        if (!(term.line[*y][term.col - 1].mode & ATTR_WRAP)) {
          break;
        }
      }
    }
    break;
  case SNAP_NONE:
    break;
  }
}

char *getsel(void) {
  if (sel.ob.x == -1)
    return NULL;

  int bufsize = (term.col + 1) * (sel.ne.y - sel.nb.y + 1) * UTF_SIZ;
  char *str = xmalloc(bufsize);
  char *ptr = str;

  /* append every set & selected glyph to the selection */
  for (int y = sel.nb.y; y <= sel.ne.y; y++) {
    int linelen = tlinelen(y);
    if (linelen == 0) {
      *ptr++ = '\n';
      continue;
    }

    int lastx;
    MTGlyph *gp;
    if (sel.type == SEL_RECTANGULAR) {
      gp = &term.line[y][sel.nb.x];
      lastx = sel.ne.x;
    } else {
      gp = &term.line[y][sel.nb.y == y ? sel.nb.x : 0];
      lastx = (sel.ne.y == y) ? sel.ne.x : term.col - 1;
    }
    MTGlyph *last = &term.line[y][MIN(lastx, linelen - 1)];
    while (last >= gp && last->u == ' ')
      --last;

    for (; gp <= last; ++gp) {
      if (gp->mode & ATTR_WDUMMY)
        continue;

      ptr += utf8encode(gp->u, ptr);
    }

    /*
     * Copy and pasting of line endings is inconsistent
     * in the inconsistent terminal and GUI world.
     * The best solution seems like to produce '\n' when
     * something is copied from st and convert '\n' to
     * '\r', when something to be pasted is received by
     * st.
     * FIXME: Fix the computer world.
     */
    if ((y < sel.ne.y || lastx >= linelen) && !(last->mode & ATTR_WRAP))
      *ptr++ = '\n';
  }
  *ptr = 0;
  return str;
}

void selpaste(const Arg *dummy) { xselpaste(); }

void clipcopy(const Arg *dummy) { xclipcopy(); }

void clippaste(const Arg *dummy) { xclippaste(); }

void selclear(void) {
  if (sel.ob.x == -1)
    return;
  sel.mode = SEL_IDLE;
  sel.ob.x = -1;
  tsetdirt(sel.nb.y, sel.ne.y);
}

void die(const char *errstr, ...) {
  va_list ap;

  va_start(ap, errstr);
  vfprintf(stderr, errstr, ap);
  va_end(ap);
  exit(1);
}

void execsh(void) {
  errno = 0;
  const struct passwd *pw = getpwuid(getuid());

  if (pw == NULL) {
    if (errno)
      die("getpwuid:%s\n", strerror(errno));
    else
      die("who are you?\n");
  }

  char *sh = getenv("SHELL");
  if (sh == NULL)
    sh = (pw->pw_shell[0]) ? pw->pw_shell : shell;

  char *prog;
  if (opt_cmd)
    prog = opt_cmd[0];
  else if (utmp)
    prog = utmp;
  else
    prog = sh;
  char **args = (opt_cmd) ? opt_cmd : (char *[]){prog, NULL};

  unsetenv("COLUMNS");
  unsetenv("LINES");
  unsetenv("TERMCAP");
  setenv("LOGNAME", pw->pw_name, 1);
  setenv("USER", pw->pw_name, 1);
  setenv("SHELL", sh, 1);
  setenv("HOME", pw->pw_dir, 1);
  setenv("TERM", termname, 1);
  xsetenv();

  signal(SIGCHLD, SIG_DFL);
  signal(SIGHUP, SIG_DFL);
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGALRM, SIG_DFL);

  execvp(prog, args);
  _exit(1);
}

void sigchld(int unused) {
  int stat;
  pid_t p = waitpid(pid, &stat, WNOHANG);
  if (p < 0)
    die("Waiting for pid %hd failed: %s\n", pid, strerror(errno));

  if (pid != p)
    return;

  if (!WIFEXITED(stat) || WEXITSTATUS(stat))
    die("child finished with error '%d'\n", stat);
  exit(0);
}

void stty(void) {
  char cmd[_POSIX_ARG_MAX];
  size_t n = strlen(stty_args);
  if (n > sizeof(cmd) - 1)
    die("incorrect stty parameters\n");
  memcpy(cmd, stty_args, n);
  char *q = cmd + n;
  size_t siz = sizeof(cmd) - n;
  char *s;
  for (char **p = opt_cmd; p && (s = *p); ++p) {
    if ((n = strlen(s)) > siz - 1)
      die("stty parameter length too long\n");
    *q++ = ' ';
    memcpy(q, s, n);
    q += n;
    siz -= n + 1;
  }
  *q = '\0';
  if (system(cmd) != 0)
    perror("Couldn't call stty");
}

void ttynew(void) {
  if (opt_io) {
    term.mode |= MODE_PRINT;
    iofd = (!strcmp(opt_io, "-")) ? 1 : open(opt_io, O_WRONLY | O_CREAT, 0666);
    if (iofd < 0) {
      fprintf(stderr, "Error opening %s:%s\n", opt_io, strerror(errno));
    }
  }

  if (opt_line) {
    if ((cmdfd = open(opt_line, O_RDWR)) < 0)
      die("open line failed: %s\n", strerror(errno));
    dup2(cmdfd, 0);
    stty();
    return;
  }

  /* seems to work fine on linux, openbsd and freebsd */
  int m, s;
  struct winsize w = {term.row, term.col, 0, 0};
  if (openpty(&m, &s, NULL, NULL, &w) < 0)
    die("openpty failed: %s\n", strerror(errno));

  switch (pid = fork()) {
  case -1:
    die("fork failed\n");
    break;
  case 0:
    close(iofd);
    setsid(); /* create a new process group */
    dup2(s, 0);
    dup2(s, 1);
    dup2(s, 2);
    if (ioctl(s, TIOCSCTTY, NULL) < 0)
      die("ioctl TIOCSCTTY failed: %s\n", strerror(errno));
    close(s);
    close(m);
    execsh();
    break;
  default:
    close(s);
    cmdfd = m;
    signal(SIGCHLD, sigchld);
    break;
  }
}

size_t ttyread(void) {
  static char buf[BUFSIZ];
  static int buflen = 0;

  /* append read bytes to unprocessed bytes */
  int ret = read(cmdfd, buf + buflen, LEN(buf) - buflen);
  if (ret < 0)
    die("Couldn't read from shell: %s\n", strerror(errno));

  buflen += ret;
  char *ptr = buf;

  for (;;) {
    if (IS_SET(MODE_UTF8) && !IS_SET(MODE_SIXEL)) {
      /* process a complete utf8 char */
      Rune unicodep;
      int charsize = utf8decode(ptr, &unicodep, buflen);
      if (charsize == 0)
        break;
      tputc(unicodep);
      ptr += charsize;
      buflen -= charsize;

    } else {
      if (buflen <= 0)
        break;
      tputc(*ptr++ & 0xFF);
      buflen--;
    }
  }
  /* keep any uncomplete utf8 char for the next call */
  if (buflen > 0)
    memmove(buf, ptr, buflen);

  return ret;
}

void ttywrite(const char *s, size_t n) {
  /*
   * Remember that we are using a pty, which might be a modem line.
   * Writing too much will clog the line. That's why we are doing this
   * dance.
   * FIXME: Migrate the world to Plan 9.
   */
  fd_set wfd, rfd;
  size_t lim = 256;
  while (n > 0) {
    FD_ZERO(&wfd);
    FD_ZERO(&rfd);
    FD_SET(cmdfd, &wfd);
    FD_SET(cmdfd, &rfd);

    /* Check if we can write. */
    if (pselect(cmdfd + 1, &rfd, &wfd, NULL, NULL, NULL) < 0) {
      if (errno == EINTR)
        continue;
      die("select failed: %s\n", strerror(errno));
    }
    if (FD_ISSET(cmdfd, &wfd)) {
      /*
       * Only write the bytes written by ttywrite() or the
       * default of 256. This seems to be a reasonable value
       * for a serial line. Bigger values might clog the I/O.
       */
      ssize_t r = write(cmdfd, s, (n < lim) ? n : lim);
      if (r < 0)
        goto write_error;
      if (r < n) {
        /*
         * We weren't able to write out everything.
         * This means the buffer is getting full
         * again. Empty it.
         */
        if (n < lim)
          lim = ttyread();
        n -= r;
        s += r;
      } else {
        /* All bytes have been written. */
        break;
      }
    }
    if (FD_ISSET(cmdfd, &rfd))
      lim = ttyread();
  }
  return;

write_error:
  die("write error on tty: %s\n", strerror(errno));
}

void ttysend(char *s, size_t n) {
  ttywrite(s, n);
  if (!IS_SET(MODE_ECHO))
    return;

  char *lim = &s[n];
  int len;
  for (char *t = s; t < lim; t += len) {
    Rune u;
    if (IS_SET(MODE_UTF8) && !IS_SET(MODE_SIXEL)) {
      len = utf8decode(t, &u, n);
    } else {
      u = *t & 0xFF;
      len = 1;
    }
    if (len <= 0)
      break;
    techo(u);
    n -= len;
  }
}

void ttyresize(void) {
  struct winsize w;
  w.ws_row = term.row;
  w.ws_col = term.col;
  w.ws_xpixel = win.tw;
  w.ws_ypixel = win.th;
  if (ioctl(cmdfd, TIOCSWINSZ, &w) < 0)
    fprintf(stderr, "Couldn't set window size: %s\n", strerror(errno));
}

bool tattrset(int attr) {
  for (int i = 0; i < term.row - 1; i++) {
    for (int j = 0; j < term.col - 1; j++) {
      if (term.line[i][j].mode & attr)
        return true;
    }
  }

  return false;
}

void tsetdirt(int top, int bot) {
  LIMIT(top, 0, term.row - 1);
  LIMIT(bot, 0, term.row - 1);

  for (int i = top; i <= bot; i++)
    term.dirty[i] = true;
}

void tsetdirtattr(int attr) {
  for (int i = 0; i < term.row - 1; i++) {
    for (int j = 0; j < term.col - 1; j++) {
      if (term.line[i][j].mode & attr) {
        tsetdirt(i, i);
        break;
      }
    }
  }
}

void tfulldirt(void) { tsetdirt(0, term.row - 1); }

void tcursor(enum cursor_movement mode) {
  static TCursor c[2];
  bool alt = IS_SET(MODE_ALTSCREEN);

  if (mode == CURSOR_SAVE) {
    c[alt] = term.c;
  } else if (mode == CURSOR_LOAD) {
    term.c = c[alt];
    tmoveto(c[alt].x, c[alt].y);
  }
}

void treset(void) {
  term.c = (TCursor){{.mode = ATTR_NULL, .fg = defaultfg, .bg = defaultbg},
                     .x = 0,
                     .y = 0,
                     .state = CURSOR_DEFAULT};

  memset(term.tabs, 0, term.col * sizeof(*term.tabs));
  for (uint i = tabspaces; i < term.col; i += tabspaces)
    term.tabs[i] = true;
  term.top = 0;
  term.bot = term.row - 1;
  term.mode = MODE_WRAP | MODE_UTF8;
  memset(term.trantbl, CS_USA, sizeof(term.trantbl));
  term.charset = 0;

  for (uint i = 0; i < 2; i++) {
    tmoveto(0, 0);
    tcursor(CURSOR_SAVE);
    tclearregion(0, 0, term.col - 1, term.row - 1);
    tswapscreen();
  }
}

void tnew(int col, int row) {
  term = (Term){.c = {.attr = {.fg = defaultfg, .bg = defaultbg}}};
  tresize(col, row);
  term.numlock = true;

  treset();
}

void tswapscreen(void) {
  Line *tmp = term.line;

  term.line = term.alt;
  term.alt = tmp;
  term.mode ^= MODE_ALTSCREEN;
  tfulldirt();
}

void tscrolldown(int orig, int n) {
  LIMIT(n, 0, term.bot - orig + 1);

  tsetdirt(orig, term.bot - n);
  tclearregion(0, term.bot - n + 1, term.col - 1, term.bot);

  Line temp;
  for (int i = term.bot; i >= orig + n; i--) {
    temp = term.line[i];
    term.line[i] = term.line[i - n];
    term.line[i - n] = temp;
  }

  selscroll(orig, n);
}

void tscrollup(int orig, int n) {
  LIMIT(n, 0, term.bot - orig + 1);

  tclearregion(0, orig, term.col - 1, orig + n - 1);
  tsetdirt(orig + n, term.bot);

  Line temp;
  for (int i = orig; i <= term.bot - n; i++) {
    temp = term.line[i];
    term.line[i] = term.line[i + n];
    term.line[i + n] = temp;
  }

  selscroll(orig, -n);
}

void selscroll(int orig, int n) {
  if (sel.ob.x == -1)
    return;

  if (BETWEEN(sel.ob.y, orig, term.bot) || BETWEEN(sel.oe.y, orig, term.bot)) {
    if ((sel.ob.y += n) > term.bot || (sel.oe.y += n) < term.top) {
      selclear();
      return;
    }
    if (sel.type == SEL_RECTANGULAR) {
      if (sel.ob.y < term.top)
        sel.ob.y = term.top;
      if (sel.oe.y > term.bot)
        sel.oe.y = term.bot;
    } else {
      if (sel.ob.y < term.top) {
        sel.ob.y = term.top;
        sel.ob.x = 0;
      }
      if (sel.oe.y > term.bot) {
        sel.oe.y = term.bot;
        sel.oe.x = term.col;
      }
    }
    selnormalize();
  }
}

void tnewline(bool first_col) {
  int y = term.c.y;

  if (y == term.bot) {
    tscrollup(term.top, 1);
  } else {
    y++;
  }
  tmoveto(first_col ? 0 : term.c.x, y);
}

void csiparse(void) {
  csiescseq.narg = 0;
  char *p = csiescseq.buf;
  if (*p == '?') {
    csiescseq.priv = true;
    p++;
  }

  csiescseq.buf[csiescseq.len] = '\0';
  while (p < csiescseq.buf + csiescseq.len) {
    char *np = NULL;
    long int v = strtol(p, &np, 10);
    if (np == p)
      v = 0;
    if (v == LONG_MAX || v == LONG_MIN)
      v = -1;
    csiescseq.arg[csiescseq.narg++] = v;
    p = np;
    if (*p != ';' || csiescseq.narg == ESC_ARG_SIZ)
      break;
    p++;
  }
  csiescseq.mode[0] = *p++;
  csiescseq.mode[1] = (p < csiescseq.buf + csiescseq.len) ? *p : '\0';
}

/* for absolute user moves, when decom is set */
void tmoveato(int x, int y) {
  tmoveto(x, y + ((term.c.state & CURSOR_ORIGIN) ? term.top : 0));
}

void tmoveto(int x, int y) {
  int miny, maxy;
  if (term.c.state & CURSOR_ORIGIN) {
    miny = term.top;
    maxy = term.bot;
  } else {
    miny = 0;
    maxy = term.row - 1;
  }
  term.c.state &= ~CURSOR_WRAPNEXT;
  term.c.x = LIMIT(x, 0, term.col - 1);
  term.c.y = LIMIT(y, miny, maxy);
}

void tsetchar(Rune u, MTGlyph *attr, int x, int y) {
  static char *vt100_0[62] = {
      /* 0x41 - 0x7e */
      "↑", "↓", "→", "←", "█", "▚", "☃",      /* A - G */
      0,   0,   0,   0,   0,   0,   0,   0,   /* H - O */
      0,   0,   0,   0,   0,   0,   0,   0,   /* P - W */
      0,   0,   0,   0,   0,   0,   0,   " ", /* X - _ */
      "◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
      "␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
      "⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
      "│", "≤", "≥", "π", "≠", "£", "·",      /* x - ~ */
  };

  /*
   * The table is proudly stolen from rxvt.
   */
  if (term.trantbl[term.charset] == CS_GRAPHIC0 && BETWEEN(u, 0x41, 0x7e) &&
      vt100_0[u - 0x41])
    utf8decode(vt100_0[u - 0x41], &u, UTF_SIZ);

  if (term.line[y][x].mode & ATTR_WIDE) {
    if (x + 1 < term.col) {
      term.line[y][x + 1].u = ' ';
      term.line[y][x + 1].mode &= ~ATTR_WDUMMY;
    }
  } else if (term.line[y][x].mode & ATTR_WDUMMY) {
    term.line[y][x - 1].u = ' ';
    term.line[y][x - 1].mode &= ~ATTR_WIDE;
  }

  term.dirty[y] = true;
  term.line[y][x] = *attr;
  term.line[y][x].u = u;
}

void tclearregion(int x1, int y1, int x2, int y2) {
  int temp;
  if (x1 > x2)
    temp = x1, x1 = x2, x2 = temp;
  if (y1 > y2)
    temp = y1, y1 = y2, y2 = temp;

  LIMIT(x1, 0, term.col - 1);
  LIMIT(x2, 0, term.col - 1);
  LIMIT(y1, 0, term.row - 1);
  LIMIT(y2, 0, term.row - 1);

  for (int y = y1; y <= y2; y++) {
    term.dirty[y] = true;
    for (int x = x1; x <= x2; x++) {
      MTGlyph *gp = &term.line[y][x];
      if (selected(x, y))
        selclear();
      gp->fg = term.c.attr.fg;
      gp->bg = term.c.attr.bg;
      gp->mode = 0;
      gp->u = ' ';
    }
  }
}

void tdeletechar(int n) {
  LIMIT(n, 0, term.col - term.c.x);

  int dst = term.c.x;
  int src = term.c.x + n;
  int size = term.col - src;
  MTGlyph *line = term.line[term.c.y];

  memmove(&line[dst], &line[src], size * sizeof(MTGlyph));
  tclearregion(term.col - n, term.c.y, term.col - 1, term.c.y);
}

void tinsertblank(int n) {
  LIMIT(n, 0, term.col - term.c.x);

  int dst = term.c.x + n;
  int src = term.c.x;
  int size = term.col - dst;
  MTGlyph *line = term.line[term.c.y];

  memmove(&line[dst], &line[src], size * sizeof(MTGlyph));
  tclearregion(src, term.c.y, dst - 1, term.c.y);
}

void tinsertblankline(int n) {
  if (BETWEEN(term.c.y, term.top, term.bot))
    tscrolldown(term.c.y, n);
}

void tdeleteline(int n) {
  if (BETWEEN(term.c.y, term.top, term.bot))
    tscrollup(term.c.y, n);
}

int32_t tdefcolor(int *attr, int *npar, int l) {
  switch (attr[*npar + 1]) {
  case 2: /* direct color in RGB space */
    if (*npar + 4 >= l) {
      fprintf(stderr, "erresc(38): Incorrect number of parameters (%d)\n",
              *npar);
      break;
    }
    uint r = attr[*npar + 2];
    uint g = attr[*npar + 3];
    uint b = attr[*npar + 4];
    *npar += 4;
    if (!BETWEEN(r, 0, 255) || !BETWEEN(g, 0, 255) || !BETWEEN(b, 0, 255))
      fprintf(stderr, "erresc: bad rgb color (%u,%u,%u)\n", r, g, b);
    else
      return TRUECOLOR(r, g, b);
    break;
  case 5: /* indexed color */
    if (*npar + 2 >= l) {
      fprintf(stderr, "erresc(38): Incorrect number of parameters (%d)\n",
              *npar);
      break;
    }
    *npar += 2;
    if (!BETWEEN(attr[*npar], 0, 255))
      fprintf(stderr, "erresc: bad fgcolor %d\n", attr[*npar]);
    else
      return attr[*npar];
    break;
  case 0: /* implemented defined (only foreground) */
  case 1: /* transparent */
  case 3: /* direct color in CMY space */
  case 4: /* direct color in CMYK space */
  default:
    fprintf(stderr, "erresc(38): gfx attr %d unknown\n", attr[*npar]);
  }
  return -1;
}

void tsetattr(int *attr, int l) {
  for (int i = 0; i < l; i++) {
    switch (attr[i]) {
    case 0:
      term.c.attr.mode &=
          ~(ATTR_BOLD | ATTR_FAINT | ATTR_ITALIC | ATTR_UNDERLINE | ATTR_BLINK |
            ATTR_REVERSE | ATTR_INVISIBLE | ATTR_STRUCK);
      term.c.attr.fg = defaultfg;
      term.c.attr.bg = defaultbg;
      break;
    case 1:
      term.c.attr.mode |= ATTR_BOLD;
      break;
    case 2:
      term.c.attr.mode |= ATTR_FAINT;
      break;
    case 3:
      term.c.attr.mode |= ATTR_ITALIC;
      break;
    case 4:
      term.c.attr.mode |= ATTR_UNDERLINE;
      break;
    case 5: /* slow blink */
            /* FALLTHROUGH */
    case 6: /* rapid blink */
      term.c.attr.mode |= ATTR_BLINK;
      break;
    case 7:
      term.c.attr.mode |= ATTR_REVERSE;
      break;
    case 8:
      term.c.attr.mode |= ATTR_INVISIBLE;
      break;
    case 9:
      term.c.attr.mode |= ATTR_STRUCK;
      break;
    case 22:
      term.c.attr.mode &= ~(ATTR_BOLD | ATTR_FAINT);
      break;
    case 23:
      term.c.attr.mode &= ~ATTR_ITALIC;
      break;
    case 24:
      term.c.attr.mode &= ~ATTR_UNDERLINE;
      break;
    case 25:
      term.c.attr.mode &= ~ATTR_BLINK;
      break;
    case 27:
      term.c.attr.mode &= ~ATTR_REVERSE;
      break;
    case 28:
      term.c.attr.mode &= ~ATTR_INVISIBLE;
      break;
    case 29:
      term.c.attr.mode &= ~ATTR_STRUCK;
      break;
    case 38: {
      int32_t idx = tdefcolor(attr, &i, l);
      if (idx >= 0)
        term.c.attr.fg = idx;
      break;
    }
    case 39:
      term.c.attr.fg = defaultfg;
      break;
    case 48: {
      int32_t idx = tdefcolor(attr, &i, l);
      if (idx >= 0)
        term.c.attr.bg = idx;
      break;
    }
    case 49:
      term.c.attr.bg = defaultbg;
      break;
    default:
      if (BETWEEN(attr[i], 30, 37)) {
        term.c.attr.fg = attr[i] - 30;
      } else if (BETWEEN(attr[i], 40, 47)) {
        term.c.attr.bg = attr[i] - 40;
      } else if (BETWEEN(attr[i], 90, 97)) {
        term.c.attr.fg = attr[i] - 90 + 8;
      } else if (BETWEEN(attr[i], 100, 107)) {
        term.c.attr.bg = attr[i] - 100 + 8;
      } else {
        fprintf(stderr, "erresc(default): gfx attr %d unknown\n", attr[i]),
            csidump();
      }
      break;
    }
  }
}

void tsetscroll(int t, int b) {
  LIMIT(t, 0, term.row - 1);
  LIMIT(b, 0, term.row - 1);
  if (t > b) {
    int temp = t;
    t = b;
    b = temp;
  }
  term.top = t;
  term.bot = b;
}

void tsetmode(bool priv, bool set, int *args, int narg) {
  for (int *lim = args + narg; args < lim; ++args) {
    if (priv) {
      switch (*args) {
      case 1: /* DECCKM -- Cursor key */
        MODBIT(term.mode, set, MODE_APPCURSOR);
        break;
      case 5: { /* DECSCNM -- Reverse video */
        enum term_mode mode = term.mode;
        MODBIT(term.mode, set, MODE_REVERSE);
        if (mode != term.mode)
          redraw();
        break;
      }
      case 6: /* DECOM -- Origin */
        MODBIT(term.c.state, set, CURSOR_ORIGIN);
        tmoveato(0, 0);
        break;
      case 7: /* DECAWM -- Auto wrap */
        MODBIT(term.mode, set, MODE_WRAP);
        break;
      case 0:  /* Error (IGNORED) */
      case 2:  /* DECANM -- ANSI/VT52 (IGNORED) */
      case 3:  /* DECCOLM -- Column  (IGNORED) */
      case 4:  /* DECSCLM -- Scroll (IGNORED) */
      case 8:  /* DECARM -- Auto repeat (IGNORED) */
      case 18: /* DECPFF -- Printer feed (IGNORED) */
      case 19: /* DECPEX -- Printer extent (IGNORED) */
      case 42: /* DECNRCM -- National characters (IGNORED) */
      case 12: /* att610 -- Start blinking cursor (IGNORED) */
        break;
      case 25: /* DECTCEM -- Text Cursor Enable Mode */
        MODBIT(term.mode, !set, MODE_HIDE);
        break;
      case 9: /* X10 mouse compatibility mode */
        xsetpointermotion(false);
        MODBIT(term.mode, false, MODE_MOUSE);
        MODBIT(term.mode, set, MODE_MOUSEX10);
        break;
      case 1000: /* 1000: report button press */
        xsetpointermotion(false);
        MODBIT(term.mode, false, MODE_MOUSE);
        MODBIT(term.mode, set, MODE_MOUSEBTN);
        break;
      case 1002: /* 1002: report motion on button press */
        xsetpointermotion(false);
        MODBIT(term.mode, false, MODE_MOUSE);
        MODBIT(term.mode, set, MODE_MOUSEMOTION);
        break;
      case 1003: /* 1003: enable all mouse motions */
        xsetpointermotion(set);
        MODBIT(term.mode, false, MODE_MOUSE);
        MODBIT(term.mode, set, MODE_MOUSEMANY);
        break;
      case 1004: /* 1004: send focus events to tty */
        MODBIT(term.mode, set, MODE_FOCUS);
        break;
      case 1006: /* 1006: extended reporting mode */
        MODBIT(term.mode, set, MODE_MOUSESGR);
        break;
      case 1034:
        MODBIT(term.mode, set, MODE_8BIT);
        break;
      case 1049: /* swap screen & set/restore cursor as xterm */
        if (!allowaltscreen)
          break;
        tcursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
      /* FALLTHROUGH */
      case 47: /* swap screen */
      case 1047: {
        if (!allowaltscreen)
          break;
        bool alt = IS_SET(MODE_ALTSCREEN);
        if (alt) {
          tclearregion(0, 0, term.col - 1, term.row - 1);
        }
        if (set ^ alt)
          tswapscreen();
        if (*args != 1049)
          break;
      }
      /* FALLTHROUGH */
      case 1048:
        tcursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
        break;
      case 2004: /* 2004: bracketed paste mode */
        MODBIT(term.mode, set, MODE_BRCKTPASTE);
        break;
      /* Not implemented mouse modes. See comments there. */
      case 1001: /* mouse highlight mode; can hang the
                    terminal by design when implemented. */
      case 1005: /* UTF-8 mouse mode; will confuse
                    applications not supporting UTF-8
                    and luit. */
      case 1015: /* urxvt mangled mouse mode; incompatible
                    and can be mistaken for other control
                    codes. */
      default:
        fprintf(stderr, "erresc: unknown private set/reset mode %d\n", *args);
        break;
      }
    } else {
      switch (*args) {
      case 0: /* Error (IGNORED) */
        break;
      case 2: /* KAM -- keyboard action */
        MODBIT(term.mode, set, MODE_KBDLOCK);
        break;
      case 4: /* IRM -- Insertion-replacement */
        MODBIT(term.mode, set, MODE_INSERT);
        break;
      case 12: /* SRM -- Send/Receive */
        MODBIT(term.mode, !set, MODE_ECHO);
        break;
      case 20: /* LNM -- Linefeed/new line */
        MODBIT(term.mode, set, MODE_CRLF);
        break;
      default:
        fprintf(stderr, "erresc: unknown set/reset mode %d\n", *args);
        break;
      }
    }
  }
}

void csihandle(void) {
  switch (csiescseq.mode[0]) {
  default:
  unknown:
    fprintf(stderr, "erresc: unknown csi ");
    csidump();
    /* die(""); */
    break;
  case '@': /* ICH -- Insert <n> blank char */
    DEFAULT(csiescseq.arg[0], 1);
    tinsertblank(csiescseq.arg[0]);
    break;
  case 'A': /* CUU -- Cursor <n> Up */
    DEFAULT(csiescseq.arg[0], 1);
    tmoveto(term.c.x, term.c.y - csiescseq.arg[0]);
    break;
  case 'B': /* CUD -- Cursor <n> Down */
  case 'e': /* VPR --Cursor <n> Down */
    DEFAULT(csiescseq.arg[0], 1);
    tmoveto(term.c.x, term.c.y + csiescseq.arg[0]);
    break;
  case 'i': /* MC -- Media Copy */
    switch (csiescseq.arg[0]) {
    case 0:
      tdump();
      break;
    case 1:
      tdumpline(term.c.y);
      break;
    case 2:
      tdumpsel();
      break;
    case 4:
      term.mode &= ~MODE_PRINT;
      break;
    case 5:
      term.mode |= MODE_PRINT;
      break;
    }
    break;
  case 'c': /* DA -- Device Attributes */
    if (csiescseq.arg[0] == 0)
      ttywrite(vtiden, sizeof(vtiden) - 1);
    break;
  case 'C': /* CUF -- Cursor <n> Forward */
  case 'a': /* HPR -- Cursor <n> Forward */
    DEFAULT(csiescseq.arg[0], 1);
    tmoveto(term.c.x + csiescseq.arg[0], term.c.y);
    break;
  case 'D': /* CUB -- Cursor <n> Backward */
    DEFAULT(csiescseq.arg[0], 1);
    tmoveto(term.c.x - csiescseq.arg[0], term.c.y);
    break;
  case 'E': /* CNL -- Cursor <n> Down and first col */
    DEFAULT(csiescseq.arg[0], 1);
    tmoveto(0, term.c.y + csiescseq.arg[0]);
    break;
  case 'F': /* CPL -- Cursor <n> Up and first col */
    DEFAULT(csiescseq.arg[0], 1);
    tmoveto(0, term.c.y - csiescseq.arg[0]);
    break;
  case 'g': /* TBC -- Tabulation clear */
    switch (csiescseq.arg[0]) {
    case 0: /* clear current tab stop */
      term.tabs[term.c.x] = false;
      break;
    case 3: /* clear all the tabs */
      memset(term.tabs, 0, term.col * sizeof(*term.tabs));
      break;
    default:
      goto unknown;
    }
    break;
  case 'G': /* CHA -- Move to <col> */
  case '`': /* HPA */
    DEFAULT(csiescseq.arg[0], 1);
    tmoveto(csiescseq.arg[0] - 1, term.c.y);
    break;
  case 'H': /* CUP -- Move to <row> <col> */
  case 'f': /* HVP */
    DEFAULT(csiescseq.arg[0], 1);
    DEFAULT(csiescseq.arg[1], 1);
    tmoveato(csiescseq.arg[1] - 1, csiescseq.arg[0] - 1);
    break;
  case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
    DEFAULT(csiescseq.arg[0], 1);
    tputtab(csiescseq.arg[0]);
    break;
  case 'J': /* ED -- Clear screen */
    selclear();
    switch (csiescseq.arg[0]) {
    case 0: /* below */
      tclearregion(term.c.x, term.c.y, term.col - 1, term.c.y);
      if (term.c.y < term.row - 1) {
        tclearregion(0, term.c.y + 1, term.col - 1, term.row - 1);
      }
      break;
    case 1: /* above */
      if (term.c.y > 1)
        tclearregion(0, 0, term.col - 1, term.c.y - 1);
      tclearregion(0, term.c.y, term.c.x, term.c.y);
      break;
    case 2: /* all */
      tclearregion(0, 0, term.col - 1, term.row - 1);
      break;
    default:
      goto unknown;
    }
    break;
  case 'K': /* EL -- Clear line */
    switch (csiescseq.arg[0]) {
    case 0: /* right */
      tclearregion(term.c.x, term.c.y, term.col - 1, term.c.y);
      break;
    case 1: /* left */
      tclearregion(0, term.c.y, term.c.x, term.c.y);
      break;
    case 2: /* all */
      tclearregion(0, term.c.y, term.col - 1, term.c.y);
      break;
    }
    break;
  case 'S': /* SU -- Scroll <n> line up */
    DEFAULT(csiescseq.arg[0], 1);
    tscrollup(term.top, csiescseq.arg[0]);
    break;
  case 'T': /* SD -- Scroll <n> line down */
    DEFAULT(csiescseq.arg[0], 1);
    tscrolldown(term.top, csiescseq.arg[0]);
    break;
  case 'L': /* IL -- Insert <n> blank lines */
    DEFAULT(csiescseq.arg[0], 1);
    tinsertblankline(csiescseq.arg[0]);
    break;
  case 'l': /* RM -- Reset Mode */
    tsetmode(csiescseq.priv, false, csiescseq.arg, csiescseq.narg);
    break;
  case 'M': /* DL -- Delete <n> lines */
    DEFAULT(csiescseq.arg[0], 1);
    tdeleteline(csiescseq.arg[0]);
    break;
  case 'X': /* ECH -- Erase <n> char */
    DEFAULT(csiescseq.arg[0], 1);
    tclearregion(term.c.x, term.c.y, term.c.x + csiescseq.arg[0] - 1, term.c.y);
    break;
  case 'P': /* DCH -- Delete <n> char */
    DEFAULT(csiescseq.arg[0], 1);
    tdeletechar(csiescseq.arg[0]);
    break;
  case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
    DEFAULT(csiescseq.arg[0], 1);
    tputtab(-csiescseq.arg[0]);
    break;
  case 'd': /* VPA -- Move to <row> */
    DEFAULT(csiescseq.arg[0], 1);
    tmoveato(term.c.x, csiescseq.arg[0] - 1);
    break;
  case 'h': /* SM -- Set terminal mode */
    tsetmode(csiescseq.priv, true, csiescseq.arg, csiescseq.narg);
    break;
  case 'm': /* SGR -- Terminal attribute (color) */
    tsetattr(csiescseq.arg, csiescseq.narg);
    break;
  case 'n': /* DSR – Device Status Report (cursor position) */
    if (csiescseq.arg[0] == 6) {
      char buf[40];
      int len =
          snprintf(buf, sizeof(buf), "\033[%i;%iR", term.c.y + 1, term.c.x + 1);
      ttywrite(buf, len);
    }
    break;
  case 'r': /* DECSTBM -- Set Scrolling Region */
    if (csiescseq.priv) {
      goto unknown;
    } else {
      DEFAULT(csiescseq.arg[0], 1);
      DEFAULT(csiescseq.arg[1], term.row);
      tsetscroll(csiescseq.arg[0] - 1, csiescseq.arg[1] - 1);
      tmoveato(0, 0);
    }
    break;
  case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
    tcursor(CURSOR_SAVE);
    break;
  case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
    tcursor(CURSOR_LOAD);
    break;
  case ' ':
    switch (csiescseq.mode[1]) {
    case 'q': /* DECSCUSR -- Set Cursor Style */
      DEFAULT(csiescseq.arg[0], 1);
      if (!BETWEEN(csiescseq.arg[0], 0, 6)) {
        goto unknown;
      }
      win.cursor = csiescseq.arg[0];
      break;
    default:
      goto unknown;
    }
    break;
  }
}

void csidump(void) {
  fprintf(stderr, "ESC[");
  for (int i = 0; i < csiescseq.len; i++) {
    uint c = csiescseq.buf[i] & 0xff;
    if (isprint(c)) {
      putc(c, stderr);
    } else if (c == '\n') {
      fprintf(stderr, "(\\n)");
    } else if (c == '\r') {
      fprintf(stderr, "(\\r)");
    } else if (c == 0x1b) {
      fprintf(stderr, "(\\e)");
    } else {
      fprintf(stderr, "(%02x)", c);
    }
  }
  putc('\n', stderr);
}

void csireset(void) { memset(&csiescseq, 0, sizeof(csiescseq)); }

void strhandle(void) {
  term.esc &= ~(ESC_STR_END | ESC_STR);
  strparse();
  int narg = strescseq.narg;
  int par = narg ? atoi(strescseq.args[0]) : 0;

  char *p = NULL;
  switch (strescseq.type) {
  case ']': /* OSC -- Operating System Command */
    switch (par) {
    case 0:
    case 1:
    case 2:
      if (narg > 1)
        xsettitle(strescseq.args[1]);
      return;
    case 52:
      if (narg > 2) {
        char *dec;

        dec = base64dec(strescseq.args[2]);
        if (dec) {
          xsetsel(dec, CurrentTime);
          clipcopy(NULL);
        } else {
          fprintf(stderr, "erresc: invalid base64\n");
        }
      }
      return;
    case 4: /* color set */
      if (narg < 3)
        break;
      p = strescseq.args[2];
    /* FALLTHROUGH */
    case 104: { /* color reset, here p = NULL */
      int j = (narg > 1) ? atoi(strescseq.args[1]) : -1;
      if (!xsetcolorname(j, p)) {
        fprintf(stderr, "erresc: invalid color %s\n", p);
      } else {
        /*
         * TODO if defaultbg color is changed, borders
         * are dirty
         */
        redraw();
      }
      return;
    }
    break;
    }
  case 'k': /* old title set compatibility */
    xsettitle(strescseq.args[0]);
    return;
  case 'P': /* DCS -- Device Control String */
    term.mode |= ESC_DCS;
  case '_': /* APC -- Application Program Command */
  case '^': /* PM -- Privacy Message */
    return;
  }

  fprintf(stderr, "erresc: unknown str ");
  strdump();
}

void strparse(void) {
  strescseq.narg = 0;
  strescseq.buf[strescseq.len] = '\0';

  char *p = strescseq.buf;
  if (*p == '\0')
    return;

  while (strescseq.narg < STR_ARG_SIZ) {
    strescseq.args[strescseq.narg++] = p;
    while (*p != ';' && *p != '\0')
      ++p;
    if (*p == '\0')
      return;
    *p++ = '\0';
  }
}

void strdump(void) {
  fprintf(stderr, "ESC%c", strescseq.type);
  for (int i = 0; i < strescseq.len; i++) {
    uint c = strescseq.buf[i] & 0xff;
    if (c == '\0') {
      putc('\n', stderr);
      return;
    } else if (isprint(c)) {
      putc(c, stderr);
    } else if (c == '\n') {
      fprintf(stderr, "(\\n)");
    } else if (c == '\r') {
      fprintf(stderr, "(\\r)");
    } else if (c == 0x1b) {
      fprintf(stderr, "(\\e)");
    } else {
      fprintf(stderr, "(%02x)", c);
    }
  }
  fprintf(stderr, "ESC\\\n");
}

void strreset(void) { memset(&strescseq, 0, sizeof(strescseq)); }

void sendbreak(const Arg *arg) {
  if (tcsendbreak(cmdfd, 0))
    perror("Error sending break");
}

void tprinter(char *s, size_t len) {
  if (iofd != -1 && xwrite(iofd, s, len) < 0) {
    fprintf(stderr, "Error writing in %s:%s\n", opt_io, strerror(errno));
    close(iofd);
    iofd = -1;
  }
}

void iso14755(const Arg *arg) {
  char cmd[sizeof(ISO14755CMD) + NUMMAXLEN(xwinid())];
  snprintf(cmd, sizeof(cmd), ISO14755CMD, xwinid());
  FILE *p = popen(cmd, "r");
  if (!p)
    return;

  char codepoint[9];
  char *us = fgets(codepoint, sizeof(codepoint), p);
  pclose(p);

  if (!us || *us == '\0' || *us == '-' || strlen(us) > 7)
    return;
  char *e;
  unsigned long utf32 = strtoul(us, &e, 16);
  if (utf32 == ULONG_MAX || (*e != '\n' && *e != '\0'))
    return;

  char uc[UTF_SIZ];
  ttysend(uc, utf8encode(utf32, uc));
}

void toggleprinter(const Arg *arg) { term.mode ^= MODE_PRINT; }

void printscreen(const Arg *arg) { tdump(); }

void printsel(const Arg *arg) { tdumpsel(); }

void tdumpsel(void) {
  char *ptr = getsel();
  if (ptr) {
    tprinter(ptr, strlen(ptr));
    free(ptr);
  }
}

void tdumpline(int n) {
  MTGlyph *bp = &term.line[n][0];
  MTGlyph *end = &bp[MIN(tlinelen(n), term.col) - 1];
  if (bp != end || bp->u != ' ') {
    char buf[UTF_SIZ];
    for (; bp <= end; ++bp)
      tprinter(buf, utf8encode(bp->u, buf));
  }
  tprinter("\n", 1);
}

void tdump(void) {
  for (int i = 0; i < term.row; ++i)
    tdumpline(i);
}

void tputtab(int n) {
  uint x = term.c.x;

  if (n > 0) {
    while (x < term.col && n--)
      for (++x; x < term.col && !term.tabs[x]; ++x)
        /* nothing */;
  } else if (n < 0) {
    while (x > 0 && n++)
      for (--x; x > 0 && !term.tabs[x]; --x)
        /* nothing */;
  }
  term.c.x = LIMIT(x, 0, term.col - 1);
}

void techo(Rune u) {
  if (ISCONTROL(u)) { /* control code */
    if (u & 0x80) {
      u &= 0x7f;
      tputc('^');
      tputc('[');
    } else if (u != '\n' && u != '\r' && u != '\t') {
      u ^= 0x40;
      tputc('^');
    }
  }
  tputc(u);
}

void tdefutf8(char ascii) {
  if (ascii == 'G')
    term.mode |= MODE_UTF8;
  else if (ascii == '@')
    term.mode &= ~MODE_UTF8;
}

void tdeftran(char ascii) {
  static char cs[] = "0B";
  static enum charset vcs[] = {CS_GRAPHIC0, CS_USA};

  char *p = strchr(cs, ascii);
  if (p == NULL) {
    fprintf(stderr, "esc unhandled charset: ESC ( %c\n", ascii);
  } else {
    term.trantbl[term.icharset] = vcs[p - cs];
  }
}

void tdectest(char c) {
  if (c == '8') { /* DEC screen alignment test. */
    for (int x = 0; x < term.col; ++x) {
      for (int y = 0; y < term.row; ++y)
        tsetchar('E', &term.c.attr, x, y);
    }
  }
}

void tstrsequence(uchar c) {
  strreset();

  switch (c) {
  case 0x90: /* DCS -- Device Control String */
    c = 'P';
    term.esc |= ESC_DCS;
    break;
  case 0x9f: /* APC -- Application Program Command */
    c = '_';
    break;
  case 0x9e: /* PM -- Privacy Message */
    c = '^';
    break;
  case 0x9d: /* OSC -- Operating System Command */
    c = ']';
    break;
  }
  strescseq.type = c;
  term.esc |= ESC_STR;
}

void tcontrolcode(uchar ascii) {
  switch (ascii) {
  case '\t': /* HT */
    tputtab(1);
    return;
  case '\b': /* BS */
    tmoveto(term.c.x - 1, term.c.y);
    return;
  case '\r': /* CR */
    tmoveto(0, term.c.y);
    return;
  case '\f': /* LF */
  case '\v': /* VT */
  case '\n': /* LF */
    /* go to first col if the mode is set */
    tnewline(IS_SET(MODE_CRLF));
    return;
  case '\a': /* BEL */
    if (term.esc & ESC_STR_END) {
      /* backwards compatibility to xterm */
      strhandle();
    } else {
      if (!(win.state & WIN_FOCUSED))
        xseturgency(true);
      if (bellvolume)
        xbell(bellvolume);
    }
    break;
  case '\033': /* ESC */
    csireset();
    term.esc &= ~(ESC_CSI | ESC_ALTCHARSET | ESC_TEST);
    term.esc |= ESC_START;
    return;
  case '\016': /* SO (LS1 -- Locking shift 1) */
  case '\017': /* SI (LS0 -- Locking shift 0) */
    term.charset = 1 - (ascii - '\016');
    return;
  case '\032': /* SUB */
    tsetchar('?', &term.c.attr, term.c.x, term.c.y);
  case '\030': /* CAN */
    csireset();
    break;
  case '\005': /* ENQ (IGNORED) */
  case '\000': /* NUL (IGNORED) */
  case '\021': /* XON (IGNORED) */
  case '\023': /* XOFF (IGNORED) */
  case 0177:   /* DEL (IGNORED) */
    return;
  case 0x80: /* TODO: PAD */
  case 0x81: /* TODO: HOP */
  case 0x82: /* TODO: BPH */
  case 0x83: /* TODO: NBH */
  case 0x84: /* TODO: IND */
    break;
  case 0x85:     /* NEL -- Next line */
    tnewline(true); /* always go to first col */
    break;
  case 0x86: /* TODO: SSA */
  case 0x87: /* TODO: ESA */
    break;
  case 0x88: /* HTS -- Horizontal tab stop */
    term.tabs[term.c.x] = true;
    break;
  case 0x89: /* TODO: HTJ */
  case 0x8a: /* TODO: VTS */
  case 0x8b: /* TODO: PLD */
  case 0x8c: /* TODO: PLU */
  case 0x8d: /* TODO: RI */
  case 0x8e: /* TODO: SS2 */
  case 0x8f: /* TODO: SS3 */
  case 0x91: /* TODO: PU1 */
  case 0x92: /* TODO: PU2 */
  case 0x93: /* TODO: STS */
  case 0x94: /* TODO: CCH */
  case 0x95: /* TODO: MW */
  case 0x96: /* TODO: SPA */
  case 0x97: /* TODO: EPA */
  case 0x98: /* TODO: SOS */
  case 0x99: /* TODO: SGCI */
    break;
  case 0x9a: /* DECID -- Identify Terminal */
    ttywrite(vtiden, sizeof(vtiden) - 1);
    break;
  case 0x9b: /* TODO: CSI */
  case 0x9c: /* TODO: ST */
    break;
  case 0x90: /* DCS -- Device Control String */
  case 0x9d: /* OSC -- Operating System Command */
  case 0x9e: /* PM -- Privacy Message */
  case 0x9f: /* APC -- Application Program Command */
    tstrsequence(ascii);
    return;
  }
  /* only CAN, SUB, \a and C1 chars interrupt a sequence */
  term.esc &= ~(ESC_STR_END | ESC_STR);
}

/*
 * returns true when the sequence is finished and it hasn't to read
 * more characters for this sequence
 */
bool eschandle(uchar ascii) {
  switch (ascii) {
  case '[':
    term.esc |= ESC_CSI;
    return false;
  case '#':
    term.esc |= ESC_TEST;
    return false;
  case '%':
    term.esc |= ESC_UTF8;
    return false;
  case 'P': /* DCS -- Device Control String */
  case '_': /* APC -- Application Program Command */
  case '^': /* PM -- Privacy Message */
  case ']': /* OSC -- Operating System Command */
  case 'k': /* old title set compatibility */
    tstrsequence(ascii);
    return false;
  case 'n': /* LS2 -- Locking shift 2 */
  case 'o': /* LS3 -- Locking shift 3 */
    term.charset = 2 + (ascii - 'n');
    break;
  case '(': /* GZD4 -- set primary charset G0 */
  case ')': /* G1D4 -- set secondary charset G1 */
  case '*': /* G2D4 -- set tertiary charset G2 */
  case '+': /* G3D4 -- set quaternary charset G3 */
    term.icharset = ascii - '(';
    term.esc |= ESC_ALTCHARSET;
    return false;
  case 'D': /* IND -- Linefeed */
    if (term.c.y == term.bot) {
      tscrollup(term.top, 1);
    } else {
      tmoveto(term.c.x, term.c.y + 1);
    }
    break;
  case 'E':      /* NEL -- Next line */
    tnewline(true); /* always go to first col */
    break;
  case 'H': /* HTS -- Horizontal tab stop */
    term.tabs[term.c.x] = true;
    break;
  case 'M': /* RI -- Reverse index */
    if (term.c.y == term.top) {
      tscrolldown(term.top, 1);
    } else {
      tmoveto(term.c.x, term.c.y - 1);
    }
    break;
  case 'Z': /* DECID -- Identify Terminal */
    ttywrite(vtiden, sizeof(vtiden) - 1);
    break;
  case 'c': /* RIS -- Reset to inital state */
    treset();
    resettitle();
    xloadcols();
    break;
  case '=': /* DECPAM -- Application keypad */
    term.mode |= MODE_APPKEYPAD;
    break;
  case '>': /* DECPNM -- Normal keypad */
    term.mode &= ~MODE_APPKEYPAD;
    break;
  case '7': /* DECSC -- Save Cursor */
    tcursor(CURSOR_SAVE);
    break;
  case '8': /* DECRC -- Restore Cursor */
    tcursor(CURSOR_LOAD);
    break;
  case '\\': /* ST -- String Terminator */
    if (term.esc & ESC_STR_END)
      strhandle();
    break;
  default:
    fprintf(stderr, "erresc: unknown sequence ESC 0x%02X '%c'\n", (uchar)ascii,
            isprint(ascii) ? ascii : '.');
    break;
  }
  return true;
}

void tputc(Rune u) {
  char c[UTF_SIZ];
  bool control = ISCONTROL(u);
  int width, len;
  if (!IS_SET(MODE_UTF8) && !IS_SET(MODE_SIXEL)) {
    c[0] = u;
    width = len = 1;
  } else {
    len = utf8encode(u, c);
    if (!control && (width = wcwidth(u)) == -1) {
      memcpy(c, "\357\277\275", 4); /* UTF_INVALID */
      width = 1;
    }
  }

  if (IS_SET(MODE_PRINT))
    tprinter(c, len);

  /*
   * STR sequence must be checked before anything else
   * because it uses all following characters until it
   * receives a ESC, a SUB, a ST or any other C1 control
   * character.
   */
  if (term.esc & ESC_STR) {
    if (u == '\a' || u == 030 || u == 032 || u == 033 || ISCONTROLC1(u)) {
      term.esc &= ~(ESC_START | ESC_STR | ESC_DCS);
      if (IS_SET(MODE_SIXEL)) {
        /* TODO: render sixel */;
        term.mode &= ~MODE_SIXEL;
        return;
      }
      term.esc |= ESC_STR_END;
      goto check_control_code;
    }

    if (IS_SET(MODE_SIXEL)) {
      /* TODO: implement sixel mode */
      return;
    }
    if (term.esc & ESC_DCS && strescseq.len == 0 && u == 'q')
      term.mode |= MODE_SIXEL;

    if (strescseq.len + len >= sizeof(strescseq.buf) - 1) {
      /*
       * Here is a bug in terminals. If the user never sends
       * some code to stop the str or esc command, then st
       * will stop responding. But this is better than
       * silently failing with unknown characters. At least
       * then users will report back.
       *
       * In the case users ever get fixed, here is the code:
       */
      /*
       * term.esc = 0;
       * strhandle();
       */
      return;
    }

    memmove(&strescseq.buf[strescseq.len], c, len);
    strescseq.len += len;
    return;
  }

check_control_code:
  /*
   * Actions of control codes must be performed as soon they arrive
   * because they can be embedded inside a control sequence, and
   * they must not cause conflicts with sequences.
   */
  if (control) {
    tcontrolcode(u);
    /*
     * control codes are not shown ever
     */
    return;
  } else if (term.esc & ESC_START) {
    if (term.esc & ESC_CSI) {
      csiescseq.buf[csiescseq.len++] = u;
      if (BETWEEN(u, 0x40, 0x7E) ||
          csiescseq.len >= sizeof(csiescseq.buf) - 1) {
        term.esc = 0;
        csiparse();
        csihandle();
      }
      return;
    } else if (term.esc & ESC_UTF8) {
      tdefutf8(u);
    } else if (term.esc & ESC_ALTCHARSET) {
      tdeftran(u);
    } else if (term.esc & ESC_TEST) {
      tdectest(u);
    } else {
      if (!eschandle(u))
        return;
      /* sequence already finished */
    }
    term.esc = 0;
    /*
     * All characters which form part of a sequence are not
     * printed
     */
    return;
  }
  if (sel.ob.x != -1 && BETWEEN(term.c.y, sel.ob.y, sel.oe.y))
    selclear();

  MTGlyph *gp = &term.line[term.c.y][term.c.x];
  if (IS_SET(MODE_WRAP) && (term.c.state & CURSOR_WRAPNEXT)) {
    gp->mode |= ATTR_WRAP;
    tnewline(true);
    gp = &term.line[term.c.y][term.c.x];
  }

  if (IS_SET(MODE_INSERT) && term.c.x + width < term.col)
    memmove(gp + width, gp, (term.col - term.c.x - width) * sizeof(MTGlyph));

  if (term.c.x + width > term.col) {
    tnewline(true);
    gp = &term.line[term.c.y][term.c.x];
  }

  tsetchar(u, &term.c.attr, term.c.x, term.c.y);

  if (width == 2) {
    gp->mode |= ATTR_WIDE;
    if (term.c.x + 1 < term.col) {
      gp[1].u = '\0';
      gp[1].mode = ATTR_WDUMMY;
    }
  }
  if (term.c.x + width < term.col) {
    tmoveto(term.c.x + width, term.c.y);
  } else {
    term.c.state |= CURSOR_WRAPNEXT;
  }
}

void tresize(int col, int row) {
  if (col < 1 || row < 1) {
    fprintf(stderr, "tresize: error resizing to %dx%d\n", col, row);
    return;
  }

  /*
   * slide screen to keep cursor where we expect it -
   * tscrollup would work here, but we can optimize to
   * memmove because we're freeing the earlier lines
   */
  int j;
  for (j = 0; j <= term.c.y - row; j++) {
    free(term.line[j]);
    free(term.alt[j]);
  }
  /* ensure that both src and dst are not NULL */
  if (j > 0) {
    memmove(term.line, term.line + j, row * sizeof(Line));
    memmove(term.alt, term.alt + j, row * sizeof(Line));
  }
  for (j += row; j < term.row; j++) {
    free(term.line[j]);
    free(term.alt[j]);
  }

  /* resize to new width */
  term.specbuf = xrealloc(term.specbuf, col * sizeof(XftGlyphFontSpec));

  /* resize to new height */
  term.line = xrealloc(term.line, row * sizeof(Line));
  term.alt = xrealloc(term.alt, row * sizeof(Line));
  term.dirty = xrealloc(term.dirty, row * sizeof(*term.dirty));
  term.tabs = xrealloc(term.tabs, col * sizeof(*term.tabs));

  int minrow = MIN(row, term.row);
  int mincol = MIN(col, term.col);
  /* resize each row to new width, zero-pad if needed */
  for (int i = 0; i < minrow; i++) {
    term.line[i] = xrealloc(term.line[i], col * sizeof(MTGlyph));
    term.alt[i] = xrealloc(term.alt[i], col * sizeof(MTGlyph));
  }

  /* allocate any new rows */
  for (int i = minrow; i < row; i++) {
    term.line[i] = xmalloc(col * sizeof(MTGlyph));
    term.alt[i] = xmalloc(col * sizeof(MTGlyph));
  }
  if (col > term.col) {
    bool *bp = term.tabs + term.col;

    memset(bp, 0, sizeof(*term.tabs) * (col - term.col));
    while (--bp > term.tabs && !*bp)
      /* nothing */;
    for (bp += tabspaces; bp < term.tabs + col; bp += tabspaces)
      *bp = true;
  }
  /* update terminal size */
  term.col = col;
  term.row = row;
  /* reset scrolling region */
  tsetscroll(0, row - 1);
  /* make use of the LIMIT in tmoveto */
  tmoveto(term.c.x, term.c.y);
  /* Clearing both screens (it makes dirty all lines) */
  TCursor c = term.c;
  for (int i = 0; i < 2; i++) {
    if (mincol < col && 0 < minrow) {
      tclearregion(mincol, 0, col - 1, minrow - 1);
    }
    if (0 < col && minrow < row) {
      tclearregion(0, minrow, col - 1, row - 1);
    }
    tswapscreen();
    tcursor(CURSOR_LOAD);
  }
  term.c = c;
}

void zoom(const Arg *arg) {
  Arg larg = {.f = usedfontsize + arg->f};
  zoomabs(&larg);
}

void zoomabs(const Arg *arg) {
  xunloadfonts();
  xloadfonts(usedfont, arg->f);
  cresize(0, 0);
  ttyresize();
  redraw();
  xhints();
}

void zoomreset(const Arg *arg) {
  if (defaultfontsize > 0) {
    Arg larg = {.f = defaultfontsize};
    zoomabs(&larg);
  }
}

void resettitle(void) { xsettitle(opt_title ? opt_title : "mt"); }

void redraw(void) {
  tfulldirt();
  draw();
}

bool match(uint mask, uint state) {
  return mask == XK_ANY_MOD || mask == (state & ~ignoremod);
}

void numlock(const Arg *dummy) { term.numlock = !term.numlock; }

char *kmap(KeySym k, uint state) {
  /* Check for mapped keys out of X11 function keys. */
  int i;
  for (i = 0; i < LEN(mappedkeys); i++) {
    if (mappedkeys[i] == k)
      break;
  }
  if (i == LEN(mappedkeys)) {
    if ((k & 0xFFFF) < 0xFD00)
      return NULL;
  }

  for (Key *kp = key; kp < key + LEN(key); kp++) {
    if (kp->k != k)
      continue;

    if (!match(kp->mask, state))
      continue;

    if (IS_SET(MODE_APPKEYPAD) ? kp->appkey < 0 : kp->appkey > 0)
      continue;
    if (term.numlock && kp->appkey == 2)
      continue;

    if (IS_SET(MODE_APPCURSOR) ? kp->appcursor < 0 : kp->appcursor > 0)
      continue;

    if (IS_SET(MODE_CRLF) ? kp->crlf < 0 : kp->crlf > 0)
      continue;

    return kp->s;
  }

  return NULL;
}

void cresize(int width, int height) {
  if (width != 0)
    win.w = width;
  if (height != 0)
    win.h = height;

  int col = (win.w - 2 * borderpx) / win.cw;
  int row = (win.h - 2 * borderpx) / win.ch;

  tresize(col, row);
  xresize(col, row);
}

void usage(void) {
  die("usage: %s [-aiv] [-c class] [-f font] [-g geometry]"
      " [-n name] [-o file]\n"
      "          [-T title] [-t title] [-w windowid]"
      " [[-e] command [args ...]]\n"
      "       %s [-aiv] [-c class] [-f font] [-g geometry]"
      " [-n name] [-o file]\n"
      "          [-T title] [-t title] [-w windowid] -l line"
      " [stty_args ...]\n",
      argv0, argv0);
}
