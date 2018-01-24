/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2018 IBM Corp. All rights reserved.
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
** FILE NAME:       utils/ltfsck.c
**
** DESCRIPTION:     Utility to check/recovery/rollback an LTFS volume.
**
** AUTHOR:          Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
*************************************************************************************
*/

#ifdef mingw_PLATFORM
#include "libltfs/arch/win/win_util.h"
#else
#include <uuid/uuid.h>
#include <syslog.h>
#endif /* mingw_PLATFORM */

#include <getopt.h>

#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>

#include <sys/param.h>
#include "libltfs/ltfs_internal.h"
#include "libltfs/ltfs.h"
#include "ltfs_copyright.h"
#include "libltfs/plugin.h"
#include "libltfs/tape.h"
#include "libltfs/arch/time_internal.h"
#include "libltfs/kmi.h"

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

#ifdef __APPLE__
#include "libltfs/arch/osx/osx_string.h"
#endif

#ifdef mingw_PLATFORM
char *bin_ltfsck_dat;
#else
extern char bin_ltfsck_dat[];
#endif

/**< Operation mode */
enum {
	MODE_CHECK,
	MODE_VERIFY,
	MODE_ROLLBACK,
	MODE_LIST_POINT
};

/**< Search mode for index */
enum {
	SEARCH_NONE,
	SEARCH_BY_GEN,
};

struct other_check_opts {
	struct config_file *config; /**< Configurate data read from the global LTFS config file. */
	char *devname;              /**< Device to format */
	char *backend_path;         /**< Path to tape backend shared library */
	char *kmi_backend_name;     /**< Name or path to the key manager interface backend library */
	int  op_mode;               /**< Operation mode */
	int  search_mode;           /**< Search mode for index */
	char *str_gen;              /**< Rollback point specified by command line (generation)*/
	unsigned int point_gen;     /**< Rollback point (generation) */
	bool erase_history;         /**< overwrite existing data at rollback */
	bool recover_blocks;        /**< Recover unreferenced blocks at the ends of the partitions? */
	bool deep_recovery;         /**< Recover EOD missing cartridge? */
	int verbosity;              /**< Print extra messages? */
	char *prg_name;             /**< Program name */
	bool quiet;                 /**< Suppress information messages */
	bool trace;                 /**< Generate debug output */
	bool syslogtrace;           /**< Generate debug output to stderr and syslog*/
	bool fulltrace;             /**< Trace function calls */
	int  traverse_mode;         /**< Traverse strategy for listing index */
	bool full_index_info;       /**< Print full index infomation in list mode */
	bool capture_index;         /**< Capture index in list mode */
	bool salvage_points;		/**< List rollback points from no-EOD cartridge? */
};

struct index_info
{
	unsigned int generation;        /**< Generation number written to tape */
	tape_position position;         /**< tape position of the index including partition */
	struct ltfs_timespec mod_time;  /**< time of last modification */
	struct tape_offset selfptr;     /**< self-pointer (where this index was recovered from tape) */
	struct tape_offset backptr;     /**< back pointer (to prior generation on data partition) */
	char *commit_message;           /**< commit message */
	struct index_info *next;        /**< link to the next entry */
	int version;                    /**< version number of XML */
	char *creator;                  /**< creator string */
	char *volume_name;              /**< volume name */
	bool criteria_allow_update;     /**< criteria mutable? */
	const struct index_criteria *criteria; /**< index criteria */
};

struct partition_info
{
	tape_position eod;
	tape_position last_fm;
	struct index_info last_index;
};

struct rollback_info
{
	struct ltfs_index  *current;
	struct tape_offset current_pos;
	struct ltfs_index  *target;
	struct index_info  *target_info;
};

/* Forward declarations */
int ltfsck(struct ltfs_volume *vol, struct other_check_opts *opt, void *args);
int check_ltfs_volume(struct ltfs_volume *vol, struct other_check_opts *opt);
int load_tape(struct ltfs_volume *vol);
int rollback(struct ltfs_volume *vol, struct other_check_opts *opt);
int list_rollback_points(struct ltfs_volume *vol, struct other_check_opts *opt);
int _ltfsck_validate_options(struct other_check_opts *opt);
void print_criteria_info(struct ltfs_volume *vol);

/* Command line options */
static const char *short_options = "i:e:g:v:rnfzlmjkqtxhpoV";
static struct option long_options[] = {
	{"config",               1, 0, 'i'},
	{"backend",              1, 0, 'e'},
	{"generation",           1, 0, 'g'},
	{"traverse",             1, 0, 'v'},
	{"kmi-backend",          1, 0, '-'},
	{"capture-index",        0, 0, '+'},
	{"rollback"            , 0, 0, 'r'},
	{"no-rollback"         , 0, 0, 'n'},
	{"full-recovery"       , 0, 0, 'f'},
	{"deep-recovery"       , 0, 0, 'z'},
	{"list-rollback-points", 0, 0, 'l'},
	{"salvage-rollback-points", 0, 0, 0},
	{"full-index-info"     , 0, 0, 'm'},
	{"erase-history",        0, 0, 'j'},
	{"keep-history",         0, 0, 'k'},
	{"quiet",                0, 0, 'q'},
	{"trace",                0, 0, 't'},
	{"syslogtrace",          0, 0, '!'},
	{"fulltrace",            0, 0, 'x'},
	{"help",                 0, 0, 'h'},
	{"advanced-help",        0, 0, 'p'},
	{"version",				 0, 0, 'V' },
	{0, 0, 0, 0}
};

void show_usage(char *appname, struct config_file *config, bool full)
{
	ltfsresult(16400I, appname); /* Usage: %s [options] filesys */
	fprintf(stderr, "\n");
	ltfsresult(16401I); /* filesys   Device file for the tape drive */
	fprintf(stderr, "\n");
	ltfsresult(16402I); /* Available options are: */
	ltfsresult(16403I); /* -g, --generation */
	ltfsresult(16404I); /* -r, --rollback */
	ltfsresult(16405I); /* -n, --no-rollback */
	ltfsresult(16406I, LTFS_LOSTANDFOUND_DIR); /* -f, --full-recovery */
	ltfsresult(16421I); /* -z --deep-recovery */
	ltfsresult(16407I); /* -l, --list-rollback-points */
	ltfsresult(16422I); /* -m, --full-index-info */
	ltfsresult(16420I); /* -v, --traverse */
	ltfsresult(16408I); /* -j, --erase-history */
	ltfsresult(16409I); /* -k, --keep-history */
	ltfsresult(16410I); /* -q, --quiet */
	ltfsresult(16411I); /* -t, --trace */
	ltfsresult(16425I); /* --syslogtrace */
	ltfsresult(16426I); /* -V --version */
	ltfsresult(16412I); /* -h, --help */
	ltfsresult(16413I); /* -p, --advanced-help */
	if (full) {
		ltfsresult(16414I, LTFS_CONFIG_FILE); /* -i, --config=<file> */
		ltfsresult(16415I);                   /* -e, --backend=<name> */
		ltfsresult(16423I);                   /*     --kmi-backend=<name> */
		ltfsresult(16416I);                   /* -x, --fulltrace */
		ltfsresult(16424I);                   /*     --capture-index */
		ltfsresult(16427I);                   /*     --salvage-rollback-points */
		fprintf(stderr, "\n");
		plugin_usage("driver", config);
		fprintf(stderr, "\n");
		plugin_usage("kmi", config);
	}
	fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
	struct ltfs_volume *vol;
	struct other_check_opts opt;
	int ret, log_level, syslog_level, i, cmd_args_len;
	char *lang, *cmd_args;
	const char *config_file = NULL;
	void *message_handle;

	int fuse_argc = argc;
	char **fuse_argv = calloc(fuse_argc, sizeof(char *));
	if (! fuse_argv) {
		return LTFSCK_OPERATIONAL_ERROR;
	}
	for (i = 0; i < fuse_argc; ++i) {
		fuse_argv[i] = strdup(argv[i]);
		if (! fuse_argv[i]) {
			return LTFSCK_OPERATIONAL_ERROR;
		}
	}
	struct fuse_args args = FUSE_ARGS_INIT(fuse_argc, fuse_argv);

	/* Check for LANG variable and set it to en_US.UTF-8 if it is unset. */
	lang = getenv("LANG");
	if (! lang) {
		fprintf(stderr, "LTFS9015W Setting the locale to 'en_US.UTF-8'. If this is wrong, please set the LANG environment variable before starting ltfsck.\n");
		ret = setenv("LANG", "en_US.UTF-8", 1);
		if (ret) {
			fprintf(stderr, "LTFS9016E Cannot set the LANG environment variable\n");
			return LTFSCK_OPERATIONAL_ERROR;
		}
	}

	/* Start up libltfs with the default logging level. */
#ifndef mingw_PLATFORM
	openlog("ltfsck", LOG_PID, LOG_USER);
#endif
	ret = ltfs_init(LTFS_INFO, true, false);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10000E, ret);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	/*  Setup signal handler to terminate cleanly */
	ret = ltfs_set_signal_handlers();
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10013E);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	/* Register messages with libltfs */
	ret = ltfsprintf_load_plugin("bin_ltfsck", bin_ltfsck_dat, &message_handle);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10012E, ret);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	/* Set up default format options and load the config file. */
	memset(&opt, 0, sizeof(struct other_check_opts));
	opt.op_mode = MODE_CHECK;
	opt.search_mode = SEARCH_NONE;
	opt.erase_history = false;
	opt.traverse_mode = TRAVERSE_BACKWARD;
	opt.salvage_points = false;

	/* Check for a config file path given on the command line */
	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, short_options, long_options, &option_index);
		if (c == -1)
			break;
		if (c == 'i') {
			config_file = strdup(optarg);
			break;
		}
	}

	/* Load configuration file */
	ret = config_file_load(config_file, &opt.config);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10008E, ret);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	/* Parse all command line arguments */
	optind = 1;
	int num_of_o = 0;
	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, short_options,
			long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 0:
				if (!strcmp(long_options[option_index].name, "salvage-rollback-points")) {
					opt.op_mode = MODE_LIST_POINT;
					opt.salvage_points = true;
				}
				break;
			case 'i':
				break;
			case 'e':
				opt.backend_path = strdup(optarg);
				break;
			case 'g':
				if(opt.op_mode == MODE_CHECK)
					opt.op_mode = MODE_VERIFY;
				opt.search_mode = SEARCH_BY_GEN;
				opt.str_gen = strdup(optarg);
				break;
			case 'v':
				if ( strcmp(optarg, "forward") == 0)
					opt.traverse_mode = TRAVERSE_FORWARD;
				else if ( strcmp(optarg, "backward") == 0)
					opt.traverse_mode = TRAVERSE_BACKWARD;
				else
					opt.traverse_mode = TRAVERSE_UNKNOWN;
				break;
			case '-':
				opt.kmi_backend_name = strdup(optarg);
				break;
			case '+':
				opt.op_mode = MODE_LIST_POINT;
				opt.capture_index = true;
				break;
			case 'r':
				opt.op_mode = MODE_ROLLBACK;
				break;
			case 'n':
				opt.op_mode = MODE_VERIFY;
				break;
			case 'f':
				opt.recover_blocks = true;
				break;
			case 'z':
				opt.deep_recovery  = true;
				break;
			case 'l':
				opt.op_mode = MODE_LIST_POINT;
				break;
			case 'm':
				opt.full_index_info = true;
				break;
			case 'j':
				opt.erase_history = true;
				break;
			case 'k':
				opt.erase_history = false;
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
			case 'o':
				/* ignore -o here to parse them by fuse */
				++num_of_o;
				break;
			case 'V':
				ltfsresult(16108I, "ltfsck", PACKAGE_VERSION);
				ltfsresult(16108I, "LTFS Format Specification", LTFS_INDEX_VERSION_STR);
				return 0;
			case '?':
			default:
				show_usage(argv[0], opt.config, false);
				return LTFSCK_USAGE_SYNTAX_ERROR;
		}
	}

	/* Pick up default backend if one wasn't specified before */
	if (! opt.backend_path) {
		const char *default_backend = config_file_get_default_plugin("tape", opt.config);
		if (! default_backend) {
			ltfsmsg(LTFS_ERR, 10009E);
			return LTFSCK_OPERATIONAL_ERROR;
		}
		opt.backend_path = strdup(default_backend);
	}
	if (! opt.kmi_backend_name) {
		const char *default_backend = config_file_get_default_plugin("kmi", opt.config);
		if (default_backend)
			opt.kmi_backend_name = strdup(default_backend);
		else
			opt.kmi_backend_name = strdup("none");
	}
	if (opt.kmi_backend_name && strcmp(opt.kmi_backend_name, "none") == 0)
		opt.kmi_backend_name = NULL;

	/* Set the logging level */
	if (opt.quiet && (opt.trace || opt.fulltrace)) {
		ltfsmsg(LTFS_ERR, 9013E);
		show_usage(argv[0], opt.config, false);
		return LTFSCK_OPERATIONAL_ERROR;
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
	ltfs_set_syslog_level(log_level);

	/* Starting ltfsck */
	ltfsmsg(LTFS_INFO, 16000I, PACKAGE_NAME, PACKAGE_VERSION, log_level);

	/* Show command line arguments */
	for (i = 0, cmd_args_len = 0 ; i < argc; i++) {
		cmd_args_len += strlen(argv[i]) + 1;
	}
	cmd_args = calloc(1, cmd_args_len + 1);
	if (!cmd_args) {
		/* Memory allocation failed */
		ltfsmsg(LTFS_ERR, 10001E, "ltfsck (arguments)");
		return LTFSCK_OPERATIONAL_ERROR;
	}
	strcat(cmd_args, argv[0]);
	for (i = 1; i < argc; i++) {
		strcat(cmd_args, " ");
		strcat(cmd_args, argv[i]);
	}
	ltfsmsg(LTFS_INFO, 16088I, cmd_args);
	free(cmd_args);

	/* Show build time information */
	ltfsmsg(LTFS_INFO, 16089I, BUILD_SYS_FOR);
	ltfsmsg(LTFS_INFO, 16090I, BUILD_SYS_GCC);

	/* Show run time information */
	show_runtime_system_info();

	/* Actually mkltfs logic starts here */
	ret = ltfs_volume_alloc("ltfsck", &vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 16001E);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	if(argv[optind + num_of_o])
		opt.devname = strdup(argv[optind + num_of_o]);

	opt.prg_name = strdup(argv[0]);

	if (_ltfsck_validate_options(&opt)) {
		ltfsmsg(LTFS_ERR, 16002E);
		show_usage(argv[0], opt.config, false);
		return LTFSCK_USAGE_SYNTAX_ERROR;
	}

	ret = ltfs_fs_init();
	if (ret)
		return LTFSCK_OPERATIONAL_ERROR;

	ret = ltfsck(vol, &opt, &args);

	free(opt.prg_name);
	free(opt.backend_path);
	free(opt.kmi_backend_name);
	free(opt.devname);
	config_file_free(opt.config);
	ltfsprintf_unload_plugin(message_handle);
	ltfs_finish();
	return ret;
}

int ltfsck(struct ltfs_volume *vol, struct other_check_opts *opt, void *args)
{
	int ret, ret_close;
	struct libltfs_plugin backend; /* tape driver backend */
	struct libltfs_plugin kmi; /* key manager interface backend */

	/* load the backend, open the tape device, and load a tape */
	ret = plugin_load(&backend, "tape", opt->backend_path, opt->config);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 16010E, opt->backend_path, ret);
		return LTFSCK_OPERATIONAL_ERROR;
	}
	if (opt->kmi_backend_name) {
		ret = plugin_load(&kmi, "kmi", opt->kmi_backend_name, opt->config);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 16102E, opt->kmi_backend_name);
			return LTFSCK_OPERATIONAL_ERROR;
		}
	}

	ret = LTFSCK_OPERATIONAL_ERROR;
	if (ltfs_device_open(opt->devname, backend.ops, vol) < 0) {
		ltfsmsg(LTFS_ERR, 16011E, opt->devname);
		goto out_unload_backend;
	}
	ret = ltfs_parse_tape_backend_opts(args, vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 16106E);
		goto out_unload_backend;
	}
	if (opt->kmi_backend_name) {
		ret = kmi_init(&kmi, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 16104E, opt->devname, ret);
			goto out_unload_backend;
		}

		ret = ltfs_parse_kmi_backend_opts(args, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 16105E);
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
				ltfsmsg(LTFS_ERR, 16107E, a->argv[i], a->argv[i + 1] ? a->argv[i + 1] : "");
				ret = LTFSCK_USAGE_SYNTAX_ERROR;
				goto out_unload_backend;
			}
		}
	}
	vol->append_only_mode = false;
	vol->set_pew = false;

	ltfs_load_tape(vol);
	ret = ltfs_wait_device_ready(vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 16092E, opt->devname);
		goto out_close;
	}

	if (ltfs_setup_device(vol)) {
		ltfsmsg(LTFS_ERR, 16092E, opt->devname);
		goto out_close;
	}

	switch (opt->op_mode) {
	case MODE_CHECK:
		ltfsmsg(LTFS_INFO, 16014I, opt->devname);
		opt->full_index_info = false;
		ret = check_ltfs_volume(vol, opt);
		break;
	case MODE_ROLLBACK:
		ltfsmsg(LTFS_INFO, 16015I, opt->devname);
		opt->full_index_info = false;
		switch(opt->search_mode) {
		case SEARCH_BY_GEN:
			ret = rollback(vol, opt);
			break;
		default:
			ltfsmsg(LTFS_ERR, 16016E);
			ret = LTFSCK_USAGE_SYNTAX_ERROR;
			break;
		}
		break;
	case MODE_VERIFY:
		ltfsmsg(LTFS_INFO, 16017I, opt->devname);
		opt->full_index_info = false;
		switch(opt->search_mode) {
		case SEARCH_BY_GEN:
			ret = rollback(vol, opt);
			if (ret == LTFSCK_CORRECTED)
				ret = LTFSCK_NO_ERRORS;
			break;
		default:
			ltfsmsg(LTFS_ERR, 16016E);
			show_usage(opt->prg_name, opt->config, false);
			ret = LTFSCK_USAGE_SYNTAX_ERROR;
			break;
		}
		break;
	case MODE_LIST_POINT:
		ltfsmsg(LTFS_INFO, 16018I, opt->devname);
		ret = list_rollback_points(vol, opt);
		break;
	default:
		ltfsmsg(LTFS_ERR, 16019E);
		ret = LTFSCK_USAGE_SYNTAX_ERROR;
		break;
	}

	/* close the tape device and unload the backend */
out_close:
	ltfs_device_close(vol);
	ltfs_volume_free(&vol);
	ltfs_unset_signal_handlers();
out_unload_backend:
	ret_close = plugin_unload(&backend);
	if (ret == 0 && ret_close < 0) {
		ltfsmsg(LTFS_WARN, 16020W, ret_close);
		ret = LTFSCK_OPERATIONAL_ERROR;
	}
	if (opt->kmi_backend_name) {
		ret_close = plugin_unload(&kmi);
		if (ret == 0 && ret_close < 0) {
			ltfsmsg(LTFS_WARN, 16103W);
			ret = LTFSCK_OPERATIONAL_ERROR;
		}
	}

	return ret;
}

int check_ltfs_volume(struct ltfs_volume *vol, struct other_check_opts *opt)
{
	int ret;
	int vollock = VOLUME_UNLOCKED;

	/* Load tape and read labels */
	ret = load_tape(vol);
	if (ret != LTFSCK_NO_ERRORS) {
		ltfsmsg(LTFS_ERR, 16080E, ret);
		return LTFSCK_UNCORRECTED;
	}

	ret = tape_get_cart_volume_lock_status(vol->device, &vollock);
	if (vollock != VOLUME_UNLOCKED) {
		ltfsmsg(LTFS_INFO, 16111I, vollock);
	} else if (opt->deep_recovery) {
		/* Performe EOD recovery, if deep_recovery option is set */
		bool is_worm;
		ret = tape_get_worm_status(vol->device, &is_worm);
		if (ret < 0) {
			return LTFSCK_OPERATIONAL_ERROR;
		}

		if (is_worm && opt->deep_recovery) {
			ltfsmsg(LTFS_ERR, 16109E, "Deep Recovery");
			return LTFSCK_USAGE_SYNTAX_ERROR;
		}

		/* Performe EOD simple recovery first */
		ltfs_recover_eod_simple(vol);
		/*
		 * Performe normal EOD recover procedure
		 * if EOD was recoverd in simple recovery this function does nothing.
		 */
		ret = ltfs_recover_eod(vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 16091E, ret);
			return LTFSCK_UNCORRECTED;
		}
		vol->ignore_wrong_version = true;
	}

	ret = ltfs_mount(true, true, opt->recover_blocks, true, 0, vol);
	if (ret < 0) {
		if(ret == -LTFS_BOTH_EOD_MISSING && opt->deep_recovery) {
			/* CM corrupted? try -o force_mount_no_eod */
			ltfsmsg(LTFS_ERR, 16093E);
			ltfsmsg(LTFS_ERR, 16094E);
		} else if (ret == -LTFS_UNSUPPORTED_INDEX_VERSION) {
			ltfsmsg(LTFS_ERR, 16100E);
			ltfsmsg(LTFS_ERR, 16101E);
		} else /* Could not mount. Please format (mkltfs) or check (ltfsck). */
			ltfsmsg(LTFS_ERR, 16021E);
		return LTFSCK_UNCORRECTED;
	} else {
		print_criteria_info(vol);
		ltfs_unmount(SYNC_CHECK, vol);
		ltfsmsg(LTFS_INFO, 16022I);
		return LTFSCK_CORRECTED;
	}
}

struct index_info* _add_list(struct index_info *new, struct index_info *list)
{
	struct index_info *ret, *cur;

	if(!(list)) {
		ret = new;
	} else {
		cur = list;
		if (cur->generation > new->generation) {
			new->next = cur;
			ret = new;
		} else {
			while(cur->next != NULL) {
				if(cur->next->generation > new-> generation)
					break;
				cur = cur->next;
			}
			new->next = cur->next;
			cur->next = new;
			ret = list;
		}
	}

	return ret;
}

#if 0
void _store_index(struct index_info *dst, struct ltfs_index *src)
{
	dst->generation = src->generation;
	dst->mod_time   = src->mod_time;
	dst->selfptr    = src->selfptr;
	dst->backptr    = src->backptr;
	if(src->commit_message)
		dst->commit_message= strdup(src->commit_message);
	dst->next       = NULL;
}
#endif

void destroy_index_array(struct index_info *list)
{
	struct index_info *next, *cur;

	cur = list;

	while (cur) {
		next = cur->next;
		if (cur->commit_message)
			free(cur->commit_message);
		if (cur->creator)
			free(cur->creator);
		if (cur->volume_name)
			free(cur->volume_name);
		free(cur);
		cur = next;
	}

	return;
}

struct index_info * _make_new_index(struct ltfs_volume *vol)
{
	int ret;
	struct index_info *new;

	new = calloc(1, sizeof(struct index_info));
	if (!new) {
		ltfsmsg(LTFS_ERR, 10001E, __FUNCTION__);
		return NULL;
	}

	new->next = NULL;
	new->generation     = ltfs_get_index_generation(vol);
	new->mod_time       = ltfs_get_index_time(vol);
	new->selfptr        = ltfs_get_index_selfpointer(vol);
	new->backptr        = ltfs_get_index_backpointer(vol);
	new->criteria = ltfs_get_index_criteria(vol);
	new->criteria_allow_update = ltfs_get_criteria_allow_update(vol);

	new->version = ltfs_get_index_version(vol);
	ret = ltfs_get_index_creator(&new->creator, vol);
	if (ret < 0) {
		destroy_index_array(new);
		return NULL;
	}
	ret = ltfs_get_index_commit_message(&new->commit_message, vol);
	if (ret < 0) {
		destroy_index_array(new);
		return NULL;
	}
	ret = ltfs_get_volume_name(&new->volume_name, vol);
	if (ret < 0) {
		destroy_index_array(new);
		return NULL;
	}

	return new;
}

void _print_index_header(bool full_info)
{
#ifdef mingw_PLATFORM
	printf("Time zone: %s\n", get_local_timezone());
	printf("Generation: Date       Time                        SelfPtr->BackPtr (Part, Pos)\n");
	if (strcmp(get_local_timezone(), TIMEZONE_UTC) != 0) {
	printf("           (UTC Date   UTC Time)                                               \n");
	}
#else
	printf("Generation: Date       Time               Zone     SelfPtr->BackPtr (Part, Pos)\n");
	printf("           (UTC Date   UTC Time           UTC)                                 \n");
#endif
	if(full_info) {
	printf("            LTFS Format Version, Creator\n");
	printf("            Volume name\n");
	printf("            Placement Policy: [Overwrite] size_threshold pattern\n");
	}
	printf("            Commit Message                                                     \n");
	printf("-------------------------------------------------------------------------------\n");
}

void _print_index(struct ltfs_volume *vol, struct index_info *list, struct other_check_opts *opt)
{
	struct tm *t_st;
	int i;

	if(!opt)
		return;

	t_st = get_localtime(&list->mod_time.tv_sec);

	if (list->generation == (unsigned int)-1){
		printf("%s: %04d-%02d-%02d %02d:%02d:%02d.%09ld %s      (%d, %"PRIu64")->(\?\?, \?\?)\n",
			   " WRONG VER", 0, 0, 0,
			   0, 0, 0, (unsigned long) 0, "---",
			   ltfs_part_id2num(list->selfptr.partition, vol), list->selfptr.block);
	} else if(list->backptr.partition == 0 &&
		list->backptr.block == 0) {
		printf("%10d: %04d-%02d-%02d %02d:%02d:%02d.%09ld %s      (%d, %"PRIu64") <<Initial Index>>\n",
			   list->generation, t_st->tm_year+1900, t_st->tm_mon+1, t_st->tm_mday,
#ifdef mingw_PLATFORM
			   t_st->tm_hour, t_st->tm_min, t_st->tm_sec, list->mod_time.tv_nsec, "   ",
#else
			   t_st->tm_hour, t_st->tm_min, t_st->tm_sec, list->mod_time.tv_nsec, t_st->tm_zone,
#endif
			   ltfs_part_id2num(list->selfptr.partition, vol), list->selfptr.block);
	} else {
		printf("%10d: %04d-%02d-%02d %02d:%02d:%02d.%09ld %s      (%d, %"PRIu64")->(%d, %"PRIu64")\n",
			   list->generation, t_st->tm_year+1900, t_st->tm_mon+1, t_st->tm_mday,
#ifdef mingw_PLATFORM
			   t_st->tm_hour, t_st->tm_min, t_st->tm_sec, list->mod_time.tv_nsec, "   ",
#else
			   t_st->tm_hour, t_st->tm_min, t_st->tm_sec, list->mod_time.tv_nsec, t_st->tm_zone,
#endif
			   ltfs_part_id2num(list->selfptr.partition, vol), list->selfptr.block,
			   ltfs_part_id2num(list->backptr.partition, vol), list->backptr.block);
	}

	/* Print UTC time */
	if (list->generation == (unsigned int)-1) {
		printf("           (%04d-%02d-%02d %02d:%02d:%02d.%09ld %s)\n",
			   0, 0, 0,
			   0, 0, 0, (unsigned long) 0, "---");
#ifdef mingw_PLATFORM
	} else if(strcmp(get_local_timezone(), TIMEZONE_UTC) != 0) {
		t_st = get_gmtime(&list->mod_time.tv_sec);
		printf("           (%04d-%02d-%02d %02d:%02d:%02d.%09ld)\n",
			   t_st->tm_year+1900, t_st->tm_mon+1, t_st->tm_mday,
			   t_st->tm_hour, t_st->tm_min, t_st->tm_sec, list->mod_time.tv_nsec);
#else
	}else if(strcmp(t_st->tm_zone, "UTC") != 0) {
		const char *tz = getenv("TZ");
		setenv("TZ", "", 1);
		tzset();
		t_st = get_localtime(&list->mod_time.tv_sec);
		printf("           (%04d-%02d-%02d %02d:%02d:%02d.%09ld %s)\n",
			   t_st->tm_year+1900, t_st->tm_mon+1, t_st->tm_mday,
			   t_st->tm_hour, t_st->tm_min, t_st->tm_sec, list->mod_time.tv_nsec, t_st->tm_zone);
		if(tz)
			setenv("TZ", tz, 1);
		else
			unsetenv("TZ");
		tzset();
#endif
	}

	if (opt->full_index_info) {
		printf("            %d.%d.%d, \"%s\"\n",
			   list->version / 10000, (list->version / 100) % 100, list->version % 100,
			   list->creator);

		if(list->volume_name)
			printf("            %s\n", list->volume_name);
		else
			printf("            No Volume Name\n");

		if(list->criteria && list->criteria->have_criteria) {
			printf("            [%s] ", list->criteria_allow_update ? "  Allowed  " : "Not allowed");
			printf("%llu ", (long long unsigned int)list->criteria->max_filesize_criteria);

			if(list->criteria->glob_patterns) {
				i = 0;
				while(1) {
					if(list->criteria->glob_patterns[i].name == NULL)
						break;
					printf("%s ", list->criteria->glob_patterns[i].name);
					i++;
				}
			}
			printf("\n");
		} else
			printf("            No criteria\n");
	}

	if(list->commit_message)
		printf("           %s\n", list->commit_message);
	else
		printf("            No commit message\n");

	if (opt->capture_index)
		ltfs_save_index_to_disk(".", NULL, true, vol);

	return;
}

int print_a_index_noheader(struct ltfs_volume *vol, unsigned int target, void **list, void * priv)
{
	struct index_info *new;
	struct other_check_opts *opt = priv;

	CHECK_ARG_NULL(priv, LTFSCK_OPERATIONAL_ERROR);

	new = _make_new_index(vol);
	if (!new)
		return -ENOMEM;

	_print_index(vol, new, opt);

	free(new);

	return 0;
}

void print_index_array(struct ltfs_volume *vol, struct index_info *list, void *opt)
{
	struct index_info *cur;

	cur = list;
	_print_index_header(false);

	while (cur) {
		_print_index(vol, cur, opt);
		cur = cur-> next;
	}

	return;
}

void print_volume_info(struct ltfs_volume *vol)
{
	struct ltfs_timespec format_time;
	struct tm *t_st;

	ltfsmsg(LTFS_INFO, 16023I);
	ltfsmsg(LTFS_INFO, 16024I, ltfs_get_barcode(vol));
	ltfsmsg(LTFS_INFO, 16025I, ltfs_get_volume_uuid(vol));

	format_time = ltfs_get_format_time(vol);
	t_st = get_localtime(&(format_time.tv_sec));
	ltfsmsg(LTFS_INFO, 16026I,
			t_st->tm_year+1900, t_st->tm_mon+1, t_st->tm_mday,
			t_st->tm_hour, t_st->tm_min, t_st->tm_sec, format_time.tv_nsec,
			t_st->tm_zone);

	ltfsmsg(LTFS_INFO, 16027I, ltfs_get_blocksize(vol));
	ltfsmsg(LTFS_INFO, 16028I, ltfs_get_compression(vol) ? "Enabled" : "Disabled");
	ltfsmsg(LTFS_INFO, 16029I,
			   ltfs_ip_id(vol), ltfs_part_id2num(ltfs_ip_id(vol), vol));
	ltfsmsg(LTFS_INFO, 16030I,
			   ltfs_dp_id(vol), ltfs_part_id2num(ltfs_dp_id(vol), vol));
	if (ltfs_log_level >= LTFS_INFO)
		fprintf(stderr, "\n");

	return;
}

void print_criteria_info(struct ltfs_volume *vol)
{
	int i;
	bool update = ltfs_get_criteria_allow_update(vol);
	const struct index_criteria *ic = ltfs_get_index_criteria(vol);

	if(ic->have_criteria) {
		ltfsmsg(LTFS_INFO, 16031I);
		ltfsmsg(LTFS_INFO, 16032I,
			(unsigned long long)ic->max_filesize_criteria);

		if(ic->glob_patterns) {
			i = 0;
			while(1) {
				if(ic->glob_patterns[i].name == NULL)
					break;
				ltfsmsg(LTFS_INFO, 16033I, ic->glob_patterns[i].name);
				i++;
			}
		}

		ltfsmsg(LTFS_INFO, 16034I, update ? "Allowed" : "Not allowed");
		if (ltfs_log_level >= LTFS_INFO)
			fprintf(stderr, "\n");
	}

	return;
}

int search_index_by_gen(struct ltfs_volume *vol, unsigned int target, void **list, void *priv)
{
	struct index_info *new;

	if (vol->index->generation == (unsigned int)-1) {
		ltfsmsg(LTFS_ERR, 16098E);
		ltfsmsg(LTFS_ERR, 16099E);
		return -LTFS_UNSUPPORTED_INDEX_VERSION;
	}

	if(target == ltfs_get_index_generation(vol)) {
		new = _make_new_index(vol);
		if (!new)
			return -ENOMEM;

		*list = _add_list(new, *list);
		return 1; /* return 1 to stop searcing */
	}

	return 0;
}

int load_tape(struct ltfs_volume *vol)
{
	int ret;

	CHECK_ARG_NULL(vol, LTFSCK_USAGE_SYNTAX_ERROR);

	/* Load tape, read indexes */
	ret = ltfs_start_mount(false, vol);
	if (ret < 0) {
		/* ltfs_start_mount() already generated an appropriate error message */
		return LTFSCK_OPERATIONAL_ERROR;
	}

	/* Print label Information */
	print_volume_info(vol);

	return LTFSCK_NO_ERRORS;
}

int num_of_index(struct index_info *index)
{
	int num = 0;
	struct index_info *cur = index;

	while (cur) {
		++num;
		cur = cur->next;
	}

	return num;
}

int _erase_history(struct ltfs_volume *vol, struct other_check_opts *opt, struct tape_offset *position)
{
	int ret;
	struct tc_position pos;

	ltfsmsg(LTFS_DEBUG, 16045D, (int)position->partition, (unsigned long long)position->block);

	pos.partition = ltfs_part_id2num(position->partition, vol);
	pos.block = position->block;

	ret = tape_seek(vol->device, &pos);
	if (ret < 0)
		return LTFSCK_OPERATIONAL_ERROR;

	ltfsmsg(LTFS_DEBUG, 16050D);
	ret = tape_spacefm(vol->device, 1);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 16051E, ret);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	ltfsmsg(LTFS_DEBUG, 16052D);
	ret = tape_spacefm(vol->device, -1);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 16053E, ret);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	ret = tape_write_filemark(vol->device, 1, true, true, false);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 16054E, ret);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	return LTFSCK_NO_ERRORS;
}

int _rollback_ip(struct ltfs_volume *vol, struct other_check_opts *opt, struct tape_offset *position)
{
	int ret;

	if (position)
		ltfsmsg(LTFS_DEBUG, 16046D, "IP", (int)position->partition, (unsigned long long)position->block);

	if(opt->erase_history && position) {
		ret = _erase_history(vol, opt, position);
		if (ret != LTFSCK_NO_ERRORS)
			ltfsmsg(LTFS_ERR, 16059E, ret);
	} else {
		ret = ltfs_write_index(ltfs_ip_id(vol), SYNC_ROLLBACK, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 16060E, ret);
			ret = LTFSCK_OPERATIONAL_ERROR;
		}
	}

	return ret;
}

int _rollback_dp(struct ltfs_volume *vol, struct other_check_opts *opt, struct tape_offset *position)
{
	int ret;

	ltfsmsg(LTFS_DEBUG, 16046D, "DP", (int)position->partition, (unsigned long long)position->block);

	if(opt->erase_history && position) {
		ret = _erase_history(vol, opt, position);
		if (ret != LTFSCK_NO_ERRORS)
			ltfsmsg(LTFS_ERR, 16055E, ret);
	} else {
		ret = ltfs_write_index(ltfs_dp_id(vol), SYNC_ROLLBACK, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 16056E, ret);
			ret = LTFSCK_OPERATIONAL_ERROR;
		}
	}

	return ret;
}

int _rollback(struct ltfs_volume *vol, struct other_check_opts *opt, struct rollback_info *rb)
{
	int ret, index_num;

	index_num = num_of_index(rb->target_info);

	if (index_num == 1) {
		ltfsmsg(LTFS_INFO, 16067I);
		print_index_array(vol, rb->target_info, opt);

		if (opt->op_mode == MODE_ROLLBACK) {
			ret = ltfs_get_partition_readonly(ltfs_ip_id(vol), vol);
			if (! ret || ret == -LTFS_NO_SPACE || ret == -LTFS_LESS_SPACE)
				ret = ltfs_get_partition_readonly(ltfs_dp_id(vol), vol);
			if (ret < 0 && ret != -LTFS_NO_SPACE && ret != -LTFS_LESS_SPACE) { /* Try to roll back even in low space condition. */
				ltfsmsg(LTFS_ERR, 16057E);
				return LTFSCK_OPERATIONAL_ERROR;
			}

			/* Set target index and append position */
			vol->index = rb->target;
			if (! opt->erase_history)
				vol->index->generation = rb->current->generation;
			ltfs_set_index_dirty(true, false, vol->index);

			/* no need to call set_ip_append_position becuase WORM cart cannot be rollbacked */
			ret = tape_set_append_position(
				vol->device, ltfs_part_id2num(ltfs_ip_id(vol), vol), rb->current_pos.block - 1);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, 16079E, ret);
				return LTFSCK_OPERATIONAL_ERROR;
			}

			if (rb->target_info->selfptr.partition == ltfs_ip_id(vol)) {
				/* Recover from an index on index partition */
				ltfsmsg(LTFS_INFO, 16058I);
				ret = _rollback_dp(vol, opt, &(rb->target->backptr));
				if (ret != LTFSCK_NO_ERRORS)
					return ret;
				ret = _rollback_ip(vol, opt, &(rb->target->selfptr));
				if (ret != LTFSCK_NO_ERRORS)
					return ret;
			} else if (rb->target_info->selfptr.partition == ltfs_dp_id(vol)) {
				/* Recover from an index on data partition */
				ltfsmsg(LTFS_INFO, 16062I);
				ret = _rollback_dp(vol, opt, &rb->target->selfptr);
				if (ret != LTFSCK_NO_ERRORS)
					return ret;
				ret = _rollback_ip(vol, opt, NULL);
				if (ret != LTFSCK_NO_ERRORS)
					return ret;
			} else {
				ltfsmsg(LTFS_ERR, 16061E, rb->target->selfptr.partition);
				return LTFSCK_OPERATIONAL_ERROR;
			}
		}
	}  else {
		ltfsmsg(LTFS_ERR, 16068E, index_num);
		print_index_array(vol, rb->target_info, opt);
		return LTFSCK_OPERATIONAL_ERROR;
	}

	return LTFSCK_NO_ERRORS;
}

int rollback(struct ltfs_volume *vol, struct other_check_opts *opt)
{
	int ret;
	struct rollback_info  r;
	bool is_worm;

	memset(&r, 0, sizeof(struct rollback_info));

	/* Load tape and read labels */
	ret = load_tape(vol);
	if (ret !=  LTFSCK_NO_ERRORS) {
		ltfsmsg(LTFS_ERR, 16070E, ret);
		return ret;
	}

	ret = tape_get_worm_status(vol->device, &is_worm);
	if (ret < 0) {
		return LTFSCK_OPERATIONAL_ERROR;
	}

	if (is_worm && opt->op_mode == MODE_ROLLBACK) {
		ltfsmsg(LTFS_ERR, 16109E, "Rollback");
		return LTFSCK_USAGE_SYNTAX_ERROR;
	}

	/* Attempt to mount a medium to confirm the consistency */
	ret = ltfs_mount(false, false, false, false, 0, vol);
	if (ret < 0) {
		if(ret == -LTFS_BOTH_EOD_MISSING)
			ltfsmsg(LTFS_ERR, 16097E);
		else
			ltfsmsg(LTFS_ERR, 16087E);
		return LTFSCK_UNCORRECTED;
	}
	else {
		r.current = vol->index;
		r.current_pos = ltfs_get_index_selfpointer(vol);
		ltfsmsg(LTFS_DEBUG, 16081D, ltfs_get_index_generation(vol),
				(int)r.current_pos.partition, (unsigned long long)r.current_pos.block);
		ltfs_unmount(SYNC_ROLLBACK, vol);
		vol->index = NULL;
	}

	/* Cartridge is concistent and genaration is latest. No operation is needed */
	if (opt->point_gen == r.current->generation) {
		ltfsmsg(LTFS_INFO, 16063I);
		return LTFSCK_NO_ERRORS;
	}

	/* Find target index */
	ret = ltfs_traverse_index_backward(vol, ltfs_ip_id(vol), opt->point_gen,
									   search_index_by_gen, (void *)(&(r.target_info)), (void *)opt);
	if (ret == -LTFS_NO_INDEX) {
		if (opt->erase_history) {
			ret = ltfs_traverse_index_forward(vol, ltfs_dp_id(vol), opt->point_gen,
											  search_index_by_gen, (void *)(&(r.target_info)), (void *)opt);
			if (ret == -LTFS_NO_INDEX) {
				ltfsmsg(LTFS_ERR, 16072E, ret);
				return LTFSCK_OPERATIONAL_ERROR;
			} else if (ret < 0) {
				ltfsmsg(LTFS_ERR, 16072E, ret);
				return LTFSCK_OPERATIONAL_ERROR;;
			}
		} else {
			ret = ltfs_traverse_index_backward(vol, ltfs_dp_id(vol), opt->point_gen,
											   search_index_by_gen, (void *)(&(r.target_info)), (void *)opt);
			if (ret !=  LTFSCK_NO_ERRORS) {
				ltfsmsg(LTFS_ERR, 16072E, ret);
				return LTFSCK_OPERATIONAL_ERROR;
			}
		}
	} else if ( ret < 0) {
		ltfsmsg(LTFS_ERR, 16071E, ret);
		return ret;
	}

	/* Copy current index to data partition */
	r.target = vol->index;
	if (opt->op_mode == MODE_ROLLBACK && !opt->erase_history) {
		struct tape_offset selfptr = ltfs_get_index_selfpointer(vol);
		ltfsmsg(LTFS_INFO, 16082I);
		ret = ltfs_get_partition_readonly(ltfs_ip_id(vol), vol);
		if (! ret || ret == -LTFS_NO_SPACE || ret == -LTFS_LESS_SPACE)
			ret = ltfs_get_partition_readonly(ltfs_dp_id(vol), vol);
		if (ret < 0 && ret != -LTFS_NO_SPACE && ret != -LTFS_LESS_SPACE) { /* Try to roll back even in low space condition. */
			ltfsmsg(LTFS_ERR, 16057E);
			return LTFSCK_OPERATIONAL_ERROR;
		}
		vol->index = r.current;
		ltfs_set_index_dirty(true, false, vol->index);
		ret = _rollback_dp(vol, opt, &selfptr);
	}

	/* Roll back */
	if (r.target_info)
		ret = _rollback(vol, opt, &r);
	else {
		ltfsmsg(LTFS_ERR, 16073E);
		ret = LTFSCK_OPERATIONAL_ERROR;
		goto out_destroy;
	}

	/* Check consistency after roll back and recovery if nessesary */
	if(ret == 0) {
		ret = ltfs_mount(true, true, false, false, 0, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 16021E);
			ret = LTFSCK_UNCORRECTED;
			goto out_destroy;
		}
		ltfsmsg(LTFS_INFO, 16086I);
		ret = LTFSCK_CORRECTED;
	}

out_destroy:
	destroy_index_array(r.target_info);

	return ret;
}

int list_rollback_points_normal(struct ltfs_volume *vol, struct other_check_opts *opt)
{
	int ret = LTFSCK_NO_ERRORS;

	/* Load tape and read labels */
	ret = load_tape(vol);
	if (ret !=  LTFSCK_NO_ERRORS) {
		ltfsmsg(LTFS_ERR, 16074E, ret);
		return ret;
	}

	/* Attempt to mount a medium to confirm the consistency */
	ret = ltfs_mount(false, false, false, false, 0, vol);
	if (ret < 0) {
		if(ret == -LTFS_BOTH_EOD_MISSING)
			ltfsmsg(LTFS_WARN, 16096W);
		else {
			ltfsmsg(LTFS_ERR, 16087E);
			return LTFSCK_UNCORRECTED;
		}
	}

	_print_index_header(opt->full_index_info);

	/* read index from the index partition */
	if(opt->traverse_mode == TRAVERSE_FORWARD)
		ret = ltfs_traverse_index_forward(vol, ltfs_ip_id(vol), opt->point_gen,
										  print_a_index_noheader, NULL, (void *)opt);
	else
		ret = ltfs_traverse_index_backward(vol, ltfs_ip_id(vol), opt->point_gen,
										   print_a_index_noheader, NULL, (void *)opt);
	if (ret !=  LTFSCK_NO_ERRORS) {
		ltfsmsg(LTFS_ERR, 16075E, ret);
		return ret;
	}

	/* read index from the data partition */
	if(opt->traverse_mode == TRAVERSE_FORWARD)
		ret = ltfs_traverse_index_forward(vol, ltfs_dp_id(vol), opt->point_gen,
										  print_a_index_noheader, NULL, (void *)opt);
	else
		ret = ltfs_traverse_index_backward(vol, ltfs_dp_id(vol), opt->point_gen,
										   print_a_index_noheader, NULL, (void *)opt);
	if (ret !=  LTFSCK_NO_ERRORS) {
		ltfsmsg(LTFS_ERR, 16076E, ret);
	}

	return ret;
}

int list_rollback_points_no_eod(struct ltfs_volume *vol, struct other_check_opts *opt)
{
	int ret = LTFSCK_NO_ERRORS;
	bool is_worm;

	/* Load tape and read labels */
	ret = load_tape(vol);
	if (ret !=  LTFSCK_NO_ERRORS) {
		ltfsmsg(LTFS_ERR, 16074E, ret);
		return ret;
	}

	ret = tape_get_worm_status(vol->device, &is_worm);
	if (ret < 0 || !is_worm) {
		ltfsmsg(LTFS_ERR, 16109E, "Salvage Rollback Points");
		return LTFSCK_USAGE_SYNTAX_ERROR;
	}

	/* Check EOD status to reject normal tape. */
	ret = ltfs_check_eod_status(vol);
	if (ret == 0) {
		ltfsmsg(LTFS_ERR, 16110E);
		return LTFSCK_USAGE_SYNTAX_ERROR;
	}

	/* Read index from the data partition */
	/* We don't need to read IP because index in DP is always newer (or same) than IP in case of WORM */
	ret = ltfs_traverse_index_no_eod(vol, ltfs_dp_id(vol), opt->point_gen,
									  print_a_index_noheader, NULL, (void *)opt);
	if (ret !=  LTFSCK_NO_ERRORS) {
		ltfsmsg(LTFS_ERR, 16076E, ret);
	}

	return ret;
}

int list_rollback_points(struct ltfs_volume *vol, struct other_check_opts *opt)
{
	int ret;

	if (opt->salvage_points) {
		ret = list_rollback_points_no_eod(vol, opt);
	}
	else {
		ret = list_rollback_points_normal(vol, opt);
	}

	return ret;
}

int _ltfsck_validate_options(struct other_check_opts *opt)
{
	if(opt->op_mode == MODE_VERIFY || opt->op_mode == MODE_ROLLBACK) {
		if (!opt->str_gen) {
			ltfsmsg(LTFS_ERR, 16003E);
			return LTFSCK_USAGE_SYNTAX_ERROR;
		}

		if (opt->search_mode == SEARCH_BY_GEN) {
			if (!opt->str_gen) {
				ltfsmsg(LTFS_ERR, 16004E);
				return LTFSCK_USAGE_SYNTAX_ERROR;
			} else {
				char *invalid_start;
				errno = 0;
				opt->point_gen = strtoul(opt->str_gen, &invalid_start, 0);
				if( (*invalid_start == '\0') && opt->str_gen )
					ltfsmsg(LTFS_INFO, 16006I, opt->point_gen);
				else {
					ltfsmsg(LTFS_ERR, 16005E, opt->str_gen);
					return LTFSCK_USAGE_SYNTAX_ERROR;
				}
			}
		}
	}

	if (opt->traverse_mode != TRAVERSE_FORWARD && opt->traverse_mode != TRAVERSE_BACKWARD) {
		ltfsmsg(LTFS_ERR, 16085E);
		return LTFSCK_USAGE_SYNTAX_ERROR;
	}

	if(opt->op_mode == MODE_LIST_POINT) {
		if ( opt->traverse_mode == TRAVERSE_FORWARD )
			ltfsmsg(LTFS_INFO, 16083I);
		else if (opt->traverse_mode == TRAVERSE_BACKWARD)
			ltfsmsg(LTFS_INFO, 16084I);

		if (opt->capture_index && opt->search_mode == SEARCH_BY_GEN) {
			if (!opt->str_gen) {
				ltfsmsg(LTFS_ERR, 16004E);
				return LTFSCK_USAGE_SYNTAX_ERROR;
			} else {
				char *invalid_start;
				errno = 0;
				opt->point_gen = strtoul(opt->str_gen, &invalid_start, 0);
				if( (*invalid_start == '\0') && opt->str_gen )
					ltfsmsg(LTFS_INFO, 16006I, opt->point_gen);
				else {
					ltfsmsg(LTFS_ERR, 16005E, opt->str_gen);
					return LTFSCK_USAGE_SYNTAX_ERROR;
				}
				opt->op_mode = MODE_VERIFY;
			}
		}

	}

	if (!opt->devname) {
		ltfsmsg(LTFS_ERR, 16009E);
		return LTFSCK_USAGE_SYNTAX_ERROR;
	}

	return 0;
}
