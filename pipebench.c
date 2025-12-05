/* $Id: pipebench.c,v 1.12 2003/04/20 16:45:45 marvin Exp $
 *
 * Pipebench
 *
 * By Thomas Habets <thomas@habets.pp.se>
 *
 * Measures the speed of stdin/stdout communication.
 *
 * TODO:
 * -  Variable update time  (now just updates once a second)
 */
/*
 * Copyright (C) 2002 Thomas Habets <thomas@habets.pp.se>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#define _POSIX_C_SOURCE 199309L /* Required for clock_gettime */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

static float version = 0.40;

static volatile sig_atomic_t done = 0;

static void sigint(int n)
{
	(void)n;
	done = 1;
}

/* Helper for monotonic time */
static void get_mono_time(struct timespec *ts)
{
	if (clock_gettime(CLOCK_MONOTONIC, ts) == -1) {
		perror("pipebench: clock_gettime");
		exit(1);
	}
}

/*
 * Turn a 64 int into SI or pseudo-SI units ("nunit" based).
 * Two decimal places.
 */
static char *unitify(uint64_t _in, char *buf, size_t max, unsigned long nunit,
		     int dounit)
{
	int e = 0;
	uint64_t in;
	double inf;
	const char *unit = "";
	const char *units[] = {
		"",
		"k",
		"M",
		"G",
		"T",
		"P",
		"E",
	};
	int fra = 0;

	inf = (double)_in;
	in = _in;
	if (dounit) {
		if (in > (nunit*nunit)) {
			e++;
			in/=nunit;
		}
		in *= 100;
		while (in > (100*nunit)) {
			e++;
			in/=nunit;
		}
		while (e && (e >= (int)(sizeof(units)/sizeof(char*)))) {
			e--;
			in*=nunit;
		}
		unit = units[e];
		inf = in / 100.0;
		fra = 2;
	}
	snprintf(buf, max, "%7.*f %s",fra,inf,unit);
	return buf;
}

/*
 * Return a string representation of time difference.
 */
static char *time_diff(struct timespec *start, struct timespec *end, char *buf,
		       size_t max)
{
	long sec = end->tv_sec - start->tv_sec;
	long nsec = end->tv_nsec - start->tv_nsec;
	
	if (nsec < 0) {
		sec--;
		nsec += 1000000000L;
	}
	
	snprintf(buf,max,"%.2ldh%.2ldm%.2ld.%.2lds",
		 sec / 3600,
		 (sec / 60) % 60,
		 sec % 60,
		 nsec / 10000000L);
	return buf;
}

static void usage(void)
{
	printf("Pipebench %1.2f, by Thomas Habets <thomas@habets.pp.se>\n",
	       version);
	printf("usage: ... | pipebench [ -ehqQIoru ] [ -b <bufsize ] "
	       "[ -s <file> | -S <file> ]\\\n           | ...\n");
}

/*
 * main
 */
int main(int argc, char **argv)
{
	int c;
	uint64_t datalen = 0, last_datalen = 0, speed = 0;
	struct timespec start, tv, tv2;
	char tdbuf[64];
	char speedbuf[64];
	char datalenbuf[64];
	unsigned int bufsize = 819200;
	int summary = 1;
	int errout = 0;
	int quiet = 0;
	int fancy = 1;
	int dounit = 1;
	FILE *statusf;
	int statusf_append = 0;
	const char *statusfn = 0;
	int unit = 1024;
	char *buffer;

	statusf = stderr;

	while (EOF != (c = getopt(argc, argv, "ehqQb:ros:S:Iu"))) {
		switch(c) {
		case 'e':
			errout = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'Q':
			quiet = 1;
			summary = 0;
			break;
		case 'o':
			summary = 0;
			break;
		case 'b':
			bufsize = atoi(optarg);
			break;
		case 'h':
			usage();
			return 0;
		case 'r':
			fancy = 0;
			summary = 0;
			break;
		case 's':
			statusfn = optarg;
			statusf_append = 0;
			break;
		case 'S':
			statusfn = optarg;
			statusf_append = 1;
			break;
		case 'I':
			unit = 1000;
			break;
		case 'u':
			dounit = 0;
			break;
		default:
			usage();
			return 1;
		}
	}

	if (statusfn) {
		if (!(statusf = fopen(statusfn, statusf_append?"a":"w"))) {
			perror("pipebench: fopen(status file)");
			if (errout) {
				return 1;
			}
		}
	}

	/* Initial timing */
	get_mono_time(&tv);
	start = tv;

	if (signal(SIGINT, sigint) == SIG_ERR) {
		perror("pipebench: signal()");
		if (errout) {
			return 1;
		}
	}
	
	while (!(buffer = malloc(bufsize))) {
		perror("pipebench: malloc()");
		bufsize >>= 1;
		if (bufsize < 1024) return 1; /* Safety break */
	}

	while (!done) {
		size_t n;
		char ctimebuf[64];
		time_t rawtime;
		struct tm * timeinfo;

		/* Check EOF on stdin specifically */
		int ch = fgetc(stdin);
		if (ch == EOF) break;
		ungetc(ch, stdin);

		if ((n = fread(buffer, 1, bufsize, stdin)) == 0) {
			if (ferror(stdin)) {
				perror("pipebench: fread()");
				if (errout) return 1;
			}
			break;
		}

		datalen += n;
		if (fwrite(buffer, 1, n, stdout) != n) {
			perror("pipebench: fwrite()");
			if (errout) {
				return 1;
			}
			break;
		}

		/* Update loop timing */
		get_mono_time(&tv2);

		/* Display formatting logic */
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(ctimebuf, sizeof(ctimebuf), "%a %b %d %H:%M:%S", timeinfo);
		
		if (fancy && !quiet) {
			fprintf(statusf, "%s: %sB %sB/second (%s)%c",
				time_diff(&start, &tv2, tdbuf, sizeof(tdbuf)),
				unitify(datalen, datalenbuf, sizeof(datalenbuf),
					unit, dounit),
				unitify(speed, speedbuf, sizeof(speedbuf),
					unit, dounit),
				ctimebuf,
				statusfn ? '\n' : '\r');
		}

		if (tv.tv_sec != tv2.tv_sec) {
			speed = (datalen - last_datalen);
			last_datalen = datalen;
			tv = tv2; /* Update last interval time */
			
			if (!fancy) {
				fprintf(statusf, "%" PRIu64 "\n", speed);
			}
		}
	}
	free(buffer);

	if (summary) {
		double seconds;
		
		/* Capture final end time properly */
		get_mono_time(&tv2);

		seconds = (tv2.tv_sec - start.tv_sec) + 
			  (tv2.tv_nsec - start.tv_nsec) / 1e9;

		fprintf(statusf,"                                     "
			"            "
			"                              "
			"%c"
			"Summary:\nPiped %sB in %s: %sB/second\n",
			statusfn?'\n':'\r',
			unitify(datalen, datalenbuf, sizeof(datalenbuf),
				unit, dounit),
			time_diff(&start, &tv2, tdbuf, sizeof(tdbuf)),
			unitify(seconds > 0 ? (uint64_t)(datalen/seconds) : 0,
				speedbuf, sizeof(speedbuf), unit, dounit));
	}
	if (statusfn && statusf != stderr) fclose(statusf);
	return 0;
}
