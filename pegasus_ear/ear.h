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

// Configuration flags
#ifndef EAR_CLEAR_EXCEPTION_STACK
#define EAR_CLEAR_EXCEPTION_STACK 0
#endif

#ifndef EAR_DEBUG
#define EAR_DEBUG 0
#endif


// Machine data types
#define EAR_BITS 16
typedef int16_t EAR_Word;
#define EAR_WORD_MIN INT16_MIN
#define EAR_WORD_MAX INT16_MAX
typedef uint16_t EAR_Size;
#define EAR_SIZE_MAX UINT16_MAX
typedef uint8_t EAR_Byte;
typedef EAR_Byte EAR_PageNumber;


// Register numbers
typedef uint8_t EAR_Register;
#define R0 ((EAR_Register)0)
#define R1 ((EAR_Register)1)
#define R2 ((EAR_Register)2)
#define R3 ((EAR_Register)3)
#define R4 ((EAR_Register)4)
#define R5 ((EAR_Register)5)
#define R6 ((EAR_Register)6)
#define R7 ((EAR_Register)7)
#define R8 ((EAR_Register)8)
#define R9 ((EAR_Register)9)
#define R10 ((EAR_Register)10)
#define R11 ((EAR_Register)11)
#define R12 ((EAR_Register)12)
#define R13 ((EAR_Register)13)
#define R14 ((EAR_Register)14)
#define R15 ((EAR_Register)15)

// Primary register aliases
#define ZERO   R0
#define RV     R2
#define RVX    R3
#define FP    R10
#define SP    R11
#define RA    R12
#define RD    R13
#define PC    R14
#define DPC   R15

// Aliases for argument registers
#define A0     R1
#define A1     R2
#define A2     R3
#define A3     R4
#define A4     R5
#define A5     R6

// Aliases for callee-saved registers
#define S0     R7
#define S1     R8
#define S2     R9
#define S3    R10


// Condition flag values
typedef uint8_t EAR_Flag;
#define FLAG_ZF ((EAR_Flag)(1 << 0))
#define FLAG_SF ((EAR_Flag)(1 << 1))
#define FLAG_PF ((EAR_Flag)(1 << 2))
#define FLAG_CF ((EAR_Flag)(1 << 3))
#define FLAG_VF ((EAR_Flag)(1 << 4))
#define FLAG_MF ((EAR_Flag)(1 << 5))


// Instruction condition codes
typedef uint8_t EAR_Cond;
//! Equal (Zero)
#define COND_EQ ((EAR_Cond)0x0)
#define COND_ZR COND_EQ
//! Not Equal (Nonzero)
#define COND_NE ((EAR_Cond)0x1)
#define COND_NZ COND_NE
//! Unsigned Greater Than
#define COND_GT ((EAR_Cond)0x2)
//! Unsigned Less Than or Equal
#define COND_LE ((EAR_Cond)0x3)
//! Unsigned Less Than (Carry Clear)
#define COND_LT ((EAR_Cond)0x4)
#define COND_CC COND_LT
//! Unsigned Greater Than or Equal (Carry Set)
#define COND_GE ((EAR_Cond)0x5)
#define COND_CS COND_GE
//! Unconditional Special (Instruction Prefix)
#define COND_SP ((EAR_Cond)0x6)
//! Unconditional (Always)
#define COND_AL ((EAR_Cond)0x7)
//! Negative
#define COND_NG ((EAR_Cond)0x8)
//! Positive or Zero
#define COND_PS ((EAR_Cond)0x9)
//! Signed Greater Than
#define COND_BG ((EAR_Cond)0xA)
//! Signed Less Than or Equal
#define COND_SE ((EAR_Cond)0xB)
//! Signed Less Than
#define COND_SM ((EAR_Cond)0xC)
//! Signed Greater Than or Equal
#define COND_BE ((EAR_Cond)0xD)
//! Odd Parity
#define COND_OD ((EAR_Cond)0xE)
//! Even Parity
#define COND_EV ((EAR_Cond)0xF)


// Instruction opcodes
typedef uint8_t EAR_Opcode;

#define OP_ADD ((EAR_Opcode)0x00)
#define OP_SUB ((EAR_Opcode)0x01)
#define OP_MLU ((EAR_Opcode)0x02)
#define OP_MLS ((EAR_Opcode)0x03)
#define OP_DVU ((EAR_Opcode)0x04)
#define OP_DVS ((EAR_Opcode)0x05)
#define OP_XOR ((EAR_Opcode)0x06)
#define OP_AND ((EAR_Opcode)0x07)
#define OP_ORR ((EAR_Opcode)0x08)
#define OP_SHL ((EAR_Opcode)0x09)
#define OP_SRU ((EAR_Opcode)0x0A)
#define OP_SRS ((EAR_Opcode)0x0B)
#define OP_MOV ((EAR_Opcode)0x0C)
#define OP_CMP ((EAR_Opcode)0x0D)
#define OP_RSV_0E ((EAR_Opcode)0x0E)
#define OP_RSV_0F ((EAR_Opcode)0x0F)
#define OP_LDW ((EAR_Opcode)0x10)
#define OP_STW ((EAR_Opcode)0x11)
#define OP_LDB ((EAR_Opcode)0x12)
#define OP_STB ((EAR_Opcode)0x13)
#define OP_BRA ((EAR_Opcode)0x14)
#define OP_BRR ((EAR_Opcode)0x15)
#define OP_FCA ((EAR_Opcode)0x16)
#define OP_FCR ((EAR_Opcode)0x17)
#define OP_RDB ((EAR_Opcode)0x18)
#define OP_WRB ((EAR_Opcode)0x19)
#define OP_PSH ((EAR_Opcode)0x1A)
#define OP_POP ((EAR_Opcode)0x1B)
#define OP_INC ((EAR_Opcode)0x1C)
#define OP_BPT ((EAR_Opcode)0x1D)
#define OP_HLT ((EAR_Opcode)0x1E)
#define OP_NOP ((EAR_Opcode)0x1F)


// Instruction prefix opcodes

#define PREFIX_XC ((EAR_Opcode)0x00)
#define PREFIX_TF ((EAR_Opcode)0x01)
#define PREFIX_EM ((EAR_Opcode)0x02)
#define PREFIX_RSV_MASK ((EAR_Opcode)0x0F)
#define PREFIX_DR_MASK ((EAR_Opcode)0x10)


// Address space geometry
#define EAR_NULL ((EAR_Size)0x0000)
#define EAR_ADDRESS_SPACE_SIZE ((uint32_t)0x10000)
#define EAR_PAGE_SIZE ((EAR_Size)0x100)
#define EAR_TTE_COUNT ((EAR_Size)(EAR_ADDRESS_SPACE_SIZE / EAR_PAGE_SIZE))
#define EAR_TTB_PADDR ((EAR_Size)(EAR_ADDRESS_SPACE_SIZE - EAR_TTE_COUNT * sizeof(EAR_TTE)))

/*
Initial physical address space layout:

+======+==============================+
| Addr | Description                  |
+======+==============================+
| 0000 | NULL page                    |
+------+------------------------------+
| 0100 | Physical allocation table    |
+------+------------------------------+
| 0200 |                              |
| .... | Unallocated physical pages   |
| EF00 |                              |
+------+------------------------------+
| F000 | Exception stack bottom guard |
+------+------------------------------+
| F100 |                              |
| .... | Exception stack              |
| FA00 |                              |
+------+------------------------------+
| FB00 | Exception stack top guard    |
+------+------------------------------+
| FC00 | Page tables 00-3F            |
| FD00 | Page tables 40-7F            |
| FE00 | Page tables 80-BF            |
| FF00 | Page tables C0-FF            |
+======+==============================+

Initial virtual address space layout:

+======+=====+==============================+
| Addr |Perms| Description                  |
+======+=====+==============================+
| 0000 | --- | NULL page                    |
+------+-----+------------------------------+
| 0100 | --- |                              |
| .... | --- | Unmapped region              |
| E900 | --- |                              |
+------+-----+------------------------------+
| EA00 | --- | Stack bottom guard           |
+------+-----+------------------------------+
| EB00 | RW- |                              |
| .... | RW- | Stack                        |
| FA00 | RW- |                              |
+------+-----+------------------------------+
| FB00 | --- | Stack top guard              |
+------+-----+------------------------------+
| FC00 | RW- | Page tables 00-3F            |
| FD00 | RW- | Page tables 40-7F            |
| FE00 | RW- | Page tables 80-BF            |
| FF00 | RW- | Page tables C0-FF            |
+======+=====+==============================+
*/

// Physical address of the physical page attributes region
#define EAR_PHYSICAL_ALLOCATION_TABLE_PADDR ((EAR_Size)0x0100)
#define EAR_PHYSICAL_ALLOCATION_TABLE_SIZE EAR_PAGE_SIZE

// One byte per physical page
typedef EAR_Byte EAR_PTE;

//! The physical page is dirty and must be zeroed before being allocated
#define PHYS_DIRTY ((EAR_PTE)(1 << 0))

//! The physical page is in use and should not be used to fulfill an allocation
#define PHYS_IN_USE ((EAR_PTE)(1 << 1))

//! The physical page is accessible and will not cause a fault on access
#define PHYS_ALLOW ((EAR_PTE)(1 << 2))

//! The physical page is not accessible, and accessing it will cause HALT_DOUBLEFAULT
#define PHYS_DENY ((EAR_PTE)(0 << 2))


// Exception stack physical address range: F000-F100 (bottom guard), F100-FB00 (exception stack), FB00-FC00 (top guard)
#define EAR_EXCEPTION_STACK_TOP_GUARD ((EAR_Size)(EAR_TTB_PADDR - EAR_PAGE_SIZE))
#define EAR_EXCEPTION_STACK_TOP EAR_EXCEPTION_STACK_TOP_GUARD
#define EAR_EXCEPTION_STACK_BOTTOM ((EAR_Size)0xF100)
#define EAR_EXCEPTION_STACK_SIZE ((EAR_Size)(EAR_EXCEPTION_STACK_TOP - EAR_EXCEPTION_STACK_BOTTOM))
#define EAR_EXCEPTION_STACK_BOTTOM_GUARD ((EAR_Size)(EAR_EXCEPTION_STACK_BOTTOM - EAR_PAGE_SIZE))

// Normal stack virtual address range: EA00-EB00 (bottom guard), EB00-FB00 (stack region), FB00-FC00 (top guard)
#define EAR_STACK_TOP_GUARD ((EAR_Size)(EAR_TTB_PADDR - EAR_PAGE_SIZE))
#define EAR_STACK_TOP EAR_STACK_TOP_GUARD
#define EAR_STACK_BOTTOM ((EAR_Size)0xEB00)
#define EAR_STACK_SIZE ((EAR_Size)(EAR_STACK_TOP - EAR_STACK_BOTTOM))
#define EAR_STACK_BOTTOM_GUARD ((EAR_Size)(EAR_STACK_BOTTOM - EAR_PAGE_SIZE))

// Special values used to decide when the program returns from an externally invoked function
#define EAR_CALL_RA ((EAR_Size)0xFFFF)
#define EAR_CALL_RD ((EAR_Size)0xFFFF)

// Helper macros for page math
#define EAR_PAGE_NUMBER(addr) ((EAR_PageNumber)((EAR_Size)(addr) / EAR_PAGE_SIZE))
#define EAR_PAGE_OFFSET(addr) ((EAR_Size)(addr) & (EAR_PAGE_SIZE - 1))
#define EAR_IS_PAGE_ALIGNED(addr) (EAR_PAGE_OFFSET(addr) == 0)
#define EAR_CEIL_PAGE(addr) EAR_FLOOR_PAGE((EAR_Size)(addr) + EAR_PAGE_SIZE - 1)
#define EAR_FLOOR_PAGE(addr) ((EAR_Size)((EAR_Size)(addr) & ~(EAR_PAGE_SIZE - 1)))

// Architecture structures
typedef struct EAR_ThreadState EAR_ThreadState;
typedef struct EAR_Instruction EAR_Instruction;

// Memory structures
typedef struct EAR_TTE EAR_TTE;
typedef struct EAR_PageTable EAR_PageTable;
typedef struct EAR_Memory EAR_Memory;

// Memory protections
typedef uint8_t EAR_Protection; //lol
#define EAR_PROT_NONE ((EAR_Protection)0)
#define EAR_PROT_READ ((EAR_Protection)(1 << 0))
#define EAR_PROT_WRITE ((EAR_Protection)(1 << 1))
#define EAR_PROT_EXECUTE ((EAR_Protection)(1 << 2))

// This one is special
#define EAR_PROT_PHYSICAL ((EAR_Protection)(1 << 3))

// Main processor structure
typedef struct EAR EAR;

// Processor halt reason
typedef enum EAR_HaltReason {
	HALT_UNALIGNED     = -1,  //!< Tried to access a word at an unaligned (odd) memory address
	HALT_UNMAPPED      = -2,  //!< Accessed unmapped virtual memory
	HALT_DOUBLEFAULT   = -3,  //!< Accessed unmapped memory in a page fault handler
	HALT_DECODE        = -4,  //!< Tried to execute an illegal instruction
	HALT_ARITHMETIC    = -5,  //!< Divide/modulo by zero, or signed div/mod INT16_MIN by -1
	HALT_SW_BREAKPOINT = -6,  //!< Executed a `BPT` instruction
	HALT_HW_BREAKPOINT = -7,  //!< Hit a hardware breakpoint
	HALT_NONE = 0,            //!< No unusual halt reason
	HALT_INSTRUCTION,         //!< Executed a `HLT` instruction
	HALT_RETURN,              //!< Program tried to return from the topmost stack frame
	HALT_COMPLETE,            //!< For internal use only, used to support fault handlers
	HALT_DEBUGGER,            //!< Halted by the debugger
} EAR_HaltReason;

#define EAR_FAILED(haltReason) ((haltReason) < 0)

// Debugger state
typedef uint8_t EAR_DebugFlags;

//! A debugger is waiting to attach to the CPU when it resumes
#define DEBUG_ATTACH ((EAR_DebugFlags)(1 << 0))

//! The CPU is running under a debugger
#define DEBUG_ACTIVE ((EAR_DebugFlags)(1 << 1))

//! The CPU should skip the next instruction if it's a breakpoint
#define DEBUG_RESUMING ((EAR_DebugFlags)(1 << 2))

//! Each instruction will be printed as it executes
#define DEBUG_TRACE ((EAR_DebugFlags)(1 << 3))

//! Be more verbose about things like segment mapping
#define DEBUG_VERBOSE ((EAR_DebugFlags)(1 << 4))

//! When a page fault occurs, don't execute a page fault handler in the VM
#define DEBUG_NOFAULT ((EAR_DebugFlags)(1 << 5))

//! Allow invasive commands that change processor state
#define DEBUG_INVASIVE ((EAR_DebugFlags)(1 << 6))


struct EAR_ThreadState {
	EAR_Size r[16];           //!< Register values
	EAR_Size cur_pc;          //!< Address of currently-executing instruction
	EAR_Flag flags;           //!< Condition flags
	uint64_t ins_count;       //!< Total number of instructions executed by this thread state
};

struct EAR_Instruction {
	EAR_Cond cond;            //!< Condition code
	EAR_Opcode op;            //!< Opcode
	
	EAR_Register rd;          //!< Rd register number, destination which is usually the same as Rx
	EAR_Register rx;          //!< Rx register number, first operand and often the destination
	EAR_Register ry;          //!< Ry register number, often the source
	EAR_Size ry_val;          //!< Ry register value or immediate value when Ry is DPC
	
	uint16_t regs16;          //!< Register bitmap (for PSH and POP)
	
	uint8_t port_number;      //!< Port number (for RDB and WRB)
	
	uint8_t toggle_flags : 1; //!< Whether the TF instruction modifier was present
	uint8_t enable_mmu   : 1; //!< Whether the EM instruction modifier was present
};

struct EAR_TTE {
	EAR_PageNumber r_ppn;     //!< Physical page number of backing memory when accessed in read mode
	EAR_PageNumber w_ppn;     //!< Physical page number of backing memory when accessed in write mode
	EAR_PageNumber x_ppn;     //!< Physical page number of backing memory when accessed in execute mode
	
	/*!
	 * @brief Physical page number of function called to handle illegal accesses to this page
	 * 
	 * @note Detailed explanation and function prototype follows.
	 */
	EAR_PageNumber fault_ppn;
	
	// /*!
	//  * @brief Handle a page fault triggered by an instruction or during instruction execution.
	//  * 
	//  * @param tte_paddr Physical address of the page table entry corresponding to the faulting virtual
	//  *        address
	//  * @param fault_vmaddr Virtual address being accessed that caused the page fault
	//  * @param prot Attempted access mode. Read is 0, write is 1, execute is 2.
	//  * @param saved_regs Pointer to the saved registers area, holding all 16 general purpose registers.
	//  *        PC here is the value of PC before the instruction started executing, aka the first byte
	//  *        of the current instruction. If upon return from this fault handler ZERO contains a
	//  *        nonzero value, the current instruction is marked as complete. All registers besides ZERO
	//  *        will be written back to the faulting thread's context upon return.
	//  * @param next_pc Holds the address of the next instruction byte after the current instruction, or
	//  *        if the fault occurred during instruction decoding, it should be exactly fault_vmaddr.
	//  * 
	//  * @return Physical address to be used in place of fault_vmaddr for accesses. This is ignored when
	//  *         the instruction is marked as complete by writing a nonzero value to saved_regs[ZERO].
	//  */
	// void* handle_fault(
	//     uint16_t    tte_paddr,
	//     void*       fault_vmaddr,
	//     uint8_t     prot,
	//     uint16_t*   saved_regs,
	//     void*       next_pc
	// ) {
	//     // Handle fault
	// }
};

struct EAR_PageTable {
	//! Translation table for the address space
	EAR_TTE entries[EAR_TTE_COUNT];
};

struct EAR_Memory {
	//! All physical memory accessible to the CPU
	EAR_Byte bytes[EAR_ADDRESS_SPACE_SIZE];
};

typedef bool EAR_PortRead(void* cookie, uint8_t port, EAR_Byte* out_byte);
typedef bool EAR_PortWrite(void* cookie, uint8_t port, EAR_Byte byte);
typedef EAR_HaltReason EAR_FaultHandler(void* cookie, EAR_Size vmaddr, EAR_Protection prot, EAR_TTE* tte, EAR_HaltReason faultReason, EAR_Size* out_paddr);
typedef EAR_HaltReason EAR_MemoryHook(void* cookie, EAR_Size vmaddr, EAR_Protection prot, EAR_Size size, void* data);
typedef EAR_HaltReason EAR_DebugAttach(void* cookie);

struct EAR {
	EAR_Memory mem;              //!< Physical memory accessible to the CPU
	EAR_ThreadState context;     //!< Normal CPU thread context, containing register values
	EAR_ThreadState exc_ctx;     //!< Exception CPU thread context
	EAR_ThreadState* active;     //!< Actively executing thread context
	EAR_PortRead* read_fn;       //!< Function pointer called during `RDB` execution
	EAR_PortWrite* write_fn;     //!< Function pointer called during `WRB` execution
	void* port_cookie;           //!< Opaque cookie value passed to read_fn and write_fn
	EAR_FaultHandler* fault_fn;  //!< Function pointer called during an unhandled page fault
	void* fault_cookie;          //!< Opaque cookie value passed to fault_fn
	EAR_MemoryHook* mem_fn;      //!< Function pointer called during all memory accesses
	void* mem_cookie;            //!< Opaque cookie value passed to mem_fn
	EAR_DebugAttach* debug_fn;   //!< Function pointer called when execution is about to start
	void* debug_cookie;          //!< Opaque cookie value passed to debug_fn
	uint64_t ins_count;          //!< Total number of instructions executed
	EAR_DebugFlags debug_flags;  //!< Debugger state
};


/*!
 * @brief Initialize an EAR CPU with default register values and memory mappings
 * 
 * @param debugFlags Initial debug flags
 */
void EAR_init(EAR* ear, EAR_DebugFlags debugFlags);

/*! Reset the normal thread state to its default values */
void EAR_resetRegisters(EAR* ear);

/*!
 * @brief Add a memory segment that should be accessible to the EAR CPU
 * 
 * @param vmaddr Virtual memory address where the segment starts, or NULL to
 *        find the next available virtual region that's large enough
 * @param vmsize Size of the memory segment in virtual memory space
 * @param phys_page_array Array of physical addresses for this region, or NULL
 *        to set them all to EAR_NULL
 * @param vmprot Memory protections for this new virtual memory segment
 * @param fault_physaddr Physical address of function called to handle page faults
 * 
 * @return Virtual address where the segment was mapped. Usually vmaddr unless
 *         it was passed as EAR_NULL. If a virtual region could not be found
 *         that was large enough to hold the requested size, then EAR_NULL is
 *         returned.
 */
EAR_Size EAR_addSegment(EAR* ear, EAR_Size vmaddr, EAR_Size vmsize, const EAR_PageNumber* phys_page_array, EAR_Protection vmprot, EAR_Size fault_physaddr);

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
 * @brief Set the callback function used to handle unhandled page faults
 * 
 * @param fault_fn Function pointer called during an unhandled page fault
 * @param cookie Opaque value passed to fault_fn
 */
void EAR_setFaultHandler(EAR* ear, EAR_FaultHandler* fault_fn, void* cookie);

/*!
 * @brief Set the callback function used whenever an instruction accesses memory
 * 
 * @param mem_fn Function pointer called during all memory accesses
 * @param cookie Opaque value passed to mem_fn
 */
void EAR_setMemoryHook(EAR* ear, EAR_MemoryHook* mem_fn, void* cookie);

/*!
 * @brief Set the callback function used when execution resumes
 * 
 * @param debug_fn Function pointer called when execution is about to start
 * @param cookie Opaque value passed to debug_fn
 */
void EAR_attachDebugger(EAR* ear, EAR_DebugAttach* debug_fn, void* cookie);

/*!
 * @brief Fetch the next code instruction starting at *pc.
 * 
 * @param pc In/out pointer to the address of the instruction to fetch. This will
 *           be written back with the address of the code byte directly following
 *           the instruction that was fetched.
 * @param dpc Value of DPC register (delta PC) to use while fetching code bytes
 * @param out_insn Output pointer where the decoded instruction will be written
 * 
 * @return Reason for halting, typically HALT_NONE
 */
EAR_HaltReason EAR_fetchInstruction(EAR* ear, EAR_Size* pc, EAR_Size dpc, EAR_Instruction* out_insn);

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
	EAR_Size func_vmaddr,
	EAR_Size func_dpc,
	EAR_Size arg1,
	EAR_Size arg2,
	EAR_Size arg3,
	EAR_Size arg4,
	EAR_Size arg5,
	EAR_Size arg6,
	bool run
);

/*!
 * @brief Copy data into EAR virtual memory.
 * 
 * @param dst Destination virtual address
 * @param src Source pointer
 * @param size Number of bytes to copy
 * @param prot Type of memory to write to (exactly one bit should be set)
 * @param out_r Output pointer where the halt reason will be written (or NULL).
 *              Will be HALT_NONE when all bytes are copied successfully.
 * 
 * @return Number of bytes copied
 */
EAR_Size EAR_copyin(EAR* ear, EAR_Size dst, const void* src, EAR_Size size, EAR_Protection prot, EAR_HaltReason* out_r);

/*!
 * @brief Copy data out from EAR virtual memory.
 * 
 * @param dst Destination pointer
 * @param src Source virtual address
 * @param size Number of bytes to copy
 * @param prot Type of memory to read from (exactly one bit should be set)
 * @param out_r Output pointer where the halt reason will be written (or NULL).
 *              Will be HALT_NONE when all bytes are copied successfully.
 * 
 * @return Number of bytes copied
 */
EAR_Size EAR_copyout(EAR* ear, void* dst, EAR_Size src, EAR_Size size, EAR_Protection prot, EAR_HaltReason* out_r);

/*!
 * @brief Copy data into EAR physical memory, truncating if necessary.
 * 
 * @param dst_ppns Array of physical page numbers to copy to
 * @param dst_page_count Number of pages described by the dst_ppns array
 * @param dst_offset Number of bytes to skip in the destination physical region
 * @param src Source pointer
 * @param size Number of bytes to copy
 * 
 * @return Number of bytes copied
 */
EAR_Size EAR_copyinPhys(EAR* ear, EAR_PageNumber* dst_ppns, uint8_t dst_page_count, EAR_Size dst_offset, const void* src, EAR_Size size);

/*!
 * @brief Copy data from EAR physical memory, truncating if necessary.
 * 
 * @param dst Destination pointer
 * @param src_ppns Array of source physical page numbers to copy from
 * @param src_page_count Number of pages described by the src_ppns array
 * @param src_offset Number of bytes to skip in the source physical region
 * @param size Number of bytes to copy
 * 
 * @return Number of bytes copied
 */
EAR_Size EAR_copyoutPhys(EAR* ear, void* dst, EAR_PageNumber* src_ppns, uint8_t src_page_count, EAR_Size src_offset, EAR_Size size);

/*!
 * @brief Get a pointer to an EAR physical memory address range.
 * 
 * @param paddr EAR physical address
 * @param size In: number of bytes requested. Out: number of bytes available (potentially truncated)
 * 
 * @return Pointer into the EAR emulator's physical memory region corresponding to paddr
 */
void* EAR_getPhys(EAR* ear, EAR_Size paddr, EAR_Size* size);

/*!
 * @brief Allocates multiple physical pages, returning their page numbers in an array.
 * 
 * @param num_pages Number of physical pages to allocate
 * @param out_phys_array Array of physical page numbers allocated
 * 
 * @return Number of pages successfully allocated, will be less than num_pages when
 *         physical memory runs out.
 */
uint8_t EAR_allocPhys(EAR* ear, uint8_t num_pages, EAR_PageNumber* out_phys_array);

/*! Converts an EAR halt reason code to a string description. */
const char* EAR_haltReasonToString(EAR_HaltReason status);

/*! Given an EAR opcode, return a string representing that instruction's mnemonic */
const char* EAR_getMnemonic(EAR_Opcode op);

/*! Given an EAR condition code, return a string representing the condition */
const char* EAR_getConditionString(EAR_Cond cond);

/*! Given an EAR register number, return a string name of the register */
const char* EAR_getRegisterName(EAR_Register reg);

/*!
 * @brief Prints a description of an EAR instruction.
 * 
 * @param insn Instruction to print
 * @param fp Stream where the instruction should be written
 */
void EAR_writeInstruction(EAR* ear, EAR_Instruction* insn, FILE* fp);

/*!
 * @brief Disassemble a number of instructions from the given starting point.
 * 
 * @param addr Code address where disassembling should start
 * @param dpc DPC (delta PC) value to use while fetching code bytes for disassembly
 * @param count Number of instructions to disassemble
 * @param fp Output stream where disassembled instructions should be written
 * 
 * @return Number of instructions disassembled
 */
EAR_Size EAR_writeDisassembly(EAR* ear, EAR_Size addr, EAR_Size dpc, EAR_Size count, FILE* fp);

/*! Writes the active thread context register state(s) to the output stream. */
void EAR_writeRegs(EAR* ear, FILE* fp);

/*! Writes the virtual memory regions to the output stream. */
void EAR_writeVMMap(EAR* ear, FILE* fp);


/*! Global flag that is set when a keyboard interrupt is caught. */
volatile sig_atomic_t g_interrupted;

/*! Sets up the signal handler for keyboard interrupts. */
void enable_interrupt_handler(void);

/*! Tears down the signal handler for keyboard interrupts. */
void disable_interrupt_handler(void);

#endif /* EAR_EAR_H */
