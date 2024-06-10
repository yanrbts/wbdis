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
#include <stdlib.h>
#include <string.h>
#include <event.h>
#include <hiredis/adapters/libevent.h>
#include "pool.h"
#include "worker.h"
#include "conf.h"
#include "server.h"

static void pool_schedule_reconnect(struct pool* p);

struct pool *pool_new(struct worker *w, int count) {
    struct pool *p = calloc(1, sizeof(struct pool));

    p->count = count;
    p->ac = calloc(count, sizeof(redisAsyncContext*));
    p->w = w;
    p->cfg = w->s->cfg;

    return p;
}

void pool_free_context(redisAsyncContext *ac) {
	if (ac)	{
		redisAsyncDisconnect(ac);
	}
}

static void pool_on_connect(const redisAsyncContext *ac, int status) {
    struct pool *p = ac->data;
    int i = 0;

    if (!p || status == REDIS_ERR || ac->err) {
        if (p) {
            pool_schedule_reconnect(p);
        }
        return;
    }
    /* connected to redis! */

	/* add to pool */
	for(i = 0; i < p->count; ++i) {
		if(p->ac[i] == NULL) {
			p->ac[i] = ac;
			return;
		}
	}
}

struct pool_reconnect {
	struct event ev;
	struct pool *p;
	struct timeval tv;
};

static void pool_can_connect(int fd, short event, void *ptr) {
    struct pool_reconnect *pr = ptr;
    struct pool *p = pr->p;

    (void)fd;
    (void)event;
    free(pr);
    pool_connect(p, p->cfg->database, 1);
}

static void pool_schedule_reconnect(struct pool *p) {
    struct pool_reconnect *pr = malloc(sizeof(struct pool_reconnect));

    pr->p = p;
    pr->tv.tv_sec = 0;
    pr->tv.tv_usec = 100 * 1000; /* 0.1 sec*/

    evtimer_set(&pr->ev, pool_can_connect, pr);
    event_base_set(p->w->base, &pr->ev);
    evtimer_add(&pr->ev, &pr->tv);
}

static void pool_on_disconnect(const redisAsyncContext *ac, int status) {
    struct pool *p = ac->data;
    int i = 0;

    /* no need to clean anything here. */
    if (p == NULL) {
        return;
    }

    if (status != REDIS_OK) {
        char format[] = "Error disconnecting: %s";
		size_t msg_sz = sizeof(format) - 2 + ((ac && ac->errstr) ? strlen(ac->errstr) : 6);
		char *log_msg = calloc(msg_sz + 1, 1);
		if(log_msg) {
			snprintf(log_msg, msg_sz + 1, format, ((ac && ac->errstr) ? ac->errstr : "(null)"));
			slog(p->w->s, WEBDIS_ERROR, log_msg, msg_sz-1);
			free(log_msg);
		}
    }
    /* remove from the pool */
    for(i = 0; i < p->count; ++i) {
		if(p->ac[i] == ac) {
			p->ac[i] = NULL;
			break;
		}
	}
	/* schedule reconnect */
	pool_schedule_reconnect(p);
}

static void pool_log_auth(struct server *s, 
                        log_level level, 
                        const char *format, 
                        size_t format_len, 
                        const char *str) {
	/* -2 for `%s`, 6 for "(null)", +1 for \0 */
	size_t msg_size = format_len - 2 + (str ? strlen(str) : 6) + 1;
	char *msg = calloc(1, msg_size);
	if(msg) {
		snprintf(msg, msg_size, format, str ? str : "(null)");
		slog(s, level, msg, msg_size - 1);
		free(msg);
	}
}

static void pool_on_auth_complete(redisAsyncContext *c, void *r, void *data) {
    redisReply *reply = r;
    struct pool *p = data;
    const char err_format[] = "Authentication failed: %s";
	const char ok_format[] = "Authentication succeeded: %s";
	struct server *s = p->w->s;
	(void)c;

    if (!reply) {
        return;
    }
    pthread_mutex_lock(&s->auth_log_mutex);
	if(s->auth_logged) {
		pthread_mutex_unlock(&s->auth_log_mutex);
		return;
	}
	if(reply->type == REDIS_REPLY_ERROR) {
		pool_log_auth(s, WEBDIS_ERROR, err_format, sizeof(err_format)-1, reply->str);
	} else if(reply->type == REDIS_REPLY_STATUS) {
		pool_log_auth(s, WEBDIS_INFO, ok_format, sizeof(ok_format)-1, reply->str);
	}
	s->auth_logged++;
	pthread_mutex_unlock(&s->auth_log_mutex);
}