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

#include "engine.h"

struct engine_instance_info {
	struct program	*program;
	struct column	*columns;
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

long long instances;
unsigned int stride = 100000;	// FIXME

void engine_init()
{
	errno = 0;

	instances = 1;
	if (getenv("INSTANCES"))
		instances = strtoll(getenv("INSTANCES"), NULL, 10);
	if (errno == ERANGE || instances < 0)
		err(EX_USAGE, "invalid INSTANCES");
	if (instances == 0)
		instances = sysconf(_SC_NPROCESSORS_ONLN);
	if (instances == -1)
		err(EX_OSERR, "sysconf(_SC_NPROCESSORS_ONLN)");

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
#	define CALL(x)	assert(opcode[x]);				\
			jmp = opcode[x](eii->columns, n, &ip->ops);	\
			assert(jmp != 0);						
	/* http://www.complang.tuwien.ac.at/forth/threading/ : direct */
#	define NEXT	ip = &ip[jmp]; goto *ip->code;

	goto compile;
compiled_already:

	do {
		n = column_get(eii->columns);
		if (n == 0)
			goto ret;

		ip = &eii->program->insns[0];
		jmp = 0;
#		pragma GCC diagnostic push
#		pragma GCC diagnostic ignored "-Wpedantic"
		NEXT
#		pragma GCC diagnostic pop
ret:
		column_put(eii->columns);
	} while (n);

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

	unsigned int nC = 0;
	while (program->columns[nC++].ctype != VOID)
		;

	for (int i = 0; i < instances; i++) {
		eii[i].program	= program;

		eii[i].columns	= malloc(nC * sizeof(struct column));
		if (!eii[i].columns)
			err(EX_OSERR, "malloc()");
		memcpy(eii[i].columns, program->columns, nC * sizeof(struct column));

		/* program compile run */
		if (i == 0)
			engine_instance(&eii[i]);

		errno = pthread_create(&eii[i].thread, NULL, engine_instance, &eii[i]);
		if (errno)
			err(EX_OSERR, "pthread_create()");
		if (instances > 1) {
			CPU_ZERO(&cpuset);
			CPU_SET(i, &cpuset);
			errno = pthread_setaffinity_np(eii[i].thread, sizeof(cpuset), &cpuset);
			if (errno)
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

	column_fini(program->columns);
}
