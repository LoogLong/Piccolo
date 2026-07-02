 - 每次对话完成时，如果git工作区的changes里面有文件修改，则需要commit
 - 对话默认开启sub agents，并且对话完成时需要及时清理sub agents
 - 没有明确要求添加测试代码的情况下，不需要添加Test代码
 - 代码修改不应该以保守作为准则，需要按照既定计划执行
 - 代码修改前，需要先列出修改计划，经过我的同意后才能实施代码修改。
 - 计划文档不需要加入版本控制

## Cursor Cloud specific instructions

Piccolo Engine is a C++17/CMake game engine. The product is the **PiccoloEditor**
desktop GUI app (Vulkan renderer). There is no client/server split — building and
running the editor is the whole flow.

### Build & run
- Build: `./build_linux.sh release ninja` (or `debug`). The script forces
  `CC=clang`/`CXX=clang++`, configures into `build/`, and deploys the runnable
  app into `bin/`. Run the editor from there: `cd bin && ./PiccoloEditor`
  (it reads `bin/PiccoloEditor.ini`; `RenderBackend=Auto` resolves to Vulkan on Linux).
- A clean release build takes ~1–2 min on 4 cores. `engine/3rdparty` is fully
  vendored, so there are no package-manager dependencies to fetch.

### Toolchain caveat (important, non-obvious)
- The vendored JoltPhysics / VulkanMemoryAllocator and the `lganim` animation
  sources predate modern toolchains: they rely on transitive `<cstdint>`,
  `<cstdio>`, `<cstdlib>`, `<cstring>`, `<algorithm>` includes (dropped by
  Clang 18 / libstdc++ 14) and use the MSVC-only `fopen_s`/`errno_t` API. Without
  a fix the Linux build fails with many "unknown type name 'uint32_t' / undeclared
  'fopen_s'" errors.
- This is worked around at the **environment** level (no repo source is modified):
  a `clang++` wrapper at `/usr/local/bin/clang++` (ahead of `/usr/bin/clang++` in
  PATH) force-includes `/usr/local/include/piccolo_linux_compat.h`, which pulls the
  missing headers and defines a non-Windows `fopen_s` shim. The startup update
  script recreates both files idempotently. Do not delete them or the build breaks.
- Clang selects the GCC 14 toolchain, so `libstdc++-14-dev` must be present
  (provides the `libstdc++.so` link). It is installed in the VM snapshot.

### Running headless (no GPU in the VM)
- Vulkan runs via Mesa **lavapipe** (software, llvmpipe). Set:
  `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json` and
  `XDG_RUNTIME_DIR=/tmp/runtime-ubuntu` (mkdir it). Expect ~6–10 FPS.
- For a non-visible smoke test use a virtual display:
  `Xvfb :99 -screen 0 1920x1080x24 &` then `DISPLAY=:99 ./PiccoloEditor`.
- Display `:1` is the real desktop that screen-recording / computer-use tools see.
  Run the editor on `DISPLAY=:1` for visible/interactive demos; use `xdotool` to
  position the window. The default world (`asset/world/hello.world.json`) loads a
  scene with a character, walls and physics bodies.

### System dependencies (in the VM snapshot; reinstall via apt if missing)
`libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libglvnd-dev libvulkan-dev
libxrender-dev libstdc++-14-dev cmake ninja-build clang` for building, and
`mesa-vulkan-drivers vulkan-tools xvfb x11-utils xdotool ffmpeg` to run/record headless.

### Lint & tests
- There are **no automated tests** wired into the Linux build (`engine/source/test`
  is empty and its `add_subdirectory` is commented out in `engine/CMakeLists.txt`).
  The `scripts/tests/render_backend` smoke tests are Windows/PowerShell only.
- Lint configs `.clang-format` / `.clang-tidy` exist but there is no CI lint step;
  `clang-format`/`clang-tidy` are not installed by default.
