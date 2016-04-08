#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
#include <sysexits.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>

#include "engine.h"

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

static struct insn insns[] = {
	{ .code	= BSWAP	},
	{0},
};

static struct program program = {
	.insns	= insns,
	.len	= ARRAY_SIZE(insns),
};

static struct data data[] = {
	{ .path	= "datafile" },
	{0},
};

int main(int argc, char **argv)
{
	engine_init();

	if (getenv("DATAFILE"))
		data[0].path = getenv("DATAFILE");

	data[0].fd = open(data[0].path, O_RDONLY);
	if (data[0].fd == -1)
		err(EX_NOINPUT, "open('%s')", data[0].path);

	struct stat stat;
	if (fstat(data[0].fd, &stat) == -1)
		err(EX_NOINPUT, "fstat()");

	/* TODO: move into header */
	data[0].reclen = sizeof(float);
	data[0].numrec = stat.st_size / data[0].reclen;
	data[0].endian = BIG;

	errno = posix_fadvise(data[0].fd, 0, data[0].numrec * data[0].reclen, POSIX_FADV_SEQUENTIAL|POSIX_FADV_WILLNEED);
	if (errno)
		warn("posix_fadvise()");

	data[0].addr = mmap(NULL, data[0].numrec * data[0].reclen, PROT_READ|PROT_WRITE, MAP_PRIVATE, data[0].fd, 0);
	if (data[0].addr == MAP_FAILED)
		err(EX_OSERR, "mmap()");

	engine_run(&program, data);

	float *d = data[0].addr;
	for (uint64_t i = 0; i < data[0].numrec; i++)
		printf("%f\n", d[i]);

	return 0;
}
