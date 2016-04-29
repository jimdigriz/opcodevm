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

#include "column.h"

struct dispatch {
	void		(*init)(struct column *C);
	void		(*fini)(struct column *C);
	unsigned int	 (*get)(struct column *C, const unsigned int o, const unsigned int s);
	void		 (*put)(struct column *C);
};

static unsigned int zero_get(struct column *C, const unsigned int o, const unsigned int s)
{
	(void)o;

	C->nrecs = s;

	C->addr = mmap(NULL, C->width * C->nrecs / 8, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (C->addr == MAP_FAILED)
		err(EX_OSERR, "mmap(MAP_ANONYMOUS)");

	return C->nrecs;
}

static void zero_put(struct column *C)
{
	if (!C->addr)
		return;

	if (munmap(C->addr, C->width * C->nrecs / 8))
		err(EX_OSERR, "munmap(MAP_ANONYMOUS)");
	C->addr = NULL;
}

static void backed_init(struct column *C)
{
	if (!C->width)
		errx(EX_USAGE, "C->width");

	C->backed.fd = open(C->backed.path, O_RDONLY);
	if (C->backed.fd == -1)
		err(EX_NOINPUT, "open('%s')", C->backed.path);
	if (fstat(C->backed.fd, &C->backed.stat) == -1)
		err(EX_NOINPUT, "fstat('%s')", C->backed.path);

	if (C->backed.stat.st_size < C->backed.offset)
		errx(EX_USAGE, "offset after EOF for '%s'", C->backed.path);

	C->backed.nrecs = (C->backed.stat.st_size - C->backed.offset) * 8 / C->width;
}

static void backed_fini(struct column *C)
{
	if (close(C->backed.fd) == -1)
		err(EX_OSERR, "close('%s')", C->backed.path);
	C->backed.fd = -1;
}

static unsigned int backed_get(struct column *C, const unsigned int o, const unsigned int s)
{
	if (o >= C->backed.nrecs)
		return 0;

	C->nrecs = (o + s < C->backed.nrecs) ? s : C->backed.nrecs - o;

	C->addr = mmap(NULL, C->width * C->nrecs / 8, PROT_READ|PROT_WRITE, MAP_PRIVATE, C->backed.fd, C->backed.offset + C->width * o / 8);
	if (C->addr == MAP_FAILED)
		err(EX_OSERR, "mmap('%s')", C->backed.path);

	errno = posix_madvise(C->addr, C->width * C->nrecs / 8, POSIX_MADV_WILLNEED);
	if (errno)
		warn("posix_madvise('%s', POSIX_MADV_WILLNEED)", C->backed.path);
	errno = posix_madvise(C->addr, C->width * C->nrecs / 8, POSIX_MADV_SEQUENTIAL);
	if (errno)
		warn("posix_madvise('%s', POSIX_MADV_SEQUENTIAL)", C->backed.path);

	return C->nrecs;
}

static void backed_put(struct column *C)
{
	if (!C->addr)
		return;

	if (munmap(C->addr, C->width * C->nrecs / 8))
		err(EX_OSERR, "munmap('%s')", C->backed.path);
	C->addr = NULL;
}

static struct dispatch dispatch[] = {
	[ZERO]		= {
		.get	= zero_get,
		.put	= zero_put,
	},
	[BACKED]	= {
		.init	= backed_init,
		.fini	= backed_fini,
		.get	= backed_get,
		.put	= backed_put,
	},
};

#define METHOD(x)	void column_##x(struct column *C)				\
			{								\
				for (struct column *c = C; c->ctype != VOID; c++)	\
					if (dispatch[c->ctype].x)			\
						dispatch[c->ctype].x(c);		\
			}

METHOD(init)
METHOD(fini)

unsigned int column_get(struct column *C, unsigned int * const offset, pthread_mutex_t * const offsetlk, const unsigned int s)
{
	unsigned int nrecs = UINT_MAX;

	pthread_mutex_lock(offsetlk);
	unsigned int o = *offset;
	*offset += s;
	pthread_mutex_unlock(offsetlk);

	for (struct column *c = C; c->ctype != VOID; c++) {
		if (dispatch[c->ctype].get) {
			unsigned int n = dispatch[c->ctype].get(c, o, s);
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
