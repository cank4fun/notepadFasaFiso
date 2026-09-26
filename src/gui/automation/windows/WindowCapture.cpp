#include "WindowCapture.hpp"

#if defined(_WIN32)

#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#include <ocidl.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace nff::gui::automation::windows {
namespace {

template <typename T>
void releaseCom(T*& value) noexcept {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

}

std::expected<void, DriverError>
captureWindowPng(const HWND window, const std::filesystem::path& outputPath) {
    if (window == nullptr || ::IsWindow(window) == FALSE) {
        return std::unexpected(DriverError::WindowUnavailable);
    }

    RECT rect{};
    if (::GetWindowRect(window, &rect) == FALSE) {
        return std::unexpected(DriverError::CaptureFailed);
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return std::unexpected(DriverError::CaptureFailed);
    }
    if (width > static_cast<int>(std::numeric_limits<UINT>::max() / 4U) ||
        static_cast<std::uint64_t>(width) * 4ULL * static_cast<std::uint64_t>(height) >
            std::numeric_limits<UINT>::max()) {
        return std::unexpected(DriverError::CaptureFailed);
    }

    HDC windowDc = ::GetWindowDC(window);
    if (windowDc == nullptr) return std::unexpected(DriverError::CaptureFailed);
    HDC memoryDc = ::CreateCompatibleDC(windowDc);
    if (memoryDc == nullptr) {
        ::ReleaseDC(window, windowDc);
        return std::unexpected(DriverError::CaptureFailed);
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1U;
    info.bmiHeader.biBitCount = 32U;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(windowDc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0U);
    if (bitmap == nullptr || pixels == nullptr) {
        if (bitmap != nullptr) ::DeleteObject(bitmap);
        ::DeleteDC(memoryDc);
        ::ReleaseDC(window, windowDc);
        return std::unexpected(DriverError::CaptureFailed);
    }
    HGDIOBJ oldObject = ::SelectObject(memoryDc, bitmap);
    static_cast<void>(::PatBlt(memoryDc, 0, 0, width, height, BLACKNESS));

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
    BOOL printed = ::PrintWindow(window, memoryDc, PW_RENDERFULLCONTENT);
    if (printed == FALSE) {
        printed = ::PrintWindow(window, memoryDc, 0U);
    }
    if (printed != FALSE) {
        const auto strideBytes = static_cast<std::size_t>(width) * 4U;
        const auto totalBytes = strideBytes * static_cast<std::size_t>(height);
        auto* bytes = static_cast<unsigned char*>(pixels);
        for (std::size_t index = 3U; index < totalBytes; index += 4U) {
            bytes[index] = 0xFFU;
        }
    }

    std::expected<void, DriverError> result{};
    if (printed == FALSE) {
        result = std::unexpected(DriverError::CaptureFailed);
    } else {
        const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitialize = SUCCEEDED(initialized);
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
            result = std::unexpected(DriverError::CaptureFailed);
        } else {
            IWICImagingFactory* factory = nullptr;
            IWICStream* stream = nullptr;
            IWICBitmapEncoder* encoder = nullptr;
            IWICBitmapFrameEncode* frame = nullptr;
            IPropertyBag2* properties = nullptr;

            HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                            CLSCTX_INPROC_SERVER,
                                            IID_PPV_ARGS(&factory));
            if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
            if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE);
            if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
            if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
            if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &properties);
            if (SUCCEEDED(hr)) hr = frame->Initialize(properties);
            if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
            WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
            if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&pixelFormat);
            if (SUCCEEDED(hr) && pixelFormat != GUID_WICPixelFormat32bppBGRA) {
                hr = E_FAIL;
            }
            const auto stride = static_cast<UINT>(width) * 4U;
            const auto imageBytes = stride * static_cast<UINT>(height);
            if (SUCCEEDED(hr)) {
                hr = frame->WritePixels(static_cast<UINT>(height), stride, imageBytes,
                                        static_cast<BYTE*>(pixels));
            }
            if (SUCCEEDED(hr)) hr = frame->Commit();
            if (SUCCEEDED(hr)) hr = encoder->Commit();

            releaseCom(properties);
            releaseCom(frame);
            releaseCom(encoder);
            releaseCom(stream);
            releaseCom(factory);
            if (uninitialize) ::CoUninitialize();

            if (FAILED(hr)) {
                result = std::unexpected(DriverError::CaptureFailed);
            }
        }
    }

    static_cast<void>(::SelectObject(memoryDc, oldObject));
    ::DeleteObject(bitmap);
    ::DeleteDC(memoryDc);
    ::ReleaseDC(window, windowDc);
    return result;
}

}

#endif
