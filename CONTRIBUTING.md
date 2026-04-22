# Contributing to Kuma

Thanks for poking around. Kuma is primarily a learning project, but it's written like a
real codebase — same conventions, same discipline. If you're making a change (or just want
to know why things look the way they do), this is the doc.

> **Principle:** machine-enforced rules live in tools (`.clang-format`, `CMakeLists`,
> `.vscode/`). Human-enforced rules live here. If a rule appears in both, the tool wins.

---

## Quickstart

Build and run:

```bash
cmake -B build -S .
cmake --build build --config Debug
./build/bin/Debug/sandbox
```

Run tests:

```bash
cd build && ctest -C Debug --output-on-failure
```

See [README.md](README.md) for requirements and deeper build notes.

---

## Commit conventions

Kuma uses [Conventional Commits](https://www.conventionalcommits.org/) with a scope:

```
<type>(<scope>): <short imperative summary>

<optional longer body explaining the why>

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
```

**Types used here:** `feat`, `fix`, `refactor`, `docs`, `chore`, `style`, `test`, `perf`.

**Scopes are module names:** `renderer`, `input`, `math`, `sandbox`, `vscode`, etc.

### Rules

1. **Atomic and single-purpose.** A formatting pass is its own commit, separate from a
   feature. A feature that touches five files is still one commit. Mixing them ruins
   `git blame` and makes reverts painful.
2. **Imperative summary.** `add input enums`, not `added input enums` or `adds input enums`.
3. **Explain the why** in the body when it isn't obvious from the diff. A good rule:
   if someone looking at this commit in a year needs context, it belongs in the message.
4. **Co-authored-by trailer** on commits touching agent-authored code (most of them, in
   this repo).

### Examples

```
✅ feat(input): add Key and MouseButton enums
✅ fix(renderer): handle zero-size swapchain on minimize
✅ style: apply clang-format across engine and sandbox
❌ Updated some stuff
❌ feat: added input enums and also refactored the renderer
```

---

## Comment style

### Section headers

Files with multiple logical sections use box-drawing dividers:

```cpp
// ── Section Name ────────────────────────────────────────────────
```

**Exact format:**

- Prefix: `// ── ` (slashes, space, two `─` box-drawing chars `U+2500`, space)
- Title: Title Case, as short as clear
- Suffix: trailing `─` chars padded to **exactly 67 columns total**
- One blank line after the header, before the code it describes

The character is `─` (`U+2500 BOX DRAWINGS LIGHT HORIZONTAL`), **not** an em-dash `—`
and **not** a regular hyphen `-`. Easy to copy from an existing file.

Use section headers in any file with more than one logical concept (multiple structs,
distinct responsibility groups, etc.). Short files don't need them.

### Doc comments

- **Comments explain the "why"; code explains the "what".** If a comment restates the
  code, delete the comment or rewrite the code.
- Non-obvious choices, tradeoffs, or pitfalls are exactly what comments are for.
- Reference links to SDL/Vulkan docs are welcome when a function's behavior is
  surprising.

```cpp
// ✅ Good — explains why
// SDL distinguishes left/right modifier keys; we preserve that because some games
// bind different actions to each. The action layer can collapse them if needed.
enum class Key : uint16_t { ... LShift, RShift, ... };

// ❌ Bad — restates the code
// Enum of keys
enum class Key : uint16_t { ... };
```

---

## Code style

### Formatting — enforced by `.clang-format`

Indentation, braces, pointer alignment, include ordering: all handled by clang-format.
Format-on-save is pinned in `.vscode/settings.json`; CI will eventually fail on unformatted code.

Don't hand-format around the rules. If the formatter's output reads poorly, the fix is
to restructure the code, not to add `// clang-format off`.

### Naming

| Thing              | Convention                                      | Example                     |
| ------------------ | ----------------------------------------------- | --------------------------- |
| Namespaces         | lower_snake_case                                | `kuma::input`               |
| Types (class/struct/enum) | PascalCase                               | `InputState`, `Key::W`      |
| Functions          | snake_case                                      | `is_key_down(Key k)`        |
| Local variables    | snake_case                                      | `current_state`             |
| Private class members | snake_case with trailing `_`                 | `window_`, `width_`         |
| Public POD struct fields | snake_case, no trailing underscore        | `WindowConfig::width`       |
| Constants / enum values | PascalCase (inside `enum class`)           | `Key::LShift`               |
| Macros             | `UPPER_SNAKE_CASE`, avoid unless unavoidable    | `KUMA_LOG_LEVEL`            |

The trailing underscore is a visual cue for "this is private state inside a class."
Plain data configs (`WindowConfig`, `AppConfig`) are caller-facing knobs, so their
fields read cleaner without the underscore.

### General C++ rules

- **C++20.** Use modern features freely: `std::expected`, concepts, ranges, `if constexpr`.
- **RAII everywhere.** No raw `new`/`delete`. `std::unique_ptr` for ownership.
- **Raw pointers are non-owning references only.** Never for ownership.
- **No exceptions in hot paths.** Return `bool`, error enums, or `std::expected`.
- **Forward-declare** in headers when possible; push includes into `.cpp`s.

---

## File organization

```
engine/
├── include/kuma/         Public headers — game code includes these.
│   └── <module>.h        Must not pull in SDL, Vulkan, or any platform API.
└── src/
    ├── core/             Engine lifecycle, logging, math, foundational bits
    ├── platform/         SDL-dependent (windowing, input, filesystem)
    ├── renderer/         Vulkan graphics
    └── resources/        Asset loading
```

### The one inviolable rule

> **Public headers (`engine/include/kuma/*.h`) must not leak third-party types.**

Game code includes `<kuma/input.h>` and gets `kuma::Key::W`. It must never see
`SDL_Scancode`, `VkFormat`, or any other SDL/Vulkan symbol transitively. Forward-declare
where needed (see `window.h` for the pattern).

This is what makes the platform layer actually a layer.

---

## When you're stuck

- Build: `cmake --build build --config Debug`
- Tests: `cd build && ctest -C Debug --output-on-failure`
- Clean slate: delete the `build/` directory and reconfigure
- Format a file: save it in VS Code (auto), or run clang-format manually

For deeper architectural questions, check [CLAUDE.md](CLAUDE.md) — it's the
engine's design + agent-collaboration guide.
