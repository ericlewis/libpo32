#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  printf 'usage: %s "<subject>"\n' "$0" >&2
  exit 64
fi

subject=$1

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
Subject must follow Conventional Commits:

  <type>[optional scope]: <summary>

Examples:
  feat(pattern): add high-level pattern builder helpers
  fix(protocol): correct per-lane pattern packet layout
  docs(readme): clarify PO-32 transfer workflow
  docs: clarify PO-32 transfer workflow
  ci: add static-analysis workflow

Allowed types:
  feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert

SemVer guidance:
  feat     -> minor release
  fix/perf -> patch release
  !        -> breaking change / major release
EOF

exit 1
