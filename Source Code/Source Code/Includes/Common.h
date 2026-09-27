#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <dcomp.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <thread>

namespace Nullframe {
    constexpr wchar_t ClassName[] = L"NullframeWindowClass";
    constexpr wchar_t Title[] = L"Nullframe";
    constexpr int     UnloadKey = VK_END;

    struct InputState {
        std::atomic<long>      x{ 0 };
        std::atomic<long>      y{ 0 };
        std::atomic<long long> changedTick{ 0 };
    };

    struct FrameStats {
        float time = 0.0f;
        float fps = 0.0f;
        float inputLatencyMs = 0.0f;
        float averageInputLatencyMs = 0.0f;
        float displayLatencyMs = 0.0f;
        float cpuMs = 0.0f;
        float frameIntervalMs = 0.0f;
        int   cursorX = 0;
        int   cursorY = 0;
    };

    class LatencyTracker {
    public:
        LatencyTracker() {
            QueryPerformanceFrequency(&freq_);
            QueryPerformanceCounter(&lastFrameTime_);
            frameStart_ = lastFrameTime_;
            inputTime_ = lastFrameTime_;
            presentTime_ = lastFrameTime_;

            inputPending_ = false;
            presentPending_ = false;
            inputLatencyMs_ = 0.0f;
            averageMs_ = 0.0f;
            displayLatencyMs_ = 0.0f;
            cpuMs_ = 0.0f;
            intervalMs_ = 0.0f;
            accumMs_ = 0.0f;
            sampleCount_ = 0;
        }

        void BeginFrame() {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            intervalMs_ = static_cast<float>(now.QuadPart - lastFrameTime_.QuadPart)
                * 1000.0f / static_cast<float>(freq_.QuadPart);
            lastFrameTime_ = now;
            frameStart_ = now;
        }

        void MarkInputAt(long long qpcTick) {
            inputTime_.QuadPart = qpcTick;
            inputPending_ = true;
        }

        void MarkPresentStart() {
            QueryPerformanceCounter(&presentTime_);
            presentPending_ = true;
        }

        void EndFrame() {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            cpuMs_ = static_cast<float>(now.QuadPart - frameStart_.QuadPart)
                * 1000.0f / static_cast<float>(freq_.QuadPart);
        }

        void MarkDisplayed() {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);

            if (presentPending_) {
                displayLatencyMs_ = static_cast<float>(now.QuadPart - presentTime_.QuadPart)
                    * 1000.0f / static_cast<float>(freq_.QuadPart);
                presentPending_ = false;
            }

            if (inputPending_) {
                inputLatencyMs_ = static_cast<float>(now.QuadPart - inputTime_.QuadPart)
                    * 1000.0f / static_cast<float>(freq_.QuadPart);
                accumMs_ += inputLatencyMs_;
                sampleCount_++;
                if (sampleCount_ >= 30) {
                    averageMs_ = accumMs_ / static_cast<float>(sampleCount_);
                    accumMs_ = 0.0f;
                    sampleCount_ = 0;
                }
                inputPending_ = false;
            }
        }

        FrameStats ToStats(float time, float fps, int cursorX, int cursorY) const noexcept {
            FrameStats s;
            s.time = time;
            s.fps = fps;
            s.inputLatencyMs = inputLatencyMs_;
            s.averageInputLatencyMs = averageMs_;
            s.displayLatencyMs = displayLatencyMs_;
            s.cpuMs = cpuMs_;
            s.frameIntervalMs = intervalMs_;
            s.cursorX = cursorX;
            s.cursorY = cursorY;
            return s;
        }

    private:
        LARGE_INTEGER freq_;
        LARGE_INTEGER lastFrameTime_;
        LARGE_INTEGER frameStart_;
        LARGE_INTEGER inputTime_;
        LARGE_INTEGER presentTime_;

        bool  inputPending_;
        bool  presentPending_;
        float inputLatencyMs_;
        float averageMs_;
        float displayLatencyMs_;
        float cpuMs_;
        float intervalMs_;
        float accumMs_;
        int   sampleCount_;
    };
}