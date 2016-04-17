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
#include <byteswap.h>
#include <unistd.h>

#include "engine.h"

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

static struct insn insns[] = {
	{ .code = "bswap", .dest = 0 },
	{ .code = "ret"				},
};

static struct routine loop = {
	.insns	= insns,
	.len	= ARRAY_SIZE(insns),
};

static struct program program = {
	.loop	= &loop,
};

int main(int argc, char **argv)
{
	struct data *D = calloc(argc - 1, sizeof(struct data));

	if (argc == 1)
		errx(EX_USAGE, "need to supply data files as arguments");

	engine_init();

	uint64_t nrec = UINT64_MAX;
	for (int i = 1; i < argc; i++) {
		D[i - 1].fd = open(argv[i], O_RDONLY);
		if (D[i - 1].fd == -1)
			err(EX_NOINPUT, "open('%s')", argv[i]);

		struct stat stat;
		if (fstat(D[i - 1].fd, &stat) == -1)
			err(EX_NOINPUT, "fstat('%s')", argv[i]);

		if ((size_t)stat.st_size < sizeof(struct store))
			err(EX_DATAERR, "file '%s' too short to have header", argv[i]);

		D[i - 1].addr = mmap(NULL, sizeof(struct store), PROT_READ, MAP_SHARED, D[i - 1].fd, 0);
		if (D[i - 1].addr == MAP_FAILED)
			err(EX_OSERR, "mmap(hdr, '%s')", argv[i]);

		struct store *store = D[i - 1].addr;

		if (store->magic != MAGIC && bswap_32(store->magic) != MAGIC)
			errx(EX_DATAERR, "incorrrect magic %08x in '%s'", store->magic, argv[i]);

		if (store->version != 0)
			errx(EX_DATAERR, "unknown version %08x in '%s'", store->version, argv[i]);

		switch (store->v0.type) {
		case INT:
		case UINT:
		case FLOAT:
		case CHAR:
			break;
		default:
			errx(EX_DATAERR, "unknown type %d in '%s'", store->v0.type, argv[i]);
		}

		switch (store->v0.pow2) {
		case B16:
		case B32:
		case B64:
		case B128:
			break;
		default:
			errx(EX_DATAERR, "unknown pow2 %d in '%s'", store->v0.pow2, argv[i]);
		}

		D[i - 1].width = POW2LEN(store->v0.pow2);
		D[i - 1].nrec = (stat.st_size - sizeof(struct store)) / D[i - 1].width;

		if (D[i - 1].nrec == 0)
			errx(EX_DATAERR, "no data in '%s'", argv[i]);

		if (D[i - 1].nrec < nrec)
			nrec = D[i - 1].nrec;

		if (munmap(D[i - 1].addr, sizeof(struct store)))
			err(EX_OSERR, "munmap(hdr)");
	}

	for (int i = 0; i < argc - 1; i++) {
		D[i].nrec = nrec;

		D[i].addr = mmap(NULL, nrec * D[i].width, PROT_READ|PROT_WRITE, MAP_PRIVATE, D[i].fd, sizeof(struct store));
		if (D[i].addr == MAP_FAILED)
			err(EX_OSERR, "mmap(dat)");

		errno = posix_madvise(D[i].addr, nrec * D[i].width, MADV_SEQUENTIAL|MADV_WILLNEED);
		if (errno)
			warn("posix_madvise()");
	}

	engine_run(&program, argc - 1, D);

	if (getenv("NODISP"))
		return 0;

	float *d = D[0].addr;
	for (uint64_t i = 0; i < D[0].nrec; i++)
		printf("%f\n", d[i]);

	for (int i = 0; i < argc - 1; i++) {
		if (munmap(D[i].addr, nrec * D[i].width))
			err(EX_OSERR, "munmap(dat)");

		close(D[i].fd);
	}

	return 0;
}
