#!/usr/bin/env bash
#
# lint — the alias the old tools/ci.sh dispatch also answered to; it is `tidy`.
#
# Called from gate.toml as `[stage.lint] cmd = "scripts/lint.sh"`.
set -uo pipefail
exec "$(dirname "$0")/tidy.sh" "$@"
