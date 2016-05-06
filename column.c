#include <err.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>

#include "column.h"

#define DISPATCH_INIT_PARAMS	struct column *C, const unsigned int i
#define DISPATCH_FINI_PARAMS	struct column *C
#define DISPATCH_GET_PARAMS	struct column *C, const unsigned int s, const unsigned int o, const unsigned int i
#define DISPATCH_PUT_PARAMS	struct column *C

struct dispatch {
	void		(*init)(DISPATCH_INIT_PARAMS);
	void		(*fini)(DISPATCH_FINI_PARAMS);
	unsigned int	 (*get)(DISPATCH_GET_PARAMS);
	void		 (*put)(DISPATCH_PUT_PARAMS);
};

static void generic_put(DISPATCH_PUT_PARAMS)
{
	if (!C->addr)
		return;

	if (munmap(C->addr, C->width * C->nrecs / 8))
		err(EX_OSERR, "munmap(MAP_ANONYMOUS)");
	C->addr = NULL;
}

static unsigned int zero_get(DISPATCH_GET_PARAMS)
{
	(void)o;

	C[i].nrecs = s;

	C[i].addr = mmap(NULL, C[i].width * C[i].nrecs / 8, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (C[i].addr == MAP_FAILED)
		err(EX_OSERR, "mmap(MAP_ANONYMOUS)");

	return C[i].nrecs;
}

static void backed_init(DISPATCH_INIT_PARAMS)
{
	if (!C[i].width)
		errx(EX_USAGE, "C[i].width");

	C[i].backed.fd = open(C[i].backed.path, O_RDONLY);
	if (C[i].backed.fd == -1)
		err(EX_NOINPUT, "open('%s')", C[i].backed.path);
	if (fstat(C[i].backed.fd, &C[i].backed.stat) == -1)
		err(EX_NOINPUT, "fstat('%s')", C[i].backed.path);

	if (C[i].backed.stat.st_size < C[i].backed.offset)
		errx(EX_USAGE, "offset after EOF for '%s'", C[i].backed.path);

	C[i].backed.nrecs = (C[i].backed.stat.st_size - C[i].backed.offset) * 8 / C[i].width;
}

static void backed_fini(DISPATCH_FINI_PARAMS)
{
	if (close(C->backed.fd) == -1)
		err(EX_OSERR, "close('%s')", C->backed.path);
	C->backed.fd = -1;
}

static unsigned int backed_get(DISPATCH_GET_PARAMS)
{
	if (o >= C[i].backed.nrecs)
		return 0;

	C[i].nrecs = (o + s < C[i].backed.nrecs) ? s : C[i].backed.nrecs - o;

	C[i].addr = mmap(NULL, C[i].width * C[i].nrecs / 8, PROT_READ|PROT_WRITE, MAP_PRIVATE, C[i].backed.fd, C[i].backed.offset + C[i].width * o / 8);
	if (C[i].addr == MAP_FAILED)
		err(EX_OSERR, "mmap('%s')", C[i].backed.path);

	errno = posix_madvise(C[i].addr, C[i].width * C[i].nrecs / 8, POSIX_MADV_WILLNEED);
	if (errno)
		warn("posix_madvise('%s', POSIX_MADV_WILLNEED)", C[i].backed.path);
	errno = posix_madvise(C[i].addr, C[i].width * C[i].nrecs / 8, POSIX_MADV_SEQUENTIAL);
	if (errno)
		warn("posix_madvise('%s', POSIX_MADV_SEQUENTIAL)", C[i].backed.path);

	return C[i].nrecs;
}

static unsigned int cast_get(DISPATCH_GET_PARAMS)
{
	(void)s;
	(void)o;

	assert(C[C[i].cast.src].nrecs > 0);

	assert(C[i].width % 8 == 0);
	assert(C[C[i].cast.src].width % 8 == 0);

	C[i].nrecs = C[C[i].cast.src].nrecs;

	C[i].addr = mmap(NULL, C[i].width * C[i].nrecs / 8, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (C[i].addr == MAP_FAILED)
		err(EX_OSERR, "mmap(MAP_ANONYMOUS)");

	const uint64_t *S64;
	const uint32_t *S32;
	const uint16_t *S16;
	const uint8_t *S = C[C[i].cast.src].addr;
	S += C[i].cast.offset / 8;

	uint64_t *D64;
	uint32_t *D32;
	uint16_t *D16;
	uint8_t *D = C[i].addr;

	const unsigned int stride = C[C[i].cast.src].width / 8;

	for (unsigned int j = 0; j < C[i].nrecs; j++) {
		switch (C[i].width) {
		case 64: 	S64 = (uint64_t *)S;
				D64 = (uint64_t *)D;
				*D64++ = ((S64++)[j * stride] >> C[i].cast.shift) & C[i].cast.mask;
				break;
		case 32: 	S32 = (uint32_t *)S;
				D32 = (uint32_t *)D;
				*D32++ = ((S32++)[j * stride] >> C[i].cast.shift) & C[i].cast.mask;
				break;
		case 16: 	S16 = (uint16_t *)S;
				D16 = (uint16_t *)D;
				*D16++ = ((S16++)[j * stride] >> C[i].cast.shift) & C[i].cast.mask;
				break;
		case  8: 	*D++ = ((S++)[j * stride] >> C[i].cast.shift) & C[i].cast.mask;
				break;
		default:	errx(EX_USAGE, "CAST unknown width");
		}
	}

	return C[i].nrecs;
}

static struct dispatch dispatch[] = {
	[ZERO]		= {
		.get	= zero_get,
		.put	= generic_put,
	},
	[BACKED]	= {
		.init	= backed_init,
		.fini	= backed_fini,
		.get	= backed_get,
		.put	= generic_put,
	},
	[CAST]		= {
		.get	= cast_get,
		.put	= generic_put,
	},
};

void column_init(struct column *C)
{
	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		if (dispatch[C[i].ctype].init)
			dispatch[C[i].ctype].init(C, i);
}

void column_fini(struct column *C)
{
	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		if (dispatch[C[i].ctype].fini)
			dispatch[C[i].ctype].fini(C);
}

unsigned int column_get(struct column *C, const unsigned int s, unsigned int * const offset, pthread_mutex_t * const offsetlk)
{
	unsigned int nrecs = UINT_MAX;

	pthread_mutex_lock(offsetlk);
	unsigned int o = *offset;
	*offset += s;
	pthread_mutex_unlock(offsetlk);

	for (unsigned int i = 0; C[i].ctype != VOID; i++) {
		if (dispatch[C[i].ctype].get) {
			unsigned int n = dispatch[C[i].ctype].get(C, s, o, i);
			if (n < nrecs)
				nrecs = n;
		}
	}

	assert(nrecs != UINT_MAX);

	return nrecs;
}

void column_put(struct column *C)
{
	for (struct column *c = C; c->ctype != VOID; c++)
		if (dispatch[c->ctype].put)
			dispatch[c->ctype].put(c);
}
