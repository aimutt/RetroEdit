#include "Beep.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <cstdint>
#include <vector>

// Replaces an earlier `Beep()` + `std::thread` implementation that had
// noticeable onset latency. The waveform is pre-generated once into a tiny
// in-memory RIFF/WAV buffer, then played via PlaySound(SND_MEMORY|SND_ASYNC),
// which returns immediately and lets the Windows audio mixer schedule the
// playback. No per-call thread spawn, no Beep() cold-start.

namespace
{
    constexpr uint32_t kSampleRate = 22050;
    constexpr uint32_t kFreqHz     = 1760;   // A6 — high-pitched alert tone
    constexpr uint32_t kPulseMs    = 60;
    constexpr uint32_t kGapMs      = 50;
    constexpr uint8_t  kHighAmp    = 192;    // 8-bit unsigned PCM (centre = 128)
    constexpr uint8_t  kLowAmp     = 64;
    constexpr uint8_t  kSilence    = 128;

    std::vector<unsigned char> g_buffer;
    bool g_built = false;

    void AppendU16LE(std::vector<unsigned char>& v, uint16_t x)
    {
        v.push_back(static_cast<unsigned char>( x        & 0xff));
        v.push_back(static_cast<unsigned char>((x >> 8)  & 0xff));
    }

    void AppendU32LE(std::vector<unsigned char>& v, uint32_t x)
    {
        v.push_back(static_cast<unsigned char>( x        & 0xff));
        v.push_back(static_cast<unsigned char>((x >> 8)  & 0xff));
        v.push_back(static_cast<unsigned char>((x >> 16) & 0xff));
        v.push_back(static_cast<unsigned char>((x >> 24) & 0xff));
    }

    void AppendStr(std::vector<unsigned char>& v, const char* s)
    {
        for (; *s; ++s) v.push_back(static_cast<unsigned char>(*s));
    }

    void AppendPulse(std::vector<unsigned char>& v, uint32_t samples)
    {
        // Square wave: flip amplitude every halfPeriod samples.
        uint32_t halfPeriod = kSampleRate / (2 * kFreqHz);
        if (halfPeriod == 0) halfPeriod = 1;
        bool     high = true;
        uint32_t togglesIn = 0;
        for (uint32_t i = 0; i < samples; ++i)
        {
            v.push_back(high ? kHighAmp : kLowAmp);
            if (++togglesIn >= halfPeriod)
            {
                high = !high;
                togglesIn = 0;
            }
        }
    }

    void AppendSilence(std::vector<unsigned char>& v, uint32_t samples)
    {
        v.insert(v.end(), samples, kSilence);
    }

    void BuildBuffer()
    {
        if (g_built) return;

        const uint32_t pulseSamples = (kSampleRate * kPulseMs) / 1000;
        const uint32_t gapSamples   = (kSampleRate * kGapMs)   / 1000;
        const uint32_t dataSize     = pulseSamples * 2 + gapSamples;

        g_buffer.clear();
        g_buffer.reserve(44 + dataSize);

        // RIFF header
        AppendStr   (g_buffer, "RIFF");
        AppendU32LE (g_buffer, 36 + dataSize);     // file size minus 8
        AppendStr   (g_buffer, "WAVE");

        // fmt chunk (PCM)
        AppendStr   (g_buffer, "fmt ");
        AppendU32LE (g_buffer, 16);                // fmt chunk size
        AppendU16LE (g_buffer, 1);                 // audio format = PCM
        AppendU16LE (g_buffer, 1);                 // channels = mono
        AppendU32LE (g_buffer, kSampleRate);       // sample rate
        AppendU32LE (g_buffer, kSampleRate * 1);   // byte rate (8-bit mono)
        AppendU16LE (g_buffer, 1);                 // block align
        AppendU16LE (g_buffer, 8);                 // bits per sample

        // data chunk
        AppendStr   (g_buffer, "data");
        AppendU32LE (g_buffer, dataSize);
        AppendPulse  (g_buffer, pulseSamples);
        AppendSilence(g_buffer, gapSamples);
        AppendPulse  (g_buffer, pulseSamples);

        g_built = true;
    }
}

void PlayBeep()
{
    BuildBuffer();
    PlaySoundA(reinterpret_cast<const char*>(g_buffer.data()),
               nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
