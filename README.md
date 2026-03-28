# Kuma

Lightweight game engine built from scratch in C++ and Rust.

Kuma is designed for small, indie games — prioritizing simplicity, modularity, and ease of use over being everything to everyone.

## Status

🚧 **Early development** — scaffolding and architecture phase.

## Features (Planned)

- **Vulkan renderer** — modern GPU-driven rendering
- **SDL platform layer** — cross-platform windowing and input
- **ECS architecture** — data-oriented entity management
- **Asset pipeline** — resource loading with hot-reload support
- **Audio system** — spatial and ambient sound

## Building

### Requirements

- CMake 3.24+
- C++20 compatible compiler (MSVC 2022, GCC 12+, or Clang 15+)

### Build

```bash
cmake -B build -S .
cmake --build build
```

### Run the sandbox

```bash
./build/bin/sandbox
```

## Project Structure

```
engine/          Engine library (core, platform, renderer)
  include/kuma/  Public API headers
  src/           Implementation
sandbox/         Test application
tests/           Unit tests
docs/            Documentation
```

## License

TBD
