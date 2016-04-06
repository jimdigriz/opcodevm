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
#include <errno.h>
#include <fcntl.h>

#ifdef __amd64__
#include <x86intrin.h>
#endif

#ifndef __STDC_IEC_559__
#	error __STDC_IEC_559__ is not set, requires IEEE 745
#endif

#define IS_PAGE_ALIGNED(x) assert((uintptr_t)(const void *)(x) % getpagesize() == 0)

void _endian(void *data, const unsigned int reclen, const unsigned int nrec)
{
	uint8_t *v = data;
	unsigned int i = 0;

	if (!getenv("NOVEC")) {
/* http://stackoverflow.com/a/17509569 */
#ifdef __AVX__
		const __m256i mm256_mask32 = _mm256_set_epi8(28, 29, 30, 31, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19, 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
		const __m256i mm256_mask64 = _mm256_set_epi8(24, 25, 26, 27, 28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7);
		__m256i mm256_mask;

		switch (reclen) {
		case 4:
			mm256_mask = mm256_mask32;
			break;
		case 8:
			mm256_mask = mm256_mask64;
			break;
		}

		for (; i < nrec - (nrec % (256/8/reclen)); i += 256/8/reclen)
			_mm256_storeu_si256((__m256i *)&v[i*reclen],
				_mm256_shuffle_epi8(_mm256_loadu_si256((__m256i *)&v[i*reclen]), mm256_mask));
#endif
#ifdef __SSSE3__
		const __m128i mm_mask32 = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
		const __m128i mm_mask64 = _mm_set_epi8(8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7);
		__m128i mm_mask;

		switch (reclen) {
		case 4:
			mm_mask = mm_mask32;
			break;
		case 8:
			mm_mask = mm_mask64;
			break;
		}

		for (; i < nrec - (nrec % (128/8/reclen)); i += 128/8/reclen)
			_mm_storeu_si128((__m128i *)&v[i*reclen],
				_mm_shuffle_epi8(_mm_loadu_si128((__m128i *)&v[i*reclen]), mm_mask));
#endif
	}

	switch (reclen) {
	case 4:
		for (; i < nrec; i++)
			*(uint32_t *)&v[i*reclen] = be32toh(*(uint32_t *)&v[i*reclen]);
		break;
	case 8:
		for (; i < nrec; i++)
			*(uint64_t *)&v[i*reclen] = be64toh(*(uint64_t *)&v[i*reclen]);
		break;
	}
}

void endian(void *data, const unsigned int reclen, const unsigned int nrec, const int byteorder)
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
	case 8:
		_endian(data, reclen, nrec);
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

	errno = posix_fadvise(fd, 0, stat.st_size, POSIX_FADV_SEQUENTIAL|POSIX_FADV_WILLNEED);
	if (errno)
		warn("posix_fadvise()");

	float *data = mmap(NULL, stat.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (data == MAP_FAILED)
		err(EX_OSERR, "mmap()");

	endian(data, sizeof(float), stat.st_size / sizeof(float), __ORDER_BIG_ENDIAN__);

	for (unsigned int i = 0; i < stat.st_size / sizeof(float); i++)
		printf("%f\n", data[i]);

	return 0;
}
