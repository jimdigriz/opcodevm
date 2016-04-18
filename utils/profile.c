#include <stdio.h>
#include <dlfcn.h>
#include <err.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/queue.h>

#include "engine.h"
#include "engine-hooks.h"

SLIST_HEAD(opcode_list, opcode) opcode_list = SLIST_HEAD_INITIALIZER(opcode_list);

static long pagesize, l2_cache_size;

static void profile_engine_init() {
	pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	l2_cache_size = sysconf(_SC_LEVEL2_CACHE_SIZE);
	if (l2_cache_size == -1)
		err(EX_OSERR, "sysconf(_SC_LEVEL2_CACHE_SIZE)");
}

int main(int argc, char **argv)
{
	if (argc != 3)
		errx(EX_USAGE, "%s code/<opcode>.so code/<opcode>/<imp>.so", argv[0]);

	for (int i = 1; i < argc; i++) {
		void *handle = dlopen(argv[i], RTLD_NOW|RTLD_LOCAL);
		if (!handle)
			errx(EX_SOFTWARE, "dlopen(%s): %s\n", argv[i], dlerror());
	}

	profile_engine_init();

	SLIST_FIRST(&opcode_list)->profile_init(l2_cache_size / 2, pagesize, 4);

	SLIST_FIRST(&opcode_list)->profile();

	SLIST_FIRST(&opcode_list)->profile_fini();

	return 0;
}
