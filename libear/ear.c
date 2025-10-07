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

#define DEVEL_RETURN(verbose, reason) do { \
	if(verbose) { \
		fprintf(stderr, STRINGIFY(reason) "(" STRINGIFY(__LINE__) ")\n"); \
	} \
	return (reason); \
} while(0)

#else /* EAR_DEVEL */

#define DEVEL_RETURN(verbose, reason) do { \
	return (reason); \
} while(0)

#endif /* EAR_DEVEL */


/*!
 * @brief Initialize an EAR CPU with default register values and memory mappings
 */
void EAR_init(EAR* ear) {
	memset(ear, 0, sizeof(*ear));
	
	// Init registers
	EAR_resetRegisters(ear);
}

/*!
 * @brief Set the function called to handle all memory accesses
 * 
 * @param mem_fn Function pointer called to handle all memory accesses
 * @param cookie Opaque value passed to mem_fn
 */
void EAR_setMemoryHandler(EAR* ear, EAR_MemoryHandler* mem_fn, void* cookie) {
	ear->mem_fn = mem_fn;
	ear->mem_cookie = cookie;
}

/*! Reset the normal thread state to its default values */
void EAR_resetRegisters(EAR* ear) {
	memset(&ear->ctx, 0, sizeof(ear->ctx));
}

/*! Set the current thread state of the EAR CPU
 * @param thstate New thread state
 */
void EAR_setThreadState(EAR* ear, const EAR_ThreadState* thstate) {
	memcpy(&ear->ctx, thstate, sizeof(ear->ctx));
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
 * @brief Set the function called before executing each instruction
 * 
 * @param exec_fn Function pointer called before executing each instruction
 * @param exec_cookie Opaque value passed as the first parameter to `exec_fn`
 */
void EAR_setExecHook(EAR* ear, EAR_ExecHook* exec_fn, void* exec_cookie) {
	ear->exec_fn = exec_fn;
	ear->exec_cookie = exec_cookie;
}

static EAR_HaltReason EAR_raiseException(EAR* ear, EAR_ExceptionInfo exc_info, EAR_UWord exc_addr) {
	CTX(*ear)->cr[CR_EXC_ADDR] = exc_addr;
	CTX(*ear)->cr[CR_EXC_INFO] = exc_info;
	
	// Check if the other thread is in an exception state
	if(CTX_X(*ear, 1)->cr[CR_EXC_INFO] & 1) {
		return HALT_DOUBLE_FAULT;
	}
	
	// Swap thread contexts
	ear->ctx.active ^= 1;
	
	// Debugger wants to break on HLT?
	if(!exc_info && ear->exc_catch & EXC_MASK_HLT) {
		return HALT_DEBUGGER;
	}
	
	// Debugger wants to break on this exception type?
	if(ear->exc_catch & (1 << EXC_CODE_GET(exc_info))) {
		return HALT_DEBUGGER;
	}
	
	return HALT_EXCEPTION;
}

static EAR_HaltReason EAR_readByte(
	EAR_MemoryHandler* mem_fn, void* mem_cookie,
	EAR_FullAddr addr, EAR_Byte* out_byte
) { //EAR_readByte
	EAR_HaltReason r = HALT_NONE;
	ASSERT(mem_fn != NULL);
	mem_fn(
		mem_cookie, EAR_PROT_READ, BUS_MODE_READ,
		addr, /*is_byte=*/true, out_byte, &r
	);
	return r;
}

static EAR_HaltReason EAR_writeByte(
	EAR_MemoryHandler* mem_fn, void* mem_cookie,
	EAR_FullAddr addr, EAR_Byte byte
) { //EAR_writeByte
	EAR_HaltReason r = HALT_NONE;
	ASSERT(mem_fn != NULL);
	mem_fn(
		mem_cookie, EAR_PROT_WRITE, BUS_MODE_WRITE,
		addr, /*is_byte=*/true, &byte, &r
	);
	return r;
}

static EAR_HaltReason EAR_readWord(
	EAR_MemoryHandler* mem_fn, void* mem_cookie,
	EAR_FullAddr addr, EAR_UWord* out_word
) { //EAR_readWord
	// Can only read words from an aligned address
	if(addr & 1) {
		return HALT_UNALIGNED;
	}
	
	EAR_HaltReason r = HALT_NONE;
	ASSERT(mem_fn != NULL);
	mem_fn(
		mem_cookie, EAR_PROT_READ, BUS_MODE_READ,
		addr, /*is_byte=*/false, out_word, &r
	);
	return r;
}

static EAR_HaltReason EAR_writeWord(
	EAR_MemoryHandler* mem_fn, void* mem_cookie,
	EAR_FullAddr addr, EAR_UWord word
) { //EAR_writeWord
	// Can only write words to an aligned address
	if(addr & 1) {
		return HALT_UNALIGNED;
	}
	
	EAR_HaltReason r = HALT_NONE;
	ASSERT(mem_fn != NULL);
	mem_fn(
		mem_cookie, EAR_PROT_WRITE, BUS_MODE_WRITE,
		addr, /*is_byte=*/false, &word, &r
	);
	return r;
}

static EAR_HaltReason EAR_fetchCodeByte(
	EAR_MemoryHandler* mem_fn, void* mem_cookie,
	EAR_FullAddr* pc, EAR_FullAddr pc_mask, EAR_UWord dpc,
	EAR_Byte* out_byte, EAR_ExceptionInfo* out_exc_info, EAR_UWord* out_exc_addr
) { //EAR_fetchCodeByte
	EAR_HaltReason r = HALT_NONE;
	ASSERT(mem_fn != NULL);
	if(!mem_fn(
		mem_cookie, EAR_PROT_EXECUTE, BUS_MODE_READ,
		*pc, /*is_byte=*/true, out_byte, &r
	)) {
		if(EAR_FAILED(r)) {
			*out_exc_addr = *pc;
			*out_exc_info = EXC_FAULT_MAKE(r, EAR_PROT_EXECUTE);
			return HALT_EXCEPTION;
		}
		return r;
	}
	
	// Update PC
	*pc = (*pc + 1 + dpc) & pc_mask;
	return HALT_NONE;
}

static EAR_HaltReason EAR_fetchCodeImm16(
	EAR_MemoryHandler* mem_fn, void* mem_cookie,
	EAR_FullAddr* pc, EAR_FullAddr pc_mask, EAR_UWord dpc,
	EAR_UWord* out_value, EAR_ExceptionInfo* out_exc_info, EAR_UWord* out_exc_addr
) { //EAR_fetchCodeImm16
	EAR_HaltReason ret;
	EAR_Byte imm_byte;
	
	*out_value = 0;
	
	// Fetch first byte (low byte)
	ret = EAR_fetchCodeByte(
		mem_fn, mem_cookie, pc, pc_mask, dpc,
		&imm_byte, out_exc_info, out_exc_addr
	);
	if(ret != HALT_NONE) {
		return ret;
	}
	
	// Store low byte
	*out_value = imm_byte;
	
	// Fetch second byte (high byte)
	ret = EAR_fetchCodeByte(
		mem_fn, mem_cookie, pc, pc_mask, dpc,
		&imm_byte, out_exc_info, out_exc_addr
	);
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
	EAR_FullAddr* pc, EAR_FullAddr pc_mask, EAR_UWord dpc, bool verbose,
	EAR_Instruction* out_insn, EAR_ExceptionInfo* out_exc_info, EAR_UWord* out_exc_addr
) { //EAR_fetchInstruction
	EAR_Byte ins_byte = 0;
	EAR_HaltReason ret = HALT_NONE;
	EAR_Cond cond = 0;
	EAR_Opcode op = 0;
	bool hasRdPrefix = false;
	bool hasRdxPrefix = false;
	bool hasXZPrefix = false;
	
	// Zero-initialize outputs
	memset(out_insn, 0, sizeof(*out_insn));
	*out_exc_info = 0;
	
	// Handle instruction prefixes
	do {
		// Read instruction byte
		ret = EAR_fetchCodeByte(
			mem_fn, mem_cookie, pc, pc_mask, dpc,
			&ins_byte, out_exc_info, out_exc_addr
		);
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
				// Doesn't make sense to allow multiple XC prefixes
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			out_insn->cond |= 0x8;
		}
		else if(op == PREFIX_TF) {
			if(out_insn->toggle_flags) {
				// Doesn't make sense to allow multiple TF prefixes
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			out_insn->toggle_flags = true;
		}
		else if(op == PREFIX_XX) {
			if(out_insn->cross_rx) {
				// Doesn't make sense to allow multiple XX prefixes
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			out_insn->cross_rx = true;
		}
		else if(op == PREFIX_XY) {
			if(out_insn->cross_ry) {
				// Doesn't make sense to allow multiple XY prefixes
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			out_insn->cross_ry = true;
		}
		else if(op == PREFIX_XZ) {
			if(hasXZPrefix) {
				// Doesn't make sense to allow multiple XZ prefixes
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			hasXZPrefix = true;
		}
		else if(op & PREFIX_DR_MASK) {
			// Destination Register prefix, set destination register
			if(hasRdPrefix) {
				if(hasRdxPrefix) {
					// Doesn't make sense to allow more than two DR prefixes
					DEVEL_RETURN(verbose, HALT_DECODE);
				}
				hasRdxPrefix = true;
				out_insn->rdx = op & 0x0F;
				if(out_insn->rdx == out_insn->rd) {
					// Rdx can't be the same as Rd
					DEVEL_RETURN(verbose, HALT_DECODE);
				}
			}
			else {
				hasRdPrefix = true;
				out_insn->rd = op & 0x0F;
			}
			
			if(hasXZPrefix) {
				// XZ prefix makes Rd/Rdx refer to the inactive thread context
				out_insn->cross_rd = true;
			}
		}
		else {
			// Invalid instruction prefix
			DEVEL_RETURN(verbose, HALT_DECODE);
		}
	} while(cond == COND_SP);
	
	// Write condition code and opcode to the decoded instruction
	out_insn->cond |= cond;
	out_insn->op = op;
	
	// Decode additional instruction-specific bytes (if any)
	switch(out_insn->op) {
		case OP_PSH:
		case OP_POP: // PSH/POP Rd, {Regs16}
			if(hasRdxPrefix) {
				// Rdx doesn't make sense for PSH/POP
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			
			if(!hasRdPrefix) {
				// Rd is usually SP unless overridden
				out_insn->rd = SP;
			}
			
			if(hasXZPrefix) {
				out_insn->cross_rd = true;
			}
			
			// Read low byte of regs16
			ret = EAR_fetchCodeByte(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&ins_byte, out_exc_info, out_exc_addr
			);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Set low byte of regs16
			out_insn->imm = ins_byte;
			
			// Read high byte of regs16
			ret = EAR_fetchCodeByte(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&ins_byte, out_exc_info, out_exc_addr
			);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Set high byte of regs16
			out_insn->imm |= ins_byte << 8;
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
		case OP_RDC:
		case OP_WRC:
		case OP_LDW:
		case OP_STW:
		case OP_LDB:
		case OP_STB:
		case OP_BRA:
		case OP_FCA: // INSN [Rdx:]Rd, Rx, Vy
			// Read Rx:Ry byte
			ret = EAR_fetchCodeByte(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&ins_byte, out_exc_info, out_exc_addr
			);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Extract Rx and Ry register numbers
			out_insn->rx = ins_byte >> 4;
			out_insn->ry = ins_byte & 0x0F;
			
			// Set Rd, defaulting to Rx unless there was an DR prefix
			if(!hasRdPrefix) {
				if(op == OP_LDW || op == OP_STW || op == OP_LDB || op == OP_STB) {
					// For load and store instructions, Rd is used as the
					// base address for indexing. Meaning, the full address
					// is calculated as Rd + Vy. When no DR prefix is
					// present, Rd is set to ZERO to only use Vy.
					out_insn->rd = ZERO;
				}
				else {
					out_insn->rd = out_insn->rx;
					out_insn->cross_rd = out_insn->cross_rx;
				}
			}
			else if(!(OP_BIT(op) & INSN_ALLOWS_DR_BITMAP)) {
				// DR prefix is pointless
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			
			if(!hasRdxPrefix) {
				out_insn->rdx = ZERO;
			}
			else if(op != OP_MLU && op != OP_MLS && op != OP_DVU && op != OP_DVS) {
				// Rdx is only allowed for some instructions
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			
			// CMP is handled just like SUB except Rd is set to ZERO so the result is discarded
			if(op == OP_CMP) {
				out_insn->rd = ZERO;
			}
			
			// RDC doesn't allow an Imm16
			if(op == OP_RDC) {
				return HALT_NONE;
			}
			
			// When Ry is DPC, there will be an immediate value after the opcode byte
			if(out_insn->ry != DPC || out_insn->cross_ry) {
				return HALT_NONE;
			}
			
			// For shift instructions, the immediate is a V8
			if(op == OP_SHL || op == OP_SRU || op == OP_SRS) {
				ret = EAR_fetchCodeByte(
					mem_fn, mem_cookie, pc, pc_mask, dpc,
					&ins_byte, out_exc_info, out_exc_addr
				);
				out_insn->imm = ins_byte;
				return ret;
			}
			
			// Read immediate value
			return EAR_fetchCodeImm16(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&out_insn->imm, out_exc_info, out_exc_addr
			);
		
		case OP_BRR:
		case OP_FCR: // BRR/FCR <label> (encoded as Imm16)
			// DR, XX, XY, XZ prefixes are pointless
			if(hasRdPrefix || out_insn->cross_rx || out_insn->cross_ry || hasXZPrefix) {
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			
			// Read Imm16
			return EAR_fetchCodeImm16(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&out_insn->imm, out_exc_info, out_exc_addr
			);
		
		case OP_RDB: // RDB Rx, (portnum)
			// DR prefix is pointless
			if(hasRdPrefix) {
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			
			// Read Rx:Imm4 byte
			ret = EAR_fetchCodeByte(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&ins_byte, out_exc_info, out_exc_addr
			);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Extract Rx register number and port number
			out_insn->rx = ins_byte >> 4;
			out_insn->port_number = ins_byte & 0x0F;
			out_insn->rd = out_insn->rx;
			out_insn->cross_rd = out_insn->cross_rx;
			return HALT_NONE;
		
		case OP_WRB:
			// DR prefix is pointless
			if(hasRdPrefix) {
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			
			// Read Imm4:Ry byte
			ret = EAR_fetchCodeByte(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&ins_byte, out_exc_info, out_exc_addr
			);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Port number and Ry register number
			out_insn->port_number = ins_byte >> 4;
			out_insn->ry = ins_byte & 0x0F;
			
			// If Ry is DPC, there's another byte for the immediate
			if(out_insn->ry != DPC || out_insn->cross_ry) {
				return HALT_NONE;
			}
			
			// Read immediate byte (Imm8)
			ret = EAR_fetchCodeByte(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&ins_byte, out_exc_info, out_exc_addr
			);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Set value of Vy to the Imm8
			out_insn->imm = ins_byte;
			return HALT_NONE;
		
		case OP_INC: // INC Rx, SImm4
			// Read byte containing Rx and SImm4
			ret = EAR_fetchCodeByte(
				mem_fn, mem_cookie, pc, pc_mask, dpc,
				&ins_byte, out_exc_info, out_exc_addr
			);
			if(ret != HALT_NONE) {
				return ret;
			}
			
			// Extract Rx and SImm4 (increment amount)
			out_insn->rx = ins_byte >> 4;
			out_insn->imm = ins_byte & 0x0F;
			
			// Check sign bit of SImm4
			if(ins_byte & (1 << 3)) {
				// Sign extend ry_val
				out_insn->imm |= (EAR_UWord)-1 << 4;
			}
			else {
				// No need to allow INC 0, so increment non-negative values to expand
				// the positive max value to 8
				++out_insn->imm;
			}
			
			// If there's no DR prefix, then default Rd to Rx
			if(!hasRdPrefix) {
				out_insn->rd = out_insn->rx;
				out_insn->cross_rd = out_insn->cross_rx;
			}
			return HALT_NONE;
		
		case OP_BPT:
		case OP_HLT:
		case OP_NOP: // Single-byte instructions
			// DR prefix is pointless
			if(hasRdPrefix) {
				DEVEL_RETURN(verbose, HALT_DECODE);
			}
			return HALT_NONE;
		
		default:
			if(verbose) {
				fprintf(stderr, "Invalid opcode: 0x%02X (0x%02X)\n", op, ins_byte);
			}
			DEVEL_RETURN(verbose, HALT_DECODE);
	}
}

static bool EAR_evaluateCondition(EAR* ear, EAR_Cond cond) {
	EAR_Flag flags = CTX(*ear)->cr[CR_FLAGS];
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
	EAR_ThreadState* ctx = CTX(*ear);
	
	// Denied instruction?
	EAR_UWord insn_deny = ctx->cr[insn->op < 16 ? CR_INSN_DENY_0 : CR_INSN_DENY_1];
	if(insn_deny & (1 << (insn->op & 0xF))) {
		return EAR_raiseException(ear, EXC_DENIED_INSN, insn->op);
	}
	
	// Denied XREGS prefixes?
	if((ctx->cr[CR_FLAGS] & FLAG_DENY_XREGS) && (insn->cross_rx || insn->cross_ry || insn->cross_rd)) {
		EAR_UWord which = 0;
		if(insn->cross_rx) {
			which = PREFIX_XX;
		}
		else if(insn->cross_ry) {
			which = PREFIX_XY;
		}
		else if(insn->cross_rd) {
			which = PREFIX_XZ;
		}
		return EAR_raiseException(ear, EXC_DENIED_INSN, which);
	}
	
	EAR_ThreadState* rx_ctx = CTX_X(*ear, insn->cross_rx);
	EAR_ThreadState* ry_ctx = CTX_X(*ear, insn->cross_ry);
	EAR_ThreadState* rd_ctx = CTX_X(*ear, insn->cross_rd);
	EAR_UWord vxu = rx_ctx->r[insn->rx];
	EAR_UWord vyu = insn->imm;
	EAR_SWord vxs, vys;
	EAR_Flag flags = ctx->cr[CR_FLAGS];
	EAR_FullAddr addr = {0};
	EAR_Register rd = insn->rd;
	EAR_Register rdx = insn->rdx;
	EAR_Register rx = insn->rx;
	EAR_Register ry = insn->ry;
	bool resume = !!(flags & FLAG_RESUME);
	flags &= ~FLAG_RESUME;
	ctx->cr[CR_FLAGS] = flags;
	
	// Need to lookup the value of Ry?
	if(ry != DPC || insn->cross_ry) {
		vyu = ry_ctx->r[ry];
	}
	
	// Use memcpy() to avoid UB via signed-overflow
	memcpy(&vxs, &vxu, sizeof(vxs));
	memcpy(&vys, &vyu, sizeof(vys));
	
	// Destination register values
	EAR_UWord vd = 0;
	EAR_UWord vdx = 0;
	bool write_rd = false;
	bool write_rdx = false;
	
	// Controls whether the FLAGS register will be written back after executing this instruction
	bool write_flags = true;
	bool use_rdx_for_flags = false;
	
	// Conditional instructions by default don't update FLAGS
	if(insn->cond != COND_AL && insn->cond != COND_SP) {
		write_flags = false;
	}
	
	// Apply the TF prefix
	if(insn->toggle_flags) {
		write_flags = !write_flags;
	}
	
	// Should the ZF, SF, and PF flags be updated with the value of vd?
	bool update_zsp = false;
	
	// Intermediate values in computations
	uint32_t ubig = 0;
	int32_t sbig = 0;
	EAR_SWord stmp = 0;
	EAR_Byte btmp = 0;
	
	switch(insn->op) {
		case OP_INC: // Increment
			vyu = insn->imm;
			//FALLTHROUGH
		
		handle_ADD:  // Used by SUB and CMP
		case OP_ADD: // Add
			vd = vxu + vyu;
			write_rd = true;
			
			// Update CF (unsigned overflow)
			if(vd < vxu) {
				flags |= FLAG_CF;
			}
			else {
				flags &= ~FLAG_CF;
			}
			
			// Update VF (signed overflow)
			if(
				(vxu & EAR_SIGN_BIT) == (vyu & EAR_SIGN_BIT)
				&& (vd & EAR_SIGN_BIT) != (vxu & EAR_SIGN_BIT)
			) {
				// Adding two positive numbers or two negative numbers and getting a result
				// with opposite sign from the operands means that signed overflow happened
				flags |= FLAG_VF;
			}
			else {
				flags &= ~FLAG_VF;
			}
			
			update_zsp = true;
			break;
		
		case OP_CMP: // Compare (same as SUB but Rd is ZERO)
			// CMP is pointless without setting flags
			write_flags = !insn->toggle_flags;
			//FALLTHROUGH
		
		case OP_SUB: // Subtract
			// Negate Ry and then treat like ADD
			vys = -vys;
			memcpy(&vyu, &vys, sizeof(vyu));
			goto handle_ADD;
		
		case OP_MLU: // Multiply unsigned
			// Zero-extend each operand from u16 to u32, then multiply
			ubig = (uint32_t)vxu * (uint32_t)vyu;
			
			// Split result value into high part and low part
			vd = (EAR_UWord)(ubig & EAR_UWORD_MAX);
			write_rd = true;
			vdx = (EAR_UWord)(ubig >> EAR_REGISTER_BITS);
			write_rdx = true;
			
			// Update CF if the result doesn't fit into 16 bits
			if(vdx != 0) {
				flags |= FLAG_CF;
			}
			else {
				flags &= ~FLAG_CF;
			}
			
			// Does not update VF
			update_zsp = true;
			use_rdx_for_flags = true;
			break;
		
		case OP_MLS: // Multiply signed
			// Sign-extend each operand from u16 to s32, then multiply
			sbig = (int32_t)vxs * (int32_t)vys;
			
			// Prevent UB via signed-overflow in the cast by instead using memcpy
			// (will certainly be optimized away)
			memcpy(&ubig, &sbig, sizeof(ubig));
			
			// Split result value into high part and low part
			vd = (EAR_UWord)(ubig & EAR_UWORD_MAX);
			write_rd = true;
			vdx = (EAR_UWord)(ubig >> EAR_REGISTER_BITS);
			write_rdx = true;
			
			// Update CF if the result doesn't fit into 16 bits
			if(vdx != 0 && vdx != EAR_UWORD_MAX) {
				flags |= FLAG_CF;
			}
			else {
				flags &= ~FLAG_CF;
			}
			
			// Does not update VF
			update_zsp = true;
			use_rdx_for_flags = true;
			break;
		
		case OP_DVU: // Divide/modulo unsigned
			// Check for illegal division
			if(vyu == 0) {
				return EAR_raiseException(ear, EXC_ARITHMETIC, 0);
			}
			
			// Compute Rx / Ry
			vd = vxu / vyu;
			write_rd = true;
			
			// Compute Rx % Ry
			vdx = vxu % vyu;
			write_rdx = true;
			
			// Does not update CF or VF
			update_zsp = true;
			break;
		
		case OP_DVS: // Divide/modulo signed
			// Check for illegal division
			if(vys == 0 || (vxs == INT16_MIN && vys == -1)) {
				return EAR_raiseException(ear, EXC_ARITHMETIC, vyu);
			}
			
			// Compute Rx / Ry
			stmp = vxs / vys;
			memcpy(&vd, &stmp, sizeof(vd));
			write_rd = true;
			
			// Compute Rx % Ry
			stmp = vxs % vys;
			memcpy(&vdx, &stmp, sizeof(vdx));
			write_rdx = true;
			
			// Does not update CF or VF
			update_zsp = true;
			break;
		
		case OP_XOR: // Bitwise xor
			vd = vxu ^ vyu;
			write_rd = true;
			
			// Does not update CF or VF
			update_zsp = true;
			break;
		
		case OP_AND: // Bitwise and
			vd = vxu & vyu;
			write_rd = true;
			
			// Does not update CF or VF
			update_zsp = true;
			break;
		
		case OP_ORR: // Bitwise or
			vd = vxu | vyu;
			write_rd = true;
			
			// Does not update CF or VF
			update_zsp = true;
			break;
		
		case OP_SHL: // Shift left
			// Avoid UB from shifting by more than the number of bits in the value
			if(vyu < EAR_REGISTER_BITS) {
				vd = vxu << vyu;
			}
			else {
				vd = 0;
			}
			write_rd = true;
			
			// Update CF
			if(vyu == 0) {
				// Shifting by zero means no carry
				flags &= ~FLAG_CF;
			}
			else if(vyu <= EAR_REGISTER_BITS) {
				// Check the bit that was last shifted out
				if(vxu & (1 << (EAR_REGISTER_BITS - vyu))) {
					flags |= FLAG_CF;
				}
				else {
					flags &= ~FLAG_CF;
				}
			}
			
			// Does not update VF
			update_zsp = true;
			break;
		
		case OP_SRU: // Shift right unsigned
			// Avoid UB from shifting by more than the number of bits in the value
			if(vyu < EAR_REGISTER_BITS) {
				vd = vxu >> vyu;
			}
			else {
				vd = 0;
			}
			write_rd = true;
			
			// Update CF
			if(vyu == 0) {
				// Shifting by zero means no carry
				flags &= ~FLAG_CF;
			}
			else if(vyu <= EAR_REGISTER_BITS) {
				if(vxu & (1 << (vyu - 1))) {
					flags |= FLAG_CF;
				}
				else {
					flags &= ~FLAG_CF;
				}
			}
			
			// Does not update VF
			update_zsp = true;
			break;
		
		case OP_SRS: // Shift right signed
			// Avoid UB from shifting by more than the number of bits in the value
			if(vyu < EAR_REGISTER_BITS) {
				vd = vxs >> vyu;
			}
			else {
				// Shifting by >= 16 bits means just use the sign bit
				vd = vxs < 0 ? -1 : 0;
			}
			write_rd = true;
			
			// Update CF
			bool new_cf = false;
			if(vyu == 0) {
				// Shifting by zero means no carry
				new_cf = false;
			}
			else if(vyu >= EAR_REGISTER_BITS) {
				// Shifted enough that the last bit shifted out will be the sign bit
				new_cf = vxs < 0;
			}
			else {
				new_cf = !!(vxs & (1 << (vyu - 1)));
			}
			
			// Apply newly computed value of CF
			if(new_cf) {
				flags |= FLAG_CF;
			}
			else {
				flags &= ~FLAG_CF;
			}
			
			// Does not update VF
			update_zsp = true;
			break;
		
		case OP_MOV: // Move/assign
			vd = vyu;
			write_rd = true;
			update_zsp = true;
			break;
		
		case OP_RDC: // Read control register
			// Check if the control register is readable
			if(ctx->cr[CR_CREG_DENY_R] & (1 << ry)) {
				// Control register is read-protected
				return EAR_raiseException(ear, EXC_DENIED_CREG, ry);
			}
			
			// Read from control register
			vd = ry_ctx->cr[ry];
			rd_ctx = rx_ctx;
			write_rd = true;
			update_zsp = true;
			break;
		
		case OP_WRC: // Write control register
			// Check if the control register is writable
			if(ctx->cr[CR_CREG_DENY_W] & (1 << rx)) {
				// Control register is write-protected
				return EAR_raiseException(ear, EXC_DENIED_CREG, rx);
			}
			
			// Write to control register
			rx_ctx->cr[rx] = ry_ctx->r[ry];
			break;
		
		case OP_LDW: // Read word from memory
			addr = (EAR_UWord)((rd_ctx->r[rd] + vyu) & EAR_UWORD_MAX);
			ret = EAR_readWord(ear->mem_fn, ear->mem_cookie, addr, &vd);
			if(EAR_FAILED(ret)) {
				return EAR_raiseException(
					ear, EXC_FAULT_MAKE(ret, EAR_PROT_READ), addr
				);
			}
			else if(ret != HALT_NONE) {
				goto out;
			}
			// Hack to allow LDW to write to Rx instead of Rd
			rd = rx;
			rd_ctx = rx_ctx;
			write_rd = true;
			update_zsp = true;
			break;
		
		case OP_STW: // Write word to memory
			addr = (EAR_UWord)((rd_ctx->r[rd] + vyu) & EAR_UWORD_MAX);
			ret = EAR_writeWord(ear->mem_fn, ear->mem_cookie, addr, vxu);
			if(EAR_FAILED(ret)) {
				return EAR_raiseException(
					ear, EXC_FAULT_MAKE(ret, EAR_PROT_WRITE), addr
				);
			}
			else if(ret != HALT_NONE) {
				goto out;
			}
			break;
		
		case OP_LDB: // Read byte from memory
			addr = (EAR_UWord)((rd_ctx->r[rd] + vyu) & EAR_UWORD_MAX);
			ret = EAR_readByte(ear->mem_fn, ear->mem_cookie, addr, &btmp);
			if(EAR_FAILED(ret)) {
				return EAR_raiseException(
					ear, EXC_FAULT_MAKE(ret, EAR_PROT_READ), addr
				);
			}
			else if(ret != HALT_NONE) {
				goto out;
			}
			// Hack to allow LDW to write to Rx instead of Rd
			rd = rx;
			rd_ctx = rx_ctx;
			vd = btmp;
			write_rd = true;
			update_zsp = true;
			break;
		
		case OP_STB: // Write byte to memory
			addr = (EAR_UWord)((rd_ctx->r[rd] + vyu) & EAR_UWORD_MAX);
			ret = EAR_writeByte(ear->mem_fn, ear->mem_cookie, addr, (EAR_Byte)vxu);
			if(EAR_FAILED(ret)) {
				return EAR_raiseException(
					ear, EXC_FAULT_MAKE(ret, EAR_PROT_WRITE), addr
				);
			}
			else if(ret != HALT_NONE) {
				goto out;
			}
			break;
		
		// BRA, BRR, FCA, FCR all disallow XR & XW prefixes, just use active context
		
		case OP_BRA: // Absolute jump
			ctx->r[DPC] = vxu;
			ctx->r[PC] = vyu;
			break;
		
		case OP_BRR: // Relative jump
			ctx->r[PC] += insn->imm;
			break;
		
		case OP_FCA: // Absolute call
			ctx->r[RD] = ctx->r[DPC];
			ctx->r[RA] = ctx->r[PC];
			ctx->r[DPC] = vxu;
			ctx->r[PC] = vyu;
			break;
		
		case OP_FCR: // Relative call
			ctx->r[RD] = ctx->r[DPC];
			ctx->r[RA] = ctx->r[PC];
			ctx->r[PC] += insn->imm;
			break;
		
		case OP_RDB: // Read byte from port
			// Invoke port read callback
			if(!ear->read_fn) {
				flags |= FLAG_CF;
				break;
			}
			
			ret = ear->read_fn(ear->port_cookie, insn->port_number, &btmp);
			if(ret != HALT_NONE) {
				if(!EAR_FAILED(ret)) {
					// Don't change flags or set `EXC_INFO`, just return the halt reason
					return ret;
				}
				
				// Set the CF flag whenever reading from a port fails
				flags |= FLAG_CF;
			}
			else {
				// The read succeeded, so clear CF
				flags &= ~FLAG_CF;
				
				// The byte value that was read will be stored in the destination register
				vd = btmp;
				write_rd = true;
				update_zsp = true;
			}
			
			// Keep going after an I/O error
			if(ret == HALT_IO_ERROR) {
				ret = HALT_NONE;
			}
			break;
		
		case OP_WRB: // Write byte to port
			// Invoke port write callback
			if(!ear->write_fn) {
				flags |= FLAG_CF;
				break;
			}
			
			ret = ear->write_fn(ear->port_cookie, insn->port_number, (EAR_Byte)vyu);
			if(ret != HALT_NONE) {
				if(!EAR_FAILED(ret)) {
					// Don't change flags or set `EXC_INFO`, just return the halt reason
					return ret;
				}
				
				// Set the CF flag whenever writing to a port fails
				flags |= FLAG_CF;
			}
			else {
				// The write succeeded, so clear CF
				flags &= ~FLAG_CF;
			}
			
			// Keep going after an I/O error
			if(ret == HALT_IO_ERROR) {
				ret = HALT_NONE;
			}
			break;
		
		case OP_PSH: // Push registers
		{
			EAR_UWord* regs = ry_ctx->r;
			EAR_UWord* pshpop_addr = &ctx->cr[CR_EXEC_STATE_0];
			EAR_UWord* pshpop_regs = &ctx->cr[CR_EXEC_STATE_1];
			EAR_VirtAddr addr = rd_ctx->r[rd];
			uint16_t regs16 = insn->imm;
			
			if(!resume) {
				// Copy starting value of stack pointer (and Regs16) into cregs
				*pshpop_addr = addr;
				*pshpop_regs = regs16;
			}
			
			// Push in reverse order so that the registers are ordered properly
			// in memory (R0 at lowest address)
			while(regs16) {
				// Get index of highest bit set
				int i = 31 - __builtin_clz(regs16);
				ASSERT(i < 16);
				ASSERT(regs16 & (1 << i));
				
				// Advance to next register location, wrapping on overflow
				addr -= sizeof(EAR_UWord);
				
				// Push this register to the destination memory location
				ret = EAR_writeWord(ear->mem_fn, ear->mem_cookie, addr, regs[i]);
				if(EAR_FAILED(ret)) {
					return EAR_raiseException(
						ear, EXC_FAULT_MAKE(ret, EAR_PROT_WRITE), addr
					);
				}
				else if(ret != HALT_NONE) {
					goto out;
				}
				
				// Clear bit, as this register was just pushed
				regs16 &= ~(1 << i);
				
				// Update saved state after each operation
				*pshpop_addr = addr;
				*pshpop_regs = regs16;
			}
			
			// Write back stack pointer register
			if(rd != ZERO) {
				rd_ctx->r[rd] = *pshpop_addr;
			}
			break;
		}
		
		case OP_POP: // Pop registers
		{
			EAR_UWord* regs = ry_ctx->r;
			EAR_UWord* pshpop_addr = &ctx->cr[CR_EXEC_STATE_0];
			EAR_UWord* pshpop_regs = &ctx->cr[CR_EXEC_STATE_1];
			EAR_VirtAddr addr = rd_ctx->r[rd];
			uint16_t regs16 = insn->imm;
			
			if(!resume) {
				// Copy starting value of stack pointer (and Regs16) into creg
				*pshpop_addr = addr;
				*pshpop_regs = regs16;
			}
			
			// Pop in forward order so that the registers are ordered properly
			// in memory (R0 at lowest address)
			while(regs16) {
				// Get index of lowest bit set
				int i = __builtin_ctz(regs16);
				ASSERT(i < 16);
				ASSERT(regs16 & (1 << i));
				
				// Pop this register from the source memory location
				ret = EAR_readWord(ear->mem_fn, ear->mem_cookie, addr, &vd);
				if(EAR_FAILED(ret)) {
					return EAR_raiseException(
						ear, EXC_FAULT_MAKE(ret, EAR_PROT_READ), addr
					);
				}
				else if(ret != HALT_NONE) {
					goto out;
				}
				
				// Don't overwrite ZERO
				if(i != ZERO) {
					regs[i] = vd;
				}
				
				// Clear bit, as this register was just popped
				regs16 &= ~(1 << i);
				
				// Advance to next register location, wrapping on overflow
				addr += sizeof(EAR_UWord);
				
				// Update saved state after each operation
				*pshpop_addr = addr;
				*pshpop_regs = regs16;
			}
			
			// Write back stack pointer register, unless
			// this stack register was just popped!
			if(rd != ZERO && !(insn->imm & (1 << rd))) {
				rd_ctx->r[rd] = *pshpop_addr;
			}
			break;
		}
		
		case OP_HLT: // High-Level Transfer
			return EAR_raiseException(ear, EXC_NONE, 0);
		
		case OP_BPT: // Breakpoint (acts as NOP unless debugger hooks it)
		case OP_NOP: // No-op
			break;
		
		default: // Illegal instruction
			// Should be caught during instruction decoding
			abort();
	}
	
out:
	// Does the CPU need to halt?
	if(ret != HALT_NONE) {
		return ret;
	}
	
	// Write back Rd
	if(write_rd && rd != ZERO) {
		rd_ctx->r[rd] = vd;
	}
	
	// Write back Rdx
	if(write_rdx && rdx != ZERO) {
		rd_ctx->r[rdx] = vdx;
	}
	
	if(write_flags) {
		// Should ZF, SF, and PF flags be updated from the value of vd?
		if(update_zsp) {
			uint32_t alu_result = vd;
			bool is_signed;
			if(use_rdx_for_flags && rdx != ZERO) {
				alu_result |= (uint32_t)vdx << EAR_REGISTER_BITS;
				is_signed = !!(alu_result >> 31);
			}
			else {
				is_signed = !!(alu_result & EAR_SIGN_BIT);
			}
			
			// Recompute ZF
			if(alu_result == 0) {
				flags |= FLAG_ZF;
			}
			else {
				flags &= ~FLAG_ZF;
			}
			
			// Recompute SF
			if(is_signed) {
				flags |= FLAG_SF;
			}
			else {
				flags &= ~FLAG_SF;
			}
			
			// Recompute PF
			uint32_t parity = alu_result;
			parity ^= parity >> 16;
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
		}
		
		// Write back FLAGS register
		ctx->cr[CR_FLAGS] = flags;
	}
	
	return HALT_NONE;
}

/*! Executes a single instruction
 * @return Reason for halting, typically HALT_NONE
 */
EAR_HaltReason EAR_stepInstruction(EAR* ear) {
	EAR_HaltReason ret;
	EAR_ThreadState* ctx = CTX(*ear);
	bool cond = false;
	
	// Are both threads in an exception state?
	if(ctx->cr[CR_EXC_INFO] & 1) {
		return HALT_DOUBLE_FAULT;
	}
	
	// Check if the program tried to return from the topmost stack frame
	if(ctx->r[PC] == EAR_CALL_RA && ctx->r[DPC] == EAR_CALL_RD) {
		return HALT_RETURN;
	}
	
	// Save initial timer value to know if the timer was just set in this cycle
	EAR_UWord* timer = &ctx->cr[CR_TIMER];
	EAR_UWord timer_initial = *timer;
	
	EAR_FullAddr pc;
	EAR_UWord dpc = ctx->r[DPC];
	
	// Resuming execution of an interrupted instruction?
	if(ctx->cr[CR_FLAGS] & FLAG_RESUME) {
		pc = ctx->cr[CR_INSN_ADDR];
		cond = true;
	}
	else {
		// Read PC value
		pc = ctx->cr[CR_INSN_ADDR] = ctx->r[PC];
		
		// Fetch instruction from PC
		EAR_ExceptionInfo tmp_exc_info = 0;
		EAR_UWord tmp_exc_addr = 0;
		ret = EAR_fetchInstruction(
			ear->mem_fn, ear->mem_cookie,
			&pc, EAR_VIRTUAL_ADDRESS_SPACE_SIZE - 1, dpc,
			ear->verbose, &ctx->insn, &tmp_exc_info, &tmp_exc_addr
		);
		if(ret != HALT_NONE) {
			if(tmp_exc_info) {
				ret = EAR_raiseException(ear, tmp_exc_info, tmp_exc_addr);
			}
			else if(ret == HALT_DECODE) {
				ret = EAR_raiseException(ear, EXC_DECODE, tmp_exc_addr);
			}
			goto post_exec;
		}
		
		// Update PC to point to the next instruction before executing the current one
		ctx->r[PC] = (EAR_UWord)(pc & EAR_UWORD_MAX);
		
		cond = EAR_evaluateCondition(ear, ctx->insn.cond);
		
		// Execute the pre-exec hook, if installed
		if(ear->exec_fn) {
			ret = ear->exec_fn(ear->exec_cookie, &ctx->insn, pc, /*before=*/true, cond);
			if(ret != HALT_NONE) {
				goto post_exec;
			}
		}
	}
	
	// Conditionally execute instruction
	if(cond) {
		ret = EAR_executeInstruction(ear, &ctx->insn);
	}
	
post_exec:
	// Exec hook function decided to handle the instruction
	if(ret == HALT_COMPLETE) {
		ret = HALT_NONE;
	}
	
	// Restore old PC if execution failed
	if(EAR_FAILED(ret)) {
		ctx->r[PC] = ctx->cr[CR_INSN_ADDR];
		return ret;
	}
	
	// An instruction executed, so invoke the post-exec hook
	if(ear->exec_fn) {
		EAR_HaltReason ret2 = ear->exec_fn(ear->exec_cookie, &ctx->insn, pc, /*before=*/false, cond);
		if(EAR_FAILED(ret2)) {
			return ret2;
		}
		else if(ret == HALT_NONE) {
			ret = ret2;
		}
	}
	
	// Return if there's a (non-failing) halt reason
	if(ret != HALT_NONE) {
		return ret;
	}
	
	// Increment instruction counts
	if(!++ctx->cr[CR_INSN_COUNT_LO]) {
		++ctx->cr[CR_INSN_COUNT_HI];
	}
	++ear->ins_count;
	
	// Handle timer only if the timer wasn't just set in this cycle
	if(timer_initial && *timer == timer_initial) {
		if(!--*timer) {
			// Will be handled in the next cycle
			return EAR_raiseException(ear, EXC_TIMER, 0);
		}
	}
	
	// Check if the program tried to return from the topmost stack frame
	if(ctx->r[PC] == EAR_CALL_RA && ctx->r[DPC] == EAR_CALL_RD) {
		ret = HALT_RETURN;
	}
	
	return ret;
}

/*! Begins execution from the current state.
 * @return Reason for halting, never HALT_NONE
 */
EAR_HaltReason EAR_continue(EAR* ear) {
	EAR_HaltReason reason;
	
	do {
		reason = EAR_stepInstruction(ear);
		
		// Allow exceptions to be handled normally
		if(reason == HALT_EXCEPTION) {
			reason = HALT_NONE;
		}
	} while(reason == HALT_NONE);
	
	return reason;
}

/*! Invokes a function at a given virtual address and passing up to 6 arguments.
 * @note This will overwrite the values of registers A0-A5, PC, DPC, RA, and RD.
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
	EAR_VirtAddr func_vmaddr,
	EAR_UWord func_dpc,
	EAR_UWord arg1,
	EAR_UWord arg2,
	EAR_UWord arg3,
	EAR_UWord arg4,
	EAR_UWord arg5,
	EAR_UWord arg6,
	bool run
) {
	EAR_UWord* r = CTX(*ear)->r;
	
	// Set function call context
	r[A0] = arg1;
	r[A1] = arg2;
	r[A2] = arg3;
	r[A3] = arg4;
	r[A4] = arg5;
	r[A5] = arg6;
	r[RA] = EAR_CALL_RA;
	r[RD] = EAR_CALL_RD;
	r[PC] = func_vmaddr;
	r[DPC] = func_dpc;
	
	if(!run) {
		return HALT_NONE;
	}
	
	// Run until the current function returns or the CPU is halted for some other reason
	return EAR_continue(ear);
}

const char* EAR_haltReasonToString(EAR_HaltReason status) {
	switch(status) {
		case HALT_UNALIGNED:
			return "Tried to access a word at an unaligned (odd) memory address";
		case HALT_MMU_FAULT:
			return "Accessed unmapped memory";
		case HALT_BUS_FAULT:
			return "Accessed unmapped physical memory";
		case HALT_BUS_PROTECTED:
			return "Protection violation";
		case HALT_BUS_ERROR:
			return "Bus peripheral error";
		case HALT_DECODE:
			return "Encountered an illegal instruction";
		case HALT_DOUBLE_FAULT:
			return "Kernel panic";
		case HALT_NONE:
			return "No unusual halt reason";
		case HALT_EXCEPTION:
			return "An exception was raised";
		case HALT_BREAKPOINT:
			return "A breakpoint was hit";
		case HALT_DEBUGGER:
			return "Halted by the debugger";
		case HALT_RETURN:
			return "Program tried to return from the topmost stack frame";
		case HALT_COMPLETE:
			return "For internal use only, used to support fault handlers and callbacks";
		default:
			return "Unknown halt reason";
	}
}

/*! Get a textual description of the kind of exception from exc_info */
const char* EAR_exceptionKindToString(EAR_ExceptionInfo ei) {
	switch(ei & 0xF) {
		case EXC_NONE:
			return "No exception";
		case EXC_UNALIGNED:
			return "Unaligned memory access fault";
		case EXC_MMU:
			switch(EXC_FAULT_PROT(ei)) {
				case EAR_PROT_READ:
					return "Memory read fault";
				case EAR_PROT_WRITE:
					return "Memory write fault";
				case EAR_PROT_EXECUTE:
					return "Memory execute fault";
				default:
					return "Unknown memory fault";
			}
		case EXC_BUS:
			switch(EXC_FAULT_PROT(ei)) {
				case EAR_PROT_READ:
				case EAR_PROT_EXECUTE:
					return "Bus read fault";
				case EAR_PROT_WRITE:
					return "Bus write fault";
				default:
					return "Unknown bus fault";
			}
		case EXC_DECODE:
			return "Illegal instruction";
		case EXC_ARITHMETIC:
			return "Arithmetic exception";
		case EXC_DENIED_CREG:
			return "Accessed a denied control register";
		case EXC_DENIED_INSN:
			return "Executed a denied instruction";
		case EXC_TIMER:
			return "Timer expired";
	}
	
	ASSERT(false);
}

const char* EAR_getMnemonic(EAR_Opcode op) {
	static const char* opcodes[] = {
		"ADD", "SUB", "MLU", "MLS", "DVU", "DVS", "XOR", "AND",
		"ORR", "SHL", "SRU", "SRS", "MOV", "CMP", "RDC", "WRC",
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
		"ZERO", "A0", "A1", "A2", "A3", "A4", "A5", "S0",
		"S1", "S2", "FP", "SP", "RA", "RD", "PC", "DPC"
	};
	return reg < ARRAY_COUNT(regnames) ? regnames[reg] : NULL;
}

const char* EAR_getControlRegisterName(EAR_ControlRegister cr) {
	static const char* crnames[] = {
		"CREG_DENY_R",
		"CREG_DENY_W",
		"INSN_DENY_0",
		"INSN_DENY_1",
		"INSN_COUNT_LO",
		"INSN_COUNT_HI",
		"EXEC_STATE_0",
		"EXEC_STATE_1",
		"MEMBASE_R",
		"MEMBASE_W",
		"MEMBASE_X",
		"EXC_INFO",
		"EXC_ADDR",
		"TIMER",
		"INSN_ADDR",
		"FLAGS"
	};
	return cr < ARRAY_COUNT(crnames) ? crnames[cr] : NULL;
}
