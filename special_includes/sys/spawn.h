/*	$NetBSD: spawn.h,v 1.1.2.1 2012/04/12 17:05:38 riz Exp $	*/

/*-
 * Copyright (c) 2008 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/include/spawn.h,v 1.3.2.1.4.1 2010/06/14 02:09:06 kensmith Exp $
 */

#ifndef _SYS_SPAWN_H_
#define _SYS_SPAWN_H_

#include <special_includes/sys/cdefs.h>
#include <special_includes/sys/types.h>
#include <special_includes/sys/sigtypes.h>
#include <special_includes/sys/signal.h>
#include <special_includes/sys/sched.h>

struct posix_spawnattr {
	short			sa_flags;
	pid_t			sa_pgroup;
	struct sched_param	sa_schedparam;
	int			sa_schedpolicy;
	sigset_t		sa_sigdefault;
	sigset_t		sa_sigmask;
};

typedef struct posix_spawn_file_actions_entry {
	enum { FAE_OPEN, FAE_DUP2, FAE_CLOSE } fae_action;

	int fae_fildes;
	union {
		struct {
			char *path;
#define fae_path	fae_data.open.path
			int oflag;
#define fae_oflag	fae_data.open.oflag
			mode_t mode;
#define fae_mode	fae_data.open.mode
		} open;
		struct {
			int newfildes;
#define fae_newfildes	fae_data.dup2.newfildes
		} dup2;
	} fae_data;
} posix_spawn_file_actions_entry_t;

struct posix_spawn_file_actions {
	unsigned int size;	/* size of fae array */
	unsigned int len;	/* how many slots are used */
	posix_spawn_file_actions_entry_t *fae;	
};

typedef struct posix_spawnattr		posix_spawnattr_t;
typedef struct posix_spawn_file_actions	posix_spawn_file_actions_t;

#define POSIX_SPAWN_RESETIDS		0x01
#define POSIX_SPAWN_SETPGROUP		0x02
#define POSIX_SPAWN_SETSCHEDPARAM	0x04
#define POSIX_SPAWN_SETSCHEDULER	0x08
#define POSIX_SPAWN_SETSIGDEF		0x10
#define POSIX_SPAWN_SETSIGMASK		0x20
/*
 * THIS IS A NON-PORTABLE NetBSD-ONLY EXTENSION, DO NOT USE OUTSIDE
 * OF UNIT TEST CODE!
 * With this flag set, the kernel part of posix_spawn will not try to
 * maximize parallelism, but instead the parent will wait for the child
 * process to complete all file/scheduler actions and report back errors
 * from that via the return value of the posix_spawn syscall. This is
 * usefull for testing, as it can verify the generated error codes and
 * match to the supposedly triggered failures.
 * In general, the kernel will return from the posix_spawn syscall as
 * early as possible, as soon as creating the new process is known to
 * work. Errors might either be reported back via the return value in
 * the parent, or (less explicit) by an error exit of the child
 * process. Our test cases deal with both behaviours in the general
 * case, but request the POSIX_SPAWN_RETURNERROR for some tests.
 */
#define POSIX_SPAWN_RETURNERROR		0x40

#endif /* !_SYS_SPAWN_H_ */

