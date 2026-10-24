# Driveclub Native

[![YouTube](https://img.shields.io/badge/YouTube-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://youtube.com/@blacklineinteractive)
[![Linux Support](https://img.shields.io/badge/Linux-Native%20Port-success?style=for-the-badge&logo=linux&logoColor=white)](https://github.com/afnan-mojumder/DriveClub-Linux)

![LOGO](media/logo.png)
![Driveclub In-Game](media/2026-10-14_22-34-39.png)

A native port of **Driveclub** (PS4, CUSA00003 / CUSA00093) to **Linux**, **Windows**, and **macOS**.

| Platform | Game version | Renderer | Where | Maintainer |
| --- | --- | --- | --- | --- |
| **Linux (x86_64)** | **v1.28** | **Native Vulkan (Zero Artifacts, 60 FPS)** | **[`runtime-linux/`](runtime-linux/) & [`setup.sh`](setup.sh)** | **[afnan-mojumder](https://github.com/afnan-mojumder)** |
| Windows 10/11 | v1.28 | Vulkan (shadPS4's renderer), FSR 3.1 / FSR 4 / FSR 4.1.1, frame generation | [`windows/`](windows/README.md) | [qnfn](https://github.com/qnfn) |
| macOS (Intel) | v1.05 | Metal 3 | `src/` | [BlackLineInteractive](https://github.com/BlackLineInteractive) |

---

# 🐧 Driveclub on Linux (Native Port)

Driveclub runs **100% natively on Linux** without Wine, Proton, or general-purpose emulation layers.
PS4 x86-64 code executes directly on the host CPU via POSIX threads and `mmap`; console-specific Sony system libraries (HLE) and AMD Liverpool GCN GPU commands are translated directly into high-performance host **Vulkan**.

### 🌟 Linux Highlights
- **Rock-Solid 60 FPS:** Optimized frame pacing with decoupled 480 Hz virtual vblank eliminates harsh drops to 30/25 FPS.
- **Zero Artifacts:** Fully stabilized synchronous draw preparation eliminates vertex explosions, mesh tearing, flashing, and flickering.
- **Seamless Desktop Integration:** Includes an automated installer that adds **DRIVECLUB** directly to your application menu / desktop launcher with full art and icons.
- **Hardware Agnostic:** Tested and optimized across modern NVIDIA GPUs, AMD Radeon GPUs, Intel Arc, and Steam Deck.

---

## ⚡ Quick Start for Gamers

### 1. Clone the Repository
```bash
git clone --recursive https://github.com/afnan-mojumder/DriveClub-Linux.git
cd DriveClub-Linux
```

### 2. Run the Automated Setup
```bash
./setup.sh
```
The setup script will automatically:
1. Verify system dependencies and provide one-click package commands for Arch, Ubuntu/Debian, Fedora, or SteamOS.
2. Build the native Linux runtime (`driveclub-probe` and `libbbgpu.so`).
3. Locate or prompt for your decrypted Driveclub PS4 game folder (`CUSA00003` containing `eboot.bin`).
4. Generate the linked executable and content profile.
5. Compile the verified 60 FPS dynamic deltatime patch.
6. Install a desktop entry into your application launcher (`~/.local/share/applications/driveclub.desktop`).

> **Tip:** You can also pass your game path directly to setup:
> ```bash
> ./setup.sh /path/to/Driveclub_CUSA00003
> ```

### 3. Launch & Play
You can start the game directly from your desktop app menu (**DRIVECLUB**), or from terminal:
```bash
./play.sh
```

#### Optional Launch Flags
```bash
./play.sh --fullscreen              # Launch in fullscreen mode
./play.sh --windowed                # Launch in windowed mode (default)
./play.sh --fps 60                  # Cap target framerate (e.g., 30, 60, 120)
./play.sh --res 1920x1080           # Set window resolution (e.g., 2560x1440, 1280x720)
./play.sh --help                    # Display all launcher options
```

---

## 💻 System Requirements (Linux)

| Component | Minimum | Recommended |
| --- | --- | --- |
| **OS** | 64-bit Linux (Arch, Ubuntu 22.04+, Fedora 38+, SteamOS 3+) | Arch Linux / Fedora / Ubuntu with Wayland or X11 |
| **CPU** | 4 cores / 8 threads x86_64 with AVX / SSE4.2 | 6+ cores / 12+ threads modern CPU (Ryzen 5000+ / Intel 12th gen+) |
| **GPU** | Vulkan 1.3 capable GPU (GTX 1060 6GB / RX 580) | NVIDIA RTX 2060+ / AMD Radeon RX 6600+ (8 GB+ VRAM) |
| **RAM** | 16 GB System Memory | 16 GB+ High-speed Dual Channel |
| **Storage** | 40 GB free space on SSD (NVMe / SATA SSD required for streaming) | NVMe PCIe SSD |
| **Game Copy** | Decrypted dump of Driveclub (v01.28 EU `CUSA00003` or US `CUSA00093`) | Same |

### Distribution Dependencies

- **Arch Linux / Manjaro / EndeavourOS:**
  ```bash
  sudo pacman -S --needed base-devel git cmake ninja clang pkgconf vulkan-headers vulkan-icd-loader sdl3 freetype2 boost
  ```
- **Ubuntu / Debian / Pop!_OS / Linux Mint:**
  ```bash
  sudo apt update && sudo apt install -y build-essential git cmake ninja-build clang pkg-config libvulkan-dev vulkan-tools libsdl3-dev libfreetype6-dev libboost-dev
  ```
- **Fedora / Nobara / RHEL:**
  ```bash
  sudo dnf install -y @development-tools git cmake ninja-build clang pkgconf-pkg-config vulkan-loader-devel vulkan-headers SDL3-devel freetype-devel boost-devel
  ```
- **Steam Deck (SteamOS):**
  Works in Desktop Mode. Add `play.sh` as a Non-Steam Game to launch from Steam Deck Gaming Mode.

---

## 🎮 Controls

The game natively supports DualShock 4, DualSense, Xbox controllers, and generic SDL3 gamepads out of the box via USB or Bluetooth.

| Action | PlayStation Controller | Xbox Controller | Keyboard |
| --- | --- | --- | --- |
| **Accelerate** | R2 | RT | W / Up Arrow |
| **Brake / Reverse** | L2 | LT | S / Down Arrow |
| **Steer** | Left Stick | Left Stick | A / D / Left / Right Arrow |
| **Handbrake** | Cross (✕) | A | Spacebar |
| **Change View** | Triangle (△) | Y | C |
| **Look Back** | R3 (Right Stick Click) | Right Stick Click | B |
| **Pause Menu** | Options | Menu | Escape / Enter |

---

## 🔧 Architecture & Linux Engine Pipeline

```
eboot.bin (x86-64, natively executed on Linux host)
   │  imports of 922 functions from 57 libraries
   ▼
Linux HLE runtime ── libkernel, libc, Pad (SDL3), AudioOut (Atrac9), VideoOut, Network -> host POSIX
   │  sceGnmSubmitCommandBuffers / sceGnmDingDong
   ▼
GPU frontend ── PM4 packet processor, register state, GCN shaders -> SPIR-V
   │  runtime-linux/gpu
   └──► Native Vulkan 1.3 (Host GPU Driver)
```

### Performance & Visual Consistency Principles
1. **Synchronous Preparation (`BB_PREP_WORKERS=0`):** Driveclub dynamically modifies vertex buffers during draw generation. Running worker threads causes out-of-order execution leading to exploded car meshes and artifacting. Zero prep workers ensures 100% visual stability.
2. **Subgroup Memory Barriers (Zero White Squares):** GCN wavefronts share Local Data Share (LDS) memory in lockstep without explicit barriers. The shader recompiler incorporates subgroup scoped memory barriers between shared memory dependencies (backported from PR #5285), eliminating the classic "white square" / checkerboard artifact on modern GPU compilers.
3. **Precise GPU Readbacks (`BB_READBACKS=2`):** Enables accurate GPU-to-CPU buffer synchronization, ensuring dynamic track exposure and lighting calculations resolve correctly without pitch-black or unshaded tiles.
4. **Decoupled Virtual Vblank (`BB_VBLANK_HZ=0`):** The engine's default 60 Hz vblank timer forces any frame that slightly exceeds 16.6 ms to wait for the next refresh tick (33.3 ms = 30 FPS). The Linux port engages a virtual 480 Hz timer (~2 ms granularity) paired with `BB_FPS_LIMIT=60`, preserving fluid 60 FPS even during heavy weather effects.
5. **Pure Native Shading:** Eliminates external upscaler buffer conflicts for clean, crisp native PS4 visual fidelity.

---

## 📁 Repository Layout

| Path | Backend | Platform | What it is |
| --- | --- | --- | --- |
| `runtime-linux/` | Vulkan | Linux | Native Linux port runtime: ELF loader, HLE libraries, GCN → SPIR-V, Vulkan presenter |
| `setup.sh` | Bash | Linux | Automated one-click setup script for Linux |
| `play.sh` / `run_driveclub.sh` | Bash | Linux | Game launcher scripts with CLI options and hardware overrides |
| `windows/` | Vulkan (DCport) | Windows | Windows native backend (DCport), launcher, patches |
| `src/` | Metal (`src/gpu/metal`) | macOS | macOS native runtime: Metal 3 renderer for Intel Macs |
| `tools/` | Python / Shell | Common | ELF analysis tools and helpers |

---

## ⚖️ Legal Disclaimer

- **No Proprietary Game Files**: This project does not distribute, host, or link to any copyrighted material, proprietary game assets, PKG dumps, ROMs, executables (`eboot.bin`), libraries (`.prx`), engine archives (`.dat`), audio, or textures. Users must provide their own legitimately acquired copy of Driveclub (CUSA00003 or CUSA00093).
- **Intellectual Property**: Driveclub is a registered trademark of Sony Interactive Entertainment LLC. All trademarks, service marks, game titles, and copyrighted assets belong to Sony Interactive Entertainment and Evolution Studios.
- **Educational and Preservation Purpose**: This repository is an independent reverse-engineering project conducted strictly for interoperability, software preservation, and educational purposes. It is not affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment, Evolution Studios, or any associated entities.

---

## 📜 License

This project is licensed under the **GNU General Public License v2.0 or later (GPL-2.0-or-later)** - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

- [shadPS4](https://github.com/shadps4-emu/shadPS4) - PS4 emulator, the primary source of knowledge on ELF loading, HLE libraries, PM4, and GCN shader recompilation.
- [Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) & deadinside28 - base runtime foundation for native execution.
- [BlackLineInteractive](https://github.com/BlackLineInteractive) - original Driveclub-Port macOS Metal project.
- [qnfn](https://github.com/qnfn) - DCport Windows port maintainer.
- [illusion](https://github.com/illusion0001), u/Charlie_Muggins, and kalaposfos - 60 FPS and dynamic deltatime patches.
