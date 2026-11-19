/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "logstamp.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct { int fd; bool at_start; } stamp_out;

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static stamp_out g_out[2];

static void put(int fd, const char *b, size_t n)
{
	while (n > 0) {
		ssize_t w = write(fd, b, n);

		if (w <= 0) return;   /* a log that can't be written isn't worth a hang */
		b += w;
		n -= (size_t)w;
	}
}

static void stamp(int fd)
{
	struct timespec up;
	time_t now = time(NULL);
	struct tm tm;
	struct timespec rt;
	char b[48];
	int n;

	clock_gettime(CLOCK_BOOTTIME, &up);
	clock_gettime(CLOCK_REALTIME, &rt);
	localtime_r(&now, &tm);
	n = snprintf(b, sizeof b, "[%02d:%02d:%02d.%03ld +%ld.%01lds] ",
	             tm.tm_hour, tm.tm_min, tm.tm_sec, rt.tv_nsec / 1000000,
	             (long)up.tv_sec, up.tv_nsec / 100000000);
	if (n > 0) put(fd, b, (size_t)n);
}

static ssize_t stamp_write(void *cookie, const char *buf, size_t size)
{
	stamp_out *o = cookie;
	size_t i = 0;

	pthread_mutex_lock(&g_mx);
	while (i < size) {
		const char *nl = memchr(buf + i, '\n', size - i);
		size_t len = nl ? (size_t)(nl - (buf + i)) + 1 : size - i;

		if (o->at_start) stamp(o->fd);
		put(o->fd, buf + i, len);
		o->at_start = nl != NULL;
		i += len;
	}
	pthread_mutex_unlock(&g_mx);
	return (ssize_t)size;
}

void logstamp_install(void)
{
	static bool done;
	cookie_io_functions_t io = { .write = stamp_write };
	FILE *f;
	int k;

	if (done) return;
	done = true;
	for (k = 0; k < 2; k++) {
		g_out[k].fd = k + 1;   /* stdout, stderr */
		g_out[k].at_start = true;
		f = fopencookie(&g_out[k], "w", io);
		if (!f) continue;
		/* unbuffered: each printf reaches the file at once, as stderr's always did */
		setvbuf(f, NULL, _IONBF, 0);
		if (k == 0) stdout = f; else stderr = f;
	}
}
