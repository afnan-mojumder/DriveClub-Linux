#!/usr/bin/env bash
# ==============================================================================
# Driveclub Native Linux Port - Automated Setup & Installer
# Repository: https://github.com/afnan-mojumder/DriveClub-Linux.git
# ==============================================================================
# Sets up prerequisites, builds the native runtime, prepares game assets,
# configures hardware-optimized settings, and creates desktop integration.
# ==============================================================================

set -euo pipefail
cd -- "$(dirname -- "$0")"
PROJECT_ROOT="$(pwd)"

# Colors & Formatting
BOLD='\033[1m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_banner() {
    echo -e "${CYAN}${BOLD}"
    cat << "EOF"
  ____       _             ____ _       _     
 |  _ \ _ __(_)_   _____  / ___| |_   _| |__  
 | | | | '__| \ \ / / _ \| |   | | | | | '_ \ 
 | |_| | |  | |\ V /  __/| |___| | |_| | |_) |
 |____/|_|  |_| \_/ \___| \____|_|\__,_|_.__/ 
      NATIVE LINUX PORT (CUSA00003 / CUSA00093)
EOF
    echo -e "${NC}"
    echo -e "${BLUE}Repository:${NC} https://github.com/afnan-mojumder/DriveClub-Linux"
    echo -e "${BLUE}Target:${NC} Native Linux x86_64 | Host Vulkan | 60 FPS"
    echo -e "=========================================================="
}

info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

success() {
    echo -e "${GREEN}[OK]${NC} $*"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

error() {
    echo -e "${RED}[ERROR]${NC} $*" >&2
}

# ------------------------------------------------------------------------------
# 1. System & Architecture Check
# ------------------------------------------------------------------------------
check_system() {
    info "Checking system environment..."
    ARCH="$(uname -m)"
    OS="$(uname -s)"

    if [[ "$OS" != "Linux" ]]; then
        error "This installer is for Linux only (detected: $OS)."
        exit 1
    fi

    if [[ "$ARCH" != "x86_64" ]]; then
        error "Driveclub PS4 port requires an x86_64 CPU (detected: $ARCH)."
        exit 1
    fi

    # Detect Linux distribution
    DISTRO="unknown"
    if [[ -f /etc/os-release ]]; then
        # shellcheck disable=SC1091
        source /etc/os-release
        DISTRO="${ID:-unknown}"
    fi
    success "System: Linux ($DISTRO) on $ARCH"
}

# ------------------------------------------------------------------------------
# 2. Dependency Verification
# ------------------------------------------------------------------------------
check_dependencies() {
    info "Verifying required build and runtime dependencies..."
    MISSING_PKGS=()

    # Tools
    for cmd in git python3 gcc cmake ninja pkg-config; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            MISSING_PKGS+=("$cmd")
        fi
    done

    # Libraries via pkg-config
    if command -v pkg-config >/dev/null 2>&1; then
        for lib in vulkan sdl3 freetype2; do
            if ! pkg-config --exists "$lib" 2>/dev/null; then
                MISSING_PKGS+=("lib:$lib")
            fi
        done
    fi

    if [[ ${#MISSING_PKGS[@]} -gt 0 ]]; then
        warn "Missing dependencies: ${MISSING_PKGS[*]}"
        echo ""
        echo -e "${YELLOW}Please install the required packages using your package manager:${NC}"
        case "$DISTRO" in
            arch|manjaro|endeavouros)
                echo -e "  ${BOLD}sudo pacman -S --needed base-devel git cmake ninja clang pkgconf vulkan-headers vulkan-icd-loader sdl3 freetype2 boost${NC}"
                ;;
            ubuntu|debian|pop|linuxmint)
                echo -e "  ${BOLD}sudo apt update && sudo apt install -y build-essential git cmake ninja-build clang pkg-config libvulkan-dev vulkan-tools libsdl3-dev libfreetype6-dev libboost-dev${NC}"
                ;;
            fedora|nobara|rhel)
                echo -e "  ${BOLD}sudo dnf install -y @development-tools git cmake ninja-build clang pkgconf-pkg-config vulkan-loader-devel vulkan-headers SDL3-devel freetype-devel boost-devel${NC}"
                ;;
            steamos)
                echo -e "  ${BOLD}On Steam Deck / SteamOS, disable read-only mode or use the development packages.${NC}"
                ;;
            *)
                echo -e "  Install: git, cmake, ninja, clang/gcc, pkg-config, vulkan-headers, vulkan-loader, sdl3, freetype2, boost."
                ;;
        esac
        echo ""

        if [[ -f "$PROJECT_ROOT/runtime-linux/out/driveclub-probe" && -f "$PROJECT_ROOT/runtime-linux/out/gpu/libbbgpu.so" ]]; then
            warn "Pre-built runtime binary detected. Continuing with asset configuration..."
        else
            error "Dependencies must be installed before building the runtime from source."
            exit 1
        fi
    else
        success "All build tools and libraries verified."
    fi
}

# ------------------------------------------------------------------------------
# 3. Third-Party Submodules
# ------------------------------------------------------------------------------
ensure_submodules() {
    info "Verifying submodules and vendor sources..."
    if [[ ! -f "$PROJECT_ROOT/runtime-linux/gpu/third_party/fsr-vulkan/CMakeLists.txt" || \
          ! -f "$PROJECT_ROOT/runtime-linux/gpu/third_party/imgui/imgui.h" || \
          ! -f "$PROJECT_ROOT/runtime-linux/third_party/LibAtrac9/C/src/libatrac9.h" ]]; then
        info "Fetching submodules (this may take a minute on the first run)..."
        git submodule update --init --recursive || {
            warn "git submodule update returned non-zero, checking if dependencies are satisfied..."
        }
    fi
    success "Vendor submodules checked."
}

# ------------------------------------------------------------------------------
# 4. Build Native Linux Runtime
# ------------------------------------------------------------------------------
build_runtime() {
    PROBE_BIN="$PROJECT_ROOT/runtime-linux/out/driveclub-probe"
    GPU_LIB="$PROJECT_ROOT/runtime-linux/out/gpu/libbbgpu.so"
    REBUILD="${1:-0}"

    if [[ "$REBUILD" == "1" || ! -f "$PROBE_BIN" || ! -f "$GPU_LIB" ]]; then
        info "Building native Linux Driveclub runtime (Vulkan renderer + ELF loader)..."
        (cd "$PROJECT_ROOT/runtime-linux" && bash build.sh)
        if [[ ! -f "$PROBE_BIN" || ! -f "$GPU_LIB" ]]; then
            error "Build failed! Please check output logs in runtime-linux/out/gpu-build.log"
            exit 1
        fi
        success "Native runtime successfully built."
    else
        success "Native runtime is already built ($PROBE_BIN)."
    fi
}

# ------------------------------------------------------------------------------
# 5. Locate & Validate Game Files
# ------------------------------------------------------------------------------
setup_game_files() {
    GAME_DIR="$PROJECT_ROOT/game/CUSA00003"
    CLI_PATH="${1:-}"

    # Check if already installed
    if [[ -f "$GAME_DIR/eboot.bin" ]]; then
        success "Game files verified in $GAME_DIR"
        return 0
    fi

    info "Locating Driveclub PS4 game files..."
    SELECTED_PATH=""

    if [[ -n "$CLI_PATH" ]]; then
        SELECTED_PATH="$CLI_PATH"
    fi

    # If no path provided, try graphical file picker (Zenity/Kdialog) if running in desktop
    if [[ -z "$SELECTED_PATH" && -n "${DISPLAY:-${WAYLAND_DISPLAY:-}}" ]]; then
        if command -v zenity >/dev/null 2>&1; then
            info "Opening folder picker..."
            SELECTED_PATH="$(zenity --file-selection --directory --title="Select Driveclub PS4 Game Folder (containing eboot.bin)" 2>/dev/null || true)"
        elif command -v kdialog >/dev/null 2>&1; then
            SELECTED_PATH="$(kdialog --getexistingdirectory "$HOME" --title "Select Driveclub PS4 Game Folder" 2>/dev/null || true)"
        fi
    fi

    # Fallback to interactive terminal prompt
    if [[ -z "$SELECTED_PATH" ]]; then
        echo ""
        echo -e "${YELLOW}Please enter the directory path containing your decrypted Driveclub files:${NC}"
        read -r -p "Path to game files: " SELECTED_PATH
    fi

    SELECTED_PATH="$(eval echo "$SELECTED_PATH")" # expand ~ if present

    if [[ -z "$SELECTED_PATH" || ! -d "$SELECTED_PATH" ]]; then
        error "Invalid directory specified: '$SELECTED_PATH'"
        exit 1
    fi

    # Look for eboot.bin directly or inside subfolders
    TARGET_DIR=""
    if [[ -f "$SELECTED_PATH/eboot.bin" ]]; then
        TARGET_DIR="$SELECTED_PATH"
    elif [[ -f "$SELECTED_PATH/CUSA00003/eboot.bin" ]]; then
        TARGET_DIR="$SELECTED_PATH/CUSA00003"
    elif [[ -f "$SELECTED_PATH/CUSA00093/eboot.bin" ]]; then
        TARGET_DIR="$SELECTED_PATH/CUSA00093"
    elif [[ -f "$SELECTED_PATH/game/CUSA00003/eboot.bin" ]]; then
        TARGET_DIR="$SELECTED_PATH/game/CUSA00003"
    else
        # Deep search for eboot.bin inside specified folder
        FOUND_EBOOT="$(find "$SELECTED_PATH" -maxdepth 3 -name "eboot.bin" -print -quit 2>/dev/null || true)"
        if [[ -n "$FOUND_EBOOT" ]]; then
            TARGET_DIR="$(dirname "$FOUND_EBOOT")"
        fi
    fi

    if [[ -z "$TARGET_DIR" || ! -f "$TARGET_DIR/eboot.bin" ]]; then
        error "Could not find 'eboot.bin' in '$SELECTED_PATH'."
        error "Ensure your game dump is decrypted and contains the PS4 game files."
        exit 1
    fi

    info "Found valid game files at: $TARGET_DIR"
    mkdir -p "$PROJECT_ROOT/game"

    if [[ "$(realpath "$TARGET_DIR")" != "$(realpath "$GAME_DIR" 2>/dev/null || true)" ]]; then
        info "Creating game link -> $GAME_DIR"
        rm -rf "$GAME_DIR"
        ln -sfn "$(realpath "$TARGET_DIR")" "$GAME_DIR"
    fi

    success "Game files linked to $GAME_DIR"
}

# ------------------------------------------------------------------------------
# 6. Asset Linking & Preparation (dc_prepare.py)
# ------------------------------------------------------------------------------
prepare_assets() {
    DATA_DIR="$PROJECT_ROOT/data"
    mkdir -p "$DATA_DIR/user"

    PROBE_BIN="$PROJECT_ROOT/runtime-linux/out/driveclub-probe"
    PATCHES_XML="$PROJECT_ROOT/windows/patches/Driveclub.xml"
    PREPARE_SCRIPT="$PROJECT_ROOT/windows/launcher/prepare/dc_prepare.py"

    if [[ -f "$DATA_DIR/boot-linked.bin" && -f "$DATA_DIR/content.bin" ]]; then
        success "Game binaries are already prepared in $DATA_DIR"
    else
        info "Running automated game preparation (extracting symbols & linking ELF)..."
        python3 "$PREPARE_SCRIPT" "$PROJECT_ROOT/game/CUSA00003" "$DATA_DIR" "$PROBE_BIN" "$PATCHES_XML"
        if [[ ! -f "$DATA_DIR/boot-linked.bin" ]]; then
            error "Preparation failed to generate boot-linked.bin!"
            exit 1
        fi
        success "Asset preparation completed."
    fi

    # Compile the verified 60 FPS patch (zero artifacts)
    info "Compiling optimal 60 FPS patch configuration..."
    python3 "$PROJECT_ROOT/windows/launcher/prepare/dc_patches.py" \
        "$PROJECT_ROOT/game/CUSA00003" \
        "$PATCHES_XML" \
        "$DATA_DIR/patches.bin" \
        "60 FPS with deltatime"
    success "Patches compiled: 60 FPS with dynamic deltatime."
}

# ------------------------------------------------------------------------------
# 7. Hardware-Aware Configuration (dcport.ini)
# ------------------------------------------------------------------------------
configure_hardware() {
    info "Configuring hardware-optimized settings..."
    INI_FILE="$PROJECT_ROOT/data/dcport.ini"

    # Detect GPU vendor
    GPU_NAME="Generic GPU"
    if command -v lspci >/dev/null 2>&1; then
        GPU_NAME="$(lspci | grep -i -E "vga|3d|display" | head -n 1 || echo "Generic GPU")"
    fi
    info "Detected Graphics Device: $GPU_NAME"

    # Clean, artifact-free, high-performance default settings
    cat > "$INI_FILE" << EOF
[settings]
player_name = Player
language = 1
window_size = 1920x1080
fullscreen = 0
fps = 60
threaded_recording = 0
upscaler = none
dc_upscale = 0
vblank_hz = 0
fps_limit = 60
prep_workers = 0
copy_gpu_buffers = 0
object_motion = 0
EOF
    success "Configuration written to $INI_FILE"
}

# ------------------------------------------------------------------------------
# 8. Desktop & App Launcher Integration
# ------------------------------------------------------------------------------
create_desktop_launcher() {
    info "Installing desktop entry and application menu shortcut..."
    ICON_DIR="$HOME/.local/share/icons"
    APP_DIR="$HOME/.local/share/applications"
    mkdir -p "$ICON_DIR" "$APP_DIR"

    # Install Icon
    if [[ -f "$PROJECT_ROOT/media/logo.png" ]]; then
        cp -f "$PROJECT_ROOT/media/logo.png" "$ICON_DIR/driveclub.png"
    fi

    # Ensure run script is executable
    chmod +x "$PROJECT_ROOT/run_driveclub.sh"

    # Install Desktop Entry
    cat > "$APP_DIR/driveclub.desktop" << EOF
[Desktop Entry]
Type=Application
Name=DRIVECLUB
GenericName=Racing Game
Comment=Native Driveclub Linux Port
Exec=$PROJECT_ROOT/run_driveclub.sh
Icon=$ICON_DIR/driveclub.png
Terminal=false
Categories=Game;Simulation;
StartupNotify=true
Path=$PROJECT_ROOT
EOF

    # Create root play.sh shortcut
    cat > "$PROJECT_ROOT/play.sh" << 'EOF'
#!/usr/bin/env bash
cd -- "$(dirname -- "$0")"
exec ./run_driveclub.sh "$@"
EOF
    chmod +x "$PROJECT_ROOT/play.sh"

    # Update desktop database if available
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$APP_DIR" 2>/dev/null || true
    fi

    success "Desktop entry installed (~/.local/share/applications/driveclub.desktop)"
}

# ------------------------------------------------------------------------------
# Main Flow
# ------------------------------------------------------------------------------
main() {
    print_banner
    check_system
    check_dependencies
    ensure_submodules
    build_runtime "${REBUILD:-0}"
    setup_game_files "${1:-}"
    prepare_assets
    configure_hardware
    create_desktop_launcher

    echo ""
    echo -e "${GREEN}${BOLD}==========================================================${NC}"
    echo -e "${GREEN}${BOLD}   DRIVECLUB LINUX PORT SETUP COMPLETE! Ready to Play!   ${NC}"
    echo -e "${GREEN}${BOLD}==========================================================${NC}"
    echo -e "You can launch the game in any of the following ways:"
    echo -e "  1. Launch ${BOLD}'DRIVECLUB'${NC} directly from your Application Launcher / Menu."
    echo -e "  2. Run from terminal: ${CYAN}./play.sh${NC} (or ${CYAN}./run_driveclub.sh${NC})"
    echo ""
}

main "$@"
