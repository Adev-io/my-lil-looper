// Wav.h: float32 WAV files for the saved loops (write stereo, read mono or stereo) and a linear resampler.
// Little-endian hosts only (x86, ARM). No Rack dependency.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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
    bool close()   // false when not open (a second close is a no-op)
    {
        if (! f) return false;
        const uint32_t data = (uint32_t) (frames * 8), riff = data + 36;
        std::fseek (f, 4, SEEK_SET); std::fwrite (&riff, 4, 1, f);
        std::fseek (f, 40, SEEK_SET); std::fwrite (&data, 4, 1, f);
        const bool ok = std::ferror (f) == 0;
        const bool closed = std::fclose (f) == 0;
        f = nullptr;
        return closed && ok;
    }
    ~Writer() { close(); }

private:
    std::FILE* f = nullptr;
    int64_t frames = 0;
};

// Reads a float32 mono or stereo WAV (it may come from a shared patch: every size is checked against the file).
// Mono fills both channels. False for anything else. Keeps at most maxFrames frames; with toRate, at most what resamples
// to maxFrames frames at toRate, plus one.
inline bool read (const std::string& path, std::vector<float>& l, std::vector<float>& r, int& sampleRate,
                  int64_t maxFrames = INT64_MAX, int toRate = 0)
{
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    const std::unique_ptr<std::FILE, int (*) (std::FILE*)> closer (f, std::fclose);   // also when a resize throws
    char id[4];
    uint32_t len = 0, sr = 0;
    uint16_t fmt = 0, ch = 0, bits = 0;
    const long size = std::fseek (f, 0, SEEK_END) == 0 ? std::ftell (f) : -1;   // -1 past 2 GB where long is 32-bit
    bool ok = size > 0 && size < INT32_MAX && std::fseek (f, 0, SEEK_SET) == 0 && std::fread (id, 1, 4, f) == 4 && std::memcmp (id, "RIFF", 4) == 0
              && std::fread (&len, 4, 1, f) == 1 && std::fread (id, 1, 4, f) == 4 && std::memcmp (id, "WAVE", 4) == 0;
    for (uint64_t pos = 12; ok && pos + 8 <= (uint64_t) size; pos += 8 + (uint64_t) len + (len & 1))   // absolute seeks only
    {
        ok = std::fseek (f, (long) pos, SEEK_SET) == 0 && std::fread (id, 1, 4, f) == 4 && std::fread (&len, 4, 1, f) == 1
             && pos + 8 + len <= (uint64_t) size;
        if (! ok) break;
        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            uint8_t b[16];
            if (len < 16 || std::fread (b, 1, 16, f) != 16) { ok = false; break; }
            std::memcpy (&fmt, b, 2); std::memcpy (&ch, b + 2, 2); std::memcpy (&sr, b + 4, 4); std::memcpy (&bits, b + 14, 2);
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            if (fmt != 3 || bits != 32 || (ch != 1 && ch != 2) || sr < 1000 || sr > 768000) break;
            const double cap = toRate > 0 ? std::ceil ((double) maxFrames * sr / toRate) + 1 : (double) maxFrames;   // double: no overflow
            const size_t n = (size_t) std::min ((double) (len / (4u * ch)), std::max (0.0, cap));
            l.resize (n); r.resize (n);
            float buf[2048];
            for (size_t i = 0; i < n && ok;)   // in pieces: no interleaved copy of the whole file
            {
                const size_t k = std::min (n - i, (size_t) 2048 / ch);
                ok = std::fread (buf, sizeof (float) * ch, k, f) == k;
                for (size_t j = 0; j < k; ++j, ++i) { l[i] = buf[j * ch]; r[i] = buf[j * ch + ch - 1]; }
            }
            sampleRate = (int) sr;
            return ok;
        }
    }
    return false;
}

// Linear resample of one channel to exactly n frames (n = 0 only for empty input).
inline std::vector<float> resample (const std::vector<float>& x, size_t n)
{
    if (n == x.size()) return x;
    std::vector<float> y (n);
    if (x.empty()) return y;
    const double step = (double) x.size() / n;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = i * step;
        const size_t a = std::min ((size_t) t, x.size() - 1), b = std::min (a + 1, x.size() - 1);
        const float k = (float) (t - (double) a);
        y[i] = x[a] + k * (x[b] - x[a]);
    }
    return y;
}

// Linear resample of one channel from rate `from` to rate `to`. A non-empty loop keeps at least one frame.
inline std::vector<float> resample (const std::vector<float>& x, int from, int to)
{
    if (from == to || x.empty()) return x;
    return resample (x, (size_t) std::max (1LL, std::llround ((double) x.size() * to / from)));
}
}
