#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=$(sed -n 's/^project(omaframe VERSION \([0-9][0-9.]*\) LANGUAGES.*/\1/p' "$repo_dir/CMakeLists.txt")
if [[ -z "$version" ]]; then
  echo 'Could not read the Omaframe version from CMakeLists.txt.' >&2
  exit 1
fi

working_tree=false
if [[ "${1:-}" == '--working-tree' ]]; then
  working_tree=true
  shift
fi
if (( $# > 1 )); then
  echo 'Usage: build-package.sh [--working-tree] [output-directory]' >&2
  exit 1
fi
output_dir=$(realpath -m "${1:-$repo_dir/dist-local}")
mkdir -p "$output_dir"

if ! $working_tree; then
  if [[ -n "$(git -C "$repo_dir" status --porcelain)" ]]; then
    echo 'Release package requires a clean working tree. Use --working-tree for a local test package.' >&2
    exit 1
  fi
  tag="v${version}"
  if [[ "$(git -C "$repo_dir" rev-parse HEAD)" != "$(git -C "$repo_dir" rev-parse -q --verify "refs/tags/$tag^{}" 2>/dev/null)" ]]; then
    echo "Release package requires HEAD at tag $tag." >&2
    exit 1
  fi
fi

temp_dir=$(mktemp -d "$output_dir/.package-work.XXXXXX")
trap 'rm -rf "$temp_dir"' EXIT
archive="$output_dir/omaframe-${version}.tar.gz"
if $working_tree; then
  git -C "$repo_dir" ls-files --cached --others --exclude-standard -z |
    tar -C "$repo_dir" --null -T - --sort=name --mtime='@0' \
      --owner=0 --group=0 --numeric-owner \
      --transform="s,^,omaframe-${version}/," -cf "$temp_dir/source.tar"
else
  git -C "$repo_dir" archive --format=tar --prefix="omaframe-${version}/" \
    "v${version}" > "$temp_dir/source.tar"
fi
gzip -n -c "$temp_dir/source.tar" > "$archive"
checksum=$(sha256sum "$archive" | cut -d ' ' -f 1)
sed -e "s/@VERSION@/$version/g" -e "s/@SHA256@/$checksum/g" \
  "$repo_dir/packaging/PKGBUILD.in" > "$output_dir/PKGBUILD"

(
  cd "$output_dir"
  BUILDDIR="$temp_dir/build" PKGDEST="$output_dir" \
    makepkg --force --noconfirm
)
echo "Source archive: $archive"
echo "Source SHA-256: $checksum"
if $working_tree; then
  echo 'Local working-tree package. It is not a tagged release artifact.'
fi
