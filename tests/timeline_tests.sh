#!/bin/bash

# End-to-end tests for tsk_timeline: timeline export, timestamp-type
# filtering, and compatibility with the existing tools (tsk_gettimes
# and mactime).  Creates a small ext2 image with known timestamps and
# verifies the exported mactime body files.

EXIT_SUCCESS=0
EXIT_FAILURE=1
EXIT_SKIP=77

TSK_TIMELINE=../tools/timeline/tsk_timeline
TSK_GETTIMES=../tools/autotools/tsk_gettimes
MACTIME=../tools/timeline/mactime

if ! test -x ${TSK_TIMELINE}; then
	TSK_TIMELINE=../tools/timeline/tsk_timeline.exe
fi
if ! test -x ${TSK_TIMELINE}; then
	echo "Missing tsk_timeline executable"
	exit ${EXIT_SKIP}
fi

if ! test -x ${TSK_GETTIMES}; then
	TSK_GETTIMES=../tools/autotools/tsk_gettimes.exe
fi

if ! command -v mke2fs > /dev/null 2>&1 || ! command -v debugfs > /dev/null 2>&1; then
	echo "Missing mke2fs/debugfs, skipping"
	exit ${EXIT_SKIP}
fi

fail()
{
	echo "timeline_tests FAILED: $1"
	exit ${EXIT_FAILURE}
}

WORK=$(mktemp -d)
trap 'rm -rf ${WORK}' EXIT
IMG=${WORK}/fs.dd

# Create an ext2 image with a file that has known timestamps
# (ext2 has no creation time, so crtime is always 0)
dd if=/dev/zero of=${IMG} bs=1M count=16 2> /dev/null
if ! mke2fs -q -t ext2 -F ${IMG} > /dev/null 2>&1; then
	echo "Could not create ext2 image, skipping"
	exit ${EXIT_SKIP}
fi
echo "timeline test content" > ${WORK}/content.txt
debugfs -w -R "mkdir /dir1" ${IMG} > /dev/null 2>&1
debugfs -w -R "write ${WORK}/content.txt /dir1/fileA" ${IMG} > /dev/null 2>&1
debugfs -w -R "set_inode_field /dir1/fileA atime 1600000001" ${IMG} > /dev/null 2>&1
debugfs -w -R "set_inode_field /dir1/fileA mtime 1600000000" ${IMG} > /dev/null 2>&1
debugfs -w -R "set_inode_field /dir1/fileA ctime 1600000002" ${IMG} > /dev/null 2>&1

# 1. Unfiltered export must be identical to tsk_gettimes
${TSK_TIMELINE} ${IMG} | sort > ${WORK}/timeline.body || fail "export failed"
if test -x ${TSK_GETTIMES}; then
	${TSK_GETTIMES} ${IMG} | sort > ${WORK}/gettimes.body || fail "tsk_gettimes failed"
	diff ${WORK}/timeline.body ${WORK}/gettimes.body > /dev/null || fail "output differs from tsk_gettimes"
fi

# 2. Known timestamps must be present in the export
grep "^0|dir1/fileA|" ${WORK}/timeline.body | grep -q "|1600000001|1600000000|1600000002|0$" \
	|| fail "exported timestamps for dir1/fileA are wrong"

# 3. -t m must keep only the mtime field and drop entries without one
${TSK_TIMELINE} -t m ${IMG} > ${WORK}/m.body || fail "export with -t m failed"
grep "^0|dir1/fileA|" ${WORK}/m.body | grep -q "|0|1600000000|0|0$" \
	|| fail "filtered (-t m) timestamps for dir1/fileA are wrong"
awk -F'|' '$8 != 0 || $9 == 0 || $10 != 0 || $11 != 0 { exit 1 }' ${WORK}/m.body \
	|| fail "filtered (-t m) output contains non-mtime values"

# 4. -t b on a file system without creation times must be empty
${TSK_TIMELINE} -t b ${IMG} > ${WORK}/b.body || fail "export with -t b failed"
test $(wc -l < ${WORK}/b.body) -eq 0 || fail "filtered (-t b) output is not empty"

# 5. Invalid type must be rejected
${TSK_TIMELINE} -t x ${IMG} > /dev/null 2>&1 && fail "invalid -t argument was accepted"

# 6. Output must be readable by mactime
if test -f ${MACTIME} && command -v perl > /dev/null 2>&1; then
	perl ${MACTIME} -b ${WORK}/timeline.body 2019-01-01..2030-01-01 2> /dev/null | grep -q "dir1/fileA" \
		|| fail "mactime could not process the exported timeline"
	perl ${MACTIME} -b ${WORK}/m.body 2019-01-01..2030-01-01 2> /dev/null | grep "dir1/fileA" | grep -q "m\.\.\." \
		|| fail "mactime output for filtered timeline is wrong"
fi

echo "timeline_tests: all tests passed"
exit ${EXIT_SUCCESS}
