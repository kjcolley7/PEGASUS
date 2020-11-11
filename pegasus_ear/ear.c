//
//  ear.c
//  PegasusEar
//
//  Created by Kevin Colley on 3/25/17.
//

#include "ear.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include "common/macros.h"


#ifdef EAR_DEVEL

#define DEVEL_RETURN(ear, reason) do { \
	if((ear)->debug_flags & DEBUG_VERBOSE) { \
		fprintf(stderr, STRINGIFY(reason) "(" STRINGIFY(__LINE__) ")\n"); \
	} \
	return (reason); \
} while(0)

#else /* EAR_DEVEL */

#define DEVEL_RETURN(ear, reason) do { \
	return (reason); \
} while(0)

#endif /* EAR_DEVEL */

static inline EAR_PageTable* EAR_getPageTable(EAR* ear);
static inline EAR_PTE* EAR_getPTE(EAR* ear, EAR_PageNumber ppn);
static EAR_HaltReason EAR_invokeFaultHandler(EAR* ear, EAR_Size fault_handler, EAR_Size tte_paddr, EAR_Size vmaddr, EAR_Protection prot, EAR_Size* out_paddr);
static EAR_HaltReason EAR_translate(EAR* ear, EAR_Size vmaddr, EAR_Protection prot, EAR_Size* out_paddr);
static EAR_HaltReason EAR_readByte(EAR* ear, EAR_Size addr, EAR_Byte* out_byte);
static EAR_HaltReason EAR_writeByte(EAR* ear, EAR_Size addr, EAR_Byte byte);
static EAR_HaltReason EAR_readWord(EAR* ear, EAR_Size addr, EAR_Size* out_word);
static EAR_HaltReason EAR_writeWord(EAR* ear, EAR_Size addr, EAR_Size word);
static EAR_HaltReason EAR_fetchCodeByte(EAR* ear, EAR_Size* pc, EAR_Size dpc, uint8_t* out_byte);
static EAR_HaltReason EAR_fetchCodeImm16(EAR* ear, EAR_Size* pc, EAR_Size dpc, EAR_Size* out_value);
static EAR_HaltReason EAR_executeInstruction(EAR* ear, EAR_Instruction* insn);


volatile sig_atomic_t g_interrupted = 0;
static void interrupt_handler(int sig) {
	(void)sig;
	
	// Flag the process as having been interrupted
	g_interrupted = 1;
}

static struct sigaction old_handler;
void enable_interrupt_handler(void) {
	g_interrupted = 0;
	
	struct sigaction sa;
	sa.sa_flags = SA_RESETHAND;
	sa.sa_handler = &interrupt_handler;
	
	sigaction(SIGINT, &sa, &old_handler);
}

void disable_interrupt_handler(void) {
	if(!g_interrupted) {
		sigaction(SIGINT, &old_handler, NULL);
	}
}

static inline EAR_PageTable* EAR_getPageTable(EAR* ear) {
	return (EAR_PageTable*)&ear->mem.bytes[EAR_TTB_PADDR];
}

static inline EAR_PTE* EAR_getPTE(EAR* ear, EAR_PageNumber ppn) {
	EAR_PTE* phat = &ear->mem.bytes[EAR_PHYSICAL_ALLOCATION_TABLE_PADDR];
	return &phat[ppn];
}

static EAR_HaltReason EAR_invokeFaultHandler(EAR* ear, EAR_Size fault_handler, EAR_Size tte_paddr, EAR_Size vmaddr, EAR_Protection prot, EAR_Size* out_paddr) {
	EAR_HaltReason ret = HALT_NONE;
	
	// Zero-initialize the exception context
	memset(&ear->exc_ctx, 0, sizeof(ear->exc_ctx));
	
	// Set the disable MMU flag
	ear->exc_ctx.flags |= FLAG_MF;
	
	// Replace PC register value with the address of the currently executing instruction
	EAR_Size next_pc = ear->context.r[PC];
	ear->context.r[PC] = ear->context.cur_pc;
	
	// Copy register values from standard stack to the top of the exception stack
	EAR_Size saved_regs = EAR_EXCEPTION_STACK_TOP - sizeof(ear->context.r);
	memcpy(&ear->mem.bytes[saved_regs], ear->context.r, sizeof(ear->context.r));
	
	// Set SP to the top of the exception stack location
	ear->exc_ctx.r[SP] = saved_regs;
	
	// Set the exception context as the CPU's active thread context
	ear->active = &ear->exc_ctx;
	
	// Convert prot from a bit value to a number
	EAR_Size protnum = 0;
	switch(prot) {
		case EAR_PROT_READ:
			protnum = 0;
			break;
		
		case EAR_PROT_WRITE:
			protnum = 1;
			break;
		
		case EAR_PROT_EXECUTE:
			protnum = 2;
			break;
		
		default:
			ASSERT(false);
	}
	
	// Invoke page fault handler function
	ret = EAR_invokeFunction(ear, fault_handler, 0, tte_paddr, vmaddr, protnum, saved_regs, next_pc, 0, true);
	if(ret != HALT_NONE) {
		return ret;
	}
	
	// Use fault handler function's return value as the resulting physical address
	*out_paddr = ear->active->r[RV];
	
	// Copy register values back into the standard context
	memcpy(&ear->context.r, &ear->mem.bytes[saved_regs], sizeof(ear->context.r));
	
	// If the fault handler stores a nonzero value in the the saved ZERO register, the
	// faulting instruction is marked as complete and it will not attempt to finish executing.
	if(ear->context.r[ZERO] != 0) {
		ear->context.r[ZERO] = 0;
		ret = HALT_COMPLETE;
	}
	
	// Fix up PC
	if(ear->context.r[PC] != ear->context.cur_pc) {
		ret = HALT_COMPLETE;
	}
	
	// Swap back to using the standard context as the CPU's active thread context
	ear->active = &ear->context;
	
#if EAR_CLEAR_EXCEPTION_STACK
	// Clear exception stack upon return from exception handler function
	memset(&ear->mem.bytes[EAR_EXCEPTION_STACK_BOTTOM], 0, EAR_EXCEPTION_STACK_SIZE);
#endif /* EAR_CLEAR_EXCEPTION_STACK */
	
	// Will return either HALT_NONE or HALT_COMPLETE
	return ret;
}

/*! Translate an attempt to access a virtual address with the given type of access into
 * the physical address backing that address and the virtual address of a function to be
 * called to handle page faults.
 *
 * @param vmaddr Virtual address to translate
 * @param prot Attempted access permissions, exactly one of read, write, or execute
 * @param out_paddr Output variable that will hold the physical address of the memory
 *        page that backs this virtual address.
 * @return HALT_NONE if translation succeeds, halt reason otherwise.
 */
static EAR_HaltReason EAR_translate(EAR* ear, EAR_Size vmaddr, EAR_Protection prot, EAR_Size* out_paddr) {
	EAR_PageTable* ttable = EAR_getPageTable(ear);
	EAR_TTE* tte = &ttable->entries[EAR_PAGE_NUMBER(vmaddr)];
	EAR_PageNumber ppn;
	EAR_HaltReason r = HALT_NONE;
	
	// Default output value
	*out_paddr = 0;
	
	// Disable MMU?
	if(ear->active->flags & FLAG_MF) {
		ppn = EAR_PAGE_NUMBER(vmaddr);
	}
	else {
		// Look up physical page number based on the requested access type
		if(prot == EAR_PROT_READ) {
			ppn = tte->r_ppn;
		}
		else if(prot == EAR_PROT_WRITE) {
			ppn = tte->w_ppn;
		}
		else if(prot == EAR_PROT_EXECUTE) {
			ppn = tte->x_ppn;
		}
		else {
			ASSERT(false);
		}
		
		// Is this virtual page mapped with the requested access permission?
		if(ppn == 0) {
			// Are we already handling an exception?
			if(ear->active == &ear->exc_ctx) {
				r = HALT_DOUBLEFAULT;
			}
			
			// See if there's a fault handler function to run when in invasive mode
			if(r == HALT_NONE && ((ear->debug_flags & DEBUG_NOFAULT) || tte->fault_ppn == 0)) {
				r = HALT_UNMAPPED;
			}
			
			// Invoke external fault handler if registered and there's no internal fault handler for this TTE
			if(r != HALT_NONE) {
				if(ear->fault_fn != NULL) {
					return ear->fault_fn(ear->fault_cookie, vmaddr, prot, tte, r, out_paddr);
				}
				
				// No internal or external fault handler, so raise the halt reason
				return r;
			}
			
			// Calculate physical address of this TTE
			EAR_Size tte_paddr = EAR_TTB_PADDR + EAR_PAGE_NUMBER(vmaddr) * sizeof(EAR_TTE);
			
			// Call fault handler function to resolve the virtual memory access
			return EAR_invokeFaultHandler(ear, (EAR_Size)tte->fault_ppn * EAR_PAGE_SIZE, tte_paddr, vmaddr, prot, out_paddr);
		}
	}
	
	// Check if the physical page is accessible
	EAR_PTE* pte = EAR_getPTE(ear, EAR_PAGE_NUMBER(vmaddr));
	if(!(*pte & PHYS_ALLOW)) {
		return HALT_DOUBLEFAULT;
	}
	
	// Compute output physical address
	*out_paddr = (EAR_Size)ppn * EAR_PAGE_SIZE + EAR_PAGE_OFFSET(vmaddr);
	return HALT_NONE;
}

/*!
 * @brief Initialize an EAR CPU with default register values and memory mappings
 * 
 * @param debugFlags Initial debug flags
 */
void EAR_init(EAR* ear, EAR_DebugFlags debugFlags) {
	EAR_PageNumber ppn;
	
	memset(ear, 0, sizeof(*ear));
	
	// Set initial debug flags early as they may affect some of the later code
	ear->debug_flags = debugFlags;
	
	// Mark NULL phys page as used but not allowed
	*EAR_getPTE(ear, EAR_PAGE_NUMBER(EAR_NULL)) = PHYS_IN_USE | PHYS_DENY;
	
	// Mark PHAT as used and allowed
	*EAR_getPTE(ear, EAR_PAGE_NUMBER(EAR_PHYSICAL_ALLOCATION_TABLE_PADDR)) = PHYS_IN_USE | PHYS_ALLOW;
	
	// Add page table region mirroring its physical address range
	EAR_PageNumber pageTablePPNs[sizeof(EAR_PageTable) / EAR_PAGE_SIZE];
	unsigned i;
	for(i = 0; i < ARRAY_COUNT(pageTablePPNs); i++) {
		pageTablePPNs[i] = EAR_PAGE_NUMBER(EAR_TTB_PADDR) + i;
		*EAR_getPTE(ear, pageTablePPNs[i]) = PHYS_IN_USE | PHYS_ALLOW;
	}
	EAR_addSegment(ear, EAR_TTB_PADDR, sizeof(EAR_PageTable), pageTablePPNs, EAR_PROT_READ | EAR_PROT_WRITE, EAR_NULL);
	
	// Add exception stack top guard and set the fault vmaddr to itself to mark it as used
	*EAR_getPTE(ear, EAR_PAGE_NUMBER(EAR_EXCEPTION_STACK_TOP_GUARD)) = PHYS_IN_USE | PHYS_DENY;
	
	// Add exception stack contents
	for(ppn = EAR_PAGE_NUMBER(EAR_EXCEPTION_STACK_BOTTOM); ppn != EAR_PAGE_NUMBER(EAR_EXCEPTION_STACK_TOP); ppn++) {
		*EAR_getPTE(ear, ppn) = PHYS_IN_USE | PHYS_ALLOW;
	}
	
	// Add exception stack bottom guard
	*EAR_getPTE(ear, EAR_PAGE_NUMBER(EAR_EXCEPTION_STACK_BOTTOM_GUARD)) = PHYS_IN_USE | PHYS_DENY;
	
	// The initial physical page layout is complete. It is now safe to use EAR_allocPhys().
	
	// Add virtual stack top guard page
	EAR_addSegment(ear, EAR_STACK_TOP_GUARD, EAR_PAGE_SIZE, NULL, EAR_PROT_NONE, EAR_EXCEPTION_STACK_TOP_GUARD);
	
	// Add virtual stack region
	EAR_PageNumber stackPPNs[EAR_STACK_SIZE / EAR_PAGE_SIZE];
	if(EAR_allocPhys(ear, ARRAY_COUNT(stackPPNs), stackPPNs) != ARRAY_COUNT(stackPPNs)) {
		abort();
	}
	EAR_addSegment(
		ear,
		EAR_STACK_BOTTOM,                           //vmaddr
		EAR_STACK_SIZE,                             //vmsize
		stackPPNs,                                  //physaddr
		EAR_PROT_READ | EAR_PROT_WRITE,             //vmprot
		EAR_NULL                                    //fault_vmaddr
	);
	
	// Add virtual stack bottom guard
	EAR_addSegment(ear, EAR_STACK_BOTTOM_GUARD, EAR_PAGE_SIZE, NULL, EAR_PROT_NONE, EAR_EXCEPTION_STACK_BOTTOM_GUARD);
	
	// Init registers
	EAR_resetRegisters(ear);
	
	// Set standard thread state as the active one
	ear->active = &ear->context;
}

/*! Reset the normal thread state to its default values */
void EAR_resetRegisters(EAR* ear) {
	memset(&ear->context, 0, sizeof(ear->context));
	
	// Set RA and RD to determine when the program tries to return from a top-level function
	ear->context.r[RA] = EAR_CALL_RA;
	ear->context.r[RD] = EAR_CALL_RD;
	
	// All registers are initially zero
	ear->context.flags = FLAG_ZF;
	
	// Set stack registers
	ear->context.r[SP] = EAR_STACK_TOP;
	ear->context.r[FP] = EAR_STACK_TOP;
}

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
EAR_Size EAR_addSegment(EAR* ear, EAR_Size vmaddr, EAR_Size vmsize, const EAR_PageNumber* phys_page_array, EAR_Protection vmprot, EAR_Size fault_physaddr) {
	if(ear->debug_flags & DEBUG_VERBOSE) {
		fprintf(stderr, "addSegment(0x%x, 0x%x, *..., 0x%x, 0x%x)\n", vmaddr, vmsize, vmprot, fault_physaddr);
	}
	
	// vmaddr, vmsize, and fault_physaddr must be page-aligned
	if(
		!EAR_IS_PAGE_ALIGNED(vmaddr) ||
		!EAR_IS_PAGE_ALIGNED(vmsize) ||
		!EAR_IS_PAGE_ALIGNED(fault_physaddr)
	) {
		abort();
	}
	
	// Does vmsize extend beyond the end of the address space?
	if((uint32_t)vmaddr + (uint32_t)vmsize > EAR_ADDRESS_SPACE_SIZE) {
		abort();
	}
	
	// Locate the translation table
	EAR_PageTable* ttable = EAR_getPageTable(ear);
	EAR_PageNumber page_count = EAR_PAGE_NUMBER(vmsize);
	
	// Search for a vm region?
	if(vmaddr == EAR_NULL) {
		EAR_PageNumber vpn_start = 1, vpn_end = 1;
		while(vpn_start < 256 - vmsize / EAR_PAGE_SIZE) {
			EAR_TTE* tte = &ttable->entries[vpn_start];
			
			// Is vpn_start an unmapped virtual page?
			if(tte->r_ppn == EAR_NULL && tte->w_ppn == EAR_NULL && tte->x_ppn == EAR_NULL && tte->fault_ppn == EAR_NULL) {
				// Start searching subsequent virtual pages to see if the virtual region is large enough
				vpn_end = vpn_start + 1;
				while(vpn_end - vpn_start < page_count) {
					tte = &ttable->entries[vpn_end];
					
					// Is vpn_end a mapped page?
					if(!(tte->r_ppn == EAR_NULL && tte->w_ppn == EAR_NULL && tte->x_ppn == EAR_NULL && tte->fault_ppn == EAR_NULL)) {
						// Found a mapped page before getting enough pages for the requested allocation,
						// so advance vpn_start and keep searching
						break;
					}
					
					vpn_end++;
				}
				
				// Found virtual region?
				if(vpn_end - vpn_start < page_count) {
					// Did not find a large enough region, so advance the start and try again
					vpn_start = vpn_end + 1;
					continue;
				}
				else {
					break;
				}
			}
			
			vpn_start++;
		}
		
		// Found virtual region?
		if(vpn_end - vpn_start == page_count) {
			vmaddr = vpn_start * EAR_PAGE_SIZE;
		}
		else {
			return EAR_NULL;
		}
	}
	
	EAR_PageNumber vpn = EAR_PAGE_NUMBER(vmaddr);
	EAR_PageNumber fault_ppn = EAR_PAGE_NUMBER(fault_physaddr);
	EAR_TTE* cur = &ttable->entries[vpn];
	
	// Update each translation table entry
	while(page_count != 0) {
		EAR_PageNumber ppn = phys_page_array ? *phys_page_array++ : 0;
		
		// Set physical page numbers for each allowed access type
		if(vmprot & EAR_PROT_READ) {
			cur->r_ppn = ppn;
		}
		if(vmprot & EAR_PROT_WRITE) {
			cur->w_ppn = ppn;
		}
		if(vmprot & EAR_PROT_EXECUTE) {
			cur->x_ppn = ppn;
		}
		
		// Set fault handler function pointer
		cur->fault_ppn = fault_ppn;
		
		// Advance to next page
		page_count--;
		cur++;
	}
	
	return vmaddr;
}

/*! Set the current thread state of the EAR CPU
 * @param thstate New thread state
 */
void EAR_setThreadState(EAR* ear, const EAR_ThreadState* thstate) {
	memcpy(&ear->context, thstate, sizeof(ear->context));
}

/*! Set the functions called to handle reads/writes on ports
 * @param read_fn Function pointer called to handle `RDB`
 * @param write_fn Function pointer called to handle `WRB`
 * @param cookie Opaque value passed as the first parameter to these callbacks
 */
void EAR_setPorts(EAR* ear, EAR_PortRead* read_fn, EAR_PortWrite* write_fn, void* cookie) {
	ear->read_fn = read_fn;
	ear->write_fn = write_fn;
	ear->port_cookie = cookie;
}

/*!
 * @brief Set the callback function used to handle unhandled page faults
 * 
 * @param fault_fn Function pointer called during an unhandled page fault
 * @param cookie Opaque value passed to fault_fn
 */
void EAR_setFaultHandler(EAR* ear, EAR_FaultHandler* fault_fn, void* cookie) {
	ear->fault_fn = fault_fn;
	ear->fault_cookie = cookie;
}

/*!
 * @brief Set the callback function used whenever an instruction accesses memory
 * 
 * @param mem_fn Function pointer called during all memory accesses
 * @param cookie Opaque value passed to mem_fn
 */
void EAR_setMemoryHook(EAR* ear, EAR_MemoryHook* mem_fn, void* cookie) {
	ear->mem_fn = mem_fn;
	ear->mem_cookie = cookie;
}

static EAR_HaltReason EAR_readByte(EAR* ear, EAR_Size addr, EAR_Byte* out_byte) {
	EAR_HaltReason ret;
	if(ear->mem_fn != NULL) {
		ret = ear->mem_fn(ear->mem_cookie, addr, EAR_PROT_READ, sizeof(*out_byte), out_byte);
		if(ret == HALT_COMPLETE) {
			return HALT_NONE;
		}
		if(ret != HALT_NONE) {
			return ret;
		}
	}
	
	// Translate virtual to physical address
	EAR_Size paddr;
	ret = EAR_translate(ear, addr, EAR_PROT_READ, &paddr);
	if(ret != HALT_NONE) {
		// Return if there was an error during translation or if the current instruction
		// is marked as completed by the fault handler function.
		return ret;
	}
	
	// Read byte from physical memory
	*out_byte = ear->mem.bytes[paddr];
	return HALT_NONE;
}

static EAR_HaltReason EAR_writeByte(EAR* ear, EAR_Size addr, EAR_Byte byte) {
	EAR_HaltReason ret;
	if(ear->mem_fn != NULL) {
		ret = ear->mem_fn(ear->mem_cookie, addr, EAR_PROT_WRITE, sizeof(byte), &byte);
		if(ret == HALT_COMPLETE) {
			return HALT_NONE;
		}
		if(ret != HALT_NONE) {
			return ret;
		}
	}
	
	// Translate virtual to physical address
	EAR_Size paddr;
	ret = EAR_translate(ear, addr, EAR_PROT_READ, &paddr);
	if(ret != HALT_NONE) {
		// Return if there was an error during translation or if the current instruction
		// is marked as completed by the fault handler function.
		return ret;
	}
	
	// Write byte to physical memory
	ear->mem.bytes[paddr] = byte;
	return HALT_NONE;
}

static EAR_HaltReason EAR_readWord(EAR* ear, EAR_Size addr, EAR_Size* out_word) {
	EAR_HaltReason ret;
	if(ear->mem_fn != NULL) {
		ret = ear->mem_fn(ear->mem_cookie, addr, EAR_PROT_READ, sizeof(*out_word), out_word);
		if(ret == HALT_COMPLETE) {
			return HALT_NONE;
		}
		if(ret != HALT_NONE) {
			return ret;
		}
	}
	
	// Can only read words from an aligned address
	if(addr % sizeof(*out_word) != 0) {
		return HALT_UNALIGNED;
	}
	
	// Translate virtual to physical address
	EAR_Size paddr;
	ret = EAR_translate(ear, addr, EAR_PROT_READ, &paddr);
	if(ret != HALT_NONE) {
		// Return if there was an error during translation or if the current instruction
		// is marked as completed by the fault handler function.
		return ret;
	}
	
	// Read word from physical memory
	memcpy(out_word, &ear->mem.bytes[paddr], sizeof(*out_word));
	return HALT_NONE;
}

static EAR_HaltReason EAR_writeWord(EAR* ear, EAR_Size addr, EAR_Size word) {
	EAR_HaltReason ret;
	if(ear->mem_fn != NULL) {
		ret = ear->mem_fn(ear->mem_cookie, addr, EAR_PROT_WRITE, sizeof(word), &word);
		if(ret == HALT_COMPLETE) {
			return HALT_NONE;
		}
		if(ret != HALT_NONE) {
			return ret;
		}
	}
	
	// Can only write words to an aligned address
	if(addr % sizeof(word) != 0) {
		return HALT_UNALIGNED;
	}
	
	// Translate virtual to physical address
	EAR_Size paddr;
	ret = EAR_translate(ear, addr, EAR_PROT_WRITE, &paddr);
	if(ret != HALT_NONE) {
		// Return if there was an error during translation or if the current instruction
		// is marked as completed by the fault handler function.
		return ret;
	}
	
	// Write word to physical memory
	memcpy(&ear->mem.bytes[paddr], &word, sizeof(word));
	return HALT_NONE;
}

static EAR_HaltReason EAR_fetchCodeByte(EAR* ear, EAR_Size* pc, EAR_Size dpc, EAR_Byte* out_byte) {
	EAR_HaltReason ret;
	if(ear->mem_fn != NULL) {
		ret = ear->mem_fn(ear->mem_cookie, *pc, EAR_PROT_EXECUTE, sizeof(*out_byte), out_byte);
		if(ret == HALT_COMPLETE) {
			return HALT_NONE;
		}
		if(ret != HALT_NONE) {
			return ret;
		}
	}
	
	// Look up memory page in which PC is located
	EAR_Size code_paddr;
	ret = EAR_translate(ear, *pc, EAR_PROT_EXECUTE, &code_paddr);
	if(ret != HALT_NONE) {
		return ret;
	}
	
	// Read byte from page offset
	*out_byte = ear->mem.bytes[code_paddr];
	
	// Update PC
	*pc += 1 + dpc;
	return HALT_NONE;
}

static EAR_HaltReason EAR_fetchCodeImm16(EAR* ear, EAR_Size* pc, EAR_Size dpc, EAR_Size* out_value) {
	EAR_HaltReason ret;
	EAR_Byte imm_byte;
	
	*out_value = 0;
	
	// Fetch first byte (low byte)
	ret = EAR_fetchCodeByte(ear, pc, dpc, &imm_byte);
	if(ret != HALT_NONE) {
		return ret;
	}
	
	// Store low byte
	*out_value = imm_byte;
	
	// Fetch second byte (high byte)
	ret = EAR_fetchCodeByte(ear, pc, dpc, &imm_byte);
	if(ret != HALT_NONE) {
		return ret;
	}
	
	// Store high byte
	*out_value |= imm_byte << 8;
	return HALT_NONE;
}

/*!
 * @brief Fetch the next code instruction starting at *pc.
 * 
 * @param pc In/out pointer to the address of the instruction to fetch. This will
 *           be written back with the address of the code byte directly following
 *           the instruction that was fetched.
 * @param dpc Value of DPC register (delta PC) to use while fetching code bytes
 * @param out_insn Output pointer where the decoded instruction will be written
 * @return Reason for halting, typically HALT_NONE
 */
EAR_HaltReason EAR_fetchInstruction(EAR* ear, EAR_Size* pc, EAR_Size dpc, EAR_Instruction* out_insn) {
	EAR_Byte ins_byte = 0;
	EAR_HaltReason ret = HALT_NONE;
	EAR_Cond cond = 0;
	EAR_Opcode op = 0;
	bool hasDRPrefix = false;
	
	// Zero-initialize instruction
	memset(out_insn, 0, sizeof(*out_insn));
	
	// Handle instruction prefixes
	do {
		// Read instruction byte
		ret = EAR_fetchCodeByte(ear, pc, dpc, &ins_byte);
		if(ret != HALT_NONE) {
			return ret;
		}
		
		// Decode first instruction byte
		cond = ins_byte >> 5;
		op = ins_byte & 0x1f;
		
		// Check if this is an instruction prefix byte
		if(cond != COND_SP || out_insn->cond) {
			break;
		}
		
		if(op == PREFIX_XC) {
			// Extended Condition prefix, set high bit in condition code
			if(out_insn->cond & 0x8) {
				// Doesn't make sense to allow multiple XD prefixes
				DEVEL_RETURN(ear, HALT_DECODE);
			}
			out_insn->cond |= 0x8;
		}
		else if(op == PREFIX_TF) {
			if(out_insn->toggle_flags) {
				// Doesn't make sense to allow multiple TF prefixes
				DEVEL_RETURN(ear, HALT_DECODE);
			}
			out_insn->toggle_flags = true;
		}
		else if(op & PREFIX_DR_MASK) {
			// Destination Register prefix, set destination register
			if(hasDRPrefix) {
				// Doesn't make sense to allow multiple DR prefixes
				DEVEL_RETURN(ear, HALT_DECODE);
			}
			hasDRPrefix = true;
			out_insn->rd = op & 0x0F;
		}
		else {
			// Invalid instruction prefix
			DEVEL_RETURN(ear, HALT_DECODE);
		}
	} while(cond == COND_SP);
	
	// Write condition code and opcode to the decoded instruction
	out_insn->cond |= cond;
	out_insn->op = op;
	
	// Decode additional instruction-specific bytes (if any)
	switch(out_insn->op) {
		case OP_PSH:
		case OP_POP: // PSH/POP {Regs16}
			if(!hasDRPrefix) {
				// Rd is usually SP unless overridden
				out_insn->rd = SP;
			}
			// Read low byte of regs16
			ret = EAR_fetchCodeByte(ear, pc, dpc, &ins_byte);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Set low byte of regs16
			out_insn->regs16 = ins_byte;
			
			// Read high byte of regs16
			ret = EAR_fetchCodeByte(ear, pc, dpc, &ins_byte);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Set high byte of regs16
			out_insn->regs16 |= ins_byte << 8;
			return HALT_NONE;
		
		case OP_ADD:
		case OP_SUB:
		case OP_MLU:
		case OP_MLS:
		case OP_DVU:
		case OP_DVS:
		case OP_XOR:
		case OP_AND:
		case OP_ORR:
		case OP_SHL:
		case OP_SRU:
		case OP_SRS:
		case OP_MOV:
		case OP_CMP:
		case OP_LDW:
		case OP_STW:
		case OP_LDB:
		case OP_BRA:
		case OP_FCA: // INSN [Rd,] Rx, Vy
			// Read Rx:Ry byte
			ret = EAR_fetchCodeByte(ear, pc, dpc, &ins_byte);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Extract Rx and Ry register numbers
			out_insn->rx = ins_byte >> 4;
			out_insn->ry = ins_byte & 0x0F;
			
			// Set Rd, defaulting to Rx unless there was an DR prefix
			if(!hasDRPrefix) {
				out_insn->rd = out_insn->rx;
			}
			else if(op >= OP_MOV) {
				// DR prefix is pointless
				DEVEL_RETURN(ear, HALT_DECODE);
			}
			
			if(op == OP_CMP) {
				// CMP is handled just like SUB except Rd is set to ZERO so the result is discarded
				out_insn->rd = ZERO;
			}
			
			// When Ry is DPC, there will be an immediate value after the opcode byte
			if(out_insn->ry != DPC) {
				// Read register value
				out_insn->ry_val = ear->active->r[out_insn->ry];
				return HALT_NONE;
			}
			
			// Read immediate value
			return EAR_fetchCodeImm16(ear, pc, dpc, &out_insn->ry_val);
		
		case OP_BRR:
		case OP_FCR: // BRR/FCR <label> (encoded as Imm16)
			// DR prefix is pointless
			if(hasDRPrefix) {
				DEVEL_RETURN(ear, HALT_DECODE);
			}
			
			// Read Imm16
			return EAR_fetchCodeImm16(ear, pc, dpc, &out_insn->ry_val);
		
		case OP_RDB: // RDB Rx, (portnum)
			// DR prefix is pointless
			if(hasDRPrefix) {
				DEVEL_RETURN(ear, HALT_DECODE);
			}
			
			// Read Rx:Imm4 byte
			ret = EAR_fetchCodeByte(ear, pc, dpc, &ins_byte);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Extract Rx register number and port number
			out_insn->rx = ins_byte >> 4;
			out_insn->port_number = ins_byte & 0x0F;
			out_insn->rd = out_insn->rx;
			return HALT_NONE;
		
		case OP_STB:
		case OP_WRB:
			// DR prefix is pointless
			if(hasDRPrefix) {
				DEVEL_RETURN(ear, HALT_DECODE);
			}
			
			// Read Imm4:Ry byte or Rx:Ry byte
			ret = EAR_fetchCodeByte(ear, pc, dpc, &ins_byte);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Extract Rx or port number and Ry register number
			if(op == OP_STB) {
				// STB sets Rx but not port number
				out_insn->rx = ins_byte >> 4;
			}
			else {
				// WRB sets port number but not Rx
				out_insn->port_number = ins_byte >> 4;
			}
			out_insn->ry = ins_byte & 0x0F;
			
			// If Ry is DPC, there's another byte for the immediate
			if(out_insn->ry != DPC) {
				out_insn->ry_val = ear->active->r[out_insn->ry];
				return HALT_NONE;
			}
			
			// Read immediate byte (Imm8)
			ret = EAR_fetchCodeByte(ear, pc, dpc, &ins_byte);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Set value of Ry to the Imm8
			out_insn->ry_val = ins_byte;
			return HALT_NONE;
		
		case OP_INC: // INC Rx, imm4
			// Read byte containing Rx and imm4
			ret = EAR_fetchCodeByte(ear, pc, dpc, &ins_byte);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Extract Rx and Imm4 (increment amount)
			out_insn->rx = ins_byte >> 4;
			out_insn->ry_val = ins_byte & 0x0F;
			
			// Sign extend ry_val
			if(ins_byte & 0x08) {
				out_insn->ry_val |= 0xFFF0;
			}
			
			// Is the SImm4 value positive?
			if(out_insn->ry_val <= 7) {
				// No need to allow INC 0, so increment non-negative values to expand
				// the positive max value to 8
				++out_insn->ry_val;
			}
			
			// If there's no DR prefix, then default Rd to Rx
			if(!hasDRPrefix) {
				out_insn->rd = out_insn->rx;
			}
			return HALT_NONE;
		
		case OP_BPT:
		case OP_HLT:
		case OP_NOP: // Single-byte instructions
			// DR prefix is pointless
			if(hasDRPrefix) {
				DEVEL_RETURN(ear, HALT_DECODE);
			}
			return HALT_NONE;
		
		default:
			if(ear->debug_flags & DEBUG_VERBOSE) {
				fprintf(stderr, "Invalid opcode: 0x%02X (0x%02X)\n", op, ins_byte);
			}
			DEVEL_RETURN(ear, HALT_DECODE);
	}
}

static bool EAR_evaluateCondition(EAR* ear, EAR_Cond cond) {
	EAR_Flag flags = ear->active->flags;
	switch(cond) {
		case COND_EQ: //COND := ZF
			return !!(flags & FLAG_ZF);
		
		case COND_NE: //COND := !ZF
			return !(flags & FLAG_ZF);
		
		case COND_GT: //COND := CF and !ZF
			return (flags & (FLAG_ZF | FLAG_CF)) == FLAG_CF;
		
		case COND_LE: //COND := !CF or ZF
			return !(flags & FLAG_CF) || !!(flags & FLAG_ZF);
		
		case COND_LT: //COND := !CF
			return !(flags & FLAG_CF);
		
		case COND_GE: //COND := CF
			return !!(flags & FLAG_CF);
		
		case COND_AL: //COND := true
			return true;
		
		case COND_NG: //COND := SF
			return !!(flags & FLAG_SF);
		
		case COND_PS: //COND := !SF
			return !(flags & FLAG_SF);
		
		case COND_BG: //COND := !ZF and (SF == VF)
			return !(flags & FLAG_ZF) && ((flags & FLAG_SF) == (flags & FLAG_VF));
		
		case COND_SE: //COND := ZF or (SF != VF)
			return !!(flags & FLAG_ZF) || ((flags & FLAG_SF) != (flags & FLAG_VF));
		
		case COND_SM: //COND := SF != VF
			return (flags & FLAG_SF) != (flags & FLAG_VF);
		
		case COND_BE: //COND := SF == VF
			return (flags & FLAG_SF) == (flags & FLAG_VF);
		
		case COND_OD: //COND := PF
			return !!(flags & FLAG_PF);
		
		case COND_EV: //COND := !PF
			return !(flags & FLAG_PF);
		
		case COND_SP:
		default:
			abort();
	}
}

static EAR_HaltReason EAR_executeInstruction(EAR* ear, EAR_Instruction* insn) {
	EAR_HaltReason ret = HALT_NONE;
	EAR_Size rxu = ear->active->r[insn->rx];
	EAR_Size ryu = insn->ry_val;
	EAR_Word rxs, rys;
	EAR_Flag flags = ear->active->flags;
	
	// Use memcpy() to avoid UB via signed-overflow
	memcpy(&rxs, &rxu, sizeof(rxs));
	memcpy(&rys, &ryu, sizeof(rys));
	
	// Destination register values
	EAR_Size rd_value = 0;
	EAR_Size rdx_value = 0;
	bool write_rd = false;
	bool write_rdx = false;
	
	// Controls whether the FLAGS register will be written back after executing this instruction
	bool write_flags = true;
	
	// Conditional instructions by default don't update FLAGS
	if(insn->cond != COND_AL && insn->cond != COND_SP) {
		write_flags = false;
	}
	
	// Apply the TF prefix
	if(insn->toggle_flags) {
		write_flags = !write_flags;
	}
	
	// Should the ZF, SF, and OF flags be updated with the value of rd_value?
	bool update_zso = false;
	
	// Intermediate values in computations
	uint32_t ubig = 0;
	int32_t sbig = 0;
	EAR_Word stmp = 0;
	EAR_Byte btmp = 0;
	
	switch(insn->op) {
		handle_ADD:  // Used by SUB and CMP
		case OP_INC: // Increment
		case OP_ADD: // Add
			rd_value = rxu + ryu;
			write_rd = true;
			
			// Update CF (unsigned overflow)
			if(rd_value < rxu) {
				flags |= FLAG_CF;
			}
			else {
				flags &= ~FLAG_CF;
			}
			
			// Update VF (signed overflow)
			if((rxu & 0x8000) == (ryu & 0x8000) && (rd_value & 0x8000) != (rxu & 0x8000)) {
				// Adding two positive numbers or two negative numbers and getting a result
				// with opposite sign from the operands means that signed overflow happened
				flags |= FLAG_SF;
			}
			else {
				flags &= ~FLAG_SF;
			}
			
			update_zso = true;
			break;
		
		case OP_CMP: // Compare (same as SUB but Rd is ZERO)
			// Technically nothing prevents users from using a CMP instruction with a TF prefix.
			// That does effectively turn this instruction into a NOP, but there is no reason to
			// prevent this behavior.
			//FALLTHROUGH
		
		case OP_SUB: // Subtract
			// Negate Ry and then treat like ADD
			rys = -rys;
			memcpy(&ryu, &rys, sizeof(ryu));
			goto handle_ADD;
		
		case OP_MLU: // Multiply unsigned
			// Zero-extend each operand from u16 to u32, then multiply
			ubig = (uint32_t)rxu * (uint32_t)ryu;
			
			// Split result value into high part and low part
			rd_value = (EAR_Size)ubig;
			write_rd = true;
			rdx_value = (EAR_Size)(ubig >> EAR_BITS);
			write_rdx = true;
			
			// Does not update CF or VF
			update_zso = true;
			break;
		
		case OP_MLS: // Multiply signed
			// Sign-extend each operand from u16 to s32, then multiply
			sbig = (int32_t)rxs * (int32_t)rys;
			
			// Prevent UB via signed-overflow in the cast by instead using memcpy
			// (will certainly be optimized away)
			memcpy(&ubig, &sbig, sizeof(ubig));
			
			// Split result value into high part and low part
			rd_value = (EAR_Size)ubig;
			write_rd = true;
			rdx_value = (EAR_Size)(ubig >> EAR_BITS);
			write_rdx = true;
			
			// Does not update CF or VF
			update_zso = true;
			break;
		
		case OP_DVU: // Divide/modulo unsigned
			// Check for illegal division
			if(ryu == 0) {
				return HALT_ARITHMETIC;
			}
			
			// Compute Rx / Ry
			rd_value = rxu / ryu;
			write_rd = true;
			
			// Compute Rx % Ry
			rdx_value = rxu % ryu;
			write_rdx = true;
			
			// Does not update CF or VF
			update_zso = true;
			break;
		
		case OP_DVS: // Divide/modulo signed
			// Check for illegal division
			if(rys == 0 || (rxs == INT16_MIN && rys == -1)) {
				return HALT_ARITHMETIC;
			}
			
			// Compute Rx / Ry
			stmp = rxs / rys;
			memcpy(&rd_value, &stmp, sizeof(rd_value));
			write_rd = true;
			
			// Compute Rx % Ry
			stmp = rxs % rys;
			memcpy(&rdx_value, &stmp, sizeof(rdx_value));
			write_rdx = true;
			
			// Does not update CF or VF
			update_zso = true;
			break;
		
		case OP_XOR: // Bitwise xor
			rd_value = rxu ^ ryu;
			write_rd = true;
			
			// Does not update CF or VF
			update_zso = true;
			break;
		
		case OP_AND: // Bitwise and
			rd_value = rxu & ryu;
			write_rd = true;
			
			// Does not update CF or VF
			update_zso = true;
			break;
		
		case OP_ORR: // Bitwise or
			rd_value = rxu | ryu;
			write_rd = true;
			
			// Does not update CF or VF
			update_zso = true;
			break;
		
		case OP_SHL: // Shift left
			// Avoid UB from shifting by more than the number of bits in the value
			if(ryu < EAR_BITS) {
				rd_value = rxu << ryu;
			}
			else {
				rd_value = 0;
			}
			write_rd = true;
			
			// Update CF
			if(ryu == 0) {
				// Shifting by zero means no carry
				flags &= ~FLAG_CF;
			}
			else if(ryu <= EAR_BITS) {
				// Check the bit that was last shifted out
				if(rxu & (1 << (16 - ryu))) {
					flags |= FLAG_CF;
				}
				else {
					flags &= ~FLAG_CF;
				}
			}
			
			// Does not update VF
			update_zso = true;
			break;
		
		case OP_SRU: // Shift right unsigned
			// Avoid UB from shifting by more than the number of bits in the value
			if(ryu < EAR_BITS) {
				rd_value = rxu >> ryu;
			}
			else {
				rd_value = 0;
			}
			write_rd = true;
			
			// Update CF
			if(ryu == 0) {
				// Shifting by zero means no carry
				flags &= ~FLAG_CF;
			}
			else if(ryu <= EAR_BITS) {
				if(rxu & (1 << (ryu - 1))) {
					flags |= FLAG_CF;
				}
				else {
					flags &= ~FLAG_CF;
				}
			}
			
			// Does not update VF
			update_zso = true;
			break;
		
		case OP_SRS: // Shift right signed
			// Avoid UB from shifting by more than the number of bits in the value
			if(ryu < EAR_BITS) {
				rd_value = rxs >> ryu;
			}
			else {
				// Shifting by >= 16 bits means just use the sign bit
				rd_value = rxs < 0 ? -1 : 0;
			}
			write_rd = true;
			
			// Update CF
			bool new_cf = false;
			if(ryu == 0) {
				// Shifting by zero means no carry
				new_cf = false;
			}
			else if(ryu >= EAR_BITS) {
				// Shifted enough that the last bit shifted out will be the sign bit
				new_cf = rxs < 0;
			}
			else {
				new_cf = !!(rxs & (1 << (ryu - 1)));
			}
			
			// Apply newly computed value of CF
			if(new_cf) {
				flags |= FLAG_CF;
			}
			else {
				flags &= ~FLAG_CF;
			}
			
			// Does not update VF
			update_zso = true;
			break;
		
		case OP_MOV: // Move/assign
			rd_value = ryu;
			write_rd = true;
			update_zso = true;
			break;
		
		case OP_LDW: // Read word from memory
			ret = EAR_readWord(ear, ryu, &rd_value);
			write_rd = true;
			update_zso = true;
			break;
		
		case OP_STW: // Write word to memory
			ret = EAR_writeWord(ear, rxu, ryu);
			break;
		
		case OP_LDB: // Read byte from memory
			ret = EAR_readByte(ear, ryu, &btmp);
			rd_value = btmp;
			write_rd = true;
			update_zso = true;
			break;
		
		case OP_STB: // Write byte to memory
			ret = EAR_writeByte(ear, rxu, (EAR_Byte)ryu);
			break;
		
		case OP_BRA: // Absolute jump
			ear->active->r[DPC] = rxu;
			ear->active->r[PC] = ryu;
			break;
		
		case OP_BRR: // Relative jump
			ear->active->r[PC] += ryu;
			break;
		
		case OP_FCA: // Absolute call
			ear->active->r[RD] = ear->active->r[DPC];
			ear->active->r[RA] = ear->active->r[PC];
			ear->active->r[DPC] = rxu;
			ear->active->r[PC] = ryu;
			break;
		
		case OP_FCR: // Relative call
			ear->active->r[RD] = ear->active->r[DPC];
			ear->active->r[RA] = ear->active->r[PC];
			ear->active->r[PC] += ryu;
			break;
		
		case OP_RDB: // Read byte from port
			// Invoke port read callback
			if(!ear->read_fn || !ear->read_fn(ear->port_cookie, insn->port_number, &btmp)) {
				if(g_interrupted) {
					// Don't change flags when the I/O failed due to a debugger interruption
					ret = HALT_DEBUGGER;
					break;
				}
				
				// Set the CF flag whenever reading from a port fails
				flags |= FLAG_CF;
			}
			else {
				// The read succeeded, so clear CF
				flags &= ~FLAG_CF;
				
				// The byte value that was read will be stored in the destination register
				rd_value = btmp;
				write_rd = true;
				update_zso = true;
			}
			break;
		
		case OP_WRB: // Write byte to port
			// Invoke port write callback
			if(!ear->write_fn || !ear->write_fn(ear->port_cookie, insn->port_number, (EAR_Byte)ryu)) {
				if(g_interrupted) {
					// Don't change flags when the I/O failed due to a debugger interruption
					ret = HALT_DEBUGGER;
					break;
				}
				
				// Set the CF flag whenever writing to a port fails
				flags |= FLAG_CF;
			}
			else {
				// The write succeeded, so clear CF
				flags &= ~FLAG_CF;
			}
			break;
		
		case OP_PSH: // Push registers
		{
			EAR_Size* regs = &ear->active->r[0];
			EAR_Size sp = regs[insn->rd];
			
			// Push in reverse order so that the registers are ordered properly in memory (R0 at lowest address)
			int i;
			for(i = 15; i >= 0; i--) {
				// Is this register's bit in the Regs16 bitmap set?
				if(insn->regs16 & (1 << i)) {
					// Push this register to the destination memory location
					sp -= 2;
					ret = EAR_writeWord(ear, sp, regs[i]);
					if(ret != HALT_NONE) {
						return ret;
					}
				}
			}
			
			// Write back Rd value
			regs[insn->rd] = sp;
			break;
		}
		
		case OP_POP: // Pop registers
		{
			// Copy registers and only pop to this copy so that this is like an atomic operation
			EAR_Size regs[16];
			memcpy(regs, ear->active->r, sizeof(regs));
			EAR_Size sp = regs[insn->rd];
			
			// Pop in forward order so that the registers are ordered properly in memory (R0 at lowest address)
			int i;
			for(i = 0; i <= 15; i++) {
				// Is this register's bit in the Regs16 bitmap set?
				if(insn->regs16 & (1 << i)) {
					// Pop this register from the source memory location
					ret = EAR_readWord(ear, sp, &rd_value);
					if(ret != HALT_NONE) {
						return ret;
					}
					
					// Don't overwrite ZERO
					if(i != ZERO) {
						regs[i] = rd_value;
					}
					
					// Advance to next register location
					sp += 2;
				}
			}
			
			// Write back Rd value to the copied register bank
			regs[insn->rd] = sp;
			
			// Write back all registers
			memcpy(ear->active->r, regs, sizeof(regs));
			break;
		}
		
		case OP_BPT: // Breakpoint
			if(ear->debug_flags & DEBUG_RESUMING) {
				// Resuming from a breakpoint, so skip this one. The resuming flag
				// will be disabled above in EAR_stepInstruction.
				break;
			}
			return HALT_SW_BREAKPOINT;
		
		case OP_HLT: // Halt
			return HALT_INSTRUCTION;
		
		case OP_NOP: // No-op
			break;
		
		default: // Illegal instruction
			// Should be caught during instruction decoding
			abort();
	}
	
	// Does the CPU need to halt?
	if(ret != HALT_NONE) {
		return ret;
	}
	
	// Write back Rd
	if(write_rd && insn->rd != ZERO) {
		ear->active->r[insn->rd] = rd_value;
	}
	
	// Write back Rd^1
	if(write_rdx && (insn->rd ^ 1) != ZERO) {
		ear->active->r[insn->rd ^ 1] = rdx_value;
	}
	
	if(write_flags) {
		// Should ZF, OF, and ZF flags be updated from the value of rd_value?
		if(update_zso) {
			// Recompute ZF
			if(rd_value == 0) {
				flags |= FLAG_ZF;
			}
			else {
				flags &= ~FLAG_ZF;
			}
			
			// Recompute PF
			EAR_Size parity = rd_value;
			parity ^= parity >> 8;
			parity ^= parity >> 4;
			parity ^= parity >> 2;
			parity ^= parity >> 1;
			if(parity & 1) {
				flags |= FLAG_PF;
			}
			else {
				flags &= ~FLAG_PF;
			}
			
			// Recompute SF
			if(rd_value & 0x8000) {
				flags |= FLAG_SF;
			}
			else {
				flags &= ~FLAG_SF;
			}
		}
		
		// Write back FLAGS register
		ear->active->flags = flags;
	}
	
	return HALT_NONE;
}

/*! Executes a single instruction
 * @return Reason for halting, typically HALT_NONE
 */
EAR_HaltReason EAR_stepInstruction(EAR* ear) {
	EAR_Instruction insn;
	EAR_HaltReason ret;
	
	// Read PC and DPC values
	EAR_Size pc = ear->active->r[PC];
	EAR_Size dpc = ear->active->r[DPC];
	ear->active->cur_pc = pc;
	
	// Fetch instruction from PC
	ret = EAR_fetchInstruction(ear, &pc, dpc, &insn);
	if(ret != HALT_NONE) {
		if(ret == HALT_COMPLETE) {
			ret = HALT_NONE;
		}
		
		return ret;
	}
	
	// Print the instruction being executed if we are in debug mode
	if(ear->debug_flags & DEBUG_TRACE) {
		fprintf(stderr, "%04X.%04X: ", ear->active->cur_pc, ear->active->r[DPC]);
		EAR_writeInstruction(ear, &insn, stderr);
	}
	
	// Update PC to point to the next instruction before executing the current one
	ear->active->r[PC] = pc;
	
	// Evaluate condition
	if(EAR_evaluateCondition(ear, insn.cond)) {
		// Execute instruction
		ret = EAR_executeInstruction(ear, &insn);
		if(ret == HALT_COMPLETE) {
			ret = HALT_NONE;
		}
		
		// Restore old PC if execution failed
		if(ret != HALT_NONE) {
			ear->active->r[PC] = ear->active->cur_pc;
		}
		
		// Check if the program tried to return from the topmost stack frame
		if(ear->active->r[PC] == EAR_CALL_RA && ear->active->r[DPC] == EAR_CALL_RD) {
			ret = HALT_RETURN;
		}
	}
	
	// An instruction executed, so turn off any resuming flag
	ear->debug_flags &= ~DEBUG_RESUMING;
	
	// Increment instruction counts
	++ear->active->ins_count;
	++ear->ins_count;
	return ret;
}

/*! Begins execution from the current state.
 * @return Reason for halting, never HALT_NONE
 */
EAR_HaltReason EAR_continue(EAR* ear) {
	EAR_HaltReason reason;
	
	if(ear->debug_flags & DEBUG_ACTIVE) {
		enable_interrupt_handler();
	}
	
	do {
		reason = EAR_stepInstruction(ear);
		
		if(g_interrupted) {
			reason = HALT_DEBUGGER;
		}
	} while(reason == HALT_NONE);
	
	if(ear->debug_flags & DEBUG_ACTIVE) {
		disable_interrupt_handler();
	}
	
	return reason;
}

/*! Invokes a function at a given virtual address and passing up to 6 arguments.
 * @note This will overwrite the values of registers R1-R6, PC, DPC, RA, and RD.
 *       All other registers are left untouched, so the existing values of SP and
 *       FP are used. This function will start at the given target address. When
 *       the target function returns to the initial values of RA and RD, this
 *       function will return with status HALT_RETURN. Register values will not
 *       be cleaned up after the function returns.
 * @param func_vmaddr Virtual address of the function to invoke
 * @param func_dpc Delta PC (DPC) value to be used for executing the target function
 * @param arg1 First argument to the target function
 * @param arg2 Second argument to the target function
 * @param arg3 Third argument to the target function
 * @param arg4 Fourth argument to the target function
 * @param arg5 Fifth argument to the target function
 * @param arg6 Sixth argument to the target function
 * @param run True if the VM should be run after setting up the call state
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
) {
	// Set function call context
	ear->active->r[R2] = arg1;
	ear->active->r[R3] = arg2;
	ear->active->r[R4] = arg3;
	ear->active->r[R5] = arg4;
	ear->active->r[R6] = arg5;
	ear->active->r[R7] = arg6;
	ear->active->r[RA] = EAR_CALL_RA;
	ear->active->r[RD] = EAR_CALL_RD;
	ear->active->r[PC] = func_vmaddr;
	ear->active->r[DPC] = func_dpc;
	
	if(!run) {
		return HALT_NONE;
	}
	
	// Run until the current function returns or the CPU is halted for some other reason
	return EAR_continue(ear);
}

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
EAR_Size EAR_copyin(EAR* ear, EAR_Size dst, const void* src, EAR_Size size, EAR_Protection prot, EAR_HaltReason* out_r) {
	EAR_HaltReason r = HALT_NONE;
	EAR_Size bytes_copied = 0;
	EAR_Size phys_dst = 0;
	
	// prot must be exactly one bit
	if(!(prot == EAR_PROT_READ || prot == EAR_PROT_WRITE || prot == EAR_PROT_EXECUTE)) {
		abort();
	}
	
	// Copy loop, one iteration per page
	while(size != 0) {
		// Translate virtual to physical address
		r = EAR_translate(ear, dst, prot, &phys_dst);
		if(r != HALT_NONE) {
			goto out;
		}
		
		// We will usually copy a full page at a time
		EAR_Size copy_amount = EAR_PAGE_SIZE;
		
		// However, at the beginning, the destination virtual address may not be page-aligned
		EAR_Size next_page = EAR_FLOOR_PAGE(phys_dst) + EAR_PAGE_SIZE;
		if(next_page - phys_dst < copy_amount) {
			copy_amount = next_page - phys_dst;
		}
		
		// And at the end, we might need to copy less than a full page
		if(size < copy_amount) {
			copy_amount = size;
		}
		
		// Copy data in to physical memory backing the virtual page
		memcpy(&ear->mem.bytes[phys_dst], src, copy_amount);
		
		// Advance memory position
		bytes_copied += copy_amount;
		size -= copy_amount;
		dst += copy_amount;
		src = (const char*)src + copy_amount;
	}
	
out:
	if(out_r != NULL) {
		*out_r = r;
	}
	return bytes_copied;
}

/*!
 * @brief Copy data out from EAR virtual memory.
 * 
 * @param dst Destination pointer
 * @param src Source virtual address
 * @param size Number of bytes to copy
 * @param prot Type of memory to read from (exactly one bit should be set)
 * @param invasive Whether a page fault function should be run in the VM if necessary
 * @param out_r Output pointer where the halt reason will be written (or NULL).
 *              Will be HALT_NONE when all bytes are copied successfully.
 * 
 * @return Number of bytes copied
 */
EAR_Size EAR_copyout(EAR* ear, void* dst, EAR_Size src, EAR_Size size, EAR_Protection prot, EAR_HaltReason* out_r) {
	EAR_HaltReason r = HALT_NONE;
	EAR_Size bytes_copied = 0;
	EAR_Size phys_src = 0;
	
	// prot must be exactly one bit
	if(!(prot == EAR_PROT_READ || prot == EAR_PROT_WRITE || prot == EAR_PROT_EXECUTE)) {
		abort();
	}
	
	// Copy loop, one iteration per page
	while(size != 0) {
		// Translate virtual to physical address
		r = EAR_translate(ear, src, prot, &phys_src);
		if(r != HALT_NONE) {
			goto out;
		}
		
		// We will usually copy a full page at a time
		EAR_Size copy_amount = EAR_PAGE_SIZE;
		
		// However, at the beginning, the source virtual address may not be page-aligned
		EAR_Size next_page = EAR_FLOOR_PAGE(phys_src) + EAR_PAGE_SIZE;
		if(next_page - phys_src < copy_amount) {
			copy_amount = next_page - phys_src;
		}
		
		// And at the end, we might need to copy less than a full page
		if(size < copy_amount) {
			copy_amount = size;
		}
		
		// Copy data in to physical memory backing the virtual page
		memcpy(dst, &ear->mem.bytes[phys_src], copy_amount);
		
		// Advance memory position
		bytes_copied += copy_amount;
		size -= copy_amount;
		dst = (char*)dst + copy_amount;
		src += copy_amount;
	}
	
out:
	if(out_r != NULL) {
		*out_r = r;
	}
	return bytes_copied;
}

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
EAR_Size EAR_copyinPhys(EAR* ear, EAR_PageNumber* dst_ppns, uint8_t dst_page_count, EAR_Size dst_offset, const void* src, EAR_Size size) {
	uint8_t page_idx;
	uint8_t offset = EAR_PAGE_OFFSET(dst_offset);
	EAR_Size bytes_copied = 0;
	for(page_idx = dst_offset / EAR_PAGE_SIZE; page_idx < dst_page_count && size > 0; page_idx++) {
		const void* src_pos = ((const char*)src) + bytes_copied;
		void* dst_pos = &ear->mem.bytes[dst_ppns[page_idx] * EAR_PAGE_SIZE + offset];
		size_t chunk_size = EAR_PAGE_SIZE - offset;
		if(chunk_size > size) {
			chunk_size = size;
		}
		offset = 0;
		
		memcpy(dst_pos, src_pos, chunk_size);
		bytes_copied += chunk_size;
		size -= chunk_size;
	}
	
	return bytes_copied;
}

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
EAR_Size EAR_copyoutPhys(EAR* ear, void* dst, EAR_PageNumber* src_ppns, uint8_t src_page_count, EAR_Size src_offset, EAR_Size size) {
	uint8_t page_idx;
	uint8_t offset = EAR_PAGE_OFFSET(src_offset);
	EAR_Size bytes_copied = 0;
	for(page_idx = src_offset / EAR_PAGE_SIZE; page_idx < src_page_count && bytes_copied < size; page_idx++) {
		void* dst_pos = ((char*)dst) + bytes_copied;
		const void* src_pos = &ear->mem.bytes[src_ppns[page_idx] * EAR_PAGE_SIZE + offset];
		size_t chunk_size = EAR_PAGE_SIZE - offset;
		if(chunk_size > size) {
			chunk_size = size;
		}
		offset = 0;
		
		memcpy(dst_pos, src_pos, chunk_size);
		bytes_copied += chunk_size;
	}
	
	return bytes_copied;
}

/*!
 * @brief Get a pointer to an EAR physical memory address range.
 * 
 * @param paddr EAR physical address
 * @param size In: number of bytes requested. Out: number of bytes available (potentially truncated)
 * 
 * @return Pointer into the EAR emulator's physical memory region corresponding to paddr
 */
void* EAR_getPhys(EAR* ear, EAR_Size paddr, EAR_Size* size) {
	if(paddr > EAR_ADDRESS_SPACE_SIZE - *size) {
		*size = EAR_ADDRESS_SPACE_SIZE - paddr;
	}
	
	return &ear->mem.bytes[paddr];
}

/*!
 * @brief Allocates multiple physical pages, returning their page numbers in an array.
 * 
 * @param num_pages Number of physical pages to allocate
 * @param out_phys_array Array of physical page numbers allocated
 * @return Number of pages successfully allocated, will be less than num_pages when
 *         physical memory runs out.
 */
uint8_t EAR_allocPhys(EAR* ear, uint8_t num_pages, EAR_PageNumber* out_phys_array) {
	uint8_t alloc_count = 0;
	uint16_t ppn = 0;
	while(alloc_count < num_pages && ppn < EAR_PHYSICAL_ALLOCATION_TABLE_SIZE) {
		EAR_PTE* pte = EAR_getPTE(ear, ppn);
		
		if(!(*pte & PHYS_IN_USE)) {
			if(*pte & PHYS_DIRTY) {
				memset(&ear->mem.bytes[ppn * EAR_PAGE_SIZE], 0, EAR_PAGE_SIZE);
			}
			
			*pte = PHYS_IN_USE | PHYS_ALLOW;
			out_phys_array[alloc_count++] = (EAR_PageNumber)ppn;
		}
		
		++ppn;
	}
	
	return alloc_count;
}

const char* EAR_haltReasonToString(EAR_HaltReason status) {
	switch(status) {
		case HALT_NONE:
			return "No unusual halt reason";
		case HALT_INSTRUCTION:
			return "Executed a `HLT` instruction";
		case HALT_UNALIGNED:
			return "Tried to access a word at an unaligned (odd) memory address";
		case HALT_UNMAPPED:
			return "Accessed unmapped virtual memory";
		case HALT_DOUBLEFAULT:
			return "Accessed unmapped memory in a page fault handler";
		case HALT_DECODE:
			return "Tried to execute an illegal instruction";
		case HALT_ARITHMETIC:
			return "Divide/modulo by zero, or signed div/mod INT16_MIN by -1";
		case HALT_SW_BREAKPOINT:
			return "Executed a `BPT` instruction or hit a hardware breakpoint";
		case HALT_HW_BREAKPOINT:
			return "Hit a hardware breakpoint";
		case HALT_RETURN:
			return "Program tried to return from the topmost stack frame";
		case HALT_COMPLETE:
			return "For internal use only, used to support fault handlers and callbacks";
		default:
			return "Unknown halt reason";
	}
}

const char* EAR_getMnemonic(EAR_Opcode op) {
	static const char* opcodes[] = {
		"ADD", "SUB", "MLU", "MLS", "DVU", "DVS", "XOR", "AND",
		"ORR", "SHL", "SRU", "SRS", "MOV", "CMP",
		"RSV_0E", "RSV_0F",
		"LDW", "STW", "LDB", "STB", "BRA", "BRR", "FCA", "FCR",
		"RDB", "WRB", "PSH", "POP", "INC", "BPT", "HLT", "NOP"
	};
	return op < ARRAY_COUNT(opcodes) ? opcodes[op] : NULL;
}

const char* EAR_getConditionString(EAR_Cond cond) {
	static const char* condnames[] = {
		".EQ", ".NE", ".GT", ".LE", ".LT", ".GE", "", "",
		".NG", ".PS", ".BG", ".SE", ".SM", ".BE", ".OD", ".EV"
	};
	return cond < ARRAY_COUNT(condnames) ? condnames[cond] : NULL;
}

const char* EAR_getRegisterName(EAR_Register reg) {
	static const char* regnames[] = {
		"ZERO", "TMP", "RV", "R3", "R4", "R5", "R6", "R7",
		"R8", "R9", "FP", "SP", "RA", "RD", "PC", "DPC"
	};
	return reg < ARRAY_COUNT(regnames) ? regnames[reg] : NULL;
}

/*!
 * @brief Prints a description of an EAR instruction.
 * 
 * @param insn Instruction to print
 * @param fp Stream to write to
 */
void EAR_writeInstruction(EAR* ear, EAR_Instruction* insn, FILE* fp) {
	(void)ear;
	
	const char* mnem = EAR_getMnemonic(insn->op);
	const char* cond = EAR_getConditionString(insn->cond);
	const char* suffix = insn->toggle_flags ? "F" : "";
	
	// Print instruction address, mnemonic, and condition code
	fprintf(fp, "%s%s%s", mnem, suffix, cond);
	
	// If the instruction has mnemonics, then add spaces to align the instruction as
	// needed in case there was no "F" suffix or condition code after the mnemonic.
	if(insn->op != OP_BPT && insn->op != OP_HLT && insn->op != OP_NOP) {
		if(*suffix == '\0') {
			fprintf(fp, " ");
		}
		if(*cond == '\0') {
			fprintf(fp, "   ");
		}
	}
	
	switch(insn->op) {
		default: // INScc <Rd,> Rx, Ry
			if(insn->rd != insn->rx) {
				fprintf(fp, " %s,", EAR_getRegisterName(insn->rd));
			}
			fprintf(fp, " %s,", EAR_getRegisterName(insn->rx));
			
			if(insn->ry == DPC) {
				fprintf(fp, " 0x%X\n", insn->ry_val);
			}
			else {
				fprintf(fp, " %s\n", EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_LDW:
		case OP_LDB: // LD[WB]cc Rx, [value]
			fprintf(fp, " %s,", EAR_getRegisterName(insn->rx));
			if(insn->ry == DPC) {
				fprintf(fp, " [0x%X]\n", insn->ry_val);
			}
			else {
				fprintf(fp, " [%s]\n", EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_STW:
		case OP_STB: // ST[WB]cc [Rx], value
			fprintf(fp, " [%s],", EAR_getRegisterName(insn->rx));
			if(insn->ry == DPC) {
				fprintf(fp, " 0x%X\n", insn->ry_val);
			}
			else {
				fprintf(fp, " %s\n", EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_RDB: // RDBcc Rd, (portnum)
			fprintf(fp, " %s, (%"PRIu8")\n", EAR_getRegisterName(insn->rx), insn->port_number);
			break;
		
		case OP_WRB: // WRBcc (portnum), value
			fprintf(fp, " (%"PRIu8"),", insn->port_number);
			if(insn->ry == DPC) {
				fprintf(fp, " 0x%X\n", insn->ry_val);
			}
			else {
				fprintf(fp, " %s\n", EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_BRR:
		case OP_FCR: // BRR/FCRcc Imm16
			fprintf(fp, " 0x%X\n", insn->ry_val);
			break;
		
		case OP_PSH:
		case OP_POP: // PSH/POPcc <Rd,> {Regs16}
		{
			// Print Rd if it's different than the default of SP
			if(insn->rd != SP) {
				fprintf(fp, " %s,", EAR_getRegisterName(insn->rd));
			}
			
			// Start of Regs16 bitset
			fprintf(fp, " {");
			
			// Print contents of Regs16 bitset
			bool firstReg = true;
			int i, j;
			for(i = 0; i < 16; i++) {
				if(insn->regs16 & (1 << i)) {
					// Check how many consecutive registers are being pushed/popped
					for(j = i + 1; j < 16; j++) {
						if(!(insn->regs16 & (1 << j))) {
							break;
						}
					}
					
					// Check if this is a range of consecutive registers
					if(j == i + 1) {
						fprintf(fp, "%s%s", firstReg ? "" : ", ", EAR_getRegisterName(i));
					}
					else {
						fprintf(fp, "%s%s-%s", firstReg ? "" : ", ", EAR_getRegisterName(i), EAR_getRegisterName(j - 1));
					}
					
					firstReg = false;
					
					// Skip j as we know it's not a register included in the Regs16 bitset
					i = j + 1;
				}
			}
			
			// End of Regs16 bitset
			fprintf(fp, "}\n");
			break;
		}
		
		case OP_CMP: // CMP Rx, Vy (no Rd)
			fprintf(fp, " %s, ", EAR_getRegisterName(insn->rx));
			
			if(insn->ry == DPC) {
				fprintf(fp, " 0x%X\n", insn->ry_val);
			}
			else {
				fprintf(fp, " %s\n", EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_INC: // INCcc <Rd,> Rx, SImm4
			if(insn->rd != insn->rx) {
				fprintf(fp, " %s,", EAR_getRegisterName(insn->rd));
			}
			fprintf(fp, " %s, %d\n", EAR_getRegisterName(insn->rx), (EAR_Word)insn->ry_val);
			break;
		
		case OP_BPT: // BPTcc
		case OP_HLT: // HLTcc
		case OP_NOP: // NOPcc
			// Instructions w/o any operands
			fprintf(fp, "\n");
			break;
	}
}

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
EAR_Size EAR_writeDisassembly(EAR* ear, EAR_Size addr, EAR_Size dpc, EAR_Size count, FILE* fp) {
	unsigned dis_idx;
	for(dis_idx = 0; dis_idx < count; dis_idx++) {
		fprintf(fp, "%04X.%04X: ", addr, dpc);
		
		EAR_Instruction insn;
		EAR_HaltReason r = EAR_fetchInstruction(ear, &addr, dpc, &insn);
		if(r != HALT_NONE) {
			fprintf(fp, "Failed to disassemble instruction: %s\n", EAR_haltReasonToString(r));
			break;
		}
		
		EAR_writeInstruction(ear, &insn, fp);
	}
	
	return dis_idx;
}

static void writeThreadState(EAR_ThreadState* ctx, FILE* fp) {
	fprintf(
		fp,
		"   (ZERO)R0: %04X        R8: %04X\n"
		"    (TMP)R1: %04X        R9: %04X\n"
		"(RV/ARG1)R2: %04X   (FP)R10: %04X\n"
		"   (ARG2)R3: %04X   (SP)R11: %04X\n"
		"   (ARG3)R4: %04X   (RA)R12: %04X\n"
		"   (ARG4)R5: %04X   (RD)R13: %04X\n"
		"   (ARG5)R6: %04X   (PC)R14: %04X\n"
		"   (ARG6)R7: %04X  (DPC)R15: %04X\n"
		"FLAGS: %c%c%c%c%c%c\n", //ZSPCVM
		ctx->r[0], ctx->r[8],
		ctx->r[1], ctx->r[9],
		ctx->r[2], ctx->r[10],
		ctx->r[3], ctx->r[11],
		ctx->r[4], ctx->r[12],
		ctx->r[5], ctx->r[13],
		ctx->r[6], ctx->r[14],
		ctx->r[7], ctx->r[15],
		(ctx->flags & FLAG_ZF) ? 'Z' : 'z',
		(ctx->flags & FLAG_SF) ? 'S' : 's',
		(ctx->flags & FLAG_PF) ? 'P' : 'p',
		(ctx->flags & FLAG_CF) ? 'C' : 'c',
		(ctx->flags & FLAG_VF) ? 'V' : 'v',
		(ctx->flags & FLAG_MF) ? 'M' : 'm'
	);
}

void EAR_writeRegs(EAR* ear, FILE* fp) {
	if(ear->active == &ear->exc_ctx) {
		fprintf(fp, "\nException thread state:\n");
		writeThreadState(&ear->exc_ctx, fp);
	}
	
	fprintf(fp, "\nThread state:\n");
	writeThreadState(&ear->context, fp);
}

void EAR_writeVMMap(EAR* ear, FILE* fp) {
	EAR_PageTable* pt = EAR_getPageTable(ear);
	
	EAR_Size page_index = 0;
	while(page_index < EAR_TTE_COUNT) {
		EAR_TTE region = pt->entries[page_index];
		
		EAR_Size next_index;
		for(next_index = page_index + 1; next_index < EAR_TTE_COUNT; next_index++) {
			EAR_TTE next = pt->entries[next_index];
			if(next.fault_ppn != region.fault_ppn) {
				break;
			}
			
			if(region.r_ppn == 0) {
				if(next.r_ppn != 0) {
					// All pages in a region should be readable or not
					break;
				}
			}
			else if(next.r_ppn != region.r_ppn + (next_index - page_index)) {
				// Physically contiguous (read-wise)?
				break;
			}
			
			if(region.w_ppn == 0) {
				if(next.w_ppn != 0) {
					// All pages should be writeable or not
					break;
				}
			}
			else if(next.w_ppn != region.w_ppn + (next_index - page_index)) {
				// Physically contiguous (write-wise)?
				break;
			}
			
			if(region.x_ppn == 0) {
				if(next.x_ppn != 0) {
					// All pages should be executable or not
					break;
				}
			}
			else if(next.x_ppn != region.x_ppn + (next_index - page_index)) {
				// Physically contiguous (exec-wise)?
				break;
			}
		}
		
		fprintf(
			fp, "%04X-%04X: R=%02X W=%02X X=%02X fault=%04X\n",
			page_index * EAR_PAGE_SIZE,
			next_index == EAR_TTE_COUNT ? 0xFFFF : next_index * EAR_PAGE_SIZE,
			region.r_ppn,
			region.w_ppn,
			region.x_ppn,
			region.fault_ppn * EAR_PAGE_SIZE
		);
		
		page_index = next_index;
	}
}
