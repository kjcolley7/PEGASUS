//
//  utils.h
//  PegasusEar
//
//  Created by Kevin Colley on 11/17/22.
//

#ifndef EAR_UTILS_H
#define EAR_UTILS_H

#include <stdio.h>
#include "ear.h"

void ear_xxd(const void* data, EAR_Size size, EAR_Size* base_offset, FILE* fp);

#endif /* EAR_UTILS_H */
