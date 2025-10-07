#ifndef EAR_MMU_H
#define EAR_MMU_H

#include "types.h"

struct MMU {
	EAR_Context* ctx;            //!< CPU context for accessing MEMBASE_* control registers
	Bus_AccessHandler* bus_fn;   //!< Function pointer called for physical memory accesses
	void* bus_cookie;            //!< Opaque cookie value passed to bus_fn
};


/*! Initialize the MMU with defaults */
void MMU_init(MMU* mmu);

/*! Give the MMU access to the CPU's context */
void MMU_setContext(MMU* mmu, EAR_Context* ctx);

/*! Connect the MMU to the physical memory bus */
void MMU_setBusHandler(MMU* mmu, Bus_AccessHandler* bus_fn, void* bus_cookie);


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
);

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
);

#endif /* EAR_MMU_H */
