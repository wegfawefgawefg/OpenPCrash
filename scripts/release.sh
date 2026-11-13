#!/usr/bin/env bash
# Build the player releases from the checkout as it is, by hand (never CI): build-release/
# OpenPCrash-linux/, OpenPCrash-windows/ and OpenPCrash-mac/, each a folder and a zip of it beside,
# stamped with the game's version, the commit and the day. Nothing is uploaded anywhere. See
# docs/release.md.
#
#   scripts/release.sh [--only linux|windows|mac] [--mac]
#                      [--mac-identity NAME]   (a real Developer ID for the Mac; off by default:
#                                               the Mac build is signed ad hoc, with no name on it)
#   scripts/release.sh --check FOLDER          only the bundle checks below, on a folder already made
#
# Linux is built natively. Windows is cross-compiled with llvm-mingw and the Mac with osxcross,
# their toolchains fetched into build-release/tools/ the first time at pinned versions with pinned
# sha256s (Mans In My House's pins). PHANTOMCRASH_TOOLS_SEED=DIR copies a pinned tool already
# fetched elsewhere (a folder of the same name in DIR, such as another game's build-release/tools)
# instead of downloading it again. Without a macOS SDK for osxcross the Mac is left out (it says
# so), and --only mac says how to get one. Builds run under nice -n 19 with
# PHANTOMCRASH_JOBS jobs (4).
#
# Every bundle holds the game's own files only: the executable and its libraries, the shaders, the
# port's own assets/ (from git), the notes and the third-party licenses. Nothing from the disc:
# the release stops if any file outside that list is in a bundle (check_bundle), or if a binary
# names a path of the machine it was built on (check_paths).
#
# Bug reports (docs/release.md, "Bug reports") go where ~/.config/openpcrash/report_url says, when that file is
# there (one line, the https address); without it the builds have none, and their question and
# menu items stay hidden. The address is never in the repository.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
out="$repo_root/build-release"
tools="$out/tools"
jobs="${PHANTOMCRASH_JOBS:-4}"
seed="${PHANTOMCRASH_TOOLS_SEED:-}"
product=OpenPCrash

# Mans In My House's pins (scripts/release.sh there)
llvm_mingw_release=20260922
llvm_mingw_name="llvm-mingw-$llvm_mingw_release-ucrt-ubuntu-22.04-x86_64"
llvm_mingw_sha256=bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21
# the Mac build's pins and steps
source "$repo_root/scripts/release-mac.sh"

usage() { sed -n '2,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

only=""
check_only=""
mac_identity="${PHANTOMCRASH_MAC_IDENTITY:-}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --only) only="${2:?--only needs linux, windows or mac}"; shift 2 ;;
        --mac) only=mac; shift ;;
        --check) check_only="${2:?--check needs a bundle folder}"; shift 2 ;;
        --mac-identity) mac_identity="${2:?--mac-identity needs a signing identity}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "release: unknown argument $1" >&2; usage >&2; exit 2 ;;
    esac
done
if [[ -n "$only" ]]; then
    platforms=("$only")
elif on_mac; then
    platforms=(mac)
elif mac_can_build; then
    platforms=(linux windows mac)
else
    platforms=(linux windows)
    echo "release: no Mac build: no macOS SDK for osxcross yet (scripts/release.sh --only mac says how)"
fi
for platform in "${platforms[@]}"; do
    [[ "$platform" == linux || "$platform" == windows || "$platform" == mac ]] ||
        { echo "release: --only linux, windows or mac" >&2; exit 2; }
    if on_mac && [[ "$platform" != mac ]]; then
        echo "release: a Mac makes only the Mac build (--mac)" >&2
        exit 2
    fi
done

# the stamp: the game's version, the day, the commit (-dirty with changes not committed)
game_version="$(sed -n 's/^set(PHANTOMCRASH_GAME_VERSION \(.*\))$/\1/p' "$repo_root/cmake/release.cmake")"
commit="$(git -C "$repo_root" rev-parse --short=7 HEAD)"
if ! git -C "$repo_root" diff --quiet HEAD --; then
    commit="$commit-dirty"
fi
day="$(date -u +%Y-%m-%d)"
version="$game_version-$day-$commit"
echo "release: $product $version (${platforms[*]})"

sha256() { # file
    if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

fetch() { # url file sha256
    curl -fL --retry 3 -o "$2.part" "$1"
    if [[ "$(sha256 "$2.part")" != "$3" ]]; then
        echo "release: $1 is not the file expected (sha256)" >&2
        rm -f "$2.part"
        exit 1
    fi
    mv "$2.part" "$2"
}

# a pinned tool folder (its name carries its version) copied from PHANTOMCRASH_TOOLS_SEED if it's
# there; true when it was
seed_tool() { # name
    [[ -n "$seed" && -e "$seed/$1" && ! -e "$tools/$1" ]] || return 1
    echo "release: copying $1 from $seed"
    cp -a "$seed/$1" "$tools/$1.part" && mv "$tools/$1.part" "$tools/$1"
}

mkdir -p "$tools"
stamp=(-DPHANTOMCRASH_RELEASE=ON -DPHANTOMCRASH_USE_LOCAL_DEPS=OFF
       -DPHANTOMCRASH_COMMIT="$commit" -DPHANTOMCRASH_BUILD_DATE="$day")
# where bug reports go: the owner's own file, never the repository's
report_url_file="${PHANTOMCRASH_REPORT_URL_FILE:-$HOME/.config/openpcrash/report_url}"
report_url=""
if [[ -f "$report_url_file" ]]; then
    report_url="$(head -n 1 "$report_url_file" | tr -d '[:space:]')"
fi
stamp+=(-DPHANTOMCRASH_REPORT_URL="$report_url")
if [[ -n "$report_url" ]]; then
    echo "release: bug reports go to the address in $report_url_file"
else
    echo "release: no $report_url_file: these builds send no bug reports"
fi
built=() # the zips made, for the summary

# the third-party licenses: every dependency the game links, from its fetched sources
copy_licenses() { # build licenses_dir
    local deps="$1/_deps" to="$2" name file
    mkdir -p "$to"
    for name in sdl3:SDL3 sdl3_image:SDL3_image sdl3_ttf:SDL3_ttf sdl3_mixer:SDL3_mixer gubsy:Gubsy \
                gview:GView glayout:GLayout gfxp:gfxp gsexp:gsexp cgltf:cgltf bc7enc_rdo:bc7enc_rdo \
                curl:curl; do
        # not every build fetches every one (curl is the system's on Linux and the Mac)
        [[ -d "$deps/${name%%:*}-src" ]] || continue
        file="$(find "$deps/${name%%:*}-src" -maxdepth 1 -type f \
                \( -iname 'LICENSE*' -o -iname 'COPYING*' \) 2>/dev/null | sort | head -1)"
        if [[ -n "$file" ]]; then
            cp "$file" "$to/${name##*:}.txt"
        fi
    done
    # what the SDL libraries built from their own copies (SDL_ttf's FreeType, HarfBuzz, PlutoSVG...)
    local built_dir lib base
    for built_dir in "$deps"/sdl3*-build/external/*/; do
        [[ -d "$built_dir" ]] || continue
        name="$(basename "$built_dir")"
        lib="$(basename "$(dirname "$(dirname "$built_dir")")")"
        while IFS= read -r file; do
            base="$(basename "$file")"
            cp "$file" "$to/$name-${base%.*}.txt"
        done < <(find "$deps/${lib%-build}-src/external/$name" -maxdepth 2 -type f \
                     \( -iname 'FTL.TXT' -o -iname 'LICENSE*' -o -iname 'COPYING*' \) 2>/dev/null | sort)
    done
    # the owner's libraries are GPLv3, as the game is; the pinned commits of GView, GLayout, gfxp
    # and gsexp predate their LICENSE files, so each takes Gubsy's copy of the same text
    for name in Gubsy GView GLayout gfxp gsexp; do
        if [[ ! -f "$to/$name.txt" ]]; then
            cp "$deps/gubsy-src/LICENSE" "$to/$name.txt"
        fi
    done
    cp "$repo_root/third_party/pl_mpeg/LICENSE" "$to/pl_mpeg.txt"
    cp "$repo_root/third_party/lz4/LICENSE" "$to/lz4.txt"
    cp "$repo_root/assets/fonts/OFL.txt" "$to/Jost-OFL.txt"
}

# the game's own files beside the executable (or in the Mac app's Resources), the notes beside it
lay_out_files() { # build bundle [notes]
    local build="$1" bundle="$2" notes="${3:-$2}"
    # the port's own assets, from git: never anything the extractor or the cook made
    git -C "$repo_root" ls-files -z -- assets ':(exclude)assets/shaders' |
        (cd "$repo_root" && COPYFILE_DISABLE=1 tar -cf - --null -T -) | tar -xf - -C "$bundle"
    mkdir -p "$bundle/shaders"
    cp "$build"/shaders/*.spv "$bundle/shaders/"
    cp "$repo_root/scripts/release/README.txt" "$repo_root/scripts/release/THIRD-PARTY.txt" "$notes/"
    cp "$repo_root/LICENSE" "$notes/LICENSE.txt" # the game's own, GPLv3
    copy_licenses "$build" "$notes/licenses"
    printf '%s %s\n' "$product" "$version" > "$notes/VERSION.txt"
}

# the bundle holds only what a release is made of; anything else (above all anything taken from
# the disc: data/, cooked/, movies, sounds, models, the XBE) stops the release
check_bundle() { # bundle
    local bundle="$1" bad=0 path rel
    local tracked
    tracked="$(git -C "$repo_root" ls-files -- assets)"
    while IFS= read -r -d '' path; do
        rel="${path#"$bundle"/}"
        rel="${rel#"$product.app/Contents/Resources/"}"
        case "$rel" in
            */data/*|data/*|*/cooked/*|cooked/*|*Media/*|*.xbe|*.iso|*.xiso|*.pack|*.sfd|*.wav|\
            *.adx|*.gltf|*.glb|*.bin|*.xpr|*.tga|*.bmp|*.png|*.sexp.orig)
                case "$rel" in assets/*) ;; *)
                    echo "release: $rel looks taken from the disc" >&2; bad=1; continue ;;
                esac ;;
        esac
        case "$rel" in
            "$product"|"$product.exe"|*.so|*.so.*|*.dll|*.dylib) ;;
            shaders/*.spv|README.txt|THIRD-PARTY.txt|VERSION.txt|LICENSE.txt|licenses/*.txt) ;;
            "$product.app/Contents/Info.plist"|"$product.app/Contents/PkgInfo") ;;
            "$product.app/Contents/MacOS/$product"|"$product.app/Contents/_CodeSignature/"*) ;;
            "$product.app/Contents/Frameworks/"*.dylib) ;;
            vulkan/icd.d/MoltenVK_icd.json|"$product.icns") ;;
            assets/*)
                # byte for byte the file git has
                if ! grep -qxF "$rel" <<<"$tracked" ||
                    ! cmp -s "$path" "$repo_root/$rel"; then
                    echo "release: $rel is not the port's own file in git" >&2
                    bad=1
                fi ;;
            *) echo "release: $rel is not part of a release" >&2; bad=1 ;;
        esac
    done < <(find "$bundle" -type f -print0)
    if [[ "$bad" != 0 ]]; then
        echo "release: $bundle holds files a release mustn't: not released" >&2
        exit 1
    fi
}

# no binary names a path of this machine: the checkout, the home folder, the disks (a
# toolchain's own CI paths, such as libc++'s /home/runner/..., are not this machine's)
check_paths() { # bundle
    local bundle="$1" bad=0 file found
    while IFS= read -r -d '' file; do
        found="$(strings -a "$file" | grep -F -e "$repo_root" -e "$HOME" -e "/media/$(id -un)" -e "/home/$(id -un)" | head -3 || true)"
        if [[ -n "$found" ]]; then
            echo "release: ${file#"$bundle"/} names paths of this machine:" >&2
            printf '  %s\n' "$found" >&2
            bad=1
        fi
    done < <(find "$bundle" -type f \( -name "$product" -o -name '*.exe' -o -name '*.so*' -o -name '*.dll' \
                                        -o -name '*.dylib' \) -print0)
    [[ "$bad" == 0 ]] || { echo "release: not released" >&2; exit 1; }
}

build_linux() {
    local build="$out/linux-build" bundle="$out/$product-linux"
    local curl=()
    # libcurl.so.4 (OpenSSL) is on every distribution; Debian's libcurl.so for building may be
    # GnuTLS's libcurl-gnutls.so.4, which others lack (as Mans In My House's release.sh)
    if [[ -e /usr/lib/x86_64-linux-gnu/libcurl.so.4 ]]; then
        curl=(-DCURL_LIBRARY_RELEASE=/usr/lib/x86_64-linux-gnu/libcurl.so.4)
    fi
    nice -n 19 cmake -S "$repo_root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "${stamp[@]}" \
        ${curl[@]+"${curl[@]}"}
    nice -n 19 cmake --build "$build" --target phantomcrash phantomcrash_shaders --parallel "$jobs"

    rm -rf "$bundle" && mkdir -p "$bundle" "$symbols/linux"
    cp "$build/phantomcrash" "$bundle/$product"
    # every library the game finds in the build folder, under its soname
    ldd "$build/phantomcrash" | awk -v build="$build/" 'index($3, build) == 1 {print $1, $3}' |
        while read -r soname path; do cp -L "$path" "$bundle/$soname"; done
    # symbols kept aside, and each file finds the others beside it
    local elf name
    for elf in "$bundle/$product" "$bundle"/*.so*; do
        name="$(basename "$elf")"
        objcopy --only-keep-debug --compress-debug-sections=zlib "$elf" "$symbols/linux/$name.debug"
        strip --strip-unneeded "$elf"
        objcopy --add-gnu-debuglink="$symbols/linux/$name.debug" "$elf"
        patchelf --set-rpath '$ORIGIN' "$elf"
    done
    lay_out_files "$build" "$bundle"
}

build_windows() {
    local build="$out/windows-build" bundle="$out/$product-windows"
    if [[ ! -x "$tools/$llvm_mingw_name/bin/x86_64-w64-mingw32-clang++" ]] && ! seed_tool "$llvm_mingw_name"; then
        echo "release: fetching llvm-mingw $llvm_mingw_release"
        fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$llvm_mingw_release/$llvm_mingw_name.tar.xz" \
            "$tools/llvm-mingw.tar.xz" "$llvm_mingw_sha256"
        tar -xf "$tools/llvm-mingw.tar.xz" -C "$tools"
        rm "$tools/llvm-mingw.tar.xz"
    fi
    # the PDB holds the symbols; the exe names it by file name only, not by this machine's path
    nice -n 19 cmake -S "$repo_root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$repo_root/cmake/mingw-w64.cmake" \
        -DLLVM_MINGW_ROOT="$tools/$llvm_mingw_name" \
        "-DCMAKE_C_FLAGS=-g -gcodeview" "-DCMAKE_CXX_FLAGS=-g -gcodeview" \
        "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--pdb= -Wl,-Xlink=-pdbaltpath:%_PDB%" "${stamp[@]}"
    nice -n 19 cmake --build "$build" --target phantomcrash phantomcrash_shaders --parallel "$jobs"

    rm -rf "$bundle" && mkdir -p "$bundle" "$symbols/windows"
    cp "$build/phantomcrash.exe" "$bundle/$product.exe"
    cp "$build/phantomcrash.pdb" "$symbols/windows/$product.pdb"
    "$tools/$llvm_mingw_name/bin/llvm-strip" --strip-debug "$bundle/$product.exe"
    lay_out_files "$build" "$bundle"
}

if [[ -n "$check_only" ]]; then
    check_bundle "$(cd "$check_only" && pwd)"
    check_paths "$(cd "$check_only" && pwd)"
    echo "release: $check_only holds only the game's own files"
    exit 0
fi

symbols="$out/symbols/$version"
for platform in "${platforms[@]}"; do
    "build_$platform"
    bundle="$out/$product-$platform"
    check_bundle "$bundle"
    check_paths "$bundle"
    zip_name="$product-$version-$platform.zip"
    (cd "$out" && rm -f "$product"-*-"$platform".zip && zip -q -r -X "$zip_name" "$product-$platform")
    # the same zip under a name without the stamp, for GitHub Releases' latest/download links
    cp "$out/$zip_name" "$out/$product-$platform.zip"
    built+=("$out/$zip_name ($(du -h "$out/$zip_name" | cut -f1); folder $(du -sh "$bundle" | cut -f1))")
done
echo "release: done. Symbols in $symbols. Nothing was uploaded."
printf '  %s\n' "${built[@]}"
