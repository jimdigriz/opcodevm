/* http://stackoverflow.com/a/17509569 */

#include <stdint.h>
#include <stdlib.h>
#include <err.h>

#include <x86intrin.h>

#define CONCAT(a,b) CONCAT_(a,b)
#define CONCAT_(a,b) a##b

unsigned int endian_x86(void *data, const unsigned int reclen, const unsigned int nrec)
{
	uint8_t *v = data;
	unsigned int i = 0;

#if defined(__AXV__)
#	define VEC_LEN	256
#	define VEC_APP	VEC_LEN

	const __m256i mask32 = _mm256_set_epi8(28, 29, 30, 31, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19, 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
	const __m256i mask64 = _mm256_set_epi8(24, 25, 26, 27, 28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7);
#elif defined(__SSSE3__)
#	define VEC_LEN	128
#	define VEC_APP

	const __m128i mask32 = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
	const __m128i mask64 = _mm_set_epi8(8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7);
#endif
#	define VEC	CONCAT(__m, CONCAT(VEC_LEN, i))
#	define VEC_LDR	CONCAT(_mm, CONCAT(VEC_APP, CONCAT(_loadu_si,  VEC_LEN)))
#	define VEC_STR	CONCAT(_mm, CONCAT(VEC_APP, CONCAT(_storeu_si, VEC_LEN)))
#	define VEC_SHF	CONCAT(_mm, CONCAT(VEC_APP, _shuffle_epi8))

	VEC mask;

	switch (reclen) {
	case 4:
		mask = mask32;
		break;
	case 8:
		mask = mask64;
		break;
	default:
		warnx("unknown endian size %u", reclen);
		abort();
	}

	for (; i < nrec - (nrec % (VEC_LEN/8/reclen)); i += VEC_LEN/8/reclen)
		VEC_STR((VEC *)&v[i*reclen],
			VEC_SHF(VEC_LDR((VEC *)&v[i*reclen]), mask));

	return i;
}
