/*
 * Copyright (C) 2011 Canonical
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>

#include <sys/klog.h>
#include <sys/utsname.h>

#define RTC_TIME	"/sys/class/rtc/rtc0/time"
#define RTC_DATE	"/sys/class/rtc/rtc0/date"

#define SYSTEM_MAP	"/boot/System.map-"

/* from drivers/base/power/trace.c */
#define USERHASH 	(16)
#define FILEHASH 	(997)
#define DEVHASH 	(1009)

#define MAGIC_NUMBER	"Magic number:"

#define	HASH_FROM_RTC	0x00000001
#define HASH_FROM_KLOG	0x00000002
#define HASH_FROM_BOTH	(HASH_FROM_RTC | HASH_FROM_KLOG)

#ifndef FUNCINFO
#define FUNCINFO	"./funcinfo.txt"
#endif

static char *funcinfo = FUNCINFO;
static FILE *funcfp;

/*
 * lookup_open()
 *	Open database of function -> crash explanation mappings
 */
static int lookup_open(const char *filename)
{
	if ((funcfp = fopen(filename, "r")) == NULL)
		return -1;

	return 0;
}

/*
 * lookup_close()
 *	close database
 */
static void lookup_close(void)
{
	if (funcfp)
		fclose(funcfp);
}

/*
 *  lookup_findfunc()
 *	check if function funcname is in the database. The format of the
 *	database is as follows:
 *	
 *		funcname1 funcname2 funcname3 funcnameN:\n
 *		explanation text\n
 *		more explanation text\n
 *		/n
 */
static int lookup_findfunc(FILE *fp, const char *funcname, bool *found)
{
	int ch;
	char buffer[1024];
	char *ptr = buffer;
	*found = false;

	while ((ch = fgetc(fp)) != EOF) {
		/* Hit a terminator? */
		if (isblank(ch) || (ch == ':')) {
			*ptr = 0;
			/* Matching function? - search done! */
			if (strcmp(funcname, buffer) == 0) {
				*found = true;
				break;
			}
			/* End of function name list */
			if (ch == ':')
				break;

			ptr = buffer;
		} else {
			/* Gather up func name */
			*ptr++ = ch;
			if (ptr >= buffer+sizeof(buffer)) {
				fprintf(stderr, "Found overly long function name.\n");
				return -1;
			}
		}
	}

	/* Gobble up to end of the line */
	while ((ch = fgetc(fp)) != EOF)
		if (ch == '\n')
			break;
	return 0;
}

/*
 *  lookup_print_text()
 *	if found is true then  dump the following lines of
 *	text until we hit an empty line, otherwise just
 *	gobble up the chars and don't print them.
 */
static void lookup_print_text(FILE *fp, bool found)
{
	int posn = 0;
	int ch;

	while ((ch = fgetc(fp)) != EOF) {
		if ((ch == '\n') && (posn == 0))
			break;
		if (ch != '\n')
			posn++;
		else
			posn = 0;

		if (found)
			putchar(ch);
	}
}

/*
 *  lookup_func()
 *	scan for a function in the database and if found
 *	dump out the explanation text that follows it.
 *	Note we can have the function defined multiple times
 *	as we can may want to dump out several messages depending
 *	on the kind of context we have when a machine crashes.
 */
static void lookup_func(const char *funcname)
{
	if (funcfp == NULL)
		return;

	rewind(funcfp);

	while (!feof(funcfp)) {
		bool found = false;
		if (lookup_findfunc(funcfp, funcname, &found) == -1)
			break;
		lookup_print_text(funcfp, found);
	}
}

/*
 *  func_to_hash()
 	generate a hash from a function name.
 */
static unsigned long func_to_hash(const char *funcname)
{
	const char *s;
	unsigned long h = 0;
	unsigned long g;

	/* From hash_pwj, Aho, Sethi and Ullman */
        for (s = funcname; *s; s++) {
                h = (h<<4) + *s;
                g = h & 0xf0000000;
                if (g) {
                        h ^= (g>>24);
                        h ^= g;
                }
        }

        h %= 16127999;	/* Limit to size of available RTC bits */

	return h;
}

/*
 *  klog_read_hash()
 *	scan the kernel messages for a recent Magic string containing
 *	saved hashed function in the RTC.  We need to change the magic
 *	from the kernel PM debug magic values back into our function
 *	hash.
 */
static int klog_read_hash(unsigned long *hash)
{
	char *buffer;
	int len;
	char *ptr;
	int ret = -1;

	if ((len = klogctl(10, NULL, 0)) < 0) {
		fprintf(stderr, "Cannot determine klog read buffer size.\n");
		return ret;
	}

	if ((buffer = calloc(1, len)) == NULL) {
		fprintf(stderr, "Cannot allocate klog read buffer of %d bytes.\n", len);
		return ret;
	}

	if (klogctl(3, buffer, len) < 0) {
		fprintf(stderr, "Cannot read kernel log.\n");
		free(buffer);
		return ret;
	}

	ptr = strstr(buffer, MAGIC_NUMBER);
	if (ptr) {
		unsigned int user, file, dev;
		unsigned long h;

		sscanf(ptr + strlen(MAGIC_NUMBER), "%d:%d:%d\n", &user, &file, &dev);
		
		/* reverse map MAGIC_NUMBER back to function hash */
		h = dev;
		h *= FILEHASH;
		h += file;
		h *= USERHASH;
		h += user;

		*hash = h;

		printf("  Magic: %d:%d:%d maps to hash: %lx\n", user, file, dev, h);
	
		ret = 0;
	} else
		fprintf(stderr, "Cannot find 'Magic number' in kernel log.\n");

	free(buffer);
	return ret;
}

/*
 *  rtc_to_hash()
 *	read the RTC values and convert them into a hash. This is in case we
 *	cannot read the magic values from the kernel messages.
 */
static int rtc_to_hash(unsigned long *hash)
{
	FILE *fp;

	int hour, min, secs;
	int year, month, day;
	int ret;
	unsigned long h;

	if ((fp = fopen(RTC_TIME, "r")) == NULL) {
		fprintf(stderr, "Cannot read " RTC_TIME ".\n");
		return -1;
	}
	ret = fscanf(fp, "%d:%d:%d", &hour, &min, &secs);
	fclose(fp);
	if (ret != 3) {
		fprintf(stderr, "Cannot parse time from " RTC_TIME ".\n");
		return -1;
	}

	if ((fp = fopen(RTC_DATE, "r")) == NULL) {
		fprintf(stderr, "Cannot read " RTC_DATE ".\n");
		return -1;
	}
	ret = fscanf(fp, "%d-%d-%d", &year, &month, &day);
	fclose(fp);
	if (ret != 3) {
		fprintf(stderr, "Cannot parse time from " RTC_TIME ".\n");
		return -1;
	}

	/* reverse map RTC to hash */
	h = year - 1900;
	if (h > 100)
		h -= 100;

	h += (month-1) * 100;
	h += (day-1) * 100 * 12;
	h += hour * 100 * 12 * 28;
	h += (min / 3) * 100 * 12 * 28 * 24;

	printf("  RTC setting %2.2d:%2.2d:%2.2d %2.2d/%2.2d/%4.4d maps to hash %lx\n",
		hour, min, secs,
		day, month, year, h);

	*hash = h;

	return 0;
}

/*
 *  get_kernel_release()
 *	get the kernel release name (uname -r)
 */
char *get_kernel_release(void)
{
	struct utsname buf;

	if (uname(&buf) < 0) {
		fprintf(stderr, "Cannot get kernel version from uname().\n");
		return NULL;
	}

	return strdup(buf.release);
}


/*
 *  find_func()
 *	Scan the system map file for all the known symbols compare the hash
 * 	of these to the given hash. If they map, dump the function name and
 *	also try to look up in the database any explanations to why the machine
 *	could have hung.
 */
void find_func(unsigned long hash)
{
	FILE *fp;
	char *uname;
	char *sysmap;
	size_t len;
	int  match = 0;

	if ((uname = get_kernel_release()) == NULL)
		return;

	len = strlen(SYSTEM_MAP) + strlen(uname) + 1;

	if ((sysmap = (char*)malloc(len)) == NULL) {
		fprintf(stderr, "Out of memory.\n");
		return;
	}
	strcpy(sysmap, SYSTEM_MAP);
	strcat(sysmap, uname);

	if ((fp = fopen(sysmap, "r")) == NULL) {
		fprintf(stderr, "Cannot open %s. Need root privileges.\n", sysmap);
		return;
	}

	while (!feof(fp)) {
		unsigned long addr;
		char type;
		char funcname[1024];
		unsigned long funchash;

		if (fscanf(fp, "%lx %c %s\n", &addr, &type, funcname) == 3) {
			funchash = func_to_hash(funcname);
			if (funchash == hash) {
				printf("  Hash matches: %s() (address: %lx)\n", funcname, addr);
				lookup_func(funcname);
				match++;
			}
		}
	}

	fclose(fp);
	
	if (!match)
		printf("  Hash did not match any known kernel functions. It may be that\n"
		       "  the system tap scripts did not fully instrument the kernel and\n"
		       "  hence we did not embed tracing information into all the function\n"
		       "  entry points so we cannot determine where the hang occured.\n");

	printf("\n");
}


/*
 *  syntax()
 *	how to use this utility
 */
void syntax(char *name)
{
	printf("Syntax: %s [-r] [-k] [--rtc] [--klog]\n", name);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	unsigned long rtc_hash;
	unsigned long klog_hash;
	unsigned long flags = 0;

	static struct option long_opts[] = {
		{ "rtc", 0, 0, 0 },
		{ "klog", 0, 0, 0 },
		{ "funcinfo", 1, 0, 0 },
		{ 0, 0, 0, 0 }
	};

	for (;;) {
		int opt_index;
		int c;

		c = getopt_long(argc, argv, "rk", long_opts, &opt_index);
		if (c == -1)
			break;
		switch (c) {
		case 0:
			switch (opt_index) {
			case 0:
				flags |= HASH_FROM_RTC;
				break;
			case 1:
				flags |= HASH_FROM_KLOG;
				break;
			case 2:
				funcinfo = optarg;	
				break;
			default:
				syntax(argv[0]);
				break;
			}
			break;
		case 'r':
			flags |= HASH_FROM_RTC;
			break;
		case 'k':
			flags |= HASH_FROM_KLOG;
			break;
		case 'f':
			funcinfo = optarg;
			break;
		default:
			syntax(argv[0]);
			break;
		}
	}

	lookup_open(funcinfo);

	/* Default to kernel log */
	if (flags == 0)
		flags |= HASH_FROM_KLOG;

	if (flags & HASH_FROM_RTC) {
		printf("Looking for function that matches hash from the RTC.\n");
		if (rtc_to_hash(&rtc_hash) == -1)
			printf("\tFailed to get hash from RTC.\n");
		else
			find_func(rtc_hash);
	}

	if (flags & HASH_FROM_KLOG) {
		printf("Looking for function that matches hash from the Magic Number from the kernel log.\n");
		if (klog_read_hash(&klog_hash) == -1)
			printf("\tFailed to get hash from kernel log.\n");
		else
			find_func(klog_hash);
	}

	lookup_close();

	exit(EXIT_SUCCESS);
}
