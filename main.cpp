#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wininet.h>
#include <vector>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "windowscodecs")
#pragma comment(lib, "wininet")

ID2D1Factory* pD2DFactory = nullptr;
ID2D1HwndRenderTarget* pRenderTarget = nullptr;
ID2D1Bitmap* pBitmap = nullptr;
IWICImagingFactory* pWICFactory = nullptr;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

bool DownloadImageFromURL(const wchar_t* url, std::vector<BYTE>& outData) {
    HINTERNET hInternet = InternetOpen(L"Direct2DViewer", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrl(hInternet, url, nullptr, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

    BYTE buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead) {
        outData.insert(outData.end(), buffer, buffer + bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return true;
}

bool LoadImageFromMemory(const std::vector<BYTE>& data, ID2D1RenderTarget* target, ID2D1Bitmap** outBitmap) {
    IWICStream* pStream = nullptr;
    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pFrame = nullptr;
    IWICFormatConverter* pConverter = nullptr;
    IWICBitmapSource* pSource = nullptr;

    if (FAILED(pWICFactory->CreateStream(&pStream))) return false;
    if (FAILED(pStream->InitializeFromMemory((BYTE*)data.data(), data.size()))) goto cleanup;
    if (FAILED(pWICFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnLoad, &pDecoder))) goto cleanup;
    if (FAILED(pDecoder->GetFrame(0, &pFrame))) goto cleanup;
    if (FAILED(pWICFactory->CreateFormatConverter(&pConverter))) goto cleanup;

    if (FAILED(pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) goto cleanup;

    pSource = pConverter;

    if (FAILED(target->CreateBitmapFromWicBitmap(pSource, nullptr, outBitmap))) goto cleanup;

    pStream->Release();
    pDecoder->Release();
    pFrame->Release();
    pConverter->Release();
    return true;

cleanup:
    if (pStream) pStream->Release();
    if (pDecoder) pDecoder->Release();
    if (pFrame) pFrame->Release();
    if (pConverter) pConverter->Release();
    return false;
}

void CreateRenderTarget(HWND hwnd) {
    if (pRenderTarget) return;

    RECT rc;
    GetClientRect(hwnd, &rc);

    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(hwnd, size);

    pD2DFactory->CreateHwndRenderTarget(rtProps, hwndProps, &pRenderTarget);
}

void OnPaint(HWND hwnd) {
    CreateRenderTarget(hwnd);

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));

    if (pBitmap) {
        D2D1_SIZE_F wndSize = pRenderTarget->GetSize();
        D2D1_RECT_F destRect = D2D1::RectF(0, 0, wndSize.width, wndSize.height);
        pRenderTarget->DrawBitmap(pBitmap, &destRect);
    }

    pRenderTarget->EndDraw();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Init Direct2D + WIC
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
    CoInitialize(NULL);
    CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pWICFactory));

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"Direct2DViewer";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(wc.lpszClassName, L"Direct2D Image Viewer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);

    // Download + Load Image
    std::vector<BYTE> imageData;
    if (DownloadImageFromURL(L"https://api.garythe.cat/gary/image", imageData)) {
        CreateRenderTarget(hwnd);
        LoadImageFromMemory(imageData, pRenderTarget, &pBitmap);
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (pBitmap) pBitmap->Release();
    if (pRenderTarget) pRenderTarget->Release();
    if (pD2DFactory) pD2DFactory->Release();
    if (pWICFactory) pWICFactory->Release();
    CoUninitialize();
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
        OnPaint(hwnd);
        ValidateRect(hwnd, nullptr);
        return 0;
    
    case WM_SIZE:
        if (pRenderTarget) {
            D2D1_SIZE_U size = D2D1::SizeU(LOWORD(lParam), HIWORD(lParam));
            pRenderTarget->Resize(size);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
