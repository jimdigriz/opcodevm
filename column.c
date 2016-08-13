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
#include <semaphore.h>
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

#define SEM_WAIT(x, y, z)	do { 								\
					int n = sem_wait(x);					\
					if (n == -1) {						\
						if (errno == EINTR)				\
							continue;				\
						err(EX_OSERR, "sem_wait('%s', "#y")", z);	\
					} else if (n == 0)					\
						break;						\
				} while (1)

static void * backed_spool(void *arg)
{
	struct column *C = arg;

	long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	unsigned int dlen = C->width / 8 * stride;

	unsigned char *b = C->backed.ring;
	unsigned int o = 0;

	int n;
	do {
		n = read(C->backed.fd, b + o, dlen - o);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			err(EX_OSERR, "read('%s')", C->backed.path);
		}

		o += n % dlen;
		if (o)
			continue;

		// https://github.com/angrave/SystemProgramming/wiki/Synchronization%2C-Part-8%3A-Ring-Buffer-Example#correct-implementation-of-a-ring-buffer
		SEM_WAIT(&C->backed.ring_has_room, ring_as_room, C->backed.path);
		errno = pthread_mutex_lock(&C->backed.ringlk);
		if (errno)
			err(EX_OSERR, "pthread_mutex_lock('%s')", C->backed.path);
		b = (unsigned char *)C->backed.ring + (C->backed.ring_in++ % (instances + 1)) * C->backed.blen;
		errno = pthread_mutex_unlock(&C->backed.ringlk);
		if (errno)
			err(EX_OSERR, "pthread_mutex_unlock('%s')", C->backed.path);
		if (sem_post(&C->backed.ring_has_data) == -1)
			err(EX_OSERR, "sem_post('%s', ring_has_data)", C->backed.path);
	} while (n);	// n == 0 on EOF

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

	C[i].backed.blen = C[i].width / 8 * stride;
	C[i].backed.blen += pagesize - (C[i].backed.blen % pagesize);

	C[i].backed.ring = mmap(NULL, C[i].backed.blen, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (C[i].backed.ring == MAP_FAILED)
		err(EX_OSERR, "mmap()");
	for (unsigned int j = 1; j < instances + 1; j++)
		if (mmap((unsigned char *)C[i].backed.ring + j * C[i].backed.blen, C[i].backed.blen, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
			err(EX_OSERR, "mmap(MAP_FIXED)");

	C[i].addr = NULL;
	C[i].backed.ring_in = 0;
	C[i].backed.ring_out = 0;
	errno = pthread_mutex_init(&C[i].backed.ringlk, NULL);
	if (errno)
		err(EX_OSERR, "pthread_mutex_init('%s')", C[i].backed.path);
	assert(instances + 1 < SEM_VALUE_MAX);
	if (sem_init(&C[i].backed.ring_has_data, 0, 0) == -1)
		err(EX_OSERR, "sem_init('%s', ring_has_data)", C[i].backed.path);
	if (sem_init(&C[i].backed.ring_has_room, 0, instances + 1) == -1)
		err(EX_OSERR, "sem_init('%s', ring_has_room)", C[i].backed.path);

	errno = pthread_create(&C[i].backed.thread, NULL, backed_spool, &C[i]);
	if (errno)
		err(EX_OSERR, "pthread_create(backed_spool, '%s')", C[i].backed.path);
	// FIXME pthread_setaffinity_np()
}

static void backed_fini(DISPATCH_FINI_PARAMS)
{
	sem_destroy(&C[i].backed.ring_has_data);
	sem_destroy(&C[i].backed.ring_has_room);
	pthread_mutex_destroy(&C[i].backed.ringlk);

	if (munmap(C[i].backed.ring, (instances + 1) * C[i].width / 8 * stride))
		 err(EX_OSERR, "munmap()");
	C[i].addr = NULL;

	if (close(C[i].backed.fd) == -1)
		err(EX_OSERR, "close('%s')", C->backed.path);
	C->backed.fd = -1;
}

static unsigned int backed_get(DISPATCH_GET_PARAMS)
{
	SEM_WAIT(&C[i].backed.ring_has_data, ring_has_data, C[i].backed.path);
	pthread_mutex_lock(&C[i].backed.ringlk);
	C[i].addr = (unsigned char *)C[i].backed.ring + (C[i].backed.ring_out++ % (instances + 1)) * C[i].backed.blen;
	pthread_mutex_unlock(&C[i].backed.ringlk);

	return stride;
}

static void backed_put(DISPATCH_PUT_PARAMS)
{
	if (sem_post(&C[i].backed.ring_has_room) == -1)
		err(EX_OSERR, "sem_post('%s', ring_has_room)", C[i].backed.path);
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
