#!/usr/bin/env bash
# Fail the QML lint gate when a qmllint log contains any "Warning:" or "Error:" line.
#   tools/ci/check_qmllint_log.sh <qmllint log>
# Exit 0: no warnings/errors. Exit 1: warnings/errors found (printed). Exit 2: usage error,
# missing log or grep failure. Uses only grep -E (no ripgrep), so it runs on the GitHub
# Ubuntu image and in Git Bash on Windows. "Info:" lines (e.g. unused imports) pass.
set -u

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <qmllint log>" >&2
    exit 2
fi
log=$1
if [ ! -f "$log" ]; then
    echo "check_qmllint_log: log file not found: $log" >&2
    exit 2
fi

grep -nE 'Warning:|Error:' "$log"
status=$?
case $status in
    0)
        echo "check_qmllint_log: qmllint reported warnings or errors (see above)" >&2
        exit 1
        ;;
    1)
        echo "check_qmllint_log: no qmllint warnings or errors"
        exit 0
        ;;
    *)
        echo "check_qmllint_log: grep failed with exit code $status" >&2
        exit 2
        ;;
esac
