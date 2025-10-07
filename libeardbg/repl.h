#ifndef EARDBG_REPL_H
#define EARDBG_REPL_H

#include "libear/ear.h"

typedef struct Debugger Debugger;

/*! Interactively runs the debugger. */
EAR_HaltReason Debugger_run(Debugger* dbg);

#endif /* EARDBG_REPL_H */
