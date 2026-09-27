# Nullframe

> A zero-injection, GPU-composited Windows overlay that renders **on top of fullscreen
> applications** — including exclusive-fullscreen games — without hooking, patching, or
> injecting a single byte into the target process.

Nullframe is a reference implementation of the "correct" way to build a system-wide
overlay on Windows: an **external process**, promoted to **UIAccess**, presenting a
**flip-model DXGI swapchain through DirectComposition** to a **layered, click-through,
topmost, capture-excluded** popup window.

It also ships with a first-class **latency measurement harness** (input→photon,
present→photon, CPU frame time, frame interval) so you can actually *prove* how much
latency the overlay adds.

---

## Table of Contents

- [What it does](#what-it-does)
- [How it draws over fullscreen apps](#how-it-draws-over-fullscreen-apps)
- [Why it's better than other overlays](#why-its-better-than-other-overlays)
- [Rendering pipeline](#rendering-pipeline)
- [Latency measurement](#latency-measurement)
- [Window & composition flags](#window--composition-flags)
- [Project layout](#project-layout)
- [Building](#building)
- [Requirements](#requirements)
- [Caveats](#caveats)
- [License](#license)

---

## What it does

Nullframe puts a translucent, always-on-top HUD over the entire desktop and over
fullscreen applications. Out of the box it renders:

- A framed border around the screen
- A moving shape (proof of animation / compositor correctness)
- A cursor dot tracking the *real* hardware cursor position, sampled on a
  dedicated high-resolution waitable-timer thread
- A live stats block: elapsed time, FPS, frame interval, CPU frame time,
  `present → display` latency, `input → display` latency, and a rolling 30-sample
  average of input latency

Press **`END`** to exit.

Because the window is `WS_EX_TRANSPARENT`, returns `HTTRANSPARENT` from
`WM_NCHITTEST`, and returns `MA_NOACTIVATE` from `WM_MOUSEACTIVATE`, it is **fully
click-through** and **never steals focus** — it is a pure display surface.

---

## How it draws over fullscreen apps

This is the interesting part. Windows normally makes it *deliberately hard* to draw
on top of a fullscreen application, because that's exactly what malware and cheats
want to do. Nullframe gets there through three cooperating mechanisms.

### 1. UIAccess — the thing that actually lets you sit above fullscreen

A window marked `WS_EX_TOPMOST` is only topmost **within its own desktop band**.
When a fullscreen exclusive app or an elevated window owns the screen, a normal
topmost window is pushed behind it by the shell's fullscreen-app heuristics.

**UIAccess** is a flag in the process token (`TokenUIAccess`) granted by Windows to
accessibility tooling (Narrator, Magnifier, On-Screen Keyboard). A process holding
UIAccess is permitted to:

- Set a window as topmost that will be composited **above fullscreen exclusive apps**
  and above windows owned by higher-integrity processes.
- Bypass UIPI restrictions that would otherwise block a lower-integrity process from
  interacting with the desktop at that level.

Normally, obtaining UIAccess requires your binary to be **signed by a trusted
certificate** and **installed under a secure path** (`%ProgramFiles%`). That's
expensive and painful for an overlay project.

Nullframe sidesteps this with a well-known, entirely legitimate technique — the
**same mechanism accessibility tools use to bootstrap themselves**:

```cpp
// Main.cpp — RelaunchWithUIAccess()

1. Locate (or launch hidden) osk.exe — the On-Screen Keyboard.
   osk.exe is shipped by Microsoft, is signed, lives in a secure path,
   and already runs with UIAccess in its token.

2. OpenProcessToken(hProcess, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)

3. DuplicateTokenEx(..., SecurityImpersonation, TokenPrimary, &hNewToken)

4. CreateProcessAsUserW(hNewToken, currentExe, "\"%s\" --ui-active", ...)
```

The child process inherits the **UIAccess** flag from the duplicated token. From
that point on, its topmost window is composited above fullscreen applications
without any injection whatsoever.

The parent process immediately exits; the `--ui-active` instance is the one that
runs the message loop. No elevation dialog is shown in the default path, because
the token is duplicated from an already-running process.

> The `Release|x64` configuration in `Nullframe.vcxproj` sets
> `UACExecutionLevel = RequireAdministrator` for the case where `osk.exe` isn't
> available and you need to bootstrap from an elevated context.

### 2. DirectComposition — the present path that survives fullscreen

Creating a normal `HWND` + `IDXGISwapChain::Present` overlay is not enough: on a
flip-model swapchain bound to an `HWND`, DWM can drop you when a fullscreen app
takes over, and the legacy `BLT` path routes through GDI redirection, which is both
slow and flicker-prone.

Nullframe uses **`CreateSwapChainForComposition`** and binds the swapchain to the
window through a **DirectComposition visual**:

```
ID3D11Device
   └── IDXGIDevice ──> DCompositionCreateDevice ──> IDCompositionDevice
                                                        ├── IDCompositionTarget  (CreateTargetForHwnd, topmost=TRUE)
                                                        ├── IDCompositionVisual  (SetContent(swapChain))
                                                        └── Commit()
```

This gives you:

- The swapchain is composited **by DWM in the kernel-mode compositor**, not by GDI.
- No redirection bitmap is allocated for the window — which is exactly why the
  window is created with **`WS_EX_NOREDIRECTIONBITMAP`**. A DComp-bound window
  *must* use this style, and in exchange you get zero redirection-surface cost.
- Content is presented atomically as a single visual; no tearing, no partial frames,
  no flicker on resize or occlusion.
- The window keeps its topmost/UIAccess status *and* the swapchain keeps presenting
  — DWM never arbitrates the overlay away.

### 3. Layered, click-through, capture-excluded window

The window itself is created with a carefully chosen combination of styles:

| Style / Call | Purpose |
|---|---|
| `WS_EX_TOPMOST` | Stays above normal windows in the topmost band |
| `WS_EX_TOOLWINDOW` | Hidden from the taskbar and Alt+Tab |
| `WS_EX_NOACTIVATE` | Never becomes the foreground window |
| `WS_EX_TRANSPARENT` | Hit-testing passes through to whatever is beneath |
| `WS_EX_LAYERED` | Enables per-window alpha |
| `WS_EX_NOREDIRECTIONBITMAP` | Required for DComp; removes the GDI redirection surface |
| `WS_POPUP \| WS_VISIBLE` | Borderless, immediately visible |
| `WM_NCHITTEST → HTTRANSPARENT` | Belt-and-braces click-through |
| `WM_MOUSEACTIVATE → MA_NOACTIVATE` | Never steals focus |
| `SetLayeredWindowAttributes(0, 255, LWA_ALPHA)` | Alpha blending enabled |
| `SetWindowDisplayAffinity(hwnd, 0x11)` | **`WDA_EXCLUDEFROMCAPTURE`** — the window is invisible to screen capture, OBS, and `PrintScreen` |

That last one is `WDA_EXCLUDEFROMCAPTURE` (Windows 10 2004+). The overlay renders on
your monitor but **does not appear** in screen recordings, screenshots, or streams.

---

## Why it's better than other overlays

Overlay techniques on Windows broadly fall into a few buckets. Here's how Nullframe
compares.

### Comparison table

| Approach | Covers fullscreen exclusive | Touches target process | Anti-cheat risk | Latency added | Flicker-free | Capture-excluded |
|---|---|---|---|---|---|---|
| **Nullframe** (UIAccess + DComp) | ✅ Yes | ❌ No — external | Low (no injection) | Minimal (1 frame in flight) | ✅ | ✅ |
| Injected DLL + Present hook (ImGui, etc.) | ✅ Yes | ✅ Injects & hooks | **High** | Low | ✅ | ❌ |
| `SetWindowsHookEx` overlay | ⚠️ Partially | ⚠️ Hooks input | Medium | Medium | ⚠️ | ❌ |
| Plain topmost `HWND` + GDI/D3D9 | ❌ No | ❌ No | Low | High (GDI) | ❌ | ❌ |
| `WS_EX_LAYERED` + `UpdateLayeredWindow` | ❌ No | ❌ No | Low | High (CPU copy) | ❌ | ❌ |
| Steam / Discord / Xbox Game Bar | ✅ Yes | ✅ Hooks | Varies | Low | ✅ | ❌ |
| Windows.Graphics.Capture / Magnification API | ⚠️ Not a real overlay | ❌ No | Low | High | ⚠️ | ❌ |

### The specific reasons Nullframe wins

1. **No injection, no hooking, no patching.**
   Nullframe never opens the target process for writing, never allocates memory in
   it, never writes a trampoline, never hooks `IDXGISwapChain::Present` or
   `wglSwapBuffers`. Anti-cheat and EDR systems have nothing to find. This is the
   single biggest differentiator vs. the entire ImGui-overlay ecosystem.

2. **It genuinely covers fullscreen exclusive.**
   Most "overlay" projects quietly don't. They work windowed/borderless and silently
   fail when the target flips to true exclusive fullscreen. UIAccess is the
   documented OS mechanism that makes this work.

3. **The present path is as short as it can possibly be.**
   - `DXGI_SWAP_EFFECT_FLIP_DISCARD` — flip model, no copy.
   - `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` +
     `SetMaximumFrameLatency(1)` — **exactly one frame in flight**, so the overlay
     never queues more than a frame of its own latency.
   - `DXGI_PRESENT_ALLOW_TEARING` on `Present(0, ...)` — bypasses DWM's forced vsync
     when the environment allows it, falling back to `Present(0, 0)` on
     `DXGI_ERROR_INVALID_CALL`.
   - Main loop waits on the **frame-latency waitable object** rather than sleeping,
     so it wakes exactly when DXGI is ready to accept the next frame.

4. **DirectComposition removes GDI from the equation entirely.**
   No redirection surface (`WS_EX_NOREDIRECTIONBITMAP`), no `BitBlt`, no
   `UpdateLayeredWindow`, no CPU-side pixel copies. Content goes
   D2D → D3D11 → flip-model swapchain → DWM compositor, all on the GPU.

5. **It self-heals.**
   - `DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_RESET` → full device
     reconstruction via `RecoverDevice()`.
   - `D2DERR_RECREATE_TARGET` → target bitmap discarded and recreated.
   - `DXGI_STATUS_OCCLUDED` → flagged, and the topmost re-assert loop is skipped
     while occluded so it doesn't fight the compositor.
   - Occlusion triggers a `SetWindowPos(HWND_TOPMOST, ..., SWP_NOACTIVATE |
     SWP_NOSENDCHANGING)` re-assert at most once per second, so the overlay
     recovers gracefully after another fullscreen app exits.

6. **It measures itself.**
   Almost no overlay ships with real latency instrumentation. Nullframe's
   `LatencyTracker` reports input→display, present→display, CPU frame time, and
   frame interval, all from `QueryPerformanceCounter`. See below.

7. **Zero third-party dependencies.**
   Pure Win32 + D3D11 + DXGI + Direct2D + DirectWrite + DirectComposition. No
   ImGui, no Detours, no MinHook, no DirectX SDK. The entire binary is ~1,200 lines
   of C++17.

---

## Rendering pipeline

```
                        ┌─────────────────────────────────────────┐
                        │  D3D11CreateDevice (HW / WARP fallback) │
                        └────────────────────┬────────────────────┘
                                             │
                 ┌───────────────────────────┼──────────────────────────┐
                 ▼                           ▼                          ▼
   IDXGIFactory2::                 IDXGIDevice ──>              IDXGIDevice ──>
   CreateSwapChainForComposition   DCompositionCreateDevice     ID2D1Factory1::CreateDevice
        │                                │                            │
        │ FLIP_DISCARD                   │                            ▼
        │ ALLOW_TEARING                  │                    ID2D1DeviceContext
        │ FRAME_LATENCY_WAITABLE         │                    (D2D1_UNIT_MODE_PIXELS)
        │ BufferCount = 2                │                            │
        │ AlphaMode = PREMULTIPLIED      │                            │
        ▼                                ▼                            ▼
   IDXGISwapChain1  ◄──── SetContent ──  IDCompositionVisual      BeginDraw / Clear
        │                                     ▲                       │
        │                                     │                       │
        │                            IDCompositionTarget              │
        │                            (CreateTargetForHwnd)            │
        ▼                                                             ▼
   GetBuffer(0) ──> IDXGISurface ──> CreateBitmapFromDxgiSurface ──> targetBitmap_
                                                                      │
                                                                      ▼
                                                              SetTarget + Draw
                                                                      │
                                                                      ▼
                                                              EndDraw ──> Present
```

Per frame:

1. `Begin()` — create the target bitmap from the swapchain backbuffer, `SetTarget`,
   `BeginDraw()`, `Clear(transparent)`.
2. `Draw(stats)` — D2D1 border rectangle, animated ellipse, cursor dot, three lines
   of DirectWrite text.
3. `MarkPresentStart()` — QPC timestamp immediately before `Present`.
4. `End()` — `EndDraw()`, release the target bitmap, `Present(0, ALLOW_TEARING)`.
5. Wait on the frame-latency waitable object.
6. `MarkDisplayed()` — QPC timestamp when DXGI tells us the previous frame retired.

---

## Latency measurement

`LatencyTracker` (in `Common.h`) is the part most overlay projects get wrong or skip
entirely.

| Metric | What it means | How it's measured |
|---|---|---|
| `frameIntervalMs` | Wall-clock time between `BeginFrame()` calls | QPC delta between successive frames |
| `cpuMs` | Time spent inside `Begin()` → `Draw()` → `End()` | QPC delta from `frameStart_` to `EndFrame()` |
| `inputLatencyMs` | `cursor move` → `frame displayed` | QPC delta from the sampler thread's `changedTick` to `MarkDisplayed()` |
| `averageInputLatencyMs` | Rolling average over 30 samples | Accumulated in `MarkDisplayed()` |
| `displayLatencyMs` | `Present()` → frame retired by the compositor | QPC delta from `presentTime_` to `MarkDisplayed()` |

The input timestamp is captured on a **dedicated sampling thread** driven by a
`CreateWaitableTimerExW(..., CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, ...)` set to a
**0.5 ms initial due time with a 1 ms period**, which is the highest-resolution
timer the Windows kernel exposes without spin-waiting. The sampler polls
`GetCursorPos()` and only publishes a new `changedTick` when the position actually
changes, so `MarkInputAt()` receives a genuine hardware-level timestamp rather than
a "when the main thread happened to look" approximation.

This is what makes the reported `input → display` number meaningful: it measures
from the *moment the cursor physically moved* to the *moment the frame containing
that position was retired by the compositor*.

---

## Window & composition flags

For reference, the complete window configuration:

```cpp
CreateWindowExW(
    WS_EX_TOPMOST              // above normal windows
  | WS_EX_TOOLWINDOW           // no taskbar / Alt+Tab entry
  | WS_EX_NOACTIVATE           // never foregrounded
  | WS_EX_TRANSPARENT          // hit-test passthrough
  | WS_EX_LAYERED              // per-window alpha
  | WS_EX_NOREDIRECTIONBITMAP, // required for DComp, kills GDI redirection surface
    ClassName, Title,
    WS_POPUP | WS_VISIBLE,
    0, 0, screenW, screenH,
    nullptr, nullptr, instance, nullptr);

SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE); // 0x00000011
SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
```

DPI awareness is set to `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` at startup so
the overlay matches the physical pixel grid on mixed-DPI setups.

---

## Project layout

```
Nullframe/
├── CMakeLists.txt
├── Nullframe.vcxproj
├── Nullframe.vcxproj.filters
└── Source Code/
    ├── CMakeLists.txt
    ├── Main.cpp                 # WinMain, UIAccess bootstrap, main loop, sampler thread
    ├── Window.cpp               # Win32 window class, styles, WndProc
    ├── Renderer.cpp             # D3D11 + DXGI + DComp + D2D1 + DirectWrite
    └── Includes/
        ├── Common.h             # Win32/DX headers, FrameStats, InputState, LatencyTracker
        ├── Window.h
        └── Renderer.h
```

### Class overview

| Class | Responsibility |
|---|---|
| `Nullframe::Window` | Registers the window class, creates the layered/topmost/click-through popup, owns `WndProc` |
| `Nullframe::Renderer` | Owns the D3D11 device, flip-model composition swapchain, DirectComposition tree, D2D1 device context, and all brushes / text formats. Handles device-loss recovery |
| `Nullframe::LatencyTracker` | QPC-based instrumentation for frame interval, CPU time, present→display, and input→display latency |
| `Nullframe::InputState` | Lock-free (`std::atomic`) cursor position + change tick shared between the sampler thread and the render thread |
| `Nullframe::FrameStats` | POD snapshot passed from the main loop into `Renderer::Draw()` |

---

## Building

### CMake (recommended)

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The target links: `d3d11 dxgi d2d1 dwrite dcomp user32 gdi32 advapi32 shell32`,
sets `C++17`, `/W4 /permissive-`, and links with
`/SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup`.

### MSVC (Visual Studio)

Open `Nullframe.vcxproj` and build. The project targets `v145` and
`WindowsTargetPlatformVersion 10.0`.

> ⚠️ **Release|x64** sets `UACExecutionLevel = RequireAdministrator`. If you don't
> want the elevation prompt, remove that line — the `osk.exe` token path handles
> UIAccess acquisition on its own.

### Run

```bash
Nullframe.exe              # bootstraps UIAccess via osk.exe, relaunches itself
Nullframe.exe --ui-active  # internal — skips the bootstrap
```

Press **`END`** to quit.

---

## Requirements

- **Windows 10 2004 (build 19041) or newer** — required for
  `WDA_EXCLUDEFROMCAPTURE` and reliable `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`
  behaviour with `CreateSwapChainForComposition`.
- **Windows 8 or newer** for `DCompositionCreateDevice` + `WS_EX_NOREDIRECTIONBITMAP`.
- A GPU with a D3D11 driver. WARP is used automatically as a fallback.
- `osk.exe` present on the system (Tablet PC / accessibility component). It ships
  with all consumer and Pro SKUs; it may be absent on some Server / LTSC /
  debloated images.
- A C++17 compiler (MSVC 2019+ recommended).

---

## Caveats

Being honest about what this does and doesn't do:

- **Anti-cheat.** Even though Nullframe doesn't inject or hook, some kernel
  anti-cheat drivers flag *any* process holding UIAccess, or any process with a
  topmost window over the game. Nullframe is an overlay *technique* — how you use
  it is on you. Don't point this at competitive multiplayer games.
- **HDR and exclusive-mode quirks.** True exclusive fullscreen DXGI in HDR mode may
  still preempt composition on some driver/OS combinations. Borderless-fullscreen
  and windowed always work.
- **DRM-protected content.** Playback paths using hardware DRM (Netflix UHD,
  some Blu-ray players) render to a protected surface that overlays cannot composite
  over, by design.
- **The `osk.exe` token trick is a legitimate accessibility pattern, not a
  vulnerability.** It's the same mechanism Magnifier and Narrator use. It does,
  however, produce a UIAccess process that some EDR products will log.
- **This is a reference/demo.** The drawn content (border, ellipse, stats) is
  placeholder — swap `Renderer::Draw()` for whatever you actually want to display.
- **`SetWindowDisplayAffinity` is one-way.** Once set on a window it can only be
  changed to `WDA_NONE` or back, never to another mode, and the window must not be
  a child.

---

## License

MIT — see `LICENSE`.

---

<p align="center">
  <sub>Built with Win32, D3D11, DXGI, DirectComposition, Direct2D, and DirectWrite. No third-party code.</sub>
</p>
```
