#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QMAKE=qmake6
PASS=0
FAIL=0
ERRORS=""

for test_dir in tst_chunks tst_commands tst_translationtable tst_tablesdockwidget tst_datas tst_hexdocument tst_hexedit tst_disassembler tst_audio tst_graphics; do
    echo "============================================"
    echo "Building $test_dir..."
    echo "============================================"
    cd "$SCRIPT_DIR/$test_dir"
    rm -rf build
    mkdir build
    cd build
    $QMAKE CONFIG+=sdk_no_version_check ../${test_dir}.pro 2>&1
    if ! make -j4 2>&1; then
        echo "COMPILE FAILED: $test_dir"
        ERRORS="$ERRORS\nCOMPILE FAILED: $test_dir"
        FAIL=$((FAIL + 1))
        continue
    fi

    echo ""
    echo "Running $test_dir..."
    if ./$test_dir 2>&1; then
        PASS=$((PASS + 1))
    else
        echo "TEST FAILED: $test_dir"
        ERRORS="$ERRORS\nTEST FAILED: $test_dir"
        FAIL=$((FAIL + 1))
    fi
    echo ""
done

echo "============================================"
echo "SUMMARY: $PASS passed, $FAIL failed"
if [ -n "$ERRORS" ]; then
    echo -e "Failures:$ERRORS"
fi
echo "============================================"

exit $FAIL
