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
- **Transform** — position + quaternion rotation + scale, producing a model matrix; sandbox spins the quad to demo
- **ECS** — sparse-set Registry with generational EntityID handles, sparse-set component storage, and `view<T...>()` queries with structured bindings; sandbox demos a 100-entity grid driven by spin + render systems
- **Debug overlay** — Dear ImGui integration with a custom Kuma Dark style and Cascadia Mono font; F3 toggles a default panel (FPS, frame time, 1% low, 60-frame sparkline). Game code calls `ImGui::*` directly for custom panels.
- **Asset pipeline** — `kuma-bake` (Rust) converts source assets (.obj, .png, .jpg, .tga, .gltf, .glb) into the engine's binary format (.kmesh, .ktex). Engine loads only baked binaries at runtime; no source-format parsing in the hot path. Sandbox renders a glTF icosahedron via a debug-normal pipeline alongside the textured ECS quad grid.
- **Physics** — [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 5.5.0 wired through an opaque `kuma::physics` API. Dynamic / Static / Kinematic bodies, sphere / box / capsule shapes, fixed-step accumulator with spiral-of-death clamp. Bodies plug into the ECS via a `PhysicsBody` component; the simulation owns dynamic poses and syncs them back into the entity's `Transform` each frame. Sandbox demo: invisible floor plane, F to spawn an icosahedron in front of the camera, R to clear them all.
- **Character controller** — Kinematic FPS capsule on top of Jolt's `CharacterVirtual`. Step-and-slide collision, slope detection, auto-step over short obstacles, ground state, pushes dynamic bodies. `Character` ECS component pairs with a `Transform`; `kuma::character::simulate` runs in lockstep with the physics fixed step. `FpsCameraController` reads input and writes both character and camera (mouse-look on yaw + pitch, WASD relative to character yaw, Space jump). Sandbox spawns one player; T toggles between FPS mode and the original free-fly camera for debug inspection.
- **Audio** — [miniaudio](https://miniaud.io/) 0.11 wired through an opaque `kuma::audio` API. Plays WAV / OGG sounds with 3D positional spatialization (distance attenuation + stereo panning) tracking a per-frame listener pose. Dual API surface: `play_sound` / `play_sound_at` for fire-and-forget one-shots returning a generation-checked `SoundHandle`, and an `AudioSource` ECS component for long-running music / ambience that syncs volume + looping every frame. Asset pipeline: `kuma-bake sound` uses Symphonia to convert .wav into uncompressed PCM `.ksound` (zero-decode-at-load for tight SFX) or pass .ogg bytes through unchanged (~10x compression for music). Sandbox demo: ambient music loop on the player + impact thud at each spawned icosahedron's world position.
- **Scenes** — Multi-mesh asset loading via `kuma::scene::load_and_spawn`. `kuma-bake scene` walks a glTF scene tree, dedups primitive geometry into per-primitive `.kmesh` files, composes parent-chain world transforms, and writes a `.kscene` index referencing them. Runtime spawns one ECS entity per node with `Transform` + `MeshRef` + `SceneTag`, sharing meshes across instances through the existing ResourceManager path cache. Sandbox loads [Khronos's standard Sponza](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza) (103 unique meshes, 103 nodes, ~7.5 MB baked) - the player walks around inside a real architectural test scene with the icosahedron impact-spawn demo unchanged.
- **Input** — keyboard & mouse polling with edge detection (pressed/released this frame)
- **Time** — monotonic delta / total / frame count with anti-spiral clamp
- **Frame orchestration** — engine-owned `begin_frame()` / `end_frame()` wrapping a 5-phase contract (input → time → update → render → present)
- **Logging** — severity levels, colored console output

### What's Next

- Renderer batching / instancing (when entity counts make per-entity draw calls a real cost; observed at >1000 entities)
- glTF support, texture compression (BC7/BC5), mipmaps in `kuma-bake`
- MP3/FLAC support in the audio asset pipeline

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for commit conventions, comment style, and code
style. TL;DR: Conventional Commits, clang-format on save, public headers don't leak
SDL/Vulkan types.

## Building

### Requirements

- CMake 3.24+
- C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+)
- Vulkan SDK
- Rust 1.85+ (for the `kuma-bake` asset baker - install via [rustup](https://rustup.rs))

### Build & Run

```bash
cmake -B build -S .
cmake --build build --config Debug
./build/bin/Debug/sandbox
```

Or in VS Code: press **F5** (launch.json and tasks.json are included).

#### Sponza scene

The sandbox loads [Khronos's standard Sponza](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza) (~10 MB). The source files are gitignored; download them once with:

```pwsh
pwsh tools\download_sponza.ps1
```

After that, `cmake --build` will bake `Sponza.kscene` + 103 sibling `.kmesh` files into `build/assets/scenes/`.

`cmake --build` invokes `cargo` automatically to build `kuma-bake` and bakes
the sandbox's source assets into the engine binary format (`.kmesh`, `.ktex`)
before linking the sandbox executable.

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
