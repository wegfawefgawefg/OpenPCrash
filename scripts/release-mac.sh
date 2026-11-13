# The Mac build, sourced by scripts/release.sh (build_mac): build-release/OpenPCrash-mac/ holds
# OpenPCrash.app for Apple Silicon (arm64, macOS 11 and newer) and the notes beside it. On Linux it
# is cross-compiled with osxcross and Apple's SDK, which has to come from a Mac (mac_sdk_help); on
# a Mac, with the Xcode command line tools. Either way it bundles the Vulkan loader, built here from
# Khronos' sources, and MoltenVK's release. Mans In My House's scripts/release-mac.sh, with its pins.
# See docs/release.md, "The Mac build".
#
# Signing: ad hoc by default (Apple Silicon runs nothing unsigned; an ad hoc signature carries no
# name). --mac-identity signs with a real Developer ID instead, which puts that name on the build:
# on a Mac the keychain identity ("Developer ID Application: NAME (TEAM)"), on Linux a .p12 file
# with its password in the file PHANTOMCRASH_MAC_P12_PASSWORD_FILE. Off unless asked for.
#
# Uses release.sh's out, tools, jobs, stamp, symbols, version, product, game_version, fetch,
# seed_tool, lay_out_files and mac_identity.

osxcross_rev=27d21e4977c9751d01199c7a226a6faf494c3dd9
moltenvk_release=v1.4.2
moltenvk_sha256=f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e
vulkan_tag=vulkan-sdk-1.4.357.0
vulkan_headers_sha256=e87dce08116151f6b6d7de6b6faf41498e87e6cf848ff16fa3bd5402190ad4a3
vulkan_loader_sha256=54f2537df22313768da0317dda2abdaaab7711b4081c48c869a79db343d0ae70
# Clang 19 or newer for the macOS 26 SDK's libc++; the release's Linux build, of which only the
# compiler, linker and Mach-O tools are kept
llvm_release=22.1.8
llvm_sha256=df0e1ecf16caf3489a272a5eea4eec9b0d82878f6477fa309504f918a0006384
rcodesign_release=0.29.0
rcodesign_sha256=dbe85cedd8ee4217b64e9a0e4c2aef92ab8bcaaa41f20bde99781ff02e600002
mac_minimum=11.0
mac_bundle_id=org.openpcrash.OpenPCrash
osxcross="$tools/osxcross"

on_mac() { [[ "$(uname -s)" == Darwin ]]; }

# what someone with a Mac does once so this Linux machine can build for the Mac
mac_sdk_help() {
    cat <<EOF
release: no macOS SDK for osxcross. It has to be made on a Mac, once:

  On the Mac (Xcode, or only its command line tools: xcode-select --install):
    git clone https://github.com/tpoechtrager/osxcross
    cd osxcross
    ./tools/gen_sdk_package_tools.sh   # from the command line tools
    # or, with Xcode.app in /Applications:  ./tools/gen_sdk_package.sh
  It writes MacOSX<version>.sdk.tar.xz, one per SDK found, in that folder.
  Take the newest. Apple's licence doesn't allow passing it round, so it
  never goes in a repository or anywhere public.

  Here, put it in
    $osxcross/tarballs/
  (only the one MacOSX*.sdk.tar.* there) and run scripts/release.sh --only mac
  again: the first run builds osxcross. Or, with osxcross already built for
  another game here, PHANTOMCRASH_TOOLS_SEED=<its build-release/tools>.
EOF
}

# can a Mac build be made here: a Mac, or osxcross with an SDK (built or not, or to copy)
mac_can_build() {
    on_mac && return 0
    compgen -G "$osxcross/target/SDK/MacOSX*.sdk" >/dev/null ||
        compgen -G "$osxcross/tarballs/MacOSX*.sdk.tar.*" >/dev/null ||
        { [[ -n "$seed" ]] && compgen -G "$seed/osxcross/tarballs/MacOSX*.sdk.tar.*" >/dev/null; }
}

# LLVM's own release for Linux, the parts osxcross and the bundle use, in
# build-release/tools/llvm-<release>/
mac_llvm() {
    local dir="$tools/llvm-$llvm_release" name="LLVM-$llvm_release-Linux-X64"
    if [[ ! -x "$dir/bin/clang" ]] && ! seed_tool "llvm-$llvm_release" >&2; then
        echo "release: fetching LLVM $llvm_release (2 GB, of which 1 GB is kept)" >&2
        fetch "https://github.com/llvm/llvm-project/releases/download/llvmorg-$llvm_release/$name.tar.xz" \
            "$tools/llvm.tar.xz" "$llvm_sha256" >&2
        local keep=() tool
        for tool in clang clang++ "clang-${llvm_release%%.*}" lld ld64.lld llvm-ar llvm-ranlib llvm-libtool-darwin \
                    llvm-objcopy llvm-strip llvm-install-name-tool llvm-bitcode-strip llvm-objdump llvm-otool \
                    llvm-lipo llvm-nm llvm-readtapi llvm-dwarfdump llvm-symbolizer dsymutil; do
            keep+=("$name/bin/$tool")
        done
        rm -rf "$dir.part" && mkdir -p "$dir.part"
        tar -xf "$tools/llvm.tar.xz" -C "$dir.part" --strip-components=1 "${keep[@]}" "$name/lib/clang"
        rm -rf "$dir" && mv "$dir.part" "$dir" && rm "$tools/llvm.tar.xz"
    fi
    echo "$dir/bin"
}

# the compiler and Mach-O tools: mac_cmake (configure flags), mac_otool, mac_strip, mac_dsymutil,
# mac_lipo
mac_toolchain() {
    if on_mac; then
        if ! xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1; then
            echo "release: no macOS SDK: install the Xcode command line tools (xcode-select --install)" >&2
            exit 1
        fi
        mac_cmake=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$mac_minimum")
        mac_otool=(xcrun otool)
        mac_strip=(xcrun strip)
        mac_dsymutil=(xcrun dsymutil)
        mac_lipo=(xcrun lipo)
        return
    fi
    local llvm
    llvm="$(mac_llvm)"
    [[ -x "$llvm/clang" ]] || exit 1
    export PATH="$llvm:$PATH" # osxcross' wrappers find the same LLVM
    # osxcross, built: copied from the seed (without its build folder) or cloned and built here
    if [[ ! -d "$osxcross/.git" && -n "$seed" && -d "$seed/osxcross/.git" ]]; then
        echo "release: copying osxcross from $seed"
        mkdir -p "$osxcross.part"
        (cd "$seed/osxcross" && tar -cf - --exclude=./build .) | tar -xf - -C "$osxcross.part"
        mv "$osxcross.part" "$osxcross"
    fi
    if [[ ! -d "$osxcross/.git" ]]; then
        echo "release: fetching osxcross"
        git clone -q https://github.com/tpoechtrager/osxcross "$osxcross"
    fi
    if [[ "$(git -C "$osxcross" rev-parse HEAD)" != "$osxcross_rev" ]]; then
        git -C "$osxcross" fetch -q origin
        git -C "$osxcross" checkout -q "$osxcross_rev"
    fi
    if ! compgen -G "$osxcross/target/SDK/MacOSX*.sdk/usr/include/c++/v1/vector" >/dev/null; then
        if ! compgen -G "$osxcross/tarballs/MacOSX*.sdk.tar.*" >/dev/null; then
            mac_sdk_help >&2
            exit 1
        fi
        echo "release: building osxcross (the llvm flavor, arm64) with $llvm"
        (cd "$osxcross" && UNATTENDED=1 BUILD_FLAVOR=llvm ENABLE_REPLACEMENT_LIPO=0 ENABLE_ARCHS=arm64 \
            OSX_VERSION_MIN="$mac_minimum" JOBS="$jobs" nice -n 19 ./build.sh) > "$out/osxcross-build.log" 2>&1 || {
            echo "release: osxcross didn't build; see $out/osxcross-build.log" >&2
            exit 1
        }
    fi
    # compiler-rt's builtins for the Mac (libclang_rt.osx.a: Apple's Clang brings its own; SDL's
    # @available checks need it), built by osxcross' script, which fails only at merging
    # architectures there is one of
    local runtime
    runtime="$(dirname "$llvm")/lib/clang/${llvm_release%%.*}/lib/darwin/libclang_rt.osx.a"
    if [[ ! -f "$runtime" ]]; then
        echo "release: building compiler-rt for the Mac"
        (cd "$osxcross" && PATH="$osxcross/target/bin:$PATH" ENABLE_COMPILER_RT_INSTALL=1 JOBS="$jobs" \
            nice -n 19 ./build_compiler_rt.sh) > "$out/compiler-rt-build.log" 2>&1 || true
        local built
        built="$(ls "$osxcross"/build/compiler-rt*/compiler-rt/build_arm64/lib/darwin/libclang_rt.osx.a 2>/dev/null | head -1)"
        if [[ -z "$built" ]]; then
            echo "release: compiler-rt didn't build; see $out/compiler-rt-build.log" >&2
            exit 1
        fi
        mkdir -p "$(dirname "$runtime")"
        cp "$built" "$runtime"
    fi
    mac_cmake=(-DCMAKE_TOOLCHAIN_FILE="$repo_root/cmake/osxcross.cmake" -DOSXCROSS_ROOT="$osxcross")
    mac_otool=("$llvm/llvm-otool")
    mac_strip=("$llvm/llvm-strip")
    mac_dsymutil=("$llvm/dsymutil")
    mac_lipo=("$llvm/llvm-lipo")
    if [[ ! -x "$tools/rcodesign" ]] && ! seed_tool rcodesign; then
        echo "release: fetching rcodesign $rcodesign_release"
        local name="apple-codesign-$rcodesign_release-x86_64-unknown-linux-musl"
        fetch "https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F$rcodesign_release/$name.tar.gz" \
            "$tools/rcodesign.tar.gz" "$rcodesign_sha256"
        tar -xzf "$tools/rcodesign.tar.gz" -C "$tools"
        mv "$tools/$name/rcodesign" "$tools/rcodesign"
        rm -rf "${tools:?}/$name" "$tools/rcodesign.tar.gz"
    fi
}

# the Vulkan loader (libvulkan.1.dylib) and its headers, built from Khronos' sources for arm64 and
# macOS 11 into build-release/mac-vulkan/, once
mac_vulkan() {
    local prefix="$out/mac-vulkan" source="$tools/vulkan-$vulkan_tag"
    if [[ -f "$prefix/lib/libvulkan.1.dylib" && -f "$prefix/include/vulkan/vulkan.h" ]]; then
        return
    fi
    echo "release: building the Vulkan loader ($vulkan_tag)"
    mkdir -p "$source"
    local repo sha
    for repo in Vulkan-Headers Vulkan-Loader; do
        if [[ ! -f "$source/$repo/CMakeLists.txt" ]]; then
            sha="$vulkan_headers_sha256"
            [[ "$repo" == Vulkan-Loader ]] && sha="$vulkan_loader_sha256"
            fetch "https://github.com/KhronosGroup/$repo/archive/refs/tags/$vulkan_tag.tar.gz" "$source/$repo.tar.gz" "$sha"
            tar -xzf "$source/$repo.tar.gz" -C "$source"
            mv "$source/$repo-$vulkan_tag" "$source/$repo"
            rm "$source/$repo.tar.gz"
        fi
    done
    cmake -S "$source/Vulkan-Headers" -B "$prefix-build/headers" -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DVULKAN_HEADERS_ENABLE_TESTS=OFF -DVULKAN_HEADERS_ENABLE_MODULE=OFF >/dev/null
    cmake --install "$prefix-build/headers" >/dev/null
    # USE_GAS off: no assembly trampolines for unknown extensions, which the game doesn't use (and
    # which a cross build can't generate); its paths mapped out like the game's
    nice -n 19 cmake -S "$source/Vulkan-Loader" -B "$prefix-build/loader" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        "${mac_cmake[@]}" -DVulkanHeaders_DIR="$prefix/share/cmake/VulkanHeaders" \
        -DUSE_GAS=OFF -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF -DCMAKE_SKIP_RPATH=ON \
        "-DCMAKE_C_FLAGS=-ffile-prefix-map=$source=vulkan -ffile-prefix-map=$prefix-build=build" \
        -DCMAKE_INSTALL_NAME_DIR=@rpath -DCMAKE_BUILD_WITH_INSTALL_NAME_DIR=ON >/dev/null
    nice -n 19 cmake --build "$prefix-build/loader" --target vulkan --parallel "$jobs"
    mkdir -p "$prefix/lib"
    cp -L "$prefix-build/loader/loader/libvulkan.1.dylib" "$prefix/lib/"
    cp "$source/Vulkan-Loader/LICENSE.txt" "$prefix/LICENSE-Vulkan-Loader.txt"
}

# MoltenVK's release: libMoltenVK.dylib (both Macs in one; the arm64 half is kept)
mac_moltenvk() {
    mac_moltenvk_dir="$tools/moltenvk-$moltenvk_release"
    if [[ ! -f "$mac_moltenvk_dir/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" ]] &&
        ! seed_tool "moltenvk-$moltenvk_release"; then
        echo "release: fetching MoltenVK $moltenvk_release"
        mkdir -p "$mac_moltenvk_dir"
        fetch "https://github.com/KhronosGroup/MoltenVK/releases/download/$moltenvk_release/MoltenVK-macos.tar" \
            "$tools/moltenvk.tar" "$moltenvk_sha256"
        tar -xf "$tools/moltenvk.tar" -C "$mac_moltenvk_dir"
        rm "$tools/moltenvk.tar"
    fi
}

# the libraries a Mach-O loads through @rpath, by name
mac_rpath_libraries() { # file
    "${mac_otool[@]}" -L "$1" | awk 'NR > 1 && $1 ~ /^@rpath\// {sub("@rpath/", "", $1); print $1}'
}

# OpenPCrash.app from a build folder: build-release/OpenPCrash-mac/
mac_assemble() { # build
    local build="$1" root="$out/$product-mac"
    local app="$root/$product.app"
    local contents="$app/Contents"
    rm -rf "$root" && mkdir -p "$contents/MacOS" "$contents/Frameworks" \
        "$contents/Resources/vulkan/icd.d" "$symbols/mac"
    cp "$build/phantomcrash" "$contents/MacOS/$product"
    # every library the game loads by @rpath and the build made (SDL, Gubsy...), and theirs
    local pending=("$contents/MacOS/$product") file name found
    while [[ ${#pending[@]} -gt 0 ]]; do
        file="${pending[0]}"
        pending=(${pending[@]+"${pending[@]:1}"})
        for name in $(mac_rpath_libraries "$file"); do
            [[ -e "$contents/Frameworks/$name" ]] && continue
            case "$name" in
                libvulkan.1.dylib) found="$out/mac-vulkan/lib/libvulkan.1.dylib" ;;
                *) found="$(find "$build" -name "$name" \( -type f -o -type l \) -not -path '*/CMakeFiles/*' | head -1)" ;;
            esac
            if [[ -z "$found" ]]; then
                echo "release: $(basename "$file") loads @rpath/$name, which the build didn't make" >&2
                exit 1
            fi
            cp -L "$found" "$contents/Frameworks/$name"
            chmod 755 "$contents/Frameworks/$name"
            pending+=("$contents/Frameworks/$name")
        done
    done
    # Vulkan on Metal: the loader finds MoltenVK through the driver file in the bundle's
    # Resources/vulkan/icd.d
    "${mac_lipo[@]}" "$mac_moltenvk_dir/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" \
        -thin arm64 -output "$contents/Frameworks/libMoltenVK.dylib"
    sed 's|"library_path" *: *"[^"]*"|"library_path": "../../../Frameworks/libMoltenVK.dylib"|' \
        "$mac_moltenvk_dir/MoltenVK/MoltenVK/dynamic/dylib/macOS/MoltenVK_icd.json" \
        > "$contents/Resources/vulkan/icd.d/MoltenVK_icd.json"
    lay_out_files "$build" "$contents/Resources" "$root"
    cp "$mac_moltenvk_dir/MoltenVK/LICENSE" "$root/licenses/MoltenVK.txt"
    if [[ -f "$out/mac-vulkan/LICENSE-Vulkan-Loader.txt" ]]; then
        cp "$out/mac-vulkan/LICENSE-Vulkan-Loader.txt" "$root/licenses/Vulkan-Loader.txt"
    fi
    # CFBundleVersion is numbers only
    sed -e "s|@BUNDLE_ID@|$mac_bundle_id|" -e "s|@VERSION@|${game_version%%-*}|" \
        -e "s|@BUILD@|$product $version|" -e "s|@MINIMUM@|$mac_minimum|" \
        "$repo_root/scripts/release/Info.plist" > "$contents/Info.plist"
    printf 'APPL????' > "$contents/PkgInfo"
    # the symbols kept aside (dSYM bundles); what ships keeps none, nor the build's paths
    for file in "$contents/MacOS/$product" "$contents/Frameworks"/*.dylib; do
        name="$(basename "$file")"
        if [[ "$name" != libMoltenVK.dylib ]]; then
            "${mac_dsymutil[@]}" "$file" -o "$symbols/mac/$name.dSYM" 2>/dev/null || true
            "${mac_strip[@]}" -S "$file"
        fi
    done
    mac_check "$contents"
    mac_sign "$app"
}

# every library resolves inside the bundle, and nothing points at this machine
mac_check() { # contents
    local contents="$1" file name bad=0
    for file in "$contents/MacOS/$product" "$contents/Frameworks"/*.dylib; do
        for name in $(mac_rpath_libraries "$file"); do
            if [[ ! -f "$contents/Frameworks/$name" ]]; then
                echo "release: $(basename "$file") loads @rpath/$name, not in the bundle" >&2
                bad=1
            fi
        done
        if "${mac_otool[@]}" -L "$file" | awk 'NR > 1 {print $1}' |
            grep -v -e '^@rpath/' -e '^/usr/lib/' -e '^/System/Library/'; then
            echo "release: $(basename "$file") loads the above from outside the bundle" >&2
            bad=1
        fi
        if "${mac_otool[@]}" -l "$file" | awk '$1 == "path" {print $2}' | grep -v '^@'; then
            echo "release: $(basename "$file") searches the above for libraries" >&2
            bad=1
        fi
    done
    [[ "$bad" == 0 ]] || exit 1
}

# ad hoc by default; a Developer ID only with --mac-identity
mac_sign() { # app
    local app="$1" file
    if on_mac; then
        local identity="${mac_identity:--}" options=()
        [[ -n "$mac_identity" ]] && options=(--options runtime --timestamp)
        codesign --force --sign "$identity" ${options[@]+"${options[@]}"} "$app/Contents/Frameworks"/*.dylib
        codesign --force --sign "$identity" ${options[@]+"${options[@]}"} "$app"
        codesign --verify --deep --strict "$app"
        return
    fi
    if [[ -n "$mac_identity" ]]; then
        # a Developer ID from its .p12 (the identity is the file's path)
        [[ -f "$mac_identity" && -n "${PHANTOMCRASH_MAC_P12_PASSWORD_FILE:-}" ]] || {
            echo "release: on Linux --mac-identity is a .p12 file, with PHANTOMCRASH_MAC_P12_PASSWORD_FILE" >&2
            exit 1
        }
        "$tools/rcodesign" sign --p12-file "$mac_identity" \
            --p12-password-file "$PHANTOMCRASH_MAC_P12_PASSWORD_FILE" \
            --code-signature-flags runtime "$app" >/dev/null
        echo "release: signed with the Developer ID in $mac_identity (not notarized: docs/release.md)"
        return
    fi
    "$tools/rcodesign" sign "$app" >/dev/null 2>&1
    # (rcodesign's verify doesn't take ad hoc signatures; see that each is one)
    for file in "$app/Contents/MacOS/$product" "$app/Contents/Frameworks"/*.dylib; do
        if ! "$tools/rcodesign" print-signature-info "$file" 2>/dev/null | grep -q 'flags: CodeSignatureFlags(ADHOC)'; then
            echo "release: $file isn't signed" >&2
            exit 1
        fi
    done
    [[ -f "$app/Contents/_CodeSignature/CodeResources" ]] || { echo "release: $app isn't sealed" >&2; exit 1; }
}

build_mac() {
    local build="$out/mac-build"
    mac_toolchain
    mac_vulkan
    mac_moltenvk
    nice -n 19 cmake -S "$repo_root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "${mac_cmake[@]}" \
        -DVulkan_INCLUDE_DIR="$out/mac-vulkan/include" -DVulkan_LIBRARY="$out/mac-vulkan/lib/libvulkan.1.dylib" \
        -DCMAKE_C_FLAGS=-g -DCMAKE_CXX_FLAGS=-g "${stamp[@]}"
    nice -n 19 cmake --build "$build" --target phantomcrash phantomcrash_shaders --parallel "$jobs"
    mac_assemble "$build"
}
