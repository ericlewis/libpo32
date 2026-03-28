#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

git -C "$root_dir" config core.hooksPath .githooks
git -C "$root_dir" config commit.template .gitmessage

printf 'Configured core.hooksPath to %s/.githooks\n' "$root_dir"
printf 'Configured commit.template to %s/.gitmessage\n' "$root_dir"
