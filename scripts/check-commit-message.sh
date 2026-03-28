#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  printf 'usage: %s <commit-message-file>\n' "$0" >&2
  exit 64
fi

message_file=$1

if [ ! -f "$message_file" ]; then
  printf 'commit message file not found: %s\n' "$message_file" >&2
  exit 66
fi

subject=$(sed -n '1p' "$message_file")

case "$subject" in
  Merge\ *|Revert\ *)
    exit 0
    ;;
esac

pattern='^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([[:alnum:]_.-]+\))?(!)?: .+$'

if printf '%s\n' "$subject" | grep -Eq "$pattern"; then
  exit 0
fi

cat >&2 <<'EOF'
Commit message must follow Conventional Commits:

  <type>(<scope>)?: <summary>

Examples:
  feat(pattern): add high-level pattern builder helpers
  fix(protocol): correct per-lane pattern packet layout
  docs(readme): clarify PO-32 transfer workflow
  chore(ci): add static-analysis workflow

Allowed types:
  feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert

SemVer guidance:
  feat     -> minor release
  fix/perf -> patch release
  !        -> breaking change / major release
EOF

exit 1
