#!/usr/bin/env bash
set -euo pipefail

if (( $# != 5 )); then
  echo "usage: $0 <binary> <version> <revision> <artifacts-dir> <build-image>" >&2
  exit 2
fi

binary=$1
version=$2
revision=$3
artifacts=$4
build_image=$5
max_glibc=${LINUX_CI_MAX_GLIBC:-2.28}
source_date_epoch=${SOURCE_DATE_EPOCH:-1}

if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid version: $version" >&2
  exit 1
fi
if [[ ! $revision =~ ^[0-9a-f]{40,64}$ ]]; then
  echo "Invalid revision: $revision" >&2
  exit 1
fi
if [[ ! -x $binary ]]; then
  echo "Linux executable is missing or not executable: $binary" >&2
  exit 1
fi
for source_file in LICENSE README.md; do
  if [[ ! -s $source_file ]]; then
    echo "Package input is missing: $source_file" >&2
    exit 1
  fi
done

version_is_greater() {
  local left=$1
  local right=$2
  [[ $left != "$right" && $(printf '%s\n%s\n' "$left" "$right" | sort -V | tail -n 1) == "$left" ]]
}

verify_binary() {
  local executable=$1
  local ldd_output=$2
  local headers
  local dynamic
  local glibc_versions
  local required_glibc

  headers=$(readelf -hW "$executable")
  grep -Eq '^  Class:[[:space:]]+ELF64$' <<< "$headers"
  grep -Eq '^  Machine:[[:space:]]+Advanced Micro Devices X86-64$' <<< "$headers"
  file -b "$executable" | grep -Eq '^ELF 64-bit LSB (pie )?executable, x86-64,'
  readelf -lW "$executable" \
    | grep -Fq 'Requesting program interpreter: /lib64/ld-linux-x86-64.so.2'

  dynamic=$(readelf -dW "$executable")
  grep -q '(NEEDED)' <<< "$dynamic"
  if grep -E '(RPATH|RUNPATH).*(/usr/src/tdesktop|/home/runner/work|/github/workspace)' \
      <<< "$dynamic"; then
    echo 'The binary contains a build-workspace runtime search path.' >&2
    exit 1
  fi

  if ! ldd "$executable" > "$ldd_output" 2>&1; then
    cat "$ldd_output" >&2
    exit 1
  fi
  if grep -Fq 'not found' "$ldd_output"; then
    cat "$ldd_output" >&2
    exit 1
  fi
  if grep -E '/(usr/src/tdesktop|home/runner/work|github/workspace)' "$ldd_output"; then
    echo 'A runtime dependency resolves inside the build workspace.' >&2
    exit 1
  fi

  glibc_versions=$(readelf --version-info --wide "$executable" \
    | grep -Eo 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBC_//' | sort -Vu)
  required_glibc=$(tail -n 1 <<< "$glibc_versions")
  if [[ -z $required_glibc ]]; then
    echo 'The binary has no versioned GLIBC dependency.' >&2
    exit 1
  fi
  if version_is_greater "$required_glibc" "$max_glibc"; then
    echo "The binary requires GLIBC $required_glibc; maximum allowed is $max_glibc." >&2
    exit 1
  fi
}

rm -rf "$artifacts"
mkdir -p "$artifacts"
artifacts=$(realpath "$artifacts")
binary=$(realpath "$binary")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stage="$work/stage"
package_root="$stage/AyuGram"
mkdir -p "$package_root"
install -m 755 "$binary" "$package_root/AyuGram"
strip --strip-unneeded "$package_root/AyuGram"
install -m 644 LICENSE README.md "$package_root/"

ldd_output="$work/ldd.txt"
verify_binary "$package_root/AyuGram" "$ldd_output"

runtime_report="$artifacts/runtime-dependencies.txt"
{
  echo 'AyuGram Linux x64 runtime dependency report'
  echo
  echo 'ELF identity:'
  file -b "$package_root/AyuGram"
  echo
  echo 'Program interpreter:'
  readelf -lW "$package_root/AyuGram" | grep 'Requesting program interpreter:'
  echo
  echo 'Direct ELF dependencies:'
  readelf -dW "$package_root/AyuGram" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort -u
  echo
  echo 'Resolved dependency tree:'
  sed -E 's/ \(0x[0-9a-f]+\)$//' "$ldd_output"
  echo
  echo "Maximum allowed GLIBC: $max_glibc"
  printf 'Required GLIBC versions: '
  readelf --version-info --wide "$package_root/AyuGram" \
    | grep -Eo 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tr '\n' ' '
  echo
  printf 'Required GLIBCXX versions: '
  readelf --version-info --wide "$package_root/AyuGram" \
    | grep -Eo 'GLIBCXX_[0-9]+(\.[0-9]+)+' | sort -Vu | tr '\n' ' ' || true
  echo
} > "$runtime_report"

metadata="$artifacts/build-metadata.txt"
cat > "$metadata" <<EOF
Artifact: AyuGram diagnostic Linux x64 package
Version: $version
Revision: $revision
Build image: $build_image
Architecture: x86_64
Configuration: Release
Automatic updates: disabled
Crash reports: disabled
Credentials: public build credentials from docs/api_credentials.md
EOF
install -m 644 "$runtime_report" "$metadata" "$package_root/"

package_name="AyuGram-$version-linux-x64.tar.xz"
package="$artifacts/$package_name"
tar --sort=name \
  --mtime="@$source_date_epoch" \
  --owner=0 --group=0 --numeric-owner \
  -C "$stage" -cf - AyuGram \
  | xz -9 -T0 > "$package"
test -s "$package"

cat > "$work/expected-files.txt" <<'EOF'
AyuGram/
AyuGram/AyuGram
AyuGram/LICENSE
AyuGram/README.md
AyuGram/build-metadata.txt
AyuGram/runtime-dependencies.txt
EOF
tar -tJf "$package" | LC_ALL=C sort > "$work/actual-files.txt"
LC_ALL=C sort -o "$work/expected-files.txt" "$work/expected-files.txt"
diff -u "$work/expected-files.txt" "$work/actual-files.txt"

mkdir "$work/extracted"
tar -xJf "$package" -C "$work/extracted"
cmp "$package_root/AyuGram" "$work/extracted/AyuGram/AyuGram"
verify_binary "$work/extracted/AyuGram/AyuGram" "$work/extracted-ldd.txt"

(
  cd "$artifacts"
  sha256sum "$package_name" > SHA256SUMS.txt
  sha256sum --check SHA256SUMS.txt
)
