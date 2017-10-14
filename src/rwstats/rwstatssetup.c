/*
** Copyright (C) 2001-2017 by Carnegie Mellon University.
**
** @OPENSOURCE_LICENSE_START@
** See license information in ../../LICENSE.txt
** @OPENSOURCE_LICENSE_END@
*/

/*
**  rwstatssetup.c
**
**  Application setup for rwstats.  See rwstats.c for a description.
*/

#include <silk/silk.h>

RCSIDENT("$SiLK: rwstatssetup.c e1c14c597311 2017-06-05 21:20:23Z mthomas $");

#include <silk/silkpython.h>
#include <silk/skcountry.h>
#include <silk/skplugin.h>
#include <silk/skprefixmap.h>
#include <silk/sksite.h>
#include <silk/skstringmap.h>
#include "rwstats.h"


/* TYPEDEFS AND DEFINES */

/* file handle for --help usage message */
#define USAGE_FH stdout

/* where to write filenames if --print-file specified */
#define PRINT_FILENAMES_FH  stderr

/* suffix for distinct fields */
#define DISTINCT_SUFFIX  "-Distinct"

/* type of field being defined */
typedef enum field_type_en {
    FIELD_TYPE_KEY, FIELD_TYPE_VALUE, FIELD_TYPE_DISTINCT
} field_type_t;


/* LOCAL VARIABLES */

/* create aliases for exisitng value fields.  the struct contains the
 * name of the alias and an ID to match in the builtin_values[]
 * array */
static const struct builtin_value_aliases_st {
    const char     *ba_name;
    sk_fieldid_t    ba_id;
} builtin_value_aliases[] = {
    {"Flows",   SK_FIELD_RECORDS},
    {NULL,      (sk_fieldid_t)0}
};

/* key fields used when parsing the user's --fields switch */
static sk_stringmap_t *key_field_map = NULL;

/* available aggregate value fields */
static sk_stringmap_t *value_field_map = NULL;

/* the text the user entered for the --fields switch */
static const char *fields_arg = NULL;

/* the text the user entered for the --values switch */
static const char *values_arg = NULL;

/* name of program to run to page output */
static char *pager;

/* where to copy the input to */
static skstream_t *copy_input = NULL;

/* temporary directory */
static const char *temp_directory = NULL;

/* how to print IP addresses */
static uint32_t ip_format = SKIPADDR_CANONICAL;

/* flags when registering --ip-format */
static const unsigned int ip_format_register_flags =
    (SK_OPTION_IP_FORMAT_INTEGER_IPS | SK_OPTION_IP_FORMAT_ZERO_PAD_IPS);

/* how to print timestamps */
static uint32_t timestamp_format = 0;

/* flags when registering --timestamp-format */
static const uint32_t time_register_flags =
    (SK_OPTION_TIMESTAMP_NEVER_MSEC | SK_OPTION_TIMESTAMP_OPTION_EPOCH
     | SK_OPTION_TIMESTAMP_OPTION_LEGACY);

/* the floor of the sTime and/or eTime */
static sktime_t time_bin_size = 0;

/* which of elapsed, sTime, and eTime were requested. uses the
 * PARSE_KEY_* values from rwstats.h.  this value will be used to
 * initialize the 'time_fields_key' global. */
static unsigned int time_fields;

/* input checker */
static sk_options_ctx_t *optctx = NULL;

/* fields that get defined just like plugins */
static const struct app_static_plugins_st {
    const char         *name;
    skplugin_setup_fn_t setup_fn;
} app_static_plugins[] = {
    {"addrtype",        skAddressTypesAddFields},
    {"ccfilter",        skCountryAddFields},
    {"pmapfilter",      skPrefixMapAddFields},
#if SK_ENABLE_PYTHON
    {"silkpython",      skSilkPythonAddFields},
#endif
    {NULL, NULL}        /* sentinel */
};

/* plug-ins to attempt to load at startup */
static const char *app_plugin_names[] = {
    NULL /* sentinel */
};

/* non-zero if we are shutting down due to a signal; controls whether
 * errors are printed in appTeardown(). */
static int caught_signal = 0;

/* did user ask for legacy help? */
static int legacy_help_requested = 0;


/* OPTIONS */

static struct option appOptions[] = {
    {"overall-stats",       NO_ARG,       0, OPT_OVERALL_STATS},
    {"detail-proto-stats",  REQUIRED_ARG, 0, OPT_DETAIL_PROTO_STATS},

    {"help-fields",         NO_ARG,       0, OPT_HELP_FIELDS},
    {"fields",              REQUIRED_ARG, 0, OPT_FIELDS},
    {"values",              REQUIRED_ARG, 0, OPT_VALUES},
    {"plugin",              REQUIRED_ARG, 0, OPT_PLUGIN},

    {"count",               REQUIRED_ARG, 0, OPT_COUNT},
    {"threshold",           REQUIRED_ARG, 0, OPT_THRESHOLD},
    {"percentage",          REQUIRED_ARG, 0, OPT_PERCENTAGE},

    {"top",                 NO_ARG,       0, OPT_TOP},
    {"bottom",              NO_ARG,       0, OPT_BOTTOM},

    {"presorted-input",     NO_ARG,       0, OPT_PRESORTED_INPUT},
    {"no-percents",         NO_ARG,       0, OPT_NO_PERCENTS},
    {"bin-time",            OPTIONAL_ARG, 0, OPT_BIN_TIME},
    {"integer-sensors",     NO_ARG,       0, OPT_INTEGER_SENSORS},
    {"integer-tcp-flags",   NO_ARG,       0, OPT_INTEGER_TCP_FLAGS},
    {"no-titles",           NO_ARG,       0, OPT_NO_TITLES},
    {"no-columns",          NO_ARG,       0, OPT_NO_COLUMNS},
    {"column-separator",    REQUIRED_ARG, 0, OPT_COLUMN_SEPARATOR},
    {"no-final-delimiter",  NO_ARG,       0, OPT_NO_FINAL_DELIMITER},
    {"delimited",           OPTIONAL_ARG, 0, OPT_DELIMITED},
    {"print-filenames",     NO_ARG,       0, OPT_PRINT_FILENAMES},
    {"copy-input",          REQUIRED_ARG, 0, OPT_COPY_INPUT},
    {"output-path",         REQUIRED_ARG, 0, OPT_OUTPUT_PATH},
    {"pager",               REQUIRED_ARG, 0, OPT_PAGER},

    {"legacy-help",         NO_ARG,       0, OPT_LEGACY_HELP},

    {0,0,0,0}               /* sentinel entry */
};

static const char *appHelp[] = {
    ("Print minima, maxima, quartiles, and interval-count\n"
     "\tstatistics for bytes, pkts, bytes/pkt across all flows.  Def. No"),
    ("Print above statistics for each of the specified\n"
     "\tprotocols.  List protocols or ranges separated by commas. Def. No"),

    "Describe each possible field and value and exit. Def. no",
    ("Use these fields as the grouping key. Specify fields as a\n"
     "\tcomma-separated list of names, IDs, and/or ID-ranges"),
    ("Compute these values for each group. Def. records.\n"
     "\tSpecify values as a comma-separated list of names.  The first\n"
     "\tvalue will be used as the basis for the Top-N/Bottom-N"),
    ("Load given plug-in to add fields and/or values. Switch may\n"
     "\tbe repeated to load multiple plug-ins. Def. None"),

    ("Print the specified number of bins"),
    ("Print bins where the primary value is greater-/less-than\n"
     "\tthis threshold. Not valid for primary values from plug-ins."),
    ("Print bins where the primary value is greater-/less-than\n"
     "\tthis percentage of the total across all flows. Only allowed when the\n"
     "\tprimary value field is Bytes, Packets, or Records."),

    ("Print the top N keys and their values. Def. Yes"),
    ("Print the bottom N keys and their values. Def. No"),

    ("Assume input has been presorted using\n"
     "\trwsort invoked with the exact same --fields value. Def. No"),
    ("Do not print the percentage columns. Def. Print percents"),
    ("When using 'sTime' or 'eTime' as a key, adjust time(s) to\n"
     "\tto appear in N-second bins (floor of time is used). Def. No, "),
    "Print sensor as an integer. Def. Sensor name",
    "Print TCP Flags as an integer. Def. No",
    "Do not print column titles. Def. Print titles",
    "Disable fixed-width columnar output. Def. Columnar",
    "Use specified character between columns. Def. '|'",
    "Suppress column delimiter at end of line. Def. No",
    "Shortcut for --no-columns --no-final-del --column-sep=CHAR",
    "Print names of input files as they are opened. Def. No",
    "Copy all input SiLK Flows to given pipe or file. Def. No",
    "Write the output to this stream or file. Def. stdout",
    "Invoke this program to page output. Def. $SILK_PAGER or $PAGER",
    "Print help, including legacy switches",
    (char *)NULL
};



/* LOCAL FUNCTION PROTOTYPES */

static void appHandleSignal(int sig);

static void helpFields(FILE *fh);

static int  createStringmaps(void);
static int  parseKeyFields(const char *field_string);
static int  parseValueFields(const char *field_string);
static int
appAddPlugin(
    skplugin_field_t   *pi_field,
    field_type_t        field_type);
static int
isFieldDuplicate(
    const sk_fieldlist_t   *flist,
    sk_fieldid_t            fid,
    const void             *fcontext);
static int  prepareFileForRead(skstream_t *stream);

static void topnSetup(const rwstats_legacy_t *leg);


/* FUNCTION DEFINITIONS */

/*
 *  appUsageLong();
 *
 *    Print complete usage information to USAGE_FH.  Pass this
 *    function to skOptionsSetUsageCallback(); skOptionsParse() will
 *    call this funciton and then exit the program when the --help
 *    option is given.
 */
static void
appUsageLong(
    void)
{
    FILE *fh = USAGE_FH;
    int i;

    /* use two macros to avoid CPP limit in c89 */
#define USAGE_MSG                                                             \
    ("<SWITCHES> [FILES]\n"                                                   \
     "\tSummarize SiLK Flow records by the specified field(s) into bins.\n"   \
     "\tFor each bin, compute the specified value(s), then display the\n"     \
     "\tresults as a Top-N or Bottom-N list based on the primary value.\n"    \
     "\tThe N may be a fixed value; some values allow the N to be a\n"        \
     "\tthreshold value or to be based on a percentage of the input.\n")
#define USAGE_MSG_2                                                           \
    ("\tAlternatively, provide statistics for each of bytes, packets, and\n" \
     "\tbytes-per-packet giving minima, maxima, quartile, and interval\n"     \
     "\tflow-counts across all flows or across user-specified protocols.\n"   \
     "\tWhen no files are given on command line, flows are read from STDIN.\n")

    /* Create the string maps for --fields and --values */
    createStringmaps();

    fprintf(fh, "%s %s%s", skAppName(), USAGE_MSG, USAGE_MSG_2);

    for (i = 0; appOptions[i].name; ++i) {
        /* Either print a header or invoke a function to print usage
         * for options before printing some options */
        switch ((appOptionsEnum)i) {
          case OPT_OVERALL_STATS:
            fprintf(fh, "\nPROTOCOL STATISTICS SWITCHES:\n");
            break;
          case OPT_FIELDS:
            fprintf(fh, "\nTOP-N/BOTTOM-N SWITCHES:\n");
            break;
          case OPT_COUNT:
            fprintf(fh, ("\nHow to determine the N for Top-/Bottom-N;"
                         " must specify one:\n"));
            break;
          case OPT_TOP:
            fprintf(fh, ("\nWhether to compute Top- or Bottom-N;"
                         " may specify one (top is default):\n"));
            break;
          case OPT_PRESORTED_INPUT:
            fprintf(fh, "\nMISCELLANEOUS SWITCHES:\n");
            skOptionsDefaultUsage(fh);
            break;
          case OPT_BIN_TIME:
            skOptionsCtxOptionsUsage(optctx, fh);
            skIPv6PolicyUsage(fh);
            break;
          case OPT_INTEGER_SENSORS:
            skOptionsTimestampFormatUsage(fh);
            skOptionsIPFormatUsage(fh);
            break;
          default:
            break;
        }

        fprintf(fh, "--%s %s. ", appOptions[i].name,
                SK_OPTION_HAS_ARG(appOptions[i]));
        switch ((appOptionsEnum)i) {
          case OPT_FIELDS:
            /* Dynamically build the help */
            fprintf(fh, "%s\n", appHelp[i]);
            skStringMapPrintUsage(key_field_map, fh, 4);
            break;
          case OPT_VALUES:
            fprintf(fh, "%s\n", appHelp[i]);
            skStringMapPrintUsage(value_field_map, fh, 4);
            break;
          case OPT_BIN_TIME:
            fprintf(fh, "%s%d\n", appHelp[i], DEFAULT_TIME_BIN);
            break;
          default:
            /* Simple help text from the appHelp array */
            fprintf(fh, "%s\n", appHelp[i]);
            break;
        }
    }

    skOptionsTempDirUsage(fh);
    sksiteOptionsUsage(fh);
    skPluginOptionsUsage(fh);
    if (legacy_help_requested) {
        legacyOptionsUsage(fh);
    }
}


/*
 *  appSetup(argc, argv);
 *
 *    Perform all the setup for this application include setting up
 *    required modules, parsing options, etc.  This function should be
 *    passed the same arguments that were passed into main().
 *
 *    Returns to the caller if all setup succeeds.  If anything fails,
 *    this function will cause the application to exit with a FAILURE
 *    exit status.
 */
void
appSetup(
    int                 argc,
    char              **argv)
{
    SILK_FEATURES_DEFINE_STRUCT(features);
    unsigned int optctx_flags;
    int rv;
    rwstats_legacy_t leg;
    int j;

    /* verify same number of options and help strings */
    assert((sizeof(appHelp)/sizeof(char *)) ==
           (sizeof(appOptions)/sizeof(struct option)));

    /* register the application */
    skAppRegister(argv[0]);
    skAppVerifyFeatures(&features, NULL);
    skOptionsSetUsageCallback(&appUsageLong);

    /* initialize globals */
    memset(&app_flags, 0, sizeof(app_flags));
    memset(&output, 0, sizeof(output));
    output.of_fp = stdout;
    memset(&leg, 0, sizeof(rwstats_legacy_t));
    limit.type = RWSTATS_ALL;

    optctx_flags = (SK_OPTIONS_CTX_INPUT_SILK_FLOW | SK_OPTIONS_CTX_ALLOW_STDIN
                    | SK_OPTIONS_CTX_XARGS);

    /* initialize plugin library */
    skPluginSetup(2, SKPLUGIN_APP_STATS_FIELD, SKPLUGIN_APP_STATS_VALUE);

    /* register the options */
    if (skOptionsCtxCreate(&optctx, optctx_flags)
        || skOptionsCtxOptionsRegister(optctx)
        || skOptionsRegister(appOptions, &appOptionsHandler, NULL)
        || legacyOptionsSetup(&leg)
        || skOptionsTempDirRegister(&temp_directory)
        || skOptionsTimestampFormatRegister(
            &timestamp_format, time_register_flags)
        || skOptionsIPFormatRegister(&ip_format, ip_format_register_flags)
        || skIPv6PolicyOptionsRegister(&ipv6_policy)
        || sksiteOptionsRegister(SK_SITE_FLAG_CONFIG_FILE))
    {
        skAppPrintErr("Unable to register options");
        appExit(EXIT_FAILURE);
    }

    /* register the teardown handler */
    if (atexit(appTeardown) < 0) {
        skAppPrintErr("Unable to register appTeardown() with atexit()");
        appExit(EXIT_FAILURE);
    }

    /* try to load hard-coded plugins */
    for (j = 0; app_static_plugins[j].name; ++j) {
        skPluginAddAsPlugin(app_static_plugins[j].name,
                            app_static_plugins[j].setup_fn);
    }
    for (j = 0; app_plugin_names[j]; ++j) {
        skPluginLoadPlugin(app_plugin_names[j], 0);
    }

    /* parse options */
    rv = skOptionsCtxOptionsParse(optctx, argc, argv);
    if (rv < 0) {
        skAppUsage();           /* never returns */
    }

    /* try to load site config file; if it fails, we will not be able
     * to resolve flowtype and sensor from input file names, but we
     * should not consider it a complete failure */
    sksiteConfigure(0);

    /* set final delimeter */
    if (!app_flags.no_final_delimiter) {
        final_delim[0] = delimiter;
    }

    /* create the ascii stream and set its properties */
    if (rwAsciiStreamCreate(&ascii_str)) {
        skAppPrintErr("Unable to create ascii stream");
        appExit(EXIT_FAILURE);
    }
    if (app_flags.no_percents) {
        if (app_flags.no_final_delimiter) {
            rwAsciiSetNoFinalDelimiter(ascii_str);
        }
    } else {
        /* rwstats will be printing additional columns */
        rwAsciiSetNoFinalDelimiter(ascii_str);
        rwAsciiSetNoNewline(ascii_str);
    }
    rwAsciiSetDelimiter(ascii_str, delimiter);
    rwAsciiSetIPv6Policy(ascii_str, ipv6_policy);
    rwAsciiSetIPFormatFlags(ascii_str, ip_format);
    rwAsciiSetTimestampFlags(ascii_str, timestamp_format);
    if (app_flags.no_titles) {
        rwAsciiSetNoTitles(ascii_str);
    }
    if (app_flags.no_columns) {
        rwAsciiSetNoColumns(ascii_str);
    }
    if (app_flags.integer_sensors) {
        rwAsciiSetIntegerSensors(ascii_str);
    }
    if (app_flags.integer_tcp_flags) {
        rwAsciiSetIntegerTcpFlags(ascii_str);
    }

    /* do additional setup for handling topn */
    if (!proto_stats) {
        topnSetup(&leg);
    }

    /* make certain stdout is not being used for multiple outputs */
    if (copy_input
        && ((0 == strcmp(skStreamGetPathname(copy_input), "-"))
            || (0 == strcmp(skStreamGetPathname(copy_input), "stdout"))))
    {
        if ((NULL == output.of_name)
            || (0 == strcmp(output.of_name, "-"))
            || (0 == strcmp(output.of_name, "stdout")))
        {
            skAppPrintErr("May not use stdout for multiple output streams");
            exit(EXIT_FAILURE);
        }
    }

    /* open the --output-path.  the 'of_name' member is NULL if user
     * didn't get an output-path. */
    if (output.of_name) {
        rv = skFileptrOpen(&output, SK_IO_WRITE);
        if (rv) {
            skAppPrintErr("Unable to open %s '%s': %s",
                          appOptions[OPT_OUTPUT_PATH].name,
                          output.of_name, skFileptrStrerror(rv));
            appExit(EXIT_FAILURE);
        }
    }

    /* open the --copy-input destination */
    if (copy_input) {
        rv = skStreamOpen(copy_input);
        if (rv) {
            skStreamPrintLastErr(copy_input, rv, &skAppPrintErr);
            appExit(EXIT_FAILURE);
        }
    }

    /* set signal handler to clean up temp files on SIGINT, SIGTERM, etc */
    if (skAppSetSignalHandler(&appHandleSignal)) {
        appExit(EXIT_FAILURE);
    }

    return;                       /* OK */
}


/*
 *  appTeardown()
 *
 *    Teardown all modules, close all files, and tidy up all
 *    application state.
 *
 *    This function is idempotent.
 */
void
appTeardown(
    void)
{
    static int teardownFlag = 0;
    int rv;

    if (teardownFlag) {
        return;
    }
    teardownFlag = 1;

    skUniqueDestroy(&uniq);
    skPresortedUniqueDestroy(&ps_uniq);

    /* destroy field lists */
    skFieldListDestroy(&key_fields);
    skFieldListDestroy(&distinct_fields);
    skFieldListDestroy(&value_fields);

    /* plugin teardown */
    skPluginRunCleanup(SKPLUGIN_FN_ANY);
    skPluginTeardown();

    /* destroy output */
    rwAsciiStreamDestroy(&ascii_str);

    /* close output */
    if (output.of_name) {
        skFileptrClose(&output, &skAppPrintErr);
    }
    /* close the --copy-input */
    if (copy_input) {
        rv = skStreamClose(copy_input);
        if (rv && rv != SKSTREAM_ERR_NOT_OPEN) {
            skStreamPrintLastErr(copy_input, rv, &skAppPrintErr);
        }
        skStreamDestroy(&copy_input);
    }

    /* destroy string maps for keys and values */
    if (key_field_map) {
        skStringMapDestroy(key_field_map);
        key_field_map = NULL;
    }
    if (value_field_map) {
        skStringMapDestroy(value_field_map);
        value_field_map = NULL;
    }

    skOptionsCtxDestroy(&optctx);
    skAppUnregister();
}


/*
 *  status = appOptionsHandler(cData, opt_index, opt_arg);
 *
 *    Called by skOptionsParse(), this handles a user-specified switch
 *    that the application has registered, typically by setting global
 *    variables.  Returns 1 if the switch processing failed or 0 if it
 *    succeeded.  Returning a non-zero from from the handler causes
 *    skOptionsParse() to return a negative value.
 *
 *    The clientData in 'cData' is typically ignored; 'opt_index' is
 *    the index number that was specified as the last value for each
 *    struct option in appOptions[]; 'opt_arg' is the user's argument
 *    to the switch for options that have a REQUIRED_ARG or an
 *    OPTIONAL_ARG.
 */
int
appOptionsHandler(
    clientData   UNUSED(cData),
    int                 opt_index,
    char               *opt_arg)
{
    static int saw_direction = 0;
    uint32_t val32;
    int rv;

    switch ((appOptionsEnum)opt_index) {
      case OPT_HELP_FIELDS:
        helpFields(USAGE_FH);
        exit(EXIT_SUCCESS);

      case OPT_FIELDS:
        if (fields_arg) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        fields_arg = opt_arg;
        break;

      case OPT_VALUES:
        if (values_arg) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        values_arg = opt_arg;
        break;

      case OPT_TOP:
      case OPT_BOTTOM:
        if (saw_direction) {
            skAppPrintErr("May only specify one of --%s or --%s.",
                          appOptions[OPT_TOP].name,
                          appOptions[OPT_BOTTOM].name);
            return 1;
        }
        saw_direction = 1;
        if (OPT_TOP == opt_index) {
            direction = RWSTATS_DIR_TOP;
        } else {
            direction = RWSTATS_DIR_BOTTOM;
        }
        break;

      case OPT_COUNT:
      case OPT_THRESHOLD:
      case OPT_PERCENTAGE:
        if (limit.seen != 0) {
            skAppPrintErr("May only specify one of --%s, --%s, or --%s.",
                          appOptions[OPT_COUNT].name,
                          appOptions[OPT_THRESHOLD].name,
                          appOptions[OPT_PERCENTAGE].name);
            return 1;
        }
        limit.type = ((rwstats_limit_type_t)
                      (RWSTATS_COUNT + (opt_index - OPT_COUNT)));
        if (OPT_PERCENTAGE == opt_index) {
            rv = skStringParseDouble(&limit.value[limit.type].d, opt_arg,
                                     0.0, 100.0);
        } else {
            rv = skStringParseUint64(&limit.value[limit.type].u64, opt_arg,
                                     0, 0);
        }
        if (rv) {
            goto PARSE_ERROR;
        }
        if (OPT_COUNT == opt_index && 0 == limit.value[limit.type].u64) {
            limit.type = RWSTATS_ALL;
        }
        limit.seen = 1;
        break;

      case OPT_OVERALL_STATS:
        /* combined stats for all protocols */
        proto_stats = 1;
        break;

      case OPT_DETAIL_PROTO_STATS:
        /* detailed stats for specific proto */
        if (0 != protoStatsParse(opt_arg)) {
            return 1;
        }
        proto_stats = 1;
        break;

      case OPT_PLUGIN:
        if (skPluginLoadPlugin(opt_arg, 1) != 0) {
            skAppPrintErr("Unable to load %s as a plugin", opt_arg);
            return 1;
        }
        break;

      case OPT_BIN_TIME:
        if (opt_arg == NULL || opt_arg[0] == '\0') {
            /* no time given; use default */
            time_bin_size = sktimeCreate(DEFAULT_TIME_BIN, 0);
        } else {
            /* parse user's time */
            rv = skStringParseUint32(&val32, opt_arg, 1, 0);
            if (rv) {
                goto PARSE_ERROR;
            }
            time_bin_size = sktimeCreate(val32, 0);
        }
        break;

      case OPT_PRESORTED_INPUT:
        app_flags.presorted_input = 1;
        break;

      case OPT_NO_PERCENTS:
        app_flags.no_percents = 1;
        break;

      case OPT_INTEGER_SENSORS:
        app_flags.integer_sensors = 1;
        break;

      case OPT_INTEGER_TCP_FLAGS:
        app_flags.integer_tcp_flags = 1;
        break;

      case OPT_NO_TITLES:
        app_flags.no_titles = 1;
        break;

      case OPT_NO_COLUMNS:
        memset(width, 0, RWSTATS_COLUMN_WIDTH_COUNT * sizeof(width[0]));
        app_flags.no_columns = 1;
        break;

      case OPT_NO_FINAL_DELIMITER:
        app_flags.no_final_delimiter = 1;
        break;

      case OPT_COLUMN_SEPARATOR:
        delimiter = opt_arg[0];
        break;

      case OPT_DELIMITED:
        memset(width, 0, RWSTATS_COLUMN_WIDTH_COUNT * sizeof(width[0]));
        app_flags.no_columns = 1;
        app_flags.no_final_delimiter = 1;
        if (opt_arg) {
            delimiter = opt_arg[0];
        }
        break;

      case OPT_PRINT_FILENAMES:
        app_flags.print_filenames = 1;
        break;

      case OPT_COPY_INPUT:
        if (copy_input) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        if ((rv=skStreamCreate(&copy_input, SK_IO_WRITE, SK_CONTENT_SILK_FLOW))
            || (rv = skStreamBind(copy_input, opt_arg)))
        {
            skStreamPrintLastErr(copy_input, rv, &skAppPrintErr);
            return 1;
        }
        break;

      case OPT_OUTPUT_PATH:
        if (output.of_name) {
            skAppPrintErr("Invalid %s: Switch used multiple times",
                          appOptions[opt_index].name);
            return 1;
        }
        output.of_name = opt_arg;
        break;

      case OPT_PAGER:
        pager = opt_arg;
        break;

      case OPT_LEGACY_HELP:
        legacy_help_requested = 1;
        appUsageLong();
        exit(EXIT_SUCCESS);
    }

    return 0;                     /* OK */

  PARSE_ERROR:
    skAppPrintErr("Invalid %s '%s': %s",
                  appOptions[opt_index].name, opt_arg,
                  skStringParseStrerror(rv));
    return 1;
}


/*
 *  appExit(status)
 *
 *  Exit the application with the given status.
 */
void
appExit(
    int                 status)
{
    appTeardown();
    exit(status);
}


/*
 *  appHandleSignal(signal_value)
 *
 *    Call appExit() to exit the program.  If signal_value is SIGPIPE,
 *    close cleanly; otherwise print a message that we've caught the
 *    signal and exit with EXIT_FAILURE.
 */
static void
appHandleSignal(
    int                 sig)
{
    caught_signal = 1;

    if (sig == SIGPIPE) {
        /* we get SIGPIPE if something downstream, like rwcut, exits
         * early, so don't bother to print a warning, and exit
         * successfully */
        appExit(EXIT_SUCCESS);
    } else {
        skAppPrintErr("Caught signal..cleaning up and exiting");
        appExit(EXIT_FAILURE);
    }
}


/*
 *  topnSetup(legacy_args);
 *
 *    Do everything needed to setup a top-N/bottom-N run.  This
 *    function exits on error.
 */
static void
topnSetup(
    const rwstats_legacy_t *leg)
{
    char *path;
    int rv;

    /* verify that we have an N for our top-N */
    if (!limit.seen) {
        /* remove this block if we want printing all bins to be the
         * default behavior of rwstats */
        skAppPrintErr(("No stopping condition was entered.\n"
                       "\tChoose one of --%s, --%s, or --%s"),
                      appOptions[OPT_COUNT].name,
                      appOptions[OPT_THRESHOLD].name,
                      appOptions[OPT_PERCENTAGE].name);
        skAppUsage();
    }

    /* set up the key_field_map and value_field_map */
    if (createStringmaps()) {
        appExit(EXIT_FAILURE);
    }

    /* make sure the user specified the --fields switch */
    if (leg->fields) {
        if (fields_arg) {
            skAppPrintErr("Cannot use --%s and old style switches",
                          appOptions[OPT_FIELDS].name);
            skAppUsage();
        }
        fields_arg = leg->fields;
    } else if (fields_arg == NULL || fields_arg[0] == '\0') {
        skAppPrintErr("The --%s switch is required",
                      appOptions[OPT_FIELDS].name);
        skAppUsage();         /* never returns */
    }

    /* find the values to use, or default to counting flows */
    if (leg->values) {
        if (values_arg != NULL) {
            skAppPrintErr("Cannot use --%s and old style switches",
                          appOptions[OPT_VALUES].name);
            skAppUsage();
        }
        values_arg = leg->values;
    } else if (values_arg == NULL) {
        values_arg = "records";
    } else if (values_arg[0] == '\0') {
        skAppPrintErr("The --%s switch is required",
                      appOptions[OPT_VALUES].name);
        skAppUsage();         /* never returns */
    }

    /* parse the --fields and --values switches */
    if (parseKeyFields(fields_arg)) {
        appExit(EXIT_FAILURE);
    }
    if (parseValueFields(values_arg)) {
        appExit(EXIT_FAILURE);
    }

    /* create and initialize the uniq object */
    if (app_flags.presorted_input) {
        /* cannot use the --percentage limit when using
         * --presorted-input */
        if (RWSTATS_PERCENTAGE == limit.type) {
            skAppPrintErr(("The --%s limit is not supported"
                           " when --%s is active"),
                          appOptions[OPT_PERCENTAGE].name,
                          appOptions[OPT_PRESORTED_INPUT].name);
            appExit(EXIT_FAILURE);
        }

        if (skPresortedUniqueCreate(&ps_uniq)) {
            appExit(EXIT_FAILURE);
        }

        skPresortedUniqueSetTempDirectory(ps_uniq, temp_directory);

        if (skPresortedUniqueSetFields(ps_uniq, key_fields, distinct_fields,
                                       value_fields))
        {
            skAppPrintErr("Unable to set fields");
            appExit(EXIT_FAILURE);
        }
        if (limit.distinct) {
            if (skPresortedUniqueEnableTotalDistinct(ps_uniq)) {
                skAppPrintErr("Unable to set fields");
                appExit(EXIT_FAILURE);
            }
        }

        while ((rv = skOptionsCtxNextArgument(optctx, &path)) == 0) {
            skPresortedUniqueAddInputFile(ps_uniq, path);
        }
        if (rv < 0) {
            appExit(EXIT_FAILURE);
        }

        skPresortedUniqueSetPostOpenFn(ps_uniq, prepareFileForRead);
        skPresortedUniqueSetReadFn(ps_uniq, readRecord);

    } else {
        if (skUniqueCreate(&uniq)) {
            appExit(EXIT_FAILURE);
        }

        skUniqueSetTempDirectory(uniq, temp_directory);

        rv = skUniqueSetFields(uniq, key_fields, distinct_fields, value_fields);
        if (0 == rv && limit.distinct) {
            rv = skUniqueEnableTotalDistinct(uniq);
        }
        if (0 == rv) {
            rv = skUniquePrepareForInput(uniq);
        }
        if (rv) {
            skAppPrintErr("Unable to set fields");
            appExit(EXIT_FAILURE);
        }
    }
}


/*
 *  helpFields(fh);
 *
 *    Print a description of each field to the 'fh' file pointer
 */
static void
helpFields(
    FILE               *fh)
{
    if (createStringmaps()) {
        exit(EXIT_FAILURE);
    }

    fprintf(fh,
            ("The following names may be used in the --%s switch. Names are\n"
             "case-insensitive and may be abbreviated to the shortest"
             " unique prefix.\n"),
            appOptions[OPT_FIELDS].name);
    skStringMapPrintDetailedUsage(key_field_map, fh);

    fprintf(fh,
            ("\n"
             "The following names may be used in the --%s switch. Names are\n"
             "case-insensitive and may be abbreviated to the shortest"
             " unique prefix.\n"),
            appOptions[OPT_VALUES].name);
    skStringMapPrintDetailedUsage(value_field_map, fh);
}


/*
 *  builtin_value_get_title(buf, bufsize, field_entry);
 *
 *    Invoked by rwAsciiPrintTitles() to get the title for an
 *    aggregate value field represented by an sk_fieldid_t.
 *
 *    Fill 'buf' with the title for the column represented by the
 *    field list entry 'field_entry'.  This function should write no
 *    more than 'bufsize' characters to 'buf'.
 */
static void
builtin_value_get_title(
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    builtin_field_t *bf;

    bf = (builtin_field_t*)skFieldListEntryGetContext(fl_entry);
    strncpy(text_buf, bf->bf_title, text_buf_size);
}

/*
 *  value_to_ascii(rwrec, buf, bufsize, field_entry, extra);
 *
 *    Invoked by rwAsciiPrintRecExtra() to get the value for an
 *    aggregate value field.  This function is called for built-in
 *    aggregate values as well as plug-in defined values.
 *
 *    Fill 'buf' with the value for the column represented by the
 *    aggregate value field list entry 'field_entry'.  'rwrec' is
 *    ignored; 'extra' is a byte-array of the values from the heap
 *    data structure.  This function should write no more than
 *    'bufsize' characters to 'buf'.
 */
static int
value_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry,
    void               *v_heap_ptr)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    uint64_t val64;
    uint32_t val32;
    uint8_t bin_buf[HASHLIB_MAX_VALUE_WIDTH];

    switch (skFieldListEntryGetId(fl_entry)) {
      case SK_FIELD_SUM_BYTES:
      case SK_FIELD_SUM_PACKETS:
        skFieldListExtractFromBuffer(value_fields, HEAP_PTR_VALUE(v_heap_ptr),
                                     fl_entry, (uint8_t*)&val64);
        snprintf(text_buf, text_buf_size, ("%" PRIu64), val64);
        break;

      case SK_FIELD_RECORDS:
      case SK_FIELD_SUM_ELAPSED:
        skFieldListExtractFromBuffer(value_fields, HEAP_PTR_VALUE(v_heap_ptr),
                                     fl_entry, (uint8_t*)&val32);
        snprintf(text_buf, text_buf_size, ("%" PRIu32), val32);
        break;

      case SK_FIELD_CALLER:
        /* get the binary value from the field-list */
        skFieldListExtractFromBuffer(value_fields, HEAP_PTR_VALUE(v_heap_ptr),
                                     fl_entry, bin_buf);
        /* call the plug-in to convert from binary to text */
        skPluginFieldRunBinToTextFn(
            (skplugin_field_t*)skFieldListEntryGetContext(fl_entry),
            text_buf, text_buf_size, bin_buf);
        break;

      default:
        skAbortBadCase(skFieldListEntryGetId(fl_entry));
    }

    return 0;
}

/*
 *  builtin_distinct_get_title(buf, bufsize, field_entry);
 *
 *    Invoked by rwAsciiPrintTitles() to get the title for a distinct
 *    field represented by an sk_fieldid_t.
 *
 *    Fill 'buf' with the title for the column represented by the
 *    field list entry 'field_entry'.  This function should write no
 *    more than 'bufsize' characters to 'buf'.
 */
static void
builtin_distinct_get_title(
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    int id;
    size_t sz;

    id = skFieldListEntryGetId(fl_entry);
    switch (id) {
      case SK_FIELD_SIPv4:
      case SK_FIELD_SIPv6:
        rwAsciiGetFieldName(text_buf, text_buf_size, RWREC_FIELD_SIP);
        break;
      case SK_FIELD_DIPv4:
      case SK_FIELD_DIPv6:
        rwAsciiGetFieldName(text_buf, text_buf_size, RWREC_FIELD_DIP);
        break;
      case SK_FIELD_NHIPv4:
      case SK_FIELD_NHIPv6:
        rwAsciiGetFieldName(text_buf, text_buf_size, RWREC_FIELD_NHIP);
        break;
      default:
        rwAsciiGetFieldName(text_buf, text_buf_size,
                            (rwrec_printable_fields_t)id);
        break;
    }
    sz = strlen(text_buf);
    strncpy(text_buf+sz, DISTINCT_SUFFIX, text_buf_size-sz);
    text_buf[text_buf_size-1] = '\0';
}

/*
 *  distinct_to_ascii(rwrec, buf, bufsize, field_entry, extra);
 *
 *    Invoked by rwAsciiPrintRecExtra() to get the value for a
 *    distinct field.  This function is called for built-in distinct
 *    fields as well as those from a plug-in.
 *
 *    Fill 'buf' with the value for the column represented by the
 *    distinct field list entry 'field_entry'.  'rwrec' is ignored;
 *    'extra' is a byte-array of the values from the heap data
 *    structure.  This function should write no more than 'bufsize'
 *    characters to 'buf'.
 */
static int
distinct_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry,
    void               *v_heap_ptr)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    size_t len;
    union value_un {
        uint8_t   ar[HASHLIB_MAX_VALUE_WIDTH];
        uint64_t  u64;
        uint32_t  u32;
        uint16_t  u16;
        uint8_t   u8;
    } value;

    len = skFieldListEntryGetBinOctets(fl_entry);
    switch (len) {
      case 1:
        skFieldListExtractFromBuffer(distinct_fields,
                                     HEAP_PTR_DISTINCT(v_heap_ptr),
                                     fl_entry, &value.u8);
        snprintf(text_buf, text_buf_size, ("%" PRIu8), value.u8);
        break;
      case 2:
        skFieldListExtractFromBuffer(distinct_fields,
                                     HEAP_PTR_DISTINCT(v_heap_ptr),
                                     fl_entry, (uint8_t*)&value.u16);
        snprintf(text_buf, text_buf_size, ("%" PRIu16), value.u16);
        break;
      case 4:
        skFieldListExtractFromBuffer(distinct_fields,
                                     HEAP_PTR_DISTINCT(v_heap_ptr),
                                     fl_entry, (uint8_t*)&value.u32);
        snprintf(text_buf, text_buf_size, ("%" PRIu32), value.u32);
        break;
      case 8:
        skFieldListExtractFromBuffer(distinct_fields,
                                     HEAP_PTR_DISTINCT(v_heap_ptr),
                                     fl_entry, (uint8_t*)&value.u64);
        snprintf(text_buf, text_buf_size, ("%" PRIu64), value.u64);
        break;

      case 3:
      case 5:
      case 6:
      case 7:
        value.u64 = 0;
#if SK_BIG_ENDIAN
        skFieldListExtractFromBuffer(distinct_fields,
                                     HEAP_PTR_DISTINCT(v_heap_ptr),
                                     fl_entry, &value.ar[8 - len]);
#else
        skFieldListExtractFromBuffer(distinct_fields,
                                     HEAP_PTR_DISTINCT(v_heap_ptr),
                                     fl_entry, &value.ar[0]);
#endif  /* #else of #if SK_BIG_ENDIAN */
        snprintf(text_buf, text_buf_size, ("%" PRIu64), value.u64);
        break;

      default:
        skFieldListExtractFromBuffer(distinct_fields,
                                     HEAP_PTR_DISTINCT(v_heap_ptr),
                                     fl_entry, value.ar);
        snprintf(text_buf, text_buf_size, ("%" PRIu64), value.u64);
        break;
    }

    return 0;
}

/*
 *  plugin_get_title(buf, buf_size, field_entry);
 *
 *    Invoked by rwAsciiPrintTitles() to get the title for a key or
 *    aggregate value field defined by a plug-in.
 *
 *    Fill 'buf' with the title for the column represented by the
 *    plug-in associated with 'field_entry'.  This function should
 *    write no more than 'bufsize' characters to 'buf'.
 */
static void
plugin_get_title(
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    const char *title;

    skPluginFieldTitle(
        (skplugin_field_t*)skFieldListEntryGetContext(fl_entry), &title);
    strncpy(text_buf, title, text_buf_size);
    text_buf[text_buf_size-1] = '\0';
}

/*
 *  plugin_distinct_get_title(buf, bufsize, field_entry);
 *
 *    Invoked by rwAsciiPrintTitles() to get the title for a distinct
 *    field.
 *
 *    Fill 'buf' with the title for the column represented by a
 *    distinct count over the plug-in associated with 'field_entry'.
 *    This function should write no more than 'bufsize' characters to
 *    'buf'.
 */
static void
plugin_distinct_get_title(
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    const char *title;

    skPluginFieldTitle(
        (skplugin_field_t*)skFieldListEntryGetContext(fl_entry), &title);
    snprintf(text_buf, text_buf_size, ("%s" DISTINCT_SUFFIX), title);
}

/*
 *  plugin_key_to_ascii(rwrec, buf, bufsize, keyfield, extra);
 *
 *    Invoked by rwAsciiPrintRecExtra() to get the value for a key
 *    field that is defined by a plug-in.
 *
 *    Fill 'buf' with a textual representation of the key for the
 *    column represented by the plug-in associated with 'field_entry'.
 *    'rwrec' is ignored; 'extra' is a byte-array of the values from
 *    the heap data structure.  This function should write no more
 *    than 'bufsize' characters to 'buf'.
 */
static int
plugin_key_to_ascii(
    const rwRec UNUSED(*rwrec),
    char               *text_buf,
    size_t              text_buf_size,
    void               *v_fl_entry,
    void               *v_heap_ptr)
{
    sk_fieldentry_t *fl_entry = (sk_fieldentry_t*)v_fl_entry;
    uint8_t bin_buf[HASHLIB_MAX_KEY_WIDTH];

    /* get the binary value from the field-list */
    skFieldListExtractFromBuffer(key_fields, HEAP_PTR_KEY(v_heap_ptr),
                                 fl_entry, bin_buf);

    /* call the plug-in to convert from binary to text */
    skPluginFieldRunBinToTextFn(
        (skplugin_field_t*)skFieldListEntryGetContext(fl_entry),
        text_buf, text_buf_size, bin_buf);

    return 0;
}

/*
 *  plugin_rec_to_bin(rwrec, out_buf, plugin_field);
 *
 *    Invoked by skFieldListRecToBinary() to get the binary value,
 *    based on the given 'rwrec', for a key field that is defined by a
 *    plug-in.
 *
 *    The size of 'out_buf' was specified when the field was added to
 *    the field-list.
 */
static void
plugin_rec_to_bin(
    const rwRec        *rwrec,
    uint8_t            *out_buf,
    void               *v_pi_field)
{
    skPluginFieldRunRecToBinFn((skplugin_field_t*)v_pi_field,
                               out_buf, rwrec, NULL);
}

/*
 *  plugin_add_rec_to_bin(rwrec, in_out_buf, plugin_field);
 *
 *    Invoked by skFieldListAddRecToBinary() to get the binary value,
 *    based on the given 'rwrec' and merge that with the current
 *    binary value for a key field that is defined by a plug-in.
 *
 *    The size of 'out_buf' was specified when the field was added to
 *    the field-list.
 */
static void
plugin_add_rec_to_bin(
    const rwRec        *rwrec,
    uint8_t            *in_out_buf,
    void               *v_pi_field)
{
    skPluginFieldRunAddRecToBinFn((skplugin_field_t*)v_pi_field,
                                  in_out_buf, rwrec, NULL);
}

/*
 *  plugin_bin_compare(buf1, buf2, plugin_field);
 *
 *    Invoked by skFieldListCompareBuffers() to compare current value
 *    of the key or aggregate value fields specified by 'buf1' and
 *    'buf2'.
 *
 *    The size of 'buf1' and 'buf2' were specified when the field was
 *    added to the field-list.
 */
static int
plugin_bin_compare(
    const uint8_t      *buf1,
    const uint8_t      *buf2,
    void               *v_pi_field)
{
    int val = 0;
    skPluginFieldRunBinCompareFn((skplugin_field_t*)v_pi_field,
                                 &val, buf1, buf2);
    return val;
}

/*
 *  plugin_bin_merge(in_out_buf, in_buf, plugin_field);
 *
 *    Invoked by skFieldListMergeBuffers() to merge the current values
 *    of the key or aggregate value fields specified by 'in_out_buf'
 *    and 'in_buf'.  The merged value should be placed into
 *    'in_out_buf'.
 *
 *    The size of 'in_out_buf' and 'in_buf' were specified when the
 *    field was added to the field-list.
 */
static void
plugin_bin_merge(
    uint8_t            *in_out_buf,
    const uint8_t      *in_buf,
    void               *v_pi_field)
{
    skPluginFieldRunBinMergeFn((skplugin_field_t*)v_pi_field,
                               in_out_buf, in_buf);
}


/*
 *  ok = createStringmaps();
 *
 *    Create the string-maps to assist in parsing the --fields and
 *    --values switches.
 */
static int
createStringmaps(
    void)
{
    skplugin_field_iter_t  pi_iter;
    skplugin_err_t         pi_err;
    skplugin_field_t      *pi_field;
    sk_stringmap_status_t  sm_err;
    sk_stringmap_entry_t   sm_entry;
    const char           **field_names;
    const char           **name;
    uint32_t               max_id;
    size_t                 i;
    size_t                 j;

    /* initialize string-map of field identifiers: add default fields,
     * then remove millisec fields, since unique-ing over them makes
     * little sense.
     *
     * Note that although we remove the MSEC fields from the available
     * fields here, the remainder of the code still supports MSEC
     * fields---which are mapped onto the non-MSEC versions of the
     * fields. */
    if (rwAsciiFieldMapAddDefaultFields(&key_field_map)) {
        skAppPrintErr("Unable to setup fields stringmap");
        return -1;
    }
    (void)skStringMapRemoveByID(key_field_map, RWREC_FIELD_STIME_MSEC);
    (void)skStringMapRemoveByID(key_field_map, RWREC_FIELD_ETIME_MSEC);
    (void)skStringMapRemoveByID(key_field_map, RWREC_FIELD_ELAPSED_MSEC);
    max_id = RWREC_PRINTABLE_FIELD_COUNT - 1;

    /* add "icmpTypeCode" field */
    ++max_id;
    if (rwAsciiFieldMapAddIcmpTypeCode(key_field_map, max_id)) {
        skAppPrintErr("Unable to add icmpTypeCode");
        return -1;
    }

    /* add --fields from the plug-ins */
    pi_err = skPluginFieldIteratorBind(&pi_iter, SKPLUGIN_APP_STATS_FIELD, 1);
    if (pi_err != SKPLUGIN_OK) {
        assert(pi_err == SKPLUGIN_OK);
        skAppPrintErr("Unable to bind plugin field iterator");
        return -1;
    }

    while (skPluginFieldIteratorNext(&pi_iter, &pi_field)) {
        skPluginFieldName(pi_field, &field_names);
        ++max_id;

        /* Add keys to the key_field_map */
        for (name = field_names; *name; name++) {
            memset(&sm_entry, 0, sizeof(sm_entry));
            sm_entry.name = *name;
            sm_entry.id = max_id;
            sm_entry.userdata = pi_field;
            skPluginFieldDescription(pi_field, &sm_entry.description);
            sm_err = skStringMapAddEntries(key_field_map, 1, &sm_entry);
            if (sm_err != SKSTRINGMAP_OK) {
                const char *plugin_name;
                skPluginFieldGetPluginName(pi_field, &plugin_name);
                skAppPrintErr(("Plug-in cannot add field named '%s': %s."
                               " Plug-in file: %s"),
                              *name, skStringMapStrerror(sm_err),plugin_name);
                return -1;
            }
        }
    }


    max_id = 0;

    /* create the string-map for value field identifiers */
    if (skStringMapCreate(&value_field_map)) {
        skAppPrintErr("Unable to create map for values");
        return -1;
    }

    /* add the built-in names */
    for (i = 0; i < num_builtin_values; ++i) {
        memset(&sm_entry, 0, sizeof(sk_stringmap_entry_t));
        sm_entry.name = builtin_values[i].bf_title;
        sm_entry.id = i;
        sm_entry.description = builtin_values[i].bf_description;
        sm_err = skStringMapAddEntries(value_field_map, 1, &sm_entry);
        if (sm_err) {
            skAppPrintErr("Unable to add value field named '%s': %s",
                          sm_entry.name, skStringMapStrerror(sm_err));
            return -1;
        }
        if (sm_entry.id > max_id) {
            max_id = sm_entry.id;
        }
    }

    /* add aliases for built-in fields */
    for (j = 0; builtin_value_aliases[j].ba_name; ++j) {
        for (i = 0; i < num_builtin_values; ++i) {
            if (builtin_value_aliases[j].ba_id == builtin_values[i].bf_id) {
                memset(&sm_entry, 0, sizeof(sk_stringmap_entry_t));
                sm_entry.name = builtin_value_aliases[j].ba_name;
                sm_entry.id = i;
                sm_err = skStringMapAddEntries(value_field_map, 1, &sm_entry);
                if (sm_err) {
                    skAppPrintErr("Unable to add value field named '%s': %s",
                                  sm_entry.name, skStringMapStrerror(sm_err));
                    return -1;
                }
                break;
            }
        }
        if (i == num_builtin_values) {
            skAppPrintErr("No field found with id %d",
                          builtin_value_aliases[j].ba_id);
            return -1;
        }
    }

    /* add the value fields from the plugins */
    pi_err = skPluginFieldIteratorBind(&pi_iter, SKPLUGIN_APP_STATS_VALUE, 1);
    assert(pi_err == SKPLUGIN_OK);

    while (skPluginFieldIteratorNext(&pi_iter, &pi_field)) {
        skPluginFieldName(pi_field, &field_names);
        ++max_id;

        /* Add value names to the field_map */
        for (name = field_names; *name; ++name) {
            memset(&sm_entry, 0, sizeof(sm_entry));
            sm_entry.name = *name;
            sm_entry.id = max_id;
            sm_entry.userdata = pi_field;
            skPluginFieldDescription(pi_field, &sm_entry.description);
            sm_err = skStringMapAddEntries(value_field_map, 1, &sm_entry);
            if (sm_err != SKSTRINGMAP_OK) {
                const char *plugin_name;
                skPluginFieldGetPluginName(pi_field, &plugin_name);
                skAppPrintErr(("Plug-in cannot add value named '%s': %s."
                               " Plug-in file: %s"),
                              *name, skStringMapStrerror(sm_err),plugin_name);
                return -1;
            }
        }
    }

    return 0;
}


/*
 *  status = parseKeyFields(field_string);
 *
 *    Parse the string that represents the key fields the user wishes
 *    to bin by, create and fill in the global sk_fieldlist_t
 *    'key_fields', and add columns to the rwAsciiStream.  Return 0 on
 *    success or non-zero on error.
 */
static int
parseKeyFields(
    const char         *field_string)
{
    sk_stringmap_iter_t *sm_iter = NULL;
    sk_stringmap_entry_t *sm_entry = NULL;
    sk_fieldentry_t *fl_entry;

    /* keep track of which time field we see last; uses the
     * RWREC_FIELD_* values from rwascii.h */
    rwrec_printable_fields_t final_time_field = (rwrec_printable_fields_t)0;

    /* keep track of which ICMP field(s) we see */
    int have_icmp_type_code = 0;

    /* return value; assume failure */
    int rv = -1;

    /* error message generated when parsing fields */
    char *errmsg;

    /* the field IDs for SIP, DIP, and NHIP depend on the ipv6 policy */
    sk_fieldid_t ip_fields[3] = {
        SK_FIELD_SIPv4, SK_FIELD_DIPv4, SK_FIELD_NHIPv4
    };

#if SK_ENABLE_IPV6
    if (ipv6_policy >= SK_IPV6POLICY_MIX) {
        ip_fields[0] = SK_FIELD_SIPv6;
        ip_fields[1] = SK_FIELD_DIPv6;
        ip_fields[2] = SK_FIELD_NHIPv6;
    }
#endif  /* SK_ENABLE_IPV6 */

    /* parse the --fields argument */
    if (skStringMapParse(key_field_map, field_string, SKSTRINGMAP_DUPES_ERROR,
                         &sm_iter, &errmsg))
    {
        skAppPrintErr("Invalid %s: %s",
                      appOptions[OPT_FIELDS].name, errmsg);
        goto END;
    }

    /* create the field-list */
    if (skFieldListCreate(&key_fields)) {
        skAppPrintErr("Unable to create key field list");
        goto END;
    }

    /* see which time fields and ICMP fiels are requested */
    while (skStringMapIterNext(sm_iter, &sm_entry, NULL) == SK_ITERATOR_OK) {
        switch (sm_entry->id) {
          case RWREC_FIELD_DPORT:
            dport_key = 1;
            break;
          case RWREC_FIELD_STIME:
          case RWREC_FIELD_STIME_MSEC:
            time_fields |= PARSE_KEY_STIME;
            final_time_field = (rwrec_printable_fields_t)sm_entry->id;
            break;
          case RWREC_FIELD_ELAPSED:
          case RWREC_FIELD_ELAPSED_MSEC:
            time_fields |= PARSE_KEY_ELAPSED;
            final_time_field = (rwrec_printable_fields_t)sm_entry->id;
            break;
          case RWREC_FIELD_ETIME:
          case RWREC_FIELD_ETIME_MSEC:
            time_fields |= PARSE_KEY_ETIME;
            final_time_field = (rwrec_printable_fields_t)sm_entry->id;
            break;
          case RWREC_FIELD_ICMP_TYPE:
          case RWREC_FIELD_ICMP_CODE:
            have_icmp_type_code |= 1;
            break;
          case RWREC_PRINTABLE_FIELD_COUNT:
            have_icmp_type_code |= 2;
            break;
          default:
            break;
        }
    }

    /* set 'time_fields_key' to the time fields that will be in the
     * key.  since only two of the three time fields are independent,
     * when all three are requested only the first two fields are put
     * into the key. */
    time_fields_key = time_fields;
    if (PARSE_KEY_ALL_TIMES == time_fields_key) {
        switch (final_time_field) {
          case RWREC_FIELD_STIME:
          case RWREC_FIELD_STIME_MSEC:
            time_fields_key &= ~PARSE_KEY_STIME;
            break;
          case RWREC_FIELD_ELAPSED:
          case RWREC_FIELD_ELAPSED_MSEC:
            time_fields_key &= ~PARSE_KEY_ELAPSED;
            break;
          case RWREC_FIELD_ETIME:
          case RWREC_FIELD_ETIME_MSEC:
            time_fields_key &= ~PARSE_KEY_ETIME;
            break;
          default:
            skAbortBadCase(final_time_field);
        }
    }

    /* when binning by time was requested, see if time fields make sense */
    if (time_bin_size != 0) {
        switch (time_fields) {
          case 0:
          case PARSE_KEY_ELAPSED:
            if (FILEIsATty(stderr)) {
                skAppPrintErr("Warning: Neither sTime nor eTime appear in"
                              " --%s; %s switch ignored",
                              appOptions[OPT_FIELDS].name,
                              appOptions[OPT_BIN_TIME].name);
            }
            time_bin_size = 0;
            break;
          case PARSE_KEY_ALL_TIMES:
            /* must adjust elapsed to be eTime-sTime */
            if (FILEIsATty(stderr)) {
                skAppPrintErr("Warning: Modifying duration field "
                              "to be difference of eTime and sTime");
            }
            break;
        }
    }

    /* warn when using --presorted-input and multiple time fields are
     * present or when the time field is not the final field */
    if (app_flags.presorted_input && FILEIsATty(stderr)) {
        switch (time_fields) {
          case 0:
            /* no time fields present */
            break;
          case PARSE_KEY_ELAPSED:
          case PARSE_KEY_STIME:
          case PARSE_KEY_ETIME:
            /* one field is present.  see if it is last.  note that
             * 'sm_entry' is still pointed at the final entry */
            switch (sm_entry->id) {
              case RWREC_FIELD_STIME:
              case RWREC_FIELD_STIME_MSEC:
              case RWREC_FIELD_ELAPSED:
              case RWREC_FIELD_ELAPSED_MSEC:
              case RWREC_FIELD_ETIME:
              case RWREC_FIELD_ETIME_MSEC:
                /* one field is present and it is last */
                break;
              default:
                /* one field is present but it is not last */
                skAppPrintErr(("Warning: Suggest putting '%s' last in --%s"
                               " when using --%s due to millisecond"
                               " truncation"),
                              ((PARSE_KEY_ELAPSED == time_fields)
                               ? "elapsed"
                               : ((PARSE_KEY_STIME == time_fields)
                                  ? "sTime"
                                  : "eTime")),
                              appOptions[OPT_FIELDS].name,
                              appOptions[OPT_PRESORTED_INPUT].name);
                break;
            }
            break;
          default:
            /* multiple time fields present */
            skAppPrintErr(("Warning: Using multiple time-related key"
                           " fields with\n\t--%s may lead to unexpected"
                           " results due to millisecond truncation"),
                          appOptions[OPT_PRESORTED_INPUT].name);
            break;
        }
    }

    /* handle legacy icmpTypeCode field */
    if (3 == have_icmp_type_code) {
        skAppPrintErr("Invalid %s: May not mix field %s with %s or %s",
                      appOptions[OPT_FIELDS].name,
                      skStringMapGetFirstName(
                          key_field_map, RWREC_PRINTABLE_FIELD_COUNT),
                      skStringMapGetFirstName(
                          key_field_map, RWREC_FIELD_ICMP_TYPE),
                      skStringMapGetFirstName(
                          key_field_map, RWREC_FIELD_ICMP_CODE));
        goto END;
    }

    skStringMapIterReset(sm_iter);

    /* add the key fields to the field-list and to the ascii stream. */
    while (skStringMapIterNext(sm_iter, &sm_entry, NULL) == SK_ITERATOR_OK) {
        if (sm_entry->userdata) {
            assert(sm_entry->id > RWREC_PRINTABLE_FIELD_COUNT);
            if (appAddPlugin((skplugin_field_t*)(sm_entry->userdata),
                             FIELD_TYPE_KEY))
            {
                skAppPrintErr("Cannot add key field '%s' from plugin",
                              sm_entry->name);
                goto END;
            }
            continue;
        }
        if (sm_entry->id == RWREC_PRINTABLE_FIELD_COUNT) {
            /* handle the icmpTypeCode field */
            rwrec_printable_fields_t icmp_fields[] = {
                RWREC_FIELD_ICMP_TYPE, RWREC_FIELD_ICMP_CODE
            };
            char name_buf[128];
            size_t k;

            for (k = 0; k < sizeof(icmp_fields)/sizeof(icmp_fields[0]); ++k) {
                if (rwAsciiAppendOneField(ascii_str, icmp_fields[k])
                    || !skFieldListAddKnownField(key_fields, icmp_fields[k],
                                                 NULL))
                {
                    rwAsciiGetFieldName(name_buf, sizeof(name_buf),
                                        icmp_fields[k]);
                    skAppPrintErr("Cannot add key field '%s' to stream",
                                  name_buf);
                    goto END;
                }
            }
            continue;
        }
        assert(sm_entry->id < RWREC_PRINTABLE_FIELD_COUNT);
        if (rwAsciiAppendOneField(ascii_str, sm_entry->id)) {
            skAppPrintErr("Cannot add key field '%s' to stream",
                          sm_entry->name);
            goto END;
        }
        if (PARSE_KEY_ALL_TIMES == time_fields
            && (rwrec_printable_fields_t)sm_entry->id == final_time_field)
        {
            /* when all time fields were requested, do not include the
             * final one that was seen as part of the key */
            continue;
        }
        switch ((rwrec_printable_fields_t)sm_entry->id) {
          case RWREC_FIELD_SIP:
            fl_entry = skFieldListAddKnownField(key_fields, ip_fields[0],NULL);
            break;
          case RWREC_FIELD_DIP:
            fl_entry = skFieldListAddKnownField(key_fields, ip_fields[1],NULL);
            break;
          case RWREC_FIELD_NHIP:
            fl_entry = skFieldListAddKnownField(key_fields, ip_fields[2],NULL);
            break;
          default:
            fl_entry = skFieldListAddKnownField(key_fields, sm_entry->id,NULL);
            break;
        }
        if (NULL == fl_entry) {
            skAppPrintErr("Cannot add key field '%s' to field list",
                          sm_entry->name);
            goto END;
        }
    }

    /* successful */
    rv = 0;

  END:
    if (rv != 0) {
        /* something went wrong.  clean up */
        if (key_fields) {
            skFieldListDestroy(&key_fields);
            key_fields = NULL;
        }
    }
    /* do standard clean-up */
    if (sm_iter != NULL) {
        skStringMapIterDestroy(sm_iter);
    }

    return rv;
}


/*
 *  ok = parseValueFields(field_string);
 *
 *    Parse the string that represents the aggregate value and
 *    distinct fields the user wishes to compute, create and fill in
 *    the global sk_fieldlist_t 'value_fields' and 'distinct_fields',
 *    and add columns to the rwAsciiStream.  Return 0 on success or
 *    non-zero on error.
 *
 *    Returns 0 on success, or non-zero on error.
 */
static int
parseValueFields(
    const char         *value_string)
{
    sk_stringmap_iter_t *sm_iter = NULL;
    sk_stringmap_entry_t *sm_entry;
    sk_stringmap_status_t sm_err;
    sk_stringmap_id_t sm_entry_id;
    const char *sm_attr;
    field_type_t field_type;

    /* return value; assume failure */
    int rv = -1;

    /* error message generated when parsing fields */
    char *errmsg;

    builtin_field_t *bf;
    sk_fieldentry_t *fl_entry;

#if SK_ENABLE_IPV6
    if (ipv6_policy >= SK_IPV6POLICY_MIX) {
        size_t i;

        /* change the field id of the distinct fields */
        for (i = 0, bf = builtin_values; i < num_builtin_values; ++i, ++bf) {
            switch (bf->bf_id) {
              case SK_FIELD_SIPv4:
                bf->bf_id = SK_FIELD_SIPv6;
                break;
              case SK_FIELD_DIPv4:
                bf->bf_id = SK_FIELD_DIPv6;
                break;
              default:
                break;
            }
        }
    }
#endif  /* SK_ENABLE_IPV6 */

    /* parse the --values field list */
    if (skStringMapParseWithAttributes(value_field_map, value_string,
                                       SKSTRINGMAP_DUPES_KEEP, &sm_iter,
                                       &errmsg))
    {
        skAppPrintErr("Invalid %s: %s",
                      appOptions[OPT_VALUES].name, errmsg);
        goto END;
    }

    /* create the field-lists */
    if (skFieldListCreate(&value_fields)) {
        skAppPrintErr("Unable to create value field list");
        goto END;
    }
    if (skFieldListCreate(&distinct_fields)) {
        skAppPrintErr("Unable to create distinct field list");
        goto END;
    }

    /* loop over the selected values */
    while (skStringMapIterNext(sm_iter, &sm_entry, &sm_attr)==SK_ITERATOR_OK) {
        /* initialize to a nonsense value */
        field_type = FIELD_TYPE_KEY;
        if (sm_entry->userdata) {
            /* this is a values field that comes from a plug-in */
            field_type = FIELD_TYPE_VALUE;
            if (sm_attr[0]) {
                skAppPrintErr("Invalid %s: Extra text after field name ':%s'",
                              appOptions[OPT_VALUES].name, sm_attr);
                goto END;
            }
            if (isFieldDuplicate(value_fields, SK_FIELD_CALLER,
                                 sm_entry->userdata))
            {
                skAppPrintErr("Invalid %s: Duplicate name '%s'",
                              appOptions[OPT_VALUES].name, sm_entry->name);
                goto END;
            }
            if (appAddPlugin((skplugin_field_t*)(sm_entry->userdata),
                             field_type))
            {
                skAppPrintErr("Cannot add value field '%s' from plugin",
                              sm_entry->name);
                goto END;
            }
            continue;
        }
        /* else, field is built-in */
        assert(sm_entry->id < num_builtin_values);
        bf = &builtin_values[sm_entry->id];
        if (0 == bf->bf_is_distinct) {
            /* this is a built-in values field; must have no attribute */
            field_type = FIELD_TYPE_VALUE;
            if (sm_attr[0]) {
                skAppPrintErr("Invalid %s: Unrecognized field '%s:%s'",
                              appOptions[OPT_VALUES].name,
                              bf->bf_title, sm_attr);
                goto END;
            }
            if (isFieldDuplicate(value_fields, bf->bf_id, NULL)) {
                skAppPrintErr("Invalid %s: Duplicate name '%s'",
                              appOptions[OPT_VALUES].name, bf->bf_title);
                goto END;
            }
            fl_entry = skFieldListAddKnownField(value_fields, bf->bf_id, bf);
            if (NULL == fl_entry) {
                skAppPrintErr("Cannot add value field '%s' to field list",
                              sm_entry->name);
                goto END;
            }
            if (rwAsciiAppendCallbackFieldExtra(ascii_str,
                                                &builtin_value_get_title,
                                                &value_to_ascii,
                                                fl_entry, bf->bf_text_len))
            {
                skAppPrintErr("Cannot add value field '%s' to stream",
                              sm_entry->name);
                goto END;
            }
        } else if (SK_FIELD_CALLER != bf->bf_id) {
            /* one of the old sip-distinct,dip-distinct fields; must
             * have no attribute */
            field_type = FIELD_TYPE_DISTINCT;
            if (sm_attr[0]) {
                skAppPrintErr("Invalid %s: Unrecognized field '%s:%s'",
                              appOptions[OPT_VALUES].name,
                              bf->bf_title, sm_attr);
                goto END;
            }
            /* is this a duplicate field? */
            if (isFieldDuplicate(distinct_fields, bf->bf_id, NULL)) {
                skAppPrintErr("Invalid %s: Duplicate name '%s'",
                              appOptions[OPT_VALUES].name, bf->bf_title);
                goto END;
            }
            fl_entry = skFieldListAddKnownField(distinct_fields, bf->bf_id,bf);
            if (NULL == fl_entry) {
                skAppPrintErr("Cannot add distinct field '%s' to field list",
                              sm_entry->name);
                goto END;
            }
            if (rwAsciiAppendCallbackFieldExtra(ascii_str,
                                                &builtin_distinct_get_title,
                                                &distinct_to_ascii,
                                                fl_entry, bf->bf_text_len))
            {
                skAppPrintErr("Cannot add distinct field '%s' to stream",
                              sm_entry->name);
                goto END;
            }
        } else {
            /* got a distinct:KEY field */
            field_type = FIELD_TYPE_DISTINCT;
            if (!sm_attr[0]) {
                skAppPrintErr(("Invalid %s:"
                               " The distinct value requires a field"),
                              appOptions[OPT_VALUES].name);
                goto END;
            }
            /* need to parse KEY as a key field */
            sm_err = skStringMapGetByName(key_field_map, sm_attr, &sm_entry);
            if (sm_err) {
                if (strchr(sm_attr, ',')) {
                    skAppPrintErr(("Invalid %s:"
                                   " May only distinct over a single field"),
                                  appOptions[OPT_VALUES].name);
                } else {
                    skAppPrintErr("Invalid %s: Bad distinct field '%s': %s",
                                  appOptions[OPT_VALUES].name, sm_attr,
                                  skStringMapStrerror(sm_err));
                }
                goto END;
            }
            if (sm_entry->userdata) {
                /* distinct:KEY where KEY is from a plug-in */
                if (isFieldDuplicate(distinct_fields, SK_FIELD_CALLER,
                                     sm_entry->userdata))
                {
                    skAppPrintErr("Invalid %s: Duplicate distinct '%s'",
                                  appOptions[OPT_VALUES].name, sm_entry->name);
                    goto END;
                }
                if (appAddPlugin((skplugin_field_t*)(sm_entry->userdata),
                                 field_type))
                {
                    skAppPrintErr("Cannot add distinct field '%s' from plugin",
                                  sm_entry->name);
                    goto END;
                }
                continue;
            }
            if (isFieldDuplicate(distinct_fields, (sk_fieldid_t)sm_entry->id,
                                 NULL))
            {
                skAppPrintErr("Invalid %s: Duplicate distinct '%s'",
                              appOptions[OPT_VALUES].name, sm_entry->name);
                goto END;
            }
            if (sm_entry->id == RWREC_PRINTABLE_FIELD_COUNT) {
                skAppPrintErr("Invalid %s: May not count distinct '%s' entries",
                              appOptions[OPT_VALUES].name, sm_entry->name);
                goto END;
            }
            sm_entry_id = sm_entry->id;
#if SK_ENABLE_IPV6
            if (ipv6_policy >= SK_IPV6POLICY_MIX) {
                /* make certain field can hold an IPv6 address */
                switch (sm_entry_id) {
                  case SK_FIELD_SIPv4:
                    sm_entry_id = SK_FIELD_SIPv6;
                    break;
                  case SK_FIELD_DIPv4:
                    sm_entry_id = SK_FIELD_DIPv6;
                    break;
                  case SK_FIELD_NHIPv4:
                    sm_entry_id = SK_FIELD_NHIPv6;
                    break;
                }
            }
#endif  /* #if SK_ENABLE_IPV6 */
            fl_entry = skFieldListAddKnownField(distinct_fields,
                                                sm_entry_id, NULL);
            if (NULL == fl_entry) {
                skAppPrintErr("Cannot add distinct field '%s' to field list",
                              sm_entry->name);
                goto END;
            }
            if (rwAsciiAppendCallbackFieldExtra(ascii_str,
                                                &builtin_distinct_get_title,
                                                &distinct_to_ascii,
                                                fl_entry, bf->bf_text_len))
            {
                skAppPrintErr("Cannot add distinct field '%s' to stream",
                              sm_entry->name);
                goto END;
            }
        }

        assert(FIELD_TYPE_KEY != field_type);
        /* first value determines order of output rows */
        if (NULL == limit.fl_entry) {
            limit.fl_entry = fl_entry;
            limit.fl_id = (sk_fieldid_t)skFieldListEntryGetId(fl_entry);
            limit.bf_value = bf;
            limit.distinct = (FIELD_TYPE_DISTINCT == field_type);
            if (limit.distinct) {
                builtin_distinct_get_title(limit.title, sizeof(limit.title),
                                           fl_entry);
            } else {
                builtin_value_get_title(limit.title, sizeof(limit.title),
                                        fl_entry);
            }
        }
    }

    rv = 0;

  END:
    /* do standard clean-up */
    if (sm_iter) {
        skStringMapIterDestroy(sm_iter);
    }
    if (rv != 0) {
        /* something went wrong. do additional clean-up */
        if (value_fields) {
            skFieldListDestroy(&value_fields);
            value_fields = NULL;
        }
        if (distinct_fields) {
            skFieldListDestroy(&distinct_fields);
            distinct_fields = NULL;
        }
    }

    return rv;
}


/*
 *  status = appAddPlugin(plugin_field, field_type);
 *
 *    Given a key, an aggregate value, or distinct(key) field defined
 *    in a plug-in, activate that field and get the information from
 *    the field that the application requires.  field_type indicates
 *    whether the field represents a key, an aggregate value, or a
 *    distinct field.
 *
 *    The function adds the field to the approprirate sk_fieldlist_t
 *    ('key_fields', 'value_fields', 'distinct_fields') and to the
 *    rwAsciiStream.
 */
static int
appAddPlugin(
    skplugin_field_t   *pi_field,
    field_type_t        field_type)
{
    uint8_t bin_buf[HASHLIB_MAX_VALUE_WIDTH];
    sk_fieldlist_entrydata_t regdata;
    sk_fieldentry_t *fl_entry;
    size_t text_width;
    skplugin_err_t pi_err;

    /* if this is the first value/distinct field, ensure that the limit-type
     * is valid. */
    if ((NULL == limit.fl_entry)
        && (FIELD_TYPE_VALUE == field_type)
        && (RWSTATS_PERCENTAGE == limit.type
            || RWSTATS_THRESHOLD == limit.type))
    {
        skAppPrintErr(("Only the --%s limit is supported when the"
                       " primary values field is from a plug-in"),
                      appOptions[OPT_COUNT].name);
        return -1;
    }

    /* set the regdata for the sk_fieldlist_t */
    memset(&regdata, 0, sizeof(regdata));
    regdata.bin_compare = plugin_bin_compare;
    regdata.add_rec_to_bin = plugin_add_rec_to_bin;
    regdata.bin_merge = plugin_bin_merge;
    /* regdata.bin_output; */

    /* activate the field (so cleanup knows about it) */
    pi_err = skPluginFieldActivate(pi_field);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }

    /* initialize this field */
    pi_err = skPluginFieldRunInitialize(pi_field);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }

    /* get the required textual width of the column */
    pi_err = skPluginFieldGetLenText(pi_field, &text_width);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    if (0 == text_width) {
        const char *title;
        skPluginFieldTitle(pi_field, &title);
        skAppPrintErr("Plug-in field '%s' has a textual width of 0",
                      title);
        return -1;
    }

    /* get the bin width for this field */
    pi_err = skPluginFieldGetLenBin(pi_field, &regdata.bin_octets);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    if (0 == regdata.bin_octets) {
        const char *title;
        skPluginFieldTitle(pi_field, &title);
        skAppPrintErr("Plug-in field '%s' has a binary width of 0",
                      title);
        return -1;
    }
    if (regdata.bin_octets > HASHLIB_MAX_VALUE_WIDTH) {
        return -1;
    }

    memset(bin_buf, 0, sizeof(bin_buf));
    pi_err = skPluginFieldGetInitialValue(pi_field, bin_buf);
    if (pi_err != SKPLUGIN_OK) {
        return -1;
    }
    regdata.initial_value = bin_buf;

    switch (field_type) {
      case FIELD_TYPE_KEY:
        regdata.rec_to_bin = plugin_rec_to_bin;
        fl_entry = skFieldListAddField(key_fields, &regdata, (void*)pi_field);
        break;
      case FIELD_TYPE_VALUE:
        fl_entry = skFieldListAddField(value_fields, &regdata,(void*)pi_field);
        break;
      case FIELD_TYPE_DISTINCT:
        regdata.rec_to_bin = plugin_rec_to_bin;
        fl_entry = skFieldListAddField(distinct_fields, &regdata,
                                       (void*)pi_field);
        break;
      default:
        skAbortBadCase(field_type);
    }
    if (NULL == fl_entry) {
        skAppPrintErr("Unable to add field to field list");
        return -1;
    }

    if (FIELD_TYPE_KEY != field_type && NULL == limit.fl_entry) {
        /* first aggregate value/distinct field */
        limit.pi_field = pi_field;
        limit.fl_entry = fl_entry;
        limit.fl_id = (sk_fieldid_t)skFieldListEntryGetId(fl_entry);
        limit.distinct = (FIELD_TYPE_DISTINCT == field_type);

        if (limit.distinct) {
            plugin_distinct_get_title(limit.title, sizeof(limit.title),
                                      fl_entry);
        } else {
            plugin_get_title(limit.title, sizeof(limit.title), fl_entry);
        }
    }

    switch (field_type) {
      case FIELD_TYPE_KEY:
        return rwAsciiAppendCallbackFieldExtra(ascii_str, &plugin_get_title,
                                               &plugin_key_to_ascii, fl_entry,
                                               text_width);
      case FIELD_TYPE_VALUE:
        return rwAsciiAppendCallbackFieldExtra(ascii_str, &plugin_get_title,
                                               &value_to_ascii, fl_entry,
                                               text_width);
      case FIELD_TYPE_DISTINCT:
        return rwAsciiAppendCallbackFieldExtra(ascii_str,
                                               &plugin_distinct_get_title,
                                               &distinct_to_ascii,
                                               fl_entry, text_width);
      default:
        skAbortBadCase(field_type);
    }

    return -1;                  /* NOTREACHED */
}


/*
 *  is_duplicate = isFieldDuplicate(flist, fid, fcontext);
 *
 *    Return 1 if the field-id 'fid' appears in the field-list
 *    'flist'.  If 'fid' is SK_FIELD_CALLER, return 1 when a field in
 *    'flist' has the id SK_FIELD_CALLER and its context object points
 *    to 'fcontext'.  Return 0 otherwise.
 *
 *    In this function, IPv4 and IPv6 fields are considered
 *    equivalent; that is, you cannot have both SK_FIELD_SIPv4 and
 *    SK_FIELD_SIPv6, and multiple SK_FIELD_CALLER fields are allowed.
 */
static int
isFieldDuplicate(
    const sk_fieldlist_t   *flist,
    sk_fieldid_t            fid,
    const void             *fcontext)
{
    sk_fieldlist_iterator_t fl_iter;
    sk_fieldentry_t *fl_entry;

    skFieldListIteratorBind(flist, &fl_iter);
    switch (fid) {
      case SK_FIELD_SIPv4:
      case SK_FIELD_SIPv6:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            switch (skFieldListEntryGetId(fl_entry)) {
              case SK_FIELD_SIPv4:
              case SK_FIELD_SIPv6:
                return 1;
              default:
                break;
            }
        }
        break;

      case SK_FIELD_DIPv4:
      case SK_FIELD_DIPv6:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            switch (skFieldListEntryGetId(fl_entry)) {
              case SK_FIELD_DIPv4:
              case SK_FIELD_DIPv6:
                return 1;
              default:
                break;
            }
        }
        break;

      case SK_FIELD_NHIPv4:
      case SK_FIELD_NHIPv6:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            switch (skFieldListEntryGetId(fl_entry)) {
              case SK_FIELD_NHIPv4:
              case SK_FIELD_NHIPv6:
                return 1;
              default:
                break;
            }
        }
        break;

      case SK_FIELD_CALLER:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            if ((skFieldListEntryGetId(fl_entry) == (uint32_t)fid)
                && (skFieldListEntryGetContext(fl_entry) == fcontext))
            {
                return 1;
            }
        }
        break;

      default:
        while ((fl_entry = skFieldListIteratorNext(&fl_iter)) != NULL) {
            if (skFieldListEntryGetId(fl_entry) == (uint32_t)fid) {
                return 1;
            }
        }
        break;
    }
    return 0;
}


static int
prepareFileForRead(
    skstream_t         *stream)
{
    if (app_flags.print_filenames) {
        fprintf(PRINT_FILENAMES_FH, "%s\n", skStreamGetPathname(stream));
    }
    if (copy_input) {
        skStreamSetCopyInput(stream, copy_input);
    }
    skStreamSetIPv6Policy(stream, ipv6_policy);

    return 0;
}


/*
 *  status = readRecord(stream, rwrec);
 *
 *    Fill 'rwrec' with a SiLK Flow record read from 'stream'.  Modify
 *    the times on the record if the user has requested time binning.
 *    Modify the IPs if the user has specified CIDR blocks.
 *
 *    Return the status of reading the record.
 */
int
readRecord(
    skstream_t         *stream,
    rwRec              *rwrec)
{
    sktime_t sTime;
    sktime_t sTime_mod;
    uint32_t elapsed;
    int rv;

    rv = skStreamReadRecord(stream, rwrec);
    if (SKSTREAM_OK == rv) {
        ++record_count;
        switch (limit.fl_id) {
          case SK_FIELD_RECORDS:
            ++value_total;
            break;
          case SK_FIELD_SUM_BYTES:
            value_total += rwRecGetBytes(rwrec);
            break;
          case SK_FIELD_SUM_PACKETS:
            value_total += rwRecGetPkts(rwrec);
            break;
          default:
            break;
        }

        if (cidr_sip) {
            rwRecSetSIPv4(rwrec, rwRecGetSIPv4(rwrec) & cidr_sip);
        }
        if (cidr_dip) {
            rwRecSetDIPv4(rwrec, rwRecGetDIPv4(rwrec) & cidr_dip);
        }
        if (time_bin_size > 1) {
            switch (time_fields) {
              case PARSE_KEY_STIME:
              case (PARSE_KEY_STIME | PARSE_KEY_ELAPSED):
                /* adjust start time */
                sTime = rwRecGetStartTime(rwrec);
                sTime_mod = sTime % time_bin_size;
                rwRecSetStartTime(rwrec, (sTime - sTime_mod));
                break;
              case PARSE_KEY_ALL_TIMES:
              case (PARSE_KEY_STIME | PARSE_KEY_ETIME):
                /* adjust sTime and elapsed/duration */
                sTime = rwRecGetStartTime(rwrec);
                sTime_mod = sTime % time_bin_size;
                rwRecSetStartTime(rwrec, (sTime - sTime_mod));
                /*
                 * the following sets elapsed to:
                 * ((eTime - (eTime % bin_size)) - (sTime - (sTime % bin_size)))
                 */
                elapsed = rwRecGetElapsed(rwrec);
                elapsed = (elapsed + sTime_mod
                           - ((sTime + elapsed) % time_bin_size));
                rwRecSetElapsed(rwrec, elapsed);
                break;
              case PARSE_KEY_ETIME:
              case (PARSE_KEY_ETIME | PARSE_KEY_ELAPSED):
                /* want to set eTime to (eTime - (eTime % bin_size)), but
                 * eTime is computed as (sTime + elapsed) */
                sTime = rwRecGetStartTime(rwrec);
                rwRecSetStartTime(rwrec,
                                  (sTime - ((sTime + rwRecGetElapsed(rwrec))
                                            % time_bin_size)));
                break;
              case 0:
              case PARSE_KEY_ELAPSED:
              default:
                skAbortBadCase(time_fields);
            }
        }
    }

    return rv;
}


/*
 *  int = appNextInput(&stream);
 *
 *    Fill 'stream' with the next input file to read.  Return 0 if
 *    'stream' was successfully opened, 1 if there are no more input
 *    files, or -1 if an error was encountered.
 */
int
appNextInput(
    skstream_t        **stream)
{
    char *path = NULL;
    int rv;

    rv = skOptionsCtxNextArgument(optctx, &path);
    if (0 == rv) {
        rv = skStreamOpenSilkFlow(stream, path, SK_IO_READ);
        if (rv) {
            skStreamPrintLastErr(*stream, rv, &skAppPrintErr);
            skStreamDestroy(stream);
            return -1;
        }

        (void)prepareFileForRead(*stream);
    }

    return rv;
}


/*
 *  setOutputHandle();
 *
 *    If using the pager, enable it and bind it to the Ascii stream.
 */
void
setOutputHandle(
    void)
{
    int rv;

    /* only invoke the pager when the user has not specified the
     * output-path, even if output-path is stdout */
    if (NULL == output.of_name) {
        /* invoke the pager */
        rv = skFileptrOpenPager(&output, pager);
        if (rv && rv != SK_FILEPTR_PAGER_IGNORED) {
            skAppPrintErr("Unable to invoke pager");
        }
    }

    /* bind the Ascii Stream to the output */
    rwAsciiSetOutputHandle(ascii_str, output.of_fp);
}


/*
** Local Variables:
** mode:c
** indent-tabs-mode:nil
** c-basic-offset:4
** End:
*/
