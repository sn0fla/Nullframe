#include "Includes/Renderer.h"

namespace Nullframe {

    bool Renderer::Initialize(HWND hwnd, int width, int height) {
        hwnd_ = hwnd;
        width_ = width;
        height_ = height;
        deviceLost_ = false;
        occluded_ = false;
        drawing_ = false;

        if (!CreateDevice()) { Shutdown(); return false; }
        if (!CreateSwapChain()) { Shutdown(); return false; }
        if (!CreateComposition()) { Shutdown(); return false; }
        if (!CreateResources()) { Shutdown(); return false; }
        return true;
    }

    bool Renderer::RecoverDevice() {
        HWND h = hwnd_;
        int  w = width_;
        int  hh = height_;
        if (!h) return false;
        Shutdown();
        return Initialize(h, w, hh);
    }

    bool Renderer::CreateDevice() {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            &device_, nullptr, &context_);

        if (hr == E_INVALIDARG) {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                &levels[1], 1, D3D11_SDK_VERSION,
                &device_, nullptr, &context_);
        }

        if (FAILED(hr)) {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                &device_, nullptr, &context_);
        }

        return SUCCEEDED(hr);
    }

    bool Renderer::CreateSwapChain() {
        if (!device_) return false;

        IDXGIDevice* dxgiDevice = nullptr;
        if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
            return false;

        IDXGIAdapter* adapter = nullptr;
        if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
            dxgiDevice->Release();
            return false;
        }

        IDXGIFactory2* factory = nullptr;
        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
            adapter->Release();
            dxgiDevice->Release();
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = static_cast<UINT>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = FALSE;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

        HRESULT hr = factory->CreateSwapChainForComposition(device_, &desc, nullptr, &swapChain_);

        if (FAILED(hr)) {
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            hr = factory->CreateSwapChainForComposition(device_, &desc, nullptr, &swapChain_);
        }

        if (FAILED(hr)) {
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.Flags = 0;
            hr = factory->CreateSwapChainForComposition(device_, &desc, nullptr, &swapChain_);
        }

        if (SUCCEEDED(hr)) {
            IDXGISwapChain2* sc2 = nullptr;
            if (SUCCEEDED(swapChain_->QueryInterface(IID_PPV_ARGS(&sc2)))) {
                sc2->SetMaximumFrameLatency(1);
                frameLatencyWaitable_ = sc2->GetFrameLatencyWaitableObject();
                sc2->Release();
            }
        }

        factory->Release();
        adapter->Release();
        dxgiDevice->Release();
        return SUCCEEDED(hr);
    }

    bool Renderer::CreateComposition() {
        if (!device_ || !swapChain_ || !hwnd_) return false;

        IDXGIDevice* dxgiDevice = nullptr;
        if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
            return false;

        HRESULT hr = DCompositionCreateDevice(
            dxgiDevice, __uuidof(IDCompositionDevice), (void**)&compDevice_);
        dxgiDevice->Release();
        if (FAILED(hr)) return false;

        if (FAILED(compDevice_->CreateTargetForHwnd(hwnd_, TRUE, &compTarget_))) return false;
        if (FAILED(compDevice_->CreateVisual(&compVisual_)))                     return false;
        if (FAILED(compVisual_->SetContent(swapChain_)))                         return false;
        if (FAILED(compTarget_->SetRoot(compVisual_)))                           return false;
        if (FAILED(compDevice_->Commit()))                                       return false;
        return true;
    }

    bool Renderer::CreateResources() {
        if (!device_) return false;

        D2D1_FACTORY_OPTIONS opts = {};
        if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1), &opts, (void**)&d2dFactory_)))
            return false;

        IDXGIDevice* dxgiDevice = nullptr;
        if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
            return false;

        HRESULT hr = d2dFactory_->CreateDevice(dxgiDevice, &d2dDevice_);
        dxgiDevice->Release();
        if (FAILED(hr)) return false;

        if (FAILED(d2dDevice_->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_)))
            return false;

        d2dContext_->SetUnitMode(D2D1_UNIT_MODE_PIXELS);

        if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            (IUnknown**)&dwriteFactory_)))
            return false;

        if (FAILED(dwriteFactory_->CreateTextFormat(
            L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"en-us", &textFormat_)))
            return false;

        if (FAILED(d2dContext_->CreateSolidColorBrush(
            D2D1::ColorF(0.10f, 0.90f, 0.60f, 1.0f), &borderBrush_)))
            return false;

        if (FAILED(d2dContext_->CreateSolidColorBrush(
            D2D1::ColorF(1.00f, 1.00f, 1.00f, 1.0f), &textBrush_)))
            return false;

        if (FAILED(d2dContext_->CreateSolidColorBrush(
            D2D1::ColorF(0.90f, 0.35f, 0.55f, 1.0f), &shapeBrush_)))
            return false;

        if (FAILED(d2dContext_->CreateSolidColorBrush(
            D2D1::ColorF(1.00f, 0.85f, 0.10f, 1.0f), &cursorBrush_)))
            return false;

        return true;
    }

    bool Renderer::CreateTargetBitmap() {
        if (!d2dContext_ || !swapChain_) return false;

        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
            return false;

        IDXGISurface* surface = nullptr;
        if (FAILED(backBuffer->QueryInterface(IID_PPV_ARGS(&surface)))) {
            backBuffer->Release();
            return false;
        }

        D2D1_BITMAP_PROPERTIES1 props = {};
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;

        HRESULT hr = d2dContext_->CreateBitmapFromDxgiSurface(surface, &props, &targetBitmap_);
        surface->Release();
        backBuffer->Release();
        return SUCCEEDED(hr);
    }

    void Renderer::DiscardTargetBitmap() {
        if (d2dContext_) d2dContext_->SetTarget(nullptr);
        if (targetBitmap_) {
            targetBitmap_->Release();
            targetBitmap_ = nullptr;
        }
    }

    void Renderer::Begin() {
        if (!d2dContext_ || !swapChain_) return;
        if (drawing_) return;

        if (!CreateTargetBitmap()) return;

        d2dContext_->SetTarget(targetBitmap_);
        d2dContext_->BeginDraw();
        d2dContext_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        drawing_ = true;
    }

    void Renderer::Draw(const FrameStats& stats) {
        if (!drawing_ || !d2dContext_ || !borderBrush_) return;

        const D2D1_SIZE_F size = d2dContext_->GetSize();

        D2D1_RECT_F border = D2D1::RectF(40.0f, 40.0f, size.width - 40.0f, size.height - 40.0f);
        d2dContext_->DrawRectangle(border, borderBrush_, 3.0f);

        const float cx = size.width * 0.5f + std::cos(stats.time) * 220.0f;
        const float cy = size.height * 0.5f + std::sin(stats.time * 1.3f) * 160.0f;
        D2D1_ELLIPSE circle = D2D1::Ellipse(D2D1::Point2F(cx, cy), 42.0f, 42.0f);
        d2dContext_->FillEllipse(circle, shapeBrush_);

        D2D1_ELLIPSE dot = D2D1::Ellipse(
            D2D1::Point2F(static_cast<float>(stats.cursorX), static_cast<float>(stats.cursorY)),
            10.0f, 10.0f);
        d2dContext_->FillEllipse(dot, cursorBrush_);

        wchar_t line1[256];
        swprintf_s(line1,
            L"Nullframe  |  time %.2fs  |  %.1f FPS  |  frame %.2f ms  |  cpu %.2f ms",
            stats.time, stats.fps, stats.frameIntervalMs, stats.cpuMs);

        wchar_t line2[256];
        swprintf_s(line2,
            L"present->display  %.2f ms  |  input->display  %.2f ms  |  avg %.2f ms",
            stats.displayLatencyMs, stats.inputLatencyMs, stats.averageInputLatencyMs);

        wchar_t line3[256];
        swprintf_s(line3, L"press END to exit");

        const float left = 60.0f;
        const float right = size.width - 60.0f;

        D2D1_RECT_F rect1 = D2D1::RectF(left, 60.0f, right, 120.0f);
        d2dContext_->DrawText(
            line1, static_cast<UINT32>(wcslen(line1)),
            textFormat_, rect1, textBrush_);

        D2D1_RECT_F rect2 = D2D1::RectF(left, 100.0f, right, 160.0f);
        d2dContext_->DrawText(
            line2, static_cast<UINT32>(wcslen(line2)),
            textFormat_, rect2, textBrush_);

        D2D1_RECT_F rect3 = D2D1::RectF(left, 140.0f, right, 200.0f);
        d2dContext_->DrawText(
            line3, static_cast<UINT32>(wcslen(line3)),
            textFormat_, rect3, textBrush_);
    }

    void Renderer::End() {
        if (!drawing_ || !d2dContext_ || !swapChain_) return;
        drawing_ = false;

        HRESULT hr = d2dContext_->EndDraw();
        d2dContext_->SetTarget(nullptr);
        DiscardTargetBitmap();

        if (FAILED(hr)) {
            if (hr == D2DERR_RECREATE_TARGET) deviceLost_ = true;
            return;
        }

        HRESULT presentHr = swapChain_->Present(0, DXGI_PRESENT_ALLOW_TEARING);
        if (presentHr == DXGI_ERROR_INVALID_CALL) {
            presentHr = swapChain_->Present(0, 0);
        }

        if (presentHr == DXGI_STATUS_OCCLUDED) {
            occluded_ = true;
        }
        else if (presentHr == DXGI_ERROR_DEVICE_REMOVED ||
            presentHr == DXGI_ERROR_DEVICE_RESET) {
            deviceLost_ = true;
        }
        else if (FAILED(presentHr)) {
            deviceLost_ = true;
        }
        else {
            occluded_ = false;
        }
    }

    void Renderer::ReleaseResources() {
        DiscardTargetBitmap();

        if (cursorBrush_) { cursorBrush_->Release(); cursorBrush_ = nullptr; }
        if (borderBrush_) { borderBrush_->Release();   borderBrush_ = nullptr; }
        if (textBrush_) { textBrush_->Release();     textBrush_ = nullptr; }
        if (shapeBrush_) { shapeBrush_->Release();    shapeBrush_ = nullptr; }
        if (textFormat_) { textFormat_->Release();    textFormat_ = nullptr; }
        if (dwriteFactory_) { dwriteFactory_->Release(); dwriteFactory_ = nullptr; }
        if (d2dContext_) { d2dContext_->Release();    d2dContext_ = nullptr; }
        if (d2dDevice_) { d2dDevice_->Release();     d2dDevice_ = nullptr; }
        if (d2dFactory_) { d2dFactory_->Release();    d2dFactory_ = nullptr; }
    }

    void Renderer::ReleaseSwapChainResources() {
        frameLatencyWaitable_ = nullptr;
        if (compVisual_) { compVisual_->Release(); compVisual_ = nullptr; }
        if (compTarget_) { compTarget_->Release(); compTarget_ = nullptr; }
        if (compDevice_) { compDevice_->Release(); compDevice_ = nullptr; }
        if (swapChain_) { swapChain_->Release();  swapChain_ = nullptr; }
        if (context_) { context_->Release();    context_ = nullptr; }
        if (device_) { device_->Release();     device_ = nullptr; }
    }

    void Renderer::Shutdown() {
        ReleaseResources();
        ReleaseSwapChainResources();
        drawing_ = false;
    }

}