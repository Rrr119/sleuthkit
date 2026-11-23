/*
 ** tsk_timeline
 ** The Sleuth Kit
 **
 ** Export a forensic timeline from a disk image in the mactime body
 ** file format (compatible with mactime, tsk_gettimes and fls -m),
 ** with optional filtering by timestamp type.
 */

#include "tsk/tsk_tools_i.h"
#include <locale.h>
#include <time.h>


static TSK_TCHAR *progname;

static void
usage()
{
    TFPRINTF(stderr,
        _TSK_T
        ("usage: %" PRIttocTSK " [-vVm] [-t types] [-i imgtype] [-b dev_sector_size] [-z zone] [-s seconds] [-k password] image [image]\n"),
        progname);
    tsk_fprintf(stderr,
        "\t-t types: Timestamp types to include in the timeline: m (modified), a (accessed), c (changed), b (created). Default is all (macb)\n");
    tsk_fprintf(stderr,
        "\t-i imgtype: The format of the image file (use '-i list' for supported types)\n");
    tsk_fprintf(stderr,
        "\t-b dev_sector_size: The size (in bytes) of the device sectors\n");
    tsk_fprintf(stderr, "\t-m: Calculate MD5 hash in output (slow)\n");
    tsk_fprintf(stderr, "\t-v: verbose output to stderr\n");
    tsk_fprintf(stderr, "\t-V: Print version\n");
    tsk_fprintf(stderr,
                "\t-z: Time zone of original machine (i.e. EST5EDT or GMT)\n");
    tsk_fprintf(stderr,
                "\t-s seconds: Time skew of original machine (in seconds)\n");
    tsk_fprintf(stderr, "\t-k password: Decryption password for encrypted volumes\n");

    exit(1);
}


class TskTimeline:public TskAuto {
public:
    TskTimeline(int32_t, bool, uint8_t);
    virtual TSK_RETVAL_ENUM processFile(TSK_FS_FILE * fs_file, const char *path);
    virtual TSK_FILTER_ENUM filterVol(const TSK_VS_PART_INFO * vs_part);
    virtual TSK_FILTER_ENUM filterPool(const TSK_POOL_INFO * pool_info);
    virtual TSK_FILTER_ENUM filterPoolVol(const TSK_POOL_VOLUME_INFO * pool_vol);
    virtual TSK_FILTER_ENUM filterFs(TSK_FS_INFO * fs_info);
    virtual uint8_t handleError();

private:
    int m_curVolAddr;
    int m_curPoolVol;
    int32_t m_secSkew;
    bool m_computeHash;
    uint8_t m_timeMask;
};


TskTimeline::TskTimeline(int32_t a_secSkew, bool a_computeHash,
    uint8_t a_timeMask)
{
    m_curVolAddr = -1;
    m_curPoolVol = -1;
    m_secSkew = a_secSkew;
    m_computeHash = a_computeHash;
    m_timeMask = a_timeMask;
}

// Print errors as they are encountered
uint8_t TskTimeline::handleError()
{
    fprintf(stderr, "%s", tsk_error_get());
    return 0;
}


TSK_RETVAL_ENUM TskTimeline::processFile(TSK_FS_FILE * /*fs_file*/, const char * /*path*/)
{
    return TSK_OK;
}


TSK_FILTER_ENUM
TskTimeline::filterFs(TSK_FS_INFO * fs_info)
{
    TSK_TCHAR volName[65];
    if (m_curVolAddr > -1) {
        TSNPRINTF(volName, 32, _TSK_T("vol%d/"), m_curVolAddr);
    }
    else {
        volName[0] = '\0';
    }

    TSK_TCHAR poolVolName[33];
    if (m_curPoolVol > -1) {
        TSNPRINTF(poolVolName, 32, _TSK_T("poolVol%d/"), m_curPoolVol);
        TSTRNCAT(volName, poolVolName, 64 - TSTRLEN(volName));
    }

    uint32_t fls_flags = TSK_FS_FLS_MAC | TSK_FS_FLS_DIR | TSK_FS_FLS_FILE | TSK_FS_FLS_FULL;
    if (m_computeHash) {
        fls_flags |= TSK_FS_FLS_HASH;
    }
    if (m_timeMask & TSK_FS_MAC_TYPE_M)
        fls_flags |= TSK_FS_FLS_MAC_M;
    if (m_timeMask & TSK_FS_MAC_TYPE_A)
        fls_flags |= TSK_FS_FLS_MAC_A;
    if (m_timeMask & TSK_FS_MAC_TYPE_C)
        fls_flags |= TSK_FS_FLS_MAC_C;
    if (m_timeMask & TSK_FS_MAC_TYPE_B)
        fls_flags |= TSK_FS_FLS_MAC_B;

    if (tsk_fs_fls(fs_info, (TSK_FS_FLS_FLAG_ENUM)fls_flags,
        fs_info->root_inum, (TSK_FS_DIR_WALK_FLAG_ENUM)(TSK_FS_DIR_WALK_FLAG_ALLOC | TSK_FS_DIR_WALK_FLAG_UNALLOC | TSK_FS_DIR_WALK_FLAG_RECURSE), volName, m_secSkew)) {
    }
    return TSK_FILTER_SKIP;
}


TSK_FILTER_ENUM
TskTimeline::filterVol(const TSK_VS_PART_INFO * vs_part)
{
    m_curVolAddr = vs_part->addr;
    m_curPoolVol = -1;
    return TSK_FILTER_CONT;
}

TSK_FILTER_ENUM
TskTimeline::filterPool(const TSK_POOL_INFO * pool_info)
{
    // There's nothing to do, but we need to override this to allow the pool
    // to be processed.
    return TSK_FILTER_CONT;
}


TSK_FILTER_ENUM
TskTimeline::filterPoolVol(const TSK_POOL_VOLUME_INFO * pool_vol)
{
    m_curPoolVol = pool_vol->index;
    return TSK_FILTER_CONT;
}


int
main(int argc, char **argv1)
{
    TSK_IMG_TYPE_ENUM imgtype = TSK_IMG_TYPE_DETECT;
    int ch;
    TSK_TCHAR **argv;
    unsigned int ssize = 0;
    TSK_TCHAR *cp;
    int32_t sec_skew = 0;
    bool do_hash = false;
    uint8_t time_mask = TSK_FS_MAC_TYPE_ALL;
    const char* password = "";

#ifdef TSK_WIN32
    // On Windows, get the wide arguments (mingw doesn't support wmain)
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        fprintf(stderr, "Error getting wide arguments\n");
        exit(1);
    }
#else
    argv = (TSK_TCHAR **) argv1;
#endif

    progname = argv[0];
    setlocale(LC_ALL, "");

    while ((ch = GETOPT(argc, argv, _TSK_T("b:i:k:s:mt:vVz:"))) > 0) {
        switch (ch) {
        case _TSK_T('?'):
        default:
            TFPRINTF(stderr, _TSK_T("Invalid argument: %" PRIttocTSK "\n"),
                argv[OPTIND]);
            usage();
            break;

        case _TSK_T('b'):
            ssize = (unsigned int) TSTRTOUL(OPTARG, &cp, 0);
            if (*cp || *cp == *OPTARG || ssize < 1) {
                TFPRINTF(stderr,
                    _TSK_T
                    ("invalid argument: sector size must be positive: %" PRIttocTSK "\n"),
                    OPTARG);
                usage();
            }
            break;

        case _TSK_T('i'):
            if (TSTRCMP(OPTARG, _TSK_T("list")) == 0) {
                tsk_img_type_print(stderr);
                exit(1);
            }
            imgtype = tsk_img_type_toid(OPTARG);
            if (imgtype == TSK_IMG_TYPE_UNSUPP) {
                TFPRINTF(stderr, _TSK_T("Unsupported image type: %" PRIttocTSK "\n"),
                    OPTARG);
                usage();
            }
            break;

        case _TSK_T('s'):
            sec_skew = TATOI(OPTARG);
            break;

        case _TSK_T('m'):
            do_hash = true;
            break;

        case _TSK_T('t'):
            {
                const TSK_TCHAR *tp;
                time_mask = 0;
                for (tp = OPTARG; *tp; tp++) {
                    switch (*tp) {
                    case _TSK_T('m'):
                        time_mask |= TSK_FS_MAC_TYPE_M;
                        break;
                    case _TSK_T('a'):
                        time_mask |= TSK_FS_MAC_TYPE_A;
                        break;
                    case _TSK_T('c'):
                        time_mask |= TSK_FS_MAC_TYPE_C;
                        break;
                    case _TSK_T('b'):
                        time_mask |= TSK_FS_MAC_TYPE_B;
                        break;
                    default:
                        tsk_fprintf(stderr,
                            "Invalid time type for -t (use a combination of m, a, c, b)\n");
                        usage();
                    }
                }
            }
            break;

        case _TSK_T('k'):
            password = argv1[OPTIND - 1];
            break;

        case _TSK_T('v'):
            tsk_verbose++;
            break;

        case _TSK_T('V'):
            tsk_version_print(stdout);
            exit(0);

        case 'z':
            {
                TSK_TCHAR envstr[32];
                TSNPRINTF(envstr, 32, _TSK_T("TZ=%s"), OPTARG);
                if (0 != TPUTENV(envstr)) {
                    tsk_fprintf(stderr, "error setting environment");
                    exit(1);
                }

                /* we should be checking this somehow */
                TZSET();
            }
            break;

        }
    }

    /* We need at least one more argument */
    if (OPTIND >= argc) {
        tsk_fprintf(stderr,
            "Missing image name\n");
        usage();
    }

    TskTimeline tskTimeline(sec_skew, do_hash, time_mask);
    tskTimeline.setFileSystemPassword(password);
    if (tskTimeline.openImage(argc - OPTIND, &argv[OPTIND], imgtype,
            ssize)) {
        tsk_error_print(stderr);
        exit(1);
    }

    if (tskTimeline.findFilesInImg()) {
        // we already logged the errors
        exit(1);
    }

    exit(0);
}
