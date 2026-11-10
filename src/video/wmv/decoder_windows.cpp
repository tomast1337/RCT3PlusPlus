// SPDX-License-Identifier: GPL-3.0-or-later
//
// The WMV3 decoder as Windows' own: the Media Foundation Windows Media Video decoder, used as a transform (MFT).
// It gives YV12 or NV12 pictures, which are copied out as separate Y, U and V planes. It may hold the last
// picture back until it is given an empty frame.
#include "video/wmv/decoder.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

// Lean, so dlgs.h stays out: it defines the macro rct3, which is our namespace.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <objbase.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

namespace rct3::video::wmv
{

    using Microsoft::WRL::ComPtr;

    struct Decoder::State
    {
        ComPtr<IMFTransform> mft;
        bool started = false;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool nv12 = false;
        std::size_t stride = 0; ///< Bytes a row of the decoder's Y plane
        std::array<std::vector<std::uint8_t>, 3> planes;
        std::array<std::size_t, 3> strides { };

        State()
        {
            // S_FALSE (already initialised) and RPC_E_CHANGED_MODE (another model) both leave COM usable.
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        }
        State(const State&) = delete;
        State& operator=(const State&) = delete;
        ~State()
        {
            mft.Reset();
            if (started)
                MFShutdown();
        }

        /// YV12 when the decoder offers it, else NV12; sets it and reads the stride it chose.
        bool choose_output()
        {
            ComPtr<IMFMediaType> nv;
            ComPtr<IMFMediaType> chosen;
            for (DWORD i = 0;; ++i)
            {
                ComPtr<IMFMediaType> t;
                if (FAILED(mft->GetOutputAvailableType(0, i, &t)))
                    break;
                GUID sub { };
                if (FAILED(t->GetGUID(MF_MT_SUBTYPE, &sub)))
                    continue;
                if (sub == MFVideoFormat_YV12 && !chosen)
                    chosen = t;
                else if (sub == MFVideoFormat_NV12 && !nv)
                    nv = t;
            }
            nv12 = !chosen;
            if (nv12)
                chosen = nv;
            if (!chosen || FAILED(mft->SetOutputType(0, chosen.Get(), 0)))
                return false;
            stride = width;
            UINT32 s = 0;
            if (SUCCEEDED(chosen->GetUINT32(MF_MT_DEFAULT_STRIDE, &s)) && static_cast<INT32>(s) > 0)
                stride = s;
            return true;
        }

        void begin()
        {
            mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
            mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        }

        /// The first picture the decoder has ready, or false.
        bool take_picture()
        {
            MFT_OUTPUT_STREAM_INFO info { };
            if (FAILED(mft->GetOutputStreamInfo(0, &info)))
                return false;
            const bool provides = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            for (;;)
            {
                MFT_OUTPUT_DATA_BUFFER out { };
                ComPtr<IMFSample> own;
                if (!provides)
                {
                    ComPtr<IMFMediaBuffer> buffer;
                    if (FAILED(MFCreateSample(&own)) || FAILED(MFCreateMemoryBuffer(info.cbSize, &buffer)) || FAILED(own->AddBuffer(buffer.Get())))
                        return false;
                    out.pSample = own.Get();
                }
                DWORD status = 0;
                const HRESULT hr = mft->ProcessOutput(0, 1, &out, &status);
                ComPtr<IMFSample> got;
                got.Attach(provides ? out.pSample : nullptr);
                if (out.pEvents)
                    out.pEvents->Release();
                if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
                {
                    if (!choose_output())
                        return false;
                    continue;
                }
                if (FAILED(hr))
                    return false;
                return copy_out(provides ? got.Get() : own.Get());
            }
        }

        /// Copies the picture in `sample` into planes. The decoder's rows may be padded below the picture
        /// (the buffer says by how much), so the chroma planes start where the buffer's length puts them.
        bool copy_out(IMFSample* sample)
        {
            ComPtr<IMFMediaBuffer> buffer;
            if (!sample || FAILED(sample->ConvertToContiguousBuffer(&buffer)))
                return false;
            BYTE* data = nullptr;
            DWORD length = 0;
            if (FAILED(buffer->Lock(&data, nullptr, &length)))
                return false;
            const std::size_t cw = (width + 1) / 2;
            const std::size_t ch = (height + 1) / 2;
            const std::size_t rows = std::max<std::size_t>(height, length * 2 / (3 * stride));
            const std::size_t luma = stride * rows;
            const std::size_t chroma_stride = nv12 ? stride : stride / 2;
            const std::size_t chroma_rows = rows / 2;
            bool ok = stride >= width && length >= luma + chroma_stride * chroma_rows && chroma_rows >= ch;
            if (ok)
            {
                strides = { stride, cw, cw };
                planes[0].assign(data, data + stride * height);
                planes[1].resize(cw * ch);
                planes[2].resize(cw * ch);
                if (nv12)
                {
                    const BYTE* uv = data + luma;
                    for (std::size_t y = 0; y < ch; ++y)
                    {
                        for (std::size_t x = 0; x < cw; ++x)
                        {
                            planes[1][y * cw + x] = uv[y * stride + 2 * x];
                            planes[2][y * cw + x] = uv[y * stride + 2 * x + 1];
                        }
                    }
                }
                else
                {
                    // YV12: V first, then U
                    const BYTE* v = data + luma;
                    const BYTE* u = v + chroma_stride * chroma_rows;
                    for (std::size_t y = 0; y < ch; ++y)
                    {
                        std::memcpy(&planes[2][y * cw], v + y * chroma_stride, cw);
                        std::memcpy(&planes[1][y * cw], u + y * chroma_stride, cw);
                    }
                }
            }
            buffer->Unlock();
            return ok;
        }
    };

    Decoder::Decoder(std::unique_ptr<State> state)
        : state_(std::move(state))
    {
    }
    Decoder::Decoder(Decoder&&) noexcept = default;
    Decoder& Decoder::operator=(Decoder&&) noexcept = default;
    Decoder::~Decoder() = default;

    std::optional<Decoder> Decoder::open(const Format& format)
    {
        if (format.width == 0 || format.height == 0)
            return std::nullopt;
        auto s = std::make_unique<State>();
        if (!s->started)
            return std::nullopt;
        s->width = format.width;
        s->height = format.height;

        if (FAILED(CoCreateInstance(CLSID_CWMVDecMediaObject, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s->mft))))
            return std::nullopt;

        ComPtr<IMFMediaType> in;
        if (FAILED(MFCreateMediaType(&in)))
            return std::nullopt;
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_WMV3);
        in->SetUINT64(MF_MT_FRAME_SIZE, (static_cast<UINT64>(format.width) << 32) | format.height);
        in->SetUINT64(MF_MT_FRAME_RATE, (static_cast<UINT64>(30) << 32) | 1);
        in->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, (static_cast<UINT64>(1) << 32) | 1);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (!format.extra.empty())
            in->SetBlob(MF_MT_USER_DATA, reinterpret_cast<const UINT8*>(format.extra.data()), static_cast<UINT32>(format.extra.size()));
        if (FAILED(s->mft->SetInputType(0, in.Get(), 0)) || !s->choose_output())
            return std::nullopt;
        s->begin();
        return Decoder(std::move(s));
    }

    std::optional<Picture> Decoder::decode(std::span<const std::byte> frame)
    {
        State& s = *state_;
        if (frame.empty())
        {
            s.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            s.mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        }
        else
        {
            ComPtr<IMFSample> sample;
            ComPtr<IMFMediaBuffer> buffer;
            BYTE* data = nullptr;
            if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(frame.size()), &buffer)) || FAILED(buffer->Lock(&data, nullptr, nullptr)))
                return std::nullopt;
            std::memcpy(data, frame.data(), frame.size());
            buffer->Unlock();
            buffer->SetCurrentLength(static_cast<DWORD>(frame.size()));
            sample->AddBuffer(buffer.Get());
            HRESULT hr = s.mft->ProcessInput(0, sample.Get(), 0);
            if (hr == MF_E_NOTACCEPTING)
            {
                s.take_picture(); // It holds a picture it wants taken first; that one is dropped
                hr = s.mft->ProcessInput(0, sample.Get(), 0);
            }
            if (FAILED(hr))
                return std::nullopt;
        }
        if (!s.take_picture())
            return std::nullopt;

        Picture out { .width = s.width, .height = s.height };
        for (std::size_t i = 0; i < 3; ++i)
        {
            out.strides[i] = s.strides[i];
            out.planes[i] = std::span<const std::uint8_t>(s.planes[i]);
        }
        return out;
    }

} // namespace rct3::video::wmv
