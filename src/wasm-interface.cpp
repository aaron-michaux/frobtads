

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

#include <string>
#include <vector>

using std::vector;

#ifdef WASMTADS

#include <emscripten.h>
#include <emscripten/fetch.h>

#define WASM_EXPORT __attribute__((visibility("default"))) extern "C"

// --------------------------------------------------------------------- Options

struct T3Options
{
   bool placeholder = false; //
};

// ---------------------------------------------------------------------- run-t3

static void run_t3(FILE* t3, const T3Options& opts)
{
   printf("Hello World, from Run T3!\n");

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
   const char* filename = nullptr; // how to load a file into WASM?
   printf("Have to load file into WASM applications\n");

   // Saved game position to restore (optional).
   const char* savedPosFilename = nullptr;
}

// ----------------------------------------------------------- load-t3-succeeded

static void load_t3_succeeded(emscripten_fetch_t* fetch)
{
   printf("Finished downloading %llu bytes from URL %s.\n",
          fetch->numBytes,
          fetch->url);

   vector<char> raw_data(fetch->numBytes);
   std::memcpy(&raw_data[0], &fetch->data[0], fetch->numBytes);

   T3Options opts;
   if(fetch->userData != nullptr) {
      T3Options* opts_ptr = reinterpret_cast<T3Options*>(fetch->userData);
      opts                = *opts_ptr;
      delete opts_ptr;
   }

   // The data is now available at fetch->data[0] through
   // fetch->data[fetch->numBytes-1];
   emscripten_fetch_close(fetch); // Free data associated with the fetch.

   // Now onto running
   FILE* fp = fmemopen(&raw_data[0], raw_data.size(), "r");
   if(!fp) {
      printf("Failed to `fmemopen` url\n");
   } else {
      run_t3(fp, opts);
      fclose(fp);
   }
}

// -------------------------------------------------------------- load-t3-failed

static void load_t3_failed(emscripten_fetch_t* fetch)
{
   printf("Downloading %s failed, HTTP failure status code: %d.\n",
          fetch->url,
          fetch->status);

   if(fetch->userData == nullptr) {
      T3Options* opts = reinterpret_cast<T3Options*>(fetch->userData);
      delete opts;
   }

   emscripten_fetch_close(fetch); // Also free data on failure.
}

// --------------------------------------------------------------------- load-t3

WASM_EXPORT void init_t3(const char* url)
{
   T3Options* opts_ptr = new T3Options{}; // a placeholder

   emscripten_fetch_attr_t attr;
   emscripten_fetch_attr_init(&attr);
   strcpy(attr.requestMethod, "GET");
   attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
   attr.onsuccess  = load_t3_succeeded;
   attr.onerror    = load_t3_failed;
   attr.userData   = opts_ptr;
   emscripten_fetch(&attr, url);

   printf("Started to download t3 url: '%s'\n", url);
}

// -----------------------------------------------------------------------------

WASM_EXPORT int test_function(int a, int b) { return a * a + b; }

int main(int argc, char** argv)
{
   EM_ASM({ onWasmTadsLoaded(); });
   return EXIT_SUCCESS;
}

#endif
