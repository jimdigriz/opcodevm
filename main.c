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
	{ .code	= BSWAP, .k = 0	},
	{0},
};

static struct program program = {
	.insns	= insns,
	.len	= ARRAY_SIZE(insns),
};

int main(int argc, char **argv)
{
	engine_init();

	struct data *data = calloc(argc + 1, sizeof(struct data));

	if (argc == 0)
		errx(EX_USAGE, "need to supply data files as arguments");

	for (int i = 0; i < argc; i++) {
		data[i].fd = open(argv[i], O_RDONLY);
		if (data[i].fd == -1)
			err(EX_NOINPUT, "open('%s')", argv[i]);

		struct stat stat;
		if (fstat(data[i].fd, &stat) == -1)
			err(EX_NOINPUT, "fstat()");

		data[i].addr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, data[i].fd, 0);
		if (data[i].addr == MAP_FAILED)
			err(EX_OSERR, "mmap(hdr)");

		if (munmap(data[i].addr, 4096))
			err(EX_OSERR, "munmap(hdr)");

		data[i].addr = mmap(NULL, data[i].numrec * data[i].reclen, PROT_READ|PROT_WRITE, MAP_PRIVATE, data[i].fd, 4096);
		if (data[i].addr == MAP_FAILED)
			err(EX_OSERR, "mmap(dat)");

		errno = posix_madvise(data[i].addr, data[i].numrec * data[i].reclen, MADV_SEQUENTIAL|MADV_WILLNEED);
		if (errno)
			warn("posix_madvise()");
	}

	/* TODO: move into header */
	data[0].reclen = sizeof(float);
	data[0].numrec = stat.st_size / data[0].reclen;
	data[0].endian = BIG;

	engine_run(&program, data);

	if (getenv("NODISP"))
		return 0;

	float *d = data[0].addr;
	for (uint64_t i = 0; i < data[0].numrec; i++)
		printf("%f\n", d[i]);

	return 0;
}
