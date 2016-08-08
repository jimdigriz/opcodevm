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
#define DISPATCH_GET_PARAMS	struct column *C, const unsigned int i, const unsigned int s, const unsigned int o
#define DISPATCH_PUT_PARAMS	struct column *C, const unsigned int i

struct dispatch {
	void		(*init)(DISPATCH_INIT_PARAMS);
	void		(*fini)(DISPATCH_FINI_PARAMS);
	unsigned int	(*get)(DISPATCH_GET_PARAMS);
	void		(*put)(DISPATCH_PUT_PARAMS);
};

static long long int ringsize, buflen, chunklen;
static unsigned int stride;

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

	C[i].addr = NULL;
	C[i].backed.lfd = -1;
}

static void backed_fini(DISPATCH_FINI_PARAMS)
{
	if (munmap(C[i].addr, C[i].width * stride / 8))
		 err(EX_OSERR, "munmap()");
	C[i].addr = NULL;

// FIXME
//	if (close(C[i].backed.lfd) == -1)
//		err(EX_OSERR, "close('%s' [lfd])", C->backed.path);
//	C->backed.lfd = -1;

	if (close(C[i].backed.fd) == -1)
		err(EX_OSERR, "close('%s')", C->backed.path);
	C->backed.fd = -1;
}

static unsigned int backed_get(DISPATCH_GET_PARAMS)
{
	if (o >= C[i].backed.nrecs)
		return 0;

	if (!C[i].addr) {
		C[i].addr = mmap(NULL, C[i].width * stride / 8, PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
		if (C[i].addr == MAP_FAILED)
			err(EX_OSERR, "mmap()");

		C[i].backed.lfd = dup(C[i].backed.fd);
		if (C[i].backed.lfd == -1)
			err(EX_OSERR, "dup('%s')", C[i].backed.path);
	}

	if (lseek(C[i].backed.lfd, o * C[i].width / 8, SEEK_SET) == -1)
		err(EX_OSERR, "lseek('%s')", C[i].backed.path);

	C[i].nrecs = (o + s < C[i].backed.nrecs) ? s : C[i].backed.nrecs - o;

	int len = C[i].width * C[i].nrecs / 8;

	unsigned int r = 0;
	do {
		int n = read(C[i].backed.lfd, C[i].addr, len);
		if (n == -1 && errno != EINTR)
			errx(EX_OSERR, "read('%s')", C[i].backed.path);
		else if (n == 0)
			errx(EX_SOFTWARE, "read('%s') hit EOF", C[i].backed.path);
		r += n;
	} while (len - r > 0);

	return C[i].nrecs;
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

static unsigned int	offset;
static pthread_mutex_t	offsetlk;

void column_init(struct column *C)
{
	errno = 0;

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

	offset = 0;
	stride = 100000;	// FIXME
	pthread_mutex_init(&offsetlk, NULL);

	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		dispatch[C[i].ctype].init(C, i);
}

void column_fini(struct column *C)
{
	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		dispatch[C[i].ctype].fini(C, i);

	pthread_mutex_destroy(&offsetlk);
}

unsigned int column_get(struct column *C)
{
	unsigned int nrecs = UINT_MAX;
	unsigned int o;

	pthread_mutex_lock(&offsetlk);
	o = offset;
	offset += stride;
	pthread_mutex_unlock(&offsetlk);

	for (unsigned int i = 0; C[i].ctype != VOID; i++) {
		unsigned int n = dispatch[C[i].ctype].get(C, i, stride, o);
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
