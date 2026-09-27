#pragma once
#include "Common.h"

namespace Nullframe {

    class Renderer {
    public:
        bool Initialize(HWND hwnd, int width, int height);
        void Shutdown();
        bool RecoverDevice();
        void Begin();
        void Draw(const FrameStats& stats);
        void End();

        bool   IsDeviceLost() const noexcept { return deviceLost_; }
        bool   IsOccluded()   const noexcept { return occluded_; }
        HANDLE FrameLatencyHandle() const noexcept { return frameLatencyWaitable_; }

    private:
        bool CreateDevice();
        bool CreateSwapChain();
        bool CreateComposition();
        bool CreateResources();
        bool CreateTargetBitmap();
        void DiscardTargetBitmap();
        void ReleaseResources();
        void ReleaseSwapChainResources();

        HWND hwnd_ = nullptr;
        int  width_ = 0;
        int  height_ = 0;

        ID3D11Device* device_ = nullptr;
        ID3D11DeviceContext* context_ = nullptr;
        IDXGISwapChain1* swapChain_ = nullptr;

        IDCompositionDevice* compDevice_ = nullptr;
        IDCompositionTarget* compTarget_ = nullptr;
        IDCompositionVisual* compVisual_ = nullptr;

        ID2D1Factory1* d2dFactory_ = nullptr;
        ID2D1Device* d2dDevice_ = nullptr;
        ID2D1DeviceContext* d2dContext_ = nullptr;

        IDWriteFactory* dwriteFactory_ = nullptr;
        IDWriteTextFormat* textFormat_ = nullptr;

        ID2D1SolidColorBrush* borderBrush_ = nullptr;
        ID2D1SolidColorBrush* textBrush_ = nullptr;
        ID2D1SolidColorBrush* shapeBrush_ = nullptr;
        ID2D1SolidColorBrush* cursorBrush_ = nullptr;

        ID2D1Bitmap1* targetBitmap_ = nullptr;

        HANDLE frameLatencyWaitable_ = nullptr;
        bool   drawing_ = false;
        bool   deviceLost_ = false;
        bool   occluded_ = false;
    };

}