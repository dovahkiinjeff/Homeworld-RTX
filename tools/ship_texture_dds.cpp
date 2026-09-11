#include <DirectXTex.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

using namespace DirectX;

namespace
{
bool rgba8(const wchar_t *path, bool tga, ScratchImage &result)
{
    TexMetadata metadata{};
    ScratchImage loaded;
    HRESULT hr = tga ? LoadFromTGAFile(path, &metadata, loaded)
                     : LoadFromWICFile(path, WIC_FLAGS_NONE, &metadata, loaded);
    if (FAILED(hr)) return false;
    const Image *image = loaded.GetImage(0, 0, 0);
    if (image == nullptr) return false;
    if (image->format == DXGI_FORMAT_R8G8B8A8_UNORM)
        return SUCCEEDED(result.InitializeFromImage(*image));
    return SUCCEEDED(Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM,
                             TEX_FILTER_DEFAULT, 0.0f, result));
}

bool rgba8Auto(const wchar_t *path, ScratchImage &result)
{
    std::wstring value(path);
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    const bool tga = value.size() >= 4 && value.substr(value.size() - 4) == L".tga";
    return rgba8(path, tga, result);
}

bool saveCompressedMips(const Image &base, DXGI_FORMAT format,
                        const wchar_t *path)
{
    ScratchImage mips;
    if (FAILED(GenerateMipMaps(base, TEX_FILTER_FANT, 0, mips))) return false;
    ScratchImage compressed;
    TEX_COMPRESS_FLAGS flags = TEX_COMPRESS_PARALLEL;
    if (format == DXGI_FORMAT_BC7_UNORM)
        flags = static_cast<TEX_COMPRESS_FLAGS>(flags | TEX_COMPRESS_BC7_QUICK);
    if (FAILED(Compress(mips.GetImages(), mips.GetImageCount(),
                        mips.GetMetadata(), format, flags, 1.0f, compressed)))
        return false;
    return SUCCEEDED(SaveToDDSFile(compressed.GetImages(),
                                   compressed.GetImageCount(),
                                   compressed.GetMetadata(), DDS_FLAGS_NONE,
                                   path));
}

float luminance(const std::uint8_t *pixel)
{
    const float r = pixel[0] / 255.0f;
    const float g = pixel[1] / 255.0f;
    const float b = pixel[2] / 255.0f;
    return 0.2126f * std::pow(r, 2.2f) +
           0.7152f * std::pow(g, 2.2f) +
           0.0722f * std::pow(b, 2.2f);
}

bool makeNormal(const Image &source, unsigned int targetWidth,
                unsigned int targetHeight, ScratchImage &normal)
{
    const unsigned int width = static_cast<unsigned int>(source.width);
    const unsigned int height = static_cast<unsigned int>(source.height);
    std::vector<float> heights(static_cast<size_t>(width) * height);
    for (unsigned int y = 0; y < height; ++y)
        for (unsigned int x = 0; x < width; ++x)
            heights[static_cast<size_t>(y) * width + x] = luminance(
                source.pixels + static_cast<size_t>(y) * source.rowPitch + x * 4u);
    auto sample = [&](int x, int y)
    {
        x = std::clamp(x, 0, static_cast<int>(width) - 1);
        y = std::clamp(y, 0, static_cast<int>(height) - 1);
        return heights[static_cast<size_t>(y) * width + x];
    };
    ScratchImage nativeNormal;
    if (FAILED(nativeNormal.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,
                                         width, height, 1, 1))) return false;
    const Image *image = nativeNormal.GetImage(0, 0, 0);
    std::uint8_t *normalPixels = nativeNormal.GetPixels();
    for (unsigned int y = 0; y < height; ++y)
    {
        std::uint8_t *row = normalPixels + static_cast<size_t>(y) * image->rowPitch;
        for (unsigned int x = 0; x < width; ++x)
        {
            const int ix = static_cast<int>(x), iy = static_cast<int>(y);
            const float gx = (sample(ix + 1, iy - 1) + 2.0f * sample(ix + 1, iy) +
                              sample(ix + 1, iy + 1) - sample(ix - 1, iy - 1) -
                              2.0f * sample(ix - 1, iy) - sample(ix - 1, iy + 1)) * 0.25f;
            const float gy = (sample(ix - 1, iy + 1) + 2.0f * sample(ix, iy + 1) +
                              sample(ix + 1, iy + 1) - sample(ix - 1, iy - 1) -
                              2.0f * sample(ix, iy - 1) - sample(ix + 1, iy - 1)) * 0.25f;
            float nx = -gx * 1.35f, ny = -gy * 1.35f, nz = 1.0f;
            const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= inv; ny *= inv; nz *= inv;
            row[x * 4u + 0] = static_cast<std::uint8_t>((nx * 0.5f + 0.5f) * 255.0f + 0.5f);
            row[x * 4u + 1] = static_cast<std::uint8_t>((ny * 0.5f + 0.5f) * 255.0f + 0.5f);
            row[x * 4u + 2] = static_cast<std::uint8_t>((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
            row[x * 4u + 3] = 255;
        }
    }
    if (width == targetWidth && height == targetHeight)
        return SUCCEEDED(normal.InitializeFromImage(*image));
    return SUCCEEDED(Resize(*image, targetWidth, targetHeight,
                            TEX_FILTER_CUBIC, normal));
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)comResult;
    if (argc == 4 && std::wstring(argv[1]) == L"coloronly")
    {
        ScratchImage color;
        if (!rgba8Auto(argv[2], color)) return 3;
        const Image *colorImage = color.GetImage(0, 0, 0);
        if (colorImage == nullptr) return 4;
        return saveCompressedMips(*colorImage, DXGI_FORMAT_BC7_UNORM,
                                  argv[3]) ? 0 : 5;
    }
    if (argc == 6 && std::wstring(argv[1]) == L"color")
    {
        ScratchImage color, vanilla, normal;
        if (!rgba8Auto(argv[2], color) || !rgba8Auto(argv[3], vanilla))
            return 3;
        const Image *colorImage = color.GetImage(0, 0, 0);
        const Image *vanillaImage = vanilla.GetImage(0, 0, 0);
        if (colorImage == nullptr || vanillaImage == nullptr ||
            !makeNormal(*vanillaImage, static_cast<unsigned int>(colorImage->width),
                        static_cast<unsigned int>(colorImage->height), normal))
            return 4;
        if (!saveCompressedMips(*colorImage, DXGI_FORMAT_BC7_UNORM, argv[4]) ||
            !saveCompressedMips(*normal.GetImage(0, 0, 0),
                                DXGI_FORMAT_BC5_UNORM, argv[5]))
            return 5;
        return 0;
    }
    if (argc == 5 && std::wstring(argv[1]) == L"color4")
    {
        ScratchImage color, normalSource, normal;
        if (!rgba8Auto(argv[2], color)) return 3;
        const Image *colorImage = color.GetImage(0, 0, 0);
        if (colorImage == nullptr) return 4;
        const size_t sourceWidth = std::max<size_t>(1, colorImage->width / 4);
        const size_t sourceHeight = std::max<size_t>(1, colorImage->height / 4);
        if (FAILED(Resize(*colorImage, sourceWidth, sourceHeight,
                          TEX_FILTER_FANT, normalSource))) return 4;
        if (!makeNormal(*normalSource.GetImage(0, 0, 0),
                        static_cast<unsigned int>(colorImage->width),
                        static_cast<unsigned int>(colorImage->height), normal))
            return 4;
        if (!saveCompressedMips(*colorImage, DXGI_FORMAT_BC7_UNORM, argv[3]) ||
            !saveCompressedMips(*normal.GetImage(0, 0, 0),
                                DXGI_FORMAT_BC5_UNORM, argv[4]))
            return 5;
        return 0;
    }
    if (argc == 5 && std::wstring(argv[1]) == L"colorfull")
    {
        ScratchImage color, normal;
        if (!rgba8Auto(argv[2], color)) return 3;
        const Image *colorImage = color.GetImage(0, 0, 0);
        if (colorImage == nullptr ||
            !makeNormal(*colorImage, static_cast<unsigned int>(colorImage->width),
                        static_cast<unsigned int>(colorImage->height), normal))
            return 4;
        if (!saveCompressedMips(*colorImage, DXGI_FORMAT_BC7_UNORM, argv[3]) ||
            !saveCompressedMips(*normal.GetImage(0, 0, 0),
                                DXGI_FORMAT_BC5_UNORM, argv[4]))
            return 5;
        return 0;
    }
    if (argc == 4 && std::wstring(argv[1]) == L"mask")
    {
        ScratchImage mask;
        if (!rgba8(argv[2], false, mask)) return 3;
        return saveCompressedMips(*mask.GetImage(0, 0, 0),
                                  DXGI_FORMAT_BC4_UNORM, argv[3]) ? 0 : 5;
    }
    if (argc == 9 && std::wstring(argv[1]) == L"pbr")
    {
        ScratchImage base, normal, orm;
        if (!rgba8Auto(argv[2], base) || !rgba8Auto(argv[3], normal) ||
            !rgba8Auto(argv[4], orm)) return 3;
        unsigned long requested = std::wcstoul(argv[5], nullptr, 10);
        if (requested == 0) return 2;
        ScratchImage baseSized, normalSized, ormSized;
        const Image *baseImage = base.GetImage(0, 0, 0);
        const Image *normalImage = normal.GetImage(0, 0, 0);
        const Image *ormImage = orm.GetImage(0, 0, 0);
        if (!baseImage || !normalImage || !ormImage) return 4;
        auto sizeImage = [requested](const Image &input, ScratchImage &output)
        {
            if (input.width == requested && input.height == requested)
                return SUCCEEDED(output.InitializeFromImage(input));
            return SUCCEEDED(Resize(input, requested, requested,
                                    TEX_FILTER_FANT, output));
        };
        if (!sizeImage(*baseImage, baseSized) ||
            !sizeImage(*normalImage, normalSized) ||
            !sizeImage(*ormImage, ormSized)) return 4;
        if (!saveCompressedMips(*baseSized.GetImage(0, 0, 0),
                                DXGI_FORMAT_BC7_UNORM, argv[6]) ||
            !saveCompressedMips(*normalSized.GetImage(0, 0, 0),
                                DXGI_FORMAT_BC5_UNORM, argv[7])) return 5;
        if (!saveCompressedMips(*ormSized.GetImage(0, 0, 0),
                                DXGI_FORMAT_BC7_UNORM, argv[8])) return 5;
        return 0;
    }
    std::fwprintf(stderr, L"usage: ship_texture_dds coloronly input color.dds\n"
                          L"   or: ship_texture_dds color input vanilla color.dds normal.dds\n"
                          L"   or: ship_texture_dds color4 upscaled-input color.dds normal.dds\n"
                          L"   or: ship_texture_dds colorfull upscaled-input color.dds normal.dds\n"
                          L"   or: ship_texture_dds mask input.png mask.dds\n"
                          L"   or: ship_texture_dds pbr base.png normal.png orm.png size base.dds normal.dds orm.dds\n");
    return 2;
}
