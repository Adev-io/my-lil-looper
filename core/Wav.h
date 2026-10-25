// Wav.h: float32 WAV files for the saved loops (write stereo, read mono or stereo) and a linear resampler.
// Little-endian hosts only (x86, ARM). No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wav
{
// Streams stereo frames into a float32 WAV; close() fills in the sizes.
struct Writer
{
    bool open (const std::string& path, int sampleRate)
    {
        f = std::fopen (path.c_str(), "wb");
        if (! f) return false;
        frames = 0;
        const uint32_t sr = (uint32_t) sampleRate, byteRate = sr * 8, zero = 0;
        const uint16_t fmt = 3, ch = 2, align = 8, bits = 32;
        const uint32_t fmtLen = 16;
        std::fwrite ("RIFF", 1, 4, f); std::fwrite (&zero, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
        std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
        std::fwrite (&sr, 4, 1, f); std::fwrite (&byteRate, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
        std::fwrite ("data", 1, 4, f); std::fwrite (&zero, 4, 1, f);
        return true;
    }
    void write (const float* l, const float* r, int n)
    {
        float buf[2048];
        for (int i = 0; i < n;)
        {
            int k = 0;
            for (; k < 1024 && i < n; ++k, ++i) { buf[2 * k] = l[i]; buf[2 * k + 1] = r[i]; }
            std::fwrite (buf, sizeof (float), 2 * k, f);
        }
        frames += n;
    }
    bool close()
    {
        const uint32_t data = (uint32_t) (frames * 8), riff = data + 36;
        std::fseek (f, 4, SEEK_SET); std::fwrite (&riff, 4, 1, f);
        std::fseek (f, 40, SEEK_SET); std::fwrite (&data, 4, 1, f);
        const bool ok = std::ferror (f) == 0;
        return std::fclose (f) == 0 && ok;
    }

private:
    std::FILE* f = nullptr;
    int64_t frames = 0;
};

// Reads a float32 mono or stereo WAV. Mono fills both channels. False for anything else.
inline bool read (const std::string& path, std::vector<float>& l, std::vector<float>& r, int& sampleRate)
{
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    char id[4];
    uint32_t len = 0;
    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t sr = 0;
    bool ok = std::fread (id, 1, 4, f) == 4 && std::memcmp (id, "RIFF", 4) == 0 && std::fread (&len, 4, 1, f) == 1
              && std::fread (id, 1, 4, f) == 4 && std::memcmp (id, "WAVE", 4) == 0;
    while (ok && std::fread (id, 1, 4, f) == 4 && std::fread (&len, 4, 1, f) == 1)
    {
        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            uint8_t b[16];
            if (len < 16 || std::fread (b, 1, 16, f) != 16) { ok = false; break; }
            std::memcpy (&fmt, b, 2); std::memcpy (&ch, b + 2, 2); std::memcpy (&sr, b + 4, 4); std::memcpy (&bits, b + 14, 2);
            std::fseek (f, (long) (len - 16 + (len & 1)), SEEK_CUR);
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            if (fmt != 3 || bits != 32 || (ch != 1 && ch != 2) || sr == 0) { ok = false; break; }
            const size_t n = len / (4u * ch);
            std::vector<float> buf (n * ch);
            if (std::fread (buf.data(), sizeof (float), buf.size(), f) != buf.size()) { ok = false; break; }
            l.resize (n); r.resize (n);
            for (size_t i = 0; i < n; ++i) { l[i] = buf[i * ch]; r[i] = buf[i * ch + ch - 1]; }
            sampleRate = (int) sr;
            std::fclose (f);
            return true;
        }
        else std::fseek (f, (long) (len + (len & 1)), SEEK_CUR);
    }
    std::fclose (f);
    return false;
}

// Linear resample of one channel from rate `from` to rate `to`.
inline std::vector<float> resample (const std::vector<float>& x, int from, int to)
{
    if (from == to || x.empty()) return x;
    const size_t n = (size_t) std::llround ((double) x.size() * to / from);
    std::vector<float> y (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i * from / to;
        const size_t a = std::min ((size_t) t, x.size() - 1), b = std::min (a + 1, x.size() - 1);
        const float k = (float) (t - (double) a);
        y[i] = x[a] + k * (x[b] - x[a]);
    }
    return y;
}
}
