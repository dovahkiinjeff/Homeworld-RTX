// Reproject the current Homeworld mission BC6H lat/long environment into a
// seam-guttered octahedral HDR Texture2D without any 8-bit intermediate.
//
// This decoder intentionally supports the deterministic BC6H_UF16 mode-11
// blocks emitted by tools/bc6h_encode.cpp. It is an offline migration helper,
// not a general-purpose BC6H decoder.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr std::array<int, 16> kWeights = {
    0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
struct Pixel { float r, g, b; };

struct BitReader
{
    const std::uint8_t *bytes;
    unsigned bit = 0;
    std::uint32_t get(unsigned count)
    {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i, ++bit)
            value |= ((bytes[bit >> 3] >> (bit & 7)) & 1u) << i;
        return value;
    }
};

std::uint32_t readU32(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
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
    else if (exponent == 31) bits = sign | 0x7f800000u | (mantissa << 13);
    else bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
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
float decodeEndpoint(unsigned q) { return halfToFloat(finishUnquantized(q)); }

bool decodeMode11Block(const std::uint8_t *block, Pixel out[16])
{
    BitReader reader{block};
    if (reader.get(5) != 0b00011u) return false;
    const Pixel a{decodeEndpoint(reader.get(10)), decodeEndpoint(reader.get(10)),
                  decodeEndpoint(reader.get(10))};
    const Pixel b{decodeEndpoint(reader.get(10)), decodeEndpoint(reader.get(10)),
                  decodeEndpoint(reader.get(10))};
    std::array<unsigned,16> indices{};
    indices[0] = reader.get(3);
    for (unsigned i=1; i<16; ++i) indices[i]=reader.get(4);
    for (unsigned i=0; i<16; ++i)
    {
        const float t = static_cast<float>(kWeights[indices[i]]) / 64.0f;
        out[i] = {a.r + (b.r-a.r)*t, a.g + (b.g-a.g)*t, a.b + (b.b-a.b)*t};
    }
    return true;
}

bool loadDds(const char *path, std::vector<Pixel> &pixels, unsigned &width, unsigned &height)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    if (data.size() < 148 || std::memcmp(data.data(), "DDS ", 4) != 0 ||
        readU32(data.data()+4) != 124 || readU32(data.data()+84) != 0x30315844u ||
        readU32(data.data()+128) != 95 || readU32(data.data()+132) != 3 ||
        readU32(data.data()+140) != 1)
        return false;
    height=readU32(data.data()+12); width=readU32(data.data()+16);
    if (!width || !height || (width&3u) || (height&3u) || readU32(data.data()+28)!=1)
        return false;
    const unsigned blocksWide=(width+3u)/4u, blocksHigh=(height+3u)/4u;
    if (data.size() != 148u + static_cast<std::size_t>(blocksWide)*blocksHigh*16u)
        return false;
    pixels.resize(static_cast<std::size_t>(width)*height);
    const std::uint8_t *src=data.data()+148;
    Pixel decoded[16];
    for (unsigned by=0; by<blocksHigh; ++by)
        for (unsigned bx=0; bx<blocksWide; ++bx, src+=16)
        {
            if (!decodeMode11Block(src, decoded)) return false;
            for (unsigned y=0; y<4 && by*4+y<height; ++y)
                for (unsigned x=0; x<4 && bx*4+x<width; ++x)
                    pixels[static_cast<std::size_t>(by*4+y)*width + bx*4+x]=decoded[y*4+x];
        }
    return true;
}

Pixel sampleLatLong(const std::vector<Pixel> &src, unsigned width, unsigned height,
                    float dx, float dy, float dz)
{
    const float theta=std::atan2(dy,dx);
    float u=(0.5f*kPi-theta)/(2.0f*kPi);
    u-=std::floor(u);
    const float v=std::acos(std::max(-1.0f,std::min(1.0f,dz)))/kPi;
    const float fx=u*width-0.5f, fy=v*height-0.5f;
    const int ix0=static_cast<int>(std::floor(fx));
    const int iy0=static_cast<int>(std::floor(fy));
    const float tx=fx-std::floor(fx), ty=fy-std::floor(fy);
    auto wrapX=[&](int x){ x%=static_cast<int>(width); if(x<0)x+=width; return x; };
    auto clampY=[&](int y){ return std::max(0,std::min(static_cast<int>(height)-1,y)); };
    const int x0=wrapX(ix0), x1=wrapX(ix0+1), y0=clampY(iy0), y1=clampY(iy0+1);
    const Pixel &p00=src[static_cast<std::size_t>(y0)*width+x0];
    const Pixel &p10=src[static_cast<std::size_t>(y0)*width+x1];
    const Pixel &p01=src[static_cast<std::size_t>(y1)*width+x0];
    const Pixel &p11=src[static_cast<std::size_t>(y1)*width+x1];
    Pixel top{p00.r+(p10.r-p00.r)*tx,p00.g+(p10.g-p00.g)*tx,p00.b+(p10.b-p00.b)*tx};
    Pixel bottom{p01.r+(p11.r-p01.r)*tx,p01.g+(p11.g-p01.g)*tx,p01.b+(p11.b-p01.b)*tx};
    return {top.r+(bottom.r-top.r)*ty,top.g+(bottom.g-top.g)*ty,top.b+(bottom.b-top.b)*ty};
}

void wrapOctahedralGutter(float &x, float &y)
{
    // Extending through a square edge must follow the octahedron topology,
    // not clamp/repeat ordinary image coordinates. Two iterations resolve corners.
    for (int pass=0; pass<2; ++pass)
    {
        if (x>1.0f) { x=2.0f-x; y=-y; }
        if (x<-1.0f){ x=-2.0f-x; y=-y; }
        if (y>1.0f) { y=2.0f-y; x=-x; }
        if (y<-1.0f){ y=-2.0f-y; x=-x; }
    }
}
void octahedralToDirection(float x,float y,float &dx,float &dy,float &dz)
{
    dx=x; dy=y; dz=1.0f-std::fabs(x)-std::fabs(y);
    if (dz<0.0f)
    {
        const float oldX=dx, oldY=dy;
        dx=(1.0f-std::fabs(oldY))*(oldX>=0.0f?1.0f:-1.0f);
        dy=(1.0f-std::fabs(oldX))*(oldY>=0.0f?1.0f:-1.0f);
    }
    const float length=std::sqrt(dx*dx+dy*dy+dz*dz);
    dx/=length; dy/=length; dz/=length;
}
bool writeHwf6(const char *path,const std::vector<Pixel> &pixels,unsigned width,unsigned height)
{
    std::ofstream out(path,std::ios::binary|std::ios::trunc); if(!out)return false;
    out.write("HWF6",4); writeU32(out,width); writeU32(out,height);
    out.write(reinterpret_cast<const char*>(pixels.data()),
              static_cast<std::streamsize>(pixels.size()*sizeof(Pixel)));
    return !!out;
}
}

int main(int argc,char **argv)
{
    if(argc<3||argc>5){std::fprintf(stderr,"usage: bc6h_reproject_octa <source_bc6h.dds> <output.hwf6> [core=2896] [gutter=4]\n");return 2;}
    const unsigned core=argc>=4?static_cast<unsigned>(std::stoul(argv[3])):2896u;
    const unsigned gutter=argc>=5?static_cast<unsigned>(std::stoul(argv[4])):4u;
    const unsigned full=core+2u*gutter;
    if(!core||!gutter||(full&3u)){std::fprintf(stderr,"core+2*gutter must be positive and BC-block aligned\n");return 3;}
    std::vector<Pixel> source; unsigned sw=0,sh=0;
    if(!loadDds(argv[1],source,sw,sh)){std::fprintf(stderr,"cannot decode source BC6H mode-11 DDS\n");return 4;}
    if(sw!=sh*2u){std::fprintf(stderr,"source must be the existing 2:1 mission environment\n");return 5;}
    std::vector<Pixel> output(static_cast<std::size_t>(full)*full);
#if defined(_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for(int iy=0;iy<static_cast<int>(full);++iy)
        for(unsigned ix=0;ix<full;++ix)
        {
            float ox=((static_cast<float>(ix)+0.5f-gutter)/static_cast<float>(core))*2.0f-1.0f;
            float oy=((static_cast<float>(iy)+0.5f-gutter)/static_cast<float>(core))*2.0f-1.0f;
            wrapOctahedralGutter(ox,oy);
            float dx,dy,dz; octahedralToDirection(ox,oy,dx,dy,dz);
            output[static_cast<std::size_t>(iy)*full+ix]=sampleLatLong(source,sw,sh,dx,dy,dz);
        }
    if(!writeHwf6(argv[2],output,full,full)){std::fprintf(stderr,"cannot write HWF6 output\n");return 6;}
    std::fprintf(stderr,"reprojected %ux%u lat/long -> %ux%u octahedral (core %u + %u px gutters)\n",sw,sh,full,full,core,gutter);
    return 0;
}
