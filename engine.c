#include <unistd.h>
#include <err.h>
#include <inttypes.h>
#include <sysexits.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/queue.h>

#include "engine.h"
#include "engine-hooks.h"

SLIST_HEAD(opcode_list, opcode) opcode_list = SLIST_HEAD_INITIALIZER(opcode_list);

static void (*opcode[256])(OPCODE_PARAMS);

#define MAX_PROG_LEN 100

static pthread_mutex_t bytecode_mutex = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t *bytecode[MAX_PROG_LEN];

static const char *libs[] = {
	/* first load the code points (order not important) */
	"code/bswap.so",

	/* now the ops (order is important, descending by 'speed') */
//	"code/bswap/opencl.so",
//	"code/bswap/x86_64.so",
	"code/bswap/c.so",
	NULL,
};

static long instances = 1;
static long pagesize, l2_cache_size;

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

	l2_cache_size = sysconf(_SC_LEVEL2_CACHE_SIZE);
	if (l2_cache_size == -1)
		err(EX_OSERR, "sysconf(_SC_LEVEL2_CACHE_SIZE)");

	for (const char **l = libs; *l; l++) {
		void *handle = dlopen(*l, RTLD_NOW|RTLD_LOCAL);
		if (!handle)
			errx(EX_SOFTWARE, "dlopen(%s): %s\n", *l, dlerror());
	}

	struct opcode *np;
	SLIST_FOREACH(np, &opcode_list, opcode)
		if (!strcmp(np->name, "bswap"))
			opcode[1] = np->func;
	assert(opcode[1]);
}

struct engine_instance_info {
	struct program	*program;
	struct data	*D;
	unsigned int	nD;
	pthread_t	thread;
	unsigned int	instance;
};

static void * engine_instance(void *arg)
{
	struct engine_instance_info *eii = arg;

	struct data *d = calloc(eii->nD, sizeof(struct data));
	if (!d)
		errx(EX_OSERR, "calloc(d)");

	size_t minrec = UINT64_MAX;
	size_t width = 0;
	for (unsigned int i = 0; i < eii->nD; i++) {
		d[i].width	= eii->D[i].width;
		d[i].type	= eii->D[i].type;

		if (eii->D[i].nrec < minrec)
			minrec = eii->D[i].nrec;

		width += eii->D[i].width;
	}

	size_t nrec, offset;
	/* we divide by two so not to saturate the cache (pagesize aligned for OpenCL) */
	const uint64_t stride = (l2_cache_size / width / 2)
					- ((l2_cache_size / width / 2) % pagesize);
	assert(stride >= pagesize);

	/* forgive me god, for I have sinned, lolz */
	uintptr_t **ip;
#	define CALL(x)	assert(opcode[x]);			\
			offset = 0;				\
			opcode[x](&offset, nrec, &d, 0, 0, 0);	\
			assert(offset == nrec)
	/* http://www.complang.tuwien.ac.at/forth/threading/ : direct */
#	define NEXT goto **ip++

	goto compile;
compile_ret:

	for (uint64_t pos = eii->instance * stride; pos < minrec; pos += instances * stride) {
		nrec = ((minrec - pos) > stride) ? stride : minrec - pos;

		for (unsigned int i = 0; i < eii->nD; i++) {
			d[i].addr	= &((uint8_t *)eii->D[i].addr)[pos * eii->D[i].width];
			d[i].nrec	= nrec;
		}

		ip = &bytecode[0];
#		pragma GCC diagnostic push
#		pragma GCC diagnostic ignored "-Wpedantic"
		NEXT;
#		pragma GCC diagnostic pop
ret:
		continue;
	}

	free(d);

	return NULL;

	/* here is our bytecode jumptable */
bytecode0:
	goto ret;
#	include "jumptable.h"

	/* compiler placed here to get access to the cf table from jumptable.h */
compile:
	/* lame */
	if (pthread_mutex_trylock(&bytecode_mutex)) {
		pthread_mutex_lock(&bytecode_mutex);
		goto compile_finish;
	}
	if (bytecode[0])
		goto compile_finish;

#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wpedantic"
	opcode[0] = &&bytecode0;
#	pragma GCC diagnostic pop

	bytecode[0] = cf[1];
	bytecode[1] = cf[0];

compile_finish:
	pthread_mutex_unlock(&bytecode_mutex);
	goto compile_ret;
}

void engine_run(struct program *program, size_t nD, struct data *D)
{
	assert(program->loop->len < MAX_PROG_LEN);

	if (strcmp(program->loop->insns[program->loop->len - 1].code, "ret"))
		errx(EX_USAGE, "program needs to end with 'ret'");

	if (nD == 0)
		errx(EX_USAGE, "nD needs to be greater than 0");

	for (size_t i = 0; i < nD; i++)
		if ((uintptr_t)D[i].addr % pagesize)
			errx(EX_SOFTWARE, "D[%zu] is not page aligned", i);

	struct engine_instance_info *eii = calloc(instances, sizeof(struct engine_instance_info));

	for (unsigned int i = 0; i < instances; i++) {
		eii[i].program	= program;
		eii[i].nD	= nD;
		eii[i].D	= D;
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
}
