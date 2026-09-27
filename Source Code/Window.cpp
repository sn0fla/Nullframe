#include "Includes/Window.h"

namespace Nullframe {

    LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_CLOSE:
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool Window::Create(HINSTANCE instance) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = ClassName;

        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;

        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);

        handle_ = CreateWindowExW(
            WS_EX_TOPMOST
            | WS_EX_TOOLWINDOW
            | WS_EX_NOACTIVATE
            | WS_EX_TRANSPARENT
            | WS_EX_LAYERED
            | WS_EX_NOREDIRECTIONBITMAP,
            ClassName, Title,
            WS_POPUP | WS_VISIBLE,
            0, 0, screenW, screenH,
            nullptr, nullptr, instance, nullptr);

        if (!handle_)
            return false;

        SetWindowDisplayAffinity(handle_, 0x00000011);

        SetLayeredWindowAttributes(handle_, 0, 255, LWA_ALPHA);

        SetWindowPos(handle_, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

        return true;
    }

    void Window::Destroy() {
        if (handle_) {
            DestroyWindow(handle_);
            handle_ = nullptr;
        }
        UnregisterClassW(ClassName, GetModuleHandleW(nullptr));
    }

}