/*
 * Deterministic allocation-failure tests.
 * See LICENSE for copying information.
 */
#include "event2/event-config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event2/buffer.h"
#include "event2/event.h"

#ifdef EVENT_SET_MEM_FUNCTIONS_IMPLEMENTED
static int tracking;
static unsigned allocation_count;
static unsigned fail_at;
static int injected;

static int
fail_allocation(void)
{
	if (tracking && ++allocation_count == fail_at) {
		injected = 1;
		errno = ENOMEM;
		return 1;
	}
	return 0;
}

static void *
test_malloc(size_t size)
{
	return fail_allocation() ? NULL : malloc(size);
}

static void *
test_realloc(void *ptr, size_t size)
{
	/* A failed realloc must leave the original allocation intact. */
	return fail_allocation() ? NULL : realloc(ptr, size);
}

static void
start_tracking(unsigned nth)
{
	allocation_count = 0;
	fail_at = nth;
	injected = 0;
	tracking = 1;
}

#define CHECK(condition)                                                       \
	do {                                                                       \
		if (!(condition)) {                                                    \
			fprintf(stderr, "%s:%d: %s (allocation %u)\n", __FILE__, __LINE__, \
				#condition, nth);                                              \
			goto end;                                                          \
		}                                                                      \
	} while (0)

static int
test_buffer_new(unsigned nth)
{
	struct evbuffer *buffer;
	int result = 1;

	start_tracking(nth);
	buffer = evbuffer_new();
	tracking = 0;
	CHECK(buffer != NULL || injected);
	if (!buffer)
		buffer = evbuffer_new();
	CHECK(buffer != NULL);
	CHECK(evbuffer_add(buffer, "data", 4) == 0);
	CHECK(evbuffer_get_length(buffer) == 4);
	result = 0;
end:
	if (buffer)
		evbuffer_free(buffer);
	return result;
}

static int
test_buffer_expand(unsigned nth)
{
	struct evbuffer *buffer = NULL;
	char contents[8192];
	char copy[sizeof(contents)];
	int status;
	int result = 1;

	memset(contents, 'x', sizeof(contents));
	buffer = evbuffer_new();
	CHECK(buffer != NULL);
	CHECK(evbuffer_add(buffer, "data", 4) == 0);
	start_tracking(nth);
	status = evbuffer_expand(buffer, sizeof(contents));
	tracking = 0;
	CHECK(status == 0 || (status == -1 && injected));
	/* Expansion must preserve the data, including when it fails. */
	CHECK(evbuffer_get_length(buffer) == 4);
	CHECK(evbuffer_copyout(buffer, copy, 4) == 4);
	CHECK(memcmp(copy, "data", 4) == 0);
	CHECK(evbuffer_expand(buffer, sizeof(contents)) == 0);
	CHECK(evbuffer_add(buffer, contents, sizeof(contents)) == 0);
	CHECK(evbuffer_drain(buffer, 4) == 0);
	CHECK(evbuffer_remove(buffer, copy, sizeof(copy)) == (int)sizeof(copy));
	CHECK(memcmp(copy, contents, sizeof(copy)) == 0);
	result = 0;
end:
	if (buffer)
		evbuffer_free(buffer);
	return result;
}

static void
timer_cb(evutil_socket_t fd, short events, void *arg)
{
	int *called = arg;
	(void)fd;
	(void)events;
	++*called;
}

static int
test_event_new(unsigned nth)
{
	struct event_base *base = NULL;
	struct event *event = NULL;
	int called = 0;
	int result = 1;

	base = event_base_new();
	CHECK(base != NULL);
	start_tracking(nth);
	event = evtimer_new(base, timer_cb, &called);
	tracking = 0;
	CHECK(event != NULL || injected);
	if (!event)
		event = evtimer_new(base, timer_cb, &called);
	CHECK(event != NULL);
	event_active(event, EV_TIMEOUT, 0);
	CHECK(event_base_loop(base, EVLOOP_NONBLOCK) >= 0);
	CHECK(called == 1);
	result = 0;
end:
	if (event)
		event_free(event);
	if (base)
		event_base_free(base);
	return result;
}

static int
test_timer_add(unsigned nth)
{
	struct event_base *base = NULL;
	struct event *events[64] = {NULL};
	struct timeval delay = {60, 0};
	unsigned i, added;
	int called[64] = {0};
	int status = 0;
	int result = 1;

	base = event_base_new();
	CHECK(base != NULL);
	for (i = 0; i < 64; ++i) {
		events[i] = evtimer_new(base, timer_cb, &called[i]);
		CHECK(events[i] != NULL);
	}
	/* Grow a nonempty timeout heap, exercising realloc of live storage. */
	start_tracking(nth);
	for (added = 0; added < 64; ++added) {
		status = evtimer_add(events[added], &delay);
		if (status != 0)
			break;
	}
	tracking = 0;
	CHECK(status == 0 || (status == -1 && injected));
	for (i = 0; i < 64; ++i) {
		CHECK(!!evtimer_pending(events[i], NULL) == (i < added));
		CHECK(evtimer_add(events[i], &delay) == 0);
		CHECK(evtimer_pending(events[i], NULL));
	}
	for (i = 0; i < 64; ++i)
		event_active(events[i], EV_TIMEOUT, 0);
	CHECK(event_base_loop(base, EVLOOP_NONBLOCK) >= 0);
	for (i = 0; i < 64; ++i) {
		CHECK(called[i] == 1);
		CHECK(!evtimer_pending(events[i], NULL));
	}
	result = 0;
end:
	for (i = 0; i < 64; ++i) {
		if (events[i])
			event_free(events[i]);
	}
	if (base)
		event_base_free(base);
	return result;
}

struct scenario {
	const char *name;
	int (*run)(unsigned);
};

int
main(void)
{
	static const struct scenario scenarios[] = {
		{"evbuffer_new", test_buffer_new},
		{"evbuffer_expand", test_buffer_expand},
		{"event_new", test_event_new},
		{"timer_add", test_timer_add},
	};
	unsigned i, nth, count;
	int result = 1;
#ifdef _WIN32
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
		return 1;
#endif

	/* Install once, before any allocating Libevent call. */
	event_set_mem_functions(test_malloc, test_realloc, free);
	for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
		if (scenarios[i].run(0))
			goto end;
		count = allocation_count;
		if (!count) {
			fprintf(
				stderr, "%s: no allocations exercised\n", scenarios[i].name);
			goto end;
		}
		/* Replay each allocation reached by the successful control. */
		for (nth = 1; nth <= count; ++nth) {
			printf("%s: failing allocation %u/%u\n", scenarios[i].name, nth,
				count);
			fflush(stdout);
			if (scenarios[i].run(nth))
				goto end;
			if (!injected) {
				fprintf(stderr, "%s: requested failure was not reached\n",
					scenarios[i].name);
				goto end;
			}
		}
		if (scenarios[i].run(0))
			goto end;
	}
	result = 0;
end:
	tracking = 0;
	libevent_global_shutdown();
#ifdef _WIN32
	WSACleanup();
#endif
	return result;
}

#else

int
main(void)
{
	puts("SKIP: memory replacement is disabled");
	return 77;
}

#endif
