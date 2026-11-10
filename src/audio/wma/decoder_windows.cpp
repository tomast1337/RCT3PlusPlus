// SPDX-License-Identifier: GPL-3.0-or-later
//
// The WMA decoder as Windows' own: the Media Foundation Windows Media Audio decoder, used as a transform (MFT).
// It takes packets of block_align bytes and gives 16-bit or float samples, which it turns into float planes.
#include "audio/wma/decoder.hpp"

#include <algorithm>
#include <cstdint>
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

namespace rct3::audio::wma
{

    using Microsoft::WRL::ComPtr;

    namespace
    {

        // The 0x160 and 0x161 wave format tags, as the audio subtypes MFAudioFormat_MSAudio1 and
        // MFAudioFormat_WMAudioV8 name them.
        GUID subtype_of(int version)
        {
            GUID g = MFAudioFormat_Base;
            g.Data1 = version == 1 ? 0x160 : 0x161;
            return g;
        }

    } // namespace

    struct Decoder::State
    {
        ComPtr<IMFTransform> mft;
        bool started = false;
        std::uint16_t channels = 0;
        bool is_float = false;
        std::vector<std::vector<float>> planes;
        std::vector<const float*> plane_pointers;

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

        /// Picks float output when the decoder offers it, else 16-bit PCM, and sets it.
        bool choose_output()
        {
            ComPtr<IMFMediaType> pcm;
            for (DWORD i = 0;; ++i)
            {
                ComPtr<IMFMediaType> t;
                if (FAILED(mft->GetOutputAvailableType(0, i, &t)))
                    break;
                GUID sub { };
                UINT32 bits = 0;
                if (FAILED(t->GetGUID(MF_MT_SUBTYPE, &sub)))
                    continue;
                t->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
                if (sub == MFAudioFormat_Float && bits == 32)
                {
                    if (SUCCEEDED(mft->SetOutputType(0, t.Get(), 0)))
                    {
                        is_float = true;
                        return true;
                    }
                }
                else if (sub == MFAudioFormat_PCM && bits == 16 && !pcm)
                    pcm = t;
            }
            if (pcm && SUCCEEDED(mft->SetOutputType(0, pcm.Get(), 0)))
            {
                is_float = false;
                return true;
            }
            return false;
        }

        void begin()
        {
            mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
            mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        }

        /// Takes every sample the decoder has ready and appends it to `planes`.
        void collect()
        {
            MFT_OUTPUT_STREAM_INFO info { };
            if (FAILED(mft->GetOutputStreamInfo(0, &info)))
                return;
            const bool provides = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            for (;;)
            {
                MFT_OUTPUT_DATA_BUFFER out { };
                ComPtr<IMFSample> own;
                if (!provides)
                {
                    ComPtr<IMFMediaBuffer> buffer;
                    if (FAILED(MFCreateSample(&own)) || FAILED(MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, 4096), &buffer)) || FAILED(own->AddBuffer(buffer.Get())))
                        return;
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
                        return;
                    continue;
                }
                if (FAILED(hr))
                    return; // MF_E_TRANSFORM_NEED_MORE_INPUT, or an error: nothing more now
                append(provides ? got.Get() : own.Get());
            }
        }

        void append(IMFSample* sample)
        {
            ComPtr<IMFMediaBuffer> buffer;
            if (!sample || FAILED(sample->ConvertToContiguousBuffer(&buffer)))
                return;
            BYTE* data = nullptr;
            DWORD length = 0;
            if (FAILED(buffer->Lock(&data, nullptr, &length)))
                return;
            const std::size_t bytes = is_float ? 4 : 2;
            const std::size_t frames = length / (bytes * channels);
            for (std::size_t i = 0; i < frames; ++i)
            {
                for (std::size_t ch = 0; ch < channels; ++ch)
                {
                    const BYTE* at = data + (i * channels + ch) * bytes;
                    float v;
                    if (is_float)
                        std::memcpy(&v, at, 4);
                    else
                    {
                        std::int16_t s;
                        std::memcpy(&s, at, 2);
                        v = static_cast<float>(s) / 32768.0f;
                    }
                    planes[ch].push_back(v);
                }
            }
            buffer->Unlock();
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
        if (format.channels == 0 || format.version < 1 || format.version > 2)
            return std::nullopt;
        auto s = std::make_unique<State>();
        if (!s->started)
            return std::nullopt;
        s->channels = format.channels;
        s->planes.resize(format.channels);
        s->plane_pointers.resize(format.channels);

        if (FAILED(CoCreateInstance(CLSID_CWMADecMediaObject, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s->mft))))
            return std::nullopt;

        // The input type from the wave format with its codec bytes after it, as the file holds it.
        std::vector<std::byte> wave(sizeof(WAVEFORMATEX) + format.extra.size());
        WAVEFORMATEX head { };
        head.wFormatTag = format.version == 1 ? 0x160 : 0x161;
        head.nChannels = format.channels;
        head.nSamplesPerSec = format.sample_rate;
        head.nAvgBytesPerSec = format.bit_rate / 8;
        head.nBlockAlign = format.block_align;
        head.wBitsPerSample = 16;
        head.cbSize = static_cast<WORD>(format.extra.size());
        std::memcpy(wave.data(), &head, sizeof head);
        if (!format.extra.empty())
            std::memcpy(wave.data() + sizeof head, format.extra.data(), format.extra.size());

        ComPtr<IMFMediaType> in;
        if (FAILED(MFCreateMediaType(&in)) || FAILED(MFInitMediaTypeFromWaveFormatEx(in.Get(), reinterpret_cast<const WAVEFORMATEX*>(wave.data()), static_cast<UINT32>(wave.size()))))
            return std::nullopt;
        in->SetGUID(MF_MT_SUBTYPE, subtype_of(format.version));
        if (FAILED(s->mft->SetInputType(0, in.Get(), 0)) || !s->choose_output())
            return std::nullopt;
        s->begin();
        return Decoder(std::move(s));
    }

    int Decoder::decode(std::span<const std::byte> packet, const float* const*& planes)
    {
        State& s = *state_;
        for (auto& p : s.planes)
            p.clear();

        if (packet.empty())
        {
            s.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            s.mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        }
        else
        {
            ComPtr<IMFSample> sample;
            ComPtr<IMFMediaBuffer> buffer;
            BYTE* data = nullptr;
            if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(packet.size()), &buffer)) || FAILED(buffer->Lock(&data, nullptr, nullptr)))
                return 0;
            std::memcpy(data, packet.data(), packet.size());
            buffer->Unlock();
            buffer->SetCurrentLength(static_cast<DWORD>(packet.size()));
            sample->AddBuffer(buffer.Get());
            HRESULT hr = s.mft->ProcessInput(0, sample.Get(), 0);
            if (hr == MF_E_NOTACCEPTING)
            {
                s.collect(); // It holds output it wants taken first
                hr = s.mft->ProcessInput(0, sample.Get(), 0);
            }
            if (FAILED(hr))
                return 0;
        }
        s.collect();

        const int n = static_cast<int>(s.planes[0].size());
        for (std::size_t ch = 0; ch < s.planes.size(); ++ch)
            s.plane_pointers[ch] = s.planes[ch].data();
        planes = s.plane_pointers.data();
        return n;
    }

    int Decoder::delay() const
    {
        return 0;
    }

    void Decoder::reset()
    {
        State& s = *state_;
        s.mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        s.begin();
        for (auto& p : s.planes)
            p.clear();
    }

} // namespace rct3::audio::wma
