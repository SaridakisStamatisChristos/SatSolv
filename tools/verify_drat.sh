#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "usage: $0 <drat-trim> <instance.cnf> <proof.drat>" >&2
  exit 2
fi
"$1" "$2" "$3"
