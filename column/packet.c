#include <err.h>
#include <sysexits.h>
#include <stdlib.h>
#include <pcap/pcap.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "global.h"
#include "column.h"

#define CTYPE(x) packet_##x

void CTYPE(init)(DISPATCH_INIT_PARAMS)
{
	char errbuf[PCAP_ERRBUF_SIZE];

	if (!C[i].width)
		errx(EX_USAGE, "C[i].width");

	assert(instances == 1);	// pcap_dispatch() fails

	C[i].packet.pcap = pcap_open_offline(C[i].packet.path, errbuf);
	if (!C[i].packet.pcap)
		errx(EX_NOINPUT, "pcap_open_offline('%s'): %s", C[i].packet.path, errbuf);

	C[i].addr = NULL;
}

void CTYPE(fini)(DISPATCH_FINI_PARAMS)
{
	pcap_close(C[i].packet.pcap);

	C[i].addr = NULL;
}

static void CTYPE(cb)(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes)
{
	struct column *C = (struct column *)user;
	char *dst = C->addr;

	dst += C->width / 8 * C->packet.nrecs;

	memcpy(dst, bytes, h->caplen);

	C->packet.nrecs++;
}

unsigned int CTYPE(get)(DISPATCH_GET_PARAMS)
{
	int ret;

	errno = posix_memalign(&C[i].addr, pagesize, stride * C[i].width / 8);
	if (errno)
		err(EX_OSERR, "posix_memalign()");

	C[i].packet.nrecs = 0;

	ret = pcap_dispatch(C[i].packet.pcap, stride, CTYPE(cb), (u_char *)&C[i]);
	if (ret == -1)
		errx(EX_SOFTWARE, "pcap_dispatch('%s'): %s", C[i].packet.path, pcap_geterr(C[i].packet.pcap));

	return C[i].packet.nrecs;
}

void CTYPE(put)(DISPATCH_PUT_PARAMS)
{
	free(C[i].addr);

	C[i].addr = NULL;
}
