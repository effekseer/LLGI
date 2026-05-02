# WebGPU Browser Test

This document describes how to build and run the browser-only WebGPU backend
tests. These tests compile LLGI to WebAssembly with Emscripten, run it in a real
Chromium-family browser, and use Emdawnwebgpu to access the browser WebGPU API.

The native WebGPU path in `docs/WebGPU.md` uses Dawn directly. The browser path
covered here verifies a separate runtime:

- Emscripten compilation and linking
- Emdawnwebgpu C/C++ bindings
- browser `navigator.gpu` adapter/device creation
- WGSL shader module creation
- render and compute pipeline creation
- browser canvas surface presentation
- offscreen render target readback
- storage buffer compute readback

## Requirements

- CMake 3.15 or newer
- Git
- Node.js
- Emscripten 4.x or newer with `--use-port=emdawnwebgpu`
- Playwright
- A WebGPU-capable Chromium, Chrome, or Edge browser

WebGPU requires a secure context. The runner serves the generated files from
`localhost`; do not open `LLGI_Test.html` directly with `file://`.

## Install Emscripten

Install emsdk from the official repository:

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

On Windows PowerShell:

```powershell
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
.\emsdk install latest
.\emsdk activate latest
.\emsdk_env.ps1
```

Check the installation:

```bash
emcc --version
emcmake --version
```

Run the emsdk environment script again whenever you open a new shell.

## Install Playwright

From the LLGI repository root:

```bash
npm install playwright
npx playwright install chromium
```

On Linux CI or minimal Linux installations, install browser system dependencies:

```bash
npx playwright install-deps chromium
```

## Configure

Use Emscripten's CMake wrapper:

```bash
emcmake cmake -S . -B build-webgpu-browser \
  -DBUILD_WEBGPU=ON \
  -DBUILD_WEBGPU_BROWSER_TEST=ON \
  -DBUILD_TEST=ON
```

PowerShell equivalent:

```powershell
emcmake cmake -S . -B build-webgpu-browser `
  -DBUILD_WEBGPU=ON `
  -DBUILD_WEBGPU_BROWSER_TEST=ON `
  -DBUILD_TEST=ON
```

`BUILD_WEBGPU_BROWSER_TEST=ON` requires an Emscripten toolchain and builds a
browser-focused `LLGI_Test.html`. It does not require a native Dawn checkout.

## Build

```bash
cmake --build build-webgpu-browser --target LLGI_Test
```

The generated files are:

```text
build-webgpu-browser/src_test/LLGI_Test.html
build-webgpu-browser/src_test/LLGI_Test.js
build-webgpu-browser/src_test/LLGI_Test.wasm
build-webgpu-browser/src_test/LLGI_Test.data
```

On Windows, if emsdk is installed on a non-`C:` drive, keep `EM_CACHE` on the
same drive as emsdk. This avoids Emscripten port-build failures caused by
cross-drive relative paths:

```powershell
$env:EM_CACHE = "D:\emscripten-cache-llgi"
cmake --build build-webgpu-browser --target LLGI_Test
```

For CI, cache `EM_CACHE` between runs to avoid rebuilding Emscripten system
libraries and the Emdawnwebgpu port every time.

## Run Automated Tests

Run the Playwright harness from the repository root:

```bash
node src_test/browser/run_webgpu_browser_test.mjs \
  build-webgpu-browser/src_test/LLGI_Test.html
```

PowerShell:

```powershell
node src_test/browser/run_webgpu_browser_test.mjs `
  build-webgpu-browser/src_test/LLGI_Test.html
```

The runner:

- starts a temporary localhost HTTP server
- disables caching for generated `.html`, `.js`, `.wasm`, and `.data` files
- launches Chromium with WebGPU-friendly flags
- waits for the Emscripten module to report completion
- exits non-zero on failure

The success log ends with:

```text
LLGI_TEST_PASS completed
```

The default filter is:

```text
WebGPUBrowser.*
```

Run one test with `--filter`:

```bash
node src_test/browser/run_webgpu_browser_test.mjs \
  build-webgpu-browser/src_test/LLGI_Test.html \
  --filter=WebGPUBrowser.ScreenPresentation
```

## View in a Browser

To see the canvas presentation test, serve the generated files:

```bash
cd build-webgpu-browser/src_test
python -m http.server 8000
```

Open:

```text
http://localhost:8000/LLGI_Test.html?filter=WebGPUBrowser.ScreenPresentation
```

You should see a blue canvas with a colored polygon. Open browser DevTools and
check the Console for:

```text
Start : WebGPUBrowser.ScreenPresentation
LLGI_TEST_PASS completed
```

## CTest

When Node.js is found during CMake configure, CMake registers:

```bash
ctest --test-dir build-webgpu-browser -R LLGI_WebGPU_Browser --output-on-failure
```

The CTest entry expects the `playwright` package to be available to Node.js.

## Current Test Cases

- `WebGPUBrowser.ComputeCompile`
  - loads a WGSL compute shader
  - compiles a compute pipeline
- `WebGPUBrowser.ComputeDispatch`
  - uploads structured input data
  - dispatches a storage-buffer compute shader
  - copies output to a readback buffer
  - maps and verifies computed values
- `WebGPUBrowser.OffscreenRender`
  - creates an offscreen render texture
  - loads WGSL vertex/fragment shaders
  - compiles a render pipeline
  - draws a rectangle
- `WebGPUBrowser.RenderReadback`
  - clears an offscreen render target
  - copies it to a readback buffer
  - verifies pixel values
- `WebGPUBrowser.TextureAndConstantRender`
  - uploads texture data with `Queue::WriteTexture`
  - renders with texture and sampler bind groups
  - renders again with vertex/pixel uniform buffers
  - reads back pixels and verifies clear color and rendered output
- `WebGPUBrowser.ScreenPresentation`
  - creates a browser canvas WebGPU surface
  - renders through `PlatformWebGPU::GetCurrentScreen`
  - leaves a visible blue canvas with a colored polygon

Browser readback uses Asyncify so C++ test code can wait for `MapAsync` and
`Queue::OnSubmittedWorkDone` callbacks while the browser event loop continues to
run.

## CI Notes

A typical CI flow is:

```bash
npm install playwright
npx playwright install chromium
emcmake cmake -S . -B build-webgpu-browser \
  -DBUILD_WEBGPU=ON \
  -DBUILD_WEBGPU_BROWSER_TEST=ON \
  -DBUILD_TEST=ON
cmake --build build-webgpu-browser --target LLGI_Test
node src_test/browser/run_webgpu_browser_test.mjs \
  build-webgpu-browser/src_test/LLGI_Test.html
```

The runner passes these Chromium flags:

- `--enable-unsafe-webgpu`
- `--ignore-gpu-blocklist`
- `--enable-features=Vulkan,UseSkiaRenderer`
- `--use-vulkan=swiftshader`

These help in headless or GPU-limited environments, but browser policy,
drivers, or CI sandboxing can still disable WebGPU.

## Troubleshooting

### `navigator.gpu is not available`

Use `http://localhost` or HTTPS. WebGPU is unavailable from `file://`.

Also check that the browser supports WebGPU and is not blocked by local GPU
policy.

### Playwright fails to launch Chromium

Install the browser:

```bash
npx playwright install chromium
```

If Playwright's downloaded Chromium cannot launch, use an installed browser:

```powershell
$env:CHROME_PATH = "C:\Program Files\Google\Chrome\Application\chrome.exe"
node src_test/browser/run_webgpu_browser_test.mjs `
  build-webgpu-browser/src_test/LLGI_Test.html
```

Common Windows alternatives:

```powershell
$env:CHROME_PATH = "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"
$env:CHROME_PATH = "C:\Program Files\Microsoft\Edge\Application\msedge.exe"
$env:CHROME_PATH = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
```

### Emdawnwebgpu port build fails with a cross-drive path error

Put `EM_CACHE` on the same drive as emsdk:

```powershell
$env:EM_CACHE = "D:\emscripten-cache-llgi"
cmake --build build-webgpu-browser --target LLGI_Test
```

### Browser shows a dark or blank page

Most tests render offscreen and validate results through readback. Only
`WebGPUBrowser.ScreenPresentation` intentionally draws to the visible canvas.

Open:

```text
http://localhost:8000/LLGI_Test.html?filter=WebGPUBrowser.ScreenPresentation
```

### Logs show `LLGI_TEST_FAIL`

Check the preceding browser console lines. The test code prints `Abort on
<file> : <line>` for assertion failures, and the runner also reports WebGPU
validation errors captured by the browser.
