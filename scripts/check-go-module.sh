#!/bin/sh
# Test the module and a consumer without access through the checkout layout.
set -eu
root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
"$root_dir/scripts/sync-go-core.sh" --check
check_dir=$(mktemp -d "${TMPDIR:-/tmp}/libpo-go-module.XXXXXX")
trap 'rm -rf "$check_dir"' EXIT HUP INT TERM
mkdir "$check_dir/module" "$check_dir/consumer"
cp -R "$root_dir/bindings/go/." "$check_dir/module/"
(cd "$check_dir/module" && GOWORK=off go test ./...)
go_version=$(awk '$1 == "go" { print $2 }' "$check_dir/module/go.mod")
cat > "$check_dir/consumer/go.mod" <<EOF
module example.com/libpo-consumer

go $go_version

require github.com/ericlewis/libpo32/bindings/go v0.0.0
replace github.com/ericlewis/libpo32/bindings/go => ../module
EOF
cat > "$check_dir/consumer/main.go" <<'EOF'
package main

import (
  "github.com/ericlewis/libpo32/bindings/go"
  "github.com/ericlewis/libpo32/bindings/go/po32"
  "github.com/ericlewis/libpo32/bindings/go/po33"
  "github.com/ericlewis/libpo32/bindings/go/po35"
  "github.com/ericlewis/libpo32/bindings/go/stereo"
)

func main() {
  if libpo.OK.Err() != nil { panic("status") }
  if po32.CRCUpdate(0x1d0f, 0) == 0 { panic("CRC") }
  samples, err := po33.EncodeSamples([]float32{0}, 23438)
  if err != nil || len(samples) != 1 || samples[0] != 0 { panic("samples") }
  speech, err := po35.AnalysisEncodeF32([]float32{0}, 24000, 80)
  if err != nil || len(speech) != 44 { panic("speech") }
  tx, err := stereo.NewModulator([]byte{0}, 48000)
  if err != nil { panic(err) }
  defer tx.Close()
  if tx.FramesRemaining() == 0 { panic("modem") }
}
EOF
(cd "$check_dir/consumer" && GOWORK=off go run .)
printf 'Standalone Go module and external consumer passed.\n'
