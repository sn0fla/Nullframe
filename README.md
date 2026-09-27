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
