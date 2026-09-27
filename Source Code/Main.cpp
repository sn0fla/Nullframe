#include "Includes/Common.h"
#include "Includes/Window.h"
#include "Includes/Renderer.h"

#include <tlhelp32.h>
#include <securitybaseapi.h>
#include <shellapi.h>
#include <string.h>

namespace {

    DWORD GetProcessIdByName(const wchar_t* processName) {
        DWORD pid = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry = { sizeof(entry) };
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    if (_wcsicmp(entry.szExeFile, processName) == 0) {
                        pid = entry.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
        return pid;
    }

    bool RelaunchWithUIAccess() {
        DWORD targetPid = GetProcessIdByName(L"osk.exe");
        if (targetPid == 0) {
            HINSTANCE sh = ShellExecuteW(nullptr, L"open", L"osk.exe", nullptr, nullptr, SW_HIDE);
            if (reinterpret_cast<INT_PTR>(sh) <= 32) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            targetPid = GetProcessIdByName(L"osk.exe");
        }

        if (targetPid == 0) return false;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, targetPid);
        if (!hProcess) return false;

        HANDLE hToken = nullptr;
        if (!OpenProcessToken(hProcess, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
            CloseHandle(hProcess);
            return false;
        }

        HANDLE hNewToken = nullptr;
        BOOL dup = DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, nullptr,
            SecurityImpersonation, TokenPrimary, &hNewToken);

        CloseHandle(hToken);
        CloseHandle(hProcess);

        if (!dup || !hNewToken) return false;

        wchar_t currentExe[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, currentExe, MAX_PATH)) {
            CloseHandle(hNewToken);
            return false;
        }

        wchar_t cmdLine[MAX_PATH + 32];
        swprintf_s(cmdLine, L"\"%s\" --ui-active", currentExe);

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};

        BOOL ok = CreateProcessAsUserW(
            hNewToken,
            currentExe,
            cmdLine,
            nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &si, &pi
        );

        if (ok) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        CloseHandle(hNewToken);
        return ok != FALSE;
    }

    bool Run() {
        Nullframe::Window         window;
        Nullframe::Renderer       renderer;
        Nullframe::LatencyTracker tracker;
        Nullframe::InputState     input;

        HINSTANCE instance = GetModuleHandleW(nullptr);
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);

        if (!window.Create(instance))
            return false;

        if (!renderer.Initialize(window.Handle(), screenW, screenH)) {
            renderer.Shutdown();
            window.Destroy();
            return false;
        }

        LARGE_INTEGER freq, prev, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&prev);
        const LARGE_INTEGER start = prev;
        LARGE_INTEGER lastTopmost = prev;

        float fps = 0.0f;
        int   frames = 0;
        float fpsTimer = 0.0f;

        POINT initialCursor = {};
        GetCursorPos(&initialCursor);
        input.x.store(initialCursor.x, std::memory_order_relaxed);
        input.y.store(initialCursor.y, std::memory_order_relaxed);

        HANDLE sampleTimer = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!sampleTimer) {
            sampleTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }

        std::atomic<bool> sampling{ true };
        std::thread sampler;

        if (sampleTimer) {
            LARGE_INTEGER due = {};
            due.QuadPart = -5000LL;
            SetWaitableTimer(sampleTimer, &due, 1, nullptr, nullptr, FALSE);

            sampler = std::thread([&]() {
                POINT p = {};
                long lastX = 0;
                long lastY = 0;
                bool first = true;

                while (sampling.load(std::memory_order_relaxed)) {
                    WaitForSingleObject(sampleTimer, INFINITE);
                    if (!GetCursorPos(&p)) continue;
                    if (first || p.x != lastX || p.y != lastY) {
                        LARGE_INTEGER t;
                        QueryPerformanceCounter(&t);
                        input.x.store(p.x, std::memory_order_relaxed);
                        input.y.store(p.y, std::memory_order_relaxed);
                        input.changedTick.store(t.QuadPart, std::memory_order_release);
                        lastX = p.x;
                        lastY = p.y;
                        first = false;
                    }
                }
                });
        }

        long long lastSeenTick = -1;
        int cursorX = initialCursor.x;
        int cursorY = initialCursor.y;

        bool running = true;
        MSG  msg;

        while (running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (!running)
                break;

            if (GetAsyncKeyState(Nullframe::UnloadKey) & 0x8000)
                break;

            if (renderer.IsDeviceLost()) {
                if (!renderer.RecoverDevice())
                    break;
                QueryPerformanceCounter(&prev);
                lastTopmost = prev;
                continue;
            }

            tracker.BeginFrame();

            const long long tick = input.changedTick.load(std::memory_order_acquire);
            if (tick != lastSeenTick) {
                tracker.MarkInputAt(tick);
                lastSeenTick = tick;
                cursorX = static_cast<int>(input.x.load(std::memory_order_relaxed));
                cursorY = static_cast<int>(input.y.load(std::memory_order_relaxed));
            }

            QueryPerformanceCounter(&now);
            const float dt = static_cast<float>(now.QuadPart - prev.QuadPart)
                / static_cast<float>(freq.QuadPart);
            prev = now;

            const float elapsed = static_cast<float>(now.QuadPart - start.QuadPart)
                / static_cast<float>(freq.QuadPart);

            frames++;
            fpsTimer += dt;
            if (fpsTimer >= 0.5f) {
                fps = static_cast<float>(frames) / fpsTimer;
                frames = 0;
                fpsTimer = 0.0f;
            }

            if (!renderer.IsOccluded() &&
                (now.QuadPart - lastTopmost.QuadPart) > freq.QuadPart) {
                HWND fg = GetForegroundWindow();
                if (fg != window.Handle() && fg != nullptr) {
                    SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                        | SWP_NOSENDCHANGING);
                }
                lastTopmost = now;
            }

            const Nullframe::FrameStats stats =
                tracker.ToStats(elapsed, fps, cursorX, cursorY);

            renderer.Begin();
            renderer.Draw(stats);
            tracker.MarkPresentStart();
            renderer.End();

            tracker.EndFrame();

            if (renderer.FrameLatencyHandle() != nullptr) {
                WaitForSingleObjectEx(renderer.FrameLatencyHandle(), 1000, TRUE);
            }
            else {
                std::this_thread::yield();
            }
            tracker.MarkDisplayed();
        }

        sampling.store(false, std::memory_order_relaxed);
        if (sampler.joinable()) sampler.join();
        if (sampleTimer) CloseHandle(sampleTimer);

        renderer.Shutdown();
        window.Destroy();
        return true;
    }

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    const bool uiActive = (argc >= 2 && wcscmp(argv[1], L"--ui-active") == 0);
    LocalFree(argv);

    if (!uiActive) {
        if (RelaunchWithUIAccess())
            return 0;
    }

    return Run() ? 0 : 1;
}