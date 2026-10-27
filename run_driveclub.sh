#!/usr/bin/env bash
# ==============================================================================
# Driveclub Native Linux Port Launcher
# Runs Driveclub (PS4, CUSA00003 / CUSA00093) natively on Linux via Vulkan.
# ==============================================================================

set -euo pipefail
cd -- "$(dirname -- "$0")"

SCRIPT_DIR="$(pwd)"
DATA_DIR="$SCRIPT_DIR/data"
GAME_DIR="${DC_GAME_DIR:-$SCRIPT_DIR/game/CUSA00003}"
PROBE_BIN="$SCRIPT_DIR/runtime-linux/out/driveclub-probe"
GPU_LIB_DIR="$SCRIPT_DIR/runtime-linux/out/gpu"

# Terminal formatting
if [[ -t 1 ]]; then
    BOLD='\033[1m'
    GREEN='\033[0;32m'
    BLUE='\033[0;34m'
    YELLOW='\033[1;33m'
    RED='\033[0;31m'
    CYAN='\033[0;36m'
    NC='\033[0m'
else
    BOLD=''
    GREEN=''
    BLUE=''
    YELLOW=''
    RED=''
    CYAN=''
    NC=''
fi

# Parse CLI arguments
SHOW_HELP=0
EXTRA_ARGS=()
USE_GAMEMODE=0
DISABLE_PINNING=0
CUSTOM_P_CORES=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -f|--fullscreen)
            export BB_FULLSCREEN=1
            shift
            ;;
        -w|--windowed)
            export BB_FULLSCREEN=0
            shift
            ;;
        --fps)
            export BB_FPS_LIMIT="$2"
            shift 2
            ;;
        --res|--resolution)
            export BB_WINDOW_SIZE="$2"
            shift 2
            ;;
        --readbacks)
            export BB_READBACKS="$2"
            shift 2
            ;;
        --gamemode)
            USE_GAMEMODE=1
            shift
            ;;
        --mangohud)
            export MANGOHUD=1
            shift
            ;;
        --no-pinning)
            DISABLE_PINNING=1
            shift
            ;;
        --p-cores)
            CUSTOM_P_CORES="$2"
            shift 2
            ;;
        --staging-mb)
            export BB_STAGING_KEEP_MB="$2"
            shift 2
            ;;
        --uffd)
            export BB_UFFD="$2"
            shift 2
            ;;
        --fault-window)
            export BB_FAULT_WINDOW="$2"
            shift 2
            ;;
        --upscaler)
            export BB_UPSCALER="$2"
            shift 2
            ;;
        --present-mode)
            export BB_PRESENT_MODE="$2"
            shift 2
            ;;
        --blur-auto)
            export BB_DC_BLUR_AUTO="$2"
            shift 2
            ;;
        -h|--help)
            SHOW_HELP=1
            shift
            ;;
        *)
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

if [[ "$SHOW_HELP" == "1" ]]; then
    echo "Driveclub Native Linux Port Launcher"
    echo "Usage: ./run_driveclub.sh [options]"
    echo ""
    echo "Options:"
    echo "  -f, --fullscreen        Run in fullscreen mode"
    echo "  -w, --windowed          Run in windowed mode (default)"
    echo "  --fps <limit>           Set maximum framerate target (e.g., 30, 45, 60, 120)"
    echo "  --res <WxH>             Set window resolution (e.g., 1280x800, 1920x1080, 2560x1440)"
    echo "  --readbacks <mode>      GPU readback mode (0=Disabled, 1=Relaxed, 2=Precise [default: 1])"
    echo "  --upscaler <mode>       Upscaler provider (none, fsr3, fsr4, fsr411, dlss [default: none])"
    echo "  --present-mode <mode>   Vulkan present mode (Mailbox, Fifo, FifoRelaxed, Immediate [default: Mailbox])"
    echo "  --blur-auto <0|1>       Auto scale motion blur for high framerates (default: 1)"
    echo "  --gamemode              Launch with Feral GameMode (gamemoderun)"
    echo "  --mangohud              Launch with MangoHud overlay enabled"
    echo "  --no-pinning            Disable automatic CPU P-core pinning on hybrid architectures"
    echo "  --p-cores <list>        Manually specify CPU core list for pinning (e.g., 0-7, 0-3,8-11)"
    echo "  --staging-mb <MB>       Set staging memory retention (default: 512 Desktop, 256 Deck)"
    echo "  --uffd <0|1>            Enable/disable userfaultfd tracking (default: 1)"
    echo "  --fault-window <bytes>  Set memory fault unprotect window size (default: 262144 [256 KB])"
    echo "  -h, --help              Show this help message"
    echo ""
    echo "Environment Overrides:"
    echo "  BB_FPS_LIMIT            Target framerate cap (default: 60 Desktop / 45 Steam Deck)"
    echo "  BB_FULLSCREEN           1 for fullscreen, 0 for windowed (default: 0)"
    echo "  BB_WINDOW_SIZE          Window dimensions (default: 1920x1080 / 1280x800 Steam Deck)"
    echo "  BB_READBACKS            GPU readbacks: 0=Disabled, 1=Relaxed, 2=Precise (default: 1)"
    echo "  BB_UPSCALER             Upscaler provider: none, fsr3, fsr4, fsr411, dlss (default: none)"
    echo "  BB_PRESENT_MODE         Vulkan present mode: Mailbox, Fifo, FifoRelaxed (default: Mailbox)"
    echo "  BB_DC_BLUR_AUTO         1 to auto-scale motion blur for target FPS (default: 1)"
    echo "  BB_STAGING_KEEP_MB      Staging buffer memory retention in MB (default: 512 / 256 Deck)"
    echo "  BB_UFFD                 1 to track writes with userfaultfd, 0 for mprotect (default: 1)"
    echo "  BB_FAULT_WINDOW         Memory fault unprotect window size in bytes (default: 262144)"
    echo "  BB_USER_NAME            In-game player name (default: Player)"
    echo "  DC_LANGUAGE             PS4 language code (1=English, 2=French, 4=German, etc.)"
    echo "  DC_PINNING              Set to 0 to disable automatic P-core pinning"
    echo "  GAMEMODE                Set to 1 to enable gamemoderun"
    exit 0
fi

# Ensure data and user directories exist
mkdir -p "$DATA_DIR/user"

# Verify prerequisites
if [[ ! -f "$PROBE_BIN" ]]; then
    echo "ERROR: driveclub-probe binary not found at $PROBE_BIN" >&2
    echo "Please run: ./setup.sh to build the runtime." >&2
    exit 1
fi

if [[ ! -f "$DATA_DIR/boot-linked.bin" ]]; then
    echo "ERROR: boot-linked.bin not found at $DATA_DIR/boot-linked.bin" >&2
    echo "Please run: ./setup.sh to prepare game assets." >&2
    exit 1
fi

if [[ ! -d "$GAME_DIR" || ! -f "$GAME_DIR/eboot.bin" ]]; then
    # Try looking for CUSA00093 (US version)
    if [[ -d "$SCRIPT_DIR/game/CUSA00093" && -f "$SCRIPT_DIR/game/CUSA00093/eboot.bin" ]]; then
        GAME_DIR="$SCRIPT_DIR/game/CUSA00093"
    else
        echo "ERROR: Valid game files not found in $GAME_DIR" >&2
        echo "Please run: ./setup.sh /path/to/Driveclub" >&2
        exit 1
    fi
fi

# ------------------------------------------------------------------------------
# Hardware & Platform Detection
# ------------------------------------------------------------------------------

# 1. Steam Deck & Handheld APU Detection
IS_STEAM_DECK=0
DECK_MODEL=""
DMI_PRODUCT=""
DMI_VENDOR=""
if [[ -f /sys/devices/virtual/dmi/id/product_name ]]; then
    DMI_PRODUCT="$(cat /sys/devices/virtual/dmi/id/product_name 2>/dev/null || true)"
fi
if [[ -f /sys/devices/virtual/dmi/id/sys_vendor ]]; then
    DMI_VENDOR="$(cat /sys/devices/virtual/dmi/id/sys_vendor 2>/dev/null || true)"
fi

if [[ "$DMI_PRODUCT" =~ ^(Jupiter|Galileo)$ || "$DMI_VENDOR" =~ ^Valve ]]; then
    IS_STEAM_DECK=1
    DECK_MODEL="$DMI_PRODUCT"
    if [[ -z "$DECK_MODEL" ]]; then DECK_MODEL="Steam Deck"; fi
fi

# 2. CPU Architecture & Intel Hybrid P-Core Detection
detect_p_cores() {
    # 2a. PMU cpu_core / cpu_atom interface (standard on Linux for Intel hybrid)
    if [[ -f /sys/devices/cpu_core/cpus && -f /sys/devices/cpu_atom/cpus ]]; then
        local p_cores
        p_cores="$(cat /sys/devices/cpu_core/cpus 2>/dev/null || true)"
        if [[ -n "$p_cores" ]]; then
            echo "$p_cores"
            return 0
        fi
    fi

    # 2b. sysfs core_type (Linux 6.3+ hybrid topology)
    local p_core_list=()
    local has_core_type=0
    for core_type_file in /sys/devices/system/cpu/cpu[0-9]*/topology/core_type; do
        if [[ -f "$core_type_file" ]]; then
            has_core_type=1
            local c_type
            c_type="$(cat "$core_type_file" 2>/dev/null || true)"
            if [[ "$c_type" == "core" || "$c_type" == "1" ]]; then
                local cpu_num
                cpu_num="$(basename "$(dirname "$(dirname "$core_type_file")")" | sed 's/^cpu//')"
                p_core_list+=("$cpu_num")
            fi
        fi
    done
    if [[ "$has_core_type" == "1" && ${#p_core_list[@]} -gt 0 ]]; then
        (IFS=,; echo "${p_core_list[*]}")
        return 0
    fi

    # 2c. cpu_capacity differential
    local max_cap=0
    local min_cap=999999
    local has_capacity=0
    for cap_file in /sys/devices/system/cpu/cpu[0-9]*/cpu_capacity; do
        if [[ -f "$cap_file" ]]; then
            has_capacity=1
            local cap
            cap="$(cat "$cap_file" 2>/dev/null || echo 0)"
            if (( cap > max_cap )); then max_cap=$cap; fi
            if (( cap < min_cap )); then min_cap=$cap; fi
        fi
    done
    if [[ "$has_capacity" == "1" && $max_cap -gt $min_cap && $max_cap -gt 0 ]]; then
        local cap_p_cores=()
        for cap_file in /sys/devices/system/cpu/cpu[0-9]*/cpu_capacity; do
            local cap
            cap="$(cat "$cap_file" 2>/dev/null || echo 0)"
            if (( cap == max_cap )); then
                local cpu_num
                cpu_num="$(basename "$(dirname "$cap_file")" | sed 's/^cpu//')"
                cap_p_cores+=("$cpu_num")
            fi
        done
        if [[ ${#cap_p_cores[@]} -gt 0 ]]; then
            (IFS=,; echo "${cap_p_cores[*]}")
            return 0
        fi
    fi

    return 1
}

PIN_CORES=""
if [[ "$DISABLE_PINNING" == "0" && "${DC_PINNING:-1}" != "0" ]]; then
    if [[ -n "$CUSTOM_P_CORES" ]]; then
        PIN_CORES="$CUSTOM_P_CORES"
    elif P_DETECT="$(detect_p_cores)"; then
        PIN_CORES="$P_DETECT"
    fi
fi

# 3. GPU Vendor Detection & Driver Tuning
GPU_VENDOR="unknown"
GPU_INFO="Vulkan Host Device"
LSPCI_BIN="$(command -v lspci || command -v /usr/sbin/lspci || command -v /sbin/lspci || true)"

for vendor_file in /sys/class/drm/card[0-9]*/device/vendor; do
    if [[ -f "$vendor_file" ]]; then
        v_id="$(cat "$vendor_file" 2>/dev/null || true)"
        case "$v_id" in
            0x1002|1002) GPU_VENDOR="amd"; break ;;
            0x10de|10de) GPU_VENDOR="nvidia"; break ;;
            0x8086|8086) GPU_VENDOR="intel"; break ;;
        esac
    fi
done

if [[ "$GPU_VENDOR" == "unknown" && -n "$LSPCI_BIN" ]]; then
    pci_gpu="$("$LSPCI_BIN" -nn | grep -i -E "vga|3d|display" | head -n 1 || true)"
    if [[ "$pci_gpu" =~ \[1002\] || "$pci_gpu" =~ [Aa][Mm][Dd]|[Rr]adeon ]]; then
        GPU_VENDOR="amd"
    elif [[ "$pci_gpu" =~ \[10de\] || "$pci_gpu" =~ [Nn][Vv][Ii][Dd][Ii][Aa] ]]; then
        GPU_VENDOR="nvidia"
    elif [[ "$pci_gpu" =~ \[8086\] || "$pci_gpu" =~ [Ii]ntel ]]; then
        GPU_VENDOR="intel"
    fi
fi

if [[ -n "$LSPCI_BIN" ]]; then
    GPU_INFO="$("$LSPCI_BIN" | grep -i -E "vga|3d|display" | sed -E 's/.*: //; s/\(rev .*\)//' | head -n 1 || echo "Vulkan Host Device")"
fi

# Apply GPU Vendor Driver Optimizations
case "$GPU_VENDOR" in
    amd)
        # Enable ACO compiler performance features & Graphics Pipeline Library
        if [[ -z "${RADV_PERFTEST:-}" ]]; then
            export RADV_PERFTEST="sam,csw,nggc"
        else
            for flag in sam csw nggc; do
                if [[ ! ",$RADV_PERFTEST," =~ ,$flag, ]]; then
                    RADV_PERFTEST="$RADV_PERFTEST,$flag"
                fi
            done
            export RADV_PERFTEST
        fi
        ;;
    nvidia)
        # Configure persistent 10 GB disk cache and threaded optimizations
        export __GL_SHADER_DISK_CACHE=1
        export __GL_SHADER_DISK_CACHE_SIZE="${__GL_SHADER_DISK_CACHE_SIZE:-10737418240}"
        export __GL_SHADER_DISK_CACHE_SKIP_CLEANUP=1
        export __GL_THREADED_OPTIMIZATIONS="${__GL_THREADED_OPTIMIZATIONS:-1}"
        ;;
    intel)
        # Check Intel Arc Resizable BAR (ReBAR) status
        IS_ARC=0
        if [[ "$GPU_INFO" =~ (Arc|DG2|Alchemist|Battlemage) ]]; then
            IS_ARC=1
        fi
        if [[ "$IS_ARC" == "1" ]]; then
            REBAR_ACTIVE=0
            for dev in /sys/bus/pci/devices/*; do
                if [[ -f "$dev/vendor" && "$(cat "$dev/vendor" 2>/dev/null || true)" == "0x8086" ]]; then
                    if [[ -f "$dev/class" && "$(cat "$dev/class" 2>/dev/null || true)" =~ ^0x03 ]]; then
                        if [[ -f "$dev/resource" ]]; then
                            while read -r start end flags; do
                                if [[ -n "$start" && -n "$end" && "$start" != "0x0000000000000000" ]]; then
                                    sz=$(( end - start + 1 ))
                                    # BAR size > 1GB indicates ReBAR is active
                                    if (( sz > 1073741824 )); then
                                        REBAR_ACTIVE=1
                                    fi
                                fi
                            done < "$dev/resource"
                        fi
                    fi
                fi
            done
            if [[ "$REBAR_ACTIVE" == "0" ]]; then
                echo -e "${YELLOW}[WARN] Intel Arc GPU detected without Resizable BAR (ReBAR)!${NC}"
                echo -e "${YELLOW}[WARN] Performance may be degraded up to 50%. Please enable ReBAR/Above 4G Decoding in BIOS.${NC}"
            fi
        fi
        ;;
esac

# 4. Linux Kernel Memory Optimization Checks
export BB_UFFD="${BB_UFFD:-1}"
export BB_FAULT_WINDOW="${BB_FAULT_WINDOW:-262144}"

MAP_COUNT=0
if [[ -f /proc/sys/vm/max_map_count ]]; then
    MAP_COUNT="$(cat /proc/sys/vm/max_map_count 2>/dev/null || echo 0)"
elif command -v sysctl >/dev/null 2>&1; then
    MAP_COUNT="$(sysctl -n vm.max_map_count 2>/dev/null || echo 0)"
fi

if (( MAP_COUNT > 0 && MAP_COUNT < 2147483642 )); then
    echo -e "${YELLOW}[WARN] vm.max_map_count is $MAP_COUNT (recommended: 2147483642 for PS4 memory mapping).${NC}"
    echo -e "${YELLOW}[WARN] To fix temporarily: sudo sysctl -w vm.max_map_count=2147483642${NC}"
fi

# 5. Profile Defaults (Steam Deck vs Desktop)
if [[ "$IS_STEAM_DECK" == "1" ]]; then
    export BB_STAGING_KEEP_MB="${BB_STAGING_KEEP_MB:-256}"
    export BB_FPS_LIMIT="${BB_FPS_LIMIT:-45}"
    export BB_VBLANK_HZ="${BB_VBLANK_HZ:-45}"
    export BB_WINDOW_SIZE="${BB_WINDOW_SIZE:-1280x800}"
else
    export BB_STAGING_KEEP_MB="${BB_STAGING_KEEP_MB:-512}"
    export BB_FPS_LIMIT="${BB_FPS_LIMIT:-60}"
    export BB_VBLANK_HZ="${BB_VBLANK_HZ:-0}"
    export BB_WINDOW_SIZE="${BB_WINDOW_SIZE:-1920x1080}"
fi

# Environment configuration
export LD_LIBRARY_PATH="$GPU_LIB_DIR:${LD_LIBRARY_PATH:-}"
export BB_GPU_USER_DIR="$DATA_DIR/user"
export BB_CONFIG="${BB_CONFIG:-$DATA_DIR/dcport.ini}"
export BB_LENIENT_IMPORTS=1
export BB_READBACKS="${BB_READBACKS:-1}"
export BB_DRAW_PIPE=1
export BB_DMEM_MB=${BB_DMEM_MB:-8192}
export BB_USER_NAME="${BB_USER_NAME:-Player}"
export DC_LANGUAGE="${DC_LANGUAGE:-1}"

# Stability & Artifact Prevention:
# 1. BB_PREP_WORKERS=0: Draw preparation MUST run synchronously on the main thread;
#    asynchronous workers parse vertex inputs out of order in Driveclub, causing
#    severe vertex stretching (exploded meshes), flashing, and flickering.
export BB_PREP_WORKERS=0

# 2. Command buffer copying is disabled to save CPU cycles on every submit
#    (synchronous BB_PREP_WORKERS=0 already prevents race conditions).
export BB_COPY_GPU_BUFFERS="${BB_COPY_GPU_BUFFERS:-0}"

# 3. BB_OBJECT_MOTION=0: Prevents scanning 4.2M vertices per frame.
export BB_OBJECT_MOTION=0

# 4. Upscaler & Native Anti-Aliasing (FSR / DLSS):
export BB_UPSCALER="${BB_UPSCALER:-none}"
if [[ "$BB_UPSCALER" != "none" && "$BB_UPSCALER" != "off" ]]; then
    export BB_DC_UPSCALE=1
else
    export BB_DC_UPSCALE=0
fi

# 5. Presentation Mode & High-FPS Motion Blur:
export BB_PRESENT_MODE="${BB_PRESENT_MODE:-Mailbox}"
export BB_DC_BLUR_AUTO="${BB_DC_BLUR_AUTO:-1}"

export BB_PIPELINE_CACHE=1
export BB_FULLSCREEN="${BB_FULLSCREEN:-0}"

LAUNCH_PREFIX=()
if [[ "$USE_GAMEMODE" == "1" || "${GAMEMODE:-0}" == "1" ]]; then
    if command -v gamemoderun >/dev/null 2>&1; then
        LAUNCH_PREFIX+=(gamemoderun)
        echo -e "${GREEN}[OK] Performance: GameMode enabled (gamemoderun)${NC}"
    else
        echo -e "${YELLOW}[WARN] gamemoderun requested but not found in PATH; running normally${NC}" >&2
    fi
fi

if [[ -n "$PIN_CORES" ]]; then
    if command -v taskset >/dev/null 2>&1; then
        LAUNCH_PREFIX+=(taskset -c "$PIN_CORES")
        echo -e "${GREEN}[OK] Intel Hybrid Tuning: Process pinned to P-Cores ($PIN_CORES)${NC}"
    else
        echo -e "${YELLOW}[WARN] Hybrid P-cores detected ($PIN_CORES) but taskset is not installed${NC}" >&2
    fi
fi

echo "=========================================================="
echo " Starting Driveclub Native Linux Port (CUSA00003) "
echo " Renderer: Native Vulkan on $GPU_INFO"
echo " GPU Vendor Profile: $GPU_VENDOR"
if [[ "$IS_STEAM_DECK" == "1" ]]; then
    echo " Hardware Profile: Steam Deck ($DECK_MODEL) [45 Hz / 45 FPS, 256MB staging]"
else
    echo " Hardware Profile: Desktop PC [${BB_FPS_LIMIT} FPS, 512MB staging]"
fi
echo " Memory Subsystem: UFFD=${BB_UFFD}, FaultWindow=$(( BB_FAULT_WINDOW / 1024 )) KB, Staging=${BB_STAGING_KEEP_MB} MB"
echo " Target: ${BB_FPS_LIMIT} FPS | Mode: Stable Pure Native (Zero Artifacts)"
echo " Upscaler: ${BB_UPSCALER} | Present Mode: ${BB_PRESENT_MODE} | Readbacks: ${BB_READBACKS}"
echo "=========================================================="

PATCHES_ARG=()
if [[ -f "$DATA_DIR/patches.bin" ]]; then
    PATCHES_ARG=(--patches "$DATA_DIR/patches.bin")
fi

exec ${LAUNCH_PREFIX[@]+"${LAUNCH_PREFIX[@]}"} "$PROBE_BIN" \
    "$DATA_DIR/boot-linked.bin" \
    --content-profile "$DATA_DIR/content.bin" \
    ${PATCHES_ARG[@]+"${PATCHES_ARG[@]}"} \
    --app0 "$GAME_DIR" \
    --user "$DATA_DIR/user" \
    --timeout 0 \
    ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}
