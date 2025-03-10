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
** FILE NAME:       utils/ltfsindextool.c
**
** DESCRIPTION:     The low level index tool
**
** AUTHORS:         Atsushi Abe
**                  piste.2750@gmail.com
**
*************************************************************************************
*/

#ifdef mingw_PLATFORM
#include "arch/win/win_util.h"
#include <fusefw/fusefw.h>
#include <ltfscommon/getopt.h>
#else
#include <syslog.h>
#include <fuse.h>
#include <getopt.h>
#endif /* mingw_PLATFORM */


#include "libltfs/ltfs_fuse_version.h"


#include "libltfs/ltfs.h"
#include "libltfs/xml_libltfs.h"
#include "ltfs_copyright.h"
#include "libltfs/plugin.h"
#include "libltfs/kmi.h"
#include "libltfs/tape.h"

static volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

#ifdef __APPLE__
#include "libltfs/arch/osx/osx_string.h"
#endif

#ifdef mingw_PLATFORM
char *bin_ltfsindextool_dat;
#else
extern char bin_ltfsindextool_dat[];
#endif

typedef enum {
	OP_CHECK,
	OP_CAPTURE,
} operation_mode_t;

struct indextool_opts {
	operation_mode_t mode;      /**< Operation mode */
	char *filename;             /**< Index filename to check in check mode */
	char *devname;              /**< Device name to capture index in capture mode */
	int  partition;             /**< partition to operate */
	uint64_t start_pos;         /**< start position */
	char *out_dir;              /**< Output dir for captured indexes */
	unsigned long blocksize;    /**< Nominal tape block size */
	struct config_file *config; /**< Configuration data read from the global LTFS config file */
	char *backend_path;         /**< Path to tape backend shared library */
	char *kmi_backend_name;     /**< Name or path to the key manager interface backend library */
	bool quiet;                 /**< Quiet mode indicator */
	bool trace;                 /**< Debug mode indicator */
	bool syslogtrace;           /**< Generate debug output to stderr and syslog*/
};

/* Command line options */
#define PART_BOTH  (-1)
#define START_POS  (5)
#define OUTPUT_DIR "."
#define KEY_MAX_OFFSET  (0x30)

static const char *short_options = "i:e:d:p:s:o:b:qthV";
static struct option long_options[] = {
	{"config",          1, 0, 'i'},
	{"backend",         1, 0, 'e'},
	{"device",          1, 0, 'd'},
	{"partition",       1, 0, 'p'},
	{"start-pos",       1, 0, 's'},
	{"output-dir",      1, 0, '^'},
	{"blocksize",       1, 0, 'b'},
	{"kmi-backend",     1, 0, '-'},
	{"quiet",           0, 0, 'q'},
	{"trace",           0, 0, 't'},
	{"syslogtrace",     0, 0, '!'},
	{"help",            0, 0, 'h'},
	{"version",			0, 0, 'V'},
	{0, 0, 0, 0}
};

/* Private functions */
static inline int _open_output_file(tape_partition_t   part,
									tape_block_t       start_pos,
									char               *base_path)
{
	int ret;
	char *fname = NULL;

	ret = asprintf(&fname, "%s/ltfs-index-%u-%llu.xml", base_path,
				   (unsigned int) part, (unsigned long long)start_pos);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19532E);
		return -1;
	}

	ltfsmsg(LTFS_INFO, 19547I, fname);
	SAFE_OPEN(&ret,fname, O_WRONLY | O_CREAT | O_TRUNC,SHARE_FLAG_DENYRW, PERMISSION_READWRITE);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19533E, fname, errno);
	}

	free(fname);

	return ret;
}

static inline void _close_output_file(int fd)
{
	fsync(fd);
	SAFE_CLOSE(fd);
}

/** Capture indexes on the partition.
 *
 */
static int ltfs_capture_index_raw(tape_partition_t   part,
								  tape_block_t       start_pos,
								  int                blocksize,
								  char               *base_path,
								  struct ltfs_volume *vol)
{
	int ret = 0, fd = -1;
	ssize_t nread, nwrite, index_len = 0;
	struct tc_position pos;
	char *buf = NULL, *key = NULL, check_buf[KEY_MAX_OFFSET + 1];

	pos.partition = part;
	pos.block = start_pos;

	buf = malloc(blocksize);
	if (!buf) {
		ltfsmsg(LTFS_ERR, 19516E);
		return -LTFS_NO_MEMORY;
	}

	ret = tape_seek(vol->device, &pos);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19517E, (unsigned int)part, (unsigned long long)start_pos, ret);
		return ret;
	}

	while (ret == 0) {
		index_len = 0;

		ret = tape_get_position(vol->device, &pos);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 19518E, ret);
			break;
		}

		nread = tape_read(vol->device, buf, blocksize, true, vol->kmi_handle);
		if (nread < 0) {
			ltfsmsg(LTFS_ERR, 19519E,
					(unsigned int)pos.partition, (unsigned long long)pos.block, nread);
			ret = nread;
			break;
		}
		pos.block++;

		memset(check_buf, 0x00, KEY_MAX_OFFSET + 1);
		SAFE_STRNCPY(check_buf, buf, KEY_MAX_OFFSET);
		key = strstr(check_buf, "<ltfsindex");
		if (key && (key - buf < 0x30)) {
			ltfsmsg(LTFS_ERR, 19529I,
					(unsigned int)pos.partition, (unsigned long long)(pos.block - 1));

			fd = _open_output_file(pos.partition, pos.block - 1, base_path);
			if (fd == -1) {
				/* Open failure*/
				return -LTFS_CACHE_IO;
			}

			nwrite = SAFE_WRITE(fd, buf, nread);
			if (nwrite == nread) {
				index_len += nread;
			} else {
				ltfsmsg(LTFS_ERR, 19536E, nwrite, errno);
				_close_output_file(fd);
				return -LTFS_CACHE_IO;
			}

			while (true) {
				nread = tape_read(vol->device, buf, blocksize, true, vol->kmi_handle);
				if (nread == -EDEV_EOD_DETECTED) {
					ret = nread;
					ltfsmsg(LTFS_ERR, 19538E,
							(unsigned int)pos.partition,
							(unsigned long long)(pos.block));
					ltfsmsg(LTFS_INFO, 19539I, (unsigned long long)index_len);
					break;
				}

				if (nread > 0) {
					/* Write a block to the file */
					nwrite = SAFE_WRITE(fd, buf, nread);
					if (nwrite == nread) {
						index_len += nread;
					} else {
						ltfsmsg(LTFS_ERR, 19536E, nwrite, errno);
						_close_output_file(fd);
						return -LTFS_CACHE_IO;
					}
					pos.block++;
				} else if (!nread) {
					/* Detect a FM (the end of the index), do nothing */
					ltfsmsg(LTFS_INFO, 19537I,
							(unsigned int)pos.partition,
							(unsigned long long)(pos.block));
					ltfsmsg(LTFS_INFO, 19539I, (unsigned long long)index_len);
					break;
				} else {
					ltfsmsg(LTFS_ERR, 19519E,
							(unsigned int)pos.partition, (unsigned long long)pos.block, nread);
					ret = nread;
					break;
				}
			}

			_close_output_file(fd);
		} else {
			/* seek to next FM */
			if (key)
				ltfsmsg(LTFS_INFO, 19530I,
						(unsigned int)pos.partition,
						(unsigned long long)(pos.block - 1),
						(int)(key - buf));
			else
				ltfsmsg(LTFS_INFO, 19530I,
						(unsigned int)pos.partition,
						(unsigned long long)(pos.block - 1),
						(int)0);

			/* Do nothig at (nread == 0) because tape hits a FM */
			if (nread > 0) {
				ret = tape_spacefm(vol->device, 1);
				if (ret < 0) {
					ltfsmsg(LTFS_ERR, 19531E, (unsigned int)part, (unsigned long long)start_pos, ret);
					break;
				}
			}
		}
	}

	if (ret == -EDEV_EOD_DETECTED) {
		ret = tape_get_position(vol->device, &pos);
		if (!ret) {
			ltfsmsg(LTFS_INFO, 19534I,(unsigned int)pos.partition, (unsigned long long)pos.block);
		} else {
			ltfsmsg(LTFS_INFO, 19535I, ret);
		}
		ret = 0;
	}

	if (buf)
		free(buf);

	return ret;
}

static int _capture(struct indextool_opts *opt, struct ltfs_volume *vol)
{
	int r, ret = 0;
	tape_partition_t p;

	if (opt->partition == PART_BOTH) {
		ltfsmsg(LTFS_INFO, 19504I);
		for (p = 0; p < 2; p++) {
			r = ltfs_capture_index_raw(p, 5, opt->blocksize, opt->out_dir, vol);
			if (!ret)
				ret = r;
		}
	} else {
		ltfsmsg(LTFS_INFO, 19505I, (unsigned int)opt->partition, (unsigned long long)opt->start_pos);
		ret = ltfs_capture_index_raw(opt->partition, opt->start_pos, opt->blocksize, opt->out_dir, vol);
	}

	return ret;
}

static int _indextool_validate_options(char *prg_name, struct indextool_opts *opt)
{
	ltfsmsg(LTFS_DEBUG, 19525D);

	/* Validate filename and devname and decide a operation mode*/
	if (opt->filename) {
		opt->mode = OP_CHECK;
	} else if (opt->devname) {
		opt->mode = OP_CAPTURE;
	} else {
		ltfsmsg(LTFS_ERR, 19526E);
		return 1;
	}

	/* Validate partition */
	if (opt->partition != PART_BOTH && opt->partition != 0 && opt->partition != 1) {
		ltfsmsg(LTFS_ERR, 19540E);
		return 1;
	}

	/* Validate start position */
	if (opt->start_pos < START_POS) {
		ltfsmsg(LTFS_ERR, 19548E, (unsigned long long)opt->start_pos);
		return 1;
	}

	ltfsmsg(LTFS_DEBUG, 19527D);
	return 0;
}

static int check_index(struct ltfs_volume *vol, struct indextool_opts *opt, void *args)
{
	int ret = 0;

	ltfsmsg(LTFS_INFO, 19543I, opt->filename);

	vol->label->blocksize = opt->blocksize;
	ret = xml_schema_from_file(opt->filename, vol->index, vol);

	if (!ret) {
		ltfsmsg(LTFS_INFO, 19544I);
	} else {
		ltfsmsg(LTFS_ERR, 19545E, ret);
	}

	return ret;
}

static int capture_index(struct ltfs_volume *vol, struct indextool_opts *opt, void *args)
{
	int ret = INDEXTOOL_OPERATIONAL_ERROR;
	struct libltfs_plugin backend; /* tape driver backend */
	struct libltfs_plugin kmi; /* key manager interface backend */

	/* load the backend, open the tape device, and load a tape */
	ltfsmsg(LTFS_DEBUG, 19506D);
	ret = plugin_load(&backend, "tape", opt->backend_path, opt->config);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19508E, opt->backend_path);
		return INDEXTOOL_OPERATIONAL_ERROR;
	}
	if (opt->kmi_backend_name) {
		ret = plugin_load(&kmi, "kmi", opt->kmi_backend_name, opt->config);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 19509E, opt->kmi_backend_name);
			return INDEXTOOL_OPERATIONAL_ERROR;
		}
	}
	ret = ltfs_device_open(opt->devname, backend.ops, vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19510E, opt->devname, ret);
		ret = INDEXTOOL_OPERATIONAL_ERROR;
		goto out_unload_backend;
	}
	ret = ltfs_parse_tape_backend_opts(args, vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19513E);
		goto out_unload_backend;
	}
	if (opt->kmi_backend_name) {
		ret = kmi_init(&kmi, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 19511E, opt->devname, ret);
			goto out_unload_backend;
		}

		ret = ltfs_parse_kmi_backend_opts(args, vol);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, 19512E);
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
				ltfsmsg(LTFS_ERR, 19514E, a->argv[i], a->argv[i + 1] ? a->argv[i + 1] : "");
				ret = INDEXTOOL_USAGE_SYNTAX_ERROR;
				goto out_unload_backend;
			}
		}
	}

	ltfs_load_tape(vol);
	ret = ltfs_wait_device_ready(vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19515E);
		ret = INDEXTOOL_OPERATIONAL_ERROR;
		goto out_close;
	}

	vol->append_only_mode = false;
	vol->set_pew = false;
	ret = ltfs_setup_device(vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19515E);
		ret = INDEXTOOL_OPERATIONAL_ERROR;
		goto out_close;
	}
	ltfsmsg(LTFS_DEBUG, 19507D);

	/* Capture_index */
	ret = _capture(opt, vol);

	/* close the tape device and unload the backend */
	ltfsmsg(LTFS_DEBUG, 19520D);

out_close:
	ltfs_device_close(vol);
	ltfs_volume_free(&vol);
	ltfs_unset_signal_handlers();
	if (ret == INDEXTOOL_NO_ERRORS)
		ltfsmsg(LTFS_DEBUG, 19522D);
out_unload_backend:
	if (ret == INDEXTOOL_NO_ERRORS) {
		ret = plugin_unload(&backend);
		if (ret < 0)
			ltfsmsg(LTFS_WARN, 19521W);
		if (opt->kmi_backend_name) {
			ret = plugin_unload(&kmi);
			if (ret < 0)
				ltfsmsg(LTFS_WARN, 19528W);
		}
		ret = INDEXTOOL_NO_ERRORS;
	} else {
		plugin_unload(&backend);
		if (opt->kmi_backend_name)
			plugin_unload(&kmi);
	}

	if (ret == INDEXTOOL_NO_ERRORS)
		ltfsmsg(LTFS_INFO, 19524I);
	else
		ltfsmsg(LTFS_INFO, 19523I, ret);

	return ret;
}

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
	ltfsresult(19900I, appname);  /* Usage: %s <options> */
	fprintf(stderr, "\n");
	ltfsresult(19901I);           /* Available options are: */
	ltfsresult(19902I);                                                /* -d, --device=<name> */
	ltfsresult(19903I);                                                /* -p, --partition=<0|1> */
	ltfsresult(19904I, START_POS);                                     /* -s, --start-pos */
	ltfsresult(19905I, OUTPUT_DIR);                                    /* -output-dir */
	ltfsresult(19906I, LTFS_DEFAULT_BLOCKSIZE);                        /* -b, --blocksize */
	ltfsresult(19907I, LTFS_CONFIG_FILE);                              /* -i, --config=<file> */
	ltfsresult(19908I, default_backend);                               /* -e, --backend */
	ltfsresult(19909I, config_file_get_default_plugin("kmi", config)); /* --kmi-backend */
	ltfsresult(19910I);           /* -q, --quiet */
	ltfsresult(19911I);           /* -t, --trace */
	ltfsresult(19912I);           /* -V, --version */
	ltfsresult(19913I);           /* -h, --help */
	fprintf(stderr, "\n");
	plugin_usage(appname, "driver", config);
	fprintf(stderr, "\n");
	plugin_usage(appname, "kmi", config);
	fprintf(stderr, "\n");
	ltfsresult(19914I); /* Usage example: */
	ltfsresult(19915I, appname, devname, 0);
	free(devname);
}

/* Main routine */
int main(int argc, char **argv)
{
	struct ltfs_volume *vol;
	struct indextool_opts opt;
	int ret, log_level, syslog_level, i, cmd_args_len;
	char *lang, *cmd_args;
	const char *config_file = NULL;
	void *message_handle;

	int fuse_argc = argc;
	char **fuse_argv = calloc(fuse_argc, sizeof(char *));
	if (! fuse_argv) {
		return INDEXTOOL_OPERATIONAL_ERROR;
	}
	for (i = 0; i < fuse_argc; ++i) {
		fuse_argv[i] = SAFE_STRDUP(argv[i]);
		if (! fuse_argv[i]) {
			return INDEXTOOL_OPERATIONAL_ERROR;
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
			return INDEXTOOL_OPERATIONAL_ERROR;
		}
	}

	/* Start up libltfs with the default logging level. */
#ifndef mingw_PLATFORM
	openlog("ltfsindextool", LOG_PID, LOG_USER);
#endif
	ret = ltfs_init(LTFS_INFO, true, false);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10000E, ret);
		return INDEXTOOL_OPERATIONAL_ERROR;
	}

	/*  Setup signal handler to terminate cleanly */
	ret = ltfs_set_signal_handlers();
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10013E);
		return INDEXTOOL_OPERATIONAL_ERROR;
	}

	/* Register messages with libltfs */
	ret = ltfsprintf_load_plugin("bin_ltfsindextool", bin_ltfsindextool_dat, &message_handle);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 10012E, ret);
		return INDEXTOOL_OPERATIONAL_ERROR;
	}

	/* Set up empty options and load the configuration file. */
	memset(&opt, 0, sizeof(struct indextool_opts));
	opt.blocksize = LTFS_DEFAULT_BLOCKSIZE;
	opt.partition = PART_BOTH;
	opt.start_pos = START_POS;
	opt.out_dir = OUTPUT_DIR;

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
		return INDEXTOOL_OPERATIONAL_ERROR;
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
			case 'p':
				opt.partition = atoi(optarg);
				break;
			case 's':
				opt.start_pos = strtoull(optarg, NULL, 0);
				break;
			case '^':
				opt.out_dir = SAFE_STRDUP(optarg);
				break;
			case 'b':
				opt.blocksize = atoi(optarg);
				break;
			case '-':
				opt.kmi_backend_name = SAFE_STRDUP(optarg);
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
			case 'h':
				show_usage(argv[0], opt.config, false);
				return 0;
			case 'V':
				ltfsresult(19546I, "ltfsindextool", PACKAGE_VERSION);
				ltfsresult(19546I, "LTFS Format Specification", LTFS_INDEX_VERSION_STR);
				return 0;
			case '?':
			default:
				show_usage(argv[0], opt.config, false);
				return INDEXTOOL_USAGE_SYNTAX_ERROR;
		}
	}

	if(argv[optind + num_of_o])
		opt.filename = SAFE_STRDUP(argv[optind + num_of_o]);

	if(_indextool_validate_options(argv[0], &opt)) {
		return PROG_USAGE_SYNTAX_ERROR;
	}

	/* Pick up default backend if one wasn't specified before */
	if (! opt.backend_path) {
		const char *default_backend = config_file_get_default_plugin("tape", opt.config);
		if (! default_backend) {
			ltfsmsg(LTFS_ERR, 10009E);
			return INDEXTOOL_OPERATIONAL_ERROR;
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
	if (opt.quiet && opt.trace) {
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
	else {
		log_level = LTFS_INFO;
		syslog_level = LTFS_NONE;
	}

	ltfs_set_log_level(log_level);
	ltfs_set_syslog_level(syslog_level);

	/* Starting ltfsindextool */
	ltfsmsg(LTFS_INFO, 19500I, PACKAGE_NAME, PACKAGE_VERSION, log_level);

	/* Show command line arguments */
	for (i = 0, cmd_args_len = 0 ; i < argc; i++) {
		cmd_args_len += strlen(argv[i]) + 1;
	}
	cmd_args = calloc(1, cmd_args_len + 1);
	if (!cmd_args) {
		/* Memory allocation failed */
		ltfsmsg(LTFS_ERR, 10001E, "ltfsindextool (arguments)");
		return INDEXTOOL_OPERATIONAL_ERROR;
	}
	SAFE_STRCAT_S(cmd_args, cmd_args_len, argv[0]);
	for (i = 1; i < argc; i++) {
		SAFE_STRCAT_S(cmd_args, cmd_args_len, " ");
		SAFE_STRCAT_S(cmd_args, cmd_args_len,argv[i]);
	}
	ltfsmsg(LTFS_INFO, 19542I, cmd_args);
	free(cmd_args);

	/* Show build time information */
	ltfsmsg(LTFS_INFO, 19502I, BUILD_SYS_FOR);
	ltfsmsg(LTFS_INFO, 19503I, BUILD_SYS_GCC);

	/* Show run time information */
	show_runtime_system_info();

	/* Actually mkltfs logic starts here */
	ret = ltfs_volume_alloc("dummy", &vol);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, 19501E);
		return INDEXTOOL_OPERATIONAL_ERROR;
	}

	switch (opt.mode) {
		case OP_CHECK:
			ret = check_index(vol, &opt, &args);
			break;
		case OP_CAPTURE:
			ret = capture_index(vol, &opt, &args);
			break;
		default:
			ltfsmsg(LTFS_ERR, 19541E);
			ret = PROG_USAGE_SYNTAX_ERROR;
			break;
	}

	/* Cleaning up */
	free(opt.backend_path);
	free(opt.kmi_backend_name);
	free(opt.devname);
	config_file_free(opt.config);
	ltfsprintf_unload_plugin(message_handle);
	ltfs_finish();

	return ret;
}
