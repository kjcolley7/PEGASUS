//
//  ear.h
//  PegasusEar
//
//  Created by Kevin Colley on 3/25/17.
//

#ifndef EAR_EAR_H
#define EAR_EAR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include "types.h"

// Configuration flags
#ifndef EAR_DEBUG
#define EAR_DEBUG 0
#endif


struct EAR {
	EAR_Context ctx;             //!< CPU thread context
	EAR_MemoryHandler* mem_fn;   //!< Function pointer called to handle memory accesses
	void* mem_cookie;            //!< Opaque cookie value passed to mem_fn
	EAR_PortRead* read_fn;       //!< Function pointer called during `RDB` execution
	EAR_PortWrite* write_fn;     //!< Function pointer called during `WRB` execution
	void* port_cookie;           //!< Opaque cookie value passed to read_fn and write_fn
	EAR_ExecHook* exec_fn;       //!< Function pointer called before executing each instruction
	void* exec_cookie;           //!< Opaque cookie value passed to exec_fn
	uint64_t ins_count;          //!< Total number of instructions executed
	EAR_ExceptionMask exc_catch; //!< Mask of exceptions to catch
	bool verbose;                //!< True if verbose output should be printed
};

#define CTX_X(ear, cross) (&(ear).ctx.banks[(ear).ctx.active ^ (cross)])
#define CTX(ear) CTX_X(ear, 0)


/*!
 * @brief Initialize an EAR CPU with default register values and memory mappings
 */
void EAR_init(EAR* ear);

/*!
 * @brief Set the function called to handle all memory accesses
 * 
 * @param mem_fn Function pointer called to handle all memory accesses
 * @param cookie Opaque value passed to mem_fn
 */
void EAR_setMemoryHandler(EAR* ear, EAR_MemoryHandler* mem_fn, void* cookie);

/*! Reset the normal thread state to its default values */
void EAR_resetRegisters(EAR* ear);

/*!
 * @brief Set the current thread state of the EAR CPU
 * 
 * @param thstate New thread state
 */
void EAR_setThreadState(EAR* ear, const EAR_ThreadState* thstate);

/*!
 * @brief Set the functions called to handle reads/writes on ports
 * 
 * @param read_fn Function pointer called to handle `RDB`
 * @param write_fn Function pointer called to handle `WRB`
 * @param cookie Opaque value passed as the first parameter to these callbacks
 */
void EAR_setPorts(EAR* ear, EAR_PortRead* read_fn, EAR_PortWrite* write_fn, void* cookie);

/*!
 * @brief Set the function called before executing each instruction
 * 
 * @param exec_fn Function pointer called before executing each instruction
 * @param exec_cookie Opaque value passed as the first parameter to `exec_fn`
 */
void EAR_setExecHook(EAR* ear, EAR_ExecHook* exec_fn, void* exec_cookie);

/*!
 * @brief Fetch the next code instruction starting at *pc.
 * 
 * @param mem_fn Memory handler function to use for memory accesses
 * @param mem_cookie Opaque value passed to `mem_fn`
 * @param pc In/out pointer to the address of the instruction to fetch. This will
 *           be written back with the address of the code byte directly following
 *           the instruction that was fetched.
 * @param pc_mask Bitmask used to wrap the PC value
 * @param dpc Value of DPC register (delta PC) to use while fetching code bytes
 * @param verbose True if verbose messages should be printed on decode error
 * @param out_insn Output pointer where the decoded instruction will be written
 * @param out_exc_info Output pointer where exception info will be written
 * @param out_exc_addr Output pointer where exception address will be written
 * 
 * @return Reason for halting, typically HALT_NONE
 */
EAR_HaltReason EAR_fetchInstruction(
	EAR_MemoryHandler* mem_fn, void* mem_cookie,
	EAR_FullAddr* pc, uint32_t pc_mask, EAR_UWord dpc, bool verbose,
	EAR_Instruction* out_insn, EAR_ExceptionInfo* out_exc_info, EAR_UWord* out_exc_addr
);

/*!
 * @brief Executes a single instruction
 * 
 * @return Reason for halting, typically HALT_NONE
 */
EAR_HaltReason EAR_stepInstruction(EAR* ear);

/*!
 * @brief Begins execution from the current state.
 * 
 * @return Reason for halting, never HALT_NONE
 */
EAR_HaltReason EAR_continue(EAR* ear);

/*!
 * @brief Invokes a function at a given virtual address and passing up to 6 arguments.
 * 
 * @note This will overwrite the values of registers A0-A5, PC, DPC, RA, and RD.
 *       All other registers are left untouched, so the existing values of SP and
 *       FP are used. This function will start at the given target address. When
 *       the target function returns to the initial values of RA and RD, this
 *       function will return with status HALT_RETURN. Register values will not
 *       be cleaned up after the function returns.
 * 
 * @param func_vmaddr Virtual address of the function to invoke
 * @param func_dpc Delta PC (DPC) value to be used for executing the target function
 * @param arg1 First argument to the target function
 * @param arg2 Second argument to the target function
 * @param arg3 Third argument to the target function
 * @param arg4 Fourth argument to the target function
 * @param arg5 Fifth argument to the target function
 * @param arg6 Sixth argument to the target function
 * @param run True if the VM should be run after setting up the call state
 * 
 * @return HALT_NONE when the target function returns successfully, or halt reason on error.
 */
EAR_HaltReason EAR_invokeFunction(
	EAR* ear,
	EAR_VirtAddr func_vmaddr,
	EAR_UWord func_dpc,
	EAR_UWord arg1,
	EAR_UWord arg2,
	EAR_UWord arg3,
	EAR_UWord arg4,
	EAR_UWord arg5,
	EAR_UWord arg6,
	bool run
);

/*! Converts an EAR halt reason code to a string description. */
const char* EAR_haltReasonToString(EAR_HaltReason status);

/*! Get a textual description of the kind of exception from exc_info */
const char* EAR_exceptionKindToString(EAR_ExceptionInfo kind);

/*! Given an EAR opcode, return a string representing that instruction's mnemonic */
const char* EAR_getMnemonic(EAR_Opcode op);

/*! Given an EAR condition code, return a string representing the condition */
const char* EAR_getConditionString(EAR_Cond cond);

/*! Given an EAR register number, return a string name of the register */
const char* EAR_getRegisterName(EAR_Register reg);

/*! Given an EAR control register number, return a string name of the control register */
const char* EAR_getControlRegisterName(EAR_ControlRegister cr);

#endif /* EAR_EAR_H */
