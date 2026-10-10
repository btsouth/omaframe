#!/usr/bin/env bash
# Builds the Omaframe Arch package in a clean archlinux container synced to
# Omarchy's stable mirror, a dated Arch snapshot. Qt binaries need the Qt
# release they were built against or newer, so building against the oldest Qt
# users have keeps the package working on stable, rc and edge. Needs Docker.
# OMAFRAME_ARCH_MIRROR and OMAFRAME_BUILD_IMAGE override the mirror and image.
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

image=${OMAFRAME_BUILD_IMAGE:-archlinux:latest}
mirror=${OMAFRAME_ARCH_MIRROR:-https://stable-mirror.omarchy.org/\$repo/os/\$arch}
echo "Building in $image against $mirror"
docker pull "$image" >/dev/null
docker run --rm --network host -v "$output_dir:/out" \
  -e mirror="$mirror" -e version="$version" -e owner="$(id -u):$(id -g)" \
  "$image" bash -euo pipefail -c '
    printf "Server = %s\n" "$mirror" >/etc/pacman.d/mirrorlist
    # The snapshot can be older than the image, so allow downgrades.
    pacman -Syyuu --noconfirm >/dev/null
    pacman -S --noconfirm --needed base-devel >/dev/null
    useradd -m build
    install -d -o build /build
    install -o build /out/PKGBUILD "/out/omaframe-$version.tar.gz" /build/
    cd /build
    source PKGBUILD
    pacman -S --noconfirm --needed --asdeps "${depends[@]}" "${makedepends[@]}" >/dev/null
    runuser -u build -- makepkg --force --noconfirm
    install -o "${owner%:*}" -g "${owner#*:}" -m 644 /build/omaframe-*.pkg.tar.zst /out/
  '
echo "Source archive: $archive"
echo "Source SHA-256: $checksum"
if $working_tree; then
  echo 'Local working-tree package. It is not a tagged release artifact.'
fi
