#pragma once
#include "resource.h"
#include <winhttp.h>

#include <libloaderapi.h>
#include <string>

std::wstring GetExeFolder()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    auto pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : path.substr(0, pos);
}

bool CreateStreamOnHGlobalFromData(const std::vector<BYTE>& data, IStream** ppStream)
{
    *ppStream = nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!hMem) return false;
    void* p = GlobalLock(hMem);
    memcpy(p, data.data(), data.size());
    GlobalUnlock(hMem);
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, ppStream)))
    {
        GlobalFree(hMem);
        return false;
    }
    return true;
}

// Convert WIC stream to Direct2D bitmap
HRESULT LoadBitmapFromStream(IWICImagingFactory* pWIC, ID2D1RenderTarget* pRT, IStream* pStream, ID2D1Bitmap** ppBitmap)
{
    *ppBitmap = nullptr;
    IWICBitmapDecoder* pDecoder = nullptr;
    HRESULT hr = pWIC->CreateDecoderFromStream(pStream, nullptr,
        WICDecodeMetadataCacheOnLoad, &pDecoder);
    if (FAILED(hr)) return hr;

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (SUCCEEDED(hr))
    {
        IWICFormatConverter* pConverter = nullptr;
        hr = pWIC->CreateFormatConverter(&pConverter);
        if (SUCCEEDED(hr))
        {
            hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeMedianCut);
            
            if (SUCCEEDED(hr))
                hr = pRT->CreateBitmapFromWicBitmap(pConverter, nullptr, ppBitmap);

            pConverter->Release();
        }
        pFrame->Release();
    }
    pDecoder->Release();
    return hr;
}

// Download image into memory from CDN based on g_nNumber
bool DownloadImage(std::vector<BYTE>& data, int ID)
{
    std::wstring host = L"cdn.garythe.cat";
    std::wstring path = L"/Gary/Gary" + std::to_wstring(ID) + L".jpg";

    HINTERNET hSession = WinHttpOpen(L"Win32CatViewer", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    bool ok = false;
    if (hRequest && WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr))
    {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail)
        {
            std::vector<BYTE> buf(avail);
            DWORD read = 0;
            WinHttpReadData(hRequest, buf.data(), avail, &read);
            if (!read) break;
            data.insert(data.end(), buf.begin(), buf.begin() + read);
            ok = true;
        }
    }
    if (hRequest) WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

