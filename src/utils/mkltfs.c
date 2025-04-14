/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2020 IBM Corp. All rights reserved.
**
**  Redistribution and use in source and binary forms, with or without
**   modification, are permitted provided that the following conditions
**  are met:
**  1. Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**  2. Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in the
**  documentation and/or other materials provided with the distribution.
**  3. Neither the name of the copyright holder nor the names of its
**     contributors may be used to endorse or promote products derived from
**     this software without specific prior written permission.
**
**  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
**  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
**  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
**  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
**  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
**  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
**  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
**  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
**  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
**  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
**  POSSIBILITY OF SUCH DAMAGE.
**
**
**  OO_Copyright_END
**
*************************************************************************************
**
** COMPONENT NAME:  IBM Linear Tape File System
**
** FILE NAME:       utils/mkltfs.c
**
** DESCRIPTION:     Formatter utility for LTFS volumes.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
*************************************************************************************
*/

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#else
#include <syslog.h>
#endif /* mingw_PLATFORM */


#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>
#include <getopt.h>
#include "libltfs/ltfs.h"
#include "ltfs_copyright.h"
#include "libltfs/index_criteria.h"
#include "libltfs/pathname.h"
#include "libltfs/plugin.h"
#include "libltfs/kmi.h"
#include "libltfs/tape.h"

static volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

#ifdef __APPLE__
#include "libltfs/arch/osx/osx_string.h"
#endif

#define INDEX_PART_ID 'a'
#define DATA_PART_ID 'b'
#define INDEX_PART_NUM 0
#define DATA_PART_NUM 1

#ifdef mingw_PLATFORM
char *bin_mkltfs_dat;
#else
extern char bin_mkltfs_dat[];
#endif

struct other_format_opts {
	struct config_file *config; /**< Configuration data read from the global LTFS config file */
	char *devname;              /**< Device to format */
	char *backend_path;         /**< Path to tape backend shared library */
	char *kmi_backend_name;     /**< Name or path to the key manager interface backend library */
	char *volume_name;          /**< Human-readable volume name */
	char *filterrules;          /**< Rules for files that should go to the index partition */
	char *barcode;              /**< 6-character cartridge barcode number */
	unsigned long blocksize;    /**< Nominal tape block size */
	bool enable_compression;    /**< Use compression on the tape? */
	bool allow_update;          /**< Allow overriding index rules at mount time? */
	bool keep_capacity;         /**< Reset tape capacity? */
	bool unformat;              /**< Unformat medium? */
	bool force;                 /**< Force to format */
	bool quiet;                 /**< Quiet mode indicator */
	bool trace;                 /**< Debug mode indicator */
	bool syslogtrace;           /**< Generate debug output to stderr and syslog*/
	bool fulltrace;             /**< Full trace mode indicator */
	bool long_wipe;             /**< Clean up whole tape by long erase? */
	bool destructive;           /**< Use destructive FORMAT_MEDIUM or not */
};

/* Forward declarations */
int format_tape(struct ltfs_volume *vol, struct other_format_opts *opt, void *args);
int unformat_tape(struct ltfs_volume *vol, struct other_format_opts *opt, void *args);
int _mkltfs_validate_options(char *prg_name, struct ltfs_volume *vol,
	struct other_format_opts *opt);

/* Command line options */
static const char *short_options = "i:e:d:b:s:n:r:cokwfqtxhpV";
static struct option long_options[] = {
	{"config",          1, 0, 'i'},
	{"backend",         1, 0, 'e'},
	{"device",          1, 0, 'd'},
	{"blocksize",       1, 0, 'b'},
	{"tape-serial",     1, 0, 's'},
	{"volume-name",     1, 0, 'n'},
	{"rules",           1, 0, 'r'},
	{"kmi-backend",     1, 0, '-'},
	{"no-compression",  0, 0, 'c'},
	{"no-override",     0, 0, ' '},
	{"keep-capacity",   0, 0, 'k'},
	{"wipe",            0, 0, 'w'},
	{"long-wipe",       0, 0, '+'},
	{"destructive",     0, 0, '&'},
	{"force",           0, 0, 'f'},
	{"quiet",           0, 0, 'q'},
	{"trace",           0, 0, 't'},
	{"syslogtrace",     0, 0, '!'},
	{"fulltrace",       0, 0, 'x'},
	{"help",            0, 0, 'h'},
	{"advanced-help",   0, 0, 'p'},
	{"version",			0, 0, 'V'},
	{0, 0, 0, 0}
};

static void show_usage(char *appname, struct config_file *config, bool full)
{
	struct libltfs_plugin backend;
	const char *default_backend;
	char *devname = NULL;

	default_backend = config_file_get_default_plugin("tape", config);
	if (default_backend && plugin_load(&backend, "tape", default_backend, config) == 0) {
		devname = SAFE_STRDUP(ltfs_default_device_name(backend.ops));
		plugin_unload(&backend);
	}

	if (! devname)
		devname = SAFE_STRDUP("<devname>");

	fprintf(stderr, "\n");
	ltfsresult(15400I, appname);  /* Usage: %s <options> */
	fprintf(stderr, "\n");
	ltfsresult(15401I);           /* Available options are: */
	ltfsresult(15402I);           /* -d, --device=<name> */
	ltfsresult(15420I);           /* -f, --force */
	ltfsresult(15403I);           /* -s, --tape-serial=<id> */
	ltfsresult(15404I);           /* -n, --volume-name */
	ltfsresult(15405I);           /* -r, --rules=<rule[,rule]> */
	ltfsresult(15406I);           /*     --no-override */
	ltfsresult(15418I);           /* -w, --wipe */
	ltfsresult(15407I);           /* -q, --quiet */
	ltfsresult(15408I);           /* -t, --trace */
	ltfsresult(15422I);           /* --syslogtrace */
	ltfsresult(15423I);           /* -V, --version */
	ltfsresult(15409I);           /* -h, --help */
	ltfsresult(15412I);           /* -p, --advanced-help */
	if (full) {
		ltfsresult(15413I, LTFS_CONFIG_FILE);       /* -i, --config=<file> */
		ltfsresult(15414I, default_backend);        /* -e, --backend */
		ltfsresult(15421I, config_file_get_default_plugin("kmi", config)); /* --kmi-backend */
		ltfsresult(15415I, LTFS_DEFAULT_BLOCKSIZE); /* -b, --blocksize */
		ltfsresult(15416I);                         /* -c, --no-compression */
		ltfsresult(15419I);                         /* -k, --keep-capacity */
		ltfsresult(15417I);                         /* -x, --fulltrace */
		ltfsresult(15424I);                         /* --long-wipe */
		ltfsresult(15425I);                         /* --destructive */
		fprintf(stderr, "\n");
		plugin_usage(appname, "driver", config);
		fprintf(stderr, "\n");
		plugin_usage(appname, "kmi", config);
	}

	fprintf(stderr, "\n");
	ltfsresult(15410I); /* Usage example: */
	ltfsresult(15411I, appname, devname, "size=100K"); /* %s --device=%s --rules="%s" */
	ltfsresult(15411I, appname, devname, "size=1M/name=*.jpg");
	ltfsresult(15411I, appname, devname, "size=1M/name=*.jpg:*.png");

	free(devname);
}


/* Operation */
int main(int argc, char **argv)
{
	struct ltfs_volume *newvol;
	struct other_format_opts opt;
	int ret, log_level, syslog_level, i, cmd_args_len;
	char *lang, *cmd_args;
	const char *config_file = NULL;
	void *message_handle;
	/* To do some tests sometimes GPV
	struct slmain_device_info* wt_info = (struct slmain_device_info*)malloc(sizeof(struct slmain_device_info*));
	slif_get_device_info("5.0.0.0", wt_info);
	*/
	int fuse_argc = argc;
	char **fuse_argv = calloc(fuse_argc, sizeof(char *));
	if (! fuse_argv) {
		return MKLTFS_OPERATIONAL_ERROR;
	}
	for (i = 0; i < fuse_argc; ++i) {
		fuse_argv[i] = SAFE_STRDUP(argv[i]);
		if (! fuse_argv[i]) {
			return MKLTFS_OPERATIONAL_ERROR;
		}
	}
	struct fuse_args args = FUSE_ARGS_INIT(fuse_argc, fuse_argv);

	/* Check for LANG variable and set it to en_US.UTF-8 if it is unset. */
	SAFE_GETENV(lang,"LANG");
	if (! lang) {
		fprintf(stderr, "LTFS9015W Setting the locale to 'en_US.UTF-8'. If this is wrong, please set the LANG environment variable before starting mkltfs.\n");
		ret = setenv("LANG", "en_US.UTF-8", 1);
		if (ret) {
			fprintf(stderr, "LTFS9016E Cannot set the LANG environment variable\n");
			return MKLTFS_OPERATIONAL_ERROR;
		}
	}

	/* Start up libltfs with the default logging level. */
#ifndef mingw_PLATFORM
	openlog("mkltfs", LOG_PID, LOG_USER);
#endif
	ret = ltfs_init(LTFS_INFO, true, false);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10000E, ret);
		return MKLTFS_OPERATIONAL_ERROR;
	}

	/*  Setup signal handler to terminate cleanly */
	ret = ltfs_set_signal_handlers();
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10013E);
		return MKLTFS_OPERATIONAL_ERROR;
	}

	/* Register messages with libltfs */
	ret = ltfsprintf_load_plugin("bin_mkltfs", bin_mkltfs_dat, &message_handle);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10012E, ret);
		return MKLTFS_OPERATIONAL_ERROR;
	}

	/* Set up empty format options and load the configuration file. */
	memset(&opt, 0, sizeof(struct other_format_opts));
	opt.enable_compression = true;
	opt.allow_update = true;
	opt.unformat = false;
	opt.force = false;
	opt.quiet = false;
	opt.blocksize = LTFS_DEFAULT_BLOCKSIZE;
	opt.long_wipe = false;
	opt.destructive = false;

	/* Check for a config file path given on the command line */
	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, short_options, long_options, &option_index);
		if (c == -1)
			break;
		if (c == 'i') {
			config_file = SAFE_STRDUP(optarg);
			break;
		}
	}

	/* Load configuration file */
	ret = config_file_load(config_file, &opt.config);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10008E, ret);
		return MKLTFS_OPERATIONAL_ERROR;
	}

	/* Parse all command line arguments */
	optind = 1;
	int num_of_o = 0;
	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, short_options, long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 'i':
				break;
			case 'e':
				free(opt.backend_path);
				opt.backend_path = SAFE_STRDUP(optarg);
				break;
			case 'd':
				opt.devname = SAFE_STRDUP(optarg);
				break;
			case 'b':
				opt.blocksize = atoi(optarg);
				break;
			case 's':
				opt.barcode = SAFE_STRDUP(optarg);
				break;
			case 'n':
				opt.volume_name = SAFE_STRDUP(optarg);
				break;
			case 'r':
				opt.filterrules = SAFE_STRDUP(optarg);
				break;
			case '-':
				opt.kmi_backend_name = SAFE_STRDUP(optarg);
				break;
			case 'c':
				opt.enable_compression = false;
				break;
			case 'o':
				/* ignore -o here to parse them by fuse */
				++num_of_o;
				break;
			case ' ':
				opt.allow_update = false;
				break;
			case 'k':
				opt.keep_capacity = true;
				break;
			case 'w':
				opt.unformat = true;
				break;
			case 'f':
				opt.force = true;
				break;
			case '+':
				opt.unformat = true;
				opt.long_wipe = true;
				break;
			case '&':
				opt.destructive = true;
				break;
			case 'q':
				opt.quiet = true;
				break;
			case 't':
				opt.trace = true;
				break;
			case '!':
				opt.syslogtrace = true;
				break;
			case 'x':
				opt.fulltrace = true;
				break;
			case 'h':
				show_usage(argv[0], opt.config, false);
				return 0;
			case 'p':
				show_usage(argv[0], opt.config, true);
				return 0;
			case 'V':
				ltfsresult(15059I, "mkltfs", PACKAGE_VERSION);
				ltfsresult(15059I, "LTFS Format Specification", LTFS_INDEX_VERSION_STR);
				return 0;
			case '?':
			default:
				show_usage(argv[0], opt.config, false);
				return MKLTFS_USAGE_SYNTAX_ERROR;
		}
	}

	if (optind + num_of_o < argc) {
		show_usage(argv[0], opt.config, false);
		return MKLTFS_USAGE_SYNTAX_ERROR;
	}

	/* Pick up default backend if one wasn't specified before */
	if (! opt.backend_path) {
		const char *default_backend = config_file_get_default_plugin("tape", opt.config);
		if (! default_backend) {
			ltfsmsg(LTFS_ERR, 10009E);
			return MKLTFS_OPERATIONAL_ERROR;
		}
		opt.backend_path = SAFE_STRDUP(default_backend);
	}
	if (! opt.kmi_backend_name) {
		const char *default_backend = config_file_get_default_plugin("kmi", opt.config);
		if (default_backend)
			opt.kmi_backend_name = SAFE_STRDUP(default_backend);
		else
			opt.kmi_backend_name = SAFE_STRDUP("none");
	}
	if (opt.kmi_backend_name && strcmp(opt.kmi_backend_name, "none") == 0)
		opt.kmi_backend_name = NULL;

	/* Set the logging level */
	if (opt.quiet && (opt.trace || opt.fulltrace)) {
		ltfsmsg(LTFS_ERR, 9012E);
		show_usage(argv[0], opt.config, false);
		return 1;
	} else if (opt.quiet) {
		log_level = LTFS_WARN;
		syslog_level = LTFS_NONE;
	} else if (opt.trace) {
		log_level = LTFS_DEBUG;
		syslog_level = LTFS_NONE;
	} else if (opt.syslogtrace)
		log_level = syslog_level = LTFS_DEBUG;
	else if (opt.fulltrace) {
		log_level = LTFS_TRACE;
		syslog_level = LTFS_DEBUG;
	} else {
		log_level = LTFS_INFO;
		syslog_level = LTFS_NONE;
	}

	ltfs_set_log_level(log_level);
	ltfs_set_syslog_level(syslog_level);

	/* Starting mkltfs */
	ltfsmsg(LTFS_INFO, 15000I, PACKAGE_NAME, PACKAGE_VERSION, log_level);

	/* Show command line arguments */
	for (i = 0, cmd_args_len = 0 ; i < argc; i++) {
		cmd_args_len += strlen(argv[i]) + 1;
	}
	cmd_args = calloc(1, cmd_args_len + 1);
	if (!cmd_args) {
		/* Memory allocation failed */
		ltfsmsg(LTFS_ERR, 10001E, "mkltfs (arguments)");
		return MKLTFS_OPERATIONAL_ERROR;
	}
	SAFE_STRCAT_S(cmd_args, cmd_args_len,argv[0]);
	for (i = 1; i < argc; i++) {
		SAFE_STRCAT_S(cmd_args, cmd_args_len," ");
		SAFE_STRCAT_S(cmd_args, cmd_args_len, argv[i]);
	}
	ltfsmsg(LTFS_INFO, 15041I, cmd_args);
	free(cmd_args);

	/* Show build time information */
	ltfsmsg(LTFS_INFO, 15042I, BUILD_SYS_FOR);
	ltfsmsg(LTFS_INFO, 15043I, BUILD_SYS_GCC);

	/* Show run time information */
	show_runtime_system_info();

	/* Actually mkltfs logic starts here */
	ret = ltfs_volume_alloc("mkltfs", &newvol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15001E);
		return MKLTFS_OPERATIONAL_ERROR;
	}

	ret = ltfs_set_blocksize(opt.blocksize, newvol);
	if (ret < 0) {
		if (ret == -LTFS_SMALL_BLOCKSIZE)
			ltfsmsg(LTFS_ERR, 15028E, LTFS_MIN_BLOCKSIZE);
		show_usage(argv[0], opt.config, false);
		return MKLTFS_OPERATIONAL_ERROR;
	}
	ltfs_set_compression(opt.enable_compression, newvol);
	ret = ltfs_set_barcode(opt.barcode, newvol);
	if (ret < 0) {
		if (ret == -LTFS_BARCODE_LENGTH)
			ltfsmsg(LTFS_ERR, 15029E);
		else if (ret == -LTFS_BARCODE_INVALID)
			ltfsmsg(LTFS_ERR, 15030E);
		show_usage(argv[0], opt.config, false);
		return MKLTFS_USAGE_SYNTAX_ERROR;
	}

	if (_mkltfs_validate_options(argv[0], newvol, &opt)) {
		ltfsmsg(LTFS_ERR, 15002E);
		show_usage(argv[0], opt.config, false);
		return MKLTFS_USAGE_SYNTAX_ERROR;
	}

	ret = ltfs_fs_init();
	if (ret)
		return LTFSCK_OPERATIONAL_ERROR;;

	ltfsmsg(LTFS_INFO, 15003I, opt.devname);
	ltfsmsg(LTFS_INFO, 15004I, opt.blocksize);
	ltfsmsg(LTFS_INFO, 15005I, opt.filterrules ? opt.filterrules : "None");
	if (! opt.quiet)
		fprintf(stderr, "\n");

	if(opt.unformat)
		ret = unformat_tape(newvol, &opt, &args);
	else
		ret = format_tape(newvol, &opt, &args);


	SAFE_FREE(opt.backend_path);
	SAFE_FREE(opt.kmi_backend_name);
	SAFE_FREE(opt.devname);
	config_file_free(opt.config);
	ltfsprintf_unload_plugin(message_handle);
	ltfs_finish();

	return ret;
}

int format_tape(struct ltfs_volume *vol, struct other_format_opts *opt, void *args)
{
	int ret = MKLTFS_OPERATIONAL_ERROR;
	struct device_capacity cap;
	struct libltfs_plugin backend; /* tape driver backend */
	struct libltfs_plugin kmi; /* key manager interface backend */
	struct ltfs_volume *dummy_vol;
	bool is_worm;

	ret = ltfs_set_volume_name(opt->volume_name, vol);
	if (ret < 0)
		return MKLTFS_OPERATIONAL_ERROR;
	ret = ltfs_reset_capacity(!opt->keep_capacity, vol);
	if (ret < 0) {
		return MKLTFS_OPERATIONAL_ERROR;
	}

	/* load the backend, open the tape device, and load a tape */
	ltfsmsg(LTFS_DEBUG, 15006D);
	ret = plugin_load(&backend, "tape", opt->backend_path, opt->config);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15008E, opt->backend_path);
		return MKLTFS_OPERATIONAL_ERROR;
	}
	if (opt->kmi_backend_name) {
		ret = plugin_load(&kmi, "kmi", opt->kmi_backend_name, opt->config);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15050E, opt->kmi_backend_name);
			return MKLTFS_OPERATIONAL_ERROR;
		}
	}
	ret = ltfs_device_open(opt->devname, backend.ops, vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15009E, opt->devname, ret);
		ret = MKLTFS_OPERATIONAL_ERROR;
		goto out_unload_backend;
	}
	ret = ltfs_parse_tape_backend_opts(args, vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15054E);
		goto out_unload_backend;
	}
	if (opt->kmi_backend_name) {
		ret = kmi_init(&kmi, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15052E, opt->devname, ret);
			goto out_unload_backend;
		}

		ret = ltfs_parse_kmi_backend_opts(args, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15053E);
			goto out_unload_backend;
		}

		ret = tape_clear_key(vol->device, vol->kmi_handle);
		if (ret < 0)
			goto out_unload_backend;
	}

	{
		int i = 0;
		struct fuse_args *a = args;

		for (i = 0; i < a->argc && a->argv[i]; ++i) {
			if (!strcmp(a->argv[i], "-o")) {
				ltfsmsg(LTFS_ERR, 15055E, a->argv[i], a->argv[i + 1] ? a->argv[i + 1] : "");
				ret = MKLTFS_USAGE_SYNTAX_ERROR;
				goto out_unload_backend;
			}
		}
	}

	ltfs_load_tape(vol);
	ret = ltfs_wait_device_ready(vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15044E);
		ret = MKLTFS_OPERATIONAL_ERROR;
		goto out_close;
	}

	vol->append_only_mode = false;
	vol->set_pew = false;
	ret = ltfs_setup_device(vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15044E);
		ret = MKLTFS_OPERATIONAL_ERROR;
		goto out_close;
	}
	ltfsmsg(LTFS_DEBUG, 15007D);

	ltfs_set_partition_map(DATA_PART_ID, INDEX_PART_ID, DATA_PART_NUM, INDEX_PART_NUM, vol);

	/* Check target medium state */
	if (! opt->force) {
		ltfsmsg(LTFS_INFO, 15049I, "mount");
		ret = ltfs_volume_alloc("mkltfs", &dummy_vol);
		if (ret < 0) {
			ret = MKLTFS_OPERATIONAL_ERROR;
			goto out_close;
		}
		dummy_vol->device = vol->device;
		dummy_vol->kmi_handle = vol->kmi_handle;
		ret = ltfs_start_mount(true, dummy_vol);
		dummy_vol->device = NULL;
		dummy_vol->kmi_handle = NULL;
		if (ret != -LTFS_NOT_PARTITIONED && ret != -LTFS_LABEL_INVALID && ret != -LTFS_LABEL_MISMATCH) {
			if (ret == 0) {
				ltfsmsg(LTFS_ERR, 15047E, ret);
				ltfsmsg(LTFS_INFO, 15048I);
			}
			else if (ret == -EDEV_KEY_REQUIRED) {
				ltfsmsg(LTFS_ERR, 15056E);
				ltfsmsg(LTFS_INFO, 15057I);
			}
			ret = MKLTFS_USAGE_SYNTAX_ERROR;
			ltfs_volume_free(&dummy_vol);
			goto out_close;
		}
		ltfs_volume_free(&dummy_vol);
	}
	else {
		ltfsmsg(LTFS_INFO, 15049I, "load");
		ret = tape_load_tape(vol->device, vol->kmi_handle, false);
		if (ret < 0) {
			if (ret == -LTFS_UNSUPPORTED_MEDIUM)
				ltfsmsg(LTFS_ERR, 11298E);
			else
				ltfsmsg(LTFS_ERR, 11006E);

			ret = MKLTFS_OPERATIONAL_ERROR;
			goto out_close;
		}
	}

	if (tape_get_worm_status(vol->device, &is_worm) < 0) {
		ret = MKLTFS_OPERATIONAL_ERROR;
		goto out_close;
	}

	/* Set up index data: filter rules */
	ret = index_criteria_set_allow_update(is_worm? false : opt->allow_update, vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15014E, ret);
		ret = MKLTFS_OPERATIONAL_ERROR;
		goto out_close;
	}

	if (opt->filterrules) {
		if (is_worm) {
			ltfsmsg(LTFS_ERR, 15060E);
			ret = MKLTFS_USAGE_SYNTAX_ERROR;
			goto out_close;
		}

		ret = ltfs_override_policy(opt->filterrules, true, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15015E, ret);
			ret = MKLTFS_OPERATIONAL_ERROR;
			goto out_close;
		}
	}

	/* Create partitions and write labels and indices to the tape */
	ltfsmsg(LTFS_INFO, 15010I, DATA_PART_ID, DATA_PART_NUM);
	ltfsmsg(LTFS_INFO, 15011I, INDEX_PART_ID, INDEX_PART_NUM);
	ret = ltfs_format_tape(vol, 0, opt->destructive);
	if (ret < 0) {
		if (ret == -LTFS_INTERRUPTED) {
			ltfsmsg(LTFS_ERR, 15045E);
			ret = MKLTFS_CANCELED_BY_USER;
		}else if (ret == -EDEV_WRITE_PROTECTED_WORM) {
			ltfsmsg(LTFS_ERR, 15061E);
			ret = MKLTFS_USAGE_SYNTAX_ERROR;
		}else {
			ltfsmsg(LTFS_ERR, 15012E);
			if (ret == -LTFS_WRITE_PROTECT || ret == -LTFS_WRITE_ERROR)
				ret = MKLTFS_USAGE_SYNTAX_ERROR;
			else
				ret = MKLTFS_OPERATIONAL_ERROR;
		}
		goto out_close;
	}
	ltfsmsg(LTFS_INFO, 15013I, ltfs_get_volume_uuid(vol));
	if (! opt->quiet)
		fprintf(stderr, "\n");

	/* Print volume capacity as GB (10^9 Bytes - SI) */
	memset(&cap, 0, sizeof(cap));
	ltfs_capacity_data(&cap, vol);
	ltfsmsg(LTFS_INFO, 15019I, (unsigned long long)(((cap.total_dp * (opt->blocksize / 1048576.0)
														* (1024 * 1024)) + 500000000)
													  / 1000 / 1000 / 1000));

	vol->t_attr = (struct tape_attr *) calloc(1, sizeof(struct tape_attr));
	if (! vol->t_attr) {
		ltfsmsg(LTFS_ERR, 10001E, "format_tape: vol->t_attr");
		goto out_close;
	}

	/* set Tape Attribute to vol->t_attr */
	set_tape_attribute(vol, vol->t_attr);

	ret = tape_format_attribute_to_cm(vol->device, vol->t_attr);
	if (ret < 0) {
		free(vol->t_attr);
		ltfsmsg(LTFS_ERR, 15058E, "format_tape");
	}

	ret = MKLTFS_NO_ERRORS;

	/* close the tape device and unload the backend */
	ltfsmsg(LTFS_DEBUG, 15020D);

out_close:
	ltfs_device_close(vol);
	ltfs_volume_free(&vol);
	ltfs_unset_signal_handlers();
	if (ret == MKLTFS_NO_ERRORS)
		ltfsmsg(LTFS_DEBUG, 15022D);
out_unload_backend:
	if (ret == MKLTFS_NO_ERRORS) {
		ret = plugin_unload(&backend);
		if (ret < 0)
			ltfsmsg(LTFS_WARN, 15021W);
		if (opt->kmi_backend_name) {
			ret = plugin_unload(&kmi);
			if (ret < 0)
				ltfsmsg(LTFS_WARN, 15051W);
		}
		ret = MKLTFS_NO_ERRORS;
	} else {
		plugin_unload(&backend);
		if (opt->kmi_backend_name)
			plugin_unload(&kmi);
	}

	if (ret == MKLTFS_NO_ERRORS)
		ltfsmsg(LTFS_INFO, 15024I);
	else
		ltfsmsg(LTFS_INFO, 15023I);

	return ret;
}

int unformat_tape(struct ltfs_volume *vol, struct other_format_opts *opt, void *args)
{
	int ret = MKLTFS_OPERATIONAL_ERROR;
	struct libltfs_plugin backend; /* tape driver backend */
	struct libltfs_plugin kmi; /* key manager interface backend */

	/* load the backend, open the tape device, and load a tape */
	ltfsmsg(LTFS_DEBUG, 15006D);
	ret = plugin_load(&backend, "tape", opt->backend_path, opt->config);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15008E, opt->backend_path);
		return MKLTFS_OPERATIONAL_ERROR;
	}
	if (opt->kmi_backend_name) {
		ret = plugin_load(&kmi, "kmi", opt->kmi_backend_name, opt->config);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15050E, opt->kmi_backend_name);
			return MKLTFS_OPERATIONAL_ERROR;
		}
	}
	ret = ltfs_device_open(opt->devname, backend.ops, vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15009E, opt->devname, ret);
		ret = MKLTFS_OPERATIONAL_ERROR;
		goto out_unload_backend;
	}
	if (opt->kmi_backend_name) {
		ret = kmi_init(&kmi, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15052E, opt->devname, ret);
			goto out_unload_backend;
		}

		ret = ltfs_parse_kmi_backend_opts(args, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15053E);
			goto out_unload_backend;
		}

		ret = tape_clear_key(vol->device, vol->kmi_handle);
		if (ret < 0)
			goto out_unload_backend;
	}
	{
		int i = 0;
		struct fuse_args *a = args;

		for (i = 0; i < a->argc && a->argv[i]; ++i) {
			if (!strcmp(a->argv[i], "-o")) {
				ltfsmsg(LTFS_ERR, 15055E, a->argv[i], a->argv[i + 1] ? a->argv[i + 1] : "");
				ret = MKLTFS_USAGE_SYNTAX_ERROR;
				goto out_unload_backend;
			}
		}
	}
	vol->append_only_mode = false;
	vol->set_pew = false;
	ret = ltfs_setup_device(vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 15044E);
		ret = MKLTFS_OPERATIONAL_ERROR;
		goto out_close;
	}
	ltfsmsg(LTFS_DEBUG, 15007D);

	/* Create 1 partition cartridge */
	ret = ltfs_unformat_tape(vol, opt->long_wipe, opt->destructive);
	if (ret < 0) {
		if (ret == -LTFS_INTERRUPTED) {
			ltfsmsg(LTFS_ERR, 15046E);
			ret = MKLTFS_CANCELED_BY_USER;
		}else if (ret == -EDEV_WRITE_PROTECTED_WORM) {
			ltfsmsg(LTFS_ERR, 15062E);
			ret = MKLTFS_USAGE_SYNTAX_ERROR;
		}else {
			ltfsmsg(LTFS_ERR, 15038E);
			ret = MKLTFS_OPERATIONAL_ERROR;
		}
		goto out_close;
	}

	ret = MKLTFS_UNFORMATTED;

	/* close the tape device and unload the backend */
	ltfsmsg(LTFS_DEBUG, 15020D);

out_close:
	ltfs_device_close(vol);
	ltfs_volume_free(&vol);
	ltfs_unset_signal_handlers();
	if (ret == MKLTFS_UNFORMATTED)
		ltfsmsg(LTFS_DEBUG, 15022D);
out_unload_backend:
	if (ret == MKLTFS_UNFORMATTED) {
		ret = plugin_unload(&backend);
		if (ret < 0)
			ltfsmsg(LTFS_WARN, 15021W);
		if (opt->kmi_backend_name) {
			ret = plugin_unload(&kmi);
			if (ret < 0)
				ltfsmsg(LTFS_WARN, 15051W);
		}
		ret = MKLTFS_UNFORMATTED;
	} else {
		plugin_unload(&backend);
		if (opt->kmi_backend_name)
			plugin_unload(&kmi);
	}

	if (ret == MKLTFS_UNFORMATTED)
		ltfsmsg(LTFS_INFO, 15040I);
	else
		ltfsmsg(LTFS_INFO, 15039I);

	return ret;
}

int _mkltfs_validate_options(char *prg_name, struct ltfs_volume *vol,
	struct other_format_opts *opt)
{
	int ret;
	char *tmp;

	ltfsmsg(LTFS_DEBUG, 15025D);

	if (!opt->devname) {
		ltfsmsg(LTFS_ERR, 15026E, "-d");
		return 1;
	}

	/* convert volume name to LTFS internal format, if specified */
	if (opt->volume_name) {
		ret = pathname_format(opt->volume_name, &tmp, true, false);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15031E);
			return 1;
		}
		free(opt->volume_name);
		opt->volume_name = tmp;
	}

	/* convert filter rules to UTF-8 NFC, fold the case, and parse them */
	if (opt->filterrules) {
		ret = pathname_format(opt->filterrules, &tmp, false, false);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 15034E, ret);
			return 1;
		}
		free(opt->filterrules);
		opt->filterrules = tmp;
	}

	ltfsmsg(LTFS_DEBUG, 15037D);
	return 0;
}
