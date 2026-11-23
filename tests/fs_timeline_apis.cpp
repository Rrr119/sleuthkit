/*
 * fs_timeline_apis.cpp
 *
 * Test for the timeline (mactime body file) export and timestamp-type
 * filtering APIs.  Pushes 1,000,000 entries through
 * tsk_fs_name_print_mac_md5_mask() and verifies every generated body
 * file line against an independently computed expected line.
 */

#include "tsk/tsk_tools_i.h"
#include "tsk/fs/tsk_fs_i.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>

#define NUM_ENTRIES 1000000

static uint8_t g_masks[] = {
    TSK_FS_MAC_TYPE_ALL,
    TSK_FS_MAC_TYPE_M,
    TSK_FS_MAC_TYPE_A,
    TSK_FS_MAC_TYPE_C,
    TSK_FS_MAC_TYPE_B,
    TSK_FS_MAC_TYPE_M | TSK_FS_MAC_TYPE_B,
    TSK_FS_MAC_TYPE_A | TSK_FS_MAC_TYPE_C,
    TSK_FS_MAC_TYPE_M | TSK_FS_MAC_TYPE_A | TSK_FS_MAC_TYPE_C
};
#define NUM_MASKS (sizeof(g_masks) / sizeof(g_masks[0]))

/* Format one timestamp field the same way the body file format
 * specifies: 0 when the time is 0 or filtered out, otherwise the
 * skew-adjusted value. */
static void
append_time(std::string & a_line, time_t a_time, int32_t a_skew,
    bool a_include, bool a_sep)
{
    char buf[32];
    uint32_t val = (a_include) ? (uint32_t) a_time : 0;
    if (val)
        val = val - (uint32_t) a_skew;
    snprintf(buf, sizeof(buf), "%u", val);
    a_line += buf;
    if (a_sep)
        a_line += "|";
}

/* Independently compute the body file line that the library is
 * expected to produce for the given input. */
static std::string
expected_line(const TSK_FS_FILE * a_file, const char *a_path,
    const TSK_FS_ATTR * a_attr, const char *a_prefix, int32_t a_skew,
    uint8_t a_mask)
{
    char buf[128];
    std::string line = "0|";

    line += a_prefix;
    if (a_path)
        line += a_path;
    line += a_file->name->name;

    if ((a_attr) && (a_attr->type == TSK_FS_ATTR_TYPE_NTFS_FNAME))
        line += " ($FILE_NAME)";

    if (a_file->name->flags & TSK_FS_NAME_FLAG_UNALLOC)
        line +=
            ((a_file->meta)
            && (a_file->meta->flags & TSK_FS_META_FLAG_ALLOC)) ?
            " (deleted-realloc)" : " (deleted)";

    snprintf(buf, sizeof(buf), "|%" PRIuINUM, a_file->name->meta_addr);
    line += buf;
    if (a_attr) {
        snprintf(buf, sizeof(buf), "-%" PRIu32 "-%" PRIu16,
            (uint32_t) a_attr->type, (uint16_t) a_attr->id);
        line += buf;
    }

    line += "|";
    line += tsk_fs_name_type_str[a_file->name->type];
    line += "/";

    if (!a_file->meta) {
        line += "----------|0|0|0|";
    }
    else {
        char ls[12];
        tsk_fs_meta_make_ls(a_file->meta, ls, sizeof(ls));
        line += ls;
        line += "|";
        snprintf(buf, sizeof(buf), "%" PRIuUID "|%" PRIuGID "|",
            a_file->meta->uid, a_file->meta->gid);
        line += buf;
        snprintf(buf, sizeof(buf), "%" PRIdOFF "|",
            (a_attr) ? a_attr->size : a_file->meta->size);
        line += buf;
    }

    if (!a_file->meta) {
        line += "0|0|0|0";
    }
    else if ((a_attr) && (a_attr->type == TSK_FS_ATTR_TYPE_NTFS_FNAME)) {
        append_time(line, a_file->meta->time2.ntfs.fn_atime, a_skew,
            (a_mask & TSK_FS_MAC_TYPE_A) != 0, true);
        append_time(line, a_file->meta->time2.ntfs.fn_mtime, a_skew,
            (a_mask & TSK_FS_MAC_TYPE_M) != 0, true);
        append_time(line, a_file->meta->time2.ntfs.fn_ctime, a_skew,
            (a_mask & TSK_FS_MAC_TYPE_C) != 0, true);
        append_time(line, a_file->meta->time2.ntfs.fn_crtime, a_skew,
            (a_mask & TSK_FS_MAC_TYPE_B) != 0, false);
    }
    else {
        append_time(line, a_file->meta->atime, a_skew,
            (a_mask & TSK_FS_MAC_TYPE_A) != 0, true);
        append_time(line, a_file->meta->mtime, a_skew,
            (a_mask & TSK_FS_MAC_TYPE_M) != 0, true);
        append_time(line, a_file->meta->ctime, a_skew,
            (a_mask & TSK_FS_MAC_TYPE_C) != 0, true);
        append_time(line, a_file->meta->crtime, a_skew,
            (a_mask & TSK_FS_MAC_TYPE_B) != 0, false);
    }
    return line;
}

/* Should the entry be printed for the given mask? */
static bool
expect_printed(const TSK_FS_FILE * a_file, const TSK_FS_ATTR * a_attr,
    uint8_t a_mask)
{
    if (a_mask == TSK_FS_MAC_TYPE_ALL)
        return true;
    if (!a_file->meta)
        return false;
    if ((a_attr) && (a_attr->type == TSK_FS_ATTR_TYPE_NTFS_FNAME)) {
        return (((a_mask & TSK_FS_MAC_TYPE_A)
                && a_file->meta->time2.ntfs.fn_atime)
            || ((a_mask & TSK_FS_MAC_TYPE_M)
                && a_file->meta->time2.ntfs.fn_mtime)
            || ((a_mask & TSK_FS_MAC_TYPE_C)
                && a_file->meta->time2.ntfs.fn_ctime)
            || ((a_mask & TSK_FS_MAC_TYPE_B)
                && a_file->meta->time2.ntfs.fn_crtime));
    }
    return (((a_mask & TSK_FS_MAC_TYPE_A) && a_file->meta->atime)
        || ((a_mask & TSK_FS_MAC_TYPE_M) && a_file->meta->mtime)
        || ((a_mask & TSK_FS_MAC_TYPE_C) && a_file->meta->ctime)
        || ((a_mask & TSK_FS_MAC_TYPE_B) && a_file->meta->crtime));
}

static int
read_back(FILE * a_out, long a_start, long a_end, std::string & a_line)
{
    long len = a_end - a_start;
    if (len <= 0 || len > 4096)
        return 1;
    char buf[4097];
    if (fseek(a_out, a_start, SEEK_SET) != 0)
        return 1;
    if (fread(buf, 1, (size_t) len, a_out) != (size_t) len)
        return 1;
    buf[len] = '\0';
    if (fseek(a_out, a_end, SEEK_SET) != 0)
        return 1;
    a_line = buf;
    return 0;
}

int
main()
{
    TSK_FS_FILE fs_file;
    TSK_FS_META meta;
    TSK_FS_NAME name;
    TSK_FS_ATTR attr;
    char name_buf[64];

    memset(&fs_file, 0, sizeof(fs_file));
    memset(&meta, 0, sizeof(meta));
    memset(&name, 0, sizeof(name));
    memset(&attr, 0, sizeof(attr));

    name.name = name_buf;
    name.type = TSK_FS_NAME_TYPE_REG;
    name.meta_addr = 512;

    meta.type = TSK_FS_META_TYPE_REG;
    meta.mode =
        (TSK_FS_META_MODE_ENUM) (TSK_FS_META_MODE_IRUSR |
        TSK_FS_META_MODE_IWUSR | TSK_FS_META_MODE_IRGRP |
        TSK_FS_META_MODE_IROTH);
    meta.uid = 1000;
    meta.gid = 100;

    attr.type = TSK_FS_ATTR_TYPE_NTFS_FNAME;
    attr.id = 3;

    fs_file.name = &name;

    FILE *out = tmpfile();
    if (!out) {
        fprintf(stderr, "fs_timeline_apis: could not open temp file\n");
        return EXIT_FAILURE;
    }

    size_t printed_cnt = 0;
    size_t skipped_cnt = 0;
    size_t legacy_cnt = 0;
    time_t start = time(NULL);

    for (int i = 0; i < NUM_ENTRIES; i++) {
        uint8_t mask = g_masks[i % NUM_MASKS];
        int32_t skew = (i % 7 == 0) ? 3600 : 0;
        bool use_meta = (i % 11 != 0);
        bool use_fname = (i % 2 != 0);

        snprintf(name_buf, sizeof(name_buf), "file_%d.dat", i % 1009);
        name.meta_addr = 512 + (i % 65536);
        name.flags =
            (i % 3 ==
            0) ? TSK_FS_NAME_FLAG_ALLOC : TSK_FS_NAME_FLAG_UNALLOC;

        meta.flags =
            (i % 5 ==
            0) ? TSK_FS_META_FLAG_UNALLOC : TSK_FS_META_FLAG_ALLOC;
        meta.size = 1000 + (i % 5000);
        meta.atime = (i % 3) ? 1500000000 + i : 0;
        meta.mtime = (i % 2) ? 1500000001 + i : 0;
        meta.ctime = (i % 5) ? 1500000002 + i : 0;
        meta.crtime = (i % 7) ? 1500000003 + i : 0;
        meta.time2.ntfs.fn_atime = (i % 3) ? 1510000000 + i : 0;
        meta.time2.ntfs.fn_mtime = (i % 2) ? 1510000001 + i : 0;
        meta.time2.ntfs.fn_ctime = (i % 5) ? 1510000002 + i : 0;
        meta.time2.ntfs.fn_crtime = (i % 7) ? 1510000003 + i : 0;
        meta.time2.ntfs.fn_id = 3;

        attr.size = 512 + (i % 4096);

        fs_file.meta = (use_meta) ? &meta : NULL;
        const TSK_FS_ATTR *fs_attr = (use_fname) ? &attr : NULL;

        long pos_start = ftell(out);
        uint8_t printed =
            tsk_fs_name_print_mac_md5_mask(out, &fs_file, "/dir/",
            fs_attr, "vol1/", skew, NULL, mask);
        long pos_end = ftell(out);

        bool want = expect_printed(&fs_file, fs_attr, mask);
        if ((printed != 0) != want) {
            fprintf(stderr,
                "fs_timeline_apis: entry %d: printed=%d expected=%d (mask=0x%x)\n",
                i, printed, want, mask);
            fclose(out);
            return EXIT_FAILURE;
        }

        if (!printed) {
            if (pos_end != pos_start) {
                fprintf(stderr,
                    "fs_timeline_apis: entry %d: skipped entry wrote %ld bytes\n",
                    i, pos_end - pos_start);
                fclose(out);
                return EXIT_FAILURE;
            }
            skipped_cnt++;
            continue;
        }
        printed_cnt++;

        std::string got;
        if (read_back(out, pos_start, pos_end, got)) {
            fprintf(stderr, "fs_timeline_apis: entry %d: read error\n",
                i);
            fclose(out);
            return EXIT_FAILURE;
        }

        std::string want_line =
            expected_line(&fs_file, "/dir/", fs_attr, "vol1/", skew,
            mask);
        if (got != want_line) {
            fprintf(stderr,
                "fs_timeline_apis: entry %d mismatch (mask=0x%x):\n  got:  %s\n  want: %s\n",
                i, mask, got.c_str(), want_line.c_str());
            fclose(out);
            return EXIT_FAILURE;
        }

        /* a body file line must have exactly 11 fields (10 separators)
         * so that mactime and other existing tools can parse it */
        size_t pipes = 0;
        for (size_t p = 0; p < got.size(); p++)
            if (got[p] == '|')
                pipes++;
        if (pipes != 10) {
            fprintf(stderr,
                "fs_timeline_apis: entry %d has %zu fields, expected 11\n",
                i, pipes + 1);
            fclose(out);
            return EXIT_FAILURE;
        }

        /* with no filter, output must be identical to the legacy
         * tsk_fs_name_print_mac() used by tsk_gettimes and fls -m */
        if (mask == TSK_FS_MAC_TYPE_ALL) {
            long leg_start = ftell(out);
            tsk_fs_name_print_mac(out, &fs_file, "/dir/", fs_attr,
                "vol1/", skew);
            long leg_end = ftell(out);
            std::string leg;
            if (read_back(out, leg_start, leg_end, leg)) {
                fprintf(stderr,
                    "fs_timeline_apis: entry %d: read error\n", i);
                fclose(out);
                return EXIT_FAILURE;
            }
            if (leg != got) {
                fprintf(stderr,
                    "fs_timeline_apis: entry %d legacy mismatch:\n  mask:   %s\n  legacy: %s\n",
                    i, got.c_str(), leg.c_str());
                fclose(out);
                return EXIT_FAILURE;
            }
            legacy_cnt++;
        }
    }

    fclose(out);

    if (printed_cnt == 0 || skipped_cnt == 0) {
        fprintf(stderr,
            "fs_timeline_apis: bad coverage: printed=%zu skipped=%zu\n",
            printed_cnt, skipped_cnt);
        return EXIT_FAILURE;
    }

    printf("fs_timeline_apis: %d entries OK (%zu printed, %zu skipped, "
        "%zu legacy-compared) in %lds\n", NUM_ENTRIES, printed_cnt,
        skipped_cnt, legacy_cnt, (long) (time(NULL) - start));
    return EXIT_SUCCESS;
}
