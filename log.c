/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <http://home.gna.org/conman/>.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Refer to "log.h" for documentation on public functions.
 *****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "log.h"
#include "util-str.h"


#ifndef MAX_LINE
#  define MAX_LINE 1024
#endif /* !MAX_LINE */


static FILE * log_file_fp = NULL;
static int    log_file_priority = -1;
static int    log_file_timestamp = 0;
static int    log_syslog = 0;
static int    log_fd_daemonize = -1;


static void log_aux(int errnum, int priority, char *msgbug, int msgbuflen,
    const char *format, va_list vargs);

static const char * log_prefix(int priority);


void dprintf(int level, const char *format, ...)
{
    static int debug_level = -1;
    va_list vargs;
    char *p;
    int i = 0;

    if (debug_level < 0) {
        if ((p = getenv("DEBUG"))) {
            i = atoi(p);
        }
        debug_level = (i > 0) ? i : 0;
    }
    if ((level > 0) && (level <= debug_level)) {
        va_start(vargs, format);
        vfprintf(stderr, format, vargs);
        va_end(vargs);
    }
    return;
}


void log_set_file(FILE *fp, int priority, int timestamp)
{
    if (fp && !ferror(fp)) {
        log_file_fp = fp;
        log_file_priority = (priority > 0) ? priority : 0;
        log_file_timestamp = !!timestamp;
        setvbuf(fp, NULL, _IONBF, 0);   /* set stream unbuffered */
    }
    else {
        log_file_fp = NULL;
        log_file_priority = -1;
        log_file_timestamp = 0;
    }
    return;
}


void log_set_syslog(char *ident, int facility)
{
    char *p;

    if (ident) {
        if ((p = strrchr(ident, '/'))) {
            ident = p + 1;
        }
        openlog(ident, LOG_NDELAY | LOG_PID, facility);
        log_syslog = 1;
    }
    else {
        closelog();
        log_syslog = 0;
    }
    return;
}


void log_set_err_pipe(int fd)
{
    log_fd_daemonize = (fd >= 0) ? fd : -1;
    return;
}


void log_err(int errnum, const char *format, ...)
{
    int priority = LOG_ERR;
    va_list vargs;
    char msg[MAX_LINE];
    signed char c;
    int n;
    char *p;

    va_start(vargs, format);
    log_aux(errnum, priority, msg, sizeof(msg), format, vargs);
    va_end(vargs);

    /*  Return error priority and message across "daemonize" pipe.
     */
    if (log_fd_daemonize >= 0) {
        c = (signed char) priority;
        n = write(log_fd_daemonize, &c, sizeof(c));
        if ((n > 0) && (msg[0] != '\0') && (log_file_fp != stderr)) {
            if ((p = strchr(msg, '\n'))) {
                *p = '\0';
            }
            (void) write(log_fd_daemonize, msg, strlen(msg) + 1);
        }
    }
#ifndef NDEBUG
    /*  Generate core for debugging.
     */
    if (getenv("DEBUG")) {
        abort();
    }
#endif /* !NDEBUG */

    exit(1);
}


void log_msg(int priority, const char *format, ...)
{
    va_list vargs;

    va_start(vargs, format);
    log_aux(0, priority, NULL, 0, format, vargs);
    va_end(vargs);

    return;
}


static void log_aux(int errnum, int priority, char *msgbuf, int msgbuflen,
    const char *format, va_list vargs)
{
    time_t t;
    struct tm tm;
    const char *prefix;
    char buf[MAX_LINE];                 /* buf starting with timestamp       */
    char *pbuf;                         /* buf starting with priority string */
    char *sbuf;                         /* buf starting with message         */
    char *p;
    int len;
    int n;

    p = sbuf = pbuf = buf;
    len = sizeof(buf) - 1;              /* reserve char for terminating '\n' */

    t = 0;
    get_localtime(&t, &tm);
    n = strftime(p, len, "%Y-%m-%d %H:%M:%S ", &tm);
    if (n == 0) {
        *p = '\0';
        len = 0;
    }
    p = sbuf = pbuf += n;
    len -= n;

    if ((prefix = log_prefix(priority))) {
        int m = 10 - strlen(prefix);
        if (m <= 0) {
            m = 1;
        }
        assert(strlen(prefix) < 10);
        n = snprintf(p, len, "%s:%*c", prefix, m, 0x20);
        if ((n < 0) || (n >= len)) {
            n = len - 1;
        }
        p = sbuf += n;
        len -= n;
    }

    n = vsnprintf(p, len, format, vargs);
    if ((n < 0) || (n >= len)) {
        n = len - 1;
    }
    p += n;
    len -= n;

    if (format[strlen(format) - 1] != '\n') {
        if ((errnum > 0) && (len > 0)) {
            n = snprintf(p, len, ": %s", strerror(errnum));
            if ((n < 0) || (n >= len)) {
                n = len - 1;
            }
            p += n;
            len -= n;
        }
        strcat(p, "\n");
    }

    if (msgbuf && (msgbuflen > 0)) {
        if (sbuf) {
            strncpy(msgbuf, sbuf, msgbuflen);
            msgbuf[msgbuflen - 1] = '\0';
        }
        else {
            msgbuf[0] = '\0';
        }
    }

    if (log_syslog) {
        syslog(priority, "%s", sbuf);
    }
    if (log_file_fp && (priority <= log_file_priority)) {
        n = fprintf(log_file_fp, "%s", log_file_timestamp ? buf : pbuf);
        if (n == EOF) {
            syslog(LOG_CRIT, "Logging stopped due to error");
            log_file_fp = NULL;
        }
    }
    return;
}


static const char * log_prefix(int priority)
{
    switch (priority) {
    case LOG_EMERG:
        return("EMERGENCY");
    case LOG_ALERT:
        return("ALERT");
    case LOG_CRIT:
        return("CRITICAL");
    case LOG_ERR:
        return("ERROR");
    case LOG_WARNING:
        return("WARNING");
    case LOG_NOTICE:
        return("NOTICE");
    case LOG_INFO:
        return("INFO");
    case LOG_DEBUG:
        return("DEBUG");
    default:
        return("UNKNOWN");
    }
}
