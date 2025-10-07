//
//  debugger.c
//  PegasusEar
//
//  Created by Kevin Colley on 10/31/20.
//

#include "debugger.h"
#include <stdlib.h>
#include <inttypes.h>
#include "common/dynamic_array.h"
#include "common/dynamic_string.h"
#include "libear/mmu.h"


volatile sig_atomic_t g_interrupted = 0;
static void interrupt_handler(int sig) {
	(void)sig;
	
	// Flag the process as having been interrupted
	g_interrupted = 1;
}

static bool s_interrupt_handler_enabled = false;
static struct sigaction old_handler;
bool enable_interrupt_handler(void) {
	if(s_interrupt_handler_enabled) {
		return false;
	}
	
	g_interrupted = 0;
	
	struct sigaction sa;
	sa.sa_flags = SA_RESETHAND;
	sa.sa_handler = &interrupt_handler;
	
	sigaction(SIGINT, &sa, &old_handler);
	s_interrupt_handler_enabled = true;
	
	return true;
}

void disable_interrupt_handler(void) {
	if(s_interrupt_handler_enabled) {
		sigaction(SIGINT, &old_handler, NULL);
		s_interrupt_handler_enabled = false;
	}
}


/*!
 * @brief Initializes a debugger that will control the provided EAR processor.
 * 
 * @param cpu EAR processor to debug
 * @param debug_flags Initial debug flags to set
 * 
 * @return Newly initialized debugger object
 */
Debugger* Debugger_init(EAR* cpu, DebugFlags debug_flags) {
	Debugger* dbg = calloc(1, sizeof(*dbg));
	if(!dbg) {
		return NULL;
	}
	
	dbg->debug_flags = debug_flags;
	dbg->cpu = cpu;
	dbg->r = HALT_NONE;
	
	return dbg;
}


/*!
 * @brief Allows the debugger to control whether trace output should be printed.
 * 
 * @param trace Pointer to a boolean that will be set to true if trace output should be printed
 */
void Debugger_setTraceVar(Debugger* dbg, bool* trace) {
	dbg->trace = trace;
}


/*! Connect the debugger to the physical memory bus (for xxd) */
void Debugger_setBusHandler(Debugger* dbg, Bus_AccessHandler* bus_fn, void* bus_cookie) {
	dbg->bus_fn = bus_fn;
	dbg->bus_cookie = bus_cookie;
}


/*! Set function used to dump physical memory layout */
void Debugger_setBusDumper(Debugger* dbg, Bus_DumpFunc* bus_dump_fn) {
	dbg->bus_dump_fn = bus_dump_fn;
}


/*!
 * @brief Check whether the specified thread state is in kernel mode.
 *        We do this by checking if the thread may access the opposite thread's registers.
 */
bool Debugger_isKernelMode(EAR_ThreadState* ctx) {
	return !(ctx->cr[CR_FLAGS] & FLAG_DENY_XREGS);
}


/*! Step a single instruction in the debugger, semantically */
void Debugger_stepInstruction(Debugger* dbg) {
	bool enabledInterruptHandler = enable_interrupt_handler();
	
	dbg->debug_flags |= DEBUG_RESUMING;
	do {
		dbg->r = EAR_stepInstruction(dbg->cpu);
		if(dbg->r != HALT_NONE && dbg->r != HALT_EXCEPTION) {
			break;
		}
	} while(!(dbg->debug_flags & DEBUG_KERNEL) && Debugger_isKernelMode(CTX(*dbg->cpu)));
	
	if(enabledInterruptHandler) {
		disable_interrupt_handler();
	}
}


/*!
 * @brief Hook function called before executing each instruction.
 * 
 * @param insn Instruction about to be executed
 * @param pc Address of the next instruction to be executed (after this one)
 * @param before True if the hook is called before executing the instruction, false if after
 * @param cond True if the instruction's condition evaluated to true
 */
EAR_HaltReason Debugger_execHook(void* cookie, EAR_Instruction* insn, EAR_FullAddr pc, bool before, bool cond) {
	(void)pc;
	Debugger* dbg = cookie;
	EAR_ThreadState* ctx = CTX(*dbg->cpu);
	
	// Is the debugger even attached?
	if(dbg->debug_flags & DEBUG_DETACHED) {
		return HALT_NONE;
	}
	
	if(!before) {
		dbg->debug_flags &= ~DEBUG_RESUMING;
		return HALT_NONE;
	}
	
	// Only consider stopping in kernel mode when kernel debugging
	if(!(dbg->debug_flags & DEBUG_KERNEL) && Debugger_isKernelMode(ctx)) {
		return HALT_NONE;
	}
	
	// Don't break on BPT instructions whose condition is false
	if(!cond) {
		return HALT_NONE;
	}
	
	switch(insn->op) {
		case OP_BPT:
			if(dbg->debug_flags & DEBUG_RESUMING) {
				// Resuming from a breakpoint, so skip this one. The resuming flag
				// will be disabled above in EAR_stepInstruction.
				break;
			}
			
			EAR_UWord curpc = ctx->cr[CR_INSN_ADDR];
			EAR_UWord dpc = ctx->r[DPC];
			fprintf(stderr, "Hit `BPT` at %04X.%04X\n", curpc, dpc);
			return HALT_BREAKPOINT;
	}
	
	return HALT_NONE;
}


static EAR_HaltReason Debugger_hookMemAccess(
	Debugger* dbg, BreakpointFlags prot, EAR_FullAddr addr, EAR_UWord size
) { //Debugger_hookMemAccess
	ASSERT(!(prot & ~(BP_PROT_MASK | BP_PHYSICAL)));
	
	// Is the debugger even attached?
	if(dbg->debug_flags & DEBUG_DETACHED) {
		return HALT_NONE;
	}
	
	// When resuming from a breakpoint or trying to disassemble code, don't halt
	if(dbg->debug_flags & (DEBUG_RESUMING | DEBUG_NOBREAK)) {
		return HALT_NONE;
	}
	
	// Only consider stopping in kernel mode when kernel debugging
	if(!(dbg->debug_flags & DEBUG_KERNEL) && Debugger_isKernelMode(CTX(*dbg->cpu))) {
		return HALT_NONE;
	}
	
	enumerate(&dbg->breakpoints, i, bp) {
		if(!(bp->flags & BP_ENABLED)) {
			continue;
		}
		
		if(!(bp->flags & prot)) {
			continue;
		}
		
		if(!(addr <= bp->addr && bp->addr < addr + size)) {
			continue;
		}
		
		const char* accessMode;
		switch(prot) {
			case BP_READ:
				accessMode = "read";
				break;
			
			case BP_WRITE:
				accessMode = "write";
				break;
			
			case BP_EXECUTE:
				accessMode = "execute";
				break;
			
			default:
				accessMode = "access";
				break;
		}
		fprintf(
			stderr, "HW breakpoint #%u hit trying to %s %u byte%s at ",
			(BreakpointID)(i + 1), accessMode, size, size == 1 ? "" : "s"
		);
		
		if(prot & BP_PHYSICAL) {
			fprintf(stderr, "%02X:%04X\n", EAR_FULL_REGION(addr), EAR_FULL_NOTREGION(addr));
		}
		else {
			fprintf(stderr, "%04X\n", addr);
		}
		
		return HALT_BREAKPOINT;
	}
	
	return HALT_NONE;
}


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
) { //Debugger_busHandler
	EAR_HaltReason r;
	Debugger* dbg = cookie;
	BreakpointFlags prot = BP_PHYSICAL;
	if(mode == BUS_MODE_READ) {
		prot |= BP_READ;
	}
	else if(mode == BUS_MODE_WRITE) {
		prot |= BP_WRITE;
	}
	
	// Check if this physical memory access hits a breakpoint
	r = Debugger_hookMemAccess(dbg, prot, paddr, is_byte ? 1 : 2);
	if(r != HALT_NONE) {
		if(out_r) {
			*out_r = r;
		}
		return false;
	}
	
	ASSERT(dbg->bus_fn != NULL);
	if(!dbg->bus_fn(dbg->bus_cookie, mode, paddr, is_byte, data, &r)) {
		// Only print an error if the debugger is attached
		if(dbg->debug_flags & DEBUG_DETACHED) {
			return false;
		}
		
		const char* accessMode = NULL;
		if(r == HALT_BUS_FAULT) {
			accessMode = "access";
		}
		else if(r == HALT_BUS_PROTECTED) {
			if(mode == BUS_MODE_READ) {
				accessMode = "read";
			}
			else if(mode == BUS_MODE_WRITE) {
				accessMode = "write";
			}
		}
		else {
			accessMode = "unknown";
		}
		
		fprintf(
			stderr, "Bus error: %s violation at %02X:%04X\n",
			accessMode, EAR_FULL_REGION(paddr), EAR_FULL_NOTREGION(paddr)
		);
		
		if(out_r) {
			*out_r = r;
		}
		return false;
	}
	
	return true;
}


/*! Connect the debugger to the MMU */
void Debugger_setMemoryHandler(Debugger* dbg, EAR_MemoryHandler* mem_fn, void* mem_cookie) {
	dbg->mem_fn = mem_fn;
	dbg->mem_cookie = mem_cookie;
}


static inline BreakpointFlags protToFlags(EAR_Protection prot) {
	BreakpointFlags flags = 0;
	if(prot & EAR_PROT_READ) {
		flags |= BP_READ;
	}
	if(prot & EAR_PROT_WRITE) {
		flags |= BP_WRITE;
	}
	if(prot & EAR_PROT_EXECUTE) {
		flags |= BP_EXECUTE;
	}
	return flags;
}


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
) { //Debugger_memoryHandler
	Debugger* dbg = cookie;
	BreakpointFlags flags = protToFlags(prot);
	EAR_HaltReason r = Debugger_hookMemAccess(dbg, flags, (EAR_FullAddr)vmaddr, is_byte ? 1 : 2);
	if(r != HALT_NONE) {
		if(out_r) {
			*out_r = r;
		}
		return false;
	}
	
	return dbg->mem_fn(
		dbg->mem_cookie, prot, mode,
		vmaddr, is_byte, data, out_r
	);
}


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
) { //Debugger_memoryHandler_physical
	(void)prot;
	
	Debugger* dbg = cookie;
	ASSERT(dbg->bus_fn != NULL);
	
	return dbg->bus_fn(dbg->bus_cookie, mode, paddr, is_byte, data, out_r);
}


/*! Destroys a debugger object that was previously created using `Debugger_init`. */
void Debugger_destroy(Debugger* dbg) {
	array_clear(&dbg->breakpoints);
	free(dbg);
}


/*!
 * @brief Registers a hardware breakpoint.
 * 
 * @param addr Address to place a breakpoint
 * @param flags BreakpointFlags
 * @return Registered breakpoint ID
 */
BreakpointID Debugger_addBreakpoint(Debugger* dbg, EAR_FullAddr addr, BreakpointFlags flags) {
	// Can't have an execute breakpoint on a physical address
	ASSERT(!((flags & BP_PHYSICAL) && (flags & BP_EXECUTE)));
	
	Breakpoint new_bp = {
		.addr = addr,
		.flags = BP_IN_USE | BP_ENABLED | flags,
	};
	
	enumerate(&dbg->breakpoints, i, bp) {
		if(!(bp->flags & BP_IN_USE)) {
			*bp = new_bp;
			return (BreakpointID)i;
		}
	}
	
	array_append(&dbg->breakpoints, new_bp);
	return (BreakpointID)(dbg->breakpoints.count - 1);
}


/*!
 * @brief Checks whether a breakpoint with the provided ID exists.
 * 
 * @param bpid Breakpoint ID to check for existence
 * @return True if a breakpoint with that ID exists, false otherwise
 */
static bool Debugger_breakpointExists(Debugger* dbg, BreakpointID bpid) {
	if(bpid >= dbg->breakpoints.count) {
		return false;
	}
	
	return !!(dbg->breakpoints.elems[bpid].flags & BP_IN_USE);
}


/*!
 * @brief Temporarily disables a registered breakpoint.
 * 
 * @param bpid Breakpoint ID to disable
 */
void Debugger_disableBreakpoint(Debugger* dbg, BreakpointID bpid) {
	if(!Debugger_breakpointExists(dbg, bpid)) {
		return;
	}
	
	dbg->breakpoints.elems[bpid].flags &= ~BP_ENABLED;
}


/*!
 * @brief Enables a previously disabled breakpoint.
 * 
 * @param bpid Breakpoint ID to enable
 */
void Debugger_enableBreakpoint(Debugger* dbg, BreakpointID bpid) {
	if(!Debugger_breakpointExists(dbg, bpid)) {
		return;
	}
	
	dbg->breakpoints.elems[bpid].flags |= BP_ENABLED;
}


/*!
 * @brief Toggles whether the target breakpoint is enabled or disabled.
 * 
 * @param bpid Breakpoint ID to toggle
 * @return Whether the breakpoint is now enabled or disabled
 */
bool Debugger_toggleBreakpoint(Debugger* dbg, BreakpointID bpid) {
	if(!Debugger_breakpointExists(dbg, bpid)) {
		return false;
	}
	
	Breakpoint* bp = &dbg->breakpoints.elems[bpid];
	if(bp->flags & BP_ENABLED) {
		bp->flags &= ~BP_ENABLED;
		return false;
	}
	
	bp->flags |= BP_ENABLED;
	return true;
}


/*!
 * @brief Removes a breakpoint so that its ID may be reused.
 * 
 * @param bpid Breakpoint ID to remove
 */
void Debugger_removeBreakpoint(Debugger* dbg, BreakpointID bpid) {
	if(!Debugger_breakpointExists(dbg, bpid)) {
		return;
	}
	
	dbg->breakpoints.elems[bpid].flags = 0;
}


/*! Clear all registered breakpoints. */
void Debugger_clearBreakpoints(Debugger* dbg) {
	array_clear(&dbg->breakpoints);
}


/*!
 * @brief Read a span of physical memory into a buffer.
 * 
 * @param buf Output data buffer
 * @param paddr Physical address to read from
 * @param size Number of bytes to read
 * @param out_r Optional output pointer for the halt reason
 * 
 * @return Number of bytes that could not be read (0 if all bytes were read successfully)
 */
EAR_UWord Debugger_readPhys(
	Debugger* dbg, void* buf,
	EAR_PhysAddr paddr, EAR_UWord size,
	EAR_HaltReason* out_r
) { //Debugger_readPhys
	char* p = buf;
	if(!size) {
		return 0;
	}
	
	if(!dbg->bus_fn) {
		fprintf(stderr, "Debugger doesn't know how to access the physical memory bus\n");
		return size;
	}
	
	// Initially unaligned?
	if(paddr & 1) {
		if(!dbg->bus_fn(dbg->bus_cookie, BUS_MODE_READ, paddr, /*is_byte=*/true, p, out_r)) {
			fprintf(
				stderr, "Failed to read byte at %02X:%04X\n",
				EAR_FULL_REGION(paddr), EAR_FULL_NOTREGION(paddr)
			);
			return size;
		}
		
		++p;
		++paddr;
		--size;
	}
	
	// Word-reading loop
	while(size >= 2) {
		if(!dbg->bus_fn(dbg->bus_cookie, BUS_MODE_READ, paddr, /*is_byte=*/false, p, out_r)) {
			fprintf(
				stderr, "Failed to read word at %02X:%04X\n",
				EAR_FULL_REGION(paddr), EAR_FULL_NOTREGION(paddr)
			);
			return size;
		}
		
		p += 2;
		paddr += 2;
		size -= 2;
	}
	
	// One final byte?
	if(size) {
		ASSERT(size == 1);
		if(!dbg->bus_fn(dbg->bus_cookie, BUS_MODE_READ, paddr, /*is_byte=*/true, p, out_r)) {
			fprintf(
				stderr, "Failed to read byte at %02X:%04X\n",
				EAR_FULL_REGION(paddr), EAR_FULL_NOTREGION(paddr)
			);
			return size;
		}
	}
	
	return 0;
}


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
) { //Debugger_readVirt
	char* p = buf;
	if(!size) {
		return 0;
	}
	
	if(!dbg->mem_fn) {
		fprintf(stderr, "Debugger doesn't know how to access virtual memory\n");
		return size;
	}
	
	// Bounds checking
	if(EAR_VIRTUAL_ADDRESS_SPACE_SIZE - size <= vaddr) {
		size = EAR_VIRTUAL_ADDRESS_SPACE_SIZE - vaddr;
	}
	
	// If an exception is raised partway through this copyout, it will return a smaller size.
	// We don't care what the exception is as it will be printed by the exception handler, so
	// just update the size to dump to the number of bytes that were actually copied.
	bool didSetDebugNoBreak = !(dbg->debug_flags & DEBUG_NOBREAK);
	dbg->debug_flags |= DEBUG_NOBREAK;
	
	// Initially unaligned?
	if(vaddr & 1) {
		if(!dbg->mem_fn(dbg->mem_cookie, prot, BUS_MODE_READ, vaddr, /*is_byte=*/true, p, NULL)) {
			fprintf(stderr, "Failed to read byte at %04X\n", vaddr);
			goto out;
		}
		
		++p;
		++vaddr;
		--size;
	}
	
	// Word-reading loop. This could be more optimized by doing a single MMU translation per page
	while(size >= 2) {
		if(!dbg->mem_fn(dbg->mem_cookie, prot, BUS_MODE_READ, vaddr, /*is_byte=*/false, p, NULL)) {
			fprintf(stderr, "Failed to read word at %04X\n", vaddr);
			goto out;
		}
		
		p += 2;
		vaddr += 2;
		size -= 2;
	}
	
	// One final byte?
	if(size) {
		ASSERT(size == 1);
		if(!dbg->mem_fn(dbg->mem_cookie, prot, BUS_MODE_READ, vaddr, /*is_byte=*/true, p, NULL)) {
			fprintf(stderr, "Failed to read byte at %04X\n", vaddr);
			goto out;
		}
		--size;
	}
	
out:
	if(didSetDebugNoBreak) {
		dbg->debug_flags &= ~DEBUG_NOBREAK;
	}
	
	return size;
}


static void Debugger_fillFakeTTB(MMU_PageTable* ttb, EAR_Byte region) {
	uint16_t i;
	for(i = 0; i < EAR_PAGE_COUNT; i++) {
		MMU_PTE entry = ((uint16_t)region << (EAR_REGION_SHIFT - EAR_PAGE_SHIFT)) | i;
		ttb->entries[i] = entry;
	}
}


static EAR_HaltReason Debugger_copyTTB(Debugger* dbg, MMU_PageTable* ttb, uint16_t membase) {
	EAR_HaltReason r = HALT_NONE;
	if(!(membase & MMU_ENABLED)) {
		Debugger_fillFakeTTB(ttb, (uint8_t)(membase >> MEMBASE_REGION_SHIFT));
		return r;
	}
	
	EAR_FullAddr addr = (uint32_t)(membase & ~MMU_ENABLED) << EAR_PAGE_SHIFT;
	Debugger_readPhys(dbg, ttb, addr, sizeof(*ttb), &r);
	return r;
}


/*!
 * @brief Print a description of the active thread state's virtual memory map to the file.
 * 
 * @param stream File stream used for output
 */
void Debugger_showVMMap(Debugger* dbg, FILE* stream) {
	EAR_ThreadState* ctx = CTX(*dbg->cpu);
	Pegasus* peg = dbg->pegs[dbg->cpu->ctx.active];
	uint16_t membases[3];
	MMU_PageTable ttbs[3] = {0};
	bool valid[3] = {false};
	
	membases[0] = ctx->cr[CR_MEMBASE_R];
	membases[1] = ctx->cr[CR_MEMBASE_W];
	membases[2] = ctx->cr[CR_MEMBASE_X];
	
	unsigned i;
	for(i = 0; i < 3; i++) {
		EAR_HaltReason r = Debugger_copyTTB(dbg, &ttbs[i], membases[i]);
		if(r != HALT_NONE) {
			const char* protnames[] = {"R", "W", "X"};
			fprintf(stream, "Unable to access %s TTB: %s\n", protnames[i], EAR_haltReasonToString(r));
			continue;
		}
		
		valid[i] = true;
	}
	
	EAR_UWord page_index = 0;
	while(page_index < EAR_PAGE_COUNT) {
		MMU_PTE start_ppns[3] = {0xFFFF, 0xFFFF, 0xFFFF};
		for(i = 0; i < 3; i++) {
			if(valid[i]) {
				start_ppns[i] = ttbs[i].entries[page_index];
			}
		}
		
		EAR_UWord next_index;
		for(next_index = page_index + 1; next_index < EAR_PAGE_COUNT; next_index++) {
			// Check each prot type
			bool stop = false;
			for(i = 0; i < 3; i++) {
				if(!valid[i]) {
					continue;
				}
				
				MMU_PTE start_ppn = start_ppns[i];
				MMU_PTE next_ppn = ttbs[i].entries[next_index];
				
				if(MMU_PTE_INVALID(start_ppn)) {
					if(next_ppn != start_ppn) {
						// All pages in a range should be accessible or not
						stop = true;
						break;
					}
				}
				else if(next_ppn != start_ppn + (next_index - page_index)) {
					// Physically contiguous (for this prot)?
					stop = true;
					break;
				}
			}
			
			if(stop) {
				break;
			}
		}
		
		char addrs[3][8];
		for(i = 0; i < 3; i++) {
			if(!valid[i]) {
				strncpy(addrs[i], "INVALID", sizeof(addrs[i]));
			}
			
			if(MMU_PTE_INVALID(start_ppns[i])) {
				snprintf(addrs[i], sizeof(addrs[i]), "INV::%02X", start_ppns[i] & 0xFF);
			}
			else {
				snprintf(
					addrs[i], sizeof(addrs[i]), "%02X:%02X00",
					start_ppns[i] >> 8, start_ppns[i] & 0xFF
				);
			}
		}
		
		fprintf(
			stream, "%04X-%04X: R=%7s W=%7s X=%7s",
			page_index * EAR_PAGE_SIZE,
			(uint32_t)next_index * EAR_PAGE_SIZE - 1,
			addrs[0], addrs[1], addrs[2]
		);
		
		// Is this mapped range part of a Pegasus segment?
		if(peg) {
			foreach(&peg->segments, seg) {
				if(seg->virtual_page == page_index) {
					fprintf(stream, "  %s", seg->name);
					break;
				}
			}
		}
		
		fprintf(stream, "\n");
		page_index = next_index;
	}
}


/*!
 * @brief Import segment and symbol information from a Pegasus image.
 * 
 * @param peg Pegasus image to import
 * @param alt True to import for the inactive thread state, false for the active one
 */
void Debugger_addPegasusImage(Debugger* dbg, Pegasus* peg, bool alt) {
	Pegasus** pegslot = &dbg->pegs[dbg->cpu->ctx.active ^ alt];
	if(*pegslot) {
		Pegasus_destroy(pegslot);
	}
	
	*pegslot = peg;
}


/*!
 * @brief Display context of the selected thread state's execution.
 * 
 * @param alt True to use the inactive thread state
 * @param stream File stream used for output
 */
void Debugger_showContext(Debugger* dbg, bool alt, FILE* stream) {
	EAR_ThreadState* ctx = CTX_X(*dbg->cpu, alt);
	fprintf(stream, "\nThread state:\n");
	Debugger_showRegs(dbg, alt, stream);
	
	fprintf(stream, "\nNext instructions:\n");
	
	dbg->debug_flags |= DEBUG_NOBREAK;
	EAR_VirtAddr pc = ctx->r[PC];
	dbg->cpu->ctx.active ^= alt;
	Debugger_showDisassembly(
		dbg, dbg->mem_fn, dbg->mem_cookie,
		pc, ctx->r[DPC], 5,
		/*physical=*/false, stream
	);
	dbg->cpu->ctx.active ^= alt;
	dbg->debug_flags &= ~DEBUG_NOBREAK;
}


/*!
 * @brief Prints a description of an EAR instruction.
 * 
 * @param insn Instruction to print
 * @param pc PC value of the instruction, aka address of NEXT instruction
 * @param stream Stream where the instruction should be written
 */
void Debugger_showInstruction(
	Debugger* dbg, EAR_Instruction* insn, EAR_FullAddr pc, FILE* stream
) { //Debugger_showInstruction
	const char* mnem = EAR_getMnemonic(insn->op);
	const char* cond = EAR_getConditionString(insn->cond);
	const char* suffix = insn->toggle_flags ? "F" : "";
	const char* xx = insn->cross_rx ? "!" : "";
	const char* xy = insn->cross_ry ? "!" : "";
	const char* xz = insn->cross_rd ? "!" : "";
	
	// Hack to display `BRA.cc RD, RA` as `RET.cc`
	bool is_ret = false;
	if(insn->op == OP_BRA && insn->rx == RD && insn->ry == RA) {
		mnem = "RET";
		is_ret = true;
	}
	
	// Print instruction address, mnemonic, and condition code
	fprintf(stream, "%s%s%s", mnem, suffix, cond);
	
	// Instructions w/o any operands
	if(is_ret || insn->op == OP_BPT || insn->op == OP_HLT || insn->op == OP_NOP) {
		fprintf(stream, "\n");
		return;
	}
	
	// If the instruction has mnemonics, then add spaces to align the instruction as
	// needed in case there was no "F" suffix or condition code after the mnemonic.
	if(*suffix == '\0') {
		fprintf(stream, " ");
	}
	if(*cond == '\0') {
		fprintf(stream, "   ");
	}
	
	switch(insn->op) {
		default: // INS.cc <Rd,> Rx, Vy
			if(insn->rd != insn->rx) {
				fprintf(stream, " %s%s,", xz, EAR_getRegisterName(insn->rd));
			}
			fprintf(stream, " %s%s,", xx, EAR_getRegisterName(insn->rx));
			
			if(insn->ry == DPC && !insn->cross_ry) {
				fprintf(stream, " 0x%X\n", insn->imm);
			}
			else {
				fprintf(stream, " %s%s\n", xy, EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_LDW:
		case OP_LDB: // LD[WB].cc Rx, [<Rd +> Vy]
			fprintf(stream, " %s%s, [", xx, EAR_getRegisterName(insn->rx));
			if(insn->rd != ZERO) {
				fprintf(stream, "%s%s + ", xz, EAR_getRegisterName(insn->rd));
			}
			if(insn->ry == DPC && !insn->cross_ry) {
				fprintf(stream, "0x%X]\n", insn->imm);
			}
			else {
				fprintf(stream, "%s%s]\n", xy, EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_STW:
		case OP_STB: // ST[WB].cc [<Rd +> Vy], Rx
			fprintf(stream, " [");
			if(insn->rd != ZERO) {
				fprintf(stream, "%s%s + ", xz, EAR_getRegisterName(insn->rd));
			}
			if(insn->ry == DPC && !insn->cross_ry) {
				fprintf(stream, "0x%X],", insn->imm);
			}
			else {
				fprintf(stream, "%s%s],", xy, EAR_getRegisterName(insn->ry));
			}
			fprintf(stream, "%s%s\n", xx, EAR_getRegisterName(insn->rx));
			break;
		
		case OP_RDC: // RDC.cc Rx, CReg
			fprintf(
				stream, " %s%s, %s%s\n",
				xx, EAR_getRegisterName(insn->rx),
				xy, EAR_getControlRegisterName(insn->ry)
			);
			break;
		
		case OP_WRC: // WRC.cc CReg, Ry
			fprintf(
				stream, " %s%s, %s%s\n",
				xx, EAR_getControlRegisterName(insn->rx),
				xy, EAR_getRegisterName(insn->ry)
			);
			break;
		
		case OP_RDB: // RDB.cc Rx, (portnum)
			fprintf(
				stream, " %s%s, (%" PRIu8 ")\n",
				xx, EAR_getRegisterName(insn->rx),
				insn->port_number
			);
			break;
		
		case OP_WRB: // WRB.cc (portnum), Vy
			fprintf(stream, " (%" PRIu8 "),", insn->port_number);
			if(insn->ry == DPC) {
				fprintf(stream, " 0x%X\n", insn->imm);
			}
			else {
				fprintf(stream, " %s%s\n", xy, EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_BRR:
		case OP_FCR: // BRR/FCR.cc Imm16
		{
			EAR_FullAddr target = (pc + insn->imm) & (EAR_VIRTUAL_ADDRESS_SPACE_SIZE - 1);
			Pegasus* peg = dbg->pegs[dbg->cpu->ctx.active];
			if(peg) {
				Pegasus_Symbol* sym = Pegasus_findSymbolByAddress(peg, target);
				if(sym) {
					if(sym->value == target) {
						fprintf(stream, " %s\n", sym->name);
						break;
					}
					
					unsigned sym_offset = target - sym->value;
					
					// Arbitrarily chosen limit
					if(sym_offset < 0x200) {
						fprintf(
							stream, " %s+%#x //%04X.%04X\n",
							sym->name, sym_offset, target, CTX(*dbg->cpu)->r[DPC]
						);
						break;
					}
				}
			}
			
			fprintf(stream, " 0x%X\n", target);
			break;
		}
		
		case OP_PSH:
		case OP_POP: // PSH/POP.cc <Rd,> {Regs16}
		{
			// Print Rd if it's different than the default of SP
			if(insn->rd != SP || insn->cross_rd) {
				fprintf(stream, " %s%s,", xz, EAR_getRegisterName(insn->rd));
			}
			
			// Start of Regs16 bitset
			fprintf(stream, " %s{", xy);
			
			// Print contents of Regs16 bitset
			bool firstReg = true;
			int i, j;
			for(i = 0; i < 16; i++) {
				if(insn->imm & (1 << i)) {
					// Check how many consecutive registers are being pushed/popped
					for(j = i + 1; j < 16; j++) {
						if(!(insn->imm & (1 << j))) {
							break;
						}
					}
					
					// Check if this is a range of consecutive registers
					if(j == i + 1) {
						fprintf(stream, "%s%s", firstReg ? "" : ", ", EAR_getRegisterName(i));
					}
					else {
						fprintf(
							stream, "%s%s-%s",
							firstReg ? "" : ", ", EAR_getRegisterName(i), EAR_getRegisterName(j - 1)
						);
					}
					
					firstReg = false;
					
					// Skip j as we know it's not a register included in the Regs16 bitset
					i = j;
				}
			}
			
			// End of Regs16 bitset
			fprintf(stream, "}\n");
			break;
		}
		
		case OP_CMP: // CMP.cc Rx, Vy (no Rd)
			fprintf(stream, " %s%s, ", xx, EAR_getRegisterName(insn->rx));
			
			if(insn->ry == DPC && !insn->cross_ry) {
				fprintf(stream, "0x%X\n", insn->imm);
			}
			else {
				fprintf(stream, "%s%s\n", xy, EAR_getRegisterName(insn->ry));
			}
			break;
		
		case OP_INC: // INC.cc <Rd,> Rx, SImm4
			if(insn->rd != insn->rx) {
				fprintf(stream, " %s%s,", xz, EAR_getRegisterName(insn->rd));
			}
			EAR_SWord simm;
			memcpy(&simm, &insn->imm, sizeof(simm));
			fprintf(stream, " %s%s, %d\n", xx, EAR_getRegisterName(insn->rx), simm);
			break;
	}
}


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
) { //Debugger_showDisassembly
	uint32_t pc_mask;
	if(physical) {
		pc_mask = EAR_PHYSICAL_ADDRESS_SPACE_SIZE - 1;
	}
	else {
		pc_mask = EAR_VIRTUAL_ADDRESS_SPACE_SIZE - 1;
	}
	
	Pegasus* peg = dbg->pegs[dbg->cpu->ctx.active];
	
	unsigned dis_idx;
	for(dis_idx = 0; dis_idx < count; dis_idx++) {
		if(physical) {
			fprintf(stream, "%02X:%04X.%04X: ", EAR_FULL_REGION(addr), EAR_FULL_NOTREGION(addr), dpc);
		}
		else {
			if(peg) {
				Pegasus_Symbol* sym = Pegasus_findSymbolByAddress(peg, addr);
				if(sym && sym->value == addr) {
					fprintf(stream, "%s:\n", sym->name);
				}
			}
			fprintf(stream, "\t%04X.%04X: ", addr, dpc);
		}
		
		EAR_Instruction insn;
		EAR_ExceptionInfo exc_info = 0;
		EAR_UWord exc_addr = 0;
		EAR_HaltReason r = EAR_fetchInstruction(
			mem_fn, mem_cookie,
			&addr, pc_mask, dpc, /*verbose=*/false,
			&insn, &exc_info, &exc_addr
		);
		if(r != HALT_NONE) {
			if(r == HALT_DECODE) {
				fprintf(stream, "Illegal instruction\n");
			}
			else if(r == HALT_EXCEPTION) {
				fprintf(stream, "Exception: %s\n", EAR_exceptionKindToString(exc_info));
			}
			else {
				fprintf(stream, "Failed to disassemble instruction: %s\n", EAR_haltReasonToString(r));
			}
			break;
		}
		
		Debugger_showInstruction(dbg, &insn, addr, stream);
	}
	
	return dis_idx;
}


static bool deref(Debugger* dbg, EAR_VirtAddr addr, EAR_UWord* out) {
	return !Debugger_readVirt(dbg, out, EAR_PROT_READ, addr, sizeof(*out));
}


/*!
 * @brief Display the backtrace of the selected thread state.
 * 
 * @param alt True to use the inactive thread state
 * @param stream File stream used for output
 */
void Debugger_showBacktrace(Debugger* dbg, bool alt, FILE* stream) {
	EAR_ThreadState* ctx = CTX_X(*dbg->cpu, alt);
	if(alt) {
		// HACK: Need to swap active thread state for Debugger_readVirt()
		// to access the inactive thread state.
		dbg->cpu->ctx.active ^= 1;
	}
	Pegasus* peg = dbg->pegs[dbg->cpu->ctx.active];
	Pegasus_Symbol* sym = NULL;
	
	EAR_VirtAddr pc = ctx->r[PC];
	EAR_UWord dpc = ctx->r[DPC];
	EAR_VirtAddr fp = ctx->r[FP];
	fprintf(stream, "frame #0: %04X.%04X", pc, dpc);
	if(peg) {
		sym = Pegasus_findSymbolByAddress(peg, pc);
		if(sym) {
			unsigned sym_offset = pc - sym->value;
			fprintf(stream, " %s+%#x", sym->name, sym_offset);
		}
	}
	fprintf(stream, "\n");
	
	// Cycle detection using trick with a slow and fast pointer
	EAR_VirtAddr fpSlow = fp;
	
	int frameIndex;
	for(frameIndex = 1; ; frameIndex++) {
		EAR_VirtAddr fpNext;
		if(!deref(dbg, fp, &fpNext)) {
			// fprintf(
			// 	stream, "Failed to dereference frame pointer at %04X\n",
			// 	fp, fp + 3 * 2
			// );
			break;
		}
		
		// Check for frame pointer sentinel (points to itself), indicating
		// the bottommost stack frame.
		if(fpNext == fp) {
			break;
		}
		
		// Fast pointer can only hit slow pointer if there is a cycle
		if(fpNext == fpSlow) {
			fprintf(stream, "Backtrace: cycle detected!\n");
			break;
		}
		
		if(
			!deref(dbg, fp + sizeof(EAR_UWord), &pc)
			|| !deref(dbg, fp + 2 * sizeof(EAR_UWord), &dpc)
		) {
			break;
		}
		
		fp = fpNext;
		fprintf(
			stream, "frame #%d: %04X.%04X",
			frameIndex, pc, dpc
		);
		if(peg) {
			sym = Pegasus_findSymbolByAddress(peg, pc);
			if(sym) {
				unsigned sym_offset = pc - sym->value;
				fprintf(stream, " %s+%#x", sym->name, sym_offset);
			}
		}
		fprintf(stream, "\n");
		
		// Advance slow pointer every other iteration
		if(frameIndex % 2 == 0) {
			if(!deref(dbg, fpSlow, &fpSlow)) {
				// Shouldn't be possible, as the fast pointer already got here
				ASSERT(!"Failed to dereference slow pointer in backtrace");
			}
		}
	}
	
	// Restore active thread state index (if swapped before)
	if(alt) {
		dbg->cpu->ctx.active ^= 1;
	}
}

/*! Prints the thread's register state to the output stream. */
void Debugger_showRegs(Debugger* dbg, bool alt, FILE* stream) {
	EAR_ThreadState* ctx = CTX_X(*dbg->cpu, alt);
	EAR_UWord* r = ctx->r;
	uint16_t flags = ctx->cr[CR_FLAGS];
	fprintf(
		stream,
		"   (ZERO)R0: %04X      (S1)R8: %04X\n"
		"     (A0)R1: %04X      (S2)R9: %04X\n"
		"     (A1)R2: %04X     (FP)R10: %04X\n"
		"     (A2)R3: %04X     (SP)R11: %04X\n"
		"     (A3)R4: %04X     (RA)R12: %04X\n"
		"     (A4)R5: %04X     (RD)R13: %04X\n"
		"     (A5)R6: %04X     (PC)R14: %04X",
		r[R0], r[R8],
		r[R1], r[R9],
		r[R2], r[R10],
		r[R3], r[R11],
		r[R4], r[R12],
		r[R5], r[R13],
		r[R6], r[R14]
	);
	
	Pegasus* peg = dbg->pegs[dbg->cpu->ctx.active ^ alt];
	if(peg) {
		Pegasus_Symbol* sym = Pegasus_findSymbolByAddress(peg, r[PC]);
		if(sym) {
			unsigned sym_offset = r[R14] - sym->value;
			if(sym_offset < 0x200) {
				fprintf(stream, " //%s+%#x", sym->name, sym_offset);
			}
		}
	}
	
	fprintf(
		stream,
		"\n"
		"     (S0)R7: %04X    (DPC)R15: %04X\n"
		"FLAGS: %c%c%c%c%c%c%c\n", //ZSPCVXR
		r[R7], r[R15],
		(flags & FLAG_ZF) ? 'Z' : 'z',
		(flags & FLAG_SF) ? 'S' : 's',
		(flags & FLAG_PF) ? 'P' : 'p',
		(flags & FLAG_CF) ? 'C' : 'c',
		(flags & FLAG_VF) ? 'V' : 'v',
		(flags & FLAG_DENY_XREGS) ? 'X' : 'x',
		(flags & FLAG_RESUME) ? 'R' : 'r'
	);
}

static void Debugger_showMembase(const char* name, uint16_t membase, FILE* stream) {
	bool mmu_enabled = !!(membase & 1);
	fprintf(stream, "%s: mmu_enabled=%s", name, mmu_enabled ? "true" : "false");
	if(mmu_enabled) {
		fprintf(stream, ", ttb=%02X:%02X00\n", membase >> 8, membase & 0x00FE);
	}
	else {
		fprintf(stream, ", region=0x%02X\n", membase >> 8);
	}
}

/*! Prints a description of the thread's control registers to the output stream. */
void Debugger_showControlRegs(Debugger* dbg, bool alt, FILE* stream) {
	EAR_ThreadState* ctx = CTX_X(*dbg->cpu, alt);
	uint16_t* cr = ctx->cr;
	fprintf(stream, "\nControl registers:\n\n");
	
	uint32_t val = cr[CR_CREG_DENY_R] | ((uint32_t)cr[CR_CREG_DENY_W] << 16);
	if(!val) {
		fprintf(stream, "CREG_DENY: No denials\n");
	}
	else if(val == 0xFFFFFFFF) {
		fprintf(stream, "CREG_DENY: Deny RW *\n");
	}
	else {
		fprintf(stream, "CREG_DENY:\n");
		for(uint32_t i = 0; i < 16; i++) {
			const char* denyRead = "";
			const char* denyWrite = "";
			if(val & (1 << i)) {
				denyRead = "R";
			}
			if(val & (1 << (16 + i))) {
				denyWrite = "W";
			}
			
			if(!*denyRead && !*denyWrite) {
				continue;
			}
			
			fprintf(stream, " * Deny %s%s %s\n", denyRead, denyWrite, EAR_getControlRegisterName(i));
		}
	}
	
	val = cr[CR_INSN_DENY_0] | ((uint32_t)cr[CR_INSN_DENY_1] << 16);
	if(val) {
		fprintf(stream, "INSN_DENY:\n");
	}
	else {
		fprintf(stream, "INSN_DENY: No denials\n");
	}
	for(uint32_t i = 0; i < 32; i++) {
		if(val & (1 << i)) {
			fprintf(stream, " * Deny instruction %s\n", EAR_getMnemonic((EAR_Opcode)i));
		}
	}
	
	fprintf(stream, "EXC_INFO: %" PRIu16 "\n", cr[CR_EXC_INFO]);
	fprintf(stream, "EXC_ADDR: %" PRIu16 "\n", cr[CR_EXC_ADDR]);
	Debugger_showMembase("MEMBASE_R", cr[CR_MEMBASE_R], stream);
	Debugger_showMembase("MEMBASE_W", cr[CR_MEMBASE_W], stream);
	Debugger_showMembase("MEMBASE_X", cr[CR_MEMBASE_X], stream);
	
	fprintf(stream, "INSN_ADDR: 0x%" PRIX16, cr[CR_INSN_ADDR]);
	Pegasus* peg = dbg->pegs[dbg->cpu->ctx.active ^ alt];
	if(peg) {
		Pegasus_Symbol* sym = Pegasus_findSymbolByAddress(peg, cr[CR_INSN_ADDR]);
		if(sym) {
			unsigned sym_offset = cr[CR_INSN_ADDR] - sym->value;
			if(sym_offset < 0x200) {
				fprintf(stream, " //%s+%#x", sym->name, sym_offset);
			}
		}
	}
	fprintf(stream, "\n");
	
	fprintf(stream, "TIMER: %" PRIu16 "\n", cr[CR_TIMER]);
	val = cr[CR_INSN_COUNT_LO] | ((uint32_t)cr[CR_INSN_COUNT_HI] << 16);
	fprintf(stream, "INSN_COUNT: %" PRIu32 "\n", val);
	fprintf(stream, "EXEC_STATE_0: 0x%" PRIX32 "\n", cr[CR_EXEC_STATE_0]);
	fprintf(stream, "EXEC_STATE_1: 0x%" PRIX32 "\n", cr[CR_EXEC_STATE_0]);
	
	fprintf(stream, "FLAGS: 0x%" PRIX16 "\n", cr[CR_FLAGS]);
}
