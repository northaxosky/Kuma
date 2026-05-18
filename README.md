# Kuma

[![CI](https://github.com/northaxosky/Kuma/actions/workflows/ci.yml/badge.svg)](https://github.com/northaxosky/Kuma/actions/workflows/ci.yml)

Lightweight game engine built from scratch in C++ (with Rust where it makes sense).

Kuma is designed for small, indie games — prioritizing simplicity, modularity, and ease of use over being everything to everyone.

## Status

🚧 **Early development** — core systems being built bottom-up.

### What's Working

- **Vulkan renderer** — graphics pipeline, three pipelines (textured opaque / debug-normal opaque / transparent particle billboards), depth buffer (D32_SFLOAT, standard direct-Z), shader compilation, configurable present mode (vsync / mailbox / immediate). Per-frame instance ring buffer for particle uploads. 96-byte shared push constant range covering view-projection + camera right/up.
- **SDL3 platform layer** — windowing, event loop, resize handling, relative mouse mode for FPS-style camera look. 4K window (3840×2160) by default in the sandbox.
- **Math** — `Vec2`, `Vec3`, `Vec4`, `Quat`, `Mat4`, perspective + look-at + MVP helpers, slerp, axis-angle and Euler quaternion factories.
- **Transform** — position + quaternion rotation + scale, producing a model matrix.
- **ECS** — sparse-set Registry with generational EntityID handles, sparse-set component storage, and `view<T...>()` queries with structured bindings.
- **Debug overlay** — Dear ImGui integration with a custom Kuma Dark style and Cascadia Mono font; F3 toggles a default panel (FPS, frame time, 1% low, 60-frame sparkline). Game code calls `ImGui::*` directly for custom panels.
- **Asset pipeline** — `kuma-bake` (Rust) converts source assets (.obj, .png, .jpg, .tga, .gltf, .glb, .wav, .ogg) into the engine's binary formats (.kmesh, .ktex, .ksound, .kmaterial, .kscene). Engine loads only baked binaries at runtime; no source-format parsing in the hot path. CMake invokes `kuma-bake` during build via `kuma_bake_*` helpers.
- **Camera** — reusable perspective camera plus keyboard/RMB-look free-fly controller for debug inspection.
- **Resource system** — loads .ktex / .kmesh / .kmaterial binaries with path-and-usage-keyed caching. Texture cache distinguishes sRGB (color) from UNORM (data) uploads so normal maps don't get gamma-corrected.
- **Physics** — [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 5.5.0 wired through an opaque `kuma::physics` API. Dynamic / Static / Kinematic bodies, sphere / box / capsule shapes, fixed-step accumulator with spiral-of-death clamp. Bodies plug into the ECS via a `PhysicsBody` component.
- **Character controller** — Kinematic FPS capsule on top of Jolt's `CharacterVirtual`. Step-and-slide collision, slope detection, auto-step over short obstacles, ground state, pushes dynamic bodies. `FpsCameraController` reads input and writes both character and camera (mouse-look on yaw + pitch, WASD relative to character yaw, Space jump). T toggles between FPS mode and free-fly camera.
- **Audio** — [miniaudio](https://miniaud.io/) 0.11 wired through an opaque `kuma::audio` API. WAV / OGG sounds with 3D positional spatialization (distance attenuation + stereo panning). Dual API: fire-and-forget `play_sound` / `play_sound_at` returning generation-checked `SoundHandle`, and an `AudioSource` ECS component for long-running music / ambience. `kuma-bake sound` uses Symphonia to convert .wav to PCM `.ksound` or pass .ogg bytes through.
- **Scenes** — Multi-mesh asset loading via `kuma::scene::load_and_spawn`. `kuma-bake scene` walks a glTF scene tree, dedups primitive geometry into per-primitive `.kmesh` files, composes parent-chain world transforms, and writes a `.kscene` v2 index referencing meshes AND materials. Runtime spawns one ECS entity per node with `Transform` + `MeshRef` + `MaterialRef`.
- **Materials** — Per-mesh diffuse textures with PBR-ready data layout. `kuma-bake` extracts glTF materials into `.kmaterial` files (108-byte header carrying base color, metallic, roughness, alpha mode, plus paths for five PBR texture slots) and dedups referenced textures into `.ktex`. Runtime allocates one descriptor set per material from a 256-set pool. 5-binding descriptor set layout (diffuse / normal / metallic-roughness / occlusion / emissive) with renderer-owned 1×1 defaults per glTF spec. Current shader samples only diffuse; the other slots wait for lighting.
- **Particles** — CPU-driven billboard particles. `ParticleEmitter` ECS component owns a fixed 256-particle SoA pool. One-shot (burst) and continuous emitters share the same component shape. `particles::simulate` runs in UPDATE phase, `particles::render` after opaque draws (depth test on, depth write off, alpha blend on). Per-emitter back-to-front sort plus per-emitter `draw_order` enum (Index / Lifetime / ViewDepth) for within-emitter ordering. Five built-in presets: `make_muzzle_flash`, `make_impact_spark`, `make_blood_spatter`, `make_death_poof`, `make_pickup_sparkle`. Sandbox demo: F spawns an icosahedron + impact_spark burst; mouse-1 in FPS mode fires muzzle_flash.
- **Input** — keyboard & mouse polling with edge detection (pressed/released this frame).
- **Time** — monotonic delta / total / frame count with anti-spiral clamp.
- **Frame orchestration** — engine-owned `begin_frame()` / `end_frame()` wrapping a 5-phase contract (input → time → update → render → present).
- **Logging** — severity levels, colored console output.

### What's Next

- **Lighting** — Forward / Forward+ / deferred decision pending. Will pay back the materials work — the 5-binding descriptor set finally starts sampling normal / metallic-roughness / occlusion / emissive once a lit shader exists. Render pass refactor (HDR offscreen + depth-prepass capability) likely lands as commit 1.
- **Shadows** — directional + point light shadow maps, layered on top of lighting.
- Renderer batching / instancing for static meshes when entity counts make per-entity draw calls a real cost.
- BC7 / BC5 texture compression, mipmap generation in `kuma-bake`.
- MP3 / FLAC support in the audio asset pipeline.

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

The sandbox loads [Khronos's standard Sponza](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza) with all 25 diffuse textures (~60 MB total). Source files are gitignored; download them once with:

```pwsh
pwsh tools\download_sponza.ps1
```

After that, `cmake --build` will bake `Sponza.kscene` + 103 sibling `.kmesh` files + 25 `.kmaterial` files + 25 `.ktex` files into `build/assets/scenes/`.

`cmake --build` invokes `cargo` automatically to build `kuma-bake` and bakes
the sandbox's source assets into the engine binary format (`.kmesh`, `.ktex`)
before linking the sandbox executable.

## Project Structure

```text
Kuma/
├── engine/                     Engine static library
│   ├── include/kuma/           Public API headers
│   │   ├── kuma.h              Main entry point
│   │   ├── audio.h             Sound playback + listener pose
│   │   ├── camera.h            Camera + free-fly controller
│   │   ├── character.h         Character controller component
│   │   ├── debug.h             ImGui debug overlay
│   │   ├── ecs.h               Registry + view + EntityID
│   │   ├── fps_camera.h        FPS character + camera controller
│   │   ├── input.h             Keyboard and mouse polling
│   │   ├── material.h          Material runtime struct + TextureUsage
│   │   ├── math.h              Vec2/3/4, Quat, Mat4
│   │   ├── particles.h         ParticleEmitter component + presets
│   │   ├── physics.h           Jolt-backed physics module
│   │   ├── platform.h          exe_relative path helper
│   │   ├── renderer.h          Renderer interface
│   │   ├── resource_manager.h  Asset loading + caching
│   │   ├── scene.h             Multi-mesh scene loading
│   │   ├── time.h              Frame delta / total time
│   │   ├── transform.h         Position + rotation + scale
│   │   ├── window.h            Window management
│   │   ├── log.h               Logging system
│   │   └── asset_format.h      On-disk binary format definitions
│   ├── src/
│   │   ├── core/               Engine init, frame loop, logging, ECS, debug
│   │   ├── platform/           SDL3 windowing, input, paths
│   │   ├── renderer/           Vulkan device, swapchain, pipelines, resources
│   │   ├── resources/          ResourceManager (.kmesh/.ktex/.kmaterial loading)
│   │   ├── physics/            Jolt integration
│   │   ├── character/          CharacterVirtual + FpsCameraController
│   │   ├── audio/              miniaudio integration
│   │   ├── scene/              .kscene parser + spawn
│   │   └── particles/          Sim, render, presets
│   └── shaders/                GLSL shaders (compiled to SPIR-V at build time)
├── sandbox/                    Test application (Sponza walkaround + FX demo)
├── tools/
│   ├── kuma-bake/              Rust asset converter (mesh/tex/sound/scene/gltf)
│   └── download_sponza.ps1     Fetch Sponza glTF + textures
├── assets/                     Game-side baked assets (engine defaults)
├── tests/                      GoogleTest unit + integration tests
├── cmake/                      Helper modules (kuma-bake.cmake, etc)
└── .vscode/                    VS Code launch + build tasks
```

## Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│                    Game / Application                       │
├─────────────────────────────────────────────────────────────┤
│        Scene / ECS (Registry, components, views)            │
├──────────┬──────────┬──────────┬──────────┬─────────────────┤
│ Renderer │  Audio   │ Physics  │Particles │   Character     │
├──────────┴──────────┴──────────┴──────────┴─────────────────┤
│         Resources (ResourceManager + asset cache)           │
├─────────────────────────────────────────────────────────────┤
│              Platform (SDL3 window, input, paths)           │
├─────────────────────────────────────────────────────────────┤
│         Core (frame loop, time, log, math, debug)           │
└─────────────────────────────────────────────────────────────┘
```

## Dependencies

| Library             | Purpose                | Integration                 |
| ------------------- | ---------------------- | --------------------------- |
| SDL3                | Windowing, input       | FetchContent (automatic)    |
| Vulkan              | GPU rendering          | System install (Vulkan SDK) |
| [Jolt Physics][jolt]| Rigid body + character | FetchContent (automatic)    |
| [miniaudio][miniaudio] | Audio playback      | Single header, vendored     |
| [Dear ImGui][imgui] | Debug overlay          | FetchContent (automatic)    |
| stb_image           | Image loading (Rust side via `image` crate; engine takes baked .ktex only) | Indirect |
| GoogleTest          | Unit + integration tests | FetchContent (automatic)  |

Rust-side (`kuma-bake`):

| Crate       | Purpose                                |
| ----------- | -------------------------------------- |
| `gltf`      | glTF / GLB parsing                     |
| `image`     | PNG / JPG / TGA decoding               |
| `tobj`      | OBJ parsing                            |
| `symphonia` | WAV / OGG audio decoding               |
| `bytemuck`  | Safe POD struct ↔ bytes conversion     |

[jolt]: https://github.com/jrouwe/JoltPhysics
[miniaudio]: https://miniaud.io/
[imgui]: https://github.com/ocornut/imgui

## License

TBD
