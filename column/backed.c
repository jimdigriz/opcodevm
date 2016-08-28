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

#include "global.h"
#include "utils.h"
#include "column.h"

static void * backed_spool(void *arg)
{
	struct column *C = arg;

	unsigned int dlen = C->width / 8 * stride;
	// sneaky hook to push empty blocks into ring
	// causes i calls to read() below returning EOF
	unsigned int i = instances + 1;

	// https://github.com/angrave/SystemProgramming/wiki/Synchronization%2C-Part-8%3A-Ring-Buffer-Example#correct-implementation-of-a-ring-buffer
	do {
		int n;
		unsigned char *b;
		struct ringblkinfo *info;
		unsigned int o = 0;

		EINTRSAFE(sem_wait, &C->backed.ring->has_room);

		b = (unsigned char *)C->backed.ring->addr + (C->backed.ring->in++ % (instances + 1)) * C->backed.ring->blen;
		info = (struct ringblkinfo *)(b + (dlen + (C->backed.ring->pagesize - (dlen % C->backed.ring->pagesize))));

		do {
			n = EINTRSAFE(read, C->backed.fd, b + o, dlen - o);
			o += n;
		} while (n && o < dlen);

		if (n == 0)
			i--;

		info->nrecs = o * 8 / C->width;

		if (sem_post(&C->backed.ring->has_data) == -1)
			err(EX_OSERR, "sem_post('%s', ring_has_data)", C->backed.path);
	} while (i);

	return NULL;
}

void backed_init(DISPATCH_INIT_PARAMS)
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

	errno = posix_fadvise(C[i].backed.fd, C[i].backed.offset, C[i].width / 8 * C[i].backed.nrecs, POSIX_FADV_SEQUENTIAL);
	if (errno)
		warn("posix_fadvise('%s', POSIX_FADV_SEQUENTIAL)", C[i].backed.path);
	errno = posix_fadvise(C[i].backed.fd, C[i].backed.offset, C[i].width / 8 * C[i].backed.nrecs, POSIX_FADV_NOREUSE);
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

void backed_fini(DISPATCH_FINI_PARAMS)
{
	sem_destroy(&C[i].backed.ring->has_data);
	sem_destroy(&C[i].backed.ring->has_room);
	pthread_mutex_destroy(&C[i].backed.ring->lock);

	if (munmap(C[i].backed.ring->addr, (instances + 1) * C[i].backed.ring->blen))
		 err(EX_OSERR, "munmap()");

	free(C[i].backed.ring);

	C[i].addr = NULL;

	if (close(C[i].backed.fd) == -1)
		err(EX_OSERR, "close('%s')", C[i].backed.path);
	C[i].backed.fd = -1;
}

unsigned int backed_get(DISPATCH_GET_PARAMS)
{
	unsigned int dlen = C[i].width / 8 * stride;
	struct ringblkinfo *info;

	EINTRSAFE(sem_wait, &C[i].backed.ring->has_data);
	errno = pthread_mutex_lock(&C[i].backed.ring->lock);
	if (errno)
		err(EX_OSERR, "pthread_mutex_lock('%s')", C[i].backed.path);
	C[i].addr = (unsigned char *)C[i].backed.ring->addr + (C[i].backed.ring->out++ % (instances + 1)) * C[i].backed.ring->blen;
	errno = pthread_mutex_unlock(&C[i].backed.ring->lock);
	if (errno)
		err(EX_OSERR, "pthread_mutex_unlock('%s')", C[i].backed.path);

	info = (struct ringblkinfo *)((unsigned char *)C[i].addr + (dlen + (C[i].backed.ring->pagesize - (dlen % C[i].backed.ring->pagesize))));

	return info->nrecs;
}

void backed_put(DISPATCH_PUT_PARAMS)
{
	if (sem_post(&C[i].backed.ring->has_room) == -1)
		err(EX_OSERR, "sem_post('%s', ring_has_room)", C[i].backed.path);
	C[i].addr = NULL;
}
