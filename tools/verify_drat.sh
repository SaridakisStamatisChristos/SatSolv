#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "usage: $0 <drat-trim> <instance.cnf> <proof.drat>" >&2
  exit 2
fi
set +e
output=$("$1" "$2" "$3" 2>&1)
rc=$?
set -e
printf '%s\n' "$output"
printf 'c drat-trim exit code: %s\n' "$rc" >&2
normalized=$(printf '%s\n' "$output" | tr -d '\r')
if grep -Fxq 's VERIFIED' <<<"$normalized"; then
  exit 0
fi
exit 1
