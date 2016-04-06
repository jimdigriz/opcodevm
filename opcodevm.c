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
#include <dlfcn.h>

#ifndef __STDC_IEC_559__
#	error __STDC_IEC_559__ is not set, requires IEEE 745
#endif

#define IS_PAGE_ALIGNED(x) assert((uintptr_t)(const void *)(x) % getpagesize() == 0)

unsigned int (*_endian)(void *, const unsigned int, const unsigned int);

void endian(void *data, const unsigned int reclen, const unsigned int nrec, const int byteorder)
{
	IS_PAGE_ALIGNED(data);

	if (__BYTE_ORDER__ == byteorder)
		return;

	uint8_t *v = data;
	unsigned int i = 0;

	if (!getenv("NOVEC") && _endian)
		i = _endian(data, reclen, nrec);

	switch (reclen) {
	case 2:
		for (; i < nrec; i++)
			*(uint16_t *)&v[i*reclen] = be16toh(*(uint16_t *)&v[i*reclen]);
		break;
	case 4:
		for (; i < nrec; i++)
			*(uint32_t *)&v[i*reclen] = be32toh(*(uint32_t *)&v[i*reclen]);
		break;
	case 8:
		for (; i < nrec; i++)
			*(uint64_t *)&v[i*reclen] = be64toh(*(uint64_t *)&v[i*reclen]);
		break;
	default:
		warnx("unknown endian size %u", reclen);
		abort();
	}
}

void loadjets() {
	void *handle = dlopen("jets/endian/x86.so", RTLD_LAZY);
	if (!handle) {
		warnx("unable to load endian jet %s\n", dlerror());
		return;
	}

	dlerror();

	_endian = (unsigned int (*)(void *, const unsigned int, const unsigned int))(intptr_t) dlsym(handle, "endian_x86");

	char *error = dlerror();
	if (error) {
		warnx("unable to resolve endian jet %s\n", dlerror());
		return;
	}
}

int main(int argc, char **argv)
{
	loadjets();

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
