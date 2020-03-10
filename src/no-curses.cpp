
#include "no-curses.h"

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FALSE 0
#define TRUE 1

// struct ColorPair
// {
//    ColorPair(int fg_, int bg_)
//        : fg(fg_)
//        , bg(bg_)
//    {}
//    int fg = 0;
//    int bg = 0;
// }

struct WINDOW
{
   int lines, cols;

   int curx, cury;
   int begx, begy;
   int maxx, maxy;
   int parx, pary;
   void* val = NULL;

   int timeout_ms = 0;

   FILE* in = stdin;

   BOOL is_scrollok = FALSE;
   BOOL is_keypad   = TRUE; // default on
   BOOL has_colors  = TRUE;

   WINDOW(int lines_, int cols_, int y_pos, int x_pos)
       : lines(lines_)
       , cols(cols_)
       , begx(x_pos)
       , begy(y_pos)
   {}
};

WINDOW* stdscr = nullptr;

// --------------------------------------------------- Constructon/Destruction

WINDOW* initscr() { return stdscr = newwin(100, 80, 1, 1); }

int endwin()
{
   if(!stdscr) return ERR;
   delwin(stdscr);
   stdscr = nullptr;
   return OK;
}

WINDOW* newwin(int lines, int cols, int y_pos, int x_pos)
{
   return new WINDOW(lines, cols, y_pos, x_pos);
}

int delwin(WINDOW* o)
{
   if(o == nullptr) return 1;
   delete o;
   return 0;
}

// --------------------------------------------------------------------- Getters
int getcurx(const WINDOW* o) { return o->curx; }
int getcury(const WINDOW* o) { return o->cury; }
int getbegx(const WINDOW* o) { return o->begx; }
int getbegy(const WINDOW* o) { return o->begy; }
int getmaxx(const WINDOW* o) { return o->maxx; }
int getmaxy(const WINDOW* o) { return o->maxy; }
int getparx(const WINDOW* o) { return o->parx; }
int getpary(const WINDOW* o) { return o->pary; }

// -----------------------------------------------------------------------------
// The refresh and wrefresh routines (or wnoutrefresh and doupdate) must be
// called to get actual output to the terminal, as other routines merely
// manipulate data structures. The routine wrefresh copies the named window to
// the physical terminal screen, taking into account what is already there to do
// optimizations. The refresh routine is the same, using stdscr as the default
// window. Unless leaveok has been enabled, the physical cursor of the terminal
// is left at the location of the cursor for that window.
int wrefresh(WINDOW* o) { return OK; }

int refresh() { return wrefresh(stdscr); }

// These routines move the cursor associated with the window to line y and
// column x. This routine does not move the physical cursor of the terminal
// until refresh is called. The position specified is relative to the upper
// left-hand corner of the window, which is (0,0).
int wmove(WINDOW* o, int x, int y)
{
   fprintf(stdout, "WMOVE(%d, %d);\n", x, y);
   return OK;
}

// The erase and werase routines copy blanks to every position in the window,
// clearing the screen.
int werase(WINDOW* o)
{
   fprintf(stdout, "WERASE;\n");
   return OK;
}

// The clear and wclear routines are like erase and werase, but they also call
// clearok, so that the screen is cleared completely on the next call to
// wrefresh for that window and repainted from scratch.
int wclear(WINDOW* o)
{
   fprintf(stdout, "WCLEAR;\n");
   return OK;
}

// The getch, wgetch, mvgetch and mvwgetch, routines read a character from the
// window. In no-delay mode, if no input is waiting, the value ERR is returned.
// In delay mode, the program waits until the system passes text through to the
// program. Depending on the setting of cbreak, this is after one character
// (cbreak mode), or after the first newline (nocbreak mode). In half-delay
// mode, the program waits until a character is typed or the specified timeout
// has been reached.
int wgetch(WINDOW* o)
{
   auto fpeek = [in = o->in]() -> int {
      int c = fgetc(in);
      ungetc(c, in);
      return c;
   };

   // TODO, we need the WASM equivalents for function keys, keypad, etc.
   // TODO, handle timeout
   return fgetc(o->in);
}

// The timeout and wtimeout routines set blocking or non-blocking read for a
// given window. If delay is negative, blocking read is used (i.e., waits
// indefinitely for input). If delay is zero, then non-blocking read is used
// (i.e., read returns ERR if no input is waiting). If delay is positive, then
// read blocks for delay milliseconds, and returns ERR if there is still no
// input. Hence, these routines provide the same functionality as nodelay, plus
// the additional capability of being able to block for only delay milliseconds
// (where delay is positive).
void wtimeout(WINDOW* o, int ms) { o->timeout_ms = ms; }

// These routines return the character, of type chtype, at the current position
// in the named window. If any attributes are set for that position, their
// values are OR'ed into the value returned. Constants defined in <curses.h> can
// be used with the & (logical AND) operator to extract the character or
// attributes alone.
chtype mvwinch(WINDOW* o, int y, int x) { return 0; }

// These routines copy chstr into the window image structure at and after the
// current cursor position. The four routines with n as the last argument copy
// at most n elements, but no more than will fit on the line. If n=-1 then the
// whole string is copied, to the maximum number of characters that will fit on
// the line.
int mvwaddchstr(WINDOW* o, int y, int x, const chtype* chstr)
{
   fprintf(stdout, "MVWADDCHSTR(%d, %d) called\n", y, x);
   int ret = OK;
   while(chstr && (ret = mvwaddch(o, y, x, *chstr)) == OK) chstr++;
   return ret;
}

//    The addch, waddch, mvaddch and mvwaddch routines put the character ch into
//    the given window at its current window position, which is then advanced.
//    They are analogous to putchar in stdio(3). If the advance is at the right
//    margin, the cursor automatically wraps to the beginning of the next line.
//    At the bottom of the current scrolling region, if scrollok is enabled, the
//    scrolling region is scrolled up one line.
//
// If ch is a tab, newline, or backspace, the cursor is moved appropriately
// within the window. Backspace moves the cursor one character left; at the left
// edge of a window it does nothing. Newline does a clrtoeol, then moves the
// cursor to the window left margin on the next line, scrolling the window if on
// the last line. Tabs are considered to be at every eighth column. The tab
// interval may be altered by setting the TABSIZE variable.
//
// If ch is any control character other than tab, newline, or backspace, it is
// drawn in ^X notation. Calling winch after adding a control character does not
// return the character itself, but instead returns the ^-representation of the
// control character.
int mvwaddch(WINDOW*, int, int, const chtype)
{
   // TODO, let's store an actual 2d array
   return OK;
}

// -----------------------------------------------------------------------------
// The curs_set routine sets the cursor state is set to invisible, normal, or
// very visible for visibility equal to 0, 1, or 2 respectively. If the terminal
// supports the visibility requested, the previous cursor state is returned;
// otherwise, ERR is returned.
int curs_set(int val) { return OK; }

// The napms routine is used to sleep for ms milliseconds.
int napms(int ms)
{
   usleep(size_t(ms) * 1000);
   return OK; // NOTE: will
}

// -----------------------------------------------------------------------------
// The erasewchar routine stores the current erase character in the location
// referenced by ch. If no erase character has been defined, the routine fails
// and the location referenced by ch is not changed.
//
// The erase character is the key (or key combination) used to erase text. It
// could be BACKSPACE, DEL, Ctrl+H, or whatever.
char erasechar() { return 127; }

// The killwchar routine stores the current line-kill character in the location
// referenced by ch. If no line-kill character has been defined, the routine
// fails and the location referenced by ch is not changed.
char killchar()
{
   fprintf(stderr, "SHOULD NEVER GET HERE\n");
   return 127;
}

// -----------------------------------------------------------------------------
// The scrollok option controls what happens when the cursor of a window is
// moved off the edge of the window or scrolling region, either as a result of a
// newline action on the bottom line, or typing the last character of the last
// line. If disabled, (bf is FALSE), the cursor is left on the bottom line. If
// enabled, (bf is TRUE), the window is scrolled up one line (Note that to get
// the physical scrolling effect on the terminal, it is also necessary to call
// idlok).
int scrollok(WINDOW* o, BOOL val)
{
   o->is_scrollok = val;
   return OK;
}
BOOL is_scrollok(const WINDOW* o) { return o->is_scrollok; }

// The touchwin and touchline routines throw away all optimization information
// about which parts of the window have been touched, by pretending that the
// entire window has been drawn on. This is sometimes necessary when using
// overlapping windows, since a change to one window affects the other window,
// but the records of which lines have been changed in the other window do not
// reflect the change. The routine touchline only pretends that count lines have
// been changed, beginning with line start.
int touchwin(WINDOW*)
{
   return OK; // irrelevant
}

// The keypad option enables the keypad of the user's terminal. If enabled (bf
// is TRUE), the user can press a function key (such as an arrow key) and wgetch
// returns a single value representing the function key, as in KEY_LEFT. If
// disabled (bf is FALSE), curses does not treat function keys specially and the
// program has to interpret the escape sequences itself. If the keypad in the
// terminal can be turned on (made to transmit) and off (made to work locally),
// turning on this option causes the terminal keypad to be turned on when wgetch
// is called. The default value for keypad is false.
int keypad(WINDOW* o, BOOL val)
{
   o->is_keypad = val;
   return OK;
}

// Initially, whether the terminal returns 7 or 8 significant bits on input
// depends on the control mode of the tty driver [see termio(7)]. To force 8
// bits to be returned, invoke meta(win, TRUE); this is equivalent, under POSIX,
// to setting the CS8 flag on the terminal. To force 7 bits to be returned,
// invoke meta(win, FALSE); this is equivalent, under POSIX, to setting the CS7
// flag on the terminal. The window argument, win, is always ignored. If the
// terminfo capabilities smm (meta_on) and rmm (meta_off) are defined for the
// terminal, smm is sent to the terminal when meta(win, TRUE) is called and rmm
// is sent when meta(win, FALSE) is called.
int meta(WINDOW*, BOOL)
{
   return OK; // irrelevant
}

int noecho()
{
   // The echo and noecho routines control whether characters typed by the user
   // are echoed by getch as they are typed. Echoing by the tty driver is always
   // disabled, but initially getch is in echo mode, so characters typed are
   // echoed. Authors of most interactive programs prefer to do their own
   // echoing in a controlled area of the screen, or not to echo at all, so they
   // disable echoing by calling noecho. [See curs_getch(3X) for a discussion of
   // how these routines interact with cbreak and nocbreak.]
   return OK; // irrelevant
}

// Initially the terminal may or may not be in cbreak mode, as the mode is
// inherited; therefore, a program should call cbreak or nocbreak explicitly.
// Most interactive programs using curses set the cbreak mode. Note that cbreak
// overrides raw. [See curs_getch(3X) for a discussion of how these routines
// interact with echo and noecho.]
int cbreak()
{
   return OK; // NOTE: always get a character at a time
}

// The nl and nonl routines control whether the underlying display device
// translates the return key into newline on input, and whether it translates
// newline into return and line-feed on output (in either case, the call
// addch('\n') does the equivalent of return and line feed on the virtual
// screen). Initially, these translations do occur. If you disable them using
// nonl, curses will be able to make better use of the line-feed capability,
// resulting in faster cursor motion. Also, curses will then be able to detect
// the return key.
int nonl()
{
   // NOTE, by default, when the javascript console is set upt,
   // nonl will be the default
   return OK;
}

int nl()
{
   return ERR; // never allow 'nl' mode
}

// ---------------------------------------------------------------------- Colors

// The init_pair routine changes the definition of a color-pair. It takes
// three arguments: the number of the color-pair to be changed, the
// foreground color number, and the background color number. For portable
// applications:
//
// The value of the first argument must be between 1 and COLOR_PAIRS-1,
// except that if default colors are used (see use_default_colors) the upper
// limit is adjusted to allow for extra pairs which use a default color in
// foreground and/or background.
//
// The value of the second and third arguments must be between 0 and COLORS.
// Color pair 0 is assumed to be white on black, but is actually whatever
// the terminal implements before color is initialized. It cannot be
// modified by the application. If the color-pair was previously
// initialized, the screen is refreshed and all occurrences of that
// color-pair are changed to the new definition.
int winit_pair(WINDOW* o,
               NCURSES_PAIRS_T pair,
               NCURSES_COLOR_T f,
               NCURSES_COLOR_T b)
{
   // Irrelevant, we'll translate colors directly using
   // <span class="..."></span> + css
   return OK;
}

int init_pair(NCURSES_PAIRS_T pair, NCURSES_COLOR_T f, NCURSES_COLOR_T b)
{
   return winit_pair(stdscr, pair, f, b);
}

BOOL has_colors() { return stdscr->has_colors; }

int start_color()
{
   return OK; // irrelevant
}

int use_default_colors()
{
   return OK; // irrelevant: we're rendering in a browser
}

#ifdef __cplusplus
}
#endif
