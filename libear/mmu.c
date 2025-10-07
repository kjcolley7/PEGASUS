#include "mmu.h"
#include <string.h>
#include "common/macros.h"


/*! Initialize the MMU, telling it how to talk to the physical memory bus */
void MMU_init(MMU* mmu) {
	memset(mmu, 0, sizeof(*mmu));
}

/*! Give the MMU access to the CPU's thread state */
void MMU_setContext(MMU* mmu, EAR_Context* ctx) {
	mmu->ctx = ctx;
}

/*! Connect the MMU to the physical memory bus */
void MMU_setBusHandler(MMU* mmu, Bus_AccessHandler* bus_fn, void* bus_cookie) {
	mmu->bus_fn = bus_fn;
	mmu->bus_cookie = bus_cookie;
}

static inline uint16_t get_membase(EAR_ThreadState* ctx, EAR_Protection prot) {
	EAR_ControlRegister cr;
	
	switch(prot) {
		case EAR_PROT_READ:
			cr = CR_MEMBASE_R;
			break;
		case EAR_PROT_WRITE:
			cr = CR_MEMBASE_W;
			break;
		case EAR_PROT_EXECUTE:
			cr = CR_MEMBASE_X;
			break;
		default:
			ASSERT(false);
	}
	
	return ctx->cr[cr];
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
EAR_HaltReason MMU_translate(
	MMU* mmu, EAR_VirtAddr vmaddr, EAR_Protection prot, EAR_PhysAddr* out_paddr
) {
	EAR_HaltReason r = HALT_NONE;
	uint16_t membase = get_membase(&mmu->ctx->banks[mmu->ctx->active], prot);
	bool mmu_enabled = !!(membase & MMU_ENABLED);
	if(!mmu_enabled) {
		*out_paddr = ((EAR_PhysAddr)(membase >> MEMBASE_REGION_SHIFT) << EAR_REGION_SHIFT) | vmaddr;
		return HALT_NONE;
	}
	
	EAR_PhysAddr pte_addr;
	pte_addr = (EAR_PhysAddr)(membase & ~MMU_ENABLED) << EAR_PAGE_SHIFT;
	pte_addr += EAR_PAGE_NUMBER(vmaddr) * sizeof(MMU_PTE);
	pte_addr &= EAR_PHYSICAL_ADDRESS_SPACE_SIZE - 1;
	
	MMU_PTE pte = 0;
	if(!mmu->bus_fn(mmu->bus_cookie, BUS_MODE_READ, pte_addr, /*is_byte=*/false, &pte, &r)) {
		return r;
	}
	
	// Compute output physical address
	*out_paddr = ((EAR_PhysAddr)pte << EAR_PAGE_SHIFT) | EAR_PAGE_OFFSET(vmaddr);
	if(MMU_PTE_INVALID(pte)) {
		// The 0xFF region is used to indicate an invalid PTE
		return HALT_MMU_FAULT;
	}
	
	return HALT_NONE;
}

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
bool MMU_memoryHandler(
	void* cookie, EAR_Protection prot, Bus_AccessMode mode,
	EAR_FullAddr vmaddr, bool is_byte, void* data, EAR_HaltReason* out_r
) {
	ASSERT(prot == EAR_PROT_READ || prot == EAR_PROT_WRITE || prot == EAR_PROT_EXECUTE);
	ASSERT(is_byte || !(vmaddr & 1));
	ASSERT(vmaddr < EAR_VIRTUAL_ADDRESS_SPACE_SIZE);
	MMU* mmu = cookie;
	
	// Perform MMU lookup to convert from virtual to physical address
	EAR_PhysAddr paddr;
	EAR_HaltReason r = MMU_translate(mmu, vmaddr, prot, &paddr);
	if(r != HALT_NONE) {
		if(out_r) {
			*out_r = r;
		}
		return false;
	}
	
	// Perform physical memory access on the bus
	return mmu->bus_fn(mmu->bus_cookie, mode, paddr, is_byte, data, out_r);
}
