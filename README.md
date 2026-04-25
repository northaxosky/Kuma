# Kuma

[![CI](https://github.com/northaxosky/Kuma/actions/workflows/ci.yml/badge.svg)](https://github.com/northaxosky/Kuma/actions/workflows/ci.yml)

Lightweight game engine built from scratch in C++ (with Rust where it makes sense).

Kuma is designed for small, indie games — prioritizing simplicity, modularity, and ease of use over being everything to everyone.

## Status

🚧 **Early development** — core systems being built bottom-up.

### What's Working

- **Vulkan renderer** — graphics pipeline, textured quads, shader compilation, configurable present mode (vsync / mailbox / immediate)
- **SDL3 platform layer** — windowing, event loop, resize handling
- **Resource system** — load textures (PNG/JPG) and meshes (OBJ) from disk with caching
- **Math** — Vec3, Mat4, and MVP matrix helpers
- **MVP pipeline** — perspective camera, push-constant model-view-projection in the quad shader
- **Camera** — reusable perspective camera plus keyboard/RMB-look free-fly controller in the sandbox
- **Input** — keyboard & mouse polling with edge detection (pressed/released this frame)
- **Time** — monotonic delta / total / frame count with anti-spiral clamp
- **Frame orchestration** — engine-owned `begin_frame()` / `end_frame()` wrapping a 5-phase contract (input → time → update → render → present)
- **Logging** — severity levels, colored console output

### What's Next

- Transform abstraction
- Scene graph / ECS
- Audio, physics

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for commit conventions, comment style, and code
style. TL;DR: Conventional Commits, clang-format on save, public headers don't leak
SDL/Vulkan types.

## Building

### Requirements

- CMake 3.24+
- C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+)
- Vulkan SDK

### Build & Run

```bash
cmake -B build -S .
cmake --build build --config Debug
./build/bin/Debug/sandbox
```

Or in VS Code: press **F5** (launch.json and tasks.json are included).

## Project Structure

```text
Kuma/
├── engine/                     Engine static library
│   ├── include/kuma/           Public API headers
│   │   ├── kuma.h              Main entry point
│   │   ├── camera.h            Camera + free-fly controller
│   │   ├── input.h             Keyboard and mouse polling
│   │   ├── renderer.h          Renderer interface
│   │   ├── resource_manager.h  Resource loading + caching
│   │   ├── time.h              Frame delta / total time
│   │   ├── window.h            Window management
│   │   └── log.h               Logging system
│   ├── src/
│   │   ├── core/               Engine init, logging
│   │   ├── platform/           SDL3 windowing
│   │   ├── renderer/           Vulkan graphics
│   │   └── resources/          Asset loading (textures, meshes)
│   └── shaders/                GLSL shaders (compiled to SPIR-V at build time)
├── sandbox/                    Test application
├── assets/                     Game assets (textures, models)
├── tests/                      Unit tests (GoogleTest)
└── .vscode/                    VS Code launch + build tasks
```

## Architecture

```text
┌─────────────────────────────────────────────┐
│              Game / Application              │
├─────────────────────────────────────────────┤
│          Scene / World Management            │
├──────────────┬──────────────┬───────────────┤
│   Renderer   │    Audio     │    Physics    │
├──────────────┴──────────────┴───────────────┤
│           Resource / Asset System            │
├─────────────────────────────────────────────┤
│              Platform Layer                  │
├─────────────────────────────────────────────┤
│              Core / Foundation               │
└─────────────────────────────────────────────┘
```

## Dependencies

| Library       | Purpose          | Integration                 |
| ------------- | ---------------- | --------------------------- |
| SDL3          | Windowing, input | FetchContent (automatic)    |
| Vulkan        | GPU rendering    | System install (Vulkan SDK) |
| stb_image     | Image loading    | Single header, vendored     |
| tinyobjloader | OBJ mesh loading | Single header, vendored     |

## License

TBD
