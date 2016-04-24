#include <unistd.h>
#include <err.h>
#include <inttypes.h>
#include <sysexits.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/queue.h>
#include <limits.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>

#include "engine.h"

struct engine_instance_info {
	struct program	*program;
	unsigned int	instance;
	pthread_t	thread;
	pthread_mutex_t	*compile;
};

static int (*opcode[256])(OPCODE_PARAMS);

static const char *libs[] = {
	/* first load the code points (order not important) */
	"code/list.so",
	"code/bswap.so",

	/* now the ops */
	"code/bswap/c.so",
	"code/bswap/x86_64.so",

	NULL,
};

static long instances = 1;
static long pagesize, length;

SLIST_HEAD(opcode_list, opcode) opcode_list = SLIST_HEAD_INITIALIZER(opcode_list);

void engine_opcode_init(struct opcode *opcode)
{
	struct opcode *np;
	SLIST_FOREACH(np, &opcode_list, opcode)
		if (!strcmp(opcode->name, np->name))
			errx(EX_SOFTWARE, "duplicate %s opcode calling init()", opcode->name);
	SLIST_INSERT_HEAD(&opcode_list, opcode, opcode);
}

void engine_opcode_imp_init(const char *name, const void *args)
{
	struct opcode *np;
	SLIST_FOREACH(np, &opcode_list, opcode) {
		if (!strcmp(name, np->name)) {
			np->hook(args);
			return;
		}
	}
	errx(EX_SOFTWARE, "engine_opcode_imp_init(%s)", name);
}

void engine_init_columns(struct column *columns)
{
	int fd;
	struct stat stat;

	for (struct column *C = columns; C->ctype != VOID; C++) {
		switch (C->ctype) {
		case MEMORY:
			if (C->width == 0)
				errx(EX_USAGE, "MEMORY requires width");
			if (C->nrecs == 0)
				errx(EX_USAGE, "MEMORY requires nrecs");
			C->addr = mmap(NULL, C->width / 8 * C->nrecs, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
			if (C->addr == MAP_FAILED)
				err(EX_OSERR, "mmap()");
			break;
		case MAPPED:
			if (C->width == 0)
				errx(EX_USAGE, "MAPPED requires width");
			fd = open(C->mapped.path, O_RDONLY);
			if (fd == -1)
				err(EX_NOINPUT, "open('%s')", C->mapped.path);
			if (fstat(fd, &stat) == -1)
				err(EX_NOINPUT, "fstat('%s')", C->mapped.path);
			C->nrecs = (stat.st_size - C->mapped.offset) / C->width * 8;
			C->addr = mmap(NULL, C->width / 8 * C->nrecs, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, C->mapped.offset);
			if (C->addr == MAP_FAILED)
				err(EX_OSERR, "mmap()");
			close(fd);
			break;
		default:
			errx(EX_USAGE, "unknown column ctype");
		}

		errno = posix_madvise(C->addr, C->width / 8 * C->nrecs, POSIX_MADV_SEQUENTIAL);
		if (errno)
			warn("posix_madvise(POSIX_MADV_SEQUENTIAL)");
		errno = posix_madvise(C->addr, C->width / 8 * C->nrecs, POSIX_MADV_WILLNEED);
		if (errno)
			warn("posix_madvise(POSIX_MADV_WILLNEED)");
	}
}

void engine_fini_columns(struct column *columns)
{
	for (struct column *C = columns; C->addr; C++) {
		if (munmap(C->addr, C->width / 8 * C->nrecs))
			err(EX_OSERR, "munmap()");
		C->addr = NULL;
		if (C->ctype == MAPPED)
			C->nrecs = 0;
	}
}

static void engine_free_columns(const struct column *columns, const unsigned int o, const unsigned int n)
{
	for (const struct column *C = columns; C->addr; C++) {
		errno = posix_madvise((uintptr_t *)C->addr + C->width / 8 * o, C->width / 8 * n, POSIX_MADV_DONTNEED);
		if (errno)
			warn("posix_madvise(MADV_DONTNEED)");
	}
}

void engine_init() {
	errno = 0;

	if (getenv("INSTANCES"))
		instances = strtol(getenv("INSTANCES"), NULL, 10);
	if (errno == ERANGE || instances < 0)
		err(EX_DATAERR, "invalid INSTANCES");
	if (instances == 0)
		instances = sysconf(_SC_NPROCESSORS_ONLN);
	if (instances == -1)
		err(EX_OSERR, "sysconf(_SC_NPROCESSORS_ONLN)");

	pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	if (getenv("LENGTH"))
		length = strtol(getenv("LENGTH"), NULL, 10);
	if (errno == ERANGE || length < 0)
		err(EX_DATAERR, "invalid LENGTH");
	if (length == 0) {
		length = sysconf(_SC_LEVEL2_CACHE_SIZE);
		if (length == -1)
			err(EX_OSERR, "sysconf(_SC_LEVEL2_CACHE_SIZE)");

		/* default half of L2 cache to not saturate it */
		length /= 2;
	}

	for (const char **l = libs; *l; l++) {
		void *handle = dlopen(*l, RTLD_NOW|RTLD_LOCAL);
		if (!handle)
			errx(EX_SOFTWARE, "dlopen(%s): %s\n", *l, dlerror());
	}

	unsigned int bytecode = 1;	/* 'ret' is at 0 */
	struct opcode *np;
	SLIST_FOREACH(np, &opcode_list, opcode) {
		assert(bytecode < 256);
		opcode[bytecode] = np->func;
		np->bytecode = bytecode;
		bytecode++;
	}
}

static void * engine_instance(void *arg)
{
	struct engine_instance_info *eii = arg;

	struct insn *ip;
	int jmp;
	unsigned int o, e;
#	define CALL(x)	assert(opcode[x]);					\
			jmp = opcode[x](eii->program->columns, o, e, &ip->ops);	\
			assert(jmp != 0);						
	/* http://www.complang.tuwien.ac.at/forth/threading/ : direct */
#	define NEXT	ip = &ip[jmp];						\
			goto *((uintptr_t)&&bytecode0 + ip->code);

	unsigned int nrecs = UINT_MAX;
	unsigned int rowwidth = 0;
	for (const struct column *C = eii->program->columns; C->addr; C++) {
		if (nrecs > C->nrecs)
			nrecs = C->nrecs;
		rowwidth += C->width;
	}

	const unsigned int stride = (length * 8 / rowwidth - ((length * 8 / rowwidth) % (pagesize * 8 / rowwidth)));
	assert(stride * rowwidth / 8 >= (unsigned int)pagesize);

	goto compile;
compile_ret:

	for (o = eii->instance * stride; o < nrecs; o += instances * stride) {
		const unsigned int n = (nrecs - o < stride) ? nrecs - o : stride;
		e = o + n;

		ip = &eii->program->insns[0];
		jmp = 0;
#		pragma GCC diagnostic push
#		pragma GCC diagnostic ignored "-Wpedantic"
		NEXT
#		pragma GCC diagnostic pop
ret:
		/* FIXME: combo of o and n might mean we madvise non-full pages */
		engine_free_columns(eii->program->columns, o, n);
	}

	return NULL;

	/* here is our bytecode jumptable */
bytecode0:
	goto ret;
#	include "jumptable.h"

	/* compiler placed here to get access to the cf table from jumptable.h */
compile:
	if (pthread_mutex_trylock(eii->compile)) {
		pthread_mutex_lock(eii->compile);
		goto compile_finish;
	}
	if (eii->program->insns[0].code)
		goto compile_finish;

	for (unsigned int i = 0; i < eii->program->len; i++) {
		struct opcode *np;
		SLIST_FOREACH(np, &opcode_list, opcode) {
			if (!strcmp(eii->program->insns[i].name, np->name)) {
				eii->program->insns[i].code = cf[np->bytecode];
				break;
			}
		}
	}

compile_finish:
	pthread_mutex_unlock(eii->compile);
	goto compile_ret;
}

void engine_run(struct program *program)
{
	engine_init_columns(program->columns);

	struct engine_instance_info *eii = calloc(instances, sizeof(struct engine_instance_info));
	pthread_mutex_t compile = PTHREAD_MUTEX_INITIALIZER;

	for (unsigned int i = 0; i < instances; i++) {
		eii[i].program	= program;
		eii[i].compile	= &compile;
		eii[i].instance	= i;

		if (instances == 1) {
			engine_instance(&eii[i]);
		} else {
			if (pthread_create(&eii[i].thread, NULL, engine_instance, &eii[i]))
				err(EX_OSERR, "pthread_create()");
		}
	}

	if (instances > 1) {
		for (int i = 0; i < instances; i++) {
			errno =  pthread_join(eii[i].thread, NULL);
			if (errno)
				err(EX_OSERR, "pthread_join()");
		}
	}

	free(eii);

	engine_fini_columns(program->columns);
}
