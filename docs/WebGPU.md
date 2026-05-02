# WebGPU Backend

The WebGPU backend is experimental. It uses Dawn as the native WebGPU
implementation and WGSL as the shader source format.

## Requirements

- CMake 3.15 or newer
- A C++17-capable toolchain
- `BUILD_WEBGPU=ON`
- Dawn, either supplied as a checkout or fetched by CMake
- `BUILD_TOOL=ON` when rebuilding WGSL shader assets with `ShaderTranspiler`

On Windows, Dawn normally uses D3D12. Vulkan or other Dawn adapters may be
available depending on the local Dawn build and runtime environment.

## CMake Options

| Option | Default | Description |
| --- | --- | --- |
| `BUILD_WEBGPU` | `OFF` | Enable the WebGPU backend |
| `WEBGPU_DAWN_SOURCE_DIR` | empty | Use an existing Dawn checkout |
| `WEBGPU_FETCH_DAWN` | `ON` | Fetch Dawn with `FetchContent` when no checkout is available |
| `WEBGPU_DAWN_FETCH_DEPENDENCIES` | `ON` | Let Dawn fetch/sync its own dependencies during configure |
| `WEBGPU_DAWN_BUILD_SAMPLES` | `OFF` | Build Dawn sample executables |
| `WEBGPU_DAWN_FORCE_SYSTEM_COMPONENT_LOAD` | `OFF` | Let Dawn load Windows system components such as `d3dcompiler_47.dll` from System32 |
| `WEBGPU_DAWN_GIT_REPOSITORY` | `https://dawn.googlesource.com/dawn` | Dawn repository used by `FetchContent` |
| `WEBGPU_DAWN_GIT_TAG` | `main` | Dawn branch, tag, or commit used by `FetchContent` |

## Getting Dawn

### FetchContent

If neither `WEBGPU_DAWN_SOURCE_DIR` nor `thirdparty/dawn` is present, CMake
fetches Dawn automatically:

```bash
cmake -S . -B build-webgpu \
  -DBUILD_WEBGPU=ON \
  -DBUILD_TEST=ON \
  -DBUILD_TOOL=ON \
  -DWEBGPU_FETCH_DAWN=ON \
  -DWEBGPU_DAWN_GIT_TAG=main
cmake --build build-webgpu --config Release
```

Pin `WEBGPU_DAWN_GIT_TAG` to a known commit for reproducible builds.

### Existing Dawn Checkout

You can use a separate Dawn checkout:

```bash
cmake -S . -B build-webgpu \
  -DBUILD_WEBGPU=ON \
  -DBUILD_TEST=ON \
  -DBUILD_TOOL=ON \
  -DWEBGPU_DAWN_SOURCE_DIR=/path/to/dawn
cmake --build build-webgpu --config Release
```

### `thirdparty/dawn`

The repository also checks `thirdparty/dawn` automatically. This is useful when
you want Dawn to live inside the LLGI working tree.

```bash
cd thirdparty
git clone https://dawn.googlesource.com/dawn dawn
cd dawn
cp scripts/standalone.gclient .gclient
gclient sync
```

Then configure LLGI with `-DBUILD_WEBGPU=ON`.

## Running Tests

Build `LLGI_Test` with `BUILD_TEST=ON`, then pass `--webgpu`:

```bash
# Windows
build-webgpu\src_test\Release\LLGI_Test.exe --webgpu
build-webgpu\src_test\Release\LLGI_Test.exe --webgpu --filter=SimpleRender.*

# Linux / macOS
./build-webgpu/src_test/LLGI_Test --webgpu
./build-webgpu/src_test/LLGI_Test --webgpu --filter=SimpleRender.*
```

Some environments need a visible GPU session for Dawn to create a WebGPU device.
Headless CI may need Dawn-specific setup.

## Shader Generation

`ShaderTranspiler` can emit WGSL and compiled WGSL blobs for WebGPU tests.

```bash
cmake -S . -B build-webgpu \
  -DBUILD_TOOL=ON \
  -DBUILD_WEBGPU=ON
cmake --build build-webgpu --config Release --target ShaderTranspiler

python scripts/transpile.py src_test/Shaders/
```

Generated WebGPU shaders are stored under:

- `src_test/Shaders/WebGPU/`
- `src_test/Shaders/WebGPU_Compiled/`

The compiled files use an LLGI header followed by WGSL text. Runtime WebGPU
shader creation expects WGSL or this compiled WGSL format; legacy runtime WGSL
rewrites are not applied.

## Current Limitations

- The backend is experimental and is not enabled by default.
- Dawn API and Tint WGSL output can change. Prefer pinning
  `WEBGPU_DAWN_GIT_TAG` for stable builds.
- The WebGPU backend currently depends on Dawn CMake targets
  `dawn::webgpu_dawn` or `webgpu_dawn`.
- WebGPU shader assets should be regenerated when the shader transpiler or Tint
  revision changes.
- `LLGI::CreateCompiler(DeviceType::WebGPU)` is not a runtime HLSL compiler; use
  `ShaderTranspiler` to generate WGSL assets.
- External-device construction APIs can run without an owned `wgpu::Instance`.
  In that mode, some wait processing is limited to the supplied Dawn objects.

## Troubleshooting

- If configure fails because Dawn is missing, set `WEBGPU_DAWN_SOURCE_DIR`, add
  `thirdparty/dawn`, or keep `WEBGPU_FETCH_DAWN=ON`.
- If Dawn dependency sync fails, install `depot_tools` and ensure `gclient` is
  available on `PATH`, or use a Dawn checkout whose dependencies are already
  synced.
- If shader creation fails, regenerate WGSL with the current `ShaderTranspiler`
  and check that the generated shaders are under `src_test/Shaders/WebGPU/` and
  `src_test/Shaders/WebGPU_Compiled/`.
