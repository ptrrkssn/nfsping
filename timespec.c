/*
 * timespec.c
 *
 * Copyright (c) 2025 Peter Eriksson <pen@lysator.liu.se>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timespec.h"

#define NSEC_PER_SEC 1000000000

void
timespec_normalise(struct timespec *tsp)
{
    while (tsp->tv_nsec >= NSEC_PER_SEC) {
        tsp->tv_sec++;
        tsp->tv_nsec -= NSEC_PER_SEC;
    }

    while (tsp->tv_nsec <= -NSEC_PER_SEC) {
        tsp->tv_sec--;
        tsp->tv_nsec += NSEC_PER_SEC;
    }

    if (tsp->tv_nsec < 0) {
        /* Negative nanoseconds isn't valid according to POSIX.
         * Decrement tv_sec and roll tv_nsec over.
         */
        tsp->tv_sec--;
        tsp->tv_nsec = (NSEC_PER_SEC + tsp->tv_nsec);
    }
}


double
timespec2double(struct timespec *tp) {
    double d;

    d = tp->tv_sec+(tp->tv_nsec/1000000000.0);
    return d;
}


double
timespec_diff(struct timespec *tp1,
              struct timespec *tp0) {
    return timespec2double(tp1)-timespec2double(tp0);
}


char *
timespec2str(struct timespec *tp,
             char *buf,
             size_t bufsize,
             int f_verbose) {
    static char sbuf[256];
    struct tm t;
    int rc;


    if (!buf) {
        if (bufsize) {
            buf = malloc(bufsize);
            if (!buf)
                return NULL;
        } else {
            buf = sbuf;
            bufsize = sizeof(sbuf);
        }
    }

    tzset();
    if (localtime_r(&(tp->tv_sec), &t) == NULL)
        return NULL;

    rc = strftime(buf, bufsize, "%F %T", &t);
    if (rc <= 0)
        return NULL;

    bufsize -= rc;

    rc = snprintf(buf+rc, bufsize,
                  (f_verbose ? ".%06ld" : ".%03ld"),
                  f_verbose ? tp->tv_nsec/1000 : tp->tv_nsec/1000000);
    if (rc >= bufsize)
        return NULL;

    return buf;
}



int
str2timespec(const char *s,
             struct timespec *tsp) {
    char pfx[3] = "s";
    double v;
    int rc;


    rc = sscanf(s, "%lf%2s", &v, pfx);
    if (rc < 1)
        return rc;

    if (strcmp(pfx, "s") == 0) {
        tsp->tv_sec = v;
        v -= tsp->tv_sec;
        tsp->tv_nsec = v * 1000000000.0;
    } else if (strcmp(pfx, "ms") == 0) {
        tsp->tv_sec = v / 1000.0;
        v -= tsp->tv_sec * 1000.0;
        tsp->tv_nsec = v * 1000000.0;
    } else if (strcmp(pfx, "us") == 0 || strcmp(pfx, "μs") == 0) {
        tsp->tv_sec = v / 1000000.0;
        v -= tsp->tv_sec * 1000000.0;
        tsp->tv_nsec = v * 1000.0;
    } else if (strcmp(pfx, "ns") == 0) {
        tsp->tv_sec = v / 1000000000.0;
        v -= tsp->tv_sec * 1000000000.0;
        tsp->tv_nsec = v;
    } else if (strcmp(pfx, "m") == 0) {
        tsp->tv_sec = v / 60.0;
        v -= tsp->tv_sec*60;
        tsp->tv_nsec = v * 1000000000 * 60;
    } else
        return -1;

    return 1;
}
