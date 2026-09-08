// Minimal high-quality opaque BC7 mode-6 encoder for generated mission skies.
// Input is the private HWSK raw-mip container emitted by
// generate_mission_skies.py; output is a standard DDS DX10 BC7_UNORM texture.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
        for (unsigned index = 0; index < count; ++index, ++bit)
        {
            if ((value >> index) & 1u)
                bytes[bit >> 3] |= static_cast<std::uint8_t>(1u << (bit & 7));
        }
    }
};

struct Pixel
{
    int r, g, b;
};

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
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 24)};
    stream.write(reinterpret_cast<const char *>(bytes), 4);
}

int quantizeOdd(int value)
{
    const int lower = std::max(1, std::min(255,
        (value & 1) != 0 ? value : value - 1));
    const int upper = std::max(1, std::min(255,
        (value & 1) != 0 ? value : value + 1));
    return std::abs(value - lower) <= std::abs(value - upper) ? lower : upper;
}

void chooseEndpoints(const std::array<Pixel, 16> &pixels,
                     std::array<int, 3> &endpoint0,
                     std::array<int, 3> &endpoint1)
{
    std::array<double, 3> mean{};
    for (const Pixel &pixel : pixels)
    {
        mean[0] += pixel.r;
        mean[1] += pixel.g;
        mean[2] += pixel.b;
    }
    for (double &component : mean) component /= 16.0;

    double covariance[3][3]{};
    for (const Pixel &pixel : pixels)
    {
        const double value[3] = {
            pixel.r - mean[0], pixel.g - mean[1], pixel.b - mean[2]};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                covariance[row][column] += value[row] * value[column];
    }

    std::array<double, 3> axis{0.577350269, 0.577350269, 0.577350269};
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        std::array<double, 3> next{};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                next[row] += covariance[row][column] * axis[column];
        const double length = std::sqrt(next[0] * next[0] +
                                        next[1] * next[1] +
                                        next[2] * next[2]);
        if (length < 1.0e-8) break;
        for (int component = 0; component < 3; ++component)
            axis[component] = next[component] / length;
    }

    double minimum = std::numeric_limits<double>::max();
    double maximum = -std::numeric_limits<double>::max();
    for (const Pixel &pixel : pixels)
    {
        const double projection = pixel.r * axis[0] +
                                  pixel.g * axis[1] +
                                  pixel.b * axis[2];
        minimum = std::min(minimum, projection);
        maximum = std::max(maximum, projection);
    }
    const double centerProjection = mean[0] * axis[0] +
                                    mean[1] * axis[1] +
                                    mean[2] * axis[2];
    for (int component = 0; component < 3; ++component)
    {
        endpoint0[component] = quantizeOdd(static_cast<int>(std::lround(
            mean[component] + axis[component] * (minimum - centerProjection))));
        endpoint1[component] = quantizeOdd(static_cast<int>(std::lround(
            mean[component] + axis[component] * (maximum - centerProjection))));
    }
}

void encodeBlock(const std::uint8_t *rgba, unsigned stride,
                 unsigned validWidth, unsigned validHeight,
                 std::array<std::uint8_t, 16> &encoded)
{
    std::array<Pixel, 16> pixels{};
    for (unsigned y = 0; y < 4; ++y)
    {
        const unsigned sourceY = std::min(y, validHeight - 1);
        for (unsigned x = 0; x < 4; ++x)
        {
            const unsigned sourceX = std::min(x, validWidth - 1);
            const std::uint8_t *source = rgba + sourceY * stride + sourceX * 4;
            pixels[y * 4 + x] = {source[0], source[1], source[2]};
        }
    }

    std::array<int, 3> endpoint0{}, endpoint1{};
    chooseEndpoints(pixels, endpoint0, endpoint1);
    std::array<unsigned, 16> indices{};
    for (unsigned pixelIndex = 0; pixelIndex < 16; ++pixelIndex)
    {
        long bestError = std::numeric_limits<long>::max();
        unsigned bestIndex = 0;
        for (unsigned paletteIndex = 0; paletteIndex < 16; ++paletteIndex)
        {
            const int weight = kWeights[paletteIndex];
            long error = 0;
            const int source[3] = {
                pixels[pixelIndex].r, pixels[pixelIndex].g, pixels[pixelIndex].b};
            for (int component = 0; component < 3; ++component)
            {
                const int decoded = ((64 - weight) * endpoint0[component] +
                                     weight * endpoint1[component] + 32) >> 6;
                const int delta = source[component] - decoded;
                error += delta * delta;
            }
            if (error < bestError)
            {
                bestError = error;
                bestIndex = paletteIndex;
            }
        }
        indices[pixelIndex] = bestIndex;
    }

    if (indices[0] >= 8)
    {
        std::swap(endpoint0, endpoint1);
        for (unsigned &index : indices) index = 15 - index;
    }

    BitWriter writer;
    writer.put(0x40u, 7); // BC7 mode 6 unary prefix, least-significant bit first.
    for (int component = 0; component < 3; ++component)
    {
        writer.put(static_cast<std::uint32_t>(endpoint0[component] >> 1), 7);
        writer.put(static_cast<std::uint32_t>(endpoint1[component] >> 1), 7);
    }
    writer.put(127u, 7); // alpha endpoint 0: (127 << 1) | pbit = 255
    writer.put(127u, 7); // alpha endpoint 1
    writer.put(1u, 1);
    writer.put(1u, 1);
    writer.put(indices[0], 3); // anchor omits the redundant high bit
    for (unsigned index = 1; index < 16; ++index) writer.put(indices[index], 4);
    encoded = writer.bytes;
}

void writeDdsHeader(std::ofstream &output, unsigned width, unsigned height,
                    unsigned mipCount)
{
    output.write("DDS ", 4);
    writeU32(output, 124);
    writeU32(output, 0x000A1007u); // caps, dimensions, pixel format, linear size, mip count
    writeU32(output, height);
    writeU32(output, width);
    writeU32(output, ((width + 3) / 4) * 16);
    writeU32(output, 0);
    writeU32(output, mipCount);
    for (int index = 0; index < 11; ++index) writeU32(output, 0);
    writeU32(output, 32);
    writeU32(output, 0x4);          // DDPF_FOURCC
    writeU32(output, 0x30315844u);  // DX10
    for (int index = 0; index < 5; ++index) writeU32(output, 0);
    writeU32(output, mipCount > 1 ? 0x00401008u : 0x00001000u);
    for (int index = 0; index < 4; ++index) writeU32(output, 0);
    writeU32(output, 98); // DXGI_FORMAT_BC7_UNORM
    writeU32(output, 3);  // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    writeU32(output, 0);
    writeU32(output, 1);
    writeU32(output, 0);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: bc7_encode <HWSK raw mip input> <DDS output>\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    char magic[4]{};
    input.read(magic, 4);
    if (!input || std::string(magic, 4) != "HWSK")
    {
        std::fprintf(stderr, "invalid HWSK input\n");
        return 3;
    }
    const unsigned width = readU32(input);
    const unsigned height = readU32(input);
    const unsigned mipCount = readU32(input);
    if (!input || width == 0 || height == 0 || mipCount == 0)
    {
        std::fprintf(stderr, "invalid HWSK dimensions\n");
        return 4;
    }

    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output)
    {
        std::fprintf(stderr, "could not create DDS output\n");
        return 5;
    }
    writeDdsHeader(output, width, height, mipCount);

    for (unsigned mip = 0; mip < mipCount; ++mip)
    {
        const unsigned mipWidth = readU32(input);
        const unsigned mipHeight = readU32(input);
        const std::uint32_t byteCount = readU32(input);
        if (!input || mipWidth == 0 || mipHeight == 0 ||
            byteCount != mipWidth * mipHeight * 4u)
        {
            std::fprintf(stderr, "invalid mip %u\n", mip);
            return 6;
        }
        std::vector<std::uint8_t> rgba(byteCount);
        input.read(reinterpret_cast<char *>(rgba.data()), rgba.size());
        if (!input)
        {
            std::fprintf(stderr, "truncated mip %u\n", mip);
            return 7;
        }

        const unsigned blocksX = (mipWidth + 3) / 4;
        const unsigned blocksY = (mipHeight + 3) / 4;
        std::array<std::uint8_t, 16> encoded{};
        for (unsigned blockY = 0; blockY < blocksY; ++blockY)
        {
            for (unsigned blockX = 0; blockX < blocksX; ++blockX)
            {
                const unsigned x = blockX * 4;
                const unsigned y = blockY * 4;
                encodeBlock(rgba.data() + (static_cast<std::size_t>(y) * mipWidth + x) * 4,
                            mipWidth * 4,
                            std::min(4u, mipWidth - x),
                            std::min(4u, mipHeight - y), encoded);
                output.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
            }
        }
    }
    if (!output)
    {
        std::fprintf(stderr, "failed while writing DDS output\n");
        return 8;
    }
    return 0;
}
