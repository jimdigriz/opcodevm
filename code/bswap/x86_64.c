#include <stdint.h>
#include <assert.h>
#include <x86intrin.h>

#include "utils.h"
#include "engine.h"

#define OPCODE	bswap
#define IMP	x86_64

#if defined(__AVX__)
#	define VEC_LEN	256
#	define VEC_APP	VEC_LEN
#elif defined(__SSSE3__)
#	define VEC_LEN	128
#	define VEC_APP
#endif

#define VEC	XCONCAT(__m, XCONCAT(VEC_LEN, i))
#define VEC_LDR	XCONCAT(_mm, XCONCAT(VEC_APP, XCONCAT(_loadu_si,  VEC_LEN)))
#define VEC_STR	XCONCAT(_mm, XCONCAT(VEC_APP, XCONCAT(_storeu_si, VEC_LEN)))
#define VEC_SHF	XCONCAT(_mm, XCONCAT(VEC_APP, _shuffle_epi8))

static VEC mask16, mask32, mask64;

/* http://stackoverflow.com/a/17509569 */
#define XFUNC(x,y,z)	FUNC(x,y,z)
#define FUNC(x,y,z)	static void x##_##y##_##z(OPCODE_IMP_BSWAP_PARAMS)		\
			{								\
											\
				uint##y##_t *d = C->addr;				\
											\
				assert((uintptr_t)&d[*o] % (VEC_LEN / 8) == 0);		\
											\
				for (; *o < n - (n % (VEC_LEN/y)); *o += VEC_LEN/y)	\
					VEC_STR((VEC *)&d[*o],				\
					VEC_SHF(VEC_LDR((VEC *)&d[*o]), mask##y));	\
			}								\
			static struct opcode_imp_##x x##_##y##_##z##_imp = {		\
				.func	= x##_##y##_##z,				\
				.cost	= (VEC_LEN/y),					\
				.width	= y,						\
			};

XFUNC(OPCODE, 16, IMP)
XFUNC(OPCODE, 32, IMP)
XFUNC(OPCODE, 64, IMP)

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

	XENGINE_HOOK(OPCODE, 16, IMP)
	XENGINE_HOOK(OPCODE, 32, IMP)
	XENGINE_HOOK(OPCODE, 64, IMP)
}
