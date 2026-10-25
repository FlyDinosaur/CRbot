#include "png_io.h"

#include <windows.h>
#include <wincodec.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

template <typename T>
void safe_release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// CoCreateInstance 要求当前线程先初始化 COM，否则直接 CO_E_NOTINITIALIZED。
// 这个模块可能被任意线程调用（主线程、BotRunner 线程、离线工具），
// 所以按线程初始化一次，不配对 CoUninitialize（进程生命周期内一直用）。
IWICImagingFactory* wic_factory(std::string* err) {
    static thread_local bool com_ready = false;
    if (!com_ready) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        com_ready = true;  // 返回 RPC_E_CHANGED_MODE 也照样能用 WIC
    }

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 __uuidof(IWICImagingFactory),
                                 reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        if (err) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "WIC factory unavailable (hr=0x%08lX)",
                          static_cast<unsigned long>(hr));
            *err = buf;
        }
        return nullptr;
    }
    return factory;
}

// 把解码器解出来的第一帧转成 RGBA
bool decode_first_frame(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, Image& out,
                        std::string* err) {
    auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };

    IWICBitmapFrameDecode* frame = nullptr;
    HRESULT hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) return fail("cannot read frame");

    IWICFormatConverter* conv = nullptr;
    hr = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr) && conv) {
        hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                              nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    if (FAILED(hr) || !conv) {
        safe_release(frame);
        safe_release(conv);
        return fail("cannot convert to RGBA");
    }

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
    hr = conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(buf.size()), buf.data());

    safe_release(conv);
    safe_release(frame);

    if (FAILED(hr)) return fail("CopyPixels failed");

    out = Image(static_cast<int>(w), static_cast<int>(h), std::move(buf));
    return !out.empty();
}

}  // namespace

bool load_png(const std::filesystem::path& path, Image& out, std::string* err) {
    IWICImagingFactory* factory = wic_factory(err);
    if (!factory) return false;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) {
        safe_release(factory);
        if (err) *err = "cannot open png";
        return false;
    }

    bool ok = decode_first_frame(factory, decoder, out, err);
    safe_release(decoder);
    safe_release(factory);
    return ok;
}

bool load_png_memory(const std::vector<uint8_t>& png, Image& out, std::string* err) {
    if (png.empty()) {
        if (err) *err = "empty png";
        return false;
    }

    IWICImagingFactory* factory = wic_factory(err);
    if (!factory) return false;

    IWICStream* stream = nullptr;
    HRESULT hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream) {
        safe_release(factory);
        if (err) *err = "cannot create WIC stream";
        return false;
    }

    // InitializeFromMemory 不拷贝，需要可写指针且缓冲区在解码期间有效
    std::vector<uint8_t> copy = png;
    hr = stream->InitializeFromMemory(copy.data(), static_cast<DWORD>(copy.size()));
    if (FAILED(hr)) {
        safe_release(stream);
        safe_release(factory);
        if (err) *err = "cannot init WIC stream";
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                          &decoder);
    if (FAILED(hr) || !decoder) {
        safe_release(stream);
        safe_release(factory);
        if (err) *err = "cannot decode png from memory";
        return false;
    }

    bool ok = decode_first_frame(factory, decoder, out, err);

    safe_release(decoder);
    safe_release(stream);
    safe_release(factory);
    return ok;
}
