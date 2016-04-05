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
#ifdef __SSSE3__
#include <x86intrin.h>
#endif

#ifndef __STDC_IEC_559__
#	error __STDC_IEC_559__ is not set, requires IEEE 745
#endif

#define IS_PAGE_ALIGNED(x) assert((uintptr_t)(const void *)(x) % getpagesize() == 0)

void endian(float *data, int reclen, unsigned int nrec, int byteorder)
{
	IS_PAGE_ALIGNED(data);

	switch (reclen) {
	case 1:
		return;
	case 4:
		break;
	default:
		warnx("unknown endian size %d", reclen);
		abort();
	}

	if (__BYTE_ORDER__ == byteorder)
		return;

	unsigned int i = 0;
	union v {
		float		f;
		uint32_t	i;
	};

#ifdef __SSSE3__
	/* http://stackoverflow.com/a/17509569 */
	const __m128i mask = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3); 

	for (; i < nrec - (nrec % 4); i += 4)
		_mm_storeu_si128((__m128i *)&data[i],
			_mm_shuffle_epi8(_mm_loadu_si128((__m128i *)&data[i]), mask));

	i--;
#endif

	while (i++ < nrec)
		((union v *)&data[i])->i = be32toh(((union v *)&data[i])->i);
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
