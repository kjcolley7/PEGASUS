#ifndef EAR_BUS_H
#define EAR_BUS_H

#include "types.h"
#include "common/dynamic_array.h"


// Addresses on the physical memory bus are 24-bit (but the low bit is always 0)
typedef uint32_t Bus_Addr;
#define BUS_ADDRESS_BITS 24U
#define BUS_DATA_BITS 16U

typedef struct Bus_Device Bus_Device;
typedef dynamic_array(Bus_Device) Bus_DeviceArray;
struct Bus_Device {
	//! Name of this device
	const char* name;
	
	//! Handler callback for this device
	Bus_AccessHandler* handler_fn;
	
	//! Opaque value passed to the handler function
	void* handler_cookie;
	
	/*!
	 * @brief Array of child devices whose prefix patterns match this device's
	 *        prefix pattern (or are more specific). Sorted by prefix pattern.
	 */
	Bus_DeviceArray children;
	
	//! Physical address prefix for this device
	Bus_Addr prefix_pattern;
	
	//! Number of significant bits in the prefix pattern
	uint8_t prefix_bitcount : 5;
	static_assert(BUS_ADDRESS_BITS <= (1 << 5), "prefix_bitcount needs more bits");
	
	//! Allowed access modes
	uint8_t allowed_modes : 2;
};

typedef struct Bus Bus;
struct Bus {
	//! Hook function for all bus accesses
	Bus_Hook* hook_fn;
	
	//! Opaque value passed to the hook function
	void* hook_cookie;
	
	//! Devices attached to the bus, sorted by prefix pattern
	Bus_DeviceArray devices;
};


/*!
 * @brief Initialize an EAR physical memory bus with default values.
 */
void Bus_init(Bus* bus);

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
	Bus* bus, const char* name,
	Bus_AccessHandler* handler_fn, void* handler_cookie,
	Bus_Addr prefix_pattern, unsigned prefix_bitcount
);

/*!
 * @brief Attach a blob of physical memory to the bus. The caller is responsible for
 * managing the lifetime of the memory.
 * 
 * @param name Name of the memory region, used for debugging
 * @param modes Bitmask of allowed access modes for this memory region
 * @param start Starting physical memory address
 * @param size Size of memory blob, in bytes
 * @param data Pointer to beginning of the memory to attach, must be word-aligned
 * 
 * @return True if the memory was successfully attached, false otherwise
 */
void Bus_addMemory(
	Bus* bus, const char* name, Bus_AccessMode modes,
	Bus_Addr start, uint32_t size, EAR_UWord* data
);

/*!
 * @brief Add a callback function to be called for all memory accesses on the bus.
 * This function may choose to override the access by returning HALT_COMPLETE for success.
 * Alternatively, it may return HALT_NONE to allow the access to proceed normally.
 * 
 * @param hook Function pointer to the hook function
 * @param cookie Opaque value passed to the hook function
 */
void Bus_setHook(Bus* bus, Bus_Hook* hook, void* cookie);

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
	Bus_Addr paddr, bool is_byte, void* data,
	EAR_HaltReason* out_r
);

/*!
 * @brief Handle a bus access.
 * 
 * @param cookie Opaque value passed to the callback
 * @param mode Either BUS_READ or BUS_WRITE, indicating the memory operation
 * @param addr Full 24-bit address of the memory access
 * @param is_byte True if the access is a byte access, false for word access
 * @param data Pointer to the data buffer to read from/write to
 * 
 * @return True if the access was successful
 */
bool Bus_accessHandler(
	void* cookie, Bus_AccessMode mode,
	EAR_PhysAddr paddr, bool is_byte, void* data,
	EAR_HaltReason* out_r
);

/*! Dump debug info about the physical memory layout */
void Bus_dump(void* cookie, FILE* fp);

#endif /* EAR_BUS_H */
