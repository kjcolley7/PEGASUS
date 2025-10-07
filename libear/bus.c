#include "bus.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "common/macros.h"

/*!
 * @brief Initialize an EAR physical memory bus with default values.
 */
void Bus_init(Bus* bus) {
	bus->hook_fn = NULL;
	bus->hook_cookie = NULL;
	array_init(&bus->devices);
}

static Bus_Device* Bus_addChildDevice(
	Bus_DeviceArray* devices,
	const char* name,
	Bus_AccessHandler* handler_fn, void* handler_cookie,
	Bus_Addr prefix_pattern, unsigned prefix_bitcount
) {
	ASSERT(prefix_bitcount > 0);
	
	// Word-level mapping is the most precise, don't allow byte-level mappings
	ASSERT(prefix_bitcount <= BUS_ADDRESS_BITS - 1);
	
	// Look for an existing device that overlaps with this one, either more or less specific
	size_t insert_idx = 0;
	Bus_Device* new_dev = NULL;
	
	// This could be faster as a binary search, but it's not expected that adding devices
	// will be part of a hot loop. Just do a simple linear search to save dev time. Adding
	// devices therefore has a runtime complexity of O(n^2).
	foreach(devices, dev) {
		unsigned minbits = MIN(prefix_bitcount, dev->prefix_bitcount);
		Bus_Addr mask = ~((1 << (BUS_ADDRESS_BITS - minbits)) - 1);
		
		// Overlap?
		if((dev->prefix_pattern & mask) == (prefix_pattern & mask)) {
			// This device overlaps an existing one, so check which one is more specific
			if(dev->prefix_bitcount <= prefix_bitcount) {
				// New device is more specific, so it should become a child of the existing one
				return Bus_addChildDevice(
					&dev->children,
					name,
					handler_fn, handler_cookie,
					prefix_pattern, prefix_bitcount
				);
			}
			
			// Existing device is more specific, so it should become a child of this new one
			Bus_Device child;
			memcpy(&child, dev, sizeof(child));
			new_dev = dev;
			array_init(&new_dev->children);
			array_append(&new_dev->children, child);
			break;
		}
		
		// New device should be inserted before this one?
		if(prefix_pattern < dev->prefix_pattern) {
			break;
		}
		
		++insert_idx;
	}
	
	if(!new_dev) {
		Bus_Device tmp;
		array_init(&tmp.children);
		new_dev = array_insert(devices, insert_idx, &tmp);
	}
	
	// Initialize the new device struct
	new_dev->name = name;
	new_dev->handler_fn = handler_fn;
	new_dev->handler_cookie = handler_cookie;
	new_dev->prefix_pattern = prefix_pattern;
	new_dev->prefix_bitcount = prefix_bitcount;
	new_dev->allowed_modes = BUS_MODE_RDWR;
	return new_dev;
}

/*!
 * @brief Attach a device to the physical memory bus.
 * 
 * @param name Name of the device, used for debugging
 * @param handler_fn Function pointer to the handler function for this device
 * @param handler_cookie Opaque value passed to the handler function
 * @param prefix_pattern Physical address prefix pattern for this device
 * @param prefix_bitcount Number of significant bits in the prefix pattern
 */
void Bus_addDevice(
	Bus* bus,
	const char* name,
	Bus_AccessHandler* handler_fn, void* handler_cookie,
	Bus_Addr prefix_pattern, unsigned prefix_bitcount
) {
	Bus_addChildDevice(
		&bus->devices,
		name,
		handler_fn, handler_cookie,
		prefix_pattern, prefix_bitcount
	);
}

struct Bus_memcookie {
	EAR_UWord* data;
	Bus_Addr start_addr;
	Bus_Addr end_addr;
};

static bool Bus_memoryHandler(
	void* cookie, Bus_AccessMode mode,
	Bus_Addr addr, bool is_byte, void* data,
	EAR_HaltReason* out_r
) {
	ASSERT(mode == BUS_MODE_READ || mode == BUS_MODE_WRITE);
	struct Bus_memcookie* mem = cookie;
	
	// Access before or after mapped data?
	if(addr < mem->start_addr || addr >= mem->end_addr) {
		if(out_r) {
			*out_r = HALT_BUS_FAULT;
		}
		return false;
	}
	
	// Word access would go one byte past the end of the mapping?
	if(!is_byte && addr == mem->end_addr - 1) {
		if(out_r) {
			*out_r = HALT_BUS_FAULT;
		}
		return false;
	}
	
	uint32_t offset = addr - mem->start_addr;
	
	// Handle endianness for byte accesses
#if EAR_BYTE_ORDER == EAR_LITTLE_ENDIAN
	unsigned shift = 8 * (offset & 1);
#else /* big endian */
	unsigned shift = 8 * (1 - (offset & 1));
#endif /* endianness */
	
	EAR_UWord* p = &mem->data[offset >> 1];
	if(mode == BUS_MODE_READ) {
		if(!is_byte) {
			memcpy(data, p, sizeof(*p));
			return true;
		}
		
		*(EAR_Byte*)data = (EAR_Byte)((*p >> shift) & 0xFF);
	}
	else {
		ASSERT(mode == BUS_MODE_WRITE);
		if(!is_byte) {
			memcpy(p, data, sizeof(*p));
			return true;
		}
		
		// Read word, modify one byte, write back
		EAR_UWord w = *p;
		EAR_Byte b = *(EAR_Byte*)data;
		w &= ~(0xFF << shift);
		w |= (EAR_UWord)b << shift;
		*p = w;
	}
	
	return true;
}

/*!
 * @brief Attach a blob of physical memory to the bus. The caller is responsible for
 * managing the lifetime of the memory.
 * 
 * @param name Name of the memory region, used for debugging
 * @param modes One of BUS_READ, BUS_WRITE, or BUS_RDWR, for the allowed access modes
 * @param start Starting physical memory address
 * @param size Size of memory blob, in bytes
 * @param data Pointer to beginning of the memory to attach, must be word-aligned
 */
void Bus_addMemory(
	Bus* bus, const char* name, Bus_AccessMode modes,
	Bus_Addr start, uint32_t size, EAR_UWord* data
) {
	ASSERT(!(modes & ~BUS_MODE_RDWR));
	ASSERT(start < EAR_PHYSICAL_ADDRESS_SPACE_SIZE);
	ASSERT(size <= EAR_PHYSICAL_ADDRESS_SPACE_SIZE - start);
	ASSERT(!(start & 1));
	
	struct Bus_memcookie* mem = malloc(sizeof(*mem));
	if(!mem) {
		abort();
	}
	mem->data = data;
	mem->start_addr = start;
	mem->end_addr = start + size;
	
	// Count number of significant bits that are unchanged between start and end addresses.
	Bus_Addr x = start ^ (mem->end_addr - 1);
	unsigned prefix_bitcount = __builtin_clz(x) - (BITCOUNT(x) - BUS_ADDRESS_BITS);
	
	// Start address must be naturally aligned
	Bus_Addr offset_mask = (1 << (BUS_ADDRESS_BITS - prefix_bitcount)) - 1;
	ASSERT(!(start & offset_mask));
	
	Bus_Device* dev = Bus_addChildDevice(
		&bus->devices,
		name,
		Bus_memoryHandler, mem,
		start, prefix_bitcount
	);
	dev->allowed_modes = modes;
}

/*!
 * @brief Add a callback function to be called for all memory accesses on the bus.
 * This function may choose to override the access by returning HALT_COMPLETE for success.
 * Alternatively, it may return HALT_NONE to allow the access to proceed normally.
 * 
 * @param hook Function pointer to the hook function
 * @param cookie Opaque value passed to the hook function
 */
void Bus_setHook(Bus* bus, Bus_Hook* hook, void* cookie) {
	bus->hook_fn = hook;
	bus->hook_cookie = cookie;
}

static bool Bus_deviceAccessSelf(
	Bus_Device* dev,
	Bus_AccessMode mode,
	Bus_Addr addr, bool is_byte, void* data,
	EAR_HaltReason* out_r
) {
	// Check for access violation
	if(!(dev->allowed_modes & mode)) {
		if(out_r) {
			*out_r = HALT_BUS_PROTECTED;
		}
		return false;
	}
	
	// Call the handler function for this device
	return dev->handler_fn(dev->handler_cookie, mode, addr, is_byte, data, out_r);
}

// Forward-declaration
static bool Bus_zoneAccess(
	Bus_DeviceArray* devices,
	Bus_AccessMode mode,
	Bus_Addr addr, bool is_byte, void* data,
	EAR_HaltReason* out_r
);

static bool Bus_deviceAccess(
	Bus_Device* dev,
	Bus_AccessMode mode,
	Bus_Addr addr, bool is_byte, void* data,
	EAR_HaltReason* out_r
) {
	// Physical memory operations conceptually happen in parallel on all devices,
	// so an error in one shouldn't stop the other from receiving the access.
	EAR_HaltReason r1 = HALT_NONE;
	bool ret1 = Bus_deviceAccessSelf(dev, mode, addr, is_byte, data, &r1);
	
	// No child peripherals? Easy!
	if(array_empty(&dev->children)) {
		if(out_r) {
			*out_r = r1;
		}
		return ret1;
	}
	
	// Find the child device (if any) that matches the address prefix and send
	// the access to it.
	EAR_HaltReason r2 = HALT_NONE;
	bool ret2 = Bus_zoneAccess(&dev->children, mode, addr, is_byte, data, &r2);
	
	// Reconcile any errors between this device (SELF) and its child (CHILD).
	// Here are the cases we need to handle:
	//
	// 1. SELF any, CHILD unmapped: SELF drives error (no child matches the address)
	// 2. SELF any, CHILD !unmapped: CHILD drives result (more specific device exists)
	
	// Handle case 1: CHILD is unmapped, so use SELF's result
	if(r2 == HALT_BUS_FAULT) {
		if(out_r) {
			*out_r = r1;
		}
		return ret1;
	}
	
	// Handle case 2: CHILD is mapped, so use its result
	if(out_r) {
		*out_r = r2;
	}
	return ret2;
}

static bool Bus_zoneAccess(
	Bus_DeviceArray* devices,
	Bus_AccessMode mode,
	Bus_Addr addr, bool is_byte, void* data,
	EAR_HaltReason* out_r
) {
	// Binary search for the device that matches the address prefix
	size_t lo = 0, hi = devices->count - 1;
	while(lo <= hi) {
		size_t mid = (lo + hi) / 2;
		Bus_Device* dev = array_at(devices, mid);
		
		// If prefix_bitcount is 8, this produces the 24-bit mask 0b11111111'00000000'00000000
		Bus_Addr prefix_mask = ~((1 << (BUS_ADDRESS_BITS - dev->prefix_bitcount)) - 1);
		if((dev->prefix_pattern & prefix_mask) == (addr & prefix_mask)) {
			return Bus_deviceAccess(dev, mode, addr, is_byte, data, out_r);
		}
		
		if(addr < dev->prefix_pattern) {
			hi = mid - 1;
		}
		else {
			lo = mid + 1;
		}
	}
	
	// No matching device found
	if(out_r) {
		*out_r = HALT_BUS_FAULT;
	}
	return false;
}

/*!
 * @brief Perform a read or write access on the bus.
 * 
 * @param mode One of either BUS_MODE_READ or BUS_MODE_WRITE
 * @param addr Full 24-bit physical address for memory access
 * @param is_byte True if the access is a byte access, false for word access
 * @param data Pointer to data buffer to read from/write to
 * @param out_r Pointer to where the halt reason will be written (or NULL).
 * 
 * @return True if the access was successful
 */
bool Bus_access(
	Bus* bus, Bus_AccessMode mode,
	Bus_Addr addr, bool is_byte, void* data,
	EAR_HaltReason* out_r
) {
	EAR_HaltReason r = HALT_NONE;
	if(!is_byte && (addr & 1)) {
		*out_r = HALT_UNALIGNED;
		return false;
	}
	
	if(bus->hook_fn) {
		// Call the hook function if it exists
		r = bus->hook_fn(bus->hook_cookie, mode, addr, is_byte, data);
		if(r != HALT_NONE) {
			if(out_r) {
				*out_r = r == HALT_COMPLETE ? HALT_NONE : r;
			}
			return false;
		}
	}
	
	return Bus_zoneAccess(&bus->devices, mode, addr, is_byte, data, out_r);
}

/*!
 * @brief Handle a bus access.
 * 
 * @param cookie Opaque value passed to the callback
 * @param mode Either BUS_READ or BUS_WRITE, indicating the memory operation
 * @param paddr Full 24-bit address of the memory access
 * @param is_byte True if the access is a byte access, false for word access
 * @param data Pointer to the data buffer to read from/write to
 * 
 * @return True if the access was successful
 */
bool Bus_accessHandler(
	void* cookie, Bus_AccessMode mode,
	EAR_PhysAddr paddr, bool is_byte, void* data,
	EAR_HaltReason* out_r
) {
	return Bus_access(cookie, mode, (Bus_Addr)paddr, is_byte, data, out_r);
}


const char* Bus_accessModeToString(Bus_AccessMode mode) {
	switch(mode) {
		case BUS_MODE_READ:  return "read-only";
		case BUS_MODE_WRITE: return "write-only";
		case BUS_MODE_RDWR:  return "read-write";
	}
	return "none";
}


static void Bus_dumpZone(Bus_DeviceArray* devices, FILE* fp, int indent) {
	foreach(devices, dev) {
		const char* accmode = Bus_accessModeToString(dev->allowed_modes);
		uint32_t start_addr = dev->prefix_pattern;
		uint32_t end_addr = start_addr | ((1 << (BUS_ADDRESS_BITS - dev->prefix_bitcount)) - 1);
		fprintf(
			fp, "%02X:%04X-%02X:%04X: %*s%s %s\n",
			EAR_FULL_REGION(start_addr), EAR_FULL_NOTREGION(start_addr),
			EAR_FULL_REGION(end_addr), EAR_FULL_NOTREGION(end_addr),
			indent, "",
			accmode, dev->name
		);
		
		if(dev->handler_fn == Bus_memoryHandler) {
			struct Bus_memcookie* mem = dev->handler_cookie;
			if(mem->end_addr - 1 != end_addr) {
				fprintf(
					fp, "  (mapped %02X:%04X-%02X:%04X)\n",
					EAR_FULL_REGION(mem->start_addr), EAR_FULL_NOTREGION(mem->start_addr),
					EAR_FULL_REGION(mem->end_addr - 1), EAR_FULL_NOTREGION(mem->end_addr - 1)
				);
			}
		}
		
		if(!array_empty(&dev->children)) {
			// Recursively dump child devices
			Bus_dumpZone(&dev->children, fp, indent + 2);
		}
	}
}

void Bus_dump(void* cookie, FILE* fp) {
	Bus* bus = cookie;
	return Bus_dumpZone(&bus->devices, fp, 0);
}
