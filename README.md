# VibeRendering

A modern Vulkan 1.3 rendering framework built entirely through vibe coding — **100% of the code in this repository was written by AI** (Claude Sonnet 4.6). No human has typed a single line of source code.

## What is Vibe Coding?

Vibe coding is a development style where you describe what you want, and AI writes all the code. You steer the direction, the AI does the typing. This project exists to explore how far that can go with low-level graphics programming.

## Features

- **Vulkan 1.3** — modern API only, no legacy cruft
- **Dynamic Rendering** — zero `VkRenderPass` or `VkFramebuffer` objects, uses `vkCmdBeginRendering`
- **Synchronization2** — `vkCmdPipelineBarrier2` with 64-bit stage flags
- **2 frames in flight** — per-image semaphore pools with correct WSI reuse semantics
- **Window resize handling** — swapchain recreation on resize and minimization
- **Validation layers** — enabled automatically in Debug builds
- **VMA** — Vulkan Memory Allocator wired up and ready for GPU allocations
- **C++23** — designated initializers, `[[nodiscard]]`, etc.

## Tech Stack

| Library | Purpose |
|---------|---------|
| [GLFW](https://www.glfw.org/) | Window creation and input |
| [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) | Vulkan instance, device, swapchain boilerplate |
| [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | GPU memory allocation |
| [glm](https://github.com/g-truc/glm) | Math |
| [spdlog](https://github.com/gabime/spdlog) | Logging |

Dependencies are managed via **vcpkg** in manifest mode — they download and build automatically on first configure.

---

## Prerequisites

### 1. Vulkan SDK
Download and install from [LunarG](https://vulkan.lunarg.com/). The installer sets the `VULKAN_SDK` environment variable which CMake uses to find the loader (`vulkan-1.lib`).

### 2. vcpkg
If you don't have vcpkg installed:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

Then set the environment variable (restart your terminal after):

```powershell
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
```

### 3. Visual Studio 2022
With the **Desktop development with C++** workload installed. CMake and Ninja are included.

---

## Building

```bash
# Configure (downloads and compiles all vcpkg dependencies on first run — takes a few minutes)
cmake --preset windows-msvc-debug

# Build
cmake --build build/debug
```

For a release build:

```bash
cmake --preset windows-msvc-release
cmake --build build/release
```

---

## Running

```bash
./build/debug/src/Debug/VibeRendering.exe
```

You should see a **1280×720 window filled with deep purple** and log output like:

```
[12:00:00.000] [info ] Window created: 1280x720
[12:00:00.001] [info ] Validation layers: enabled
[12:00:00.100] [info ] Vulkan instance created (API 1.3)
[12:00:00.105] [info ] GPU selected: NVIDIA GeForce RTX XXXX
[12:00:00.200] [info ] Swapchain created: 1280x720, 3 images
[12:00:00.201] [info ] Renderer initialized (2 frames in flight, 3 acquire semaphores)
[12:00:00.201] [info ] App initialized — let's vibe
```

---

## Project Structure

```
src/
  main.cpp            — entry point, logging setup
  App.h / App.cpp     — GLFW window, main loop, owns all subsystems
  VulkanContext.h/.cpp — Vulkan instance, physical/logical device, VMA
  Swapchain.h/.cpp    — swapchain creation and resize handling
  Renderer.h/.cpp     — command buffers, sync primitives, render loop
```

---

## IDE Support

**VS Code** — Install the [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) extension. It detects `CMakePresets.json` automatically. Select the `Windows MSVC Debug` preset from the status bar and press F5.

**Visual Studio 2022** — `File → Open → Folder`, select the repo root. VS detects the presets automatically. Select the Debug preset and press F5.
