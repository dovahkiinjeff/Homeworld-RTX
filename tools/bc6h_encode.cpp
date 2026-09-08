// Minimal BC6H_UF16 mode-11 encoder for Homeworld mission skies.
//
// The generated skies are smooth, single-layer HDR environments. BC6H mode 11
// is a strong fit: one subset, two explicit 10-bit RGB endpoints, and a shared
// 4-bit interpolation index per texel. This keeps the encoder small and fully
// deterministic while preserving unsigned half-float HDR range.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr std::array<int, 16> kWeights = {
    0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

struct BitWriter
{
    std::array<std::uint8_t, 16> bytes{};
    unsigned bit = 0;
    void put(std::uint32_t value, unsigned count)
    {
        for (unsigned i = 0; i < count; ++i, ++bit)
            if ((value >> i) & 1u)
                bytes[bit >> 3] |= static_cast<std::uint8_t>(1u << (bit & 7));
    }
};

struct Pixel { float r, g, b; };

std::uint32_t readU32(std::ifstream &stream)
{
    std::uint8_t bytes[4]{};
    stream.read(reinterpret_cast<char *>(bytes), 4);
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void writeU32(std::ofstream &stream, std::uint32_t value)
{
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24)};
    stream.write(reinterpret_cast<const char *>(bytes), 4);
}

float halfToFloat(std::uint16_t h)
{
    const std::uint32_t sign = (static_cast<std::uint32_t>(h) & 0x8000u) << 16;
    std::uint32_t exponent = (h >> 10) & 0x1fu;
    std::uint32_t mantissa = h & 0x3ffu;
    std::uint32_t bits;
    if (exponent == 0)
    {
        if (mantissa == 0) bits = sign;
        else
        {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3ffu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    }
    else if (exponent == 31)
        bits = sign | 0x7f800000u | (mantissa << 13);
    else
        bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::uint16_t finishUnquantized(unsigned comp)
{
    unsigned unq;
    if (comp == 0) unq = 0;
    else if (comp == 1023) unq = 0xffffu;
    else unq = ((comp << 16) + 0x8000u) >> 10;
    return static_cast<std::uint16_t>((unq * 31u) >> 6);
}

struct EndpointTable
{
    std::array<float, 1024> decoded{};
    EndpointTable()
    {
        for (unsigned i = 0; i < decoded.size(); ++i)
            decoded[i] = halfToFloat(finishUnquantized(i));
    }
    unsigned quantize(float value) const
    {
        if (!(value > 0.0f)) return 0;
        value = std::min(value, 65504.0f);
        auto it = std::lower_bound(decoded.begin(), decoded.end(), value);
        if (it == decoded.begin()) return 0;
        if (it == decoded.end()) return 1023;
        const unsigned hi = static_cast<unsigned>(it - decoded.begin());
        const unsigned lo = hi - 1u;
        return (value - decoded[lo] <= decoded[hi] - value) ? lo : hi;
    }
};

const EndpointTable kEndpointTable;

float sqr(float x) { return x * x; }
float error(const Pixel &a, const Pixel &b)
{
    // Slightly favor luminance while still retaining chroma transitions.
    const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return 0.85f * sqr(dr) + 1.25f * sqr(dg) + 0.70f * sqr(db);
}

void chooseEndpoints(const std::array<Pixel, 16> &p, Pixel &a, Pixel &b)
{
    // Pick the source channel with the widest normalized span, then use the
    // actual RGB values of its extrema. For smooth BTG gradients this tracks
    // the local color trajectory better than independent RGB min/max corners.
    float minv[3] = {p[0].r, p[0].g, p[0].b};
    float maxv[3] = {p[0].r, p[0].g, p[0].b};
    unsigned mini[3] = {0,0,0}, maxi[3] = {0,0,0};
    for (unsigned i = 1; i < 16; ++i)
    {
        const float v[3] = {p[i].r, p[i].g, p[i].b};
        for (int c = 0; c < 3; ++c)
        {
            if (v[c] < minv[c]) { minv[c] = v[c]; mini[c] = i; }
            if (v[c] > maxv[c]) { maxv[c] = v[c]; maxi[c] = i; }
        }
    }
    int axis = 0;
    float span = maxv[0] - minv[0];
    for (int c = 1; c < 3; ++c)
        if (maxv[c] - minv[c] > span) { axis = c; span = maxv[c] - minv[c]; }
    a = p[mini[axis]];
    b = p[maxi[axis]];
    if (span < 1.0e-7f) a = b = p[0];
}

Pixel paletteColor(const Pixel &a, const Pixel &b, unsigned index)
{
    const float t = static_cast<float>(kWeights[index]) / 64.0f;
    return {a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t};
}

void encodeBlock(const Pixel *source, unsigned stride, unsigned validWidth,
                 unsigned validHeight, std::array<std::uint8_t, 16> &out)
{
    std::array<Pixel, 16> p{};
    for (unsigned y = 0; y < 4; ++y)
    {
        const unsigned sy = std::min(y, validHeight - 1u);
        for (unsigned x = 0; x < 4; ++x)
        {
            const unsigned sx = std::min(x, validWidth - 1u);
            p[y * 4 + x] = source[sy * stride + sx];
        }
    }

    Pixel e0{}, e1{};
    chooseEndpoints(p, e0, e1);
    std::array<unsigned, 3> q0 = {
        kEndpointTable.quantize(e0.r), kEndpointTable.quantize(e0.g), kEndpointTable.quantize(e0.b)};
    std::array<unsigned, 3> q1 = {
        kEndpointTable.quantize(e1.r), kEndpointTable.quantize(e1.g), kEndpointTable.quantize(e1.b)};
    e0 = {kEndpointTable.decoded[q0[0]], kEndpointTable.decoded[q0[1]], kEndpointTable.decoded[q0[2]]};
    e1 = {kEndpointTable.decoded[q1[0]], kEndpointTable.decoded[q1[1]], kEndpointTable.decoded[q1[2]]};

    std::array<Pixel, 16> palette{};
    for (unsigned i = 0; i < 16; ++i) palette[i] = paletteColor(e0, e1, i);

    std::array<unsigned, 16> indices{};
    const Pixel d{e1.r - e0.r, e1.g - e0.g, e1.b - e0.b};
    const float denom = d.r*d.r + d.g*d.g + d.b*d.b;
    for (unsigned i = 0; i < 16; ++i)
    {
        float t = 0.0f;
        if (denom > 1.0e-12f)
            t = ((p[i].r-e0.r)*d.r + (p[i].g-e0.g)*d.g + (p[i].b-e0.b)*d.b) / denom;
        t = std::max(0.0f, std::min(1.0f, t));
        const float targetWeight = t * 64.0f;
        unsigned guess = 0;
        while (guess + 1 < 16 && std::abs(kWeights[guess + 1] - targetWeight) <
                                  std::abs(kWeights[guess] - targetWeight)) ++guess;
        unsigned best = guess;
        float bestError = error(p[i], palette[best]);
        for (int delta = -2; delta <= 2; ++delta)
        {
            const int candidate = static_cast<int>(guess) + delta;
            if (candidate < 0 || candidate > 15) continue;
            const float candidateError = error(p[i], palette[static_cast<unsigned>(candidate)]);
            if (candidateError < bestError)
            {
                bestError = candidateError;
                best = static_cast<unsigned>(candidate);
            }
        }
        indices[i] = best;
    }

    // Pixel 0 is the fix-up texel and stores only 3 index bits. Reverse the
    // endpoint line when needed so its redundant MSB is zero.
    if (indices[0] >= 8u)
    {
        std::swap(q0, q1);
        for (unsigned &index : indices) index = 15u - index;
    }

    BitWriter writer;
    writer.put(0b00011u, 5); // BC6H mode 11.
    writer.put(q0[0], 10); writer.put(q0[1], 10); writer.put(q0[2], 10);
    writer.put(q1[0], 10); writer.put(q1[1], 10); writer.put(q1[2], 10);
    writer.put(indices[0], 3);
    for (unsigned i = 1; i < 16; ++i) writer.put(indices[i], 4);
    out = writer.bytes;
}

void writeDdsHeader(std::ofstream &output, unsigned width, unsigned height)
{
    output.write("DDS ", 4);
    writeU32(output, 124);
    writeU32(output, 0x00081007u); // caps, dimensions, pixel format, linear size
    writeU32(output, height);
    writeU32(output, width);
    writeU32(output, ((width + 3u) / 4u) * ((height + 3u) / 4u) * 16u);
    writeU32(output, 0); writeU32(output, 1);
    for (int i = 0; i < 11; ++i) writeU32(output, 0);
    writeU32(output, 32); writeU32(output, 0x4); writeU32(output, 0x30315844u);
    for (int i = 0; i < 5; ++i) writeU32(output, 0);
    writeU32(output, 0x1000u);
    for (int i = 0; i < 4; ++i) writeU32(output, 0);
    writeU32(output, 95); // DXGI_FORMAT_BC6H_UF16
    writeU32(output, 3);  // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    writeU32(output, 0); writeU32(output, 1); writeU32(output, 0);
}
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: bc6h_encode <HWF6 float32 RGB input> <DDS output>\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    char magic[4]{}; input.read(magic, 4);
    if (!input || std::string(magic, 4) != "HWF6")
    {
        std::fprintf(stderr, "invalid HWF6 input\n"); return 3;
    }
    const unsigned width = readU32(input), height = readU32(input);
    if (!input || !width || !height || (width & 3u) || (height & 3u))
    {
        std::fprintf(stderr, "invalid dimensions\n"); return 4;
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<Pixel> pixels(count);
    input.read(reinterpret_cast<char *>(pixels.data()),
               static_cast<std::streamsize>(count * sizeof(Pixel)));
    if (!input) { std::fprintf(stderr, "truncated HWF6 input\n"); return 5; }

    const unsigned blocksWide = width / 4u;
    const unsigned blocksHigh = height / 4u;
    const std::size_t blockCount = static_cast<std::size_t>(blocksWide) * blocksHigh;
    std::vector<std::array<std::uint8_t, 16>> blocks(blockCount);
#if defined(_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for (long long blockIndex = 0; blockIndex < static_cast<long long>(blockCount); ++blockIndex)
    {
        const unsigned by = static_cast<unsigned>(blockIndex / blocksWide) * 4u;
        const unsigned bx = static_cast<unsigned>(blockIndex % blocksWide) * 4u;
        encodeBlock(pixels.data() + static_cast<std::size_t>(by) * width + bx,
                    width, 4u, 4u, blocks[static_cast<std::size_t>(blockIndex)]);
    }

    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output) { std::fprintf(stderr, "cannot create DDS\n"); return 6; }
    writeDdsHeader(output, width, height);
    for (const auto &block : blocks)
        output.write(reinterpret_cast<const char *>(block.data()), block.size());
    if (!output) { std::fprintf(stderr, "DDS write failed\n"); return 7; }
    return 0;
}
