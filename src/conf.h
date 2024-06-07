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
#ifndef __CONF__
#define __CONF__

#include <sys/types.h>
#include "slog.h"

struct auth {
	int use_legacy_auth; /* 1 if only password is used, 0 for username + password */
	char *username;
	char *password;
};

struct conf {
    /* connection to Redis */
	char *redis_host;
	int redis_port;
	struct auth *redis_auth;

    /* HTTP server interface */
    char *http_host;
    int http_port;
    int http_threads;
    size_t http_max_request_size;

    /* pool size, one pool per worker thread */
	int pool_size_per_thread;
    /* daemonize process, off by default */
	int daemonize;
	char *pidfile;
    /* WebSocket support, off by default */
	int websockets;
    /* database number */
	int database;

    /* user/group */
    uid_t user;
    gid_t group;

    /* Logging */
	char *logfile;
    log_level verbosity;
    struct {
        log_fsync_mode mode;
		int period_millis; /* only used with LOG_FSYNC_MILLIS */
    } log_fsync;

    /* HiRedis options */
	struct {
		int keep_alive_sec; /* passed to redisEnableKeepAliveWithInterval, > 0 to enable */
	} hiredis_opts;

#ifdef HAVE_SSL
	/* SSL */
	struct {
		int enabled;
		char *ca_cert_bundle;  /* File name of trusted CA/ca bundle file, optional */
		char *path_to_certs;   /* Path of trusted certificates, optional */
		char *client_cert_pem; /* File name of client certificate file, optional */
		char *client_key_pem;  /* File name of client private key, optional */
		char *redis_sni;       /* Server name to request (SNI), optional */
	} ssl;
#endif

	/* Request to serve on “/” */
	char *default_root;
};

#endif