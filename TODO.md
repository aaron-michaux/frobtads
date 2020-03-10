
# Plan #

 * Take over build system for frob /only/ (mobius+run.sh)
 * Strip `frob` to make it a cli program. (i.e., no ncurses.)
   + This means removing the banner API (?) how to do?
 * Discover what `frob` is doing with libcurl, and strip/refactor
 * Build a `tads-engine` library
 * Build the above library with WASM
 * Investigate what's involved in making a javascript-ffi
 
# Libraries #

 * ncurses
   + config.h
   + src/frobcurses.h
   + src/frobtadsappcurses.cc
   + src/osportable.cc
 * curl
   + tads3/unix/osnetunix.cpp
   + tads3/unix/osnetunix.h

