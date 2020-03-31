

#include "common.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "colors.h"
#include "frobtadsappcurses.h"
#include "frobtadsappplain.h"
#include "options.h"

#include "os.h"
#include "t3std.h"
#include "trd.h"
#include "vmhostsi.h"
#include "vmmain.h"
#include "vmmaincn.h"
#include "vmnet.h"
#include "vmvsn.h"
extern "C" {
#include "osgen.h"
}

#ifdef WASMTADS

#define WASM_EXPORT __attribute__((visibility("default"))) extern "C"

WASM_EXPORT int test_function(int a, int b) { return a * a + b; }

int main(int argc, char** argv)
{
   printf("Hello World!\n");

   // Initialize locale if available
   setlocale(LC_CTYPE, "");

   // A semi-functional, portable ostream as defined in options.h.
   ostream cerr(stderr);
   ostream cout(stdout);

   // Available screen interfaces.
   enum screenInterface { cursesInterface, plainInterface };
   screenInterface interface = cursesInterface; // default

   // Increase the T3VM undo-size 16 times by default.
   frobVmUndoMaxRecords                      = defaultVmUndoMaxRecords * 16;
   FrobTadsApplication::FrobOptions frobOpts = {
       // We assume some defaults.  They might change while
       // parsing the command line.
       true,       // Use colors.
       false,      // Don't force colors.
       true,       // Use terminal's defaults for color pair 0.
       true,       // Enable soft-scrolling.
       true,       // Pause prior to exit.
       true,       // Change to the game's directory.
       FROB_WHITE, // Text.
       FROB_BLACK, // Background.
       -1,         // Statusline text; none yet.
       -1,         // Statusline background; none yet.
       512 * 1024, // Scroll-back buffer size.
       // Default file I/O safety level is read/write access
       // in current directory only.
       VM_IO_SAFETY_READWRITE_CUR,
       VM_IO_SAFETY_READWRITE_CUR,
       // Default network I/O safety level is no access
       2,
       2,
       // TODO: Revert the default back to "\0" when Unicode output
       // is finally implemented.
       "us-ascii", // Character set.
       0,          // Replay file.
       true        // seedRand.
   };

   // Name of the game to run.
   const char* filename = 0; // how to load a file into WASM?
   printf("Have to load file into WASM applications\n");

   // Saved game position to restore (optional).
   const char* savedPosFilename = 0;

   // Next, how to load the file into TADS, given that TADS expects
   printf("Refit loading a file into TADS\n");
}

#endif
