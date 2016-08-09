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
static long pagesize;
static long long int ringsize, buflen, chunklen;
static unsigned int stride;

static void * backed_spool(void *arg)
{
	struct column *C = arg;

	unsigned int len = (instances + 1) * C->width / 8 * stride;
	unsigned char *ptr = C->backed.ring;
	int n;

	while (1) {
		n = read(C->backed.fd, ptr, pagesize);
		if (n == -1) {
			if (errno == EINTR || errno == EFAULT)
				continue;
			err(EX_OSERR, "read('%s')", C->backed.path);
		} else if (n == 0)
			break;

		assert(n == pagesize);

		if (mprotect(ptr, pagesize, PROT_READ))
			err(EX_OSERR, "mprotect('%s', PROT_READ)", C->backed.path);

		ptr += n;
		if (ptr == (unsigned char *)C->backed.ring + len)
			ptr = C->backed.ring;
	}

	return NULL;
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

	errno = posix_fadvise(C[i].backed.fd, C[i].backed.offset, C[i].width * C[i].nrecs / 8, POSIX_FADV_SEQUENTIAL);
	if (errno)
		warn("posix_fadvise('%s', POSIX_FADV_SEQUENTIAL)", C[i].backed.path);
	errno = posix_fadvise(C[i].backed.fd, C[i].backed.offset, C[i].width * C[i].nrecs / 8, POSIX_FADV_NOREUSE);
	if (errno)
		warn("posix_fadvise('%s', POSIX_FADV_NOREUSE)", C[i].backed.path);

	C[i].backed.ring = mmap(NULL, (instances + 1) * C[i].width / 8 * stride, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (C[i].backed.ring == MAP_FAILED)
		err(EX_OSERR, "mmap()");

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
	if (mprotect(C[i].addr, C[i].nrecs * C[i].width / 8, PROT_WRITE))
		err(EX_OSERR, "mprotect('%s', PROT_WRITE)", C[i].backed.path);
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
	errno = 0;

	pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	ringsize = 3;
	if (getenv("RINGSIZE"))
		ringsize = strtoll(getenv("RINGSIZE"), NULL, 10);
	if (errno == ERANGE || ringsize < 0)
		err(EX_USAGE, "invalid RINGSIZE");
	if (ringsize < 3)
		errx(EX_USAGE, "RINGSIZE cannot be less than 3");

	buflen = 32 * 1024 * 1024;
	if (getenv("BUFLEN"))
		buflen = strtoll(getenv("BUFLEN"), NULL, 10);
	if (errno == ERANGE || buflen < 0)
		err(EX_USAGE, "invalid BUFLEN");

	chunklen = 0;
	if (getenv("CHUNKLEN"))
		chunklen = strtoll(getenv("CHUNKLEN"), NULL, 10);
	if (errno == ERANGE || chunklen < 0)
		err(EX_USAGE, "invalid CHUNKLEN");
	if (chunklen == 0) {
		chunklen = sysconf(_SC_LEVEL2_CACHE_SIZE);
		if (chunklen == -1)
			err(EX_OSERR, "sysconf(_SC_LEVEL2_CACHE_SIZE)");

		/* default half of L2 cache to not saturate it */
		chunklen /= 2;
	}

	if (chunklen >= buflen)
		errx(EX_USAGE, "CHUNKLEN >= BUFLEN");

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
