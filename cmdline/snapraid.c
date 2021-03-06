/*
 * Copyright (C) 2011 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "portable.h"

#include "snapraid.h"
#include "support.h"
#include "elem.h"
#include "import.h"
#include "search.h"
#include "state.h"
#include "raid/raid.h"

/****************************************************************************/
/* main */

void version(void)
{
	msg_status(PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");
}

void usage(void)
{
	version();

	printf("Usage: " PACKAGE " status|diff|sync|scrub|list|dup|up|down|smart|pool|check|fix [options]\n");
	printf("\n");
	printf("Commands:\n");
	printf("  status Print the status of the array\n");
	printf("  diff   Show the changes that needs to be syncronized\n");
	printf("  sync   Syncronize the state of the array\n");
	printf("  scrub  Scrub the array\n");
	printf("  list   List the array content\n");
	printf("  dup    Find duplicate files\n");
	printf("  up     Spinup the array\n");
	printf("  down   Spindown the array\n");
	printf("  smart  SMART attributes of the array\n");
	printf("  pool   Create or update the virtual view of the array\n");
	printf("  check  Check the array\n");
	printf("  fix    Fix the array\n");
	printf("\n");
	printf("Options:\n");
	printf("  " SWITCH_GETOPT_LONG("-c, --conf FILE       ", "-c") "  Configuration file\n");
	printf("  " SWITCH_GETOPT_LONG("-f, --filter PATTERN  ", "-f") "  Process only files matching the pattern\n");
	printf("  " SWITCH_GETOPT_LONG("-d, --filter-disk NAME", "-f") "  Process only files in the specified disk\n");
	printf("  " SWITCH_GETOPT_LONG("-m, --filter-missing  ", "-m") "  Process only missing/deleted files\n");
	printf("  " SWITCH_GETOPT_LONG("-e, --filter-error    ", "-e") "  Process only files with errors\n");
	printf("  " SWITCH_GETOPT_LONG("-p, --percentage PERC ", "-p") "  Process only a part of the array\n");
	printf("  " SWITCH_GETOPT_LONG("-o, --older-than DAYS ", "-o") "  Process only the older part of the array\n");
	printf("  " SWITCH_GETOPT_LONG("-i, --import DIR      ", "-i") "  Import deleted files\n");
	printf("  " SWITCH_GETOPT_LONG("-l, --log FILE        ", "-l") "  Log file. Default none\n");
	printf("  " SWITCH_GETOPT_LONG("-a, --audit-only      ", "-a") "  Check only file data and not parity\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --pre-hash        ", "-h") "  Pre hash all the new data\n");
	printf("  " SWITCH_GETOPT_LONG("-Z, --force-zero      ", "-Z") "  Force synching of files that get zero size\n");
	printf("  " SWITCH_GETOPT_LONG("-E, --force-empty     ", "-E") "  Force synching of disks that get empty\n");
	printf("  " SWITCH_GETOPT_LONG("-U, --force-uuid      ", "-U") "  Force commands on disks with uuid changed\n");
	printf("  " SWITCH_GETOPT_LONG("-D, --force-device    ", "-D") "  Force commands on disks with same device id\n");
	printf("  " SWITCH_GETOPT_LONG("-N, --force-nocopy    ", "-N") "  Force commands disabling the copy detection\n");
	printf("  " SWITCH_GETOPT_LONG("-F, --force-full      ", "-F") "  Force commands requiring a full sync\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose         ", "-v") "  Verbose\n");
}

void memory(void)
{
	msg_tag("memory:used:%" PRIu64 "\n", (uint64_t)malloc_counter());

	/* size of the block + the size of the blockarr pointer to it */
	msg_tag("memory:block:%" PRIu64 "\n", (uint64_t)(sizeof(struct snapraid_block) + sizeof(void*)));

	/* size of the file + the size of the 3 hashtables pointers to it, * 2 for the hashtable grow factor */
	msg_tag("memory:file:%" PRIu64 "\n", (uint64_t)(sizeof(struct snapraid_file) + 3*2*sizeof(void*)));

	msg_progress("Using %u MiB of memory.\n", (unsigned)(malloc_counter() / 1024 / 1024));

}

/****************************************************************************/
/* log */

void log_open(const char* log)
{
	char path[PATH_MAX];
	const char* mode;
	char text_T[32];
	char text_D[32];
	time_t t;
	struct tm* tm;

	/* leave stdlog at 0 if not specified */
	if (log == 0)
		return;

	t = time(0);
	tm = localtime(&t);
	if (tm) {
		strftime(text_T, sizeof(text_T), "%H%M%S", tm);
		strftime(text_D, sizeof(text_T), "%Y%m%d", tm);
	} else {
		/* LCOV_EXCL_START */
		strcpy(text_T, "invalid");
		strcpy(text_D, "invalid");
		/* LCOV_EXCL_STOP */
	}

	/* file mode */
	mode = "wt";
	if (*log == '>') {
		++log;

		if (*log == '>') {
			mode = "at";
			++log;
		}

		if (log[0] == '&' && log[1] == '1') {
			stdlog = stdout;
			return;
		}

		if (log[0] == '&' && log[1] == '2') {
			stdlog = stderr;
			return;
		}
	}

	/* process the path */
	for (*path = 0; *log != 0; ) {
		switch (*log) {
		case '%' :
			++log;
			switch (*log) {
			case '%' :
				pathcatc(path, sizeof(path), '%');
				break;
			case 'T' :
				pathcat(path, sizeof(path), text_T);
				break;
			case 'D' :
				pathcat(path, sizeof(path), text_D);
				break;
			default :
				/* LCOV_EXCL_START */
				msg_error("Invalid type specifier '%c' in the log file.\n", *log);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		default :
			pathcatc(path, sizeof(path), *log);
			break;
		}
		++log;
	}

	stdlog = fopen(path, mode);
	if (!stdlog) {
		/* LCOV_EXCL_START */
		msg_error("Error opening the log file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void log_close(const char* log)
{
	if (stdlog != stdout && stdlog != stderr && stdlog != 0) {
		if (fclose(stdlog) != 0) {
			/* LCOV_EXCL_START */
			msg_error("Error closing the log file '%s'. %s.\n", log, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	stdlog = 0;
}

/****************************************************************************/
/* config */

void config(char* conf, size_t conf_size, const char* argv0)
{
#ifdef _WIN32
	char* slash;

	pathimport(conf, conf_size, argv0);

	slash = strrchr(conf, '/');
	if (slash) {
		slash[1] = 0;
		pathcat(conf, conf_size, PACKAGE ".conf");
	} else {
		pathcpy(conf, conf_size, PACKAGE ".conf");
	}
#else
	(void)argv0;

	pathcpy(conf, conf_size, "/etc/" PACKAGE ".conf");
#endif
}

/****************************************************************************/
/* main */

#define OPT_TEST_SKIP_SELF 256
#define OPT_TEST_KILL_AFTER_SYNC 257
#define OPT_TEST_EXPECT_UNRECOVERABLE 258
#define OPT_TEST_EXPECT_RECOVERABLE 259
#define OPT_TEST_SKIP_SIGN 260
#define OPT_TEST_SKIP_FALLOCATE 261
#define OPT_TEST_SKIP_DEVICE 262
#define OPT_TEST_SKIP_SEQUENTIAL 263
#define OPT_TEST_FORCE_MURMUR3 264
#define OPT_TEST_FORCE_SPOOKY2 265
#define OPT_TEST_SKIP_LOCK 266
#define OPT_TEST_FORCE_ORDER_PHYSICAL 267
#define OPT_TEST_FORCE_ORDER_INODE 268
#define OPT_TEST_FORCE_ORDER_ALPHA 269
#define OPT_TEST_FORCE_ORDER_DIR 270
#define OPT_TEST_FORCE_SCRUB_AT 271
#define OPT_TEST_FORCE_SCRUB_EVEN 272
#define OPT_TEST_FORCE_CONTENT_WRITE 273
#define OPT_TEST_FORCE_CONTENT_TEXT 274
#define OPT_TEST_SKIP_CONTENT_CHECK 275
#define OPT_TEST_SKIP_PARITY_ACCESS 276
#define OPT_TEST_EXPECT_FAILURE 277
#define OPT_TEST_RUN 278
#define OPT_TEST_FORCE_SCAN_WINFIND 279
#define OPT_TEST_IMPORT_CONTENT 280
#define OPT_TEST_FORCE_PROGRESS 281
#define OPT_TEST_SKIP_DISK_ACCESS 282
#define OPT_TEST_FORCE_AUTOSAVE_AT 283

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "filter", 1, 0, 'f' },
	{ "filter-disk", 1, 0, 'd' },
	{ "filter-missing", 0, 0, 'm' },
	{ "filter-error", 0, 0, 'e' },
	{ "percentage", 1, 0, 'p' },
	{ "older-than", 1, 0, 'o' },
	{ "start", 1, 0, 'S' },
	{ "count", 1, 0, 'B' },
	{ "error-limit", 1, 0, 'L' },
	{ "import", 1, 0, 'i' },
	{ "log", 1, 0, 'l' },
	{ "force-zero", 0, 0, 'Z' },
	{ "force-empty", 0, 0, 'E' },
	{ "force-uuid", 0, 0, 'U' },
	{ "force-device", 0, 0, 'D' },
	{ "force-nocopy", 0, 0, 'N' },
	{ "force-full", 0, 0, 'F' },
	{ "audit-only", 0, 0, 'a' },
	{ "pre-hash", 0, 0, 'h' },
	{ "speed-test", 0, 0, 'T' }, /* undocumented speed test command */
	{ "gen-conf", 1, 0, 'C' },
	{ "verbose", 0, 0, 'v' },
	{ "quiet", 0, 0, 'q' }, /* undocumented quiet option */
	{ "gui", 0, 0, 'G' }, /* undocumented GUI interface option */
	{ "help", 0, 0, 'H' },
	{ "version", 0, 0, 'V' },

	/* The following are test specific options, DO NOT USE! */

	/* After syncing, do not write the new content file */
	{ "test-kill-after-sync", 0, 0, OPT_TEST_KILL_AFTER_SYNC },

	/* Exit with failure if after check/fix there ARE NOT unrecoverable errors. */
	{ "test-expect-unrecoverable", 0, 0, OPT_TEST_EXPECT_UNRECOVERABLE },

	/* Exit with failure if after check/fix there ARE NOT recoverable errors. */
	{ "test-expect-recoverable", 0, 0, OPT_TEST_EXPECT_RECOVERABLE },

	/* Skip the initial self test */
	{ "test-skip-self", 0, 0, OPT_TEST_SKIP_SELF },

	/* Skip the initial sign check when reading the content file */
	{ "test-skip-sign", 0, 0, OPT_TEST_SKIP_SIGN },

	/* Skip the fallocate() when growing the parity files */
	{ "test-skip-fallocate", 0, 0, OPT_TEST_SKIP_FALLOCATE },

	/* Skip the sequential hint when reading files */
	{ "test-skip-sequential", 0, 0, OPT_TEST_SKIP_SEQUENTIAL },

	/* Skip the device check */
	{ "test-skip-device", 0, 0, OPT_TEST_SKIP_DEVICE },

	/* Force Murmur3 hash */
	{ "test-force-murmur3", 0, 0, OPT_TEST_FORCE_MURMUR3 },

	/* Force Spooky2 hash */
	{ "test-force-spooky2", 0, 0, OPT_TEST_FORCE_SPOOKY2 },

	/* Skip the use of lock file */
	{ "test-skip-lock", 0, 0, OPT_TEST_SKIP_LOCK },

	/* Force a sort order for files */
	{ "test-force-order-physical", 0, 0, OPT_TEST_FORCE_ORDER_PHYSICAL },
	{ "test-force-order-inode", 0, 0, OPT_TEST_FORCE_ORDER_INODE },
	{ "test-force-order-alpha", 0, 0, OPT_TEST_FORCE_ORDER_ALPHA },
	{ "test-force-order-dir", 0, 0, OPT_TEST_FORCE_ORDER_DIR },

	/* Force scrub of the specified number of blocks */
	{ "test-force-scrub-at", 1, 0, OPT_TEST_FORCE_SCRUB_AT },

	/* Force scrub of all the even blocks. This is really for testing, don't try it */
	{ "test-force-scrub-even", 0, 0, OPT_TEST_FORCE_SCRUB_EVEN },

	/* Force write of the content file even if no modification is done */
	{ "test-force-content-write", 0, 0, OPT_TEST_FORCE_CONTENT_WRITE },

	/* Force the use of text content file */
	{ "test-force-content-text", 0, 0, OPT_TEST_FORCE_CONTENT_TEXT },

	/* Relax the checks done at the content file */
	{ "test-skip-content-check", 0, 0, OPT_TEST_SKIP_CONTENT_CHECK },

	/* Skip the parity access */
	{ "test-skip-parity-access", 0, 0, OPT_TEST_SKIP_PARITY_ACCESS },

	/* Exit generic failure */
	{ "test-expect-failure", 0, 0, OPT_TEST_EXPECT_FAILURE },

	/* Run some command after loading the state and before the command */
	{ "test-run", 1, 0, OPT_TEST_RUN },

	/* Use the FindFirst/Next approach in Windows to list files */
	{ "test-force-scan-winfind", 0, 0, OPT_TEST_FORCE_SCAN_WINFIND },

	/* Alternative import working by data */
	{ "test-import-content", 1, 0, OPT_TEST_IMPORT_CONTENT },

	/* Force immediate progress state update */
	{ "test-force-progress", 0, 0, OPT_TEST_FORCE_PROGRESS },

	/* Skip the disk access */
	{ "test-skip-disk-access", 0, 0, OPT_TEST_SKIP_DISK_ACCESS },

	/* Force autosave at the specified block */
	{ "test-force-autosave-at", 1, 0, OPT_TEST_FORCE_AUTOSAVE_AT },

	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:f:d:mep:o:S:B:L:i:l:ZEUDNFahTC:vqHVG"

volatile int global_interrupt = 0;

/* LCOV_EXCL_START */
void signal_handler(int signal)
{
	switch (signal) {
	case SIGINT :
		global_interrupt = 1;
		break;
	}
}
/* LCOV_EXCL_STOP */

#define OPERATION_DIFF 0
#define OPERATION_SYNC 1
#define OPERATION_CHECK 2
#define OPERATION_FIX 3
#define OPERATION_DRY 4
#define OPERATION_DUP 5
#define OPERATION_LIST 6
#define OPERATION_POOL 7
#define OPERATION_REHASH 8
#define OPERATION_SCRUB 9
#define OPERATION_STATUS 10
#define OPERATION_REWRITE 11
#define OPERATION_NANO 12
#define OPERATION_SPINUP 13
#define OPERATION_SPINDOWN 14
#define OPERATION_DEVICES 15
#define OPERATION_SMART 16

int main(int argc, char* argv[])
{
	int c;
	struct snapraid_option opt;
	char conf[PATH_MAX];
	struct snapraid_state state;
	int operation;
	block_off_t blockstart;
	block_off_t blockcount;
	int ret;
	tommy_list filterlist_file;
	tommy_list filterlist_disk;
	int filter_missing;
	int filter_error;
	int percentage;
	int olderthan;
	char* e;
	const char* command;
	const char* import_timestamp;
	const char* import_content;
	const char* log;
	int lock;
	const char* gen_conf;
	const char* run;
	int speedtest;
	int period;
	time_t t;
	struct tm* tm;
	int i;

	/* defaults */
	config(conf, sizeof(conf), argv[0]);
	memset(&opt, 0, sizeof(opt));
	opt.io_error_limit = 100;
	blockstart = 0;
	blockcount = 0;
	tommy_list_init(&filterlist_file);
	tommy_list_init(&filterlist_disk);
	period = 1000;
	filter_missing = 0;
	filter_error = 0;
	percentage = -1;
	olderthan = -1;
	import_timestamp = 0;
	import_content = 0;
	log = 0;
	lock = 0;
	gen_conf = 0;
	speedtest = 0;
	run = 0;

	opterr = 0;
	while ((c =
#if HAVE_GETOPT_LONG
		getopt_long(argc, argv, OPTIONS, long_options, 0))
#else
		getopt(argc, argv, OPTIONS))
#endif
		!= EOF) {
		switch (c) {
		case 'c' :
			pathimport(conf, sizeof(conf), optarg);
			break;
		case 'f' : {
			struct snapraid_filter* filter = filter_alloc_file(1, optarg);
			if (!filter) {
				/* LCOV_EXCL_START */
				msg_error("Invalid filter specification '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&filterlist_file, &filter->node, filter);
		} break;
		case 'd' : {
			struct snapraid_filter* filter = filter_alloc_disk(1, optarg);
			if (!filter) {
				/* LCOV_EXCL_START */
				msg_error("Invalid filter specification '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&filterlist_disk, &filter->node, filter);
		} break;
		case 'm' :
			filter_missing = 1;
			break;
		case 'e' :
			/* when processing only error, we filter both files and blocks */
			/* and we apply fixes only to synced ones */
			filter_error = 1;
			opt.badonly = 1;
			opt.syncedonly = 1;
			break;
		case 'p' :
			percentage = strtoul(optarg, &e, 10);
			if (!e || *e || percentage > 100) {
				/* LCOV_EXCL_START */
				msg_error("Invalid percentage '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'o' :
			olderthan = strtoul(optarg, &e, 10);
			if (!e || *e || olderthan > 1000) {
				/* LCOV_EXCL_START */
				msg_error("Invalid number of days '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'S' :
			blockstart = strtoul(optarg, &e, 0);
			if (!e || *e) {
				/* LCOV_EXCL_START */
				msg_error("Invalid start position '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'B' :
			blockcount = strtoul(optarg, &e, 0);
			if (!e || *e) {
				/* LCOV_EXCL_START */
				msg_error("Invalid count number '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'L' :
			opt.io_error_limit = strtoul(optarg, &e, 0);
			if (!e || *e) {
				/* LCOV_EXCL_START */
				msg_error("Invalid error limit number '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'i' :
			if (import_timestamp) {
				/* LCOV_EXCL_START */
				msg_error("Import directory '%s' already specified as '%s'\n", optarg, import_timestamp);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			import_timestamp = optarg;
			break;
		case OPT_TEST_IMPORT_CONTENT :
			if (import_content) {
				/* LCOV_EXCL_START */
				msg_error("Import directory '%s' already specified as '%s'\n", optarg, import_content);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			import_content = optarg;
			break;
		case 'l' :
			if (log) {
				/* LCOV_EXCL_START */
				msg_error("Log file '%s' already specified as '%s'\n", optarg, log);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			log = optarg;
			break;
		case 'Z' :
			opt.force_zero = 1;
			break;
		case 'E' :
			opt.force_empty = 1;
			break;
		case 'U' :
			opt.force_uuid = 1;
			break;
		case 'D' :
			opt.force_device = 1;
			break;
		case 'N' :
			opt.force_nocopy = 1;
			break;
		case 'F' :
			opt.force_full = 1;
			break;
		case 'a' :
			opt.auditonly = 1;
			break;
		case 'h' :
			opt.prehash = 1;
			break;
		case 'v' :
			++msg_level;
			break;
		case 'q' :
			--msg_level;
			break;
		case 'G' :
			opt.gui = 1;
			break;
		case 'H' :
			usage();
			exit(EXIT_SUCCESS);
		case 'V' :
			version();
			exit(EXIT_SUCCESS);
		case 'T' :
			speedtest = 1;
			break;
		case 'C' :
			gen_conf = optarg;
			break;
		case OPT_TEST_KILL_AFTER_SYNC :
			opt.kill_after_sync = 1;
			break;
		case OPT_TEST_EXPECT_UNRECOVERABLE :
			opt.expect_unrecoverable = 1;
			break;
		case OPT_TEST_EXPECT_RECOVERABLE :
			opt.expect_recoverable = 1;
			break;
		case OPT_TEST_SKIP_SELF :
			opt.skip_self = 1;
			break;
		case OPT_TEST_SKIP_SIGN :
			opt.skip_sign = 1;
			break;
		case OPT_TEST_SKIP_FALLOCATE :
			opt.skip_fallocate = 1;
			break;
		case OPT_TEST_SKIP_SEQUENTIAL :
			opt.skip_sequential = 1;
			break;
		case OPT_TEST_SKIP_DEVICE :
			opt.skip_device = 1;
			period = 50; /* reduce period of the speed test */
			break;
		case OPT_TEST_SKIP_CONTENT_CHECK :
			opt.skip_content_check = 1;
			break;
		case OPT_TEST_SKIP_PARITY_ACCESS :
			opt.skip_parity_access = 1;
			break;
		case OPT_TEST_SKIP_DISK_ACCESS :
			opt.skip_disk_access = 1;
			break;
		case OPT_TEST_FORCE_MURMUR3 :
			opt.force_murmur3 = 1;
			break;
		case OPT_TEST_FORCE_SPOOKY2 :
			opt.force_spooky2 = 1;
			break;
		case OPT_TEST_SKIP_LOCK :
			opt.skip_lock = 1;
			break;
		case OPT_TEST_FORCE_ORDER_PHYSICAL :
			opt.force_order = SORT_PHYSICAL;
			break;
		case OPT_TEST_FORCE_ORDER_INODE :
			opt.force_order = SORT_INODE;
			break;
		case OPT_TEST_FORCE_ORDER_ALPHA :
			opt.force_order = SORT_ALPHA;
			break;
		case OPT_TEST_FORCE_ORDER_DIR :
			opt.force_order = SORT_DIR;
			break;
		case OPT_TEST_FORCE_SCRUB_AT :
			opt.force_scrub_at = atoi(optarg);
			break;
		case OPT_TEST_FORCE_SCRUB_EVEN :
			opt.force_scrub_even = 1;
			break;
		case OPT_TEST_FORCE_CONTENT_WRITE :
			opt.force_content_write = 1;
			break;
		case OPT_TEST_FORCE_CONTENT_TEXT :
			opt.force_content_text = 1;
			break;
		case OPT_TEST_EXPECT_FAILURE :
			/* invert the exit code */
			exit_success = 1;
			exit_failure = 0;
			break;
		case OPT_TEST_RUN :
			run = optarg;
			break;
		case OPT_TEST_FORCE_SCAN_WINFIND :
			opt.force_scan_winfind = 1;
			break;
		case OPT_TEST_FORCE_PROGRESS :
			opt.force_progress = 1;
			break;
		case OPT_TEST_FORCE_AUTOSAVE_AT :
			opt.force_autosave_at = atoi(optarg);
			break;
		default :
			/* LCOV_EXCL_START */
			msg_error("Unknown option '%c'\n", (char)c);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	os_init(opt.force_scan_winfind);
	raid_init();
	crc32c_init();

	if (speedtest != 0) {
		speed(period);
		os_done();
		exit(EXIT_SUCCESS);
	}

	if (gen_conf != 0) {
		generate_configuration(gen_conf);
		os_done();
		exit(EXIT_SUCCESS);
	}

	if (optind + 1 != argc) {
		/* LCOV_EXCL_START */
		usage();
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	command = argv[optind];
	if (strcmp(command, "diff") == 0) {
		operation = OPERATION_DIFF;
	} else if (strcmp(argv[optind], "sync") == 0) {
		operation = OPERATION_SYNC;
	} else if (strcmp(argv[optind], "check") == 0) {
		operation = OPERATION_CHECK;
	} else if (strcmp(argv[optind], "fix") == 0) {
		operation = OPERATION_FIX;
	} else if (strcmp(argv[optind], "test-dry") == 0) {
		operation = OPERATION_DRY;
	} else if (strcmp(argv[optind], "dup") == 0) {
		operation = OPERATION_DUP;
	} else if (strcmp(argv[optind], "list") == 0) {
		operation = OPERATION_LIST;
	} else if (strcmp(argv[optind], "pool") == 0) {
		operation = OPERATION_POOL;
	} else if (strcmp(argv[optind], "rehash") == 0) {
		operation = OPERATION_REHASH;
	} else if (strcmp(argv[optind], "scrub") == 0) {
		operation = OPERATION_SCRUB;
	} else if (strcmp(argv[optind], "status") == 0) {
		operation = OPERATION_STATUS;
	} else if (strcmp(argv[optind], "test-rewrite") == 0) {
		operation = OPERATION_REWRITE;
	} else if (strcmp(argv[optind], "test-nano") == 0) {
		operation = OPERATION_NANO;
	} else if (strcmp(argv[optind], "up") == 0) {
		operation = OPERATION_SPINUP;
	} else if (strcmp(argv[optind], "down") == 0) {
		operation = OPERATION_SPINDOWN;
	} else if (strcmp(argv[optind], "test-devices") == 0) {
		operation = OPERATION_DEVICES;
	} else if (strcmp(argv[optind], "smart") == 0) {
		operation = OPERATION_SMART;
	} else {
		/* LCOV_EXCL_START */
		msg_error("Unknown command '%s'\n", argv[optind]);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* check options compatibility */
	switch (operation) {
	case OPERATION_CHECK :
		break;
	default :
		if (opt.auditonly) {
			/* LCOV_EXCL_START */
			msg_error("You cannot use -a, --audit-only with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case OPERATION_SYNC :
	case OPERATION_CHECK :
	case OPERATION_FIX :
		break;
	default :
		if (opt.force_nocopy) {
			/* LCOV_EXCL_START */
			msg_error("You cannot use -N, --force-nocopy with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case OPERATION_SYNC :
		break;
	default :
		if (opt.prehash) {
			/* LCOV_EXCL_START */
			msg_error("You cannot use -h, --pre-hash with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (opt.force_full) {
			/* LCOV_EXCL_START */
			msg_error("You cannot use -F, --force-full with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	if (opt.force_full && opt.force_nocopy) {
		/* LCOV_EXCL_START */
		msg_error("You cannot use the -F, --force-full and -N, --force-nocopy options at the same time\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (opt.prehash && opt.force_nocopy) {
		/* LCOV_EXCL_START */
		msg_error("You cannot use the -h, --pre-hash and -N, --force-nocopy options at the same time\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	switch (operation) {
	case OPERATION_CHECK :
	case OPERATION_FIX :
	case OPERATION_DRY :
		break;
	default :
		if (!tommy_list_empty(&filterlist_file)) {
			/* LCOV_EXCL_START */
			msg_error("You cannot use -f, --filter with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (!tommy_list_empty(&filterlist_disk)) {
			/* LCOV_EXCL_START */
			msg_error("You cannot use -d, --filter-disk with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (filter_missing != 0) {
			/* LCOV_EXCL_START */
			msg_error("You cannot use -m, --filter-missing with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (filter_error != 0) {
			/* LCOV_EXCL_START */
			msg_error("You cannot use -e, --filter-error with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case OPERATION_CHECK :
	case OPERATION_FIX :
		break;
	default :
		if (import_timestamp != 0 || import_content != 0) {
			/* LCOV_EXCL_START */
			msg_error("You cannot import with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case OPERATION_LIST :
	case OPERATION_DUP :
	case OPERATION_STATUS :
	case OPERATION_REWRITE :
	case OPERATION_REHASH :
	case OPERATION_SPINUP : /* we want to do it in different threads to avoid blocking */
		/* avoid to check and access data disks if not needed */
		opt.skip_disk_access = 1;
		break;
	}

	switch (operation) {
	case OPERATION_DIFF :
	case OPERATION_LIST :
	case OPERATION_DUP :
	case OPERATION_POOL :
	case OPERATION_STATUS :
	case OPERATION_REWRITE :
	case OPERATION_REHASH :
	case OPERATION_NANO :
	case OPERATION_SPINUP : /* we want to do it in different threads to avoid blocking */
		/* avoid to check and access parity disks if not needed */
		opt.skip_parity_access = 1;
		break;
	}

	switch (operation) {
	case OPERATION_DIFF :
	case OPERATION_LIST :
	case OPERATION_DUP :
	case OPERATION_POOL :
	case OPERATION_NANO :
	case OPERATION_SPINUP :
	case OPERATION_SPINDOWN :
	case OPERATION_DEVICES :
	case OPERATION_SMART :
		opt.skip_self = 1;
		break;
	}

	switch (operation) {
	case OPERATION_DEVICES :
	case OPERATION_SMART :
		/* we may need to use these commands during operations */
		opt.skip_lock = 1;
		break;
	}

	/* open the log file */
	log_open(log);

	/* print generic info into the log */
	t = time(0);
	tm = localtime(&t);
	msg_tag("version:%s\n", PACKAGE_VERSION);
	msg_tag("unixtime:%" PRIi64 "\n", (int64_t)t);
	if (tm) {
		char datetime[64];
		strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", tm);
		msg_tag("time:%s\n", datetime);
	}
	msg_tag("command:%s\n", command);
	for (i = 0; i < argc; ++i)
		msg_tag("argv:%u:%s\n", i, argv[i]);
	msg_flush();

	if (!opt.skip_self)
		selftest();

	state_init(&state);

	/* read the configuration file */
	state_config(&state, conf, command, &opt, &filterlist_disk);

	/* set the raid mode */
	raid_mode(state.raid_mode);

#if HAVE_LOCKFILE
	/* create the lock file */
	if (!opt.skip_lock) {
		lock = lock_lock(state.lockfile);
		if (lock == -1) {
			/* LCOV_EXCL_START */
			if (errno != EWOULDBLOCK) {
				msg_error("Error creating the lock file '%s'. %s.\n", state.lockfile, strerror(errno));
			} else {
				msg_error("The lock file '%s' is already locked!\n", state.lockfile);
				msg_error("SnapRAID is already in use!\n");
			}
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
#else
	(void)lock;
#endif

	if (operation == OPERATION_DIFF) {
		state_read(&state);

		ret = state_diff(&state);

		/* abort if required */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	} else if (operation == OPERATION_SYNC) {

		/* in the next state read ensures to clear all the past hashes in case */
		/* we are reading from an incomplete sync */
		/* The undeterminated hash are only for CHG/DELETED blocks for which we don't */
		/* know if the previous interrupted sync was able to update or not the parity */
		/* The sync process instead needs to trust this information because it's used */
		/* to avoid to recompute the parity if all the input are equals as before. */
		state.clear_past_hash = 1;

		state_read(&state);

		state_scan(&state);

		/* refresh the size info before the content write */
		state_refresh(&state);

		memory();

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		/* save the new state before the sync */
		/* this allow to recover the case of the changes in the array after an aborted sync. */

		/* for example, think at this case: */
		/* - add some files at the array */
		/* - run a sync command, it will recompute the parity adding the new files */
		/* - abort the sync command before it stores the new content file */
		/* - delete the not yet synced files from the array */
		/* - run a new sync command */

		/* the new sync command has now way to know that the parity file was modified */
		/* because the files triggering these changes are now deleted */
		/* and they aren't listed in the content file */

		if (state.need_write)
			state_write(&state);

		/* run a test command if required */
		if (run != 0) {
			ret = system(run); /* ignore error */
			if (ret != 0) {
				/* LCOV_EXCL_START */
				msg_error("Error in running command '%s'.\n", run);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		/* waits some time to ensure that any concurrent modification done at the files, */
		/* using the same mtime read by the scan process, will be read by sync. */
		/* Note that any later modification done, potentially not read by sync, will have */
		/* a different mtime, and it will be syncronized at the next sync. */
		/* The worst case is the FAT filesystem with a two seconds resolution for mtime. */
		/* If you don't use FAT, the wait is not needed, because most filesystems have now */
		/* at least microseconds resolution, but better to be safe. */
		if (!opt.skip_self)
			sleep(2);

		ret = state_sync(&state, blockstart, blockcount);

		/* save the new state if required */
		if (!opt.kill_after_sync && (state.need_write || state.opt.force_content_write))
			state_write(&state);

		/* abort if required */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	} else if (operation == OPERATION_DRY) {
		state_read(&state);

		/* apply the command line filter */
		state_filter(&state, &filterlist_file, &filterlist_disk, filter_missing, filter_error);

		memory();

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		state_dry(&state, blockstart, blockcount);
	} else if (operation == OPERATION_REHASH) {
		state_read(&state);

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		state_rehash(&state);

		/* save the new state if required */
		if (state.need_write)
			state_write(&state);
	} else if (operation == OPERATION_SCRUB) {
		state_read(&state);

		memory();

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		ret = state_scrub(&state, percentage, olderthan);

		/* save the new state if required */
		if (state.need_write || state.opt.force_content_write)
			state_write(&state);

		/* abort if required */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	} else if (operation == OPERATION_REWRITE) {
		state_read(&state);

		state_write(&state);

		memory();
	} else if (operation == OPERATION_NANO) {
		state_read(&state);

		state_nano(&state);

		state_write(&state);

		memory();
	} else if (operation == OPERATION_SPINUP) {
		state_device(&state, DEVICE_UP);
	} else if (operation == OPERATION_SPINDOWN) {
		state_device(&state, DEVICE_DOWN);
	} else if (operation == OPERATION_DEVICES) {
		state_device(&state, DEVICE_LIST);
	} else if (operation == OPERATION_SMART) {
		state_device(&state, DEVICE_SMART);
	} else if (operation == OPERATION_STATUS) {
		state_read(&state);

		memory();

		state_status(&state);
	} else if (operation == OPERATION_DUP) {
		state_read(&state);

		state_dup(&state);
	} else if (operation == OPERATION_LIST) {
		state_read(&state);

		state_list(&state);
	} else if (operation == OPERATION_POOL) {
		state_read(&state);

		state_pool(&state);
	} else {
		state_read(&state);

		/* if we are also trying to recover */
		if (!state.opt.auditonly) {
			/* import the user specified dirs */
			if (import_timestamp != 0)
				state_search(&state, import_timestamp);
			if (import_content != 0)
				state_import(&state, import_content);

			/* import from all the array */
			if (!state.opt.force_nocopy)
				state_search_array(&state);
		}

		/* apply the command line filter */
		state_filter(&state, &filterlist_file, &filterlist_disk, filter_missing, filter_error);

		memory();

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		if (operation == OPERATION_CHECK) {
			ret = state_check(&state, 0, blockstart, blockcount);
		} else { /* it's fix */
			ret = state_check(&state, 1, blockstart, blockcount);
		}

		/* abort if required */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	/* close log file */
	log_close(log);

#if HAVE_LOCKFILE
	if (!opt.skip_lock) {
		if (lock_unlock(lock) == -1) {
			/* LCOV_EXCL_START */
			msg_error("Error closing the lock file '%s'. %s.\n", state.lockfile, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	state_done(&state);
	tommy_list_foreach(&filterlist_file, (tommy_foreach_func*)filter_free);
	tommy_list_foreach(&filterlist_disk, (tommy_foreach_func*)filter_free);

	os_done();

	return EXIT_SUCCESS;
}

