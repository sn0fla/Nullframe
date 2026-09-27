#pragma once
#include "Common.h"

namespace Nullframe {

    class Window {
    public:
        bool Create(HINSTANCE instance);
        void Destroy();
        HWND Handle() const noexcept { return handle_; }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        HWND handle_ = nullptr;
    };

}