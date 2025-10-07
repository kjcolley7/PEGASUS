//
//  utils.h
//  PegasusEar
//
//  Created by Kevin Colley on 11/17/22.
//

#ifndef EAR_UTILS_H
#define EAR_UTILS_H

#include <stdio.h>
#include <stdint.h>

void ear_xxd(
	const void* data, uint32_t size, uint32_t* base_offset,
	const char* prefix, int addr_digits, FILE* fp
);

#endif /* EAR_UTILS_H */
