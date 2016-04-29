#define _GNU_SOURCE
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
#include <fcntl.h>
#include <stdio.h>

#include "engine.h"

typedef struct {
	unsigned int	offset;
	pthread_mutex_t	offsetlk;
} offset_t;

struct engine_instance_info {
	struct program	*program;
	struct column	*columns;
	unsigned int	stride;
	offset_t	*offset;
	pthread_t	thread;
};

static int (*opcode[OPCODES_MAX])(OPCODE_PARAMS);

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

static struct opcode opcode_ret = {
	.name	= "ret",
};

void engine_init() {
	errno = 0;

	if (getenv("INSTANCES"))
		instances = strtol(getenv("INSTANCES"), NULL, 10);
	if (errno == ERANGE || instances < 0)
		err(EX_USAGE, "invalid INSTANCES");
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
		err(EX_USAGE, "invalid LENGTH");
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

	/* dummy 'ret' so slip it in at the top to give it a 0 code */
	engine_opcode_init(&opcode_ret);

	unsigned int bytecode = 0;
	struct opcode *np;
	SLIST_FOREACH(np, &opcode_list, opcode) {
		assert(bytecode < OPCODES_MAX);
		opcode[bytecode] = np->func;
		np->bytecode = bytecode;
		bytecode++;
	}

	/* make sure 'ret' has code 0 */
	assert(!strcmp(SLIST_FIRST(&opcode_list)->name, "ret"));
	assert(SLIST_FIRST(&opcode_list)->bytecode == 0);
}

static void * engine_instance(void *arg)
{
	struct engine_instance_info *eii = arg;

	struct insn *ip;
	int jmp;
	unsigned int n;
#	define CALL(x)	assert(opcode[x]);					\
			jmp = opcode[x](eii->columns, n, &ip->ops);	\
			assert(jmp != 0);						
	/* http://www.complang.tuwien.ac.at/forth/threading/ : direct */
#	define NEXT	ip = &ip[jmp]; goto *ip->code;

	goto compile;
compiled_already:

	while (1) {
		n = column_get(eii->columns, &eii->offset->offset, &eii->offset->offsetlk, eii->stride);
		if (n == 0) {
			column_put(eii->columns);
			break;
		}

		ip = &eii->program->insns[0];
		jmp = 0;
#		pragma GCC diagnostic push
#		pragma GCC diagnostic ignored "-Wpedantic"
		NEXT
#		pragma GCC diagnostic pop
ret:
		column_put(eii->columns);
	}

	return NULL;

	/* here is our bytecode jumptable */
bytecode0:
	goto ret;
#	include "jumptable.h"

	/* compiler placed here to get access to the cf table from jumptable.h */
compile:
	if (eii->program->insns[0].code)
		goto compiled_already;

	assert(strcmp(eii->program->insns[0].name, "ret"));

	for (unsigned int i = 0; i < eii->program->len; i++) {
		struct opcode *np;
		SLIST_FOREACH(np, &opcode_list, opcode) {
			if (!strcmp(eii->program->insns[i].name, np->name)) {
#				pragma GCC diagnostic push
#				pragma GCC diagnostic ignored "-Wpedantic"
				eii->program->insns[i].code = (uintptr_t)&&bytecode0 + cf[np->bytecode];
#				pragma GCC diagnostic pop
				break;
			}
		}
	}

	return NULL;
}

void engine_run(struct program *program)
{
	cpu_set_t cpuset;
	struct engine_instance_info *eii = calloc(instances, sizeof(struct engine_instance_info));
	if (!eii)
		err(EX_OSERR, "calloc()");

	column_init(program->columns);

	unsigned int nC = 1;	/* we have a VOID terminator */
	unsigned int rowwidth = 0;
	for (struct column *C = program->columns; C->ctype != VOID; C++) {
		nC++;
		rowwidth += C->width;
	}
	const unsigned int stride = length * 8 / rowwidth;
	assert(stride > 100);

	offset_t *offset = malloc(sizeof(offset_t));
	if (!offset)
		err(EX_OSERR, "malloc()");
	offset->offset = 0;
	pthread_mutex_init(&offset->offsetlk, NULL);

	for (int i = 0; i < instances; i++) {
		eii[i].offset	= offset;
		eii[i].program	= program;
		eii[i].stride	= stride;

		eii[i].columns	= malloc(nC * sizeof(struct column));
		if (!eii[i].columns)
			err(EX_OSERR, "malloc()");
		memcpy(eii[i].columns, program->columns, nC * sizeof(struct column));

		/* program compile run */
		if (i == 0)
			engine_instance(&eii[i]);

		if (pthread_create(&eii[i].thread, NULL, engine_instance, &eii[i]))
			err(EX_OSERR, "pthread_create()");
		if (instances > 1) {
			CPU_ZERO(&cpuset);
			CPU_SET(i, &cpuset);
			if (pthread_setaffinity_np(eii[i].thread, sizeof(cpuset), &cpuset))
				warn("pthread_setaffinity_np()");
		}
	}

	for (int i = 0; i < instances; i++) {
		errno =  pthread_join(eii[i].thread, NULL);
		if (errno)
			err(EX_OSERR, "pthread_join()");

		free(eii[i].columns);
	}

	free(eii);
	pthread_mutex_destroy(&offset->offsetlk);
	free(offset);

	column_fini(program->columns);
}
