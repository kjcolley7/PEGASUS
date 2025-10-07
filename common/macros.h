//
//  macros.h
//  PegasusEar
//
//  Created by Kevin Colley on 11/1/2020.
//

#ifndef PEG_MACROS_H
#define PEG_MACROS_H

#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>

#ifndef UNIQUIFY
#define UNIQUIFY(macro, ...) UNIQUIFY_(macro, __COUNTER__, ##__VA_ARGS__)
#define UNIQUIFY_(macro, id, ...) UNIQUIFY__(macro, id, ##__VA_ARGS__)
#define UNIQUIFY__(macro, id, ...) macro(id, ##__VA_ARGS__)
#endif

#define STRINGIFY(arg) STRINGIFY_(arg)
#define STRINGIFY_(arg) #arg

/* On release builds, ASSERT(cond) tells the compiler that cond must be true */
#if NDEBUG
#define ASSERT(expr) do { \
	if(!(expr)) { \
		__builtin_unreachable(); \
	} \
} while(0)
#else /* NDEBUG */
#define ASSERT(expr) assert(expr)
#endif /* NDEBUG */

#define CMP(op, a, b) ({ \
	__typeof__((a)+(b)) _a = (a); \
	__typeof__(_a) _b = (b); \
	(_a op _b) ? _a : _b; \
})
#define MIN(a, b) CMP(<, a, b)
#define MAX(a, b) CMP(>, a, b)

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
#define BITCOUNT(x) (sizeof(x) * 8)

/*! Frees the pointer pointed to by the parameter and then sets it to NULL */
#define destroy(pptr) do { \
	__typeof__(*(pptr)) volatile* _pptr = (pptr); \
	if(!_pptr) { \
		break; \
	} \
	free(*_pptr); \
	*_pptr = NULL; \
} while(0)

#define fail() fail_(__LINE__)
static inline void fail_(int line) {
	printf("Uh oh, something went wrong. Contact the admin for help and provide this failure code: %d.%d\n", line, errno);
	exit(EXIT_FAILURE);
}

#endif /* PEG_MACROS_H */
