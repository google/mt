// Font specification in fontconfig format.
// See http://freedesktop.org/software/fontconfig/fontconfig-user.html
char font[] = "RobotoMono Nerd Font:weight=Light:size=11:antialias=true:autohint=true";

// Margin between window edges and console area.
int borderpx = 2;

// Shell program to run when mt starts.
// The following sources are consulted in order:
//   1. program passed with -e
//   2. the `utmp` option
//   3. POSIX $SHELL environment variable
//   4. user's shell in /etc/passwd
//   5. the `shell` option
static char shell[] = "/bin/sh";
static char *utmp = NULL;

// Kerning / character bounding-box multipliers.
float cwscale = 1.0;
float chscale = 1.0;

// Characters that separate words.
// This affects text selection.
static char worddelimiters[] = " ";

// Timeouts for selection gestures, in milliseconds.
unsigned int doubleclicktimeout = 300;
unsigned int tripleclicktimeout = 600;

// Enable the "alternate screen" feature?
// This allows fullscreen editors etc to restore the screen contents on exit.
int allowaltscreen = 1;

// Maximum redraw rate for events triggered by the UI (keystrokes, mouse).
unsigned int xfps = 120;
// Maximum redraw rate for events triggered by the terminal (program output).
unsigned int actionfps = 30;

// Blink period in ms, for text with the blinking attribute.
// 0 disables blinking.
unsigned int blinktimeout = 800;

// Width of the underline and vertical bar cursors, in pixels.
unsigned int cursorthickness = 2;

// Ring the bell for the ^G character.
// XkbBell() is used, the volume can be controlled with xset.
static int bell = 0;

// Value of the $TERM variable.
char termname[] = "xterm-256color";

// Available terminal colors (X color names, or #123abc hex codes).
// Basic colors (required):
//  0-7:     basic colors - black, red, green, yellow, blue, magenta, cyan, grey
//  8-15:    bright versions of the above colors
// RGB colors (default to conventional values):
//  16-231:  RGB colors (6 levels of each)
//  232-255: additional shades of grey
// Additional colors (optional):
//  256+:    colors not accessible to applications (cursor colors etc)
const char *colorname[] = {
    "black", "red3", "green3", "yellow3",
    "blue2", "magenta3", "cyan3", "gray90",

    "gray50", "red", "green", "yellow",
    "#5c5cff", "magenta", "cyan", "white",

    // FIXME: this is silly.
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,

    "#cccccc", "#555555",
};

// Default foreground and background colors.
unsigned int defaultfg = 7;
unsigned int defaultbg = 0;

// Cursor color.
unsigned int defaultcs = 256;
// Cursor color in reverse mode and when selecting.
unsigned int defaultrcs = 257;

// Default shape of cursor.
//  0-2: Block ("█")
//  3-4: Underline ("_")
//  5-6: Bar ("|")
//  7: Snowman ("☃")
// 0, 1, 3, 5 are desinated blinking variants, but blinking is not implemented.
unsigned int cursorshape = 2;

// Default terminal window size.
unsigned int cols = 80;
unsigned int rows = 24;

// Default colour and shape of the mouse cursor.
unsigned int mouseshape = XC_xterm;
unsigned int mousefg = 7;
unsigned int mousebg = 0;

// Color indicating the active style (bold, italic) isn't supported by the font.
unsigned int defaultattr = 11;

// Mouse shortcuts.
// The default values map the scroll wheel (Button4/5) to ^Y/^E (vim commands).
// Note that mapping Button1 will interfere with selection.
MouseShortcut mshortcuts[] = {
  /* button               mask            string */
  { Button4,              XK_ANY_MOD,     "\031" },
  { Button5,              XK_ANY_MOD,     "\005" },
};

// Keyboard shortcuts that trigger internal functions.
Shortcut shortcuts[] = {
  // mask                      keysym          function        argument
  { XK_ANY_MOD,                XK_Break,       sendbreak,      0 },
  { ControlMask,               XK_Print,       toggleprinter,  0 },
  { ShiftMask,                 XK_Print,       printscreen,    0 },
  { XK_ANY_MOD,                XK_Print,       printsel,       0 },
  { (ControlMask | ShiftMask), XK_Prior,       zoom,           +1.f },
  { (ControlMask | ShiftMask), XK_Next,        zoom,           -1.f },
  { (ControlMask | ShiftMask), XK_Home,        zoomreset,      0.f },
  { (ControlMask | ShiftMask), XK_C,           clipcopy,       0 },
  { (ControlMask | ShiftMask), XK_V,           clippaste,      0 },
  { (ControlMask | ShiftMask), XK_Y,           selpaste,       0 },
  { (ControlMask | ShiftMask), XK_Num_Lock,    numlock,        0 },
  { (ControlMask | ShiftMask), XK_I,           iso14755,       0 },
};

// Keys outside the X11 function key range (0xFD00 - 0xFFFF) with shortcuts.
// By default, shortcuts in `key` are only scanned for KeySyms in this range.
static KeySym mappedkeys[] = {(KeySym)-1};

// Modifiers to ignore when matching keyboard and mouse events.
// By default, numlock and keyboard layout are ignored.
static uint ignoremod = Mod2Mask | XK_SWITCH_MOD;

// Keyboard modifier to force mouse selection when in mouse mode.
// This lets you select text even if the application has captured the mouse.
uint forceselmod = ShiftMask;

// Keyboard shortcuts that send bytes to the terminal.
// The first entry whose critera match the key event is chosen.
//
// Some criteria match against the current mode:
//   `appkey` is linked to whether the keypad is in "application mode"
//   `appcursor` is linked to whether cursor keys are in "application mode"
//   `crlf` is linked to whether the terminal is in "automatic newline mode"
// These have three values
//   -1: only match if mode is NOT active
//   0 : match independent of mode
//   1 : only match if mode is active
// appkey has one extra value:
//   2 : only match if mode is active AND numlock was toggled off.
static Key key[] = {
  /* keysym           mask            string      appkey appcursor crlf */
  { XK_KP_Home,       ShiftMask,      "\033[1;2H",     0,    0,    0},
  { XK_KP_Home,       XK_ANY_MOD,     "\033OH",        0,    0,    0},
  { XK_KP_Up,         XK_ANY_MOD,     "\033Ox",       +1,    0,    0},
  { XK_KP_Up,         XK_ANY_MOD,     "\033[A",        0,   -1,    0},
  { XK_KP_Up,         XK_ANY_MOD,     "\033OA",        0,   +1,    0},
  { XK_KP_Down,       XK_ANY_MOD,     "\033Or",       +1,    0,    0},
  { XK_KP_Down,       XK_ANY_MOD,     "\033[B",        0,   -1,    0},
  { XK_KP_Down,       XK_ANY_MOD,     "\033OB",        0,   +1,    0},
  { XK_KP_Left,       XK_ANY_MOD,     "\033Ot",       +1,    0,    0},
  { XK_KP_Left,       XK_ANY_MOD,     "\033[D",        0,   -1,    0},
  { XK_KP_Left,       XK_ANY_MOD,     "\033OD",        0,   +1,    0},
  { XK_KP_Right,      XK_ANY_MOD,     "\033Ov",       +1,    0,    0},
  { XK_KP_Right,      XK_ANY_MOD,     "\033[C",        0,   -1,    0},
  { XK_KP_Right,      XK_ANY_MOD,     "\033OC",        0,   +1,    0},
  { XK_KP_Prior,      ShiftMask,      "\033[5;2~",     0,    0,    0},
  { XK_KP_Prior,      XK_ANY_MOD,     "\033[5~",       0,    0,    0},
  { XK_KP_Begin,      XK_ANY_MOD,     "\033[E",        0,    0,    0},
  { XK_KP_End,        ControlMask,    "\033[1;5F",     0,    0,    0},
  { XK_KP_End,        ShiftMask,      "\033[1;2F",     0,    0,    0},
  { XK_KP_End,        XK_ANY_MOD,     "\033OF",        0,    0,    0},
  { XK_KP_Next,       ShiftMask,      "\033[6;2~",     0,    0,    0},
  { XK_KP_Next,       XK_ANY_MOD,     "\033[6~",       0,    0,    0},
  { XK_KP_Insert,     ShiftMask,      "\033[2;2~",    +1,    0,    0},
  { XK_KP_Insert,     ShiftMask,      "\033[4l",      -1,    0,    0},
  { XK_KP_Insert,     ControlMask,    "\033[L",       -1,    0,    0},
  { XK_KP_Insert,     ControlMask,    "\033[2;5~",    +1,    0,    0},
  { XK_KP_Insert,     XK_ANY_MOD,     "\033[4h",      -1,    0,    0},
  { XK_KP_Insert,     XK_ANY_MOD,     "\033[2~",      +1,    0,    0},
  { XK_KP_Delete,     ControlMask,    "\033[3;5~",     0,    0,    0},
  { XK_KP_Delete,     ShiftMask,      "\033[3;2~",     0,    0,    0},
  { XK_KP_Delete,     XK_ANY_MOD,     "\033[3~",       0,    0,    0},
  { XK_KP_Multiply,   XK_ANY_MOD,     "\033Oj",       +2,    0,    0},
  { XK_KP_Add,        XK_ANY_MOD,     "\033Ok",       +2,    0,    0},
  { XK_KP_Enter,      XK_ANY_MOD,     "\033OM",       +2,    0,    0},
  { XK_KP_Enter,      XK_ANY_MOD,     "\r",           -1,    0,   -1},
  { XK_KP_Enter,      XK_ANY_MOD,     "\r\n",         -1,    0,   +1},
  { XK_KP_Subtract,   XK_ANY_MOD,     "\033Om",       +2,    0,    0},
  { XK_KP_Decimal,    XK_ANY_MOD,     "\033On",       +2,    0,    0},
  { XK_KP_Divide,     XK_ANY_MOD,     "\033Oo",       +2,    0,    0},
  { XK_KP_0,          XK_ANY_MOD,     "\033Op",       +2,    0,    0},
  { XK_KP_1,          XK_ANY_MOD,     "\033Oq",       +2,    0,    0},
  { XK_KP_2,          XK_ANY_MOD,     "\033Or",       +2,    0,    0},
  { XK_KP_3,          XK_ANY_MOD,     "\033Os",       +2,    0,    0},
  { XK_KP_4,          XK_ANY_MOD,     "\033Ot",       +2,    0,    0},
  { XK_KP_5,          XK_ANY_MOD,     "\033Ou",       +2,    0,    0},
  { XK_KP_6,          XK_ANY_MOD,     "\033Ov",       +2,    0,    0},
  { XK_KP_7,          XK_ANY_MOD,     "\033Ow",       +2,    0,    0},
  { XK_KP_8,          XK_ANY_MOD,     "\033Ox",       +2,    0,    0},
  { XK_KP_9,          XK_ANY_MOD,     "\033Oy",       +2,    0,    0},
  { XK_Up,            ShiftMask,      "\033[1;2A",     0,    0,    0},
  { XK_Up,            Mod1Mask,       "\033[1;3A",     0,    0,    0},
  { XK_Up,         ShiftMask|Mod1Mask,"\033[1;4A",     0,    0,    0},
  { XK_Up,            ControlMask,    "\033[1;5A",     0,    0,    0},
  { XK_Up,      ShiftMask|ControlMask,"\033[1;6A",     0,    0,    0},
  { XK_Up,       ControlMask|Mod1Mask,"\033[1;7A",     0,    0,    0},
  { XK_Up,ShiftMask|ControlMask|Mod1Mask,"\033[1;8A",  0,    0,    0},
  { XK_Up,            XK_ANY_MOD,     "\033[A",        0,   -1,    0},
  { XK_Up,            XK_ANY_MOD,     "\033OA",        0,   +1,    0},
  { XK_Down,          ShiftMask,      "\033[1;2B",     0,    0,    0},
  { XK_Down,          Mod1Mask,       "\033[1;3B",     0,    0,    0},
  { XK_Down,       ShiftMask|Mod1Mask,"\033[1;4B",     0,    0,    0},
  { XK_Down,          ControlMask,    "\033[1;5B",     0,    0,    0},
  { XK_Down,    ShiftMask|ControlMask,"\033[1;6B",     0,    0,    0},
  { XK_Down,     ControlMask|Mod1Mask,"\033[1;7B",     0,    0,    0},
  { XK_Down,ShiftMask|ControlMask|Mod1Mask,"\033[1;8B",0,    0,    0},
  { XK_Down,          XK_ANY_MOD,     "\033[B",        0,   -1,    0},
  { XK_Down,          XK_ANY_MOD,     "\033OB",        0,   +1,    0},
  { XK_Left,          ShiftMask,      "\033[1;2D",     0,    0,    0},
  { XK_Left,          Mod1Mask,       "\033[1;3D",     0,    0,    0},
  { XK_Left,       ShiftMask|Mod1Mask,"\033[1;4D",     0,    0,    0},
  { XK_Left,          ControlMask,    "\033[1;5D",     0,    0,    0},
  { XK_Left,    ShiftMask|ControlMask,"\033[1;6D",     0,    0,    0},
  { XK_Left,     ControlMask|Mod1Mask,"\033[1;7D",     0,    0,    0},
  { XK_Left,ShiftMask|ControlMask|Mod1Mask,"\033[1;8D",0,    0,    0},
  { XK_Left,          XK_ANY_MOD,     "\033[D",        0,   -1,    0},
  { XK_Left,          XK_ANY_MOD,     "\033OD",        0,   +1,    0},
  { XK_Right,         ShiftMask,      "\033[1;2C",     0,    0,    0},
  { XK_Right,         Mod1Mask,       "\033[1;3C",     0,    0,    0},
  { XK_Right,      ShiftMask|Mod1Mask,"\033[1;4C",     0,    0,    0},
  { XK_Right,         ControlMask,    "\033[1;5C",     0,    0,    0},
  { XK_Right,   ShiftMask|ControlMask,"\033[1;6C",     0,    0,    0},
  { XK_Right,    ControlMask|Mod1Mask,"\033[1;7C",     0,    0,    0},
  { XK_Right,ShiftMask|ControlMask|Mod1Mask,"\033[1;8C",0,   0,    0},
  { XK_Right,         XK_ANY_MOD,     "\033[C",        0,   -1,    0},
  { XK_Right,         XK_ANY_MOD,     "\033OC",        0,   +1,    0},
  { XK_ISO_Left_Tab,  ShiftMask,      "\033[Z",        0,    0,    0},
  { XK_Return,        Mod1Mask,       "\033\r",        0,    0,   -1},
  { XK_Return,        Mod1Mask,       "\033\r\n",      0,    0,   +1},
  { XK_Return,        XK_ANY_MOD,     "\r",            0,    0,   -1},
  { XK_Return,        XK_ANY_MOD,     "\r\n",          0,    0,   +1},
  { XK_Insert,        ShiftMask,      "\033[4l",      -1,    0,    0},
  { XK_Insert,        ShiftMask,      "\033[2;2~",    +1,    0,    0},
  { XK_Insert,        ControlMask,    "\033[L",       -1,    0,    0},
  { XK_Insert,        ControlMask,    "\033[2;5~",    +1,    0,    0},
  { XK_Insert,        XK_ANY_MOD,     "\033[4h",      -1,    0,    0},
  { XK_Insert,        XK_ANY_MOD,     "\033[2~",      +1,    0,    0},
  { XK_Delete,        ControlMask,    "\033[3;5~",     0,    0,    0},
  { XK_Delete,        ShiftMask,      "\033[3;2~",     0,    0,    0},
  { XK_Delete,        XK_ANY_MOD,     "\033[3~",       0,    0,    0},
  { XK_BackSpace,     XK_ANY_MOD,     "\010",          0,    0,    0},
  { XK_Home,          ShiftMask,      "\033[1;2H",     0,    0,    0},
  { XK_Home,          XK_ANY_MOD,     "\033OH",        0,    0,    0},
  { XK_End,           ControlMask,    "\033[1;5F",     0,    0,    0},
  { XK_End,           ShiftMask,      "\033[1;2F",     0,    0,    0},
  { XK_End,           XK_ANY_MOD,     "\033OF",        0,    0,    0},
  { XK_Prior,         ControlMask,    "\033[5;5~",     0,    0,    0},
  { XK_Prior,         ShiftMask,      "\033[5;2~",     0,    0,    0},
  { XK_Prior,         XK_ANY_MOD,     "\033[5~",       0,    0,    0},
  { XK_Next,          ControlMask,    "\033[6;5~",     0,    0,    0},
  { XK_Next,          ShiftMask,      "\033[6;2~",     0,    0,    0},
  { XK_Next,          XK_ANY_MOD,     "\033[6~",       0,    0,    0},
  { XK_F1,            XK_NO_MOD,      "\033OP" ,       0,    0,    0},
  { XK_F1, /* F13 */  ShiftMask,      "\033[1;2P",     0,    0,    0},
  { XK_F1, /* F25 */  ControlMask,    "\033[1;5P",     0,    0,    0},
  { XK_F1, /* F37 */  Mod4Mask,       "\033[1;6P",     0,    0,    0},
  { XK_F1, /* F49 */  Mod1Mask,       "\033[1;3P",     0,    0,    0},
  { XK_F1, /* F61 */  Mod3Mask,       "\033[1;4P",     0,    0,    0},
  { XK_F2,            XK_NO_MOD,      "\033OQ" ,       0,    0,    0},
  { XK_F2, /* F14 */  ShiftMask,      "\033[1;2Q",     0,    0,    0},
  { XK_F2, /* F26 */  ControlMask,    "\033[1;5Q",     0,    0,    0},
  { XK_F2, /* F38 */  Mod4Mask,       "\033[1;6Q",     0,    0,    0},
  { XK_F2, /* F50 */  Mod1Mask,       "\033[1;3Q",     0,    0,    0},
  { XK_F2, /* F62 */  Mod3Mask,       "\033[1;4Q",     0,    0,    0},
  { XK_F3,            XK_NO_MOD,      "\033OR" ,       0,    0,    0},
  { XK_F3, /* F15 */  ShiftMask,      "\033[1;2R",     0,    0,    0},
  { XK_F3, /* F27 */  ControlMask,    "\033[1;5R",     0,    0,    0},
  { XK_F3, /* F39 */  Mod4Mask,       "\033[1;6R",     0,    0,    0},
  { XK_F3, /* F51 */  Mod1Mask,       "\033[1;3R",     0,    0,    0},
  { XK_F3, /* F63 */  Mod3Mask,       "\033[1;4R",     0,    0,    0},
  { XK_F4,            XK_NO_MOD,      "\033OS" ,       0,    0,    0},
  { XK_F4, /* F16 */  ShiftMask,      "\033[1;2S",     0,    0,    0},
  { XK_F4, /* F28 */  ControlMask,    "\033[1;5S",     0,    0,    0},
  { XK_F4, /* F40 */  Mod4Mask,       "\033[1;6S",     0,    0,    0},
  { XK_F4, /* F52 */  Mod1Mask,       "\033[1;3S",     0,    0,    0},
  { XK_F5,            XK_NO_MOD,      "\033[15~",      0,    0,    0},
  { XK_F5, /* F17 */  ShiftMask,      "\033[15;2~",    0,    0,    0},
  { XK_F5, /* F29 */  ControlMask,    "\033[15;5~",    0,    0,    0},
  { XK_F5, /* F41 */  Mod4Mask,       "\033[15;6~",    0,    0,    0},
  { XK_F5, /* F53 */  Mod1Mask,       "\033[15;3~",    0,    0,    0},
  { XK_F6,            XK_NO_MOD,      "\033[17~",      0,    0,    0},
  { XK_F6, /* F18 */  ShiftMask,      "\033[17;2~",    0,    0,    0},
  { XK_F6, /* F30 */  ControlMask,    "\033[17;5~",    0,    0,    0},
  { XK_F6, /* F42 */  Mod4Mask,       "\033[17;6~",    0,    0,    0},
  { XK_F6, /* F54 */  Mod1Mask,       "\033[17;3~",    0,    0,    0},
  { XK_F7,            XK_NO_MOD,      "\033[18~",      0,    0,    0},
  { XK_F7, /* F19 */  ShiftMask,      "\033[18;2~",    0,    0,    0},
  { XK_F7, /* F31 */  ControlMask,    "\033[18;5~",    0,    0,    0},
  { XK_F7, /* F43 */  Mod4Mask,       "\033[18;6~",    0,    0,    0},
  { XK_F7, /* F55 */  Mod1Mask,       "\033[18;3~",    0,    0,    0},
  { XK_F8,            XK_NO_MOD,      "\033[19~",      0,    0,    0},
  { XK_F8, /* F20 */  ShiftMask,      "\033[19;2~",    0,    0,    0},
  { XK_F8, /* F32 */  ControlMask,    "\033[19;5~",    0,    0,    0},
  { XK_F8, /* F44 */  Mod4Mask,       "\033[19;6~",    0,    0,    0},
  { XK_F8, /* F56 */  Mod1Mask,       "\033[19;3~",    0,    0,    0},
  { XK_F9,            XK_NO_MOD,      "\033[20~",      0,    0,    0},
  { XK_F9, /* F21 */  ShiftMask,      "\033[20;2~",    0,    0,    0},
  { XK_F9, /* F33 */  ControlMask,    "\033[20;5~",    0,    0,    0},
  { XK_F9, /* F45 */  Mod4Mask,       "\033[20;6~",    0,    0,    0},
  { XK_F9, /* F57 */  Mod1Mask,       "\033[20;3~",    0,    0,    0},
  { XK_F10,           XK_NO_MOD,      "\033[21~",      0,    0,    0},
  { XK_F10, /* F22 */ ShiftMask,      "\033[21;2~",    0,    0,    0},
  { XK_F10, /* F34 */ ControlMask,    "\033[21;5~",    0,    0,    0},
  { XK_F10, /* F46 */ Mod4Mask,       "\033[21;6~",    0,    0,    0},
  { XK_F10, /* F58 */ Mod1Mask,       "\033[21;3~",    0,    0,    0},
  { XK_F11,           XK_NO_MOD,      "\033[23~",      0,    0,    0},
  { XK_F11, /* F23 */ ShiftMask,      "\033[23;2~",    0,    0,    0},
  { XK_F11, /* F35 */ ControlMask,    "\033[23;5~",    0,    0,    0},
  { XK_F11, /* F47 */ Mod4Mask,       "\033[23;6~",    0,    0,    0},
  { XK_F11, /* F59 */ Mod1Mask,       "\033[23;3~",    0,    0,    0},
  { XK_F12,           XK_NO_MOD,      "\033[24~",      0,    0,    0},
  { XK_F12, /* F24 */ ShiftMask,      "\033[24;2~",    0,    0,    0},
  { XK_F12, /* F36 */ ControlMask,    "\033[24;5~",    0,    0,    0},
  { XK_F12, /* F48 */ Mod4Mask,       "\033[24;6~",    0,    0,    0},
  { XK_F12, /* F60 */ Mod1Mask,       "\033[24;3~",    0,    0,    0},
  { XK_F13,           XK_NO_MOD,      "\033[1;2P",     0,    0,    0},
  { XK_F14,           XK_NO_MOD,      "\033[1;2Q",     0,    0,    0},
  { XK_F15,           XK_NO_MOD,      "\033[1;2R",     0,    0,    0},
  { XK_F16,           XK_NO_MOD,      "\033[1;2S",     0,    0,    0},
  { XK_F17,           XK_NO_MOD,      "\033[15;2~",    0,    0,    0},
  { XK_F18,           XK_NO_MOD,      "\033[17;2~",    0,    0,    0},
  { XK_F19,           XK_NO_MOD,      "\033[18;2~",    0,    0,    0},
  { XK_F20,           XK_NO_MOD,      "\033[19;2~",    0,    0,    0},
  { XK_F21,           XK_NO_MOD,      "\033[20;2~",    0,    0,    0},
  { XK_F22,           XK_NO_MOD,      "\033[21;2~",    0,    0,    0},
  { XK_F23,           XK_NO_MOD,      "\033[23;2~",    0,    0,    0},
  { XK_F24,           XK_NO_MOD,      "\033[24;2~",    0,    0,    0},
  { XK_F25,           XK_NO_MOD,      "\033[1;5P",     0,    0,    0},
  { XK_F26,           XK_NO_MOD,      "\033[1;5Q",     0,    0,    0},
  { XK_F27,           XK_NO_MOD,      "\033[1;5R",     0,    0,    0},
  { XK_F28,           XK_NO_MOD,      "\033[1;5S",     0,    0,    0},
  { XK_F29,           XK_NO_MOD,      "\033[15;5~",    0,    0,    0},
  { XK_F30,           XK_NO_MOD,      "\033[17;5~",    0,    0,    0},
  { XK_F31,           XK_NO_MOD,      "\033[18;5~",    0,    0,    0},
  { XK_F32,           XK_NO_MOD,      "\033[19;5~",    0,    0,    0},
  { XK_F33,           XK_NO_MOD,      "\033[20;5~",    0,    0,    0},
  { XK_F34,           XK_NO_MOD,      "\033[21;5~",    0,    0,    0},
  { XK_F35,           XK_NO_MOD,      "\033[23;5~",    0,    0,    0},
};

// Keyboard modifiers that alter the behavior of mouse selection.
// If no match is found, regular selection is used.
uint selmasks[] = {
    /* none= */ 0,
    /* SEL_REGULAR= */ 0,
    /* SEL_RECTANGULAR= */ Mod1Mask,
};
