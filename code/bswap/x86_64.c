#include <stdint.h>

#include "engine.h"
#include "engine-hooks.h"

#include <x86intrin.h>

#define OPCODE	bswap
#define IMP	x86_64

#define CONCAT(a,b) CONCAT_(a,b)
#define CONCAT_(a,b) a##b

#if defined(__AVX__)
#	define VEC_LEN	256
#	define VEC_APP	VEC_LEN
#elif defined(__SSSE3__)
#	define VEC_LEN	128
#	define VEC_APP
#endif

#define VEC	CONCAT(__m, CONCAT(VEC_LEN, i))
#define VEC_LDR	CONCAT(_mm, CONCAT(VEC_APP, CONCAT(_loadu_si,  VEC_LEN)))
#define VEC_STR	CONCAT(_mm, CONCAT(VEC_APP, CONCAT(_storeu_si, VEC_LEN)))
#define VEC_SHF	CONCAT(_mm, CONCAT(VEC_APP, _shuffle_epi8))

static VEC mask16, mask32, mask64;

/* http://stackoverflow.com/a/17509569 */
#define FUNC(x)	static void bswap_##x##_x86_64(size_t *offset, const size_t nrec, void *D)	\
		{										\
			uint##x##_t *d = D;							\
												\
			for (; *offset < nrec - (nrec % (VEC_LEN/x));				\
					*offset += VEC_LEN/x)					\
				VEC_STR((VEC *)&d[*offset],					\
				VEC_SHF(VEC_LDR((VEC *)&d[*offset]), mask##x));			\
		}										\
		static struct opcode_imp bswap##_##x##_x86_64_imp = {				\
			.func	= bswap##_##x##_x86_64,						\
			.cost	= (VEC_LEN/x),							\
			.width	= x,								\
			.name	= "bswap",							\
		};

FUNC(16)
FUNC(32)
FUNC(64)

static void __attribute__((constructor)) init()
{
	if (getenv("NOARCH"))
		return;

#if defined(__AVX__)
	mask16 = _mm256_set_epi8(30, 31, 28, 29, 26, 27, 24, 25, 22, 23, 20, 21, 18, 19, 16, 17, 14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1);
	mask32 = _mm256_set_epi8(28, 29, 30, 31, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19, 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
	mask64 = _mm256_set_epi8(24, 25, 26, 27, 28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7);
#elif defined(__SSSE3__)
	mask16 = _mm_set_epi8(14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1);
	mask32 = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
	mask64 = _mm_set_epi8(8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7);
#else
	return;
#endif

#	define HOOK(x)	engine_opcode_imp_init(&bswap##_##x##_x86_64_imp)
	HOOK(16);
	HOOK(32);
	HOOK(64);
}
