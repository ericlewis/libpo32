#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

chmod +x \
  "$root_dir/.githooks/pre-commit" \
  "$root_dir/.githooks/pre-push" \
  "$root_dir/.githooks/commit-msg" \
  "$root_dir/scripts/check-commit-message.sh" \
  "$root_dir/scripts/check-conventional-subject.sh"

git -C "$root_dir" config core.hooksPath .githooks
git -C "$root_dir" config commit.template .gitmessage

printf 'Configured core.hooksPath to %s/.githooks\n' "$root_dir"
printf 'Configured commit.template to %s/.gitmessage\n' "$root_dir"
