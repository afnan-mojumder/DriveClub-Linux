#!/usr/bin/env bash
# tools/fsr4cap/stage_windows.sh <target dir>
# Builds (once) and copies the Windows FSR 4.1.1 set-up tools into <target dir> (a release's
# bin/fsr411): fsr4cap.exe, dxil-spirv with its library, SPIRV-Tools' spirv-as/spirv-dis, the
# C++ runtime they share, the scripts, and the tools' licences. The launcher (dcport/fsr411.py)
# runs them on the player's own AMD DLL. Nothing of AMD's is copied: the FidelityFX headers
# fetched for building fsr4cap.exe are MIT, and AMD's DLLs are downloaded by the launcher.
# Run from Git Bash or MSYS2; needs MSYS2 CLANG64 (clang, cmake, ninja, spirv-tools) and git.
set -euo pipefail
target=$(realpath -m -- "${1:?target dir}")
cd -- "$(dirname -- "$0")/../.."
work=$PWD/out/fsr4cap
clang64=/c/msys64/clang64/bin
mkdir -p "$work" "$target"
in_clang64() { /c/msys64/usr/bin/env.exe MSYSTEM=CLANG64 CHERE_INVOKING=1 /usr/bin/bash -lc "$1"; }

dx=$work/dxil-spirv
if [[ ! -x $dx/build/dxil-spirv.exe ]]; then
    rm -rf "$dx"
    git clone -q https://github.com/HansKristian-Work/dxil-spirv.git "$dx"
    git -C "$dx" checkout -q 7dc52786cdb1f53c54dd5c5c2698dff00ea5f0a3
    git -C "$dx" submodule update -q --init --recursive
    git -C "$dx" apply "$PWD/tools/fsr4cap/dxil-spirv-class-bindings.patch"
    # libc++ wants <cstdlib> spelled out (third_party/cli_parser).
    in_clang64 "cd '$dx' && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS=-include cstdlib' >/dev/null && ninja -C build dxil-spirv >/dev/null"
fi

sdk=$work/ffxsdk
if [[ ! -f $sdk/Kits/FidelityFX/api/include/ffx_api.h ]]; then
    rm -rf "$sdk"
    git clone -q --filter=blob:none --sparse https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git "$sdk"
    git -C "$sdk" checkout -q 60f4ea81909200d8542eca14dccb2628b763a9a3
    MSYS_NO_PATHCONV=1 git -C "$(cygpath -w "$sdk")" sparse-checkout set --no-cone \
        /Kits/FidelityFX/api/include/ /Kits/FidelityFX/upscalers/include/
fi
k=$sdk/Kits/FidelityFX
if [[ ! -f $work/fsr4cap.exe || tools/fsr4cap/fsr4cap.c -nt $work/fsr4cap.exe || tools/fsr4cap/capture.c -nt $work/fsr4cap.exe ]]; then
    in_clang64 "clang -std=c11 -O1 -I'$k/api/include' -I'$k/upscalers/include' tools/fsr4cap/fsr4cap.c tools/fsr4cap/capture.c tools/fsr4cap/rootsig.c -o '$work/fsr4cap.exe' -ld3d12 -ldxguid -static"
fi

cp "$work/fsr4cap.exe" "$dx/build/dxil-spirv.exe" "$dx/build/libdxil-spirv-c-shared.dll" "$target/"
cp "$clang64/spirv-as.exe" "$clang64/spirv-dis.exe" "$clang64/libSPIRV-Tools.dll" "$clang64/libc++.dll" "$target/"
cp tools/fsr4cap/extract.py tools/fsr4cap/name_bindings.py tools/fsr4cap/postpass_lds.py "$target/"
mkdir -p "$target/licenses"
cp "$dx/LICENSE.MIT" "$target/licenses/dxil-spirv-LICENSE.txt"
cp /c/msys64/clang64/share/licenses/spirv-tools/LICENSE "$target/licenses/SPIRV-Tools-LICENSE.txt" 2>/dev/null || \
    printf 'SPIRV-Tools: Apache License 2.0, https://github.com/KhronosGroup/SPIRV-Tools\n' > "$target/licenses/SPIRV-Tools-LICENSE.txt"
cp /c/msys64/clang64/share/licenses/libc++/LICENSE.TXT "$target/licenses/libc++-LICENSE.txt" 2>/dev/null || \
    printf 'libc++: Apache License 2.0 with LLVM Exceptions, https://llvm.org\n' > "$target/licenses/libc++-LICENSE.txt"
printf 'fsr4cap.exe, extract.py, name_bindings.py, postpass_lds.py: GPL-2.0-or-later (bbport, DCport).\n' > "$target/licenses/README.txt"
echo "FSR 4.1.1 tools in $target"
