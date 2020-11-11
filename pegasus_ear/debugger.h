//
//  debugger.h
//  PegasusEar
//
//  Created by Kevin Colley on 10/31/20.
//

#ifndef EAR_DEBUGGER_H
#define EAR_DEBUGGER_H

#include "ear.h"
#include <stdbool.h>

typedef struct Debugger Debugger;
typedef EAR_Byte BreakpointID;


/*!
 * @brief Initializes a debugger that will control the provided EAR processor.
 * 
 * @param cpu EAR processor to debug
 * @return Newly initialized debugger object
 */
Debugger* Debugger_init(EAR* cpu);

/*! Destroys a debugger object that was previously created using `Debugger_init`. */
void Debugger_destroy(Debugger* dbg);

/*!
 * @brief Registers a hardware breakpoint.
 * 
 * @param addr Virtual address to place a breakpoint
 * @param prot Mask of memory access modes to break on
 * @return Registered breakpoint ID
 */
BreakpointID Debugger_addBreakpoint(Debugger* dbg, EAR_Size addr, EAR_Protection prot);

/*!
 * @brief Temporarily disables a registered breakpoint.
 * 
 * @param bpid Breakpoint ID to disable
 */
void Debugger_disableBreakpoint(Debugger* dbg, BreakpointID bpid);

/*!
 * @brief Enables a previously disabled breakpoint.
 * 
 * @param bpid Breakpoint ID to enable
 */
void Debugger_enableBreakpoint(Debugger* dbg, BreakpointID bpid);

/*!
 * @brief Toggles whether the target breakpoint is enabled or disabled.
 * 
 * @param bpid Breakpoint ID to toggle
 * @return Whether the breakpoint is now enabled or disabled
 */
bool Debugger_toggleBreakpoint(Debugger* dbg, BreakpointID bpid);

/*!
 * @brief Removes a breakpoint so that its ID may be reused.
 * 
 * @param bpid Breakpoint ID to remove
 */
void Debugger_removeBreakpoint(Debugger* dbg, BreakpointID bpid);

/*! Clear all registered breakpoints. */
void Debugger_clearBreakpoints(Debugger* dbg);

/*! Interactively runs the debugger. */
EAR_HaltReason Debugger_run(Debugger* dbg);

#endif /* EAR_DEBUGGER_H */
