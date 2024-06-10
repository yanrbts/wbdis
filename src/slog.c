/*
 * Copyright (c) 2024-2024, Yanruibing <yanruibing@kxyk.com> All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "slog.h"
#include "server.h"
#include "conf.h"

#if SLOG_MSG_MAX_LEN < 64
#error "SLOG_MSG_MAX_LEN must be at least 64"
#endif

/**
 * Initialize log writer.
 */
void slog_init(struct server *s) {
    s->log.self = getpid();

    if (s->cfg->logfile) {
        int old_fd = s->log.fd;

        s->log.fd = open(s->cfg->logfile, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR|S_IWUSR);

        /* close old log */
        if (old_fd != -1) {
            close(old_fd);
        }

        if (s->log.fd != -1)
            return;
        fprintf(stderr, "Could not open %s: %s\n", s->cfg->logfile, strerror(errno));
    }
    /* stderr */
    s->log.fd = 2;
}

static void slog_fsync_tick(int fd, short event, void *data) {
	struct server *s = data;
    /*
     * fsync()  transfers  ("flushes")  all modified in-core data 
     * of (i.e., modified buffer cache pages for) the file
     * referred to by the file descriptor fd to the disk device 
     * (or other  permanent  storage  device)  so  that  all
     * changed information can be retrieved even if the system 
     * crashes or is rebooted.  This includes writing through
     * or flushing a disk cache if present.  The call blocks 
     * until the device reports  that  the  transfer  has  com‐pleted.
     */
	int ret = fsync(s->log.fd);
	(void)fd;
	(void)event;
	(void)ret;
}

void slog_fsync_init(struct server *s) {
    if (s->cfg->log_fsync.mode != LOG_FSYNC_MILLIS) {
		return;
	}
	/* install a libevent timer for handling fsync */
    s->log.fsync_ev = event_new(s->base, -1, EV_PERSIST, slog_fsync_tick, s);
    if (s->log.fsync_ev == NULL) {
        const char evnew_error[] = "fsync timer could not be created";
        slog(s, WEBDIS_ERROR, evnew_error, sizeof(evnew_error)-1);
        return;
    }

    int period_usec =s->cfg->log_fsync.period_millis * 1000;
	s->log.fsync_tv.tv_sec = period_usec / 1000000;
	s->log.fsync_tv.tv_usec = period_usec % 1000000;
    int ret_ta = evtimer_add(s->log.fsync_ev, &s->log.fsync_tv);
    if (ret_ta != 0) {
        const char reason[] = "fsync timer could not be added: %d";
		char error_msg[sizeof(reason) + 16]; /* plenty of extra space */
		int error_len = snprintf(error_msg, sizeof(error_msg), reason, ret_ta);
		slog(s, WEBDIS_ERROR, error_msg, (size_t)error_len);
    }
}

/**
 * Returns whether this log level is enabled.
 */
int slog_enabled(struct server *s, log_level level) {
    return level <= s->cfg->verbosity ? 1 : 0;
}

/**
 * Write log message to disk, or stderr.
 */
static void
slog_internal(struct server *s, log_level level, const char *body, size_t sz) {
    const char *c = "EWNIDT";
    time_t now;
    struct tm now_tm, *lt_ret;
    char time_buf[64];
    char msg[1 + SLOG_MSG_MAX_LEN];
	char line[2 * SLOG_MSG_MAX_LEN]; /* bounds are checked. */
	int line_sz, ret;

    if (!s->log.fd) return;

    /* limit message size */
    sz = sz ? sz : strlen(body);
    snprintf(msg, sz+1 > sizeof(msg) ? sizeof(msg) : sz+1, "%s", body);

    /* get current time */
    now = time(NULL);
    lt_ret = localtime_r(&now, &now_tm);
    if (lt_ret) {
        strftime(time_buf, sizeof(time_buf), "%d %b %H:%M:%S", lt_ret);
    } else {
        const char err_msg[] = "(NO TIME AVAILABLE)";
		memcpy(time_buf, err_msg, sizeof(err_msg));
    }

    /* generate output line. */
    char letter = (level == WEBDIS_TRACE ? c[5] : c[level]);
    line_sz = snprintf(line, sizeof(line),
		"[%d] %s %c %s\n", (int)s->log.self, time_buf, letter, msg);
    
    /* write to log and maybe flush to disk. */
    ret = write(s->log.fd, line, line_sz);
    if (s->cfg->log_fsync.mode == LOG_FSYNC_ALL) {
        ret = fsync(s->log.fd);
    }
    (void)ret;
}

/**
 * Thin wrapper around slog_internal that first checks the log level.
 */
void slog(struct server *s, log_level level,
		const char *body, size_t sz) {
	if(level <= s->cfg->verbosity) { /* check log level first */
		slog_internal(s, level, body, sz);
	}
}