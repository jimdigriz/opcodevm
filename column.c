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
#include <signal.h>

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

	unsigned int dlen = C->width / 8 * stride;
	unsigned int i = instances + 1;	// sneaky hook to push empty blocks into ring

	// https://github.com/angrave/SystemProgramming/wiki/Synchronization%2C-Part-8%3A-Ring-Buffer-Example#correct-implementation-of-a-ring-buffer
	do {
		int n;
		unsigned char *b;
		struct ringblkinfo *info;
		unsigned int o = 0;

		SEM_WAIT(&C->backed.ring->has_room, ring_has_room, C->backed.path);

		b = (unsigned char *)C->backed.ring->addr + (C->backed.ring->in++ % (instances + 1)) * C->backed.ring->blen;

		do {
			n = read(C->backed.fd, b + o, dlen - o);
			if (n == 0)
				i--;
			else if (n == -1) {
				if (errno == EINTR)
					continue;
				err(EX_OSERR, "read('%s')", C->backed.path);
			}

			o += n;
		} while (n && o < dlen);

		info = (struct ringblkinfo *)(b + (dlen + (C->backed.ring->pagesize - (dlen % C->backed.ring->pagesize))));
		info->nrecs = o * 8 / C->width;

		if (sem_post(&C->backed.ring->has_data) == -1)
			err(EX_OSERR, "sem_post('%s', ring_has_data)", C->backed.path);
	} while (i);

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

	errno = posix_fadvise(C[i].backed.fd, C[i].backed.offset, C[i].width * C[i].backed.nrecs / 8, POSIX_FADV_SEQUENTIAL);
	if (errno)
		warn("posix_fadvise('%s', POSIX_FADV_SEQUENTIAL)", C[i].backed.path);
	errno = posix_fadvise(C[i].backed.fd, C[i].backed.offset, C[i].width * C[i].backed.nrecs / 8, POSIX_FADV_NOREUSE);
	if (errno)
		warn("posix_fadvise('%s', POSIX_FADV_NOREUSE)", C[i].backed.path);

	C[i].backed.ring = malloc(sizeof(struct ring));
	if (!C[i].backed.ring)
		err(EX_OSERR, "malloc(struct ring)");

	C[i].backed.ring->in = 0;
	C[i].backed.ring->out = 0;

	C[i].backed.ring->pagesize = sysconf(_SC_PAGESIZE);
	if (C[i].backed.ring->pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	C[i].backed.ring->blen = C[i].width / 8 * stride;
	C[i].backed.ring->blen += C[i].backed.ring->pagesize - (C[i].backed.ring->blen % C[i].backed.ring->pagesize);

	C[i].backed.ring->blen += sizeof(struct ringblkinfo);
	C[i].backed.ring->blen += C[i].backed.ring->pagesize - (C[i].backed.ring->blen % C[i].backed.ring->pagesize);

	C[i].backed.ring->addr = mmap(NULL, (instances + 1) * C[i].backed.ring->blen, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (C[i].backed.ring->addr == MAP_FAILED)
		err(EX_OSERR, "mmap()");

	C[i].addr = NULL;

	errno = pthread_mutex_init(&C[i].backed.ring->lock, NULL);
	if (errno)
		err(EX_OSERR, "pthread_mutex_init('%s')", C[i].backed.path);
	assert(instances + 1 < SEM_VALUE_MAX);
	if (sem_init(&C[i].backed.ring->has_data, 0, 0) == -1)
		err(EX_OSERR, "sem_init('%s', ring_has_data)", C[i].backed.path);
	if (sem_init(&C[i].backed.ring->has_room, 0, instances + 1) == -1)
		err(EX_OSERR, "sem_init('%s', ring_has_room)", C[i].backed.path);

	errno = pthread_create(&C[i].backed.ring->thread, NULL, backed_spool, &C[i]);
	if (errno)
		err(EX_OSERR, "pthread_create(backed_spool, '%s')", C[i].backed.path);
	// FIXME pthread_setaffinity_np()
}

static void backed_fini(DISPATCH_FINI_PARAMS)
{
	sem_destroy(&C[i].backed.ring->has_data);
	sem_destroy(&C[i].backed.ring->has_room);
	pthread_mutex_destroy(&C[i].backed.ring->lock);

	if (munmap(C[i].backed.ring->addr, (instances + 1) * C[i].backed.ring->blen))
		 err(EX_OSERR, "munmap()");

	free(C[i].backed.ring);

	C[i].addr = NULL;

	if (close(C[i].backed.fd) == -1)
		err(EX_OSERR, "close('%s')", C->backed.path);
	C->backed.fd = -1;
}

static unsigned int backed_get(DISPATCH_GET_PARAMS)
{
	unsigned int dlen = C->width / 8 * stride;
	struct ringblkinfo *info;

	SEM_WAIT(&C[i].backed.ring->has_data, ring_has_data, C[i].backed.path);
	errno = pthread_mutex_lock(&C[i].backed.ring->lock);
	if (errno)
		err(EX_OSERR, "pthread_mutex_lock('%s')", C[i].backed.path);
	C[i].addr = (unsigned char *)C[i].backed.ring->addr + (C[i].backed.ring->out++ % (instances + 1)) * C[i].backed.ring->blen;
	errno = pthread_mutex_unlock(&C[i].backed.ring->lock);
	if (errno)
		err(EX_OSERR, "pthread_mutex_unlock('%s')", C[i].backed.path);

	info = (struct ringblkinfo *)((unsigned char *)C[i].addr + (dlen + (C->backed.ring->pagesize - (dlen % C[i].backed.ring->pagesize))));

	return info->nrecs;
}

static void backed_put(DISPATCH_PUT_PARAMS)
{
	if (sem_post(&C[i].backed.ring->has_room) == -1)
		err(EX_OSERR, "sem_post('%s', ring_has_room)", C[i].backed.path);
	C[i].addr = NULL;
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
