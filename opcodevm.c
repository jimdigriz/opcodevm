#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <endian.h>
#include <endian.h>
#ifdef __amd64__
#include <x86intrin.h>
#endif

#ifndef __STDC_IEC_559__
#	error __STDC_IEC_559__ is not set, requires IEEE 745
#endif

#define IS_PAGE_ALIGNED(x) assert((uintptr_t)(const void *)(x) % getpagesize() == 0)

void endian32(void *data, const unsigned int nrec)
{
	uint32_t *v = data;
	unsigned int i = 0;

	if (!getenv("NOVEC")) {
/* http://stackoverflow.com/a/17509569 */
#ifdef __AVX__
		const __m256i mask256 = _mm256_set_epi8(28, 29, 30, 31, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19, 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);

		for (; i < nrec - (nrec % 8); i += 8)
			_mm256_storeu_si256((__m256i *)&v[i],
				_mm256_shuffle_epi8(_mm256_loadu_si256((__m256i *)&v[i]), mask256));
#endif
#ifdef __SSSE3__
		const __m128i mask = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);

		for (; i < nrec - (nrec % 4); i += 4)
			_mm_storeu_si128((__m128i *)&v[i],
				_mm_shuffle_epi8(_mm_loadu_si128((__m128i *)&v[i]), mask));
#endif
	}

	do {
		v[i] = be32toh(v[i]);
	} while (i++ < nrec);
}

void endian64(void *data, const unsigned int nrec)
{
	uint64_t *v = data;
	unsigned int i = 0;

	if (!getenv("NOVEC")) {
#ifdef __AVX__
		const __m256i mask256 = _mm256_set_epi8(24, 25, 26, 27, 28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7);

		for (; i < nrec - (nrec % 4); i += 4)
			_mm256_storeu_si256((__m256i *)&v[i],
				_mm256_shuffle_epi8(_mm256_loadu_si256((__m256i *)&v[i]), mask256));
#endif
#ifdef __SSSE3__
		const __m128i mask128 = _mm_set_epi8(8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7);

		for (; i < nrec - (nrec % 2); i += 2)
			_mm_storeu_si128((__m128i *)&v[i],
				_mm_shuffle_epi8(_mm_loadu_si128((__m128i *)&v[i]), mask128));
#endif
	}

	do {
		v[i] = be64toh(v[i]);
	} while (i++ < nrec);
}

void endian(void *data, int reclen, unsigned int nrec, int byteorder)
{
	IS_PAGE_ALIGNED(data);

	switch (reclen) {
	case 1:
		return;
	case 4:
	case 8:
		break;
	default:
		warnx("unknown endian size %d", reclen);
		abort();
	}

	if (__BYTE_ORDER__ == byteorder)
		return;

	switch (reclen) {
	case 4:
		endian32(data, nrec);
		break;
	case 8:
		endian64(data, nrec);
		break;
	}
}

int main(int argc, char **argv)
{
	char *datafile = getenv("DATAFILE");
	if (!datafile)
		datafile = "datafile";

	int fd = open(datafile, O_RDONLY);
	if (fd == -1)
		err(EX_NOINPUT, "open('%s')", datafile);

	struct stat stat;
	if (fstat(fd, &stat) == -1)
		err(EX_NOINPUT, "fstat()");

	float *data = mmap(NULL, stat.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (data == MAP_FAILED)
		err(EX_OSERR, "mmap()");

	endian(data, sizeof(float), stat.st_size / sizeof(float), __ORDER_BIG_ENDIAN__);

	for (unsigned int i = 0; i < stat.st_size / sizeof(float); i++)
		printf("%f\n", data[i]);

	return 0;
}
