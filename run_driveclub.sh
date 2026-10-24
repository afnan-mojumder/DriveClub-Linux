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

# Parse CLI arguments
SHOW_HELP=0
EXTRA_ARGS=()

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
    echo "  --fps <limit>           Set maximum framerate target (e.g., 30, 60, 120)"
    echo "  --res <WxH>             Set window resolution (e.g., 1920x1080, 1280x720, 2560x1440)"
    echo "  --readbacks <mode>      GPU readback mode (0=Disabled, 1=Relaxed, 2=Precise [default: 2])"
    echo "  -h, --help              Show this help message"
    echo ""
    echo "Environment Overrides:"
    echo "  BB_FPS_LIMIT            Target framerate cap (default: 60)"
    echo "  BB_FULLSCREEN           1 for fullscreen, 0 for windowed (default: 0)"
    echo "  BB_WINDOW_SIZE          Window dimensions (default: 1920x1080)"
    echo "  BB_READBACKS            GPU readbacks: 0=Disabled, 1=Relaxed, 2=Precise (default: 2)"
    echo "  BB_USER_NAME            In-game player name (default: Player)"
    echo "  DC_LANGUAGE             PS4 language code (1=English, 2=French, 4=German, etc.)"
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

# Hardware Detection for Display
GPU_INFO="Vulkan Host Device"
if command -v lspci >/dev/null 2>&1; then
    GPU_INFO="$(lspci | grep -i -E "vga|3d|display" | sed -E 's/.*: //; s/\(rev .*\)//' | head -n 1 || echo "Vulkan Host Device")"
fi

# Environment configuration
export LD_LIBRARY_PATH="$GPU_LIB_DIR:${LD_LIBRARY_PATH:-}"
export BB_GPU_USER_DIR="$DATA_DIR/user"
export BB_CONFIG="${BB_CONFIG:-$DATA_DIR/dcport.ini}"
export BB_LENIENT_IMPORTS=1
export BB_READBACKS="${BB_READBACKS:-2}"
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

# 4. Pure Native PS4 Engine Rendering:
#    Disables experimental temporal upscaler injection which causes buffer conflicts.
export BB_UPSCALER=none
export BB_DC_UPSCALE=0
unset BB_THREADED_RECORDING
unset BB_FSR1
unset BB_RCAS
export BB_PIPELINE_CACHE=1

# Display settings & Frame Pacing:
# Setting BB_VBLANK_HZ=0 runs virtual vblank at 480 Hz (~2 ms intervals) rather than
# a rigid 60 Hz (16.6 ms). This eliminates the harsh drop to 30/25 FPS whenever a frame
# slightly exceeds 16.6 ms, allowing smooth dynamic pacing locked to 60 FPS by BB_FPS_LIMIT=60.
export BB_FULLSCREEN="${BB_FULLSCREEN:-0}"
export BB_WINDOW_SIZE="${BB_WINDOW_SIZE:-1920x1080}"
export BB_VBLANK_HZ="${BB_VBLANK_HZ:-0}"
export BB_FPS_LIMIT="${BB_FPS_LIMIT:-60}"

echo "=========================================================="
echo " Starting Driveclub Native Linux Port (CUSA00003) "
echo " Renderer: Native Vulkan on $GPU_INFO"
echo " Target: ${BB_FPS_LIMIT} FPS | Mode: Stable Pure Native (Zero Artifacts)"
echo "=========================================================="

PATCHES_ARG=()
if [[ -f "$DATA_DIR/patches.bin" ]]; then
    PATCHES_ARG=(--patches "$DATA_DIR/patches.bin")
fi

exec "$PROBE_BIN" \
    "$DATA_DIR/boot-linked.bin" \
    --content-profile "$DATA_DIR/content.bin" \
    ${PATCHES_ARG[@]+"${PATCHES_ARG[@]}"} \
    --app0 "$GAME_DIR" \
    --user "$DATA_DIR/user" \
    --timeout 0 \
    ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}
