/* Explicit guest timeval layout; host pointers never escape this boundary. */
#define _DEFAULT_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
typedef struct { int64_t seconds, microseconds; } GuestTimeval;
typedef struct { int32_t minuteswest, dsttime; } GuestTimezone;
static ABI int guest_gettimeofday(GuestTimeval *value,GuestTimezone *zone) {
    struct timeval t;
#ifdef _WIN32
    if (gettimeofday(&t,NULL)) { perror("STOP: host gettimeofday failed"); exit(21); }
    if (zone) { zone->minuteswest=(int32_t)(-compat_utc_offset((time_t)t.tv_sec)/60); zone->dsttime=0; }
#else
    if (gettimeofday(&t,NULL)) {
        perror("Runtime: host gettimeofday failed");
        return -1;
    }
    if (zone) {
        time_t sec=t.tv_sec;
        struct tm tm_val;
        localtime_r(&sec,&tm_val);
        zone->minuteswest=(int32_t)(-tm_val.tm_gmtoff/60);
        zone->dsttime=tm_val.tm_isdst;
    }
#endif
    if (value) { value->seconds=t.tv_sec; value->microseconds=t.tv_usec; }
    return 0;
}
uintptr_t runtime_time_resolve(const char *name) {
    if (!strcmp(name,"n88vx3C5nW8#I#J")) return (uintptr_t)guest_gettimeofday;
    return 0;
}
