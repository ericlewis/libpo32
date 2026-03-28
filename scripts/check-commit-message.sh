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

if ! sh "$(dirname "$0")/check-conventional-subject.sh" "$subject"; then
  printf '\nCommit message validation failed.\n' >&2
  exit 1
fi
