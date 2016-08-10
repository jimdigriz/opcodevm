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
#include <stdlib.h>

#include "column.h"

#define DISPATCH_INIT_PARAMS	struct column *C, const unsigned int i
#define DISPATCH_FINI_PARAMS	struct column *C, const unsigned int i
#define DISPATCH_GET_PARAMS	struct column *C, const unsigned int i
#define DISPATCH_PUT_PARAMS	struct column *C, const unsigned int i

struct dispatch {
	void		(*init)(DISPATCH_INIT_PARAMS);
	void		(*fini)(DISPATCH_FINI_PARAMS);
	unsigned int	(*get)(DISPATCH_GET_PARAMS);
	void		(*put)(DISPATCH_PUT_PARAMS);
};

static long long int instances;
static unsigned int stride;

static void * backed_spool(void *arg)
{
	struct column *C = arg;

	long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	unsigned int dlen = C->width / 8 * stride;
	unsigned int blen = dlen + pagesize - (dlen % pagesize);

	unsigned char *b;
	unsigned int o = 0;
	int i = -1;

	while (1) {
		if (o == 0)
			b = (unsigned char *)C->backed.ring + (++i % (instances + 1)) * blen;

		int n = read(C->backed.fd, b + o, dlen - o);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			err(EX_OSERR, "read('%s')", C->backed.path);
		} else if (n == 0)
			break;

		o += n % dlen;
	}

	return NULL;
}

static void backed_init(DISPATCH_INIT_PARAMS)
{
	if (!C[i].width)
		errx(EX_USAGE, "C[i].width");

	long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	C[i].backed.fd = open(C[i].backed.path, O_RDONLY);
	if (C[i].backed.fd == -1)
		err(EX_NOINPUT, "open('%s')", C[i].backed.path);
	if (fstat(C[i].backed.fd, &C[i].backed.stat) == -1)
		err(EX_NOINPUT, "fstat('%s')", C[i].backed.path);

	if (C[i].backed.stat.st_size < C[i].backed.offset)
		errx(EX_USAGE, "offset after EOF for '%s'", C[i].backed.path);

	C[i].backed.nrecs = (C[i].backed.stat.st_size - C[i].backed.offset) * 8 / C[i].width;

	errno = posix_fadvise(C[i].backed.fd, C[i].backed.offset, C[i].width * C[i].nrecs / 8, POSIX_FADV_SEQUENTIAL);
	if (errno)
		warn("posix_fadvise('%s', POSIX_FADV_SEQUENTIAL)", C[i].backed.path);
	errno = posix_fadvise(C[i].backed.fd, C[i].backed.offset, C[i].width * C[i].nrecs / 8, POSIX_FADV_NOREUSE);
	if (errno)
		warn("posix_fadvise('%s', POSIX_FADV_NOREUSE)", C[i].backed.path);

	unsigned int blen = C[i].width / 8 * stride;
	blen += pagesize - (blen % pagesize);

	C[i].backed.ring = mmap(NULL, blen, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (C[i].backed.ring == MAP_FAILED)
		err(EX_OSERR, "mmap()");
	for (unsigned int j = 1; j < instances + 1; j++)
		if (mmap((unsigned char *)C[i].backed.ring + j * blen, blen, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) == MAP_FAILED)
			err(EX_OSERR, "mmap(MAP_FIXED)");

	C[i].addr = NULL;
	C[i].backed.ringi = 0;
	pthread_mutex_init(&C[i].backed.ringilk, NULL);

	if (pthread_create(&C[i].backed.thread, NULL, backed_spool, &C[i]))
		err(EX_OSERR, "pthread_create(backed_spool, '%s')", C[i].backed.path);
}

static void backed_fini(DISPATCH_FINI_PARAMS)
{
	pthread_mutex_destroy(&C[i].backed.ringilk);

	if (munmap(C[i].backed.ring, (instances + 1) * C[i].width / 8 * stride))
		 err(EX_OSERR, "munmap()");
	C[i].addr = NULL;

	if (close(C[i].backed.fd) == -1)
		err(EX_OSERR, "close('%s')", C->backed.path);
	C->backed.fd = -1;
}

static unsigned int backed_get(DISPATCH_GET_PARAMS)
{
	unsigned int ringi;

	pthread_mutex_lock(&C[i].backed.ringilk);
	ringi = C[i].backed.ringi;
	C[i].backed.ringi = (C[i].backed.ringi + 1) % (instances + 1);
	pthread_mutex_unlock(&C[i].backed.ringilk);

	C[i].addr = &((unsigned char *)C[i].backed.ring)[ringi * C[i].width / 8 * stride];

	return stride;
}

static void backed_put(DISPATCH_PUT_PARAMS)
{
}

static struct dispatch dispatch[] = {
	[BACKED]	= {
		.init	= backed_init,
		.fini	= backed_fini,
		.get	= backed_get,
		.put	= backed_put,
	},
};

void column_init(struct column *C, long long int insts)
{
	instances = insts;
	stride = 100000;	// FIXME

	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		dispatch[C[i].ctype].init(C, i);
}

void column_fini(struct column *C)
{
	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		dispatch[C[i].ctype].fini(C, i);
}

unsigned int column_get(struct column *C)
{
	unsigned int nrecs = UINT_MAX;

	for (unsigned int i = 0; C[i].ctype != VOID; i++) {
		unsigned int n = dispatch[C[i].ctype].get(C, i);
		if (n < nrecs)
			nrecs = n;
	}

	assert(nrecs != UINT_MAX);

	return nrecs;
}

void column_put(struct column *C)
{
	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		dispatch[C[i].ctype].put(C, i);
}
