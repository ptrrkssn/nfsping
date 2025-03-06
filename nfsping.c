/*
 * nfsping.c
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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <rpc/rpc.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <netdb.h>

#define NFS_PROGRAM 100003
#define NFS_VERSION 4
#define NFSPROC_NULL 0

int f_verbose = 0;
int f_ignore = 0;
int f_keepalive = 0;
int f_nodelay = 0;
int f_count = 0;
int f_numeric = 0;
int f_family = AF_UNSPEC;
char *f_service = "2049";


struct timespec t_interval = { 1, 0 }; /* 1s */
struct timeval t_wait = { 25, 0 }; /* 25s */

double t_warn = 0.5;
double t_crit = 2.0;

static _Atomic(int) got_sigint = 0;

void
sigint_handler(int sig) {
    got_sigint = 1;
}


double
timespec2double(struct timespec *tsp) {
    double d;

    d = tsp->tv_sec+(tsp->tv_nsec/1000000000.0);
    return d;
}

double
diff_timespec(struct timespec *t0,
              struct timespec *t1) {
    return (t0->tv_sec-t1->tv_sec) + (t0->tv_nsec-t1->tv_nsec)/1000000000.0;
}


char *
timespec2str(struct timespec *ts,
             char *buf,
             size_t bufsize) {
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
    if (localtime_r(&(ts->tv_sec), &t) == NULL)
        return NULL;

    rc = strftime(buf, bufsize, "%F %T", &t);
    if (rc <= 0)
        return NULL;

    bufsize -= rc;

    rc = snprintf(buf+rc, bufsize,
                  (f_verbose ? ".%09ld" : ".%03ld"),
                  f_verbose ? ts->tv_nsec : ts->tv_nsec/1000000);
    if (rc >= bufsize)
        return NULL;

    return buf;
}


int
print_timespec(FILE *fp,
	       struct timespec *ts) {
    return fputs(timespec2str(ts, NULL, 0), fp);
}


int
sscan_timespec(const char *s,
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


void
usage(char *argv0) {
    printf("Usage:\n\t%s [<options>] <address> [<version>]\n", argv0);
    puts("\nOptions:");
    puts("\t-h         Display this information");
    puts("\t-v         Increase verbosity");
    puts("\t-i         Ignore errors and continue");
    puts("\t-c         Continuous pings");
    puts("\t-3         Send 3 pings");
    puts("\t-4         Use IPv4");
    puts("\t-6         Use IPv6");
    puts("\t-k         Keep TCP session open between pings");
    puts("\t-I <time>  Interval between pings (default: 1000ms)");
    puts("\t-W <time>  RTT warning time");
    puts("\t-C <time>  RTT critical time");
    puts("\t-T <uri>   Send timeseries data to <uri>");
}


char *
addrinfo2str(struct addrinfo *aip) {
    static char buf[1024];
    int rc;

    rc = getnameinfo(aip->ai_addr, aip->ai_addrlen,
                     buf, sizeof(buf),
                     NULL, 0,
                     f_numeric ? NI_NUMERICHOST : 0);
    if (rc != 0)
        return NULL;

    return buf;
}




int
main(int argc,
     char *argv[]) {
    struct addrinfo hints, *result, *rp;
    struct netbuf nbuf;
    CLIENT *cl = NULL;
    int i, j, rc, *rpcrc;
    void *clnt_msg = NULL;
    char clnt_res;
    int sock = -1;
    unsigned long n, version = NFS_VERSION;
    struct timespec t0, t1, t2;
    double dt,  dt_min,  dt_max,  dt_sum;
    double dtc, dtc_min, dtc_max, dtc_sum;
    double dto, dto_min, dto_max, dto_sum;


    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        for (j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
            case 'h':
                usage(argv[0]);
                exit(0);

            case '-':
                ++i;
                goto LastArg;

            case 'v':
                ++f_verbose;
                break;

            case 'i':
                ++f_ignore;
                break;

            case 'c':
                f_count = -1;
                break;

            case '3':
                f_count = 3;
                break;

            case '4':
                f_family = AF_INET;
                break;

            case '6':
                f_family = AF_INET6;
                break;

            case 'k':
                ++f_keepalive;
                break;

            case 'n':
                ++f_numeric;
                break;

            case 'I':
                if (sscan_timespec(argv[i]+j+1, &t_interval) != 1) {
                    fprintf(stderr, "%s: Error: %s: Invalid interval time\n",
                            argv[0], argv[i]+j+1);
                    exit(1);
                }
                goto NextArg;

            case 'W':
                if (sscanf(argv[i]+j+1, "%lf", &t_warn) != 1) {
                    fprintf(stderr, "%s: Error: %s: Invalid warning time limit\n",
                            argv[0], argv[i]+j+1);
                    exit(1);
                }
                t_warn /= 1000.0;
                goto NextArg;

            case 'C':
                if (sscanf(argv[i]+j+1, "%lf", &t_crit) != 1) {
                    fprintf(stderr, "%s: Error: %s: Invalid critical time limit\n",
                            argv[0], argv[i]+j+1);
                    exit(1);
                }
                t_crit /= 1000.0;
                goto NextArg;

            default:
                fprintf(stderr, "%s: Error: -%c: Invalid switch\n", argv[0], argv[i][j]);
                exit(1);
            }
        }
    NextArg:;
    }

 LastArg:
    if (i >= argc) {
        fprintf(stderr, "%s: Error: Missing required <address> argument\n", argv[0]);
        exit(1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = f_family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    rc = getaddrinfo(argv[i], f_service, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "%s: Error: %s: getaddrinfo: %s\n",
                argv[0], argv[i], gai_strerror(rc));
        exit(1);
    }

    for (rp = result; rp != NULL; rp++) {
        if (f_family == AF_UNSPEC || f_family == rp->ai_family)
            break;
    }
    if (!rp) {
        fprintf(stderr, "%s: Error: %s: No IP address found\n",
                argv[0], argv[i]);
        exit(1);
    }

    nbuf.buf = rp->ai_addr;
    nbuf.len = nbuf.maxlen = rp->ai_addrlen;

    if (i+1 < argc) {
        if (sscanf(argv[i+1], "%lu", &version) != 1) {
            fprintf(stderr, "%s: Error: %s: Invalid version\n",
                    argv[0], argv[i+1]);
            exit(1);
        }
    }

    signal(SIGINT, sigint_handler);

    n = 0;
    dt_min  = dt_max  = dt_sum = 0.0;
    dtc_min = dtc_max = dtc_sum = 0.0;
    dto_min = dto_max = dto_sum = 0.0;

    do {
        ++n;

        clock_gettime(CLOCK_REALTIME, &t0);

        if (sock == -1) {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock < 0) {
                fprintf(stderr, "%s: Error: %s: socket: %s\n",
                        argv[0], argv[i], strerror(errno));
                exit(1);
            }

            if (f_nodelay) {
                int one = 1;
                if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
                    fprintf(stderr, "%s: Error: %s: setsockopt(TCP_NODELAY): %s\n",
                            argv[0], argv[i], strerror(errno));
                    exit(1);
                }
            }
        }

        if (!cl) {
            cl = clnt_vc_create(sock, &nbuf, NFS_PROGRAM, version, 0, 0);
            if (!cl) {
                fprintf(stderr, "%s: Error: %s[%s]: %s\n",
                        argv[0], argv[i], addrinfo2str(rp), clnt_spcreateerror("clnt_vc_create"));
                exit(1);
            }
        }

        clock_gettime(CLOCK_REALTIME, &t1);
#if 0
        rpcrc = nfsproc_null_4(&clnt_msg, cl);
#else
        rc = clnt_call(cl, NFSPROC_NULL,
                       (xdrproc_t) xdr_void, (caddr_t) clnt_msg,
                       (xdrproc_t) xdr_void, (caddr_t) &clnt_res,
                       t_wait);
#endif
        clock_gettime(CLOCK_REALTIME, &t2);

        dtc = diff_timespec(&t1, &t0);
        dto = diff_timespec(&t2, &t1);
        dt  = diff_timespec(&t2, &t0);

        if (rc != RPC_SUCCESS) {
            fprintf(stderr, "%s: Error: %s [%.3f ms]: %s\n",
                    argv[0], argv[i], dt, clnt_sperror(cl, "nfsproc_null"));
            if (!f_ignore)
                exit(1);
        }

        dtc_sum += dtc;
        if (dtc > dtc_max)
            dtc_max = dtc;
        if (!dtc_min || dtc < dtc_min)
            dtc_min = dtc;

        dto_sum += dto;
        if (dto > dto_max)
            dto_max = dto;
        if (!dto_min || dto < dto_min)
            dto_min = dto;

        dt_sum += dt;
        if (dt > dt_max)
            dt_max = dt;
        if (!dt_min || dt < dt_min)
            dt_min = dt;

        if (f_verbose || dt >= t_warn || dt >= t_crit) {
            if (got_sigint)
                putchar('\r');
            printf("%s : %s : %6lu : %10.3f ms",
                   timespec2str(&t0, NULL, 0), argv[i], n,
                   dt*1000.0);
            if (f_verbose > 1)
                printf(" : %.3f+%.3f ms", dtc*1000.0, dto*1000.0);

            if (dt >= t_crit || dt >= t_warn || rc != RPC_SUCCESS) {
                fputs(" : ", stdout);
                if (dt >= t_crit)
                    fputs("C", stdout);
                else if (dt >= t_warn)
                    fputs("W", stdout);
                if (rc != RPC_SUCCESS)
                    fputs("E", stdout);
                putchar('!');
            }
            putchar('\n');
        }

        if (!f_keepalive) {
            clnt_destroy(cl);
            cl = NULL;

            close(sock);
            sock = -1;
        }

        t1 = t0;
        t1.tv_nsec += t_interval.tv_nsec;
        if (t1.tv_nsec >= 1000000000) {
            t1.tv_sec++;
            t1.tv_nsec -= 1000000000;
        }
        t1.tv_sec += t_interval.tv_sec;
    } while ((f_count < 0 || n < f_count) && !got_sigint &&
             (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &t1, &t1), 1));

    if (n > 0) {
        printf("[%lu packets, min = %.3f ms, max = %.3f ms, avg = %.3f ms]\n",
               n, dt_min*1000, dt_max*1000, dt_sum*1000/n);
    }

    if (result)
        freeaddrinfo(result);

    return 0;
}
