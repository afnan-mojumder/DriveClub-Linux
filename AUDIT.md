# Audit Findings

Generated 2026-10-29 by a read-only multi-agent audit (15 areas). Verification was cut short by a usage limit:

- **confirmed**: both skeptic agents agreed the defect is real (second field = their adjusted severity)
- **contested**: skeptics disagreed
- **unverified**: no skeptic pass was completed (macOS port, tests/CI, repo hygiene/docs) - treat as likely, not confirmed
- Refuted findings are omitted.

Context: this repo is a Driveclub port built on a fork of the Bloodborne Windows port, so many issues are Bloodborne leftovers or partly-applied Driveclub changes.

## Suggested priorities
1. Wave64 subgroup-size mismatch in `lower_wave64_pass.cpp` (Driveclub-specific, can cause wrong rendering)
2. Missing bounds checks on guest command buffers in `amdgpu/liverpool.cpp` (heap overflow)
3. `runtime_memory.c`: stale flexible-VMA offsets and unbounded `MAP_FIXED`
4. `windows/launcher/prepare/dc_patches.py:47` patch compile error
5. Bloodborne-specific asserts in `liverpool.cpp:866`, cpu_compat eboot hash binding
6. Add Linux tests to CI

---


## core-common

_common/ and core/ are mostly upstream shadPS4 with sparse port patches. core/ holds only 36 files under libraries/ (avplayer, gnmdriver, kernel, videodec, videoout, system). The owner edits are small: thread.cpp (SCHED_RR realtime, clock_nanosleep) and path_util.cpp (create_directories). Those edits and the port's BB_GPU_USER_DIR handling are where the real defects are. Defects confirmed: an uninitialized-read in AccurateSleep, a std::max/min inversion, a user-dir override that is silently ignored when the directory is missing, unchecked getenv("HOME"), and an unsynchronized global kqueue map in equeue.cpp. gnmdriver.cpp appears to be unchanged upstream code with assert-based validation of guest values. I reviewed it only partially._


### [medium] AccurateSleep reads uninitialized timespec 'remain' on success
- id `core-common-1` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/common/thread.cpp:166`
- clock_nanosleep (flags 0) only writes 'remain' when interrupted. When the sleep completes (ret==0) 'remain' is never written, but it is read unconditionally when 'remaining' is non-null. The result is an indeterminate (stack garbage) remaining duration, which is UB. The change from nanosleep to clock_nanosleep kept this latent upstream bug.
- **Fix:** Zero-initialize: timespec remain{}; or set *remaining = 0 when ret == 0.

### [medium] User dir override (BB_GPU_USER_DIR) silently ignored when the directory does not exist
- id `core-common-2` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/common/path_util.cpp:91`
- If BB_GPU_USER_DIR is set but the directory does not yet exist, the `!exists(user_dir)` branch overwrites it with the XDG/HOME shadPS4 path on Linux. Caches and shaders then go to ~/.local/share/shadPS4 instead of the requested location. The CI workaround 'mkdir -p ~/.local/share/shadPS4' points at this. The comment says the port wants GPU caches w
- **Fix:** Skip the fallback when port_dir is set (and create_directories(user_dir, ec) on all platforms).

### [low] getenv("HOME") dereferenced without null check (static init)
- id `core-common-3` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/common/path_util.cpp:102`
- std::filesystem::path(getenv("HOME")) with HOME unset (systemd service, sanitized env, some launchers) constructs a path from nullptr, which is UB and typically crashes. This runs in a static initializer before main, so it is hard to diagnose. The same applies to the __APPLE__ branch at line 96.
- **Fix:** Check for null and fall back to getpwuid(getuid())->pw_dir or the cwd portable dir.

### [medium] kqueues global map accessed from multiple threads without a lock; delete races with waiters
- id `core-common-4` · status **confirmed** (medium,low) · `runtime-linux/gpu/shadps4/core/libraries/kernel/equeue.cpp:24`
- static std::unordered_map<s32, EqueueInternal*> kqueues is mutated by posix_kqueue/sceKernelCreateEqueue/sceKernelDeleteEqueue (guest threads) and read by the Hr/Timer callbacks (io_context thread) and WaitForEvents callers, with no synchronization. Concurrent insert (rehash) while another thread does contains()/operator[] is a data race. sceKernel
- **Fix:** Guard the map with a mutex (or shared_mutex) and use find(); hold a shared_ptr in the map and take a copy before use so delete cannot free it under a waiter.

### [low] SetCurrentThreadPriority uses std::max where min is intended
- id `core-common-5` · status **contested** (low,none) · `runtime-linux/gpu/shadps4/common/thread.cpp:148`
- `level = std::max(new_priority + 1, 4U)` makes level always >= 4, so priority is always max_prio regardless of the requested priority. For SCHED_OTHER min==max==0, so it is currently a no-op. Any switch to a RT policy would make every thread top priority.
- **Fix:** Use std::min, and check the pthread_setschedparam return value.

### [low] SetCurrentThreadRealtime: SCHED_RR with fixed prio 10, ignores period, errors unlogged
- id `core-common-6` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/common/thread.cpp:80`
- On failure (normal for unprivileged users without RLIMIT_RTPRIO) it falls back to setpriority(-10), which also fails silently without CAP_SYS_NICE. Nothing is logged, so the user cannot tell frame-pacing priority was not applied. The period_ns parameter is unused. If it succeeds, an RR thread that spins can starve other threads on the same core. Pr
- **Fix:** Log once at info/warn level on failure of both calls; document the rtprio limits requirement in setup.sh.

### [low] PreciseSleepUntil uses x86-only __builtin_ia32_pause on all non-Windows platforms
- id `core-common-7` · status **contested** (none,low) · `runtime-linux/gpu/shadps4/common/thread.cpp:271`
- Compile failure on aarch64 (and __APPLE__ arm64 builds). Also clock_nanosleep(TIMER_ABSTIME) with steady_clock epoch assumes steady_clock==CLOCK_MONOTONIC (true for libstdc++, not guaranteed), and the return value is ignored so an EINTR wake will fall through to a pure spin until the deadline.
- **Fix:** Use an arch-guarded pause (_mm_pause / __asm__ yield) and retry clock_nanosleep on EINTR.

### [low] Directory creation errors discarded; notice file written without checking dir
- id `core-common-8` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/common/path_util.cpp:113`
- The recent fix passes an error_code but never inspects or logs it. UserPaths is registered even when creation failed, so later GetUserPath callers fail with obscure errors (e.g. an unwritable XDG_DATA_HOME). current_path() on line 90 can also throw in a static initializer if the cwd was deleted.
- **Fix:** Log (or abort with a clear message) when ec is set for UserDir; use current_path(ec).

### [low] Missing standard includes in thread.cpp
- id `core-common-9` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/common/thread.cpp:209`
- std::min, strlen, errno and clock_nanosleep are used, but only <ctime>, <string>, <thread> are included directly. This relies on transitive includes and is fragile across libstdc++/libc++ versions.
- **Fix:** Add <algorithm>, <cstring>, <cerrno>.
- *Improvement:* Make the fixed 60 FPS vblank timing robust: log and report when realtime priority fails, and expose the result so run_driveclub.sh can print a hint (limits.conf rtprio / RLIMIT_RTPRIO). - The silent fallback hides the most common cause of frame-pacing jitter on stock distros.
- *Improvement:* Replace the equeue global map with a mutex-protected map of shared_ptr<EqueueInternal> and a lookup helper returning a shared_ptr. - Removes the race and use-after-free on delete, and removes the repeated contains()+operator[] double lookup.
- *Improvement:* Honor BB_GPU_USER_DIR unconditionally and create it with create_directories; drop the CI-only mkdir workaround for ~/.local/share/shadPS4. - Makes the cache location deterministic for run_driveclub.sh and CI.
- *Improvement:* Use an architecture-neutral cpu_relax() helper (pause/yield) in spin loops. - Allows aarch64 builds, such as a Steam Deck-class or ARM host, to compile.
- *Improvement:* Add a unit test for AccurateSleep and AccurateTimer (remaining value after a full sleep must be 0) to the existing tests dir. - Would have caught the uninitialized 'remain' and guards the owner's clock_nanosleep migration.
- *Improvement:* Replace ASSERT_MSG on guest-supplied cmdbuf/size in PatchFlipRequest with an error return. - `cmdbuf += size - 64` with a small guest size reads before the buffer, and the assert aborts the whole emulator on bad guest data rather than returnin

## vk-renderer

_The renderer_vulkan directory is almost entirely Bloodborne-port/shadPS4 code. The owner's diff (660be01) touches this area in only six small places: draw-prep worker count forced to 0, a per-stage subgroup-size-64 check, a FifoRelaxed present mode, the DLSS branch in RunDriveclub, and a shader-binary version bump. None of those edits introduces a serious defect. Because DefaultWorkerCount() now returns 0, the multithreaded draw-prep scanner and workers are off by default (BB_PREP_WORKERS re-enables them), so the race and lifetime surface there is not live. I found no confirmed races or missing barriers in the owner's edits. I did not read the 4k-line rasterizer, the 2.5k-line upscaler or the scheduler end to end, so the findings below are limited to code I actually read. They are all low-to-medium severity robustness issues in swapchain and presenter code, plus debug-only code._


### [medium] Swapchain recreated with zero window size (minimize) hits an ASSERT
- id `vk-renderer-1` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp:672`
- PresentImage recreates the swapchain whenever the window size differs from the swapchain size, and again after a failed acquire. Neither call guards against a zero width or height. Only the post-present recreate at line 820 checks for non-zero. On a compositor that reports a 0x0 surface when minimized, SetSurfaceProperties copies capabilities.curre
- **Fix:** At the top of PresentImage, return false (skip the frame) when window.GetWidth() == 0 || window.GetHeight() == 0. Apply the same guard to the recreate after a failed acquire at line 676.

### [low] Swapchain::Present asserts on SurfaceLost and other non-success results
- id `vk-renderer-2` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/renderer_vulkan/vk_swapchain.cpp:243`
- Present() treats only OutOfDate and Suboptimal as recoverable. Any other result, including eErrorSurfaceLostKHR, hits ASSERT_MSG(result == eSuccess). AcquireNextImage already treats SurfaceLost as recoverable (needs_recreation), so the two paths are inconsistent. A transient surface loss during a Wayland or X11 resize or compositor restart crashes
- **Fix:** Add eErrorSurfaceLostKHR to the recoverable set. Call Breadcrumbs::Report() on eErrorDeviceLost, as the acquire path does.

### [low] FindPresentFormat indexes formats[0] without an empty check and can leave surface_format uninitialised
- id `vk-renderer-3` · status **contested** (none,low) · `runtime-linux/gpu/shadps4/video_core/renderer_vulkan/vk_swapchain.cpp:249`
- formats[0].format is read unconditionally; if the surface reports zero formats this is out-of-bounds. If no R8G8B8A8 or B8G8R8A8 Unorm format matches, the function falls through to UNREACHABLE_MSG. surface_format is declared without an initialiser (vk_swapchain.h:119).
- **Fix:** ASSERT !formats.empty() before the access. Value-initialise surface_format in the header.

### [low] DC TAA debug dump dereferences possibly-null guest pointers
- id `vk-renderer-4` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:1541`
- Under BB_DC_TAA_DUMP, the 'first 3' branch reads b0.base_address and b1.base_address as float* and indexes f0[64..99] and f1[96..119] with no null or size check against GetSize(). The 'every n' branch does check base_address. Debug-only (env-gated), but it crashes or reads out of bounds if the sharp is smaller than 100 floats.
- **Fix:** Guard on base_address != 0 and GetSize() >= 100*4 (and >= 120*4 for b1). Or delete this research code.

### [low] Stale header comment and logging after draw-prep was disabled by default
- id `vk-renderer-5` · status **confirmed** (none,low) · `runtime-linux/gpu/shadps4/video_core/renderer_vulkan/vk_draw_prep.cpp:54`
- DefaultWorkerCount() now returns 0, so DrawPreparation::Enabled() is false by default and Build/BeginSubmission/Collect do nothing. vk_draw_prep.h still describes the system as active ('one per spare hardware thread'). The constructor always prints 'draw preparation workers: 0 (+1 scanner ...)' even though no scanner is started. The stated reason (
- **Fix:** Fix the log line, and document in vk_draw_prep.h and docs/parallel_gpu.md that the feature is opt-in and unsafe for Driveclub. Alternatively gate the env override behind a warning.
- *Improvement:* Remove or compile out Driveclub research/debug code in the hot path - vk_rasterizer.cpp holds many BB_DC_* env-gated blocks (TAA dump, TAA_ZERO, RES_SCAN, BLUR_DEBUG) with function-local statics. They add dead branches a
- *Improvement:* Replace device.waitIdle() inside Swapchain::Destroy with per-swapchain fences, or pass oldSwapchain - Every resize or HDR toggle does a full-device waitIdle and creates the new swapchain with oldSwapchain=null. This stalls the whole pipeline and risks 
- *Improvement:* Re-enable draw preparation safely, or delete it - About 500 lines of scanner and worker code is dead by default. The root cause (workers mis-parsing dynamic vertex inputs for Driveclub) should be capt
- *Improvement:* Make the DLSS path fallback explicit in RunDriveclub - The owner removed the `provider == UpscalerDlss` bail-out, so when DLSS is selected but unavailable or failed (UseDlss() false) the code silently runs
- *Improvement:* Use the per-stage subgroup helper in the compute pipeline too - vk_compute_pipeline.cpp:28 relies on the default eCompute argument. It is correct today, but passing the stage explicitly keeps the two call sites sym
- *Improvement:* Create the BB_DUMP_DIR directory in DumpRaw - DumpRaw writes to out/dump by default and silently drops the dump if the directory does not exist, because fopen failure is ignored. A mkdir -p and an

## scripts-shell

_The Driveclub-specific shell (setup.sh, run_driveclub.sh, play.sh) is mostly well-written: set -euo pipefail, array-based launch prefix, hardware detection with fallbacks. build.sh, run.sh and the tools/*.sh helpers are largely unchanged Bloodborne upstream and still carry Bloodborne assumptions. No critical issues; the real defects are a destructive rm -rf in setup.sh, a silently ignored patch failure in build.sh, a broken desktop-entry quoting, dead PCI-ID regexes, and several Bloodborne leftovers (tools/extract.sh, restart.sh, run.sh defaults). The root CMakeLists.txt is macOS-only by design. CI is minimal but valid. No setup.log or large binaries are tracked; .gitignore covers them._


### [medium] setup.sh rm -rf on an existing real game directory
- id `scripts-shell-1` · status **confirmed** (low,low) · `setup.sh:280`
- If game/CUSA00003 exists as a real directory with no eboot.bin (partial copy, user-placed files, failed extraction), setup.sh deletes it recursively before creating the symlink. The only guard is eboot.bin absence at line 215. Anything else the user put in that directory is lost.
- **Fix:** Only remove it if it is a symlink ([[ -L $GAME_DIR ]] && rm -f). If it is a non-empty real directory, abort with a message or move it aside.

### [medium] build.sh silently ignores FSR-Vulkan patch failures on Linux
- id `scripts-shell-2` · status **confirmed** (low,low) · `runtime-linux/build.sh:70`
- The Linux path runs `git apply ... 2>/dev/null || true`. If a patch fails to apply (submodule bumped or dirty), the build continues against unpatched FSR-Vulkan, with no message. The MSYS2 branch (line 18-22) does fail loudly, so the two branches are inconsistent. The same loop does nothing at all if the submodule has no .git (line 68), so a tarbal
- **Fix:** Drop `|| true` and `2>/dev/null` so a failed apply aborts or at least warns. Warn when the submodule is not a git repo.

### [medium] Desktop entry written without quoting or escaping
- id `scripts-shell-3` · status **confirmed** (low,low) · `setup.sh:431`
- Exec=, Icon= and Path= interpolate $PROJECT_ROOT and $ICON_DIR raw. A checkout path containing spaces (or %) makes Exec= split into several arguments or be mis-expanded per the Desktop Entry spec, so the menu launcher does not start. Terminal=false also hides every launcher error message (missing game, missing deps).
- **Fix:** Write Exec="$PROJECT_ROOT/run_driveclub.sh" with `%` escaped as `%%`. Set Path= and Icon= from a sanitized path, or use `bash -c` with quoting. Consider logging launcher output to a file.

### [low] PCI-ID regexes can never match lspci -nn output
- id `scripts-shell-4` · status **confirmed** (low,low) · `run_driveclub.sh:296`
- lspci -nn prints vendor:device as [10de:1e84] and the class as [0300]. The patterns \[1002\], \[10de\] and \[8086\] need a bare bracketed vendor ID, so they are dead code. Detection only works through the name fallback (AMD/Radeon/NVIDIA/Intel), checked in that order. The same pattern is repeated at setup.sh:362-366. The primary sysfs path usually
- **Fix:** Match `\[1002:` / `\[10de:` / `\[8086:` instead.

### [low] GPU vendor picked from the first DRM card or first lspci line, wrong on hybrid-graphics laptops
- id `scripts-shell-5` · status **confirmed** (low,low) · `run_driveclub.sh:283`
- The loop breaks on the first card[0-9]* with a known vendor. On an iGPU + dGPU laptop, card0 is often the iGPU, so the wrong vendor profile is applied: RADV_PERFTEST on an AMD iGPU while the NVIDIA dGPU renders, or the NVIDIA profile skipped. The Arc ReBAR scan then iterates every Intel display device. setup.sh:360 picks the first lspci line the sa
- **Fix:** Prefer a discrete GPU (for example by boot_vga=0, or by honouring a user override such as DC_GPU_VENDOR / MESA_VK_DEVICE_SELECT). Document that the first card wins.

### [low] Bloodborne-era tools/extract.sh depends on a missing, gitignored tool and a hardcoded PKG name
- id `scripts-shell-6` · status **confirmed** (low,low) · `tools/extract.sh:5`
- extract.sh needs tools/pkg_pfs_tool. That path is in .gitignore (line 93) and absent from the tree, so the script cannot work from a clone. It also hardcodes the all-zero passcode and a default rom/EP9000-CUSA00003... filename, and its messages are in Ukrainian. It is not mentioned in setup.sh or any README flow.
- **Fix:** Document or fetch the tool, add a `[ -x "$PKG_TOOL" ]` check with a clear error, translate messages to English, or remove the script.

### [low] tools/restart.sh and soak.sh kill/detect 'bb-probe' only; run_driveclub.sh launches driveclub-probe
- id `scripts-shell-7` · status **confirmed** (low,low) · `runtime-linux/tools/restart.sh:6`
- pkill -x bb-probe matches only processes exec'd through the out/bb-probe symlink (which run.sh does). A game started by ../run_driveclub.sh runs as driveclub-probe and will not be killed. Two instances then run, and soak.sh's pgrep -x bb-probe checks the wrong process. restart.sh also uses stdbuf/setsid with unquoted ${CPUS:+taskset -c $CPUS}.
- **Fix:** Kill both names (pkill -x -e 'bb-probe|driveclub-probe' via pkill -f or two calls), or make the tools use out/bb-probe consistently.

### [low] runtime-linux/run.sh keeps Bloodborne defaults (bbport.ini, CUSA03173) and does not use Driveclub's data/ flow
- id `scripts-shell-8` · status **contested** (none,low) · `runtime-linux/run.sh:44`
- Fallback game dir is ../CUSA03173, which is Bloodborne's title ID. BB_CONFIG defaults to bbport.ini (line 21), while the shim reports saving to dcport.ini and run_driveclub.sh uses data/dcport.ini. run.sh also calls scripts/prepare.py, scripts/patches.py with `--fps uncap`, and never applies the Driveclub patch set. Running run.sh therefore produce
- **Fix:** Either remove the Bloodborne paths and make run.sh a thin dev wrapper around the Driveclub flow, or document it as developer-only. Drop the CUSA03173 fallback.

### [low] Option parsing with set -u crashes cryptically when a value is missing
- id `scripts-shell-9` · status **confirmed** (low,low) · `run_driveclub.sh:53`
- All value options (`--fps`, `--res`, `--readbacks`, `--p-cores`, ...) do `$2` / `shift 2` without checking $#. `./run_driveclub.sh --fps` dies with 'unbound variable', and under shift semantics a missing value is never reported. The same applies to setup.sh `--game-dir` at line 470.
- **Fix:** Check `[[ $# -ge 2 ]] || { echo "--fps needs a value" >&2; exit 2; }`, or use a small helper.

### [low] Invalid DC_GAME_DIR silently falls back to CUSA00093; --fps/--res are not validated
- id `scripts-shell-10` · status **confirmed** (low,low) · `run_driveclub.sh:171`
- If the user's DC_GAME_DIR lacks eboot.bin the script silently substitutes game/CUSA00093 instead of reporting the bad override. It also never checks that game/CUSA00003 is a dangling symlink. Values for --fps/--res/--p-cores go into env or taskset unvalidated, so a bad core list shows up only as a taskset failure at exec.
- **Fix:** Fail if DC_GAME_DIR is set and invalid. Validate numeric/WxH formats up front.

### [low] setup.sh overwrites data/dcport.ini on every run and hard-codes settings
- id `scripts-shell-11` · status **confirmed** (low,low) · `setup.sh:387`
- `cat > $INI_FILE` clobbers the user's saved settings (the overlay saves to dcport.ini, per bbport_overlay.cpp:434) every time setup.sh is re-run, for example with --rebuild. It also hard-codes `upscaler = none` and `readbacks = 1` etc., while run_driveclub.sh separately forces BB_PREP_WORKERS=0 and BB_OBJECT_MOTION=0 as env vars (lines 408, 415) th
- **Fix:** Write the ini only if missing. Keep a single source of truth for the forced values.

### [low] Prepared assets never refreshed when the game directory changes
- id `scripts-shell-12` · status **confirmed** (low,low) · `setup.sh:298`
- prepare_assets is skipped whenever data/boot-linked.bin and content.bin exist, so pointing setup.sh at a different dump (CUSA00093 vs CUSA00003, or a patched version) reuses the old linked binary. Only patches.bin is rebuilt. After a rebuild of the probe, the linked output is also not regenerated.
- **Fix:** Record the game path / eboot hash in a stamp file and re-run prepare when it differs, or offer a --reprepare flag.

### [low] setup.sh dependency check and install hints omit libraries gpu/CMakeLists.txt requires
- id `scripts-shell-13` · status **confirmed** (low,low) · `setup.sh:133`
- Only vulkan, sdl3 and freetype2 are checked, and the printed install commands add only boost. runtime-linux/gpu/CMakeLists.txt also requires fmt, tsl-robin-map, xxhash, Zydis, ffmpeg (libav*), Protobuf, OpenSSL, zlib, and X11 on Linux. CI installs many of these, so a user following the hint fails later in CMake.
- **Fix:** Extend the pkg-config check and distro hint lines to the full set (mirror the CI apt list).

### [low] play.sh is tracked but setup.sh overwrites it, and the tracked copy lacks set -e/exec guard
- id `scripts-shell-14` · status **contested** (none,low) · `setup.sh:440`
- play.sh is tracked in git (commit 990b9bc), yet setup.sh regenerates it with different content (`cd -- ... ` with no check). Running setup.sh dirties the work tree and diverges from the committed copy. Neither version guards `cd`, and exec of ./run_driveclub.sh fails with an unhelpful message if the cd failed.
- **Fix:** Remove the heredoc and keep play.sh as a committed file.

### [low] Root CMakeLists.txt calls project() with OBJC/OBJCXX before rejecting non-Apple hosts
- id `scripts-shell-15` · status **contested** (none,low) · `CMakeLists.txt:3`
- `project(... LANGUAGES C CXX OBJC OBJCXX)` runs before the `if(NOT APPLE) FATAL_ERROR`. On Linux the configure step may fail first with a missing Objective-C compiler error (or succeed in probing), so the intended friendly pointer to the DCport backend is not guaranteed to show. Anyone running cmake at the repo root on Linux, where the real build i
- **Fix:** Do the APPLE check first (project(DriveclubNative LANGUAGES C CXX) then enable_language(OBJC OBJCXX) inside if(APPLE)).
- *Improvement:* Add shellcheck and a lint job to CI - No CI step checks the shell scripts (setup.sh, run_driveclub.sh, build.sh, tools/*.sh). A `shellcheck` step (apt install shellcheck) would catch unquo
- *Improvement:* Harden CI: permissions, timeouts, caching, action pinning - The workflow has no top-level `permissions: contents: read`, no timeout-minutes, and no concurrency cancel. The Linux job compiles Vulkan-Headers and 
- *Improvement:* Share hardware-detection code between setup.sh and run_driveclub.sh - Steam Deck, GPU-vendor and P-core detection are duplicated and already diverged (sysfs-first in the launcher, lspci-only in setup). A sourced lib file
- *Improvement:* Add a --dry-run / DC_DEBUG print of the final env and command line - The launcher exports roughly 25 BB_*/vendor variables and a taskset/gamemode prefix, and execs. Users filing bug reports cannot easily see the effecti
- *Improvement:* Verify downloads and make setup non-interactive friendly - fetch_fsr4_assets.sh downloads about 100+ binary files from a third-party repo, pinned by commit but with no checksums. setup.sh `read -p` blocks or a
- *Improvement:* Make build.sh Linux path skip the tracked-patch loop gracefully and cache the cmake configure - The configure step reruns on every invocation, and the C probe compile always rebuilds. Using a stamp for submodules/patches and ninja for the probe w

## launcher-py

_The whole runtime-linux/launcher/ directory is unchanged upstream Bloodborne launcher code with no Driveclub adaptation, and nothing in the Driveclub flow (setup.sh, run_driveclub.sh, play.sh, ci.yml) invokes or installs it. It is effectively dead code here, and if run it would misbehave: Bloodborne CUSA03173 defaults, a Bloodborne trophy list, Supermedo update endpoints, and a Windows updater that installs unverified zips. install-desktop.sh and bb-launcher.sh are tiny and not Driveclub-wired. No shell=True was found. Everything else (vdf parser, ini editor, i18n) is sound upstream code. Findings are code-reading only; nothing was executed._


### [high] Windows self-update downloads and runs an unverified zip from Supermedo's Bloodborne repo
- id `launcher-py-1` · status **confirmed** (medium,low) · `runtime-linux/launcher/bbport_launcher_win.py:2134`
- latest_release() queries RELEASES_API (api.github.com/repos/Supermedo/bloodborne_pc, line 46). install_update() downloads the first .zip asset, extractall()s it with no checksum or signature, then launches the extracted Bloodborne.exe with --install-update. install_update() then copytree()s over the installation. Anyone controlling that repo or the
- **Fix:** Remove or repoint the updater to the Driveclub repo, and verify a pinned SHA-256 or signature before extracting or executing. Disable it entirely for the fork.

### [medium] Launcher is not wired into the Driveclub flow and is entirely Bloodborne
- id `launcher-py-3` · status **confirmed** (low,low) · `runtime-linux/launcher/bbport_launcher.py:70`
- setup.sh creates its own driveclub.desktop pointing at run_driveclub.sh and never references runtime-linux/launcher. The launcher defaults game_dir to ../CUSA03173, titles the window 'Bloodborne', labels the folder row 'CUSA03173', and uses application id io.github.bbport.Launcher. The launcher's play() execs runtime-linux/run.sh with BB_* env vars
- **Fix:** Either delete the launcher from the fork, or rebrand it (title, app id, config dir, default game dir, Name= in the .desktop) and route Launch through run_driveclub.sh.

### [medium] install-desktop.sh installs a 'Bloodborne' menu entry and breaks on special characters in the path
- id `launcher-py-4` · status **confirmed** (low,low) · `runtime-linux/launcher/install-desktop.sh:7`
- The .desktop template says Name=Bloodborne / Comment=Native Bloodborne port launcher. The script substitutes the checkout path via sed with '|' as the delimiter and no escaping. A path containing '|', '&' or '\' corrupts the Exec line. The .desktop Exec is unquoted, so a path with spaces fails to launch. The script also installs a menu entry that c
- **Fix:** Quote Exec="@LAUNCHER@" with desktop-entry escaping and escape sed metacharacters, or generate the file with printf. Rebrand or remove the entry.

### [medium] Trophy data and logic are Bloodborne-specific
- id `launcher-py-5` · status **confirmed** (low,low) · `runtime-linux/launcher/bbport_trophies.py:2`
- The module hard-codes the 40 Bloodborne trophies (CUSA03173, DLC 'The Old Hunters') and platinum rule. Driveclub trophy ids and names would be wrong or missing for any save log, and it would award a Bloodborne platinum.
- **Fix:** Replace with the Driveclub trophy set or remove the trophies page.

### [medium] Leftover Bloodborne community/online endpoints and credit links in Windows launcher
- id `launcher-py-6` · status **confirmed** (low,low) · `runtime-linux/launcher/bbport_launcher_win.py:45`
- COMMUNITY_SERVER=https://thehuntersdream.com, SUPERMEDO_SIGNUP and SUPERMEDO_SERVER, and probes to ':20443/bb-eu/ss.info', ':31315/status' and a POST (line 389) send user data (account sign-up or login bodies) to Bloodborne third-party servers, none of which relate to Driveclub. webbrowser credit links point to Awakenchan/BloodBorneButPc.
- **Fix:** Strip the online/account UI and endpoints in the fork, or point to Driveclub infrastructure.

### [medium] FSR4 shader assets fetched over HTTPS from a third-party repo without hash verification
- id `launcher-py-7` · status **confirmed** (low,low) · `runtime-linux/launcher/bbport_launcher_win.py:1809`
- download_fsr4 fetches ~dozens of .spv files from raw.githubusercontent.com/FireBurn/Q2RTX pinned by commit (good), but there is no checksum. Pinning to a commit limits tampering; the files are loaded into the Vulkan driver. Also response.read() has no size limit and a partial .part file is left behind on error.
- **Fix:** Add expected SHA-256 per file and delete .part on failure.

### [low] settings.json loaded without type validation; load_settings crashes on wrong types
- id `launcher-py-9` · status **confirmed** (low,low) · `runtime-linux/launcher/bbport_launcher.py:105`
- settings.update(json.loads(...)) accepts any JSON; a corrupted/hand-edited value (e.g. fps_limit as string, extra_env null, or a non-dict top-level JSON) raises TypeError/AttributeError in game_environment (s['fps_limit'] > 0, s['extra_env'].split()), which is not caught (only OSError/ValueError are). Also play() does Path(settings['game_dir']) whi
- **Fix:** Validate that the result is a dict and coerce known keys to expected types, falling back to DEFAULTS.

### [low] Duplicate dict comprehension in store() and non-atomic writes of config files
- id `launcher-py-10` · status **confirmed** (low,low) · `runtime-linux/launcher/bbport_launcher.py:600`
- The effect_rows comprehension is repeated twice in self.ini.update(...) (harmless duplication). save_settings, save_ini and mods/patches json are written with write_text directly, so a crash mid-write truncates the user's config; load_ini then silently falls back to defaults and the next save overwrites the loss.
- **Fix:** Remove duplicate; write to a temp file and os.replace.

### [low] Windows shortcut creation interpolates paths into a PowerShell command string
- id `launcher-py-11` · status **confirmed** (low,low) · `runtime-linux/launcher/bbport_launcher_win.py:2185`
- target, PORT_DIR, icon and arguments are inserted into a PowerShell script inside double/single quotes without escaping. A install path containing '"', '$' or '`' (or a single quote in the arguments path) breaks the script or enables injection from the directory name. Local self-inflicted, low severity.
- **Fix:** Pass paths via environment variables or -File with parameters, or escape single quotes by doubling them and use single-quoted literals.

### [low] bb-launcher.sh falls back to nix-shell unconditionally and embeds path in a quoted --run string
- id `launcher-py-12` · status **confirmed** (low,low) · `runtime-linux/launcher/bb-launcher.sh:8`
- On non-Nix systems lacking PyGObject/libadwaita the script fails with 'nix-shell: command not found' instead of a useful message. The nix-shell --run string wraps $here in single quotes, so a path containing a single quote breaks; also user arguments ("$@") are dropped on this path.
- **Fix:** Check command -v nix-shell and print install hints; pass args with printf %q.

### [low] Steam shortcut self-test and AppName hard-coded to Bloodborne; Windows-only winreg
- id `launcher-py-14` · status **contested** (none,none) · `runtime-linux/launcher/bbport_steam.py:84`
- The module is only usable on Windows (winreg import in steam_folder; tasklist in steam_running). Its CLI self-test hard-codes Bloodborne and add_shortcut writes a .bak then overwrites shortcuts.vdf non-atomically, while Steam may be running (steam_running check is left to the caller). Not applicable to the Linux build.
- **Fix:** Remove from the Linux fork or add a Linux Steam path (~/.local/share/Steam) if Steam shortcuts are wanted.
- *Improvement:* Remove or fully rebrand runtime-linux/launcher - It is untouched Bloodborne code, unused by setup.sh and run_driveclub.sh, and ships a Bloodborne desktop entry and trophy list. Either delete it or re
- *Improvement:* Disable the auto-update and online endpoints in the fork - The GitHub release check and the community server probes contact Supermedo/Bloodborne services. A fork should not phone home to or auto-install from t
- *Improvement:* Add checksums for any downloaded artifacts - Both the update zip and the FSR4 shaders are fetched without hash verification. Pin SHA-256 values in the source.
- *Improvement:* Write config files atomically and validate types - Use tmp+os.replace and schema-check settings.json so a corrupted file cannot crash the launcher or reset settings.
- *Improvement:* Make the desktop installer robust and consistent with setup.sh - setup.sh already writes driveclub.desktop with a heredoc. Reuse that, quote Exec, and avoid sed substitution of arbitrary paths.
- *Improvement:* Add a minimal test for the launcher modules if they are kept - CI covers only windows/launcher. bbport_steam has a self-test and bbport_input_config / ini load/save have none run in CI.

## windows-launcher

_Python/Tk launcher (dcport), prepare tools, and the Driveclub.xml patch set. The launcher is generally careful (atomic settings write, zip path check, rollback on update install), but there is one confirmed functional bug: two shipped patches cannot be compiled because of an address parsing mismatch, and no test exercises compilation against the real XML. Updater integrity depends on an optional GitHub digest. The Linux path applies the same fixed-tickrate patch as the Windows default, but without the launcher's FPS gating._


### [high] Barrier Disable / Reset Disable patches fail to compile (int(...,0) on '00FD4149')
- id `windows-launcher-1` · status **confirmed** (low,medium) · `windows/launcher/prepare/dc_patches.py:47`
- dc_patches parses Address with int(x, 0), but Driveclub.xml has two patches with unprefixed zero-padded hex addresses (lines ~96 and ~106: '00FD4149', '00D75063'). Python base 0 rejects these (ValueError), whereas dc_prepare.py:46 parses the same attributes with base 16, so the two tools disagree. Selecting either toggle in the PATCHES page makes p
- **Fix:** Parse with a shared helper: int(addr, 16) after stripping optional 0x (as dc_prepare does), or fix the XML to 0x00FD4149 / 0x00D75063. Add a test compiling every Metadata in Driveclub.xml.

### [medium] One bad toggle aborts all patches and leaves stale patches.bin in use
- id `windows-launcher-2` · status **confirmed** (low,low) · `windows/launcher/dcport/play.py:131`
- On build error, run() only print()s the message (no stdout in the frozen GUI exe) and launches with whatever patches.bin existed before, which may include patches the user has since turned off (e.g. the previous 60 FPS/deltatime choice) or lack the new timing patch. The user is never shown the error.
- **Fix:** Delete or truncate patches.bin on failure, and surface the error via show_error/log instead of print.

### [medium] Update integrity is optional: no digest means no verification
- id `windows-launcher-3` · status **confirmed** (low,low) · `windows/launcher/dcport/updater.py:80`
- Release.sha256 is '' when GitHub's asset has no 'digest' field, and download() then only checks size (also skipped when size is 0). The unverified zip is then extracted and its DriveClub.exe/bin/DriveClub.exe swapped in and run. The digest comes from the same API response/origin as the asset, so it only guards corruption, not a compromised release;
- **Fix:** Refuse to install when no sha256 is available (or fetch a checksum file), and ideally verify a detached signature.

### [low] Invalid-UTF-8 dcport.ini crashes runtime env building
- id `windows-launcher-4` · status **confirmed** (low,low) · `windows/launcher/dcport/runtime.py:33`
- _ini_value catches only OSError, but ini._lines uses read_text(encoding='utf-8') which raises UnicodeDecodeError (a ValueError) on a file saved in ANSI by a text editor. play.py's _default_fps_counter catches ValueError, but game_env/player_name/game_language do not, so --play and the launcher crash with a traceback instead of defaulting.
- **Fix:** Catch (OSError, ValueError) in _ini_value, or make ini._lines use errors='replace'.

### [low] update_ini rewrites the config non-atomically
- id `windows-launcher-5` · status **confirmed** (low,low) · `windows/launcher/dcport/ini.py:44`
- update_ini opens dcport.ini with 'w' (truncating) and writes in place, while settings.save_settings uses temp+os.replace. A crash/power loss or a concurrent read by the running game (BB_CONFIG, in-game menu also writes it) can see an empty or partial file; concurrent writers from launcher and game race with lost updates.
- **Fix:** Write to a temp file in the same directory and os.replace, as settings.py does.

### [low] Fixed-tickrate patch applied unconditionally on Linux, unlike Windows launcher gating
- id `windows-launcher-6` · status **contested** (none,low) · `setup.sh:316`
- setup.sh (and dc_prepare.PATCHES) always compile '60 FPS with fixed tickrate' (0x01762801 -> 0x67), while the Windows launcher omits it at fps==30 and offers the deltatime alternative. run_driveclub.sh sets BB_FPS_LIMIT=45 on Steam Deck and accepts --fps 30/45, but patches.bin is compiled only in setup (fixed 60 Hz engine tick). patches.bin is also
- **Fix:** Share the patch-selection logic (or an env/flag) between platforms and document the intended Deck 45 FPS behavior; recompile patches.bin in run_driveclub.sh when the XML is newer.

### [low] Prepared-data stamp ignores patch selection and prepare logic changes
- id `windows-launcher-7` · status **contested** (none,low) · `windows/launcher/prepare/stamp.py:9`
- source_stamp uses only size and int(mtime) of eboot, runtime exe and the XML; changes to the prepare tool itself or to the module list are not detected, and mtime-based comparison breaks when a zip update preserves older mtimes with identical size. Also dc_prepare.py computes red-zone-avoid addresses from every 01.28 patch but any later XML edit re
- **Fix:** Hash the files (or include a prepare-tool version constant) in the stamp.

### [low] Tests never compile the real Driveclub.xml; CI runs on Windows only
- id `windows-launcher-9` · status **confirmed** (low,low) · `windows/launcher/tests/test_patches.py:16`
- Tests only check that names exist in the XML; none call dc_patches.compile_patches or parse every Address, which is why WL-1 went unnoticed. The Linux-side use of dc_patches/dc_prepare is only exercised by setup.sh, and the CI job windows-launcher runs on windows-latest only.
- **Fix:** Add a test compiling each Metadata block (and the Linux default) into a BBPATCH2 blob and checking offsets/lengths; run pytest for prepare/ on ubuntu too.
- *Improvement:* Share one address/patch parser between dc_patches and dc_prepare - The two tools parse Address differently (base 0 vs base 16), the root of WL-1; one helper plus XML schema validation removes the class of bug.
- *Improvement:* Require checksum verification (and surface a hash-missing state) in the updater - Install only when a sha256 is present; show the failure to the user; consider signed releases.
- *Improvement:* Make run_driveclub.sh drive patch selection like the Windows launcher - Linux hardcodes one patch at setup time; recompiling per run from --fps (and offering toggles) would unify behavior and avoid stale patches.bin.
- *Improvement:* Atomic write for dcport.ini - Use temp+os.replace like settings.py to avoid corrupt config and partial reads by the game.
- *Improvement:* Show patch-build errors in the UI instead of print() - The GUI exe has no console, so users never learn that toggles were not applied.
- *Improvement:* Add a Linux CI job for the python prepare/ tests - Linux users depend on dc_prepare/dc_patches via setup.sh but CI only exercises them on Windows.

## bbnet-dlss

_bbnet is compiled and linked into every Driveclub build: gpu/CMakeLists.txt adds the `bbnet` static library unconditionally and links it into bbgpu with no option to turn it off. It is runtime-gated, though. runtime_services_resolve (src/runtime_services.c:570-572) only routes sceNet/sceNp/sceHttp/sceSsl imports to it when BB_ONLINE=1. bbnet_initialize and BbThd::Enabled need BB_ONLINE=1 and BB_NET_BACKEND=thd. I found nothing in setup.sh, run_driveclub.sh, play.sh or CI that sets BB_ONLINE. The shadNet path has no usable default server (it takes BB_SHADNET_SERVER, empty if unset), so it does not phone home by default. The Bloodborne leftover that can leak is the `thd` backend. If a user exports BB_ONLINE=1 and BB_NET_BACKEND=thd, it hard-defaults to https://thehuntersdream.com:18671 and https://thehuntersdream.com, sends requests even when not signed in, and attaches the X-BB-Token header to all of the game's sceHttp traffic. The Bloodborne-specific memory poking in SetImage is guarded by a 1.09 prologue check and is inert for Driveclub. TLS verification is on by default; only BB_THD_VERIFY_TLS=0 turns it off. bbnet/vendor is mostly unchanged shadPS4 0.20.0, and I did not audit it except for the bbport hooks. The first-party `thd` code and glue are reasonable, but they have real gaps: a blocking P2P recv that cannot be woken, an unbounded datagram queue, and a use-after-close fd race in the punch thread. The build also has a dependency gap that breaks setup.sh users. The DLSS bridge is a Windows-only MSVC DLL built separately, never reached on Linux, and has only minor robustness issues._


### [high] setup.sh omits dependencies that gpu/CMakeLists.txt requires unconditionally
- id `bbnet-dlss-1` · status **confirmed** (medium,medium) · `runtime-linux/gpu/CMakeLists.txt:171`
- The Bloodborne online stack is a mandatory build dependency. CMake has find_package(Protobuf REQUIRED), find_package(OpenSSL REQUIRED), find_package(ZLIB REQUIRED), and on non-WIN32 a hard link to `miniupnpc` from /usr/include/miniupnpc. The dependency lists in setup.sh (lines 145-155) name none of protobuf, miniupnpc, openssl or zlib. CI installs
- **Fix:** Add protobuf, miniupnpc, openssl and zlib dev packages to every distro line in setup.sh. Better, add `option(BB_ONLINE_SUPPORT ... OFF)` and skip bbnet, along with the bbnet_register call, for Drivecl

### [medium] bbnet (Bloodborne network stack) is built and linked unconditionally into Driveclub
- id `bbnet-dlss-2` · status **confirmed** (low,low) · `runtime-linux/gpu/CMakeLists.txt:235`
- `target_link_libraries(bbgpu PUBLIC bbnet)` has no CMake option. bbgpu_register_kernel() always calls bbnet_register() (gpu/shim/bbgpu.cpp:276). Driveclub's build therefore always carries GPL-3 thd code, the hard-coded thehuntersdream.com defaults, and the Bloodborne-1.09 memory patcher. The protobuf, OpenSSL, miniupnpc and httplib dependencies als
- **Fix:** Gate bbnet behind `option(BB_NET OFF)` for Driveclub and stub bbnet_* functions when it is off.

### [medium] thd backend contacts thehuntersdream.com even when not signed in
- id `bbnet-dlss-3` · status **confirmed** (low,low) · `runtime-linux/gpu/bbnet/thd/thd_session.cpp:261`
- With BB_ONLINE=1 and BB_NET_BACKEND=thd, Initialize() only logs "the game stays offline" when SignedIn() is false (line 149-155). NpPost/NpGet, the STUN refresh, the event poller and the keepalive have no SignedIn() gate; SignedIn() is checked only in np_get_state. ServerContextStart (line 626) sends OnlineId ("Player"), the LAN IP, the P2P port an
- **Fix:** Return failure from NpPost/NpGet/P2pStun/PollerStart when !SignedIn() (or when BB_THD_SERVER is unset). Remove the hard-coded defaults, or refuse non-Bloodborne titles (check Common::ElfInfo serial).

### [medium] Account token is attached to every sceHttp request the game makes, to any host
- id `bbnet-dlss-4` · status **unverified** · `runtime-linux/gpu/bbnet/thd/thd_session.cpp:274`
- AddGameHeaders adds X-BB-Token and X-BBHost-Ruleset, and appends the client string to the User-Agent. It is called from sceHttpSendRequest (vendor-changes.diff hunk at http.cpp:1760) for every request when thd is enabled, with no host check. Any non-THD host the game contacts (Sony/Evolution/CDN endpoints, or whatever Driveclub requests) receives t
- **Fix:** Pass the destination host and add the token only when it equals HostOf(np_base) or HostOf(auth_base).

### [medium] Blocking P2P recv cannot be woken by close or abort; sceNetSocketAbort is a no-op
- id `bbnet-dlss-5` · status **unverified** · `runtime-linux/gpu/bbnet/thd/thd_net.cpp:530`
- dgram_recv for a P2P socket does `q.cv.wait(lk, [&]{ return !q.q.empty(); })` on a copy of the shared SockQueue and holds no reference that close can signal. net_socket_close (line ~690) erases the socket and detaches it from the port without notifying q->cv or setting a closed flag. net_socket_abort (line ~705) only checks that the socket exists.
- **Fix:** Add an `aborted/closed` flag to SockQueue, set it in close/abort and notify_all, and make the wait predicate also return on the flag (EBADF or EINTR to the caller).

### [medium] Per-socket datagram queue is unbounded and fed by unauthenticated UDP
- id `bbnet-dlss-6` · status **unverified** · `runtime-linux/gpu/bbnet/thd/thd_net.cpp:498`
- p2p_reader binds INADDR_ANY and pushes every datagram from any source into q->q with no cap. A datagram with no or an unknown vport is also forwarded to the first bound vport (the `forwarded` path). If the game is not reading, or a remote host floods the P2P port (default 3658) with 1-2 KB packets, host memory grows without limit.
- **Fix:** Cap the queue (for example 256 datagrams, drop oldest or newest). Drop non-peer sources when the peer table is known.

### [low] Detached hole-punch thread uses port->fd after the port may be closed
- id `bbnet-dlss-7` · status **unverified** · `runtime-linux/gpu/bbnet/thd/thd_net.cpp:1159`
- P2pPunch spawns a detached thread that calls sock_sendto(port->fd, ...) for up to 20 s. p2p_detach sets stop, joins only the reader, closes the fd and sets port->fd = kBadSock with no synchronisation against the punch thread. The thread can therefore send to a closed or reused fd number. The stop flag is checked only once per 500 ms loop. Probe tar
- **Fix:** Track the punch threads and join them in p2p_detach, or guard fd use with port->mu. Reject private, loopback and multicast targets from remote-supplied local_addr unless on the same LAN.

### [low] BB_THD_VERIFY_TLS=0 silently disables certificate verification for the token-bearing session
- id `bbnet-dlss-8` · status **unverified** · `runtime-linux/gpu/bbnet/thd/thd_session.cpp:86`
- `c.verify_tls = Env("BB_THD_VERIFY_TLS") != "0"` is the only switch, and nothing is logged when it is off. BB_THD_SERVER/BB_THD_AUTH may also be plain http://, so X-BB-Token is sent in cleartext with no warning. Verification is otherwise on, and hostname verification uses httplib's default.
- **Fix:** Log a prominent warning when verification is disabled or the base URL is not https, and refuse to send the token over plain http to non-loopback hosts.

### [low] P2pStun/DetectLanAddress call getaddrinfo on a possibly-attacker-controlled host and ignore failure of the blocking resolve in the poller path
- id `bbnet-dlss-9` · status **unverified** · `runtime-linux/gpu/bbnet/thd/thd_net.cpp:1063`
- MappedRefresh runs P2pStun up to 3 times with a 1 s wait each, and each call does a blocking getaddrinfo (no timeout) while holding g_mapped.mu. Both the keepalive thread and ServerContextStart (called from the game's matching thread) take that mutex, so a slow or dead DNS server stalls the game's context_start for as long as the resolver takes.
- **Fix:** Resolve once and cache, outside g_mapped.mu. Do not run the STUN loop on the game's callback thread.

### [low] p2p_port_open can wrap the 16-bit port when scanning for a free port
- id `bbnet-dlss-10` · status **unverified** · `runtime-linux/gpu/bbnet/thd/thd_net.cpp:518`
- `static_cast<std::uint16_t>(want + i)` wraps past 65535 if BB_P2P_PORT is set near the top of the range, so it ends up binding low (privileged or unexpected) ports. Cfg() accepts up to 65535.
- **Fix:** Stop the loop when want + i > 65535. Also clamp BB_P2P_PORT so want + 15 <= 65535.

### [low] Two divergent EmulatorSettings headers with different defaults (UPnP, server)
- id `bbnet-dlss-11` · status **unverified** · `runtime-linux/gpu/shim/core/emulator_settings.h:80`
- The shim header defaults IsUPnPEnabled to true (BB_UPNP default true) and the shadNet server to 127.0.0.1:31313. The bbnet/include header defaults UPnP to false and the server to empty. Which one a vendored net/np TU sees depends on include order. bbnet lists bbnet/include before shim, so bbnet gets the safe one, but any TU that picks up the shim h
- **Fix:** Change the shim default to false, or delete the duplicated net settings from the shim.

### [low] DLSS bridge: state is left inconsistent after a failed Initialize, and Shutdown does not reset it
- id `bbnet-dlss-12` · status **unverified** · `runtime-linux/gpu/dlss_bridge/bbport_dlss_bridge.cpp:119`
- Initialize() sets state.initialized = true immediately after NGX init, then returns 0 on capability, needs-driver or unavailable failures. A caller that treats 0 as 'not initialised' and skips Shutdown() leaks the NGX context, and a retry returns 0 immediately at `state.initialized` being true. Shutdown() leaves state.configured and state.feature_d
- **Fix:** Call Shutdown() on every failure path in Initialize, or set initialized only after all checks pass. Reset the whole State in Shutdown.

### [low] Hard-coded NGX project id borrowed from the shadps4_dlss project
- id `bbnet-dlss-13` · status **unverified** · `runtime-linux/gpu/dlss_bridge/bbport_dlss_bridge.cpp:19`
- ProjectId is a fixed GUID inherited from the upstream Bloodborne DLSS bridge, with EngineVersion "bbport-windows". NGX uses the project id for application identification and driver-side telemetry, so Driveclub sessions are attributed to the Bloodborne project. This is also Windows-only code that Linux builds never use.
- **Fix:** Register a Driveclub-specific NGX project id, or document that the id is intentionally shared.

### [low] dlss_bridge/CMakeLists does not validate VULKAN_INCLUDE
- id `bbnet-dlss-14` · status **unverified** · `runtime-linux/gpu/dlss_bridge/CMakeLists.txt:23`
- Only DLSS_SDK_ROOT is checked with FATAL_ERROR. VULKAN_INCLUDE defaults to an empty string and goes straight into target_include_directories, so a misconfigured build fails later with a confusing missing `vulkan/vulkan.h` error.
- **Fix:** Add `if (NOT EXISTS "${VULKAN_INCLUDE}/vulkan/vulkan.h") message(FATAL_ERROR ...)`.
- *Improvement:* Add an off-by-default CMake option (BB_NET) for bbnet and stub out bbnet_* when off - Driveclub has no use for the Bloodborne online stack. Removing it from the default build drops the protobuf/OpenSSL/miniupnpc/zlib requirement (F1), t
- *Improvement:* Make the thd backend refuse to run for non-Bloodborne titles - Check Common::ElfInfo game_serial/title at BbThd::Enabled(), or refuse when SetImage's 1.09 prologue check fails. The Driveclub eboot would otherwise 
- *Improvement:* Print a one-line startup banner naming every remote endpoint when BB_ONLINE=1 - The user would see exactly which hosts will be contacted (np_base, auth_base, STUN host, shadNet server). The current logs only mention the server whe
- *Improvement:* Document BB_ONLINE/BB_NET_BACKEND/BB_THD_* as Bloodborne-only in the repo README and UPGRADE_PLAN.md - The runtime-linux README and launcher still describe Bloodborne online play, and the Driveclub docs never say it is unsupported or what it would conta
- *Improvement:* Add regression tests for the P2P queue and relay framing - p2p_header, the relay framing and the STUN parser are pure functions with several length-sensitive branches, and tests/test_bridge.py does not cover t
- *Improvement:* Fix the weak error-check mutex emulation in bbnet_glue - posix_pthread_mutex_unlock does not verify ownership and decrements depth unconditionally, so an unlock without a lock underflows depth. cond_wait on 

## gpu-shim

_The shim is glue code (SDL3 window thread, ImGui overlay, ini settings, copy thread pool, kernel-service thread) and is mostly Bloodborne-port code with light Driveclub edits (dcport.ini name, DC overlay mode). The Linux build has no process-memory patching: bloodborne_cam.cpp only patches guest code under _WIN32, and its Linux path just returns false, so Bloodborne addresses cannot corrupt Driveclub memory on Linux. They remain a hazard in the Windows build. I found no memory-safety bug reachable in normal Linux play. The real defects are a lost-wakeup race in the kernel timer service, several unsynchronised shared variables (wheel pulses, overlay flags), an off-thread SDL call polled every 1 ms for a feature that cannot work on Linux, and settings handling that loses data. The rest are Bloodborne leftovers and robustness issues. bbport_command_buffers.h, bbport_threads.h and bbport_process_memory.* looked correct. In bbport_process_memory the 'private_bytes' value is really the process virtual size, but it is documented as such and only used in a log line._


### [medium] Lost wakeup in kernel service thread can strand equeue timers
- id `gpu-shim-1` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbgpu.cpp:312`
- KernelServiceThread runs io_context.run(), then io_context.restart(), then sets asio_requests = 0 with no lock. KernelSignalRequest increments the counter under m_asio_req, after the caller has already queued a timer. If a request arrives after run() has returned but before the reset, the new timer sits in io_context with nobody running it. The res
- **Fix:** Reset the counter under m_asio_req before calling run(), then run until the io_context is empty and re-check the counter under the lock before sleeping. Alternatively, keep an asio executor_work_guard

### [medium] BbMouse sampler thread calls SDL mouse API off the video thread, and on Linux it does nothing useful
- id `gpu-shim-2` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbport_mouse.cpp:31`
- The constructor in window.cpp:67 always starts BbMouse. On Linux the thread wakes every 1 ms for the whole session (ThreadMain). It calls SDL_GetRelativeMouseState from a thread other than the one pumping SDL events (window thread), and SDL's mouse state is not thread-safe. Its only consumer is BloodborneCam::OnMouseMotion, which is a no-op on non-
- **Fix:** Do not start BbMouse (or build bloodborne_cam/bbport_mouse) when not _WIN32. If kept, sample on the window thread inside PollEvents.

### [medium] Bloodborne camera hook on Windows scans all committed regions of 80-82 MB and patches unverified sites
- id `gpu-shim-3` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bloodborne_cam.cpp:296`
- This affects the Windows build only; the Linux build is unaffected. SetEnabled(true) finds the camera by scanning every committed region of 0x5000000-0x5200000 bytes for an 18-byte vmovss sequence. It then derives four more patch addresses from the Bloodborne 1.09 offsets (ADDR_CAM_*_ORIG, bloodborne_cam.cpp:18-23). Only the first site's bytes are
- **Fix:** Compile the class out for Driveclub (or make it a stub), or verify each NOP site against saved expected bytes before patching. Gate on the Bloodborne title ID or serial from BbGpuConfig.

### [low] Cross-thread unsynchronised access to wheel pulse expiry array
- id `gpu-shim-4` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/window.cpp:157`
- g_wheel_expiry_ms is written on the window thread in the event loop (lines 157-160) and by fill(0) (line 263), both outside mouse_mutex. TakeMouseInput (line 271, any thread: the pad thread through bbgpu_mouse_take) reads it under mouse_mutex only. The comment says 'window-thread only' (line 18), which is wrong, so this is a data race on a plain ui
- **Fix:** Make the elements std::atomic<uint64_t>, or perform the writes while holding mouse_mutex.

### [low] Overlay flags read from other threads without synchronisation
- id `gpu-shim-5` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbport_overlay.cpp:720`
- text_entry_active and initialized are plain bools. text_entry_active is written under imgui_mutex on the window thread (SetTextEntry, line 728). Visible() (present thread) and CapturesInput() (pad thread through bbgpu_overlay_captures_input) read both without the lock. initialized is written under the lock in Init (present thread) and read in Visib
- **Fix:** Declare them std::atomic<bool> like menu_open.

### [low] Settings changed in the menu are lost when quitting from the menu
- id `gpu-shim-6` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbport_overlay.cpp:422`
- Changes only set dirty = true and are written by SetOpen(false) when the menu closes (lines 78-81). The 'Quit game' button pushes SDL_EVENT_QUIT while the menu is still open. The window thread then calls std::_Exit(0) (bbgpu.cpp:255) with no flush and no Save(), so every change made in that menu session is dropped.
- **Fix:** Call SetOpen(false) or BbSettings::Save() before pushing the quit event. Also save on the window-close path.

### [low] Window close ends the process with _Exit(0) from the window thread, skipping all teardown
- id `gpu-shim-7` · status **contested** (none,low) · `runtime-linux/gpu/shim/bbgpu.cpp:250`
- When PollEvents returns false the thread logs, flushes stdout and calls std::_Exit(0). It never runs ~WindowSDL (so BbMouse::Stop and BloodborneCam::Shutdown never run, and on Windows the camera hook is never restored), never saves dirty settings, and never lets the Vulkan device, pipeline cache or guest threads shut down. The exit code is always 0
- **Fix:** Request an orderly shutdown (a flag the main loop honours, or at minimum Save() and a pipeline-cache flush) before exiting.

### [low] 'Apply and restart' execs a run.sh that does not exist for the Driveclub launch
- id `gpu-shim-8` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbport_overlay.cpp:401`
- The button calls runtime_restart(). On Linux this does execlp("bash","bash","run.sh") in the current directory (src/probe.c:497-501). The Driveclub launcher run_driveclub.sh cd's to the repo root, where no run.sh exists (only runtime-linux/run.sh), and it launches driveclub-probe directly. A restart would kill the game with _exit(1) instead of rela
- **Fix:** Make runtime_restart use a configurable command (for example BB_RESTART_COMMAND on Linux too, set by run_driveclub.sh), or hide the button in dc_menu mode.

### [low] ini parser: no key/value trimming and 256-byte line buffer splits long lines
- id `gpu-shim-9` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbport_settings.cpp:99`
- Load() only trims trailing whitespace, so 'upscaler = fsr3' or an indented key silently does nothing. Values are matched exactly (for example the output_res string). fgets with char line[256] splits longer lines, and the tail is then parsed as a new line, which could assign a key from a fragment (a long comment containing '='). Parsing uses atoi/at
- **Fix:** Trim both sides of key and value, read whole lines with std::getline, and warn on unknown keys or bad values (strtol with end-pointer checks).

### [low] Settings Save is non-atomic and runs under the ImGui mutex
- id `gpu-shim-10` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbport_settings.cpp:220`
- Save() reads the foreign lines, then fopen("w") truncates dcport.ini and writes in place. A crash or kill mid-write leaves a truncated config. Write errors (disk full) are never checked, since fprintf and fclose return values are ignored. It is called from SetOpen() while imgui_mutex is held (overlay.cpp:78-81, via the window thread or the present
- **Fix:** Write to Path()+".tmp", fsync or fclose-check, then rename(). Do the save outside the lock.

### [low] Bloodborne effects/cheat table is still persisted and restart-tracked for Driveclub
- id `gpu-shim-11` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbport_settings.h:28`
- The Effects[] table (Bloodborne patches: cheat_no_death, cheat_stealth, tweak_ragdoll, debug_camera, quit_to_desktop, skip_intro and so on) is loaded, written to dcport.ini on every Save (settings.cpp:241-244) and compared for the 'restart required' banner (overlay.cpp:395-397). The menu section is hidden unless BB_BLOODBORNE_MENU is set (overlay.c
- **Fix:** Remove or compile out Effects, model_lod and the Bloodborne menu block for the Driveclub build. Unify the default ini name.

### [low] Write-log fault dump dereferences arbitrary guest registers inside the signal path
- id `gpu-shim-12` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/bbport_write_log.cpp:123`
- With BB_WRITE_LOG set, bbgpu_dump_guest_writes (called from the SEGV handler) memcpy's 8 bytes at rax-0x30..rax+0x60 with no mapping check. A wild rax (the very fault being diagnosed) causes a nested SIGSEGV in the handler. It also uses fprintf (not async-signal-safe). Elapsed time is computed with a hard-coded 3.0e9 TSC rate (line 115), and the fi
- **Fix:** Check with runtime_memory_is_mapped before reading, use runtime_tsc_frequency() for scaling, and drop or parameterise the Bloodborne pattern.

### [low] Window creation failures abort instead of reporting
- id `gpu-shim-13` · status **confirmed** (low,low) · `runtime-linux/gpu/shim/window.cpp:60`
- On any SDL video driver other than x11 or wayland (wayland-less kmsdrm, offscreen, a dummy driver in CI or containers), the constructor hits UNREACHABLE_MSG, which aborts. The same goes for SDL_InitSubSystem failure and window creation failure (ASSERT_MSG). These run on the detached window thread while bbgpu_init waits on g_window_cv, so the user g
- **Fix:** Catch the failure on the window thread, set an error flag, notify the cv and make bbgpu_init return nonzero with a message.

### [low] BbCopy::WaitAll spins with yield() while waiting for tasks held by other threads
- id `gpu-shim-14` · status **contested** (low,none) · `runtime-linux/gpu/shim/bbport_copy.cpp:77`
- When there is no queued task to help with but pending > 0 (tasks running on workers) or draining is true, the waiter loops on std::this_thread::yield(), taking the mutex each iteration. This burns a core during long copy bursts (the 50-400 MB streaming uploads this module exists for). A callback running during draining that itself calls WaitAsync w
- **Fix:** Wait on a condition_variable signalled from Complete() when pending reaches 0 and draining clears.

### [low] Text-input poll truncates at a byte boundary and trusts the out pointer
- id `gpu-shim-15` · status **contested** (low,none) · `runtime-linux/gpu/shim/bbgpu.cpp:359`
- bbgpu_text_input_poll copies min(text.size(), size-1) bytes, which can cut a multi-byte UTF-8 sequence and hand the guest an invalid string. It also dereferences out without a null check when size != 0. It is only called by the loader, so exposure is low.
- **Fix:** Back up n to a UTF-8 code point boundary, and check out != nullptr.
- *Improvement:* Compile out Bloodborne-only pieces on the Linux/Driveclub build - bloodborne_cam, bbport_mouse, the Effects table and the BB_BLOODBORNE_MENU block are dead code on Linux, and F2 and F3 stem from them. Removing them s
- *Improvement:* Replace the 1 ms mouse sampler with event-driven input - If a direct mouse camera is ever wanted for Driveclub, accumulate SDL_EVENT_MOUSE_MOTION xrel on the window thread (already done for stick mode) inste
- *Improvement:* Use a proper ini parser with validation and atomic saves - Trim whitespace, use std::from_chars/strtol with end-pointer checks, log unknown keys, write via temp file plus rename, preserve comments. This fixes 
- *Improvement:* Make window/exit lifecycle orderly - Replace _Exit(0) in the window thread with a shutdown request that saves settings, flushes pipeline cache and logs, and propagate init failures from t
- *Improvement:* Back the kernel timer service with an executor_work_guard and stop callback - Removes the request-counter protocol and the F1 lost wakeup in one change: keep io_context running continuously on the service thread and stop it on s
- *Improvement:* Add unit tests for BbCopy ordering and BbSettings parsing - The epoch/AfterCopies drain logic is subtle (ordering, draining handoff) and untested here. The tests directory already has a stub harness (tests/test

## shader-recompiler

_Mostly vendored upstream shadPS4 code. The owner's additions are the LDS bounds check (emit_spirv_shared_memory_bounds.h plus its use in emit_spirv_shared_memory.cpp and emit_spirv_atomic.cpp), the shared_memory_size equality in runtime_info.h, the DenormPreserve mode, and lower_wave64_pass.cpp. The bounds math is sound: the unsigned `offset < size - access + 1` test cannot overflow, the index is always inside the array, out-of-bounds loads and atomics return 0, out-of-bounds writes are dropped, and the OpPhi predecessor handling is valid. The real defects are in how the wave64 lowering interacts with the rest of the pipeline: it keys on the wrong subgroup size, adds a U64 shared type after the simplify pass, and sizes its scratch against the wrong limit. There are also two unchecked guest-controlled array indices in the fetch-shader parser, and the decoder has no end-of-code bounds checks. The static checks all came from reading the code. I did not build or run anything._


### [high] Wave64 lowering keys on the device default subgroup size, but compute pipelines force 64
- id `shader-recompiler-1` · status **confirmed** (medium,medium) · `runtime-linux/gpu/shadps4/shader_recompiler/ir/passes/lower_wave64_pass.cpp:79`
- The pass skips only when `profile.subgroup_size == 64`. That value comes from `instance.SubgroupSize()`, which returns `vk11_props.subgroupSize`, the device default (32 on RADV/NVIDIA/Intel). Separately, `vk_compute_pipeline.cpp:24-28` forces `requiredSubgroupSize = 64` whenever `IsSubgroupSize64Supported()` is true. On such a device the shader is
- **Fix:** Compute the effective subgroup size once. If the compute stage will be created with requiredSubgroupSize 64 (`IsSubgroupSize64Supported()`), set `Profile::subgroup_size` for compute to 64 or pass a pe

### [medium] Wave64 lowering assumes 32-wide subgroups for any non-64 size
- id `shader-recompiler-2` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/shader_recompiler/ir/passes/lower_wave64_pass.cpp:138`
- The pass runs for any `subgroup_size != 64`, including 8 and 16 (Intel), but the indexing is hard-coded for 32: `subgroup_id = invocation_index >> 5`, `offset & ~7u`, and one 32-bit Ballot word per subgroup. `scratch_size` is computed from `profile.subgroup_size`, so with size 16 it is twice as large as the indexing needs. The Ballot low word is on
- **Fix:** Handle only `subgroup_size == 32`. Use a fixed 4-byte slot count of `AlignUp(num_threads, 64) / 32` and log or assert for any other size.

### [medium] Wave64 scratch U64 load is added after SharedMemorySimplifyPass, producing invalid SPIR-V without explicit-layout support
- id `shader-recompiler-3` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/ir/passes/lower_wave64_pass.cpp:160`
- The Ballot lowering emits `ir.LoadShared(64, ...)`. In recompiler.cpp the order is `SharedMemorySimplifyPass` (line 114), `SharedMemoryToStoragePass` (115), then `LowerWave64BallotPass` (140). The pass that turns U64 shared ops into U32 pairs for devices without `supports_workgroup_explicit_memory_layout` has therefore already run. The new U32 scra
- **Fix:** Read the mask as two 32-bit loads (`offset & ~7` and `+4`) and pack them with `PackUint2x32`. That keeps the scratch region U32-only and independent of the simplify pass.

### [medium] Wave64 scratch is added on top of the full guest LDS size even when the guest LDS was moved to a storage buffer
- id `shader-recompiler-4` · status **confirmed** (low,medium) · `runtime-linux/gpu/shadps4/shader_recompiler/backend/spirv/spirv_emit_context.cpp:1072`
- SharedMemoryToStoragePass rewrites all guest shared accesses to a storage buffer. It runs when `shared_memory_size > max_shared_memory_size` or when explicit layout is unsupported and several types are used. `runtime_info.hw.cs.shared_memory_size` is not changed. If the wave64 pass then adds scratch accesses, DefineSharedMemory declares a Workgroup
- **Fix:** Record in `Info` whether the storage pass converted the guest LDS. In that case use a guest base of 0 and size only the scratch region. Alternatively, put the scratch behind the same storage-buffer pa

### [low] LDS bounds limit includes the wave64 scratch, so out-of-bounds guest accesses can read and write the scratch
- id `shader-recompiler-5` · status **confirmed** (low) · `runtime-linux/gpu/shadps4/shader_recompiler/backend/spirv/emit_spirv_shared_memory.cpp:15`
- Every shared load, store and atomic checks against `cs.shared_memory_size + shared_memory_scratch_size`, and the same sum is computed in about 16 places. The scratch starts at `AlignUp(shared_memory_size, 8)`, so a guest access in the alignment pad or in the scratch region passes the bounds check. Real hardware would return 0 for a read there. A wr
- **Fix:** Give guest accesses limit `cs.shared_memory_size`. Have the lowering emit its accesses with a distinct flag or opcode that uses the full size. Also deduplicate the `alloc_size` computation into one he

### [medium] Zero-sized LDS produces an invalid SPIR-V array type
- id `shader-recompiler-6` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/backend/spirv/spirv_emit_context.cpp:1082`
- `shared_memory_size` is `lds_dwords*128*4` and can be 0 (SharedMemoryBarrierPass explicitly handles size 0). A shader with DS ops and zero LDS still reaches `TypeArray(element_type, ConstU32(0))` because `DivCeil(0, n)` is 0. OpTypeArray length must be greater than 0, so the module is invalid. The bounds helper correctly handles size 0 (`valid_star
- **Fix:** Use `std::max(num_elements, 1u)`, or skip the shared variables when the size is 0 and make every access resolve to the out-of-bounds path.

### [medium] Fetch shader parser indexes a 104-entry array with guest-controlled values
- id `shader-recompiler-7` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/frontend/fetch_shader.cpp:93`
- `loads` is `std::array<VsharpLoad,104>`. It is indexed by `inst.dst[0].code` (SMRD sdst, 7 bits, 0..127) on the write at line 93, and by `inst.src[2].code * 4` (5-bit srsrc times 4, up to 124) on the reads at lines 119-120. Values of 104 or more give an out-of-bounds stack write and read. The fetch shader is guest memory, and the parser also uses `
- **Fix:** Validate `dst[0].code < loads.size()` and `base_sgpr < loads.size()`. On a bad value, fail the parse or skip the instruction, rather than writing to the stack.

### [medium] GcnCodeSlice has no bounds checks, so truncated or unterminated shaders read past the buffer
- id `shader-recompiler-8` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/frontend/decode.h:33`
- `at()`, `readu32()` and `readu64()` never compare against `m_end`. `decodeInstruction` reads `at(0)` and the 64-bit encodings read the next dword, literal constants call `readu32()`, and `readu64` also does an unaligned `*(u64*)m_ptr` type pun. `atEnd()` uses `==`, so a truncated final instruction moves the pointer past `m_end`, `atEnd()` never fir
- **Fix:** Use `m_ptr >= m_end` in `atEnd`. Bounds-check the dwords needed in `decodeInstruction` and before each literal read. Use memcpy for `readu64`. Validate that branch targets land on instruction starts i

### [low] Uniformity analysis for wave64 lowering is a narrow heuristic, and non-uniform cases are left unlowered with only a warning
- id `shader-recompiler-9` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/ir/passes/lower_wave64_pass.cpp:14`
- `IsDivergentCondition` treats a condition as divergent only if it depends on LaneId or LocalInvocationId through the IR use graph. Divergence from per-lane data (buffer loads, ballots, derived values) is missed. Any block inside a loop is classed non-uniform. For ReadLane and Ballot in non-uniform blocks the pass only logs a warning and leaves the
- **Fix:** Add uniformity tracking for scalar-register-derived values, and lower in loops when the loop trip count is scalar. For anything still left unlowered, emit the lowering without the barrier path or fail

### [low] Ballot lowering reads uninitialised scratch when the workgroup is not a multiple of 64 threads
- id `shader-recompiler-10` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/ir/passes/lower_wave64_pass.cpp:160`
- The 64-bit load covers the slots of two 32-wide subgroups. If `num_threads % 64 == 32` (for example 96), the last wave has only one subgroup, and the high 32-bit slot is never written. It holds uninitialised Workgroup memory, which corrupts the upper half of the ballot. The scratch size is rounded up to 64 threads, which hides the problem.
- **Fix:** Zero the slots before use, or mask the upper word to 0 when the subgroup count is odd.

### [low] std::ranges::contains requires C++23, and the file does not include <algorithm>
- id `shader-recompiler-11` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/ir/passes/lower_wave64_pass.cpp:102`
- `std::ranges::contains` is a C++23 library feature (libstdc++ 13+, newer libc++). The CI already needed fixes for earlier C++23 code (the deducing-this change in commit 54783c1). The file also uses `std::ranges` and `std::optional` without including <algorithm> or <optional>. It compiles only through transitive includes. The lookup is also an O(n)
- **Fix:** Use `std::ranges::find(...) != end` or a hash set, and include <algorithm> and <optional>.

### [low] Unknown FP32 denorm modes log a warning on every shader compile
- id `shader-recompiler-12` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/backend/spirv/emit_spirv.cpp:474`
- The new `InOutAllow` branch is correct, and SPV_KHR_float_controls is added at line 641. The remaining mode values (such as the in-flush/out-allow combinations) fall into the final else, which logs LOG_WARNING on each compile with no once-guard, unlike the other warnings in this function. This floods the log in games that use those modes.
- **Fix:** Wrap the warning in a `std::once_flag` as the other branches do.
- *Improvement:* Skip the LDS bounds check when the access is provably in range - Every LDS access now splits into three SPIR-V blocks (check, access, merge) plus an OpPhi, even when the offset is an immediate below the size or come
- *Improvement:* Factor out the repeated shared-memory emit boilerplate - Each of about 20 emit functions repeats the same shift, `alloc_size` and lambda pattern. A single helper (access size, element array, offset) plus one
- *Improvement:* Add unit tests for bounds math and the wave64 lowering - There are no tests for `valid_start_count` at the edges (size < access, unaligned offsets, offset near 0xFFFFFFFF), for zero-size LDS, or for the pass
- *Improvement:* Replace ASSERT/UNREACHABLE on guest input with recoverable errors - The decoder, CFG linking (S_SETPC_B64 resolution), the fetch-shader parser and GetScratchVgpr abort the whole emulator on unexpected guest shaders. Re
- *Improvement:* Add a profile-level subgroup-size field per stage - The recompiler profile has a single `subgroup_size`, while the Vulkan side can force 64 per pipeline stage. A per-stage effective size, set from requi
- *Improvement:* Use the GDS flag in the SPIR-V backend or reject GDS ops - The `is_gds` flag on shared ops is read only in resource_pass.h. The SPIR-V backend ignores it, so GDS-addressed DS ops are emitted as LDS accesses an

## python-scripts

_The area is almost entirely unchanged Bloodborne-port code (Supermedo's scripts: prepare, link_*, cpu_compat, patches, mods, quit_menu, hud_layout, run.py), plus unrelated macOS-port helpers in tools/. Driveclub's real prepare pipeline is the forked copy in windows/launcher/prepare (called by setup.sh); runtime-linux/run.sh still drives the Bloodborne scripts. The parsers are generally bounds-checked (span()) and the linkers use atomic_output. The confirmed defects are Bloodborne hardcoding that breaks Driveclub, chiefly the legacy-CPU path, which fails outright for the Driveclub eboot. The tools/ SELF helpers have thin validation, and two env-flag and Linux-portability bugs are confirmed. Nothing here is a memory-safety issue; this is Python with checked slicing._


### [high] Legacy-CPU translation hard-bound to Bloodborne eboot hash; fails for Driveclub on CPUs without BMI1/LZCNT/MOVBE
- id `python-scripts-1` · status **confirmed** (medium,medium) · `runtime-linux/scripts/cpu_compat.py:385`
- patch_image() requires plan['modules'] to equal the linked modules and their SHA256s. legacy-cpu-plan.json pins eboot.bin to d65f0b4f..., which is Bloodborne's. Driveclub's game/CUSA00003/eboot.bin hashes to b0899931.... (libc.prx and libSceFios2.prx hashes do match, so only the eboot differs.) enabled() turns on automatically for any CPU lacking B
- **Fix:** Generate a Driveclub plan (build_cpu_compat.py) and ship it, or detect an unknown eboot hash and skip translation with a loud warning and BB_CPU_COMPAT unset. Also fix the plan path in the windows/lau

### [medium] BB_FORCE_PATCHES=0 is treated as 'force' (truthiness of a non-empty string)
- id `python-scripts-2` · status **confirmed** (low,low) · `runtime-linux/scripts/patches.py:678`
- The env var is read as a string and used as a boolean, so BB_FORCE_PATCHES=0 or false is truthy. That bypasses both the app-version check and the eboot fingerprint check. Byte patches are then written over the wrong code, which is the crash the fingerprint comment at line 511 describes. The same pattern is in run.py.
- **Fix:** Use os.environ.get('BB_FORCE_PATCHES')=='1' in patches.py and run.py.

### [medium] runtime-linux/run.py is Windows-only and crashes on Linux
- id `python-scripts-3` · status **confirmed** (low,low) · `runtime-linux/run.py:116`
- no_console() calls ctypes.windll and subprocess.CREATE_NO_WINDOW, and wait_for() uses kernel32. run_script() calls no_console() on every path, and the probe is looked up as bb-probe.exe, so main() fails on Linux with AttributeError before running anything. The built Linux binary is driveclub-probe. The default game dir is ../CUSA03173. restart uses
- **Fix:** Delete run.py from the Linux tree, or guard with os.name=='nt'. Restart via run_driveclub.sh, using shlex.join.

### [medium] Bloodborne-only filters make Driveclub patches and mods inert in the runtime-linux scripts
- id `python-scripts-4` · status **confirmed** (low,low) · `runtime-linux/scripts/patches.py:583`
- external_patches() drops any third-party XML whose <ID> is not in BLOODBORNE_IDS, so a Driveclub patch file (CUSA00003) is silently ignored. The built-in XML, 01.09 version and AppVer, fingerprint, EBOOT offsets for resolution/ultrawide, FPS_PRESETS names and the quit_menu code check are all Bloodborne's. mods.py content_root() only accepts Bloodbo
- **Fix:** If runtime-linux/run.sh stays, parametrise title IDs, app version, ini name. Otherwise mark these scripts as legacy and point users to windows/launcher/prepare.

### [medium] self2elf.py does not reject encrypted/compressed segments and has no size cap
- id `python-scripts-5` · status **confirmed** (low,low) · `tools/self2elf.py:29`
- self2elf builds from Self() but never checks s.encrypted (elfinfo.py main does). SEG_COMPRESSED (0x8) is not checked anywhere in elfinfo/self2elf, whereas prepare.py rejects flags & 10. An undecrypted or compressed SELF yields a plausible-looking ELF full of ciphertext. The output buffer is sized from untrusted ph offsets+blob lengths with no limit
- **Fix:** Reject s.encrypted and flag 0x8, cap size, and check ph offsets >= 64+phnum*56.

### [low] self2elf.py assumes e_phoff==64 and ignores the original header's phoff/phentsize
- id `python-scripts-6` · status **confirmed** (low,none) · `tools/self2elf.py:34`
- Program headers are read from elf_off+64 regardless of the original e_phoff, and e_phoff is rewritten to 64. If the source phoff differs, the wrong bytes are used as phdrs, and the file is only valid for typical PS4 images. phnum is read from the header but is not bounded by the file size.
- **Fix:** Use phoff=ehdr.e_phoff, assert it equals 64 or copy from there, and validate phnum*56 within the file.

### [low] elfinfo.py/gen_nid_db.py: unchecked offsets give silent mis-parse or raw tracebacks
- id `python-scripts-7` · status **confirmed** (low,low) · `tools/elfinfo.py:87`
- start = dyn_ph[2] - dynlib_ph[2] can be negative, so the slice silently takes the wrong data. tags is a defaultdict, so a missing DT_SCE_STRTAB/SYMTAB gives IndexError on [0]. dynlib.index(b'\0', begin) raises ValueError on a bad string offset. The dynamic loop does not stop at DT_NULL. struct.error is not handled for truncated files. gen_nid_db.py
- **Fix:** Validate start>=0 and the end bound, break at tag 0, require the tags, and wrap main() with a friendly error. Have gen_nid_db import a shared helper.

### [low] Unvalidated numeric/user config crashes patches.py with tracebacks
- id `python-scripts-9` · status **confirmed** (low,low) · `runtime-linux/scripts/patches.py:395`
- int(settings.get('preset','0') or 0), the render override int(v) split on 'x', and output_res parsing in main (line 699) raise ValueError on malformed bbport.ini/env values. patches.py main has no try/except, so run.sh aborts the launch. mods.py selected() calls json.loads with no handling of non-dict JSON (AttributeError is not caught by main's ex
- **Fix:** Wrap parsing, falling back to defaults with a warning.

### [low] BB_LEGACY_CPU override only recognises '1'; unknown CPU defaults to translating
- id `python-scripts-10` · status **contested** (none,low) · `runtime-linux/scripts/cpu_compat.py:238`
- BB_LEGACY_CPU=true/yes silently means disabled. When /proc/cpuinfo has no 'flags' line (non-x86, containers), missing_host_extensions() returns None and enabled() returns True. That triggers the hash-bound path from PY-1 and aborts the launch.
- **Fix:** Accept the usual truthy strings, and treat unknown CPU as disabled unless the translation plan matches the eboot.

### [low] build_cpu_compat.py puts an env-controlled directory first on sys.path
- id `python-scripts-11` · status **contested** (low,none) · `runtime-linux/tools/build_cpu_compat.py:15`
- BB_CPU_DEPS_DIR (default tools/python-deps) is inserted at sys.path[0] before importing capstone/keystone, so any python module in that directory shadows stdlib or other modules (code execution if the dir is attacker-writable). The default directory does not exist in the repo.
- **Fix:** Use sys.path.append, or only insert when the dir exists.

### [low] Redundant/stale link_libc.py still run by run.sh
- id `python-scripts-12` · status **confirmed** (low,low) · `runtime-linux/scripts/link_libc.py:75`
- run.sh runs link_libc.py and then link_modules.py. link_modules supersedes it (its docstring says so), but link_libc still re-parses libc, builds boot-libc.bin and libc-link.json (unused by the probe), and calls patch_libc, so the same legacy-CPU ValueError can come from either step. Only encode_id is shared.
- **Fix:** Move encode_id into link_modules and drop the link_libc call.

### [low] Non-atomic writes of prepared artifacts and misleading Bloodborne hardcodes in tool output
- id `python-scripts-13` · status **confirmed** (low,low) · `runtime-linux/scripts/prepare.py:220`
- prepare.py (boot.bin, eboot.elf, entry.bin, analysis.json), content_profile.py (content.bin, JSON), and patches.py (patches.bin) write straight to the destination, so a crash mid-write leaves a truncated file that the next stage treats as valid. cpu_compat.atomic_output exists for this. gen_nid_db.py writes via open(out,'w') without an encoding. re
- **Fix:** Route generated outputs through atomic_output; parametrise the redzone_sites label.

### [low] got_import.py has wrong comments/paths and no argument validation
- id `python-scripts-14` · status **confirmed** (none,low) · `runtime-linux/tools/got_import.py:4`
- The comment says 'repository root' but the path is runtime-linux. There is no usage message and no check for out/boot.bin existing. int(arg,16) raises on bad input and the slot lookup silently prints None for unknown offsets.
- **Fix:** Fix the comment, add argparse and an existence check.
- *Improvement:* Make the legacy-CPU path title-aware with graceful fallback - Detect an unknown eboot SHA in cpu_compat and skip translation with a warning, or ship a Driveclub plan. Today old CPUs cannot prepare Driveclub at al
- *Improvement:* Deduplicate the runtime-linux/scripts and windows/launcher/prepare forks - prepare.py, link_modules.py, cpu_compat.py and content_profile.py exist in two diverging copies (e.g. the extrq and redzone additions). Bug fixes must
- *Improvement:* Add a minimal pytest for parsers and patch compilation - Add cheap synthetic-SELF tests for parse_self/span bounds, patches.encode/compile_patches, and mods.content_root. They would catch regressions like PY
- *Improvement:* Share one SELF/dynamic-section parser across tools/ and scripts/ - elfinfo.Self, gen_nid_db.imported_nids, prepare.parse_self and the module() functions each reimplement parsing with different validation. prepare.pars
- *Improvement:* Remove or clearly isolate macOS/Bloodborne-only scripts for the Linux Driveclub tree - run.py, quit_menu.py, hud_layout.py, redzone_sites.py and the Bloodborne.xml patch file never run for Driveclub (menu movies and the Bloodborne ultraw
- *Improvement:* Verify original bytes for every patch write, not only the fingerprinted ones - Adding an expected-bytes check (the 'Original' attribute where the XML provides one, or a stored fingerprint per patch) would stop external or effect 

## macos-src

_src/ is a self-contained macOS/Metal backend (x86_64 Mac only). The root CMakeLists.txt hard-fails with FATAL_ERROR on non-Apple hosts (lines 11-15), so it never builds on or affects the Linux runtime-linux/ build; CI builds it only on macos-15-intel. The code appears to be original macOS-port code, not vendored upstream. Its design trusts the game files and guest: the loader is not hardened against corrupt or hostile SELF/ELF input, there are many missing bounds checks, and the signal handler does much async-signal-unsafe work and can fault recursively. The most severe issues are in loader/self.cpp, module.cpp, linker.cpp and signals.cpp. GPU/Metal and most HLE were not read in depth; the kernel memory and file HLE were checked._


### [high] SELF/ELF header parsing reads out of bounds of the file buffer
- id `macos-src-1` · status **unverified** · `src/core/loader/self.cpp:34`
- Open() only checks file.size() >= sizeof(SelfHeader). It then memcpy's segment_count*sizeof(SelfSegment) bytes after the header, the Elf64_Ehdr at elf_offset, and e_phnum*sizeof(Elf64_Phdr) at elf_offset+e_phoff, all without checking against file.size(). The initial read() result and tellg()==-1 are also unchecked. A truncated or corrupt file gives
- **Fix:** Validate each (offset+length) against file.size() with overflow-safe arithmetic before each memcpy. Check tellg()>=0 and in.gcount()==size.

### [high] Segment copy can write past the mapped module (p_filesz/p_vaddr never validated against mapped size)
- id `macos-src-2` · status **unverified** · `src/core/loader/module.cpp:126`
- The mapped size is computed from max(p_vaddr+p_memsz) with no overflow check. The copy then uses p_vaddr and p_filesz without checking p_filesz <= p_memsz or p_vaddr+p_filesz <= size. A malformed segment makes memcpy write past the mapping, into adjacent guest or host memory. Unsigned overflow of p_vaddr+p_memsz can also make size tiny.
- **Fix:** Reject if p_filesz > p_memsz, or p_vaddr+p_memsz overflows or exceeds kModuleGap or the reserved range. Reject overlapping or oversized segments.

### [high] Relocation targets and symbol values are not bounds-checked
- id `macos-src-3` · status **unverified** · `src/core/loader/linker.cpp:248`
- RelocateModule writes a u64 to m.Base()+r.r_offset for every RELA/JMPREL entry without checking r_offset+8 <= m.Size(). A corrupt or hostile module gives an arbitrary write across the guest address space. The write is also unaligned-cast to u64*.
- **Fix:** Skip and log any relocation with r_offset > m.Size()-8.

### [medium] String() used before dynamic-table bounds validation; strtab.offset unchecked
- id `macos-src-4` · status **unverified** · `src/core/loader/module.cpp:222`
- In ParseDynamic the DT_SCE_IMPORT_LIB/EXPORT_LIB/NEEDED_MODULE cases call DecodeRef, which calls String(), which reads dynlib.data()+strtab.offset+offset. The strtab/symtab/rela/jmprel range check runs only afterwards (line 235), so a bad DT_SCE_STRTAB offset gives an OOB read. The check itself computes r->offset + r->size in u64 and can overflow,
- **Fix:** Validate the ranges (overflow-safe: size > dynlib.size() || offset > dynlib.size()-size) before the second tag loop. Make String() check strtab.offset+strtab.size <= dynlib.size().

### [medium] Signal handler dereferences faulting RIP and scans stack unguarded, causing recursive faults
- id `macos-src-5` · status **unverified** · `src/core/signals.cpp:209`
- For SIGSEGV the handler reads code[0]/code[1] at the faulting rip. When the fault is a jump to an unmapped or non-readable address (the common crash shape), that read faults inside the handler. SIGSEGV is blocked, so the kernel kills the process with no diagnostic. Describe() reads 512 stack slots, DumpStackStrings() reads 0x3000 bytes, and the int
- **Fix:** Probe memory safely before reading (e.g. write()/mincore/process_vm-style probe via a pipe, or a nested sigsetjmp guard), only read code[] when rip != fault address, and bound stack scans by the threa

### [medium] Async-signal-unsafe work inside the SIGILL/SIGSEGV handler
- id `macos-src-6` · status **unverified** · `src/core/signals.cpp:196`
- The SIGILL path takes a std::mutex (g_patch_lock), walks a std::vector, and calls DC_ERROR/backtrace()/std::string/malloc, none of which are async-signal-safe. If a fault occurs while a thread holds g_patch_lock or the malloc/stdio lock (for example in AddPatchArea, or during logging), the handler deadlocks instead of patching or reporting. Two thr
- **Fix:** Use a spinlock (atomic_flag) for patching. In the fatal path use write(2) with a preformatted static buffer. Guard against re-entrancy with a thread_local flag.

### [medium] No alternate signal stack installed although SA_ONSTACK is requested
- id `macos-src-7` · status **unverified** · `src/core/signals.cpp:229`
- The handler is installed with SA_ONSTACK, but no sigaltstack call exists anywhere in src/ (grep finds none). A guest stack overflow, or a fault with a bad rsp, then runs the handler on the same exhausted stack and dies silently. The very crash cases the diagnostics aim to explain (guest stack overflow, wild rsp) are lost.
- **Fix:** Call sigaltstack per thread in Tls::SetupThread() and in the main thread, with at least 64 KB.

### [medium] EXTRQ patching races with other threads executing the site and uses overwide rwx mprotect
- id `macos-src-8` · status **unverified** · `src/core/signals.cpp:127`
- The patch rewrites up to 9 bytes with memcpy while other guest threads may be executing or fetching that code, so another thread can see a torn instruction. The check `code[0]==0xE9` only guards threads that fault after the write completes. It also flips whole pages to RWX then to R|X, which can drop original permissions. The trampoline search loop
- **Fix:** Write the 5-byte jmp with a single atomic 8-byte store where possible (patch after int3 first-byte technique). Reject idx+len>64. Restore the page's real protection (module Protect flags).

### [medium] TLS block allocation: aligned_alloc size/alignment contract, unchecked results, filesz>memsz
- id `macos-src-9` · status **unverified** · `src/core/tls.cpp:100`
- std::aligned_alloc requires size to be a multiple of alignment, but the size is AlignUp(memsz,16) while the alignment can be larger (m.align). The result is not null-checked. memcpy copies m.filesz without checking filesz <= memsz, and memset(memsz-filesz) underflows if filesz>memsz. The same applies to the executable's static block at line 131. Se
- **Fix:** Round the size up to the (power-of-two) alignment, validate filesz<=memsz at RegisterModule, and check for null.

### [medium] Guest path translation allows '..' traversal out of mount roots
- id `macos-src-10` · status **unverified** · `src/hle/kernel/file.cpp:60`
- Translate() appends the guest-controlled remainder to the host mount root with no normalization or '..' rejection. A path such as /app0/../../../etc/x or /data/../.. escapes the game directory; sceKernelOpen then honors O_CREAT/O_TRUNC/O_WRONLY on the result. Only matters if the guest is hostile or a corrupt game, but the HLE is meant as a sandbox
- **Fix:** Reject any component equal to '..' (or use weakly_canonical and verify the prefix) before returning.

### [medium] Memory::Protect operates on arbitrary host addresses and keeps VMA bookkeeping wrong for partial ranges
- id `macos-src-11` · status **unverified** · `src/core/memory.cpp:271`
- Protect() calls mprotect on any guest-supplied addr/size without checking AddressSpace::Contains or that the range is mapped, so sceKernelMprotect can change protection on host memory (e.g. libc, stub area). It only updates prot of VMAs wholly contained in the range (no splitting) and does not bump g_generation, so Direct-mapping consumers can see
- **Fix:** Reject ranges not inside AddressSpace::Contains and covered by VMAs. Split VMAs on partial protect. Bump the generation for Direct VMAs.

### [low] MapFlexible over an existing Direct mapping doesn't bump the direct-map generation; MapDirect phys overflow
- id `macos-src-12` · status **unverified** · `src/core/memory.cpp:235`
- MapDirect/Reserve/Unmap bump g_generation, but MapFlexible does not even though MAP_FIXED can carve away Direct VMAs via Place()/Carve(). DirectMappings() consumers (GPU address translation) can then use stale ranges. In MapDirect, `phys + size > kDirectSize` can overflow with a huge guest phys, and phys is not checked for alignment. sceKernelMapNa
- **Fix:** Bump g_generation in MapFlexible (at least when Carve removed a Direct VMA). Use `phys > kDirectSize - size`.

### [low] HLE kernel entry points deref guest pointers without validation
- id `macos-src-13` · status **unverified** · `src/hle/kernel/memory.cpp:37`
- sceKernelMapNamedDirectMemory/FlexibleMemory/ReserveVirtualRange deref *addr, and sceKernelAllocateDirectMemory writes *phys_out. sceKernelVirtualQuery memsets and writes info without null checks or info_size validation (info_size is ignored). pthread join/detach (pthread.cpp:~510) and mutexattr functions deref raw guest handles with no null check.
- **Fix:** Return EFAULT/EINVAL on null pointers and honor info_size in VirtualQuery.

### [low] Module metadata computed without range checks (eh_frame, code range, unsigned underflow)
- id `macos-src-14` · status **unverified** · `src/core/loader/module.cpp:151`
- The .eh_frame_hdr read (p[0], p[1], 8-byte memcpy) happens at base+eh_frame_hdr without checking it lies within size. eh_frame_size = size - eh_frame underflows when eh_frame > size. code_begin/code_end are overwritten by every PF_X segment, so only the last executable segment is patched/trampolined by the EXTRQ patcher. DT_INIT/entry are used by S
- **Fix:** Check ranges against size. Take the min/max across all executable segments.

### [low] Unresolved-import counters race, thread objects leak
- id `macos-src-15` · status **unverified** · `src/core/loader/linker.cpp:96`
- DcUnresolvedImport increments u.calls (plain u32/u64) from any guest thread with no synchronization (data race, UB). Separately, Hle::Kernel::CreateThread allocates `new Thread()` that is never freed on normal thread exit or join (src/hle/kernel/pthread.cpp:468). LoadGame hardcodes eboot.bin and relies on sorted filename order for module/TLS index
- **Fix:** Make calls std::atomic. Free Thread on join/detached exit.
- *Improvement:* Make the loader fuzz-hardened with one central bounds-checked reader - All loader parsing (self.cpp, module.cpp ParseDynamic, linker relocation) uses ad-hoc memcpy/pointer casts. A small checked span/reader helper with ov
- *Improvement:* Make the crash handler async-signal-safe and give it a sigaltstack - Pre-format into static buffers, use write(2), spinlock for patch areas, per-thread sigaltstack, and a safe-read probe for rip/stack. This turns silent
- *Improvement:* Add a null/range-validation layer for HLE guest pointers - A tiny helper (ValidGuestPtr(addr,len)) using AddressSpace::Contains would let HLE functions return EFAULT instead of crashing the emulator, improving
- *Improvement:* Release LDT slots / Thread objects on teardown and document thread limit - Tls uses a fixed 1020-slot LDT pool and aborts when exhausted; thread churn plus leaked Thread objects can hit this in long sessions. Log occupancy an
- *Improvement:* Make the platform split explicit in docs/CI - Root CMakeLists.txt FATAL_ERRORs on non-Apple; anyone running 'cmake .' on Linux gets an error. A README note at src/ plus a CMake message pointing to
- *Improvement:* Unify duplicated protection/VMA bookkeeping between AddressSpace and Memory - AddressSpace::Map/Unmap and Memory VMAs track the same regions independently (Linker maps patch areas and modules through AddressSpace but registers v

## tests-build-health

_Python tests were run with unittest because pytest is not installed. runtime-linux: 159 tests ran, 1 failed, 24 skipped. The windows/launcher suite could not run: it needs pytest, and conftest.py uses ctypes.windll, so it is Windows-only. The C tests are built and run by build.sh --test in CI. Everything else is not run in CI: the Python suite, the GPU/C++ tests, and the Windows launcher on Linux. Nothing covers runtime_dc.c, the Driveclub shell scripts, or the LDS-bounds/wave64 shader changes. Several tests still assert Bloodborne behaviour (patches/Bloodborne.xml, the bbport launcher)._


### [high] CI never runs the Python tests or any GPU/C++ tests on Linux
- id `tests-build-health-1` · status **unverified** · `.github/workflows/ci.yml:58`
- The Linux job only runs `bash build.sh --test`, which compiles and runs 7 C tests (pad, input_config, runtime, file_mods, sema, content, language). The ~25 runtime-linux/tests/*.py files are never invoked. The seven C++ test targets in gpu/CMakeLists.txt (command-buffer, motion-history, motion-shader, ui-composition, scene-resolution, taa-shader, c
- **Fix:** Add `python3 -m unittest discover -s tests` after build.sh --test. Add a step that runs `ninja -C out/gpu command-buffer-test motion-history-test ui-composition-test taa-shader-test camera-motion-test

### [medium] test_input_defaults fails: launcher default input.ini header still says bbport
- id `tests-build-health-2` · status **unverified** · `runtime-linux/launcher/bbport_input_config.py:144`
- The game's default text in src/runtime_input_config.c line 508 was renamed to 'DCport input', but the launcher copy was not. test_input_defaults.InputDefaultsTests.test_launcher_default_matches_the_game fails: 'bbport input...' != 'DCport input...'. This is the only failure in the 159-test run. The launcher and game defaults have drifted apart, and
- **Fix:** Change the header in bbport_input_config.py to 'DCport input', or rename it consistently in both places.

### [low] Windows launcher tests cannot run on Linux and have a hard pytest dependency
- id `tests-build-health-3` · status **unverified** · `windows/launcher/tests/conftest.py:1`
- The suite imports pytest (conftest.py, test_updater.py, test_game.py, test_garage.py) and uses ctypes.windll and wintypes. It only runs on the windows-latest job. unittest discovery fails with 6 import errors, and pytest is not installed locally. Nothing in the suite exercises launcher logic on the Linux job, even for the parts that are platform-ne
- **Fix:** Document `pip install pytest pillow` for local use. Optionally run the platform-neutral launcher tests on Linux.

### [medium] Many Python tests skip silently when binaries are not built or capstone is missing
- id `tests-build-health-4` · status **unverified** · `runtime-linux/tests/test_cpu_compat.py:217`
- 24 tests are skipped in this run. 18 of them are test_cpu_compat (no capstone module, and Windows-only anyway). 5 are Windows-only: cpu_detection, mods overlay, redzone x2. One needs out/eboot.elf. test_runtime.py, test_sema.py and test_content.py skip with 'run bash build.sh --test first' when the binaries are missing. A bare `unittest discover` t
- **Fix:** In CI, fail on skips (for example by running with an env var that turns skips into failures), or build the binaries first and assert they exist.

### [medium] Bloodborne-specific test assets still validate the wrong game
- id `tests-build-health-5` · status **unverified** · `runtime-linux/tests/test_patches.py:15`
- test_patches.py, test_ultrawide.py and test_redzone.py load patches/Bloodborne.xml and redzone.json, and assert Bloodborne patch behaviour (Title="Bloodborne", AppVer 01.09, ultrawide offsets). The only patch file shipped is Bloodborne.xml, not a Driveclub one. These tests give no confidence that Driveclub patching works. test_patches.py line 230 a
- **Fix:** Add Driveclub patch fixtures and tests, or remove or mark the Bloodborne-only tests so they do not imply Driveclub coverage.

### [medium] No tests for runtime_dc.c or the Driveclub-specific GPU changes
- id `tests-build-health-6` · status **unverified** · `runtime-linux/src/runtime_dc.c:1`
- runtime_dc.c (392 lines of Driveclub HLE stubs, which the file comments say it resolves last) has no test. Grep for 'lds', 'wave64' and 'runtime_dc' in runtime-linux/tests finds no relevant test. No test builds or compiles the LDS-bounds or wave64 changes in shader_recompiler. test_taa_shader.cpp covers only the upscaler. The only test that links t
- **Fix:** Add a C test that calls the DC stub table lookup and checks the return codes. Add a shader-recompiler SPIR-V test for LDS bounds and wave64.

### [medium] Driveclub shell scripts have no tests and no linting in CI
- id `tests-build-health-7` · status **unverified** · `setup.sh:1`
- setup.sh, run_driveclub.sh and play.sh are the user-facing entry points. CI has no `bash -n` or shellcheck step, and no test touches them. `bash -n` passes locally for all three. shellcheck is not installed locally, so quoting and injection could not be checked automatically.
- **Fix:** Add a `shellcheck setup.sh run_driveclub.sh play.sh` step (apt shellcheck is preinstalled on ubuntu-24.04).

### [low] test_input_config.c opens its fixture by a CWD-relative path
- id `tests-build-health-8` · status **unverified** · `runtime-linux/tests/test_input_config.c:302`
- TestShadps4DefaultFileImports does fopen("tests/data/shadps4_default_input.ini"). It works only when the binary runs with CWD=runtime-linux, as build.sh --test does. Running out/input-config-test from anywhere else hits assert(f). The fixture was only recently un-ignored (commit 48f5c3e), so a fresh checkout depends on that fix. The .gitignore patt
- **Fix:** Pass the fixture path via argv or a compile-time -DTEST_DATA_DIR.

### [low] Root-level CMake macOS job builds the unrelated Metal port; no Linux/DC integration smoke test
- id `tests-build-health-9` · status **unverified** · `.github/workflows/ci.yml:8`
- The macos job builds src/ (the BlackLineInteractive Metal port) on macos-15-intel. The linux job's build.sh run produces driveclub-probe but never executes it. There is no end-to-end smoke test: no `--help` or probe run, no GPU-capabilities run, and no check that setup.sh or play.sh works with a stub game. The CI also does not use `bash build.sh --
- **Fix:** Add a probe smoke run (see test_probe.py) and run the Python tests in the Linux job.
- *Improvement:* Run python tests and GPU C++ tests in the Linux CI job - Adds ~160 existing tests and 8 C++ test binaries at almost no cost, and would have caught T2.
- *Improvement:* Add shellcheck plus bash -n for setup.sh, run_driveclub.sh and play.sh - These are the main user entry points and are currently untested and unlinted.
- *Improvement:* Make skips fail in CI - Many tests skip with 'run build first', so a silently skipped suite looks green; asserting zero unexpected skips would catch it.
- *Improvement:* Add Driveclub-specific tests (runtime_dc.c stubs, LDS bounds, wave64, DC patch/fingerprint) - The repo owner's own changes are the least covered, and the existing tests validate Bloodborne.
- *Improvement:* Rename the bbport and Bloodborne leftovers consistently across launcher, tests and docs - The partial DCport rename already produced a test failure, and mixed naming risks more drift.
- *Improvement:* Document or lock test dependencies (pytest, pillow, capstone) - The local run cannot execute launcher tests, and the capstone-gated tests are skipped everywhere except Windows.

## runtime-core

_Mostly upstream Bloodborne-port code, and generally well written: the memory VMA table, rwlock registry and semaphore code are careful. I confirmed these real defects. (1) Flexible-memory VMA splitting keeps the wrong backing offset, which can zero live memory and corrupt the allocator bitmap. (2) Guest MAP_FIXED requests are not range-checked against the PS4 user range. (3) Host-side resource leaks: thread records, TLS blocks and stacks are never freed. (4) runtime_restart still launches the Bloodborne run.sh, so it is broken for Driveclub on Linux. There are also a few smaller robustness gaps: SFO parsing integer wrap, the RTC RFC2822 formatter indexing out of bounds, no sigaltstack, an infinite loop in direct_release on a host unmap failure, and Linux errno values leaking through pthread_create errors. Nothing was modified._


### [high] split_at keeps stale backing offset for KIND_FLEXIBLE VMAs; later unmap frees or discards the wrong pages
- id `runtime-core-1` · status **confirmed** (high,medium) · `runtime-linux/src/runtime_memory.c:346`
- For KIND_DIRECT, split_at advances right.phys. For KIND_FLEXIBLE the VMA's phys is also a backing offset (flex_alloc returns POOL_SIZE+offset), but it is not advanced. After a partial sceKernelMprotect, a partial munmap or a partial release of a flexible mapping, the right-hand VMA keeps the original start phys. A later drop_range then calls flex_f
- **Fix:** Advance phys for both kinds: if (right.kind!=KIND_RESERVED) right.phys+=a-vmas[i].start.

### [high] place() does not bound MAP_FIXED addresses to the PS4 user range or check for overflow; guest can mmap(MAP_FIXED) over host memory
- id `runtime-core-2` · status **confirmed** (low,medium) · `runtime-linux/src/runtime_memory.c:392`
- With MAP_FIXED the only checks are address!=0 and page alignment. There is no check that [address,address+size) lies inside [USER_MIN,USER_MAX) or that address+size does not wrap. host_map then calls mmap(address,..,MAP_FIXED), which silently replaces any host mapping at that address (glibc, the runtime image, GPU heaps, the low image and stacks in
- **Fix:** In the MAP_FIXED branch, return INVALID unless address>=USER_MIN && size<=USER_MAX-USER_MIN && address<=USER_MAX-size. Add the same check to unmap_locked and protect_locked for symmetry.

### [medium] Guest threads leak their record, TLS block and stack (never freed, never unmapped)
- id `runtime-core-3` · status **confirmed** (low,low) · `runtime-linux/src/runtime_thread.c:262`
- host_start never frees t->tls_block (aligned_alloc) or the GuestThread. thread_join never frees them after pthread_join, and the record stays in the global list forever. The stack from runtime_low_map (line 290) is never munmapped. runtime_low_map is a bump allocator (low_next only grows) over the 32 GiB window [0x0800000000,0x1000000000). Each thr
- **Fix:** Record the stack base/size in GuestThread. Free tls_block, unmap the stack and unpublish and free the record on join or detached exit (after pthread_join, or at the end of host_start for detached thre

### [medium] runtime_restart on Linux execs the Bloodborne launcher (bash run.sh) relative to cwd
- id `runtime-core-4` · status **confirmed** (low,medium) · `runtime-linux/src/probe.c:499`
- The in-game settings overlay (gpu/shim/bbport_overlay.cpp:403) calls runtime_restart when the resolution changes. On Linux this does execlp("bash","bash","run.sh") with a cwd-relative path. Driveclub is started by run_driveclub.sh at the repo root (which passes --app0, --patches, --content-profile and --timeout 0). The cwd of the probe is whatever
- **Fix:** Mirror the Windows path: have run_driveclub.sh export BB_RESTART_COMMAND (or BB_RESTART_SCRIPT, absolute path) and exec that, or re-exec /proc/self/exe with the saved argv.

### [medium] direct_release can loop forever if unmap_locked fails
- id `runtime-core-5` · status **confirmed** (low,low) · `runtime-linux/src/runtime_memory.c:511`
- The loop over VMAs calls unmap_locked() and ignores its result, then restarts at vma_index(v.start) with continue. unmap_locked returns INVALID without calling drop_range when host_unmap (munmap) fails (line 485), so the same KIND_DIRECT VMA is found again and the loop never advances. It spins while holding the exclusive region lock, which stalls e
- **Fix:** Check the result and either skip the VMA (++i) or return the error. Cap the retries.

### [medium] pool() leaves pool_fd set after a partial failure, so the next call reports success with backing_base == NULL
- id `runtime-core-6` · status **confirmed** (low,low) · `runtime-linux/src/runtime_memory.c:247`
- pool_fd is assigned from memfd_create before ftruncate and mmap. If ftruncate (a 6 to 17 GiB sparse file; can fail under RLIMIT_FSIZE or a restricted memfd) or the mmap of the view fails, pool() returns -1 but pool_fd stays >=0. The next call hits `if (pool_fd>=0) return 0` and proceeds with backing_base == NULL. The next place() then maps from the
- **Fix:** Use a local fd. Only publish pool_fd and backing_base when everything succeeded, and close the fd on failure. Test backing_base, not pool_fd, for 'already initialised'.

### [low] sfo_value: 32-bit overflow in the bounds check allows out-of-bounds reads of param.sfo data
- id `runtime-core-7` · status **confirmed** (low,low) · `runtime-linux/src/probe.c:422`
- keys, values, key_offset, offset and length are all uint32/uint16 read from the file. keys+key_offset and values+offset+length are computed in 32-bit arithmetic and wrap, so a crafted param.sfo passes the `>n` checks. The code then does strcmp((char*)data+keys+key_offset,...) and memcpy(text,data+values+offset,copy) up to 4 GiB away from the 64 KiB
- **Fix:** Do the arithmetic in size_t/uint64_t. Require key_offset < n-keys and use strnlen/memchr within n. Zero data[] first (data[65536]={0}).

### [low] rtc_rfc2822_local indexes days[] with a negative error code for out-of-range ticks
- id `runtime-core-8` · status **confirmed** (low,low) · `runtime-linux/src/runtime_rtc.c:348`
- The guest supplies utc_tick. from_tick((utc+offset)) can produce year 0 or >9999 (very large tick, or a small tick plus a negative zone offset wrapping the uint64). rtc_day_of_week then returns a negative ERR_INVALID_YEAR (0x80B50008) which is used directly as the index into days[], an out-of-bounds read that can crash the process.
- **Fix:** Check the return value of rtc_day_of_week (<0 returns an error) or compute the weekday modulo 7 directly from days_from_civil.

### [low] No sigaltstack / SA_ONSTACK: guest stack overflow kills the process without the fault report
- id `runtime-core-9` · status **confirmed** (low,low) · `runtime-linux/src/probe.c:583`
- The SIGSEGV/SIGILL/SIGBUS handler is installed with only SA_SIGINFO, and no thread ever calls sigaltstack (grep finds none in src or gpu/shim). When a guest thread overflows its stack (guest code has no stack probes; the low-mapped stacks are only 1 MiB by default), the kernel cannot deliver the signal and kills the process with a bare SIGSEGV, los
- **Fix:** Give every guest and host thread a small sigaltstack (in attach()) and add SA_ONSTACK. Format with the already-present write_hex style helpers.

### [low] Linux errno values returned to the guest from pthread_create / pthread_join failures
- id `runtime-core-10` · status **confirmed** (low,low) · `runtime-linux/src/runtime_thread.c:304`
- The ERR(e) macro uses the host (Linux) errno directly, contradicting runtime_kernel.c's own rule that host errno never reaches the guest. EAGAIN is 11 on Linux, but the guest's EAGAIN (FreeBSD) is 35. ERR(11) is the guest's EDEADLK. A thread-limit failure therefore returns SCE_KERNEL_ERROR_EDEADLK instead of EAGAIN. The same applies to the pthread_
- **Fix:** return ERR(runtime_guest_errno(e)).

### [low] Duplicate, incomplete errno translation tables; unmapped errors collapse to EIO
- id `runtime-core-11` · status **confirmed** (low,low) · `runtime-linux/src/runtime_mutex.c:13`
- runtime_mutex.c orbis_error() and runtime_rwlock.c error() each re-implement a small errno switch, and runtime_kernel.c runtime_guest_errno() has the canonical one. The canonical table lacks network and other errors (EWOULDBLOCK is aliased to EAGAIN, but EINPROGRESS 36, ENOTSOCK 38, EADDRINUSE 48, ECONNREFUSED 61, ENOTSUP 45, ETXTBSY 26 and similar
- **Fix:** Extend the canonical table (FreeBSD values) and make the mutex and rwlock helpers call it.

### [low] protect_locked computes end from the unaligned start
- id `runtime-core-12` · status **confirmed** (low,low) · `runtime-linux/src/runtime_memory.c:569`
- end = start + align_up(size,PAGE) is computed before start is rounded down. For an unaligned start the range is not extended to cover the tail. Example: start = 0x...100, size = 0x4000 gives an end that is not page-aligned and does not cover the last 0x100 bytes' page, so a 16K-page-aligned VMA boundary is mis-split by carve() at a non-page address
- **Fix:** Compute end = align_up(start+size, PAGE) after masking, and check for wraparound.

### [low] scePthread/posix mutex type 4 (adaptive) mapped to ERRORCHECK; unlock vs destroy races unguarded
- id `runtime-core-13` · status **confirmed** (low,low) · `runtime-linux/src/runtime_mutex.c:59`
- native_type = type==2 ? RECURSIVE : type==3 ? NORMAL : ERRORCHECK. PS4 type 4 (adaptive) therefore becomes ERRORCHECK, so a self-relock returns EDEADLK where the PS4 adaptive mutex would spin or block. mutex_destroy frees the GuestMutex while concurrent mutex_lock callers may have just loaded the pointer through ensure_mutex. The lock/unlock/create
- **Fix:** Map 4 to PTHREAD_MUTEX_ADAPTIVE_NP. Make the stat counters atomic or relaxed.

### [low] thread_join/thread_detach race on t->joined/t->detached and join of an already-joined pthread
- id `runtime-core-14` · status **confirmed** (low,low) · `runtime-linux/src/runtime_thread.c:316`
- thread_join checks and sets t->joined without holding the global lock, so two concurrent joins can both pass and both call pthread_join (UB). `joined` is also set before pthread_join, so a failed join leaves the thread permanently unjoinable. thread_detach does not check t->joined and then calls pthread_detach on a pthread_t that may already have b
- **Fix:** Do the test-and-set of joined/detached under `lock`, and clear joined if pthread_join fails. Make thread_detach reject joined threads.

### [low] sysctl kern.arandom ignores getrandom short reads and has no size cap
- id `runtime-core-15` · status **contested** (low,none) · `runtime-linux/src/runtime_kernel.c:223`
- getrandom(old,*oldlen,0) is called with a guest-provided length. A return value smaller than the requested length (large requests, signal interruption) is treated as success, leaving the tail of the guest buffer uninitialised or stale while *oldlen still claims the full length. A guest seeding a PRNG or canary from this gets partially predictable d
- **Fix:** Loop until all bytes are read (retry on EINTR), and cap or validate *oldlen.
- *Improvement:* Remove the atomic memory_calls counter from guest memcpy/memset/memmove/memcmp - Every guest libc memory call from every thread does an atomic increment on one shared cache line (lines 163-174). This is heavy cache-line contention 
- *Improvement:* Replace linear registries with hash/array lookups (semaphores, rwlocks) - sem find() walks a singly linked list of all semaphores under one global mutex on every wait/signal/poll. rwlock find_locked() likewise walks every rw
- *Improvement:* Stop printing on every memory/semaphore/thread operation and drop unbuffered stdout - main() sets stdout to _IONBF and runtime_memory.c/runtime_sema.c/runtime_thread.c printf on each map, allocation, sema create and thread create (e.g. 
- *Improvement:* Use CLOCK_MONOTONIC for relative timed waits on Linux (mutex_timedlock, cond_timedwait) - deadline_after uses CLOCK_REALTIME for relative usec timeouts, so an NTP step or manual clock change makes timed waits fire early or hang. The sema co
- *Improvement:* Rebase patch relocations with a sorted/indexed lookup - apply_patches scans all nr (up to 1,000,000) relocations for every patch entry (O(count*nr)); with thousands of entries startup cost grows quickly. So
- *Improvement:* Calibrate TSC frequency via CPUID 0x15/0x16 or once at start instead of a 20 ms sleep on first call - tsc_frequency() blocks the calling guest thread for 20 ms the first time and is racy (unsynchronised static, concurrent first calls each sleep). The r

## repo-hygiene-docs

_Tracked content is clean: setup.log, data/, game/, the 19 GB DRIVECLUB.v.1.28.* dir and *.spv are all gitignored and untracked (verified with git ls-files and git status --ignored). I found no committed secrets (token/key/private-key greps over non-vendored files were empty). The pack is about 33 MiB, mostly screenshots and vendored sources. The main problems are documentation drift. UPGRADE_PLAN.md marks every item "[x] Completed" but several are contradicted by the code. README disagrees with the launcher on defaults and on licensing, and .gitmodules is stale. Bloodborne leftovers (runtime-linux/README.md, the bb-launcher/bbport_* launcher files, tools/extract.sh) are untouched and misleading. Vendored code is large (1489 of 2513 tracked files are under gpu/third_party), so most files are upstream._


### [medium] UPGRADE_PLAN 3.2 claims Wave64 lowering for all shader stages; code is compute-only
- id `repo-hygiene-docs-1` · status **unverified** · `runtime-linux/gpu/shadps4/shader_recompiler/ir/passes/lower_wave64_pass.cpp:89`
- UPGRADE_PLAN.md task 3.2 is checked off and says LowerWave64BallotPass was extended beyond compute to all stages with cross-lane ops. The pass still returns early for every non-compute stage. Fragment shaders, which the plan names for the white-square fixes, get no lowering.
- **Fix:** Either implement the extension or change 3.2 to unchecked or partial, and fix the README claim.

### [medium] UPGRADE_PLAN 5.3 resolution-constant autopatcher does not exist
- id `repo-hygiene-docs-2` · status **unverified** · `UPGRADE_PLAN.md:146`
- Task 5.3 says SPIR-V constant patching (1.0/1920.0 and 1.0/1080.0 replacement) was integrated into windows/launcher/prepare/dc_prepare.py. That file is 67 lines and only links modules and compiles patches. A grep for 1920/1080/1440 in it, and for 1.0/1920-style patching in windows/launcher/**/*.py, found nothing.
- **Fix:** Uncheck 5.3, or add the feature. Also fix the stale Phase 5 target path, which points at a Windows launcher file.

### [medium] DLSS listed as completed/selectable on Linux, but the DLSS bridge is MSVC/Windows-only
- id `repo-hygiene-docs-3` · status **unverified** · `runtime-linux/gpu/dlss_bridge/CMakeLists.txt:4`
- The CMake header says bbport_dlss.dll links the NVIDIA DLSS SDK (MSVC only) and is built separately with the 'Visual Studio 17 2022' generator. The Linux launcher still advertises BB_UPSCALER=dlss, and UPGRADE_PLAN Phase 5 marks FSR/DLSS completed for the Linux port. No Linux DLSS path exists.
- **Fix:** Remove dlss from Linux help and the plan, or have the launcher reject it with a clear message. Document that FSR4/FSR411 assets are fetched separately via tools/fetch_fsr4_assets.sh.

### [medium] README says readbacks default is BB_READBACKS=2; launcher defaults to 1
- id `repo-hygiene-docs-4` · status **unverified** · `README.md:119`
- README 'Performance & Visual Consistency Principles' item 3 presents BB_READBACKS=2 (Precise) as the shipped setting. run_driveclub.sh defaults to 1 (Relaxed) and its help text says so. README also describes BB_VBLANK_HZ=0 without saying the Steam Deck profile uses 45, and says 'Pure Native Shading' while the launcher has an upscaler option.
- **Fix:** Align the README with the actual defaults, or change the default to 2 if that is what the visual fixes require.

### [medium] .gitmodules declares 6 submodules but none exist as gitlinks; sources are vendored
- id `repo-hygiene-docs-5` · status **unverified** · `.gitmodules:1`
- git ls-files -s shows no mode-160000 entries, and git submodule status prints nothing. The listed paths (fsr-vulkan, imgui, LibAtrac9, magic_enum, miniz, xbyak) are tracked as ordinary files, including imgui examples and magic_enum test/Catch2. README ('git clone --recursive') and CI ('submodules: recursive') imply submodules. Upstream pins are los
- **Fix:** Either delete .gitmodules and the --recursive wording and record upstream commits (as gpu/VENDOR.txt does), or convert the paths to real submodules.

### [medium] License statement inconsistent: README says GPL-2.0-or-later, LICENSE is plain GPLv2 text, and tree contains GPL-3.0-or-later code
- id `repo-hygiene-docs-6` · status **unverified** · `README.md:206`
- The root LICENSE is the GPLv2 text, with no 'or later' grant and no top-level notice. README claims 'GPL v2.0 or later'. The tree includes GPL-3.0-or-later code (runtime-linux/gpu/bbnet/thd/*.h, SHADNET-PORT.md, packaging/windows/licenses/GPL-3.0-bbhost-online-code.txt). Distributing the combined runtime therefore falls under GPLv3 terms, not 'v2 o
- **Fix:** State the effective license of the combined work (GPL-3.0-or-later for the runtime). Add a root THIRD_PARTY/NOTICE file listing components, licenses and upstream commits.

### [low] runtime-linux/README.md is the unmodified Bloodborne README
- id `repo-hygiene-docs-7` · status **unverified** · `runtime-linux/README.md:1`
- The README in the Linux runtime directory describes Bloodborne for Windows (Download from Supermedo releases, Bloodborne.exe, shadNet account, Discord). It is wrong for Driveclub. The adjacent runtime-linux/launcher/ files (bb-launcher.sh, bbport_launcher.py, bbport_steam.py, bbport_trophies.py, ...) are also untouched Bloodborne leftovers, and 75
- **Fix:** Replace it with a short Driveclub/Linux pointer to the root README, and keep the original as an upstream-credit note. Delete or quarantine the Bloodborne-only launcher files if they are unused on Linu

### [low] tools/extract.sh is a stale macOS-port script that cannot work on a fresh clone
- id `repo-hygiene-docs-8` · status **unverified** · `tools/extract.sh:5`
- It depends on tools/pkg_pfs_tool, which .gitignore excludes (so it is never in the repo). It defaults to a hardcoded rom/...V0105.pkg (v1.05, not the supported v1.28). Its messages are in Ukrainian. The root README describes tools/ as 'ELF analysis tools' and does not mention that the extractor is unusable without an external binary.
- **Fix:** Document where to get pkg_pfs_tool or drop the script. Note it only handles v1.05.

### [low] UPGRADE_PLAN.md uses hardcoded absolute file:// links to the author's home directory
- id `repo-hygiene-docs-9` · status **unverified** · `UPGRADE_PLAN.md:28`
- Every link is file:///home/afnan/Projects/Driveclub-Linux/...; they are dead for any other reader or on GitHub. The plan also marks Phase 7 (benchmarking and cross-vendor regression suite) complete, but the only evidence found is a --mangohud env toggle in run_driveclub.sh. No benchmarking script or AMD/NVIDIA/Intel/Deck regression results were fou
- **Fix:** Use relative links. Reword Phase 7 as 'MangoHud toggle only' until real benchmark and regression artifacts exist.

### [low] README dependency lists omit packages CI and setup actually need
- id `repo-hygiene-docs-10` · status **unverified** · `README.md:90`
- Distro install commands omit python3 (setup.sh line 116 requires it), glslang-tools, and X11 dev libraries (libx11/xext/xrandr/xcursor/xfixes/xi), all of which CI installs for the Linux build. The only thing setup.sh checks up front is git, python3, cmake, ninja. A clean Ubuntu machine following the README may fail mid-build.
- **Fix:** Sync the README and setup.sh package lists with the CI list.

### [low] Large screenshots tracked in git inflate clone size
- id `repo-hygiene-docs-11` · status **unverified** · `media/2026-10-14_22-34-39.png:1`
- media/ is 28 MB (a single 9.1 MB PNG, plus four 4 MB PNGs) and windows/media is 5.3 MB, in a 33 MiB pack. The README embeds the 9 MB image directly on the front page.
- **Fix:** Recompress to JPEG/WebP (media/DRIVECLUB.webp already exists) or host the images externally.

### [low] .gitignore blanket patterns conflict with tracked files and can hide legitimate ones
- id `repo-hygiene-docs-12` · status **unverified** · `.gitignore:4`
- Patterns *.bin, *.dat, *.ini, *.log, *.spv and *.metal match anywhere in the tree. windows/launcher/input-default.ini is tracked despite matching *.ini (git ls-files -ci lists it). The *.spv rule ignores shipped generated SPIR-V under third_party/fsr-vulkan/generated; these exist locally but are untracked, so a fresh clone lacks them unless the bui
- **Fix:** Anchor the patterns (for example /game/**/*.bin) and add negations for the tracked files. Confirm the generated .spv are rebuilt by the build.

### [low] Marketing claims in README are unverifiable absolutes
- id `repo-hygiene-docs-13` · status **unverified** · `README.md:12`
- The README states 'Zero Artifacts, 60 FPS', 'Rock-Solid 60 FPS', 'Hardware Agnostic' and 'Tested across NVIDIA, AMD, Intel Arc and Steam Deck'. No test results back these up, and Phase 7 evidence is thin (see M3). The Steam Deck profile also defaults to 45 FPS.
- **Fix:** Soften the wording or link to the test matrix.
- *Improvement:* Add a root THIRD_PARTY/NOTICE file - Consolidate attribution and licenses for shadPS4 (pinned commit from gpu/VENDOR.txt), Bloodborne/Supermedo/bbhost (GPL-3.0), IFreemz dlss_bridge (MIT)
- *Improvement:* Make UPGRADE_PLAN.md an honest tracker - Replace blanket [x] with verified/partial/not-done status and relative links; add a pointer to the code or commit proving each item. Several items (3.
- *Improvement:* Resolve the submodule/vendoring mismatch - Pick one model (real submodules or vendored with VENDOR.txt pins). Drop imgui examples, magic_enum tests and xbyak test headers (several MB each) from
- *Improvement:* Remove or quarantine Bloodborne-only assets - runtime-linux/README.md, launcher/bbport_*, packaging/windows and the Bloodborne trophy/i18n files are dead for Driveclub on Linux and confuse contrib
- *Improvement:* Add a README troubleshooting/env-var reference generated from run_driveclub.sh --help - The README duplicates env defaults that have already drifted from the script (BB_READBACKS). A single source of truth avoids that.
- *Improvement:* Extend CI to shellcheck setup.sh, run_driveclub.sh and play.sh and to run a dry-run of setup.sh dependency detection - CI builds the code but never lints or smoke-tests the owner's own scripts, which are the main user entry point.

## runtime-services

_These files are mostly Bloodborne-port code, plus Driveclub additions in runtime_dc.c, runtime_content.c, runtime_font.c and the Driveclub region fallback in runtime_savedata.c. File I/O is solid: the ".." check, the read-only /app0 enforcement and the temp-file-plus-rename save commit all hold up. The real defects are these. (1) The fixed 16-entry mount table, combined with a no-op AddcontUnmount, exhausts DLC and savedata mounts in Driveclub. (2) The AJM batch and RIFF parsers have missing bounds checks. (3) Guest-supplied title ids are not validated in savedata, which allows path traversal and recursive delete. (4) Several non-atomic writes and unsynchronised statics. I did not compile anything: the SDL3 headers are not installed here. Nothing was modified._


### [high] Fixed 16-entry mount table plus no-op AddcontUnmount breaks DLC and savedata mounts in Driveclub
- id `runtime-services-1` · status **confirmed** (low,low) · `runtime-linux/src/runtime_file.c:93`
- MAX_MOUNTS is 16 and five of the slots are taken at startup (/app0, /hostapp, /temp0, /download0, /data). Every sceAppContentAddcontMount adds an /addcontN mount (one per add-on, and Driveclub has more than 1000), but sceAppContentAddcontUnmount is stubbed to ok() in runtime_dc.c, so those mounts are never freed. After about 11 distinct add-on or s
- **Fix:** Raise MAX_MOUNTS (to 64 or more, or make the table dynamic). Implement AddcontUnmount to call runtime_file_unmount on the matching /addcontN. Check the return value of runtime_file_mount in the saveda

### [medium] parse_riff underflows the input size and the decoder then reads past the guest buffer
- id `runtime-services-2` · status **confirmed** (medium,low) · `runtime-linux/src/runtime_ajm.c:234`
- parse_riff does `p+=12` without checking against `end`. It also does `p+=length` with a guest-controlled 32-bit chunk length, so p can land beyond `end`. After the loop `*size-=(p-*input)` underflows the uint64 size whenever p>end. This happens for a RIFF input of 4 to 11 bytes (run_job only checks in_size>=4), or any chunk whose length exceeds the
- **Fix:** Require *size>=12 before skipping the header. Clamp: if length>(uint64_t)(end-p) then p=end and break. Never let p exceed end, so the final subtraction cannot wrap.

### [medium] parse_job reads chunk bodies without checking they fit inside the job
- id `runtime-services-3` · status **confirmed** (low,low) · `runtime-linux/src/runtime_ajm.c:258`
- The loop condition is only `p<end`. Each case then memcpy's 8 or 16 bytes (flags, input/output run, control buffers) or skips 16 (return address) without verifying that p+8 or p+16 is <= end. A truncated or malformed final chunk makes parse_job read up to 15 bytes past the job (and past the batch buffer for the last job) and use them as an address
- **Fix:** Before each case check `(size_t)(end-p)>=chunkbytes`, otherwise return -1. In ajm_batch_start require `end-p>=8` and chunk.size<=end-body per iteration, and check `out`.

### [medium] Guest-supplied title id is used unvalidated in save paths, giving path traversal and recursive delete
- id `runtime-services-4` · status **confirmed** (low,low) · `runtime-linux/src/runtime_savedata.c:122`
- root() formats the guest's TitleId straight into '<user>/savedata/<user>/<title>'. valid_name() is applied only to the dir name, never to the title. A title such as '../../..' redirects mount, search, SaveDataMemory and sceSaveDataDelete outside the savedata tree. save_delete then calls nftw/remove on '<anything>/<dirname>' and '<dirname>.sce_sys'
- **Fix:** Copy the title with strnlen bounded to 10, and reject it unless it is [A-Za-z0-9_]+ (no '/', '\\' or '.'). Apply the same validation to every path component from the guest.

### [medium] Save files opened with O_CREAT or O_TRUNC but never written are silently not created or truncated
- id `runtime-services-5` · status **confirmed** (low,low) · `runtime-linux/src/runtime_file.c:350`
- On save paths open_for_commit works on a temp copy, and commit_file discards the temp when no write happened, as the comment says ('opened for writing, not written'). That drops two valid operations. An open(O_CREAT) followed directly by close never creates the file. An open(O_TRUNC) followed by close without a write leaves the original contents in
- **Fix:** Set dirty=1 at open time when the flags include O_TRUNC, or when O_CREAT creates a file that did not exist before.

### [medium] Savedata param.bin, icon0.png and SaveDataMemory are written non-atomically in place
- id `runtime-services-6` · status **confirmed** (low,low) · `runtime-linux/src/runtime_savedata.c:143`
- The game's own save files use temp+rename, but write_param does fopen("wb") followed by fwrite and ignores errors. save_icon truncates and rewrites icon0.png, and does not check data_size<=buffer_size. memory_io updates memory.dat with fopen("r+b") plus an in-place fwrite with no fsync or rename. A crash or kill mid-write corrupts the profile or me
- **Fix:** Write to '<file>.tmp', fsync, then rename, as runtime_file.c does. Return the error from write_param. Validate icon->data_size<=icon->buffer_size.

### [medium] AppContent add-on cache and label buffers are unsynchronised statics
- id `runtime-services-7` · status **confirmed** (low,low) · `runtime-linux/src/runtime_content.c:126`
- collect_labels mutates the globals cached_labels, cached_count, cached and last_dlc_env with no lock. addon_list uses a function-static 34 KB labels array, and runtime_addcont_info and runtime_addcont_mount each use another one. Two guest threads calling concurrently (Driveclub queries per label, with over 1000 add-ons) race on the first scan and o
- **Fix:** Guard collect_labels with a mutex and drop the memcpy-per-call by indexing cached_labels directly under the lock. Compare the env var by full string (strdup it) or by a hash.

### [low] Guest fd table is accessed without the lock; a racing close can use a freed Listing or reused fd
- id `runtime-services-8` · status **confirmed** (low,low) · `runtime-linux/src/runtime_file.c:260`
- get(), host_fd(), host_fd_written(), do_lseek and do_fstat read or modify files[fd] without holding `lock`. do_close copies and zeroes the entry under the lock, then closes the host fd and frees the listing after unlocking. A concurrent read/lseek/write on the same fd can use a closed (or already reused) host descriptor, write the wrong file, or to
- **Fix:** Take `lock` in host_fd/host_fd_written/do_lseek/do_fstat and copy out the host fd, or use per-entry refcounts so close waits for in-flight operations.

### [low] Bounds check in memory_io can wrap
- id `runtime-services-9` · status **confirmed** (low,low) · `runtime-linux/src/runtime_savedata.c:364`
- `(uint64_t)offset+size>memory_size` wraps for a huge size. For example offset=1 and size=UINT64_MAX-0 gives 0, which passes. fwrite/fread then try to move 2^64-1 bytes against the guest buffer. Reads are cut short by EOF, but a write would read far past the guest buffer. The size is also never compared to memory_size on its own.
- **Fix:** Use `size>memory_size || (uint64_t)offset>memory_size-size`.

### [low] Audio port pointer used after table_lock is released; reopen re-inits a possibly-held mutex
- id `runtime-services-10` · status **confirmed** (low,low) · `runtime-linux/src/runtime_audio.c:127`
- output_port, audio_volume, audio_state and audio_last_time fetch Port* under table_lock, drop it, then use the port. audio_close then audio_open on the same slot can run in between. audio_open does memset(p,0,...) and pthread_mutex_init on p->lock, which an output thread may be holding or about to lock (output_port holds it while sleeping for a ful
- **Fix:** Initialise each port mutex once at startup and never memset it. Re-check p->used after taking p->lock.

### [low] input.ini float parsing accepts NaN
- id `runtime-services-11` · status **confirmed** (low,low) · `runtime-linux/src/runtime_input_config.c:235`
- strtod accepts 'nan'. Every range check in ParseMouseMovementParams and ParseMouseCameraSensitivity is written as 'x<lo || x>hi', which is false for NaN. NaN therefore passes validation and is stored in cfg->mouse. mouse_to_axis then feeds it to lround()/atan2 (undefined or garbage stick output), and the camera sensitivity is NaN.
- **Fix:** Reject non-finite values, or write the checks positively: `if (!(v>=0.05f && v<=20.0f))`.

### [low] sceSaveDataDirNameSearch: thread-unsafe sort globals, uninitialised bytes copied to guest, unchecked realloc
- id `runtime-services-12` · status **confirmed** (low,low) · `runtime-linux/src/runtime_savedata.c:295`
- compare() depends on the globals sort_key and sort_order, and save_search does not take `lock`, so concurrent searches can sort with the wrong key. entries[] is realloc'd (unchecked, NULL deref on OOM) and each name is snprintf'd into a 32-byte field, but all 32 bytes are memcpy'd to the guest names array, leaking the uninitialised tail of heap mem
- **Fix:** memset the Entry or the name before snprintf, check realloc, and hold `lock` (or use qsort_r) during sorting.

### [low] sceFontOpenFontSet ignores the requested set type; no glyph fallback and kerning is a stub
- id `runtime-services-13` · status **confirmed** (low,low) · `runtime-linux/src/runtime_font.c:149`
- open_font_set ignores `type` and `mode`, so every PS4 system font set (Latin, Japanese, Korean, Chinese, Arabic and so on) resolves to a single Latin TTF. glyph_metrics and render_glyph return NO_SUPPORT_GLYPH for any CJK, Cyrillic or Arabic code point. The candidates list has no CJK font, and there is no per-glyph fallback face. kerning() always r
- **Fix:** Keep a small fallback face list (Noto Sans CJK/Arabic) and retry FT_Get_Char_Index on fallback faces. Implement FT_Get_Kerning. Track live Font* in a table to validate handles.
- *Improvement:* Raise or remove the mount cap and implement unmount semantics for add-ons - MAX_MOUNTS=16 is a Bloodborne-era constant. Driveclub's 1000+ add-ons, five base mounts and 16 save slots can never fit. A dynamic array (or 64+ entri
- *Improvement:* Ship Driveclub-specific default input bindings - DEFAULT_KEYBOARD/DEFAULT_CONTROLLER and the comments in runtime_input_config.c/runtime_pad.c are Bloodborne-centric (CUSA03173, lock-on on Q/C, Circle
- *Improvement:* Remove or gate Bloodborne leftovers - The 'sound hack' for CUSA00207 and friends in runtime_savedata.c (harmless for DC, but it touches save files by title id), the bb:q/bb:p/bb:d/bb:t qui
- *Improvement:* Avoid per-frame SDL_GetGamepads enumeration when no pad is connected - current_gamepad() calls SDL_GetGamepads (allocates and enumerates) on every pad_read_state while no gamepad is open (keyboard-only play, i.e. every fr
- *Improvement:* Add a bounds-checked cursor helper for the AJM chunk parsers and fuzz it - parse_job, ajm_batch_start and parse_riff all walk guest-controlled length fields with ad-hoc pointer math (RS-02, RS-03). A tiny 'take(n)' reader tha
- *Improvement:* Factor the duplicated Windows/POSIX do_open and share the save-commit code; add tests for save semantics - do_open has two near-identical branches, and the font and content scanners duplicate file-loading code. The save commit/crash behaviour (temp copy, st

## buffer-texture-amdgpu

_Mostly upstream shadPS4 logic plus Bloodborne-port (bbport) and owner async-readback edits. The worst real defects are in the guest-controlled PM4 parsing (liverpool.cpp / pm4_cmds.h): register, const-RAM and cs_state writes have no bounds checks, and packet size is checked only after the packet is applied. The owner's async readback (buffer_cache.cpp AsyncReadbackFrame) is functionally reasonable but has a stale GPU-modified-bit clobber, a game-specific zero heuristic that can pin stale data, and writes guest memory from a worker thread without any invalidation or generation check. The fault-buffer parser can read past its area, and GetArena's arena-migration has a wrong base page. Texture cache is largely upstream and mostly sound (SlotVector never moves, so the "insert may move images" comments are stale). I did not trace every Rasterizer caller, so the OOB items are confirmed in the cited code but not shown to be reached by Driveclub's own command streams._


### [high] SetConfig/Context/Sh/UconfigReg memcpy has no bounds check against reg_array
- id `buffer-texture-amdgpu-1` · status **confirmed** (medium,low) · `runtime-linux/gpu/shadps4/video_core/amdgpu/liverpool.cpp:511`
- ApplyGraphicsRegisterPacket copies (count-1) guest dwords into regs.reg_array[BaseOffset + reg_offset]. reg_offset is a 16-bit guest field (pm4_cmds.h:188) and count is up to 16383, but reg_array has only Regs::NumRegs = 0xD000 entries (regs.h:25,169). E.g. UconfigRegWordOffset 0xC000 + 0xFFFF, or any offset+count past 0xD000, writes outside the Re
- **Fix:** Before each memcpy, check Base + reg_offset + (count-1) <= Regs::NumRegs (and that the packet fits in the remaining span); otherwise log and skip the packet.

### [high] WriteConstRam/DumpConstRam overflow the 48 KB constants heap
- id `buffer-texture-amdgpu-2` · status **confirmed** (medium,low) · `runtime-linux/gpu/shadps4/video_core/amdgpu/liverpool.cpp:428`
- constants_heap is std::array<u8,48_KB>. WriteConstRam copies header.count*4 bytes to heap+Offset() where Offset is a 16-bit guest field (up to 65535), so offset+size can exceed 49152 and write past the static array. DumpConstRam (line 439) reads num_dw<<2 (15-bit, up to 128 KB) from heap+Offset() and writes it to a guest-supplied address, giving an
- **Fix:** Reject if Offset()+Size() > constants_heap.size(); validate the dump destination with IsMapped.

### [high] SetShReg to compute state: only size is checked, not offset+size (cs_state OOB write)
- id `buffer-texture-amdgpu-3` · status **confirmed** (medium,low) · `runtime-linux/gpu/shadps4/video_core/amdgpu/liverpool.cpp:916`
- The compute-program range test is reg_offset in [0x200, 0x200+sizeof(ComputeProgram)/4] (inclusive upper bound, itself off by one) and ASSERT(set_size <= sizeof(ComputeProgram)). The destination is cs_state + (reg_offset-0x200) words, so reg_offset near the end plus set_size up to sizeof(ComputeProgram) writes up to ~2x past the cs_state member int
- **Fix:** Require (reg_offset-0x200)*4 + set_size <= sizeof(ComputeProgram); use '<' for the upper reg_offset bound; skip and log instead of ASSERT.

### [medium] Fault-buffer parser trusts a GPU counter that can exceed the area size
- id `buffer-texture-amdgpu-4` · status **confirmed** (medium,medium) · `runtime-linux/gpu/shadps4/video_core/buffer_cache/fault_manager.cpp:169`
- fault_buffer_process.comp does atomicAdd(download_buffer32[0],1) for every set bit and only stops storing when store_index >= 1024, but the counter keeps being incremented by every invocation that overflows. The deferred op then loops i=1..fault_count reading fault_buf[i] with no clamp to MaxPageFaults-1 (area is 1024 u64). Counts above 1023 read i
- **Fix:** const u32 fault_count = std::min<u32>(fault_buf[0], MaxPageFaults - 1);

### [medium] Compute queue tmp_packet (1024 dwords) overflowed by large or split packets
- id `buffer-texture-amdgpu-5` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/amdgpu/liverpool.cpp:1393`
- Pm4BufferSize is 1024 dwords but a type-3 packet can be 0x4000+1 dwords. When a packet straddles the ring end, memcpy(tmp_packet, acb.data(), acb.size_bytes()) (1400) copies acb.size() dwords, which can exceed 1024. In the continuation branch, next_dw_off dwords are memcpy'd from acb (1393) BEFORE the check next_dw_off > acb.size() at ~1399, so it
- **Fix:** Check sizes (<= Pm4BufferSize, and available >= needed) before each memcpy; on a continuation that is still incomplete, append at tmp_dwords instead of restarting.

### [medium] Graphics WriteData uses NumWords-2 data dwords (one too many)
- id `buffer-texture-amdgpu-6` · status **confirmed** (low,medium) · `runtime-linux/gpu/shadps4/video_core/amdgpu/liverpool.cpp:1187`
- Local `count` is type3.NumWords() = count_field+1 (the body dword count). WriteData body = control + addr_lo + addr_hi + data, so payload dwords = count-3. The code uses (count-2) at 1187/1191/1193/1196, while RunWriteData (335) and the compute path (1566) correctly use count_field-2. Result: for a normal 4-byte label write num_bytes is 8; NotePend
- **Fix:** Use header->type3.count.Value() - 2 (or count - 3) consistently and reject count < 3.

### [medium] Async readback completion unmarks GPU-modified bits written by later frames
- id `buffer-texture-amdgpu-7` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp:335`
- The deferred op runs on the scheduler priority thread after the GPU finishes the copy, possibly frames later. It calls UnmarkRegionAsGpuModified(r.addr, r.size) unconditionally, with no generation/tick check. If the same range was bound as written again in a newer frame (ObtainBuffer -> ForEachUploadRange marks GPU bit, and gpu_modified_ranges.Add)
- **Fix:** Record a per-range generation (e.g. a counter bumped in ObtainBuffer when is_written) and only unmark/write if unchanged; or do not unmark in async mode.

### [medium] Zero-clobber guard looks only at the first 8 bytes of guest memory and drops legitimate GPU zeros
- id `buffer-texture-amdgpu-8` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp:325`
- For any readback of size <= 4 KB whose downloaded data is all zero, the write-back is skipped if the first min(size,8) bytes of current guest memory are non-zero. This is a title-specific exposure hack applied to all small GPU-written buffers: counters, particle counts, indirect-argument buffers, histogram bins or flags that the GPU legitimately cl
- **Fix:** Gate behind a setting/env (default off or limited to known exposure buffers), or detect the case by a GPU-side marker rather than by content; at minimum still unmark the GPU-modified region.

### [medium] WaitRegMem register compare indexes reg_array with an unchecked 16-bit guest value
- id `buffer-texture-amdgpu-10` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/amdgpu/pm4_cmds.h:700`
- PM4CmdWaitRegMem::Test does regs[Reg()] where Reg() is a 16-bit field (up to 65535) and `regs` is the span over reg_array of 0xD000 entries (liverpool.cpp:1249, 1263, 1608). std::span operator[] is unchecked in release, so a register-space wait with reg >= 0xD000 reads out of bounds. In memory space it also dereferences a raw guest pointer (*Addres
- **Fix:** if (Reg() >= regs.size()) return true (and log); validate Address() before dereferencing.

### [medium] Bloodborne-specific NOP-hint ASSERTs abort on any other CB/DB setup sequence
- id `buffer-texture-amdgpu-11` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/amdgpu/liverpool.cpp:866`
- SetContextReg for CbColorNBase/Cmask/DbZInfo assumes a trailing NOP hint at payload[count] for count in {0x0e,0x0d,0x0b,0x04,8} and ASSERT_MSG(payload[nop_offset]==0xc0001000) (lines 866, 889, 897). A different libGnm build or a packet at the end of a submission (no following NOP) kills the process; the read of payload[nop_offset+1]/payload[21] is
- **Fix:** Check dcb.size() covers the hint words and fall back to extent=0 with a warning instead of ASSERT.

### [medium] GDS offsets/sizes from DMA/EOS packets are not bounded to the 64 KB GDS buffer
- id `buffer-texture-amdgpu-12` · status **confirmed** (low,low) · `runtime-linux/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:3478`
- FillBuffer/CopyBuffer pass the guest's dst_addr_lo/src_addr_lo directly as an offset into the GDS buffer (GDS_BUFFER_SIZE = 64_KB, buffer_cache.cpp:34) with a 21-bit num_bytes, so offset+size can exceed the buffer (invalid vkCmdFillBuffer/CopyBuffer: device loss or driver fault). ReadDataFromGds (3521) memcpy's from gds_buf->mapped_data.data()+gds_
- **Fix:** Validate offset+size <= GDS_BUFFER_SIZE and mapped_data non-empty in the three paths.

### [medium] Guest-controlled data reaches ASSERT/UNREACHABLE and raw-pointer fallbacks
- id `buffer-texture-amdgpu-13` · status **contested** (low,low) · `runtime-linux/gpu/shadps4/video_core/amdgpu/liverpool.cpp:289`
- ASSERT(dma_data->command.das==0) (289, 1450), ASSERT on WriteData dst_sel/wr_one_addr (~1177), UNREACHABLE for unknown opcodes, unknown DMA src/dst combos, SetBase index, WaitRegMem engine (1607) and unknown DataSelect in SignalFence all terminate the emulator on a single unexpected packet. Fence helpers fall back to memcpy(address,...) on a raw gu
- **Fix:** Validate remaining size against NumWords()+1 at the top of the loop; replace asserts/UNREACHABLE on guest fields with LOG_ERROR + skip; drop the raw memcpy fallback.

### [low] Sampler GC destroys VkSamplers immediately while in-flight work may use them
- id `buffer-texture-amdgpu-14` · status **contested** (low,none) · `runtime-linux/gpu/shadps4/video_core/texture_cache/texture_cache.cpp:1265`
- GarbageCollectSamplers erases the Sampler (RAII createSamplerUnique, sampler.cpp:66) as soon as it is older than 16-160 gc ticks, with no scheduler tick deferral. Images are retired through scheduler.DeferOperation (DeleteImage), samplers are not, so a sampler referenced by a command buffer still executing is destroyed (VUID violation, driver-depen
- **Fix:** Move the erased Sampler into scheduler.DeferOperation (keeps it alive until the GPU reaches the current tick) and read size() under the lock.
- *Improvement:* Central PM4 packet validator - One helper that checks (a) packet dwords <= remaining span, (b) reg_offset+count within Regs::NumRegs / ComputeProgram / constants heap, (c) opcode kn
- *Improvement:* Make async-readback policy explicit and testable - The readback path mixes env parsing in two static lambdas (BB_ASYNC_READBACK parsed twice, mode '2'), a title-specific zero heuristic, a 8 MB budget a
- *Improvement:* Clamp the GPU fault counter in the shader and the CPU loop - Add min(counter, MAX_PAGE_FAULTS-1) in fault_buffer_process.comp (or stop incrementing once full) and clamp fault_count on the CPU; cheap and removes 
- *Improvement:* Defer VkSampler destruction like images - Reuse Scheduler::DeferOperation (already used by DeleteImage) for sampler eviction so in-flight command buffers stay valid.
- *Improvement:* Remove stale 'insert may move slot_images' workarounds and use memmove - SlotVector reserves virtual pages up front (common/slot_vector.h) and never relocates elements, so the re-lookups and comments in ResolveDepthOverlap 
- *Improvement:* UffdImpl lifetime hygiene - The userfaultfd fd is never closed (destructor is = default) and ufd_thread is declared first so it is destroyed last, after the members it indirectly
