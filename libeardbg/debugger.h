//
//  debugger.h
//  PegasusEar
//
//  Created by Kevin Colley on 10/31/20.
//

#ifndef EARDBG_DEBUGGER_H
#define EARDBG_DEBUGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include "common/dynamic_array.h"
#include "libear/ear.h"
#include "repl.h"
#include "pegasus.h"


typedef EAR_Byte BreakpointID;

// Debugger state
typedef uint8_t DebugFlags;

//! The CPU should skip the next instruction if it's a breakpoint
#define DEBUG_RESUMING ((DebugFlags)(1 << 0))

//! Memory accesses shouldn't be considered for breakpoints
#define DEBUG_NOBREAK ((DebugFlags)(1 << 1))

//! Allow invasive commands that change processor state
#define DEBUG_INVASIVE ((DebugFlags)(1 << 2))

//! Treat the debugger as detached
#define DEBUG_DETACHED ((DebugFlags)(1 << 3))

//! Allow debugging of kernel memory and registers
#define DEBUG_KERNEL ((DebugFlags)(1 << 4))

typedef uint8_t BreakpointFlags;
#define BP_IN_USE ((BreakpointFlags)(1 << 0))
#define BP_ENABLED ((BreakpointFlags)(1 << 1))
#define BP_PHYSICAL ((BreakpointFlags)(1 << 2))
#define BP_READ ((BreakpointFlags)(1 << 3))
#define BP_WRITE ((BreakpointFlags)(1 << 4))
#define BP_EXECUTE ((BreakpointFlags)(1 << 5))

#define BP_PROT_MASK (BP_READ | BP_WRITE | BP_EXECUTE)

typedef struct Breakpoint {
	EAR_FullAddr addr;
	BreakpointFlags flags;
} Breakpoint;

typedef struct Debugger {
	EAR* cpu;
	bool* trace;
	EAR_MemoryHandler* mem_fn;
	void* mem_cookie;
	Bus_AccessHandler* bus_fn;
	Bus_DumpFunc* bus_dump_fn;
	void* bus_cookie;
	dynamic_array(Breakpoint) breakpoints;
	Pegasus* pegs[2];
	EAR_HaltReason r;
	DebugFlags debug_flags;
} Debugger;


/*!
 * @brief Initializes a debugger that will control the provided EAR processor.
 * 
 * @param cpu EAR processor to debug
 * @param debug_flags Initial debug flags to set
 * 
 * @return Newly initialized debugger object
 */
Debugger* Debugger_init(EAR* cpu, DebugFlags debug_flags);

/*!
 * @brief Allows the debugger to control whether trace output should be printed.
 * 
 * @param trace Pointer to a boolean that will be set to true if trace output should be printed
 */
void Debugger_setTraceVar(Debugger* dbg, bool* trace);

/*! Connect the debugger to the physical memory bus so the debugger can add breakpoints
 * on attempts to access specific physical memory addresses AND so it can dump physical
 * memory through hexdump and similar commands.
 * 
 * @param bus_fn Real underlying physical memory bus access handler function
 * @param bus_cookie Opaque value passed to `bus_fn`
 */
void Debugger_setBusHandler(Debugger* dbg, Bus_AccessHandler* bus_fn, void* bus_cookie);

/*! Set function used to dump physical memory layout */
void Debugger_setBusDumper(Debugger* dbg, Bus_DumpFunc* bus_dump_fn);

/*!
 * @brief Check whether the specified thread state is in kernel mode.
 *        We do this by seeing if any control registers are denied.
 */
bool Debugger_isKernelMode(EAR_ThreadState* ctx);

/*!
 * @brief Hook function called before executing each instruction.
 * 
 * @param insn Instruction about to be executed
 * @param pc Address of the next instruction to be executed (after this one)
 * @param before True if the hook is called before executing the instruction, false if after
 * @param cond True if the instruction's condition evaluated to true
 */
EAR_HaltReason Debugger_execHook(void* cookie, EAR_Instruction* insn, EAR_FullAddr pc, bool before, bool cond);

/*!
 * @brief Set this function as the CPU's bus handler so the debugger can add breakpoints
 * on attempts to access specific physical memory addresses.
 * 
 * @param cookie Opaque value passed to the callback
 * @param mode Either BUS_MODE_READ or BUS_MODE_WRITE, indicating the memory operation
 * @param addr Full 24-bit address of the memory access
 * @param is_byte True if the access is a byte access, false for word access
 * @param data Pointer to the data buffer to read from/write to
 * 
 * @return True if the access was successful
 */
bool Debugger_busHandler(
	void* cookie, Bus_AccessMode mode,
	EAR_PhysAddr paddr, bool is_byte, void* data,
	EAR_HaltReason* out_r
);

/*! Connect the debugger to the MMU */
void Debugger_setMemoryHandler(Debugger* dbg, EAR_MemoryHandler* mem_fn, void* mem_cookie);

/*!
 * @brief Set this function as the CPU's memory handler so the debugger can add breakpoints,
 * and dump virtual memory.
 * 
 * @param cookie Opaque value passed to the callback
 * @param prot Virtual access mode, one of EAR_PROT_READ, EAR_PROT_WRITE, or EAR_PROT_EXECUTE
 * @param mode Either BUS_MODE_READ or BUS_MODE_WRITE, indicating the memory operation
 * @param vmaddr 16-bit virtual address to access
 * @param is_byte True if the access is a byte access, false for word access
 * @param data Pointer to the data buffer to read from/write to
 * @param out_r Pointer to where the halt reason will be written (nonnull)
 * 
 * @return True for success
 */
bool Debugger_memoryHandler(
	void* cookie, EAR_Protection prot, Bus_AccessMode mode,
	EAR_FullAddr vmaddr, bool is_byte, void* data, EAR_HaltReason* out_r
);

/*!
 * @brief A memory handler function that actually bypasses the MMU and goes straight to the
 * physical memory bus.
 * 
 * @param cookie Opaque value passed to the callback
 * @param prot Virtual access mode, one of EAR_PROT_READ, EAR_PROT_WRITE, or EAR_PROT_EXECUTE
 * @param mode Either BUS_MODE_READ or BUS_MODE_WRITE, indicating the memory operation
 * @param paddr 24-bit physical address to access
 * @param is_byte True if the access is a byte access, false for word access
 * @param data Pointer to the data buffer to read from/write to
 * @param out_r Pointer to where the halt reason will be written (nonnull)
 * 
 * @return True for success
 */
bool Debugger_memoryHandler_physical(
	void* cookie, EAR_Protection prot, Bus_AccessMode mode,
	EAR_FullAddr paddr, bool is_byte, void* data, EAR_HaltReason* out_r
);

/*! Destroys a debugger object that was previously created using `Debugger_init`. */
void Debugger_destroy(Debugger* dbg);

/*!
 * @brief Registers a HW memory access breakpoint.
 * 
 * @param addr Address to place a breakpoint
 * @param flags Breakpoint flags
 * @return Registered breakpoint ID
 */
BreakpointID Debugger_addBreakpoint(Debugger* dbg, EAR_FullAddr addr, BreakpointFlags flags);

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

/*!
 * @brief Read a span of physical memory into a buffer.
 * 
 * @param buf Output data buffer
 * @param addr Physical address to read from
 * @param size Number of bytes to read
 * @param out_r Optional output pointer for the halt reason
 * 
 * @return Number of bytes that could not be read (0 if all bytes were read successfully)
 */
EAR_UWord Debugger_readPhys(
	Debugger* dbg, void* buf,
	EAR_PhysAddr addr, EAR_UWord size,
	EAR_HaltReason* out_r
);

/*!
 * @brief Read a span of virtual memory into a buffer.
 * 
 * @param buf Output data buffer
 * @param prot Virtual access mode, one of EAR_PROT_READ, EAR_PROT_WRITE, or EAR_PROT_EXECUTE
 * @param vaddr Virtual address to read from
 * @param size Number of bytes to read
 * @param out_r Optional output pointer for the halt reason
 * 
 * @return Number of bytes that could not be read (0 if all bytes were read successfully)
 */
EAR_UWord Debugger_readVirt(
	Debugger* dbg, void* buf,
	EAR_Protection prot,
	EAR_VirtAddr vaddr, EAR_UWord size
);

/*!
 * @brief Import segment and symbol information from a Pegasus image.
 * 
 * @param peg Pegasus image to import
 * @param alt True to import for the inactive thread state, false for the active one
 */
void Debugger_addPegasusImage(Debugger* dbg, Pegasus* peg, bool alt);

/*!
 * @brief Prints a description of an EAR instruction.
 * 
 * @param insn Instruction to print
 * @param pc PC value when instruction executes (address of NEXT instruction)
 * @param stream Stream where the instruction should be written
 */
void Debugger_showInstruction(
	Debugger* dbg, EAR_Instruction* insn, EAR_FullAddr pc, FILE* stream
);

/*!
 * @brief Print a description of the active thread state's virtual memory map to the file.
 * 
 * @param stream File stream used for output
 */
void Debugger_showVMMap(Debugger* dbg, FILE* stream);

/*!
 * @brief Display context of the selected thread state's execution.
 * 
 * @param alt True to use the inactive thread state
 * @param stream File stream used for output
 */
void Debugger_showContext(Debugger* dbg, bool alt, FILE* stream);

/*!
 * @brief Display the backtrace of the selected thread state.
 * 
 * @param alt True to use the inactive thread state
 * @param stream File stream used for output
 */
void Debugger_showBacktrace(Debugger* dbg, bool alt, FILE* stream);

/*!
 * @brief Disassemble a number of instructions from the given starting point.
 * 
 * @param mem_fn Memory access function used to load code bytes
 * @param mem_cookie Opaque value passed to `mem_fn`
 * @param addr Code address where disassembling should start
 * @param dpc DPC (delta PC) value to use while fetching code bytes for disassembly
 * @param count Number of instructions to disassemble
 * @param physical True if code should be fetched from physical memory instead of virtual
 * @param stream Output stream where disassembled instructions should be written
 * 
 * @return Number of instructions disassembled
 */
EAR_UWord Debugger_showDisassembly(
	Debugger* dbg, EAR_MemoryHandler* mem_fn, void* mem_cookie,
	EAR_FullAddr addr, EAR_UWord dpc, EAR_UWord count,
	bool physical, FILE* stream
);

/*! Prints the thread's register state to the output stream. */
void Debugger_showRegs(Debugger* dbg, bool alt, FILE* stream);

/*! Prints a description of the thread's control registers to the output stream. */
void Debugger_showControlRegs(Debugger* dbg, bool alt, FILE* stream);

/*! Step a single instruction in the debugger, semantically */
void Debugger_stepInstruction(Debugger* dbg);

/*! Global flag that is set when a keyboard interrupt is caught. */
extern volatile sig_atomic_t g_interrupted;

/*! Sets up the signal handler for keyboard interrupts. */
bool enable_interrupt_handler(void);

/*! Tears down the signal handler for keyboard interrupts. */
void disable_interrupt_handler(void);

#endif /* EARDBG_DEBUGGER_H */
