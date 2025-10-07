//
//  dynamic_array.h
//  PegasusEar
//
//  Created by Kevin Colley on 5/2/18.
//

#ifndef PEG_DYNAMIC_ARRAY_H
#define PEG_DYNAMIC_ARRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "macros.h"

/*! Type of a dynamic array that holds elements of a given type */
#define dynamic_array(type...) \
struct { \
	type* elems; \
	size_t count; \
	size_t cap; \
}

/*! Get the type of an element in a dynamic array */
#define element_type(parr) __typeof__(*(parr)->elems)

/*! Get the size of an element in a dynamic array */
#define element_size(parr) sizeof(*(parr)->elems)

/*! Initialize an empty dynamic array */
#define array_init(parr) do { \
	__typeof__(parr) _array_init_parr = (parr); \
	_array_init_parr->count = 0; \
	_array_init_parr->cap = 0; \
	_array_init_parr->elems = NULL; \
} while(0)

/*! Return true if the dynamic array is empty, false otherwise */
#define array_empty(parr) ((parr)->count == 0)

/*! Get a pointer to an element in a dynamic array by its index */
#define array_at(parr, idx) &(parr)->elems[idx]

/*! Iterate over a dynamic array */
#define foreach(parr, pcur) UNIQUIFY(foreach_, parr, pcur)
#define foreach_(id, parr, pcur) enumerate_(id, parr, _iter_idx_##id, pcur)

/*! Iterate over a dynamic array with access to the index variable */
#define enumerate(parr, idx, pcur) UNIQUIFY(enumerate_, parr, idx, pcur)
#define enumerate_(id, parr, idx, pcur) \
for(bool _break_##id = false; \
	!_break_##id; \
	_break_##id = true) \
for(__typeof__(parr) _parr_##id = (parr); \
	!_break_##id; \
	_break_##id = true) \
for(size_t idx = 0; !_break_##id && idx < _parr_##id->count; idx++) \
	for(bool _break_detect##id = true; \
		_break_detect##id; \
		_break_##id = _break_detect##id, _break_detect##id = false) \
	for(element_type(parr)* pcur = array_at(_parr_##id, idx); \
		((void)pcur, _break_detect##id); \
		_break_detect##id = false)

/*! Expand a dynamic array. This will zero-initialize the extra allocated space
 * and will abort if the requested allocation size cannot be allocated.
 */
#define array_expand(parr) do { \
	__typeof__(parr) _array_expand_parr = (parr); \
	_expand_array(&_array_expand_parr->elems, &_array_expand_parr->cap); \
} while(0)
#define _expand_array(pelems, pcap) \
_expand_size_array((void**)(pelems), sizeof(**(pelems)), pcap, sizeof(*(pcap)))
static inline void _expand_size_array(void** pelems, size_t elem_size, void* pcap, size_t cap_sz) {
	size_t old_count;
	switch(cap_sz) {
		case 1: old_count = *(uint8_t*)pcap; break;
		case 2: old_count = *(uint16_t*)pcap; break;
		case 4: old_count = *(uint32_t*)pcap; break;
		case 8: old_count = *(uint64_t*)pcap; break;
		default: ASSERT(!"Unexpected cap_sz");
	}
	
	size_t new_count = MAX(old_count + 1, old_count * 7 / 4);
	
	/* Expand allocation */
	void* ret = realloc(*pelems, elem_size * new_count);
	if(!ret) {
		abort();
	}
	
	/* Zero-fill the expanded space */
	memset((char*)ret + elem_size * old_count, 0, elem_size * (new_count - old_count));
	
	switch(cap_sz) {
		case 1: *(uint8_t*)pcap = (uint8_t)new_count; break;
		case 2: *(uint16_t*)pcap = (uint16_t)new_count; break;
		case 4: *(uint32_t*)pcap = (uint32_t)new_count; break;
		case 8: *(uint64_t*)pcap = (uint64_t)new_count; break;
	}
	*pelems = ret;
}

/*! Expand a dynamic array when it's full */
#define _expand_if_full(parr) do { \
	__typeof__(parr) _expand_if_full_parr = (parr); \
	if(_expand_if_full_parr->count == _expand_if_full_parr->cap) { \
		array_expand(_expand_if_full_parr); \
	} \
} while(0)

/*! Append an element to the end of a dynamic array */
#define array_append(parr, elem...) do { \
	__typeof__(parr) _array_append_parr = (parr); \
	_expand_if_full(_array_append_parr); \
	_array_append_parr->elems[_array_append_parr->count++] = (elem); \
} while(0)

/*! Append an array to the end of a dynamic array */
#define array_extend(parr, src, srccount) do { \
	__typeof__(parr) _array_extend_parr = (parr); \
	const element_type(parr)* _array_extend_src = (src); \
	size_t _array_extend_srccount = (srccount); \
	static_assert(sizeof(*_array_extend_src) == element_size(parr), "element size mismatch"); \
	if(!_array_extend_src || _array_extend_srccount == 0) { \
		break; \
	} \
	size_t _array_extend_dstsize = _array_extend_parr->count * element_size(parr); \
	size_t _array_extend_srcsize = _array_extend_srccount * sizeof(*_array_extend_src); \
	size_t _array_extend_newsize = _array_extend_dstsize + _array_extend_srcsize; \
	while(_array_extend_parr->cap < _array_extend_newsize) { \
		array_expand(_array_extend_parr); \
	} \
	memcpy(_array_extend_parr->elems + _array_extend_parr->count, src, _array_extend_srcsize); \
	_array_extend_parr->count += _array_extend_srccount; \
} while(0)

/*! Insert an element into a dynamic array */
#define array_insert(parr, index, pelem) ({ \
	__typeof__(parr) _array_insert_parr = (parr); \
	_expand_if_full(_array_insert_parr); \
	(element_type(parr)*)_insert_index_array(_array_insert_parr->elems, element_size(parr), index, pelem, _array_insert_parr->count++); \
})
static inline void* _insert_index_array(
	void* arr,
	size_t elem_size,
	size_t index,
	const void* pelem,
	size_t count
) {
	/* Move from the target position */
	void* target = (char*)arr + index * elem_size;
	void* dst = (char*)target + elem_size;
	size_t move_size = (count - index) * elem_size;
	memmove(dst, target, move_size);
	memcpy(target, pelem, elem_size);
	return target;
}

/*! Remove an indexed range of a dynamic array */
#define array_removeRange(parr, start, end) do { \
	__typeof__(parr) _array_removeRange_parr = (parr); \
	size_t _array_removeRange_start = (start); \
	size_t _array_removeRange_end = (end); \
	ASSERT(_array_removeRange_end >= _array_removeRange_start); \
	if(_array_removeRange_start == _array_removeRange_end) { \
		break; \
	} \
	if(_array_removeRange_end > _array_removeRange_parr->count) { \
		_array_removeRange_end = _array_removeRange_parr->count; \
	} \
	size_t _array_removeRange_moveBytes = ( \
		(_array_removeRange_parr->count - _array_removeRange_end) * element_size(parr) \
	); \
	memmove( \
		array_at(_array_removeRange_parr, _array_removeRange_start), \
		array_at(_array_removeRange_parr, _array_removeRange_end), \
		_array_removeRange_moveBytes \
	); \
	_array_removeRange_parr->count -= _array_removeRange_end - _array_removeRange_start; \
} while(0)

/*! Remove an element from a dynamic array by its pointer */
#define array_removeElement(parr, pelem) do { \
	__typeof__(parr) _array_removeElement_parr = (parr); \
	array_removeIndex(_array_removeElement_parr, (pelem) - _array_removeElement_parr->elems); \
} while(0)

/*! Remove an element from a dynamic array by its index */
#define array_removeIndex(parr, index) do { \
	__typeof__(parr) _array_removeIndex_parr = (parr); \
	size_t _array_removeIndex_index = (index); \
	if(_array_removeIndex_index >= _array_removeIndex_parr->count) { \
		break; \
	} \
	_remove_index_array( \
		_array_removeIndex_parr->elems, element_size(parr), \
		_array_removeIndex_index, _array_removeIndex_parr->count-- \
	); \
} while(0)
static inline void _remove_index_array(void* pelems, size_t elem_size, size_t index, size_t count) {
	void* dst = (char*)pelems + index * elem_size;
	void* src = (char*)dst + elem_size;
	size_t move_size = (count - (index + 1)) * elem_size;
	memmove(dst, src, move_size);
}

/*! Shrink excess space in a dynamic array */
#define array_shrink(parr) do { \
	__typeof__(parr) _array_shrink_parr = (parr); \
	_array_shrink_parr->elems = (element_type(parr)*)realloc( \
		_array_shrink_parr->elems, \
		_array_shrink_parr->count * element_size(parr) \
	); \
	if(!_array_shrink_parr->elems) { \
		abort(); \
	} \
	_array_shrink_parr->cap = _array_shrink_parr->count; \
} while(0)

/*! Clear the contents of an array */
#define array_clear(parr) do { \
	__typeof__(parr) _array_clear_parr = (parr); \
	destroy(&_array_clear_parr->elems); \
	_array_clear_parr->count = _array_clear_parr->cap = 0; \
} while(0)

/*! Destroy all elements in an array and then the array itself */
#define array_destroy(parr) do { \
	__typeof__(parr) _array_destroy_parr = (parr); \
	foreach(_array_destroy_parr, _array_destroy_pcur) { \
		free(*_array_destroy_pcur); \
	} \
	array_clear(_array_destroy_parr); \
} while(0)


#endif /* PEG_DYNAMIC_ARRAY_H */
