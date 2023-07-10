/**
 * Time measurement helper functions
 * 
 * VERSION HISTORY
 * 
 * 2023-06-20 Removed unused commented out code (timeval_t definition and timeval_t based functions).
 *            Removed usec_p definition (only used by time_mark_ms()), usec_t* is enough.
 *            Added time_between_ms() function.
 * unknown    Basic time_now() and time_mark_ms() functions with usec_t.
 *            Old left over timeval_t based functions (but they were commented out) .
 */

#pragma once

#define _GNU_SOURCE
//#define _POSIX_C_SOURCE 199309L
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

typedef int64_t usec_t;


static inline usec_t timeval_to_usec(struct timeval time) {
	return time.tv_sec * 1000000L + time.tv_usec;
}

static inline struct timeval usec_to_timeval(usec_t time) {
	return (struct timeval){
		.tv_sec  = time / 1000000L,
		.tv_usec = time % 1000000L
	};
}

static inline usec_t time_now() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return timeval_to_usec(now);
}

/**
 * Returns the time elapsed since `mark` in milliseconds and sets `mark` to the current time.
 * Meant to be used to measure continuous operations, e.g. time to calculate frames.
 */
static inline double time_mark_ms(usec_t* mark) {
	usec_t now = time_now();
	double elapsed = (now - *mark) / 1000.0;
	*mark = now;
	return elapsed;
}

static inline double time_between_ms(usec_t a, usec_t b) {
	return (b - a) / 1000.0;
}


// 
// Functions to query used CPU time on Linux and Windows (sum of kernel and user time)
// Based on Mysticial answer on https://stackoverflow.com/questions/17432502/how-can-i-measure-cpu-time-and-wall-clock-time-on-both-linux-windows#answer-17440673
// 

#ifdef _WIN32

// Windows version
usec_t time_process_cpu_time() {
	FILETIME creation_time, exit_time, kernel_time, user_time;
	GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time);
	uint64_t cpu_time_in_100ns_units = (kernel_time.dwLowDateTime | (uint64_t)kernel_time.dwHighDateTime << 32) + (user_time.dwLowDateTime | (uint64_t)user_time.dwHighDateTime << 32);
	return cpu_time_in_100ns_units / 10;
}

#else

// Linux version
usec_t time_process_cpu_time() {
	/**
	 * clock() returns kernel + user time
	 * Found here: https://arstechnica.com/civis/threads/measuring-time-on-linux-best-way-to-measure-user-sys-cpu-usage.1091018/
	 * man clock: Uses clock_gettime with CLOCK_PROCESS_CPUTIME_ID
	 * man clock_gettime: Description of CLOCK_PROCESS_CPUTIME_ID is ambigous
	 * man timer_create: States that CLOCK_PROCESS_CPUTIME_ID is user and system time
	 * Posts in forum topic also say that the C code adds kernel and user time
	 * CLOCKS_PER_SEC = 100 on my system (/usr/src/linux-headers-5.15.0-67/include/asm-generic/param.h)
	 */
	//uint64_t cpu_time_in_10ms_units = clock();
	//return cpu_time_in_10ms_units * 10000;
	//return (double)clock() / CLOCKS_PER_SEC;
	
	/**
	 * Initially used clock() but it only returns time in 10ms units (CLOCKS_PER_SEC = 100 on my system).
	 * Hence moved to clock_gettime() directly. Seems like it has 1ns resolution.
	 * clock_getres(CLOCK_PROCESS_CPUTIME_ID, …) output was 1ns on my system.
	 */
	struct timespec process_cpu_time;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &process_cpu_time);
	return process_cpu_time.tv_sec * 1000000ul + process_cpu_time.tv_nsec / 1000ul;
}

#endif