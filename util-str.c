/******************************************************************************\
 *  $Id: util-str.c,v 1.1 2001/09/06 21:50:52 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
 ******************************************************************************
 *  Refer to "util-str.h" for documentation on public functions.
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "errors.h"
#include "util-str.h"


#define MAX_STR_SIZE 1024


char * create_string(const char *str)
{
    char *p;

    if (!str)
        return(NULL);
    if (!(p = strdup(str)))
        err_msg(0, "Out of memory");
    return(p);
}


char * create_format_string(const char *fmt, ...)
{
    va_list vargs;
    int n, len;
    char *p;

    if (!fmt)
        return(NULL);

    va_start(vargs, fmt);
    if ((len = vsnprintf(p, 0, fmt, vargs)) < 0)
        len = MAX_STR_SIZE;
    else				/* C99 standard returns needed strlen */
        len++;				/* reserve space for terminating NUL */
    if (!(p = (char *) malloc(len)))
        err_msg(0, "Out of memory");
    n = vsnprintf(p, len, fmt, vargs);
    va_end(vargs);

    if (n < 0 || n >= len)
        p[len - 1] = '\0';		/* ensure str is NUL-terminated */
    return(p);
}


void destroy_string(char *str)
{
    if (str)
        free(str);
    return;
}


size_t append_format_string(char *dst, size_t size, const char *fmt, ...)
{
    char *p;
    int nAvail;
    int lenOrig;
    va_list vargs;
    int n;

    assert(dst);
    if (!fmt || !size)
        return(0);

    p = dst;
    nAvail = size;
    while (*p && (nAvail > 0))
        p++, nAvail--;

    /*  Assert (dst) was NUL-terminated.  If (nAvail == 0), no NUL was found.
     */
    assert(nAvail != 0);
    if (nAvail <= 1)			/* dst is full, only room for NUL */
        return(-1);
    lenOrig = p - dst;

    va_start(vargs, fmt);
    n = vsnprintf(p, nAvail, fmt, vargs);
    va_end(vargs);

    if ((n < 0) || (n >= nAvail)) {
        dst[size - 1] = '\0';		/* ensure dst is NUL-terminated */
        return(-1);
    }
    return(lenOrig + n);
}


int substitute_string(char *dst, size_t dstlen, char *src, char c, char *sub)
{
    char *p, *q;
    int n, m;

    assert(dst);
    if (!dstlen || !src)
        return(0);

    for (p=src, q=dst, n=dstlen; n>0 && p && *p; p++) {
        if (*p != c) {
            *q++ = *p;
            n--;
        }
        else if (sub) {
            m = strlcpy(q, sub, n);
            q += m;
            n -= m;
        }
    }

    if (n > 0) {
        *q = '\0';
        return(dstlen - n);
    }
    else {
        dst[dstlen - 1] = '\0';
        return(-1);
    }
}


char * create_long_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 25;			/* MM/DD/YYYY HH:MM:SS ZONE + NUL */

    if (!(p = malloc(len)))
        err_msg(0, "Out of memory");

    get_localtime(&t, &tm);

    if (strftime(p, len, "%m/%d/%Y %H:%M:%S %Z", &tm) == 0)
        err_msg(0, "strftime() failed");

    return(p);
}


char * create_short_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 6;			/* HH:MM + NUL */

    if (!(p = malloc(len)))
        err_msg(0, "Out of memory");

    get_localtime(&t, &tm);

    if (strftime(p, len, "%H:%M", &tm) == 0)
        err_msg(0, "strftime() failed");

    return(p);
}


char * create_time_delta_string(time_t t)
{
    time_t now;
    long n;
    int years, weeks, days, hours, minutes, seconds;
    char buf[25];

    if (time(&now) == ((time_t) -1))
        err_msg(errno, "time() failed -- What time is it?");
    n = difftime(now, t);
    assert(n >= 0);

    seconds = n % 60;
    n /= 60;
    minutes = n % 60;
    n /= 60;
    hours = n % 24;
    n /= 24;
    days = n % 7;
    n /= 7;
    weeks = n % 52;
    n /= 52;
    years = n;

    if (years > 0)
        n = snprintf(buf, sizeof(buf), "%dy%dw%dd%dh%dm%ds",
            years, weeks, days, hours, minutes, seconds);
    else if (weeks > 0)
        n = snprintf(buf, sizeof(buf), "%dw%dd%dh%dm%ds",
            weeks, days, hours, minutes, seconds);
    else if (days > 0)
        n = snprintf(buf, sizeof(buf), "%dd%dh%dm%ds",
            days, hours, minutes, seconds);
    else if (hours > 0)
        n = snprintf(buf, sizeof(buf), "%dh%dm%ds", hours, minutes, seconds);
    else if (minutes > 0)
        n = snprintf(buf, sizeof(buf), "%dm%ds", minutes, seconds);
    else
        n = snprintf(buf, sizeof(buf), "%ds", seconds);

    assert((n >= 0) && (n < sizeof(buf)));
    return(create_string(buf));
}


struct tm * get_localtime(time_t *t, struct tm *tm)
{
#ifndef HAVE_LOCALTIME_R

    int rc;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    struct tm *ptm;

#endif /* !HAVE_LOCALTIME_R */

    assert(t);
    assert(tm);

    if (*t == 0) {
        if (time(t) == ((time_t) -1))
            err_msg(errno, "time() failed -- What time is it?");
    }

#ifndef HAVE_LOCALTIME_R

    /*  localtime() is not thread-safe, so it is protected by a mutex.
     */
    if ((rc = pthread_mutex_lock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed");
    if (!(ptm = localtime(t)))
        err_msg(errno, "localtime() failed");
    *tm = *ptm;
    if ((rc = pthread_mutex_unlock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed");

#else /* HAVE_LOCALTIME_R */

    if (!localtime_r(t, tm))
        err_msg(errno, "localtime_r() failed");

#endif /* !HAVE_LOCALTIME_R */

    return(tm);
}