#include <windows.h>
#include <wincodec.h>

#include <d2d1.h>
#include <gdiplus.h>

#include <vector>
#include <string>
#include <fstream>

#include "helpers.hpp"
#include "resource.h"


// Globals
bool                            g_bOffline = false;
HWND                            g_hWnd;
ID2D1Factory*                   g_pD2DFactory = nullptr;
IWICImagingFactory*             g_pWICFactory = nullptr;
ID2D1HwndRenderTarget*          g_pRenderTarget = nullptr;
ID2D1Bitmap*                    g_pD2DBitmap = nullptr;
Gdiplus::Image*                 g_pGdiImage = nullptr;
bool                            g_bUseDirect2D = true;
int                             g_nNumber = 1;

// Forward declarations
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
HRESULT             InitDirect2D(HWND hwnd);
void                LoadCatImage();
INT_PTR CALLBACK    NumberDlgProc(HWND, UINT, WPARAM, LPARAM);
void                PromptForNumber(HWND hwnd);


void Deinitialize(ULONG_PTR gdiplusToken)
{
    if (g_pD2DBitmap) { g_pD2DBitmap->Release(); g_pD2DBitmap = nullptr; }
    if (g_pRenderTarget) { g_pRenderTarget->Release(); g_pRenderTarget = nullptr; }
    if (g_pD2DFactory) g_pD2DFactory->Release();
    if (g_pWICFactory) g_pWICFactory->Release();
    if (g_pGdiImage) delete g_pGdiImage;
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
}

// Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    SYSTEMTIME time;
    GetSystemTime(&time);
    srand(time.wMilliseconds * time.wSecond * time.wHour);
    
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Gdiplus::GdiplusStartupInput gdiplusInput; ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"Win32CatViewer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    RegisterClassW(&wc);

    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"Gary Viewer - #1",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd) return 0;


    SetMenu(g_hWnd,LoadMenuW(hInstance, MAKEINTRESOURCE(IDR_MAINMENU)));
    

    ShowWindow(g_hWnd, nCmdShow);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Deinitialize(gdiplusToken);
    return 0;
}



void LoadCatImage()
{
    // 1) Read image data (either offline from disk or online via HTTP)
    std::vector<BYTE> data;
    if (g_bOffline)
    {
        // Build local path: "<exeFolder>\Gary\Gary<num>.jpg"
        std::wstring path = GetExeFolder() + L"\\Gary\\Gary" + std::to_wstring(g_nNumber) + L".jpg";

        std::ifstream file(path, std::ios::binary);
        if (file)
            data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        else if (!DownloadImage(data,g_nNumber))
            return; // fallback for no file
    }
    else
    {
        if (!DownloadImage(data,g_nNumber))
            return;
    }

    // 2) Clean up any previously loaded images/bitmaps
    if (g_pGdiImage)
        delete g_pGdiImage; g_pGdiImage = nullptr;

    if (g_pD2DBitmap)
        g_pD2DBitmap->Release(); g_pD2DBitmap = nullptr;

    // 3) Create two separate IStream instances from the same data buffer
    IStream* stmGdi = nullptr;
    IStream* stmD2D = nullptr;
    CreateStreamOnHGlobalFromData(data, &stmGdi);
    CreateStreamOnHGlobalFromData(data, &stmD2D);

    // 4) Load into GDI+
    if (stmGdi)
        g_pGdiImage = Gdiplus::Image::FromStream(stmGdi); stmGdi->Release();


    // 5) Load into Direct2D (if initialized)
    if (stmD2D && g_pRenderTarget && g_pWICFactory)
        LoadBitmapFromStream(g_pWICFactory, g_pRenderTarget, stmD2D, &g_pD2DBitmap); stmD2D->Release();


    // 6) Update the window title and force a repaint
    wchar_t title[64];
    swprintf_s(title, L"Gary Viewer - #%i", g_nNumber);
    SetWindowTextW(g_hWnd, title);
    InvalidateRect(g_hWnd, nullptr, TRUE);
}


// Initialize Direct2D & WIC
HRESULT InitDirect2D(HWND hwnd)
{
    HRESULT hr;
    if (!g_pD2DFactory && FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory)))
        return hr;
    if (!g_pWICFactory && FAILED(hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pWICFactory))))
        return hr;
    if (!g_pRenderTarget)
    {
        RECT rc; GetClientRect(hwnd, &rc);
        hr = g_pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)),
            &g_pRenderTarget);
    }
    return S_OK;
}

// Main Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        InitDirect2D(hwnd);
        LoadCatImage();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_MODE_D2D:
        {
            g_bUseDirect2D = true;
            CheckMenuRadioItem(GetMenu(hwnd), IDM_MODE_D2D, IDM_MODE_GDIP,
                IDM_MODE_D2D, MF_BYCOMMAND);
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDM_MODE_GDIP:
        {
            g_bUseDirect2D = false;
            CheckMenuRadioItem(GetMenu(hwnd), IDM_MODE_D2D, IDM_MODE_GDIP,
                IDM_MODE_GDIP, MF_BYCOMMAND);
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        
        case IDM_REFRESH:
        {
            g_nNumber = 1 + (rand() % 640);
            LoadCatImage(); 
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDM_SET_NUMBER:
        {
            PromptForNumber(hwnd);
            break;
        }

        case IDM_MODE_OFFLINE:
        {
            g_bOffline = !g_bOffline;
            CheckMenuItem(GetMenu(hwnd), IDM_MODE_OFFLINE, g_bOffline ? MF_CHECKED : MF_UNCHECKED);
            LoadCatImage();
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDM_ABOUT:
        {
            DialogBoxW(GetModuleHandle(nullptr),
                MAKEINTRESOURCE(IDD_ABOUTBOX),
                hwnd,
                [](HWND hDlg, UINT msg, WPARAM wParam, LPARAM) -> INT_PTR {
                    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK) {
                        EndDialog(hDlg, IDOK);
                        return TRUE;
                    }
                    return FALSE;
                });
        }

        }
        return 0;

    case WM_SIZE:
        if (g_pRenderTarget)
        {
            RECT rc; GetClientRect(hwnd, &rc);
            g_pRenderTarget->Resize(D2D1::SizeU(rc.right, rc.bottom));
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        
        if (g_bUseDirect2D && g_pRenderTarget && g_pD2DBitmap)
        {
            g_pRenderTarget->BeginDraw();
            g_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));
            D2D1_SIZE_F sz = g_pRenderTarget->GetSize();
            g_pRenderTarget->DrawBitmap(g_pD2DBitmap,
                D2D1::RectF(0, 0, sz.width, sz.height));
            g_pRenderTarget->EndDraw();
        }
        else if (!g_bUseDirect2D && g_pGdiImage)
        {
            Gdiplus::Graphics gr(hdc);
            gr.Clear(Gdiplus::Color::White);
            gr.DrawImage(g_pGdiImage, 0, 0, rc.right, rc.bottom);
        }
        EndPaint(hwnd, &ps);
    }
    return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}


// Dialog procedure for number input
INT_PTR CALLBACK NumberDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        // lParam contains parent HWND
        SetWindowLongPtr(dlg, GWLP_USERDATA, lParam);
        // Initialize edit with current number
        SetDlgItemInt(dlg, IDC_EDIT_NUMBER, g_nNumber, FALSE);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            BOOL ok;
            int val = GetDlgItemInt(dlg, IDC_EDIT_NUMBER, &ok, FALSE);
            if (ok)
            {
                g_nNumber = val;
                LoadCatImage();
                InvalidateRect(g_hWnd, nullptr, TRUE);
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Prompt via modal dialog
void PromptForNumber(HWND hwnd)
{
    // Launch number-entry dialog, passing hwnd to update the title
    DialogBoxParam(
        GetModuleHandle(nullptr),
        MAKEINTRESOURCE(IDD_NUMBER_DIALOG),
        hwnd,
        NumberDlgProc,
        (LPARAM)hwnd
    );
}
