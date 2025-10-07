// Derived from https://github.com/teslamotors/liblithium

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "common/macros.h"

bool g_debug = false;

typedef uint16_t uword;

#define GIMLI_BYTES 48
#define GIMLI_WORDS (GIMLI_BYTES / sizeof(uword))
#define GIMLI_RATE 16
#define GIMLI_HASH_DEFAULT_LEN 32

typedef struct
{
	uword state[GIMLI_WORDS];
	uword offset;
} gimli_state;

typedef gimli_state gimli_hash_state;

__attribute__((unused)) static void gimli_dump_state(const void* state)
{
	const unsigned char* p = state;
	unsigned i;
	fprintf(stderr, "  ");
	for(i = 0; i < GIMLI_BYTES; i++) {
		if(i != 0) {
			if(i % 16 == 0) {
				fprintf(stderr, "\n  ");
			}
			else if(i % 4 == 0) {
				fprintf(stderr, " ");
			}
		}
		
		fprintf(stderr, "%02x", p[i]);
	}
	fprintf(stderr, "\n");
}

#define U32(lo, hi) (((uint32_t)(hi) << 16) | (lo))

#define rol(x, n) (((x) << ((n) % 32)) | ((x) >> ((32 - (n)) % 32)))
// 33222222 22221111 : 11111100 00000000
// 10987654 32109876 : 54321098 76543210
// abcdefgh ijklmnop : qrstuvwx yzABCDEF
// rol24
// yzABCDEF abcdefgh : ijklmnop qrstuvwx


// 33222222 22221111 : 11111100 00000000
// 10987654 32109876 : 54321098 76543210
// abcdefgh ijklmnop : qrstuvwx yzABCDEF
// rol9
// jklmnopq rstuvwxy : zABCDEFa bcdefghi


#define LDW(x) (*(const uword*)(x))
#define POP(x) ({ \
	const uword _val = LDW(x); \
	(x) += sizeof(_val); \
	_val; \
})
#define STW(x, v) do { \
	*(uword*)(x) = (v); \
} while(0)
#define PSH(x, v) do { \
	(x) -= sizeof(v); \
	STW(x, v); \
} while(0)

static void gimli(uword state[GIMLI_WORDS])
{
	if(g_debug) {
		fprintf(stderr, "!!!!!Gimli!!!!!\n");
		gimli_dump_state(state);
	}
	
	struct {
		char* state;
	} a0;
	struct {
		uword round;
		uword bl;
	} a1;
	struct {
		uword column;
	} a2;
	struct {
		uword xl;
	} a3;
	struct {
		uword xh;
	} a4;
	struct {
		uword yl;
	} a5;
	struct {
		uword yh;
	} s0;
	struct {
		uword zl;
	} s1;
	struct {
		uword zh;
	} s2;
	struct {
		uword bh;
	} s3;
	struct {
		uword al;
	} ra;
	struct {
		uword ah;
	} rd;
	bool carry;
	
	char* orig_state = (char*)state;
	a0.state = orig_state;
	
	for (a1.round = 24; a1.round > 0; --a1.round)
	{
		// push(a1.round);
		
		for (a2.column = 0; a2.column < 4; ++a2.column)
		{
			ASSERT(a0.state == orig_state + a2.column * sizeof(uint32_t));
			
			// const uint32_t* px = state + column * sizeof(uint32_t);
			// const uint32_t x = rol(*px, 24);
			a3.xl = POP(a0.state);
			a4.xh = POP(a0.state);
			uint32_t x_raw = U32(a3.xl, a4.xh);
			// x = rol(x, 24);
			ra.al = a3.xl << 8;
			rd.ah = a4.xh << 8;
			a3.xl >>= (16 - 8);
			a3.xl |= rd.ah;
			a4.xh >>= (16 - 8);
			a4.xh |= ra.al;
			uint32_t x = U32(a3.xl, a4.xh);
			ASSERT(x == rol(x_raw, 24));
			
			a0.state += 3 * sizeof(uint32_t);
			ASSERT(a0.state == orig_state + (a2.column + 4) * 4);
			
			// const uint32_t* py = state + (column + 4) * sizeof(uint32_t);
			// const uint32_t y = rol(*py, 9);
			a5.yl = POP(a0.state);
			s0.yh = POP(a0.state);
			uint32_t y_raw = U32(a5.yl, s0.yh);
			// y = rol(y, 9);
			ra.al = a5.yl >> (16 - 9);
			rd.ah = s0.yh >> (16 - 9);
			a5.yl <<= 9;
			a5.yl |= rd.ah;
			s0.yh <<= 9;
			s0.yh |= ra.al;
			uint32_t y = U32(a5.yl, s0.yh);
			ASSERT(y == rol(y_raw, 9));
			
			a0.state += 3 * sizeof(uint32_t);
			ASSERT(a0.state == orig_state + (a2.column + 8) * sizeof(uint32_t));
			
			// const uint32_t* pz = state + (column + 8) * sizeof(uint32_t);
			// const uint32_t z = *pz;
			s1.zl = POP(a0.state);
			s2.zh = POP(a0.state);
			uint32_t z = U32(s1.zl, s2.zh);
			
			if(g_debug && a1.round == 24 && a2.column == 0) {
				fprintf(
					stderr, "x_raw=%#08x x=%#08x y_raw=%#08x y=%#08x z=%#08x\n",
					x_raw, x, y_raw, y, z
				);
			}
			
			// state[column + 8] = x ^ (z << 1) ^ ((y & z) << 2);
			
			// (y & z)
			ra.al = a5.yl & s1.zl;
			rd.ah = s0.yh & s2.zh;
			ASSERT(U32(ra.al, rd.ah) == (y & z));
			
			// (y & z) << 2
			a1.bl = ra.al >> (16 - 2);
			ra.al <<= 2;
			rd.ah <<= 2;
			rd.ah |= a1.bl;
			ASSERT(U32(ra.al, rd.ah) == ((y & z) << 2));
			
			// (z << 1)
			s3.bh = s2.zh << 1;
			carry = !!(s1.zl & 0x8000);
			a1.bl = s1.zl << 1;
			if(carry) {
				s3.bh |= 1;
			}
			ASSERT(U32(a1.bl, s3.bh) == (z << 1));
			
			// (z << 1) ^ ((y & z) << 2)
			ra.al ^= a1.bl;
			rd.ah ^= s3.bh;
			ASSERT(U32(ra.al, rd.ah) == ((z << 1) ^ ((y & z) << 2)));
			
			// x ^ (z << 1) ^ ((y & z) << 2)
			ra.al ^= a3.xl;
			rd.ah ^= a4.xh;
			ASSERT(U32(ra.al, rd.ah) == (x ^ (z << 1) ^ ((y & z) << 2)));
			
			// {
			// 	uint32_t got = ((uint32_t)rd.ah << 16) | ra.al;
			// 	uint32_t x = ((uint32_t)a4.xh << 16) | a3.xl;
			// 	uint32_t y = ((uint32_t)s0.yh << 16) | a5.yl;
			// 	uint32_t z = ((uint32_t)s2.zh << 16) | s1.zl;
			// 	uint32_t expected = x ^ (z << 1) ^ ((y & z) << 2);
			// 	if(got != expected) {
			// 		fprintf(stderr, "Gimli error: column %hu, round %hu\n", a2.column, a1.round);
			// 		fprintf(stderr, "x=0x%08x, y=0x%08x, z=0x%08x\n", x, y, z);
			// 		fprintf(stderr, "got %08x, expected %08x\n", got, expected);
			// 		gimli_dump_state(state);
			// 		ASSERT(got == expected);
			// 	}
			// }
			
			ASSERT(a0.state == orig_state + (a2.column + 9) * sizeof(uint32_t));
			
			// p = state + (column + 8) * sizeof(uint32_t);
			// *p = x ^ (z << 1) ^ ((y & z) << 2);
			PSH(a0.state, rd.ah);
			PSH(a0.state, ra.al);
			
			
			// state[column + 4] = y ^ x ^ ((x | z) << 1);
			
			// (x | z)
			ra.al = a3.xl | s1.zl;
			rd.ah = a4.xh | s2.zh;
			
			// (x | z) << 1
			rd.ah <<= 1;
			carry = !!(ra.al & 0x8000);
			ra.al <<= 1;
			if(carry) {
				rd.ah |= 1;
			}
			
			// x ^ ((x | z) << 1)
			ra.al ^= a3.xl;
			rd.ah ^= a4.xh;
			
			// y ^ x ^ ((x | z) << 1)
			ra.al ^= a5.yl;
			rd.ah ^= s0.yh;
			
			a0.state -= 3 * sizeof(uint32_t);
			ASSERT(a0.state == orig_state + (a2.column + 5) * sizeof(uint32_t));
			
			// p = state + (column + 4) * sizeof(uint32_t);
			// *p = y ^ x ^ ((x | z) << 1);
			PSH(a0.state, rd.ah);
			PSH(a0.state, ra.al);
			
			
			// state[column] = z ^ y ^ ((x & y) << 3);
			
			// (x & y)
			ra.al = a3.xl & a5.yl;
			rd.ah = a4.xh & s0.yh;
			
			// (x & y) << 3
			a1.bl = ra.al >> (16 - 3);
			ra.al <<= 3;
			rd.ah <<= 3;
			rd.ah |= a1.bl;
			
			// y ^ ((x & y) << 3)
			ra.al ^= a5.yl;
			rd.ah ^= s0.yh;
			
			// z ^ y ^ ((x & y) << 3)
			ra.al ^= s1.zl;
			rd.ah ^= s2.zh;
			
			// p = state + column * sizeof(uint32_t);
			// *p = z ^ y ^ ((x & y) << 3);
			a0.state -= 3 * sizeof(uint32_t);
			ASSERT(a0.state == orig_state + (a2.column + 1) * sizeof(uint32_t));
			PSH(a0.state, rd.ah);
			PSH(a0.state, ra.al);
			a0.state += sizeof(uint32_t);
			
			// if(g_debug && a1.round == 24) {
			// 	fprintf(stderr, "After first round, column %hu:\n", a2.column);
			// 	gimli_dump_state(state);
			// }
		}
		a0.state -= 4 * sizeof(uint32_t);
		ASSERT(a0.state == orig_state);
		
		// a1.round = pop();
		
		switch (a1.round & 3)
		{
		case 0:
			/* small swap: pattern s...s...s... etc. */
			/* add constant: pattern c...c...c... etc. */
			// x = state[0];
			a3.xl = POP(a0.state);
			a4.xh = POP(a0.state);
			// y = state[1];
			a5.yl = POP(a0.state);
			s0.yh = POP(a0.state);
			// state[1] = x;
			PSH(a0.state, a4.xh);
			PSH(a0.state, a3.xl);
			
			// static uint32_t coeff(int round)
			// {
			//     return UINT32_C(0x9E377900) | (uint32_t)round;
			// }
			// state[0] = y ^ coeff(round);
			s0.yh ^= 0x9E37;
			a3.xl = a1.round | 0x7900;
			a5.yl ^= a3.xl;
			PSH(a0.state, s0.yh);
			PSH(a0.state, a5.yl);
			ASSERT(a0.state == orig_state);
			a0.state += 2 * sizeof(uint32_t);
			
			// x = state[2];
			a3.xl = POP(a0.state);
			a4.xh = POP(a0.state);
			// y = state[3];
			a5.yl = POP(a0.state);
			s0.yh = POP(a0.state);
			// state[3] = x;
			PSH(a0.state, a4.xh);
			PSH(a0.state, a3.xl);
			// state[2] = y;
			PSH(a0.state, s0.yh);
			PSH(a0.state, a5.yl);
			a0.state -= 2 * sizeof(uint32_t);
			ASSERT(a0.state == orig_state);
			break;
		
		case 2:
			/* big swap: pattern ..S...S...S. etc. */
			// x = state[0];
			a3.xl = POP(a0.state);
			a4.xh = POP(a0.state);
			// y = state[2];
			a0.state += sizeof(uint32_t);
			a5.yl = POP(a0.state);
			s0.yh = POP(a0.state);
			
			// state[2] = x;
			PSH(a0.state, a4.xh);
			PSH(a0.state, a3.xl);
			// state[0] = y;
			a0.state -= sizeof(uint32_t);
			PSH(a0.state, s0.yh);
			PSH(a0.state, a5.yl);
			ASSERT(a0.state == orig_state);
			
			// x = state[1];
			a0.state += sizeof(uint32_t);
			a3.xl = POP(a0.state);
			a4.xh = POP(a0.state);
			// y = state[3];
			a0.state += sizeof(uint32_t);
			a5.yl = POP(a0.state);
			s0.yh = POP(a0.state);
			// state[3] = x;
			PSH(a0.state, a4.xh);
			PSH(a0.state, a3.xl);
			// state[1] = y;
			a0.state -= sizeof(uint32_t);
			PSH(a0.state, s0.yh);
			PSH(a0.state, a5.yl);
			a0.state -= sizeof(uint32_t);
			ASSERT(a0.state == orig_state);
			break;
		
		default:
			break;
		}
		
		if(g_debug) {
			fprintf(stderr, "After round %hu:\n", a1.round);
			gimli_dump_state(state);
		}
	}
}


static void gimli_absorb_byte(gimli_state* g, unsigned char x)
{
	unsigned char* p = (unsigned char*)g->state + g->offset;
	unsigned char tmp = *p;
	tmp ^= x;
	*p = tmp;
}


static unsigned char gimli_squeeze_byte(const gimli_state* g)
{
	unsigned char* p = (unsigned char*)g->state + g->offset;
	return *p;
}


static void gimli_advance(gimli_state* g)
{
	++g->offset;
	if (g->offset == GIMLI_RATE)
	{
		gimli(g->state);
		g->offset = 0;
	}
}


static void gimli_absorb(gimli_state* g, const unsigned char* m, uword len)
{
	while (len--)
	{
		unsigned char c = *m++;
		gimli_absorb_byte(g, c);
		gimli_advance(g);
	}
}


static void gimli_squeeze(gimli_state* g, unsigned char* h, uword len)
{
	g->offset = GIMLI_RATE - 1;
	while (len--)
	{
		gimli_advance(g);
		*h++ = gimli_squeeze_byte(g);
	}
}


static void gimli_pad(gimli_state* g)
{
	gimli_absorb_byte(g, 0x01);
	
	// XOR last byte with 0x01
	unsigned char* p = (unsigned char*)g->state + GIMLI_BYTES - 1;
	unsigned char tmp = *p;
	tmp ^= 0x01;
	*p = tmp;
}


void gimli_hash_init(gimli_hash_state* g)
{
	(void)memset(g, 0, sizeof(*g));
}

void gimli_hash_update(gimli_hash_state* g, const unsigned char* m, uword len)
{
	gimli_absorb(g, m, len);
}

void gimli_hash_final(gimli_hash_state* g, unsigned char* h, uword len)
{
	gimli_pad(g);
	gimli_squeeze(g, h, len);
}

void gimli_hash(unsigned char* h, uword hlen, const unsigned char *m,
				uword mlen)
{
	gimli_state g;
	gimli_hash_init(&g);
	gimli_hash_update(&g, m, mlen);
	gimli_hash_final(&g, h, hlen);
}


int main(int argc, char** argv) {
	if(argc == 2 && !strcmp(argv[1], "--debug")) {
		g_debug = true;
	}
	
	gimli_state g;
	gimli_hash_init(&g);
	
	int ch;
	while((ch = getchar()) != EOF) {
		unsigned char c = (unsigned char)ch;
		gimli_hash_update(&g, &c, 1);
		
		// if(g_debug) {
		// 	fprintf(stderr, "State after absorbing byte '%c' (0x%02x):\n", c, c);
		// 	gimli_dump_state(g.state);
		// }
	}
	
	unsigned char hash[GIMLI_HASH_DEFAULT_LEN];
	gimli_hash_final(&g, hash, sizeof(hash));
	
	unsigned i;
	for(i = 0; i < sizeof(hash); i++) {
		printf("%02x", hash[i]);
	}
	printf("\n");
	
	return 0;
}
