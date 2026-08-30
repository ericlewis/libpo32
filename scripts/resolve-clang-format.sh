#!/bin/sh
# Resolve the clang-format binary and verify it matches the major version
# pinned in .clang-format-version, which is the same version CI installs.
#
# Formatting with any other major version rewrites lines the author never
# touched, so this refuses to run rather than reformatting silently.
#
# Meant to be sourced, not executed, by .githooks/pre-commit and
# scripts/ci-format.sh. Both run under `set -eu`, so a failure here exits
# the caller.
#
# Reads: root_dir, CLANG_FORMAT (optional explicit binary)
# Sets:  CLANG_FORMAT

pinned_major=$(tr -d '[:space:]' < "$root_dir/.clang-format-version")

if [ -z "$pinned_major" ]; then
  printf 'Could not read a pinned major version from %s\n' \
    "$root_dir/.clang-format-version" >&2
  exit 1
fi

if [ -z "${CLANG_FORMAT:-}" ]; then
  if command -v "clang-format-$pinned_major" >/dev/null 2>&1; then
    CLANG_FORMAT="clang-format-$pinned_major"
  else
    CLANG_FORMAT=clang-format
  fi
fi

clang_format_help() {
  cat >&2 <<EOF

Install clang-format $pinned_major, for example:

  pip install "clang-format~=$pinned_major.0"
  sudo apt-get install -y clang-format-$pinned_major
  brew install llvm@$pinned_major

Then point CLANG_FORMAT at it if it is not on PATH, for example:

  CLANG_FORMAT="\$(brew --prefix llvm@$pinned_major)/bin/clang-format"

The pinned version lives in .clang-format-version and is shared by the
pre-commit hook, scripts/ci-format.sh, and the clang-format CI job.
EOF
}

if ! command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
  printf 'clang-format not found: %s\n' "$CLANG_FORMAT" >&2
  clang_format_help
  exit 1
fi

clang_format_version=$("$CLANG_FORMAT" --version)
clang_format_major=$(
  printf '%s\n' "$clang_format_version" |
    sed -n 's/.*version \([0-9][0-9]*\).*/\1/p'
)

if [ -z "$clang_format_major" ]; then
  printf 'Could not parse a version from `%s --version`: %s\n' \
    "$CLANG_FORMAT" "$clang_format_version" >&2
  clang_format_help
  exit 1
fi

if [ "$clang_format_major" != "$pinned_major" ]; then
  printf 'clang-format version mismatch:\n\n' >&2
  printf '  using:    %s (%s)\n' "$CLANG_FORMAT" "$clang_format_version" >&2
  printf '  required: clang-format %s.x\n\n' "$pinned_major" >&2
  printf 'Refusing to format with clang-format %s: it would rewrite lines this\n' \
    "$clang_format_major" >&2
  printf 'change never touched and fail the clang-format CI job.\n' >&2
  clang_format_help
  exit 1
fi
