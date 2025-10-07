#ifndef EAR_TYPES_H
#define EAR_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

// Machine data types
typedef uint8_t EAR_Byte;
#define EAR_REGISTER_BITS 16U
typedef uint16_t EAR_UWord;
typedef int16_t EAR_SWord;
#define EAR_UWORD_MAX ((EAR_UWord)UINT16_MAX)
#define EAR_SWORD_MIN ((EAR_SWord)INT16_MIN)
#define EAR_SWORD_MAX ((EAR_SWord)INT16_MAX)
#define EAR_SIGN_BIT ((EAR_UWord)(1U << (EAR_REGISTER_BITS - 1)))

// Virtual addresses are the same size as a register
typedef EAR_UWord EAR_VirtAddr;
#define EAR_VIRTUAL_ADDRESS_BITS EAR_REGISTER_BITS

// Physical addresses are 24-bit, as we have a 23-bit physical memory bus which
// operates on 16-bit words.
typedef uint32_t EAR_PhysAddr;
#define EAR_PHYSICAL_ADDRESS_BITS 24U

// Used for holding either a virtual or physical address
typedef uint32_t EAR_FullAddr;


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


// Control register numbers
typedef EAR_Register EAR_ControlRegister;
#define CR0 ((EAR_ControlRegister)0)
#define CR1 ((EAR_ControlRegister)1)
#define CR2 ((EAR_ControlRegister)2)
#define CR3 ((EAR_ControlRegister)3)
#define CR4 ((EAR_ControlRegister)4)
#define CR5 ((EAR_ControlRegister)5)
#define CR6 ((EAR_ControlRegister)6)
#define CR7 ((EAR_ControlRegister)7)
#define CR8 ((EAR_ControlRegister)8)
#define CR9 ((EAR_ControlRegister)9)
#define CR10 ((EAR_ControlRegister)10)
#define CR11 ((EAR_ControlRegister)11)
#define CR12 ((EAR_ControlRegister)12)
#define CR13 ((EAR_ControlRegister)13)
#define CR14 ((EAR_ControlRegister)14)
#define CR15 ((EAR_ControlRegister)15)

// Control register aliases
#define CR_CREG_DENY_R     CR0
#define CR_CREG_DENY_W     CR1
#define CR_INSN_DENY_0     CR2
#define CR_INSN_DENY_1     CR3
#define CR_INSN_COUNT_LO   CR4
#define CR_INSN_COUNT_HI   CR5
#define CR_EXEC_STATE_0    CR6
#define CR_EXEC_STATE_1    CR7
#define CR_MEMBASE_R       CR8
#define CR_MEMBASE_W       CR9
#define CR_MEMBASE_X      CR10
#define CR_EXC_INFO       CR11
#define CR_EXC_ADDR       CR12
#define CR_TIMER          CR13
#define CR_INSN_ADDR      CR14
#define CR_FLAGS          CR15

#define MMU_ENABLED (EAR_UWord)(1 << 0)
#define MEMBASE_REGION_SHIFT 8


// Condition flag values
typedef EAR_UWord EAR_Flag;
#define FLAG_ZF ((EAR_Flag)(1 << 0))
#define FLAG_SF ((EAR_Flag)(1 << 1))
#define FLAG_PF ((EAR_Flag)(1 << 2))
#define FLAG_CF ((EAR_Flag)(1 << 3))
#define FLAG_VF ((EAR_Flag)(1 << 4))

// Non-flag bits shoved into the same CR
#define FLAG_DENY_XREGS ((EAR_Flag)(1 << 5))
#define FLAG_RESUME ((EAR_Flag)(1 << 6))


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
#define OP_RDC ((EAR_Opcode)0x0E)
#define OP_WRC ((EAR_Opcode)0x0F)
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
#define PREFIX_XX ((EAR_Opcode)0x02)
#define PREFIX_XY ((EAR_Opcode)0x03)
#define PREFIX_XZ ((EAR_Opcode)0x04)
#define PREFIX_RSV_MASK ((EAR_Opcode)0x0F)
#define PREFIX_DR_MASK ((EAR_Opcode)0x10)


// Decoding bitmaps
#define OP_BIT(op) ((uint32_t)(1U << (op)))
#define INSN_ALLOWS_DR_BITMAP \
	( OP_BIT(OP_ADD) | OP_BIT(OP_SUB) | OP_BIT(OP_MLU) | OP_BIT(OP_MLS) \
	| OP_BIT(OP_DVU) | OP_BIT(OP_DVS) | OP_BIT(OP_XOR) | OP_BIT(OP_AND) \
	| OP_BIT(OP_ORR) | OP_BIT(OP_SHL) | OP_BIT(OP_SRU) | OP_BIT(OP_SRS) \
	| OP_BIT(OP_LDW) | OP_BIT(OP_STW) | OP_BIT(OP_LDB) | OP_BIT(OP_STB) \
	)

// Address space geometry
#define EAR_NULL ((EAR_VirtAddr)0x0000U)
#define EAR_PAGE_BITS 8U
#define EAR_PAGE_SHIFT 8U
#define EAR_PAGE_SIZE (1U << EAR_PAGE_SHIFT)
static_assert(EAR_PAGE_SIZE == 0x100U, "bad page size");
#define EAR_PAGE_COUNT (1U << EAR_PAGE_BITS)
static_assert(EAR_PAGE_COUNT == 0x100U, "bad page count");

static_assert(EAR_VIRTUAL_ADDRESS_BITS == EAR_PAGE_BITS + EAR_PAGE_SHIFT, "page size doesn't add up");
#define EAR_VIRTUAL_ADDRESS_SPACE_SIZE (1U << (EAR_PAGE_BITS + EAR_PAGE_SHIFT))
#define EAR_REGION_BITS 8U
#define EAR_REGION_SHIFT 16U
static_assert(EAR_REGION_SHIFT == EAR_PAGE_BITS + EAR_PAGE_SHIFT, "region size doesn't add up");

static_assert(EAR_PHYSICAL_ADDRESS_BITS == EAR_REGION_BITS + EAR_REGION_SHIFT, "physical address size doesn't add up");
#define EAR_PHYSICAL_ADDRESS_SPACE_SIZE (1U << EAR_PHYSICAL_ADDRESS_BITS)
#define EAR_REGION_COUNT (1U << EAR_REGION_BITS)
static_assert(EAR_REGION_COUNT == 0x100U, "bad region count");

// Helper macros for page math
#define EAR_PAGE_NUMBER(addr) (((EAR_VirtAddr)(addr) >> EAR_PAGE_SHIFT) & (EAR_PAGE_COUNT - 1))
static_assert(EAR_PAGE_NUMBER(0x1234) == 0x12, "bad page number calculation");

#define EAR_PAGE_OFFSET(addr) ((EAR_VirtAddr)(addr) & (EAR_PAGE_SIZE - 1))
static_assert(EAR_PAGE_OFFSET(0x1234) == 0x34, "bad page offset calculation");

#define EAR_IS_PAGE_ALIGNED(addr) (EAR_PAGE_OFFSET(addr) == 0)
static_assert(EAR_IS_PAGE_ALIGNED(0x1200), "fail");
static_assert(!EAR_IS_PAGE_ALIGNED(0x1201), "fail");
static_assert(!EAR_IS_PAGE_ALIGNED(0x1234), "fail");
static_assert(!EAR_IS_PAGE_ALIGNED(0x12FF), "fail");

#define EAR_FLOOR_PAGE(addr) ((uint32_t)((uint32_t)(addr) & ~(EAR_PAGE_SIZE - 1)))
static_assert(EAR_FLOOR_PAGE(0x1200) == 0x1200, "fail");
static_assert(EAR_FLOOR_PAGE(0x1234) == 0x1200, "fail");
static_assert(EAR_FLOOR_PAGE(0x12FF) == 0x1200, "fail");

#define EAR_CEIL_PAGE(addr) EAR_FLOOR_PAGE((uint32_t)(addr) + EAR_PAGE_SIZE - 1)
static_assert(EAR_CEIL_PAGE(0x1200) == 0x1200, "fail");
static_assert(EAR_CEIL_PAGE(0x1234) == 0x1300, "fail");
static_assert(EAR_CEIL_PAGE(0x12FF) == 0x1300, "fail");

#define EAR_FULL_REGION(addr) (((addr) >> EAR_REGION_SHIFT) & (EAR_REGION_COUNT - 1))
#define EAR_FULL_NOTREGION(addr) ((addr) & (EAR_VIRTUAL_ADDRESS_SPACE_SIZE - 1))
#define EAR_FULL_PAGE(addr) (((addr) >> EAR_PAGE_SHIFT) & (EAR_PAGE_COUNT - 1))
#define EAR_FULL_OFFSET(addr) ((addr) & (EAR_PAGE_SIZE - 1))

// Architecture structures
typedef struct EAR_ThreadState EAR_ThreadState;
typedef struct EAR_Context EAR_Context;
typedef struct EAR_Instruction EAR_Instruction;

// Special values used to decide when the program returns from an externally invoked function
#define EAR_CALL_RA ((EAR_UWord)0xFFFF)
#define EAR_CALL_RD ((EAR_UWord)0xFFFF)

// Endianness
#define EAR_LITTLE_ENDIAN 0x3412
#define EAR_BIG_ENDIAN 0x1234
#define EAR_BYTE_ORDER EAR_LITTLE_ENDIAN

// Virtual memory protections
typedef uint8_t EAR_Protection; //lol
#define EAR_PROT_NONE ((EAR_Protection)0)
#define EAR_PROT_READ ((EAR_Protection)(1 << 0))
#define EAR_PROT_WRITE ((EAR_Protection)(1 << 1))
#define EAR_PROT_EXECUTE ((EAR_Protection)(1 << 2))

// Physical memory access modes
typedef uint8_t Bus_AccessMode;
#define BUS_MODE_READ ((Bus_AccessMode)(1 << 0))
#define BUS_MODE_WRITE ((Bus_AccessMode)(1 << 1))
#define BUS_MODE_RDWR ((Bus_AccessMode)(BUS_MODE_READ | BUS_MODE_WRITE))

// Main processor structure
typedef struct EAR EAR;

// Processor halt reason
typedef enum EAR_HaltReason {
	HALT_UNALIGNED         = -1,  //!< Tried to access a word at an unaligned (odd) memory address
	HALT_MMU_FAULT         = -2,  //!< Accessed unmapped virtual memory
	HALT_BUS_FAULT         = -3,  //!< Accessed unmapped physical memory
	HALT_BUS_PROTECTED     = -4,  //!< Invalid physical memory access mode
	HALT_BUS_ERROR         = -5,  //!< Bus peripheral error
	HALT_DENIED            = -6,  //!< Denied instruction, prefix, or control register
	HALT_DECODE            = -7,  //!< Tried to execute an illegal instruction
	HALT_DOUBLE_FAULT      = -8,  //!< Kernel panic
	HALT_IO_ERROR          = -9,  //!< I/O error
	HALT_NONE = 0,                //!< No unusual halt reason
	HALT_EXCEPTION,               //!< An exception is being raised and will be handled on the next cycle
	HALT_BREAKPOINT,              //!< A breakpoint was hit
	HALT_DEBUGGER,                //!< Halted by the debugger
	HALT_RETURN,                  //!< Program tried to return from the topmost stack frame
	HALT_COMPLETE,                //!< For internal use only, used by callbacks to mark completion
} EAR_HaltReason;
#define EAR_FAILED(haltReason) ((haltReason) < 0)

// Exception info stored in `EXC_INFO` control register
typedef uint16_t EAR_ExceptionInfo;
#define EXC_NONE ((EAR_ExceptionInfo)0)

#define EXC_CODE_SHIFT 1U

#define EXC(code) (EAR_ExceptionInfo)((((EAR_UWord)(code) & 0x7) << EXC_CODE_SHIFT) | 1)
#define EXC_UNALIGNED    EXC(0x00)
#define EXC_MMU          EXC(0x01)
#define EXC_BUS          EXC(0x02)
#define EXC_DECODE       EXC(0x03)
#define EXC_ARITHMETIC   EXC(0x04)
#define EXC_DENIED_CREG  EXC(0x05)
#define EXC_DENIED_INSN  EXC(0x06)
#define EXC_TIMER        EXC(0x07)

#define EXC_CODE_GET(exc) (((EAR_UWord)(exc) >> EXC_CODE_SHIFT) & 0x7)

#define EXC_MMU_MAKE(prot, unaligned) \
EXC_FAULT_MAKE_(EXC_MMU, prot, unaligned)

#define EXC_BUS_MAKE(prot, unaligned) \
EXC_FAULT_MAKE_(EXC_BUS, prot, unaligned)

// Prot takes 2 bits, put them at the top of the lower 16-bit word to
// save room for more bits in the exc code and other fields.
#define EXC_FAULT_PROT_SHIFT 14

#define EXC_FAULT_MAKE(hr, prot) ({ \
	EAR_ExceptionInfo _ei; \
	if((hr) == HALT_UNALIGNED) { \
		_ei = EXC_UNALIGNED; \
	} \
	else { \
		_ei = ( \
			(hr) == HALT_BUS_FAULT || \
			(hr) == HALT_BUS_PROTECTED \
		) ? EXC_BUS : EXC_MMU; \
	} \
	EAR_Protection _prot = (prot); \
	EAR_UWord _mode = 0; \
	if(_prot == EAR_PROT_READ) { \
		_mode = 1; \
	} \
	else if(_prot == EAR_PROT_WRITE) { \
		_mode = 2; \
	} \
	else if(_prot == EAR_PROT_EXECUTE) { \
		_mode = 3; \
	} \
	\
	_ei |= _mode << EXC_FAULT_PROT_SHIFT; \
	_ei; \
})

#define EXC_FAULT_PROT(exc) ({ \
	uint8_t _mode = ((exc) >> EXC_FAULT_PROT_SHIFT) & 3; \
	EAR_Protection _prot = EAR_PROT_NONE; \
	if(_mode == 1) { \
		_prot = EAR_PROT_READ; \
	} \
	else if(_mode == 2) { \
		_prot = EAR_PROT_WRITE; \
	} \
	else if(_mode == 3) { \
		_prot = EAR_PROT_EXECUTE; \
	} \
	_prot; \
})

// Mask of exception kinds
typedef uint32_t EAR_ExceptionMask;
#define EXC_MASK_HLT ((EAR_ExceptionMask)1 << 31)
#define EXC_MASK_UNALIGNED ((EAR_ExceptionMask)1 << EXC_CODE_GET(EXC_UNALIGNED))
#define EXC_MASK_MMU ((EAR_ExceptionMask)1 << EXC_CODE_GET(EXC_MMU))
#define EXC_MASK_BUS ((EAR_ExceptionMask)1 << EXC_CODE_GET(EXC_BUS))
#define EXC_MASK_DECODE ((EAR_ExceptionMask)1 << EXC_CODE_GET(EXC_DECODE))
#define EXC_MASK_ARITHMETIC ((EAR_ExceptionMask)1 << EXC_CODE_GET(EXC_ARITHMETIC))
#define EXC_MASK_DENIED_CREG ((EAR_ExceptionMask)1 << EXC_CODE_GET(EXC_DENIED_CREG))
#define EXC_MASK_DENIED_INSN ((EAR_ExceptionMask)1 << EXC_CODE_GET(EXC_DENIED_INSN))
#define EXC_MASK_TIMER ((EAR_ExceptionMask)1 << EXC_CODE_GET(EXC_TIMER))

#define EXC_MASK_NONE ((EAR_ExceptionMask)0)
#define EXC_MASK_ALL \
	( EXC_MASK_HLT \
	| EXC_MASK_UNALIGNED \
	| EXC_MASK_MMU \
	| EXC_MASK_BUS \
	| EXC_MASK_DECODE \
	| EXC_MASK_ARITHMETIC \
	| EXC_MASK_DENIED_CREG \
	| EXC_MASK_DENIED_INSN \
	| EXC_MASK_TIMER \
	)

struct EAR_Instruction {
	EAR_Cond cond;            //!< Condition code
	EAR_Opcode op;            //!< Opcode
	
	EAR_Register rd;          //!< Rd register number, destination which is usually the same as Rx
	EAR_Register rdx;         //!< Rdx register number, used for the extended destination in some instructions
	EAR_Register rx;          //!< Rx register number, first operand and often the destination
	EAR_Register ry;          //!< Ry register number, often the source
	EAR_UWord imm;            //!< Immediate value: Vy or register bitmap (for PSH and POP)
	
	uint8_t port_number;      //!< Port number (for RDB and WRB)
	
	uint8_t toggle_flags : 1; //!< Whether the TF instruction modifier was present
	uint8_t cross_rx : 1;     //!< Whether the Rx register refers to the inactive thread context
	uint8_t cross_ry : 1;     //!< Whether the Ry register refers to the inactive thread context
	uint8_t cross_rd : 1;     //!< Whether the Rd & Rdx registers refer to the inactive thread context
};

struct EAR_ThreadState {
	EAR_UWord r[16];          //!< Register values
	EAR_UWord cr[16];         //!< Control register values
	EAR_Instruction insn;     //!< Decoded instruction
};

struct EAR_Context {
	EAR_ThreadState banks[2]; //!< Thread state, dual bank
	uint8_t active : 1;       //!< Index of active thread state bank
};

/*!
 * @brief Function pointer type for bus access hooks. Can either silently observe
 * the access or override it with a custom implementation.
 * 
 * @param cookie Opaque value passed to the callback
 * @param mode Either BUS_MODE_READ or BUS_MODE_WRITE, indicating the memory operation
 * @param paddr Full 24-bit address of the memory access
 * @param is_byte True if the access is a byte access, false for word access
 * @param data Pointer to the data buffer to read from/write to
 * 
 * @return Number of bytes copied
 */
typedef EAR_HaltReason Bus_Hook(
	void* cookie, Bus_AccessMode mode,
	EAR_PhysAddr paddr, bool is_byte, void* data
);

/*!
 * @brief Handle a bus access.
 * 
 * @param cookie Opaque value passed to the callback
 * @param mode Either BUS_MODE_READ or BUS_MODE_WRITE, indicating the memory operation
 * @param paddr Full 24-bit address of the memory access
 * @param is_byte True if the access is a byte access, false for word access
 * @param data Pointer to the data buffer to read from/write to
 * 
 * @return True if the access was successful
 */
typedef bool Bus_AccessHandler(
	void* cookie, Bus_AccessMode mode,
	EAR_PhysAddr paddr, bool is_byte, void* data,
	EAR_HaltReason* out_r
);

typedef void Bus_DumpFunc(void* cookie, FILE* fp);

// Memory structures
typedef uint16_t MMU_PTE;
typedef struct MMU_PageTable MMU_PageTable;
struct MMU_PageTable {
	//! Translation table for the address space
	MMU_PTE entries[EAR_PAGE_COUNT];
};

#define MMU_PTE_INVALID(pte) ((pte) >> 8 == 0xFFU)

typedef struct MMU MMU;

/*!
 * @brief Function called to handle virtual memory accesses.
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
typedef bool EAR_MemoryHandler(
	void* cookie, EAR_Protection prot, Bus_AccessMode mode,
	EAR_FullAddr vmaddr, bool is_byte, void* data, EAR_HaltReason* out_r
);

typedef EAR_HaltReason EAR_PortRead(void* cookie, uint8_t port, EAR_Byte* out_byte);
typedef EAR_HaltReason EAR_PortWrite(void* cookie, uint8_t port, EAR_Byte byte);
typedef EAR_HaltReason EAR_ExecHook(void* cookie, EAR_Instruction* insn, EAR_FullAddr pc, bool before, bool cond);

#endif /* EAR_TYPES_H */
