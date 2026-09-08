#if defined(_WIN32) && defined(HW_ENABLE_D3D12_NATIVE_RASTER)

#include <windows.h>
#include <SDL2/SDL_opengl.h>
#include "LegacyD3D12GL.h"
#include "LegacyD3D12Renderer.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr unsigned int kFrameSlots = 3;
constexpr unsigned int kSrvCapacity = 65536;
constexpr DXGI_FORMAT kRasterTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kDefaultTextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDepthResourceFormat = DXGI_FORMAT_R32_TYPELESS;
constexpr DXGI_FORMAT kDepthDsvFormat = DXGI_FORMAT_D32_FLOAT;

struct Mat4
{
    float m[16];
};

Mat4 identityMatrix()
{
    Mat4 result = {};
    result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
    return result;
}

Mat4 multiply(const Mat4 &a, const Mat4 &b)
{
    Mat4 r = {};
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            r.m[column * 4 + row] =
                a.m[0 * 4 + row] * b.m[column * 4 + 0] +
                a.m[1 * 4 + row] * b.m[column * 4 + 1] +
                a.m[2 * 4 + row] * b.m[column * 4 + 2] +
                a.m[3 * 4 + row] * b.m[column * 4 + 3];
        }
    }
    return r;
}

bool inverseMatrix(const Mat4 &input, Mat4 &output)
{
    double a[4][8] = {};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
            a[row][col] = input.m[col * 4 + row];
        a[row][row + 4] = 1.0;
    }
    for (int col = 0; col < 4; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
        if (std::fabs(a[pivot][col]) < 1.0e-12) return false;
        if (pivot != col)
            for (int j = 0; j < 8; ++j) std::swap(a[pivot][j], a[col][j]);
        const double scale = a[col][col];
        for (int j = 0; j < 8; ++j) a[col][j] /= scale;
        for (int row = 0; row < 4; ++row)
        {
            if (row == col) continue;
            const double factor = a[row][col];
            for (int j = 0; j < 8; ++j) a[row][j] -= factor * a[col][j];
        }
    }
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            output.m[col * 4 + row] = static_cast<float>(a[row][col + 4]);
    return true;
}

std::array<float, 4> transform(const Mat4 &m, float x, float y, float z, float w)
{
    return {
        m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12] * w,
        m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13] * w,
        m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14] * w,
        m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15] * w
    };
}

void normalize3(float v[3])
{
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 0.000001f)
    {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

struct ClientArray
{
    bool enabled = false;
    GLint size = 4;
    GLenum type = GL_FLOAT;
    GLsizei stride = 0;
    const unsigned char *pointer = nullptr;
    GLuint buffer = 0;
};

struct LightState
{
    bool enabled = false;
    float ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float specular[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float position[4] = {0.0f, 0.0f, 1.0f, 0.0f};
};

struct RasterDrawState
{
    bool texture2D = false;
    bool blend = false;
    bool alphaTest = false;
    bool depthTest = false;
    bool depthWrite = true;
    bool cullFace = false;
    bool scissor = false;
    bool fog = false;
    GLenum blendSrc = GL_ONE;
    GLenum blendDst = GL_ZERO;
    GLenum alphaFunc = GL_ALWAYS;
    float alphaRef = 0.0f;
    GLenum depthFunc = GL_LESS;
    GLenum cullMode = GL_BACK;
    GLenum polygonMode = GL_FILL;
    GLenum texEnvMode = GL_MODULATE;
    GLuint texture = 0;
    GLint viewport[4] = {0, 0, 640, 480};
    GLint scissorBox[4] = {0, 0, 640, 480};
    float fogColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct RasterVertex
{
    float position[4];
    float color[4];
    float uv[2];
    float normal[4];
    float fogFactor;
    float clipDistance;
};
static_assert(sizeof(RasterVertex) == 64, "legacy raster vertex size");

struct RasterCommand
{
    enum Type { Clear, Draw, MissionSky } type = Draw;
    GLbitfield clearMask = 0;
    float clearColor[4] = {};
    float clearDepth = 1.0f;
    RasterDrawState state;
    D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    size_t firstVertex = 0;
    size_t vertexCount = 0;
    size_t firstIndex = 0;
    size_t indexCount = 0;
    /* RTX-AAA: ordinary compatibility draws keep object-space attributes and
       execute the legacy fixed-function transform/lighting/fog/clip equations
       in the D3D12 vertex shader. Pretransformed glDrawPixels remains on the
       explicit compatibility path. */
    uint32_t gpuFixedFunctionEnabled = 0;
    size_t fixedStateIndex = 0;
    float skyConstants[12] = {};
};

struct CpuBuffer
{
    std::vector<unsigned char> bytes;
};

struct CompressedMip
{
    unsigned int width = 0;
    unsigned int height = 0;
    std::vector<unsigned char> bytes;
};

struct TextureRecord
{
    GLuint name = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    GLenum internalFormat = GL_RGBA;
    GLenum sourceFormat = GL_RGBA;
    bool compressed = false;
    DXGI_FORMAT gpuFormat = kDefaultTextureFormat;
    std::vector<unsigned char> pixels;
    std::vector<CompressedMip> compressedMips;
    GLint minFilter = GL_LINEAR;
    GLint magFilter = GL_LINEAR;
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    bool dirty = true;
    unsigned int descriptorSlot = 0;
    ComPtr<ID3D12Resource> gpu;
    D3D12_RESOURCE_STATES gpuState = D3D12_RESOURCE_STATE_COMMON;
};

struct FixedState
{
    GLenum matrixMode = GL_MODELVIEW;
    Mat4 modelview = identityMatrix();
    Mat4 projection = identityMatrix();
    std::vector<Mat4> modelviewStack;
    std::vector<Mat4> projectionStack;
    std::vector<RasterDrawState> attribStack;
    RasterDrawState draw;
    float clearColor[4] = {0, 0, 0, 0};
    double clearDepth = 1.0;
    float currentColor[4] = {1, 1, 1, 1};
    float currentNormal[3] = {0, 0, 1};
    float currentTexcoord[2] = {0, 0};
    float rasterPos[4] = {0, 0, 0, 1};
    float lineWidth = 1.0f;
    float pointSize = 1.0f;
    GLenum shadeModel = GL_SMOOTH;
    bool lighting = false;
    bool normalize = false;
    bool colorMaterial = false;
    bool clipPlane0 = false;
    float clipEquationEye[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    float lightModelAmbient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    LightState lights[8];
    float materialAmbient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    float materialDiffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    float materialEmission[4] = {0, 0, 0, 1};
    GLenum fogMode = GL_EXP;
    float fogDensity = 1.0f;
    float fogStart = 0.0f;
    float fogEnd = 1.0f;
    ClientArray vertexArray;
    ClientArray colorArray;
    ClientArray texcoordArray;
    ClientArray normalArray;
    GLuint arrayBuffer = 0;
    GLuint elementBuffer = 0;
    GLenum error = GL_NO_ERROR;
    GLint packAlignment = 4;
    GLint unpackAlignment = 4;
    GLint packRowLength = 0;
    GLint unpackRowLength = 0;
};

/* Per-draw fixed-function state consumed by LegacyVS through root CBV b1.
   Disabled lights retain zero contribution; ambient.w is used only as the
   enabled bit because the historical CPU lighting path ignored light alpha. */
struct GpuFixedFunctionState
{
    float modelViewRows[16];
    float mvpRows[16];
    float materialAmbient[4];
    float materialDiffuse[4];
    float materialEmission[4];
    float lightModelAmbient[4];
    float clipEquationEye[4];
    float fogParameters[4];       /* density, start, end, unused */
    uint32_t flags[4];            /* lighting, colorMaterial, fogMode, clip */
    float lightPosition[8][4];
    float lightAmbient[8][4];     /* .w = enabled */
    float lightDiffuse[8][4];
};
static_assert((sizeof(GpuFixedFunctionState) & 15u) == 0u,
              "fixed-function GPU state alignment");

FixedState g;
std::unordered_map<GLuint, CpuBuffer> gBuffers;
std::unordered_map<GLuint, TextureRecord> gTextures;
std::vector<GLuint> gTransientTextures;
std::vector<RasterCommand> gCommands;
/* RTX-AAA: one retained frame arena replaces one heap allocation per draw.
   Commands reference ranges in this contiguous vector; capacity survives
   frame boundaries so ordinary gameplay stops churning the C++ allocator. */
std::vector<RasterVertex> gFrameVertices;
/* RTX-AAA: retain native index streams too.  The original compatibility
   recorder expanded glDrawElements into duplicated vertices on the CPU,
   throwing away both mesh reuse and the GPU vertex cache. */
std::vector<uint32_t> gFrameIndices;
std::vector<GpuFixedFunctionState> gFrameFixedStates;
std::vector<RasterVertex> gImmediate;
std::vector<uint32_t> gImmediateIndices;
UINT64 gFrameTextureUploadBytes = 0;
UINT64 gFrameTextureUploadUpdates = 0;
UINT64 gFrameTextureUploadArenaGrowths = 0;
GLenum gImmediateMode = 0;
bool gInBegin = false;
GLuint gNextTexture = 1;
GLuint gNextBuffer = 1;
GLuint gNextFramebuffer = 1;
unsigned int gFrameWidth = 640;
unsigned int gFrameHeight = 480;
unsigned int gFrameSlot = 0;
size_t gSnapshots[4] = {};
bool gFrameRecording = false;
bool gDetachedDirty = false;

Mat4 &currentMatrix()
{
    if (g.matrixMode == GL_PROJECTION || g.matrixMode == GL_PROJECTION_MATRIX)
        return g.projection;
    return g.modelview;
}

std::vector<Mat4> &currentMatrixStack()
{
    if (g.matrixMode == GL_PROJECTION || g.matrixMode == GL_PROJECTION_MATRIX)
        return g.projectionStack;
    return g.modelviewStack;
}

void setError(GLenum error)
{
    if (g.error == GL_NO_ERROR) g.error = error;
}

size_t typeSize(GLenum type)
{
    switch (type)
    {
        case GL_BYTE: return sizeof(GLbyte);
        case GL_UNSIGNED_BYTE: return sizeof(GLubyte);
        case GL_SHORT: return sizeof(GLshort);
        case GL_UNSIGNED_SHORT: return sizeof(GLushort);
        case GL_INT: return sizeof(GLint);
        case GL_UNSIGNED_INT: return sizeof(GLuint);
        case GL_FLOAT: return sizeof(GLfloat);
        case GL_DOUBLE: return sizeof(GLdouble);
        default: return 0;
    }
}

const unsigned char *arrayAddress(const ClientArray &array, GLint index)
{
    const size_t elementBytes = std::max<GLint>(1, array.size) * typeSize(array.type);
    const size_t stride = array.stride > 0 ? static_cast<size_t>(array.stride) : elementBytes;
    if (array.buffer != 0)
    {
        const auto found = gBuffers.find(array.buffer);
        if (found == gBuffers.end()) return nullptr;
        const size_t offset = reinterpret_cast<size_t>(array.pointer) + static_cast<size_t>(index) * stride;
        if (offset + elementBytes > found->second.bytes.size()) return nullptr;
        return found->second.bytes.data() + offset;
    }
    if (array.pointer == nullptr) return nullptr;
    return array.pointer + static_cast<size_t>(index) * stride;
}

float componentAsFloat(const unsigned char *data, GLenum type, int index, bool color)
{
    if (data == nullptr) return 0.0f;
    switch (type)
    {
        case GL_FLOAT: return reinterpret_cast<const GLfloat *>(data)[index];
        case GL_DOUBLE: return static_cast<float>(reinterpret_cast<const GLdouble *>(data)[index]);
        case GL_BYTE:
        {
            const float v = static_cast<float>(reinterpret_cast<const GLbyte *>(data)[index]);
            return color ? std::max(-1.0f, v / 127.0f) : v;
        }
        case GL_UNSIGNED_BYTE:
        {
            const float v = static_cast<float>(reinterpret_cast<const GLubyte *>(data)[index]);
            return color ? v / 255.0f : v;
        }
        case GL_SHORT:
        {
            const float v = static_cast<float>(reinterpret_cast<const GLshort *>(data)[index]);
            return color ? std::max(-1.0f, v / 32767.0f) : v;
        }
        case GL_UNSIGNED_SHORT:
        {
            const float v = static_cast<float>(reinterpret_cast<const GLushort *>(data)[index]);
            return color ? v / 65535.0f : v;
        }
        case GL_INT: return static_cast<float>(reinterpret_cast<const GLint *>(data)[index]);
        case GL_UNSIGNED_INT: return static_cast<float>(reinterpret_cast<const GLuint *>(data)[index]);
        default: return 0.0f;
    }
}

float fogFactorForEyeVertex(const std::array<float, 4> &eye)
{
    if (!g.draw.fog) return 1.0f;
    const float distance = std::fabs(eye[2]);
    float factor = 1.0f;
    if (g.fogMode == GL_EXP2)
        factor = std::exp(-(g.fogDensity * distance) * (g.fogDensity * distance));
    else if (g.fogMode == GL_LINEAR)
    {
        const float denominator = g.fogEnd - g.fogStart;
        factor = std::fabs(denominator) > 0.000001f ?
            (g.fogEnd - distance) / denominator : 1.0f;
    }
    else
        factor = std::exp(-g.fogDensity * distance);
    return std::max(0.0f, std::min(1.0f, factor));
}

void shadeColor(const std::array<float, 4> &eye, float out[4])
{
    if (!g.lighting)
    {
        std::memcpy(out, g.currentColor, sizeof(float) * 4);
        return;
    }

    float normal[3] = {
        g.modelview.m[0] * g.currentNormal[0] + g.modelview.m[4] * g.currentNormal[1] + g.modelview.m[8] * g.currentNormal[2],
        g.modelview.m[1] * g.currentNormal[0] + g.modelview.m[5] * g.currentNormal[1] + g.modelview.m[9] * g.currentNormal[2],
        g.modelview.m[2] * g.currentNormal[0] + g.modelview.m[6] * g.currentNormal[1] + g.modelview.m[10] * g.currentNormal[2]
    };
    normalize3(normal);
    const float *diffuseMaterial = g.colorMaterial ? g.currentColor : g.materialDiffuse;
    for (int channel = 0; channel < 3; ++channel)
    {
        out[channel] = g.materialEmission[channel] +
            g.materialAmbient[channel] * g.lightModelAmbient[channel];
    }
    for (const LightState &light : g.lights)
    {
        if (!light.enabled) continue;
        float direction[3];
        if (std::fabs(light.position[3]) < 0.000001f)
        {
            direction[0] = light.position[0];
            direction[1] = light.position[1];
            direction[2] = light.position[2];
        }
        else
        {
            direction[0] = light.position[0] - eye[0];
            direction[1] = light.position[1] - eye[1];
            direction[2] = light.position[2] - eye[2];
        }
        normalize3(direction);
        const float ndotl = std::max(0.0f,
            normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2]);
        for (int channel = 0; channel < 3; ++channel)
        {
            out[channel] += g.materialAmbient[channel] * light.ambient[channel] +
                diffuseMaterial[channel] * light.diffuse[channel] * ndotl;
        }
    }
    out[0] = std::max(0.0f, std::min(1.0f, out[0]));
    out[1] = std::max(0.0f, std::min(1.0f, out[1]));
    out[2] = std::max(0.0f, std::min(1.0f, out[2]));
    out[3] = diffuseMaterial[3];
}

bool gpuFixedFunctionRuntimeEnabled()
{
    /* Default ON. Set HW_NATIVE_GPU_FIXED_FUNCTION=0 for an exact A/B fallback
       to the original serial CPU fixed-function implementation. */
    static const bool enabled = []()
    {
        const char *value = std::getenv("HW_NATIVE_GPU_FIXED_FUNCTION");
        return value == nullptr || value[0] == '\0' || value[0] != '0';
    }();
    return enabled;
}

void storeMatrixRows(const Mat4 &matrix, float outRows[16])
{
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            outRows[row * 4 + column] = matrix.m[column * 4 + row];
}

GpuFixedFunctionState captureGpuFixedFunctionState()
{
    GpuFixedFunctionState state = {};
    storeMatrixRows(g.modelview, state.modelViewRows);
    const Mat4 mvp = multiply(g.projection, g.modelview);
    storeMatrixRows(mvp, state.mvpRows);
    std::memcpy(state.materialAmbient, g.materialAmbient, sizeof(state.materialAmbient));
    std::memcpy(state.materialDiffuse, g.materialDiffuse, sizeof(state.materialDiffuse));
    std::memcpy(state.materialEmission, g.materialEmission, sizeof(state.materialEmission));
    std::memcpy(state.lightModelAmbient, g.lightModelAmbient, sizeof(state.lightModelAmbient));
    std::memcpy(state.clipEquationEye, g.clipEquationEye, sizeof(state.clipEquationEye));
    state.fogParameters[0] = g.fogDensity;
    state.fogParameters[1] = g.fogStart;
    state.fogParameters[2] = g.fogEnd;
    state.flags[0] = g.lighting ? 1u : 0u;
    state.flags[1] = g.colorMaterial ? 1u : 0u;
    state.flags[2] = static_cast<uint32_t>(g.fogMode);
    state.flags[3] = g.clipPlane0 ? 1u : 0u;
    for (unsigned int index = 0; index < 8u; ++index)
    {
        const LightState &light = g.lights[index];
        if (!light.enabled) continue;
        std::memcpy(state.lightPosition[index], light.position,
                    sizeof(state.lightPosition[index]));
        std::memcpy(state.lightAmbient[index], light.ambient,
                    sizeof(state.lightAmbient[index]));
        std::memcpy(state.lightDiffuse[index], light.diffuse,
                    sizeof(state.lightDiffuse[index]));
        state.lightAmbient[index][3] = 1.0f;
    }
    return state;
}

RasterVertex makeVertex(float x, float y, float z, float w)
{
    RasterVertex result = {};
    result.uv[0] = g.currentTexcoord[0];
    result.uv[1] = g.currentTexcoord[1];
    result.normal[0] = g.currentNormal[0];
    result.normal[1] = g.currentNormal[1];
    result.normal[2] = g.currentNormal[2];
    result.normal[3] = 0.0f;

    if (gpuFixedFunctionRuntimeEnabled())
    {
        result.position[0] = x;
        result.position[1] = y;
        result.position[2] = z;
        result.position[3] = w;
        std::memcpy(result.color, g.currentColor, sizeof(result.color));
        result.fogFactor = 1.0f;
        result.clipDistance = 1.0f;
        return result;
    }

    const std::array<float, 4> eye = transform(g.modelview, x, y, z, w);
    const std::array<float, 4> clip = transform(g.projection, eye[0], eye[1], eye[2], eye[3]);

    /* Exact legacy CPU fallback for parity A/B testing. */
    result.position[0] = clip[0];
    result.position[1] = -clip[1];
    result.position[2] = 0.5f * (clip[2] + clip[3]);
    result.position[3] = clip[3];
    shadeColor(eye, result.color);
    result.fogFactor = fogFactorForEyeVertex(eye);
    result.clipDistance = g.clipPlane0 ?
        (g.clipEquationEye[0] * eye[0] + g.clipEquationEye[1] * eye[1] +
         g.clipEquationEye[2] * eye[2] + g.clipEquationEye[3] * eye[3]) : 1.0f;
    return result;
}

void loadArrayElement(GLint index, bool emitVertex);

void appendExpanded(RasterCommand &command, GLenum mode,
                    const std::vector<RasterVertex> &source)
{
    const size_t first = gFrameVertices.size();
    auto appendDirect = [&]()
    {
        gFrameVertices.insert(gFrameVertices.end(), source.begin(), source.end());
    };
    auto addTriangle = [&](size_t a, size_t b, size_t c)
    {
        if (a < source.size() && b < source.size() && c < source.size())
        {
            gFrameVertices.push_back(source[a]);
            gFrameVertices.push_back(source[b]);
            gFrameVertices.push_back(source[c]);
        }
    };
    auto addLine = [&](size_t a, size_t b)
    {
        if (a < source.size() && b < source.size())
        {
            gFrameVertices.push_back(source[a]);
            gFrameVertices.push_back(source[b]);
        }
    };

    /* Keep native D3D topologies whenever they are semantically identical.
       RTX-0061 still CPU-expanded triangle/line strips into duplicated lists,
       multiplying transform storage, upload bandwidth and Draw vertex count.
       D3D12 natively supports these topologies, so preserve them directly. */
    switch (mode)
    {
        case GL_POINTS:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            appendDirect();
            break;
        case GL_LINES:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            appendDirect();
            break;
        case GL_LINE_STRIP:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            appendDirect();
            break;
        case GL_LINE_LOOP:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            for (size_t i = 1; i < source.size(); ++i) addLine(i - 1, i);
            if (source.size() > 2) addLine(source.size() - 1, 0);
            break;
        case GL_TRIANGLES:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            appendDirect();
            break;
        case GL_TRIANGLE_STRIP:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            appendDirect();
            break;
        case GL_TRIANGLE_FAN:
        case GL_POLYGON:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            for (size_t i = 2; i < source.size(); ++i) addTriangle(0, i - 1, i);
            break;
        case GL_QUADS:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            for (size_t i = 3; i < source.size(); i += 4)
            {
                const size_t base = i - 3;
                addTriangle(base + 0, base + 1, base + 2);
                addTriangle(base + 0, base + 2, base + 3);
            }
            break;
        default:
            setError(GL_INVALID_ENUM);
            break;
    }
    command.firstVertex = first;
    command.vertexCount = gFrameVertices.size() - first;
}

void configureDrawCommand(RasterCommand &command,
                          bool verticesArePretransformed)
{
    command.state = g.draw;
    command.gpuFixedFunctionEnabled =
        (!verticesArePretransformed && gpuFixedFunctionRuntimeEnabled()) ? 1u : 0u;
    if (command.gpuFixedFunctionEnabled != 0)
    {
        GpuFixedFunctionState fixedState = captureGpuFixedFunctionState();
        /* Consecutive texture/material runs often share every fixed-function
           input. Reuse the previous CBV block instead of uploading another
           624-byte state record when it is bit-identical. */
        if (gFrameFixedStates.size() > 1 &&
            std::memcmp(&gFrameFixedStates.back(), &fixedState,
                        sizeof(fixedState)) == 0)
        {
            command.fixedStateIndex = gFrameFixedStates.size() - 1;
        }
        else
        {
            command.fixedStateIndex = gFrameFixedStates.size();
            gFrameFixedStates.push_back(fixedState);
        }
    }
}

void recordDraw(GLenum mode, const std::vector<RasterVertex> &vertices,
                bool verticesArePretransformed = false)
{
    if (vertices.empty()) return;
    if (!gFrameRecording) gDetachedDirty = true;
    RasterCommand command;
    command.type = RasterCommand::Draw;
    configureDrawCommand(command, verticesArePretransformed);
    appendExpanded(command, mode, vertices);
    if (command.vertexCount != 0) gCommands.push_back(std::move(command));
}

bool directIndexedTopology(GLenum mode, RasterCommand &command)
{
    switch (mode)
    {
        case GL_POINTS:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            return true;
        case GL_LINES:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            return true;
        case GL_LINE_STRIP:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            return true;
        case GL_TRIANGLES:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            return true;
        case GL_TRIANGLE_STRIP:
            command.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            command.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            return true;
        default:
            return false;
    }
}

/* Record an indexed draw without expanding each referenced vertex.  We retain
   one decoded compatibility vertex for each unique source index used by this
   draw, remap the source index stream to that compact vertex block, and let
   D3D12's indexed input assembler / post-transform cache do the reuse. */
void recordIndexedDraw(GLenum mode, const uint32_t *sourceIndices,
                       size_t indexCount)
{
    if (sourceIndices == nullptr || indexCount == 0) return;
    if (!gFrameRecording) gDetachedDirty = true;

    RasterCommand command;
    command.type = RasterCommand::Draw;
    configureDrawCommand(command, false);

    /* Non-native GL primitive types still use the exact compatibility
       expansion path until they can be represented without semantic drift. */
    if (!directIndexedTopology(mode, command))
    {
        gImmediate.clear();
        if (gImmediate.capacity() < indexCount) gImmediate.reserve(indexCount);
        for (size_t i = 0; i < indexCount; ++i)
            loadArrayElement(static_cast<GLint>(sourceIndices[i]), true);
        appendExpanded(command, mode, gImmediate);
        gImmediate.clear();
        if (command.vertexCount != 0) gCommands.push_back(std::move(command));
        return;
    }

    command.firstVertex = gFrameVertices.size();
    command.firstIndex = gFrameIndices.size();

    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(std::min<size_t>(indexCount, 4096u));
    for (size_t i = 0; i < indexCount; ++i)
    {
        const uint32_t sourceIndex = sourceIndices[i];
        auto found = remap.find(sourceIndex);
        uint32_t compactIndex = 0;
        if (found == remap.end())
        {
            gImmediate.clear();
            loadArrayElement(static_cast<GLint>(sourceIndex), true);
            if (gImmediate.empty()) continue;
            compactIndex = static_cast<uint32_t>(
                gFrameVertices.size() - command.firstVertex);
            gFrameVertices.push_back(gImmediate.back());
            remap.emplace(sourceIndex, compactIndex);
        }
        else
        {
            compactIndex = found->second;
        }
        gFrameIndices.push_back(compactIndex);
    }
    gImmediate.clear();

    command.vertexCount = gFrameVertices.size() - command.firstVertex;
    command.indexCount = gFrameIndices.size() - command.firstIndex;
    if (command.vertexCount != 0 && command.indexCount != 0)
        gCommands.push_back(std::move(command));
}

void loadArrayElement(GLint index, bool emitVertex)
{
    if (g.colorArray.enabled)
    {
        const unsigned char *p = arrayAddress(g.colorArray, index);
        if (p)
        {
            g.currentColor[0] = componentAsFloat(p, g.colorArray.type, 0, true);
            g.currentColor[1] = componentAsFloat(p, g.colorArray.type, std::min(1, g.colorArray.size - 1), true);
            g.currentColor[2] = componentAsFloat(p, g.colorArray.type, std::min(2, g.colorArray.size - 1), true);
            g.currentColor[3] = g.colorArray.size >= 4 ? componentAsFloat(p, g.colorArray.type, 3, true) : 1.0f;
        }
    }
    if (g.texcoordArray.enabled)
    {
        const unsigned char *p = arrayAddress(g.texcoordArray, index);
        if (p)
        {
            g.currentTexcoord[0] = componentAsFloat(p, g.texcoordArray.type, 0, false);
            g.currentTexcoord[1] = g.texcoordArray.size >= 2 ? componentAsFloat(p, g.texcoordArray.type, 1, false) : 0.0f;
        }
    }
    if (g.normalArray.enabled)
    {
        const unsigned char *p = arrayAddress(g.normalArray, index);
        if (p)
        {
            g.currentNormal[0] = componentAsFloat(p, g.normalArray.type, 0, false);
            g.currentNormal[1] = componentAsFloat(p, g.normalArray.type, 1, false);
            g.currentNormal[2] = componentAsFloat(p, g.normalArray.type, 2, false);
        }
    }
    if (emitVertex && g.vertexArray.enabled)
    {
        const unsigned char *p = arrayAddress(g.vertexArray, index);
        if (p)
        {
            const float x = componentAsFloat(p, g.vertexArray.type, 0, false);
            const float y = g.vertexArray.size >= 2 ? componentAsFloat(p, g.vertexArray.type, 1, false) : 0.0f;
            const float z = g.vertexArray.size >= 3 ? componentAsFloat(p, g.vertexArray.type, 2, false) : 0.0f;
            const float w = g.vertexArray.size >= 4 ? componentAsFloat(p, g.vertexArray.type, 3, false) : 1.0f;
            gImmediate.push_back(makeVertex(x, y, z, w));
        }
    }
}

GLuint boundBufferForTarget(GLenum target)
{
    if (target == GL_ARRAY_BUFFER) return g.arrayBuffer;
    if (target == GL_ELEMENT_ARRAY_BUFFER) return g.elementBuffer;
    return 0;
}

TextureRecord &texture(GLuint name)
{
    auto found = gTextures.find(name);
    if (found != gTextures.end()) return found->second;
    TextureRecord record;
    record.name = name;
    record.descriptorSlot = name == 0 ? 0 : std::min<unsigned int>(name, kSrvCapacity - 1);
    if (name == 0)
    {
        /* D3D12 always needs a valid SRV bound even for untextured legacy
           draws.  Texture name zero is therefore a permanent 1x1 white
           compatibility resource; TextureEnabled still controls whether the
           pixel shader samples it. */
        record.width = 1;
        record.height = 1;
        record.pixels = {255, 255, 255, 255};
        record.dirty = true;
    }
    return gTextures.emplace(name, std::move(record)).first->second;
}

struct Bc7BitReader
{
    const unsigned char *bytes = nullptr;
    unsigned int bit = 0;

    uint32_t get(unsigned int count)
    {
        uint32_t value = 0;
        for (unsigned int index = 0; index < count; ++index, ++bit)
        {
            value |= static_cast<uint32_t>((bytes[bit >> 3] >> (bit & 7)) & 1u) << index;
        }
        return value;
    }
};

bool decodeBc7Mode6Block(const unsigned char *encoded, unsigned char rgba[16 * 4])
{
    static const int weights[16] = {
        0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    if (encoded == nullptr || rgba == nullptr) return false;
    Bc7BitReader reader;
    reader.bytes = encoded;
    if (reader.get(7) != 0x40u) return false; /* generated mission skies use mode 6 */

    unsigned int endpoint0[4] = {};
    unsigned int endpoint1[4] = {};
    for (unsigned int component = 0; component < 3; ++component)
    {
        endpoint0[component] = reader.get(7);
        endpoint1[component] = reader.get(7);
    }
    endpoint0[3] = reader.get(7);
    endpoint1[3] = reader.get(7);
    const unsigned int pbit0 = reader.get(1);
    const unsigned int pbit1 = reader.get(1);
    for (unsigned int component = 0; component < 4; ++component)
    {
        endpoint0[component] = (endpoint0[component] << 1) | pbit0;
        endpoint1[component] = (endpoint1[component] << 1) | pbit1;
    }

    unsigned int indices[16] = {};
    indices[0] = reader.get(3); /* anchor omits the redundant high bit */
    for (unsigned int index = 1; index < 16; ++index) indices[index] = reader.get(4);
    for (unsigned int pixel = 0; pixel < 16; ++pixel)
    {
        const int weight = weights[indices[pixel] & 15u];
        for (unsigned int component = 0; component < 4; ++component)
        {
            rgba[pixel * 4 + component] = static_cast<unsigned char>(
                ((64 - weight) * endpoint0[component] +
                 weight * endpoint1[component] + 32) >> 6);
        }
    }
    return true;
}

bool decodeBc7Mode6Mip(const CompressedMip &mip, std::vector<unsigned char> &rgba)
{
    if (mip.width == 0 || mip.height == 0 || mip.bytes.empty()) return false;
    const unsigned int blocksWide = std::max(1u, (mip.width + 3u) / 4u);
    const unsigned int blocksHigh = std::max(1u, (mip.height + 3u) / 4u);
    if (mip.bytes.size() < static_cast<size_t>(blocksWide) * blocksHigh * 16u) return false;
    rgba.assign(static_cast<size_t>(mip.width) * mip.height * 4u, 0);
    unsigned char blockRgba[16 * 4] = {};
    for (unsigned int blockY = 0; blockY < blocksHigh; ++blockY)
    {
        for (unsigned int blockX = 0; blockX < blocksWide; ++blockX)
        {
            const unsigned char *block = mip.bytes.data() +
                (static_cast<size_t>(blockY) * blocksWide + blockX) * 16u;
            if (!decodeBc7Mode6Block(block, blockRgba)) return false;
            for (unsigned int y = 0; y < 4; ++y)
            {
                const unsigned int destY = blockY * 4u + y;
                if (destY >= mip.height) continue;
                for (unsigned int x = 0; x < 4; ++x)
                {
                    const unsigned int destX = blockX * 4u + x;
                    if (destX >= mip.width) continue;
                    std::memcpy(rgba.data() +
                                    (static_cast<size_t>(destY) * mip.width + destX) * 4u,
                                blockRgba + (y * 4u + x) * 4u, 4u);
                }
            }
        }
    }
    return true;
}

void convertPixelsToRgba(TextureRecord &record, GLsizei width, GLsizei height,
                         GLenum format, GLenum type, const GLvoid *pixels)
{
    record.width = std::max(0, width);
    record.height = std::max(0, height);
    record.compressed = false;
    record.gpuFormat = kDefaultTextureFormat;
    record.pixels.assign(static_cast<size_t>(record.width) * record.height * 4u, 0);
    if (!pixels || type != GL_UNSIGNED_BYTE) return;
    const unsigned char *source = static_cast<const unsigned char *>(pixels);
    bool rgbFormat = format == GL_RGB;
    bool rgbaFormat = format == GL_RGBA;
#ifdef GL_RGB8
    rgbFormat = rgbFormat || format == GL_RGB8;
#endif
#ifdef GL_RGBA8
    rgbaFormat = rgbaFormat || format == GL_RGBA8;
#endif
#ifdef GL_RGBA16
    rgbaFormat = rgbaFormat || format == GL_RGBA16;
#endif
    unsigned int components = 4u;
    if (rgbFormat) components = 3u;
    else if (format == GL_LUMINANCE_ALPHA) components = 2u;
    else if (format == GL_ALPHA || format == GL_LUMINANCE) components = 1u;
    const unsigned int rowPixels = g.unpackRowLength > 0 ?
        static_cast<unsigned int>(g.unpackRowLength) : record.width;
    const size_t sourceRowBytesRaw = static_cast<size_t>(rowPixels) * components;
    const size_t alignment = static_cast<size_t>(std::max(1, g.unpackAlignment));
    const size_t sourceRowBytes = (sourceRowBytesRaw + alignment - 1u) & ~(alignment - 1u);
    for (unsigned int y = 0; y < record.height; ++y)
    {
        const unsigned char *row = source + static_cast<size_t>(y) * sourceRowBytes;
        unsigned char *dest = record.pixels.data() + static_cast<size_t>(y) * record.width * 4u;
        for (unsigned int x = 0; x < record.width; ++x)
        {
            if (rgbFormat)
            {
                dest[x * 4 + 0] = row[x * 3 + 0];
                dest[x * 4 + 1] = row[x * 3 + 1];
                dest[x * 4 + 2] = row[x * 3 + 2];
                dest[x * 4 + 3] = 255;
            }
            else if (rgbaFormat)
            {
                std::memcpy(dest + x * 4, row + x * 4, 4);
            }
#ifdef GL_BGRA
            else if (format == GL_BGRA)
            {
                dest[x * 4 + 0] = row[x * 4 + 2];
                dest[x * 4 + 1] = row[x * 4 + 1];
                dest[x * 4 + 2] = row[x * 4 + 0];
                dest[x * 4 + 3] = row[x * 4 + 3];
            }
#endif
            else if (format == GL_ALPHA)
            {
                dest[x * 4 + 0] = 255;
                dest[x * 4 + 1] = 255;
                dest[x * 4 + 2] = 255;
                dest[x * 4 + 3] = row[x];
            }
            else if (format == GL_LUMINANCE)
            {
                dest[x * 4 + 0] = dest[x * 4 + 1] = dest[x * 4 + 2] = row[x];
                dest[x * 4 + 3] = 255;
            }
            else if (format == GL_LUMINANCE_ALPHA)
            {
                dest[x * 4 + 0] = dest[x * 4 + 1] = dest[x * 4 + 2] = row[x * 2];
                dest[x * 4 + 3] = row[x * 2 + 1];
            }
        }
    }
}

D3D12_BLEND mapBlend(GLenum value)
{
    switch (value)
    {
        case GL_ZERO: return D3D12_BLEND_ZERO;
        case GL_ONE: return D3D12_BLEND_ONE;
        case GL_SRC_COLOR: return D3D12_BLEND_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR: return D3D12_BLEND_INV_SRC_COLOR;
        case GL_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
        case GL_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
        case GL_DST_COLOR: return D3D12_BLEND_DEST_COLOR;
        case GL_ONE_MINUS_DST_COLOR: return D3D12_BLEND_INV_DEST_COLOR;
        default: return D3D12_BLEND_ONE;
    }
}

D3D12_BLEND mapBlendAlpha(GLenum value)
{
    switch (value)
    {
        case GL_ZERO: return D3D12_BLEND_ZERO;
        case GL_ONE: return D3D12_BLEND_ONE;
        case GL_SRC_COLOR:
        case GL_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_COLOR:
        case GL_ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
        case GL_DST_COLOR:
        case GL_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
        case GL_ONE_MINUS_DST_COLOR:
        case GL_ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
        default: return D3D12_BLEND_ONE;
    }
}

D3D12_COMPARISON_FUNC mapCompare(GLenum value)
{
    switch (value)
    {
        case GL_NEVER: return D3D12_COMPARISON_FUNC_NEVER;
        case GL_LESS: return D3D12_COMPARISON_FUNC_LESS;
        case GL_EQUAL: return D3D12_COMPARISON_FUNC_EQUAL;
        case GL_LEQUAL: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case GL_GREATER: return D3D12_COMPARISON_FUNC_GREATER;
        case GL_NOTEQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case GL_GEQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case GL_ALWAYS: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_LESS;
    }
}

struct PipelineKey
{
    uint64_t bits = 0;
    bool operator==(const PipelineKey &other) const { return bits == other.bits; }
};
struct PipelineKeyHash
{
    size_t operator()(const PipelineKey &key) const { return static_cast<size_t>(key.bits); }
};

struct PersistentVertexUpload
{
    ComPtr<ID3D12Resource> resource;
    unsigned char *mapped = nullptr;
    UINT64 capacity = 0;
};

struct GpuRenderer
{
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> depth;
    D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    ComPtr<ID3D12PipelineState> missionSkyPipeline;
    UINT srvStride = 0;
    UINT samplerStride = 0;
    UINT rtvStride = 0;
    unsigned int rtvCursor = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int activeSlot = 0;
    std::unordered_map<PipelineKey, ComPtr<ID3D12PipelineState>, PipelineKeyHash> pipelines;
    std::vector<ComPtr<ID3D12Resource>> frameResources[kFrameSlots];
    PersistentVertexUpload vertexUploads[kFrameSlots];
    PersistentVertexUpload indexUploads[kFrameSlots];
    PersistentVertexUpload fixedStateUploads[kFrameSlots];
    /* Per-flight persistently mapped texture staging arena. Dirty legacy
       textures suballocate from this buffer instead of creating/mapping a new
       committed upload resource for every texture update. */
    PersistentVertexUpload textureUploads[kFrameSlots];
    UINT64 textureUploadOffsets[kFrameSlots] = {};
    bool ready = false;
};

GpuRenderer r;

void retainAcrossFlight(const ComPtr<ID3D12Resource> &resource)
{
    if (!resource) return;
    /* Legacy GL lets callers delete/redefine texture names immediately while
       previously submitted frames may still sample the old storage.  D3D12
       requires us to keep that resource alive explicitly until all three
       swap-chain flight slots have advanced past it. */
    for (unsigned int slot = 0; slot < kFrameSlots; ++slot)
        r.frameResources[slot].push_back(resource);
}

bool ensurePersistentVertexUpload(ID3D12Device *device, UINT64 requiredBytes,
                                  PersistentVertexUpload &upload);

bool reserveTextureUpload(ID3D12Device *device, UINT64 byteCount,
                          UINT64 &outOffset)
{
    const unsigned int slot = r.activeSlot % kFrameSlots;
    PersistentVertexUpload &upload = r.textureUploads[slot];
    UINT64 &cursor = r.textureUploadOffsets[slot];
    constexpr UINT64 placementAlignment =
        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    UINT64 offset = (cursor + placementAlignment - 1ull) &
                    ~(placementAlignment - 1ull);
    if (!upload.resource || !upload.mapped ||
        offset > upload.capacity || byteCount > upload.capacity - offset)
    {
        /* A command already recorded this frame may reference the previous
           arena allocation. Keep it alive until this flight slot's fence
           retires, then start a fresh arena for subsequent texture copies. */
        if (upload.resource)
        {
            r.frameResources[slot].push_back(upload.resource);
            if (upload.mapped) upload.resource->Unmap(0, nullptr);
            upload.mapped = nullptr;
            upload.resource.Reset();
            upload.capacity = 0;
        }

        const UINT64 requested = std::max<UINT64>(
            byteCount, 16ull * 1024ull * 1024ull);
        if (!ensurePersistentVertexUpload(device, requested, upload))
            return false;
        cursor = 0;
        offset = 0;
        ++gFrameTextureUploadArenaGrowths;
    }

    outOffset = offset;
    cursor = offset + byteCount;
    return true;
}

const char *legacyShader = R"(
Texture2D<float4> LegacyTexture : register(t0);
SamplerState LegacySampler : register(s0);
cbuffer DrawConstants : register(b0)
{
    uint TextureEnabled;
    uint TextureEnvMode;
    uint AlphaTestEnabled;
    uint AlphaFunction;
    float AlphaReference;
    uint FogEnabled;
    float BlendEnabled;
    float Padding0;
    float4 FogColor;
    uint GpuFixedFunctionEnabled;
    float3 Padding1;
};
cbuffer FixedFunctionConstants : register(b1)
{
    float4 ModelViewRow0;
    float4 ModelViewRow1;
    float4 ModelViewRow2;
    float4 ModelViewRow3;
    float4 MvpRow0;
    float4 MvpRow1;
    float4 MvpRow2;
    float4 MvpRow3;
    float4 MaterialAmbient;
    float4 MaterialDiffuse;
    float4 MaterialEmission;
    float4 LightModelAmbient;
    float4 ClipEquationEye;
    float4 FogParameters;
    uint4 FixedFlags;
    float4 LightPosition[8];
    float4 LightAmbient[8];
    float4 LightDiffuse[8];
};
struct VSInput
{
    float4 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float4 normal : NORMAL0;
    float fog : TEXCOORD1;
    float clip : TEXCOORD2;
};
struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float fog : TEXCOORD1;
    float clip : SV_ClipDistance0;
};
PSInput LegacyVS(VSInput input)
{
    PSInput output;
    if (GpuFixedFunctionEnabled != 0)
    {
        float4 eye = float4(
            dot(ModelViewRow0, input.position),
            dot(ModelViewRow1, input.position),
            dot(ModelViewRow2, input.position),
            dot(ModelViewRow3, input.position));
        float4 glClip = float4(
            dot(MvpRow0, input.position),
            dot(MvpRow1, input.position),
            dot(MvpRow2, input.position),
            dot(MvpRow3, input.position));
        output.position = float4(
            glClip.x,
            -glClip.y,
            0.5 * (glClip.z + glClip.w),
            glClip.w);

        float4 shaded = input.color;
        if (FixedFlags.x != 0u)
        {
            float3 normal = float3(
                dot(ModelViewRow0.xyz, input.normal.xyz),
                dot(ModelViewRow1.xyz, input.normal.xyz),
                dot(ModelViewRow2.xyz, input.normal.xyz));
            float normalLength2 = dot(normal, normal);
            if (normalLength2 > 0.000000000001)
                normal *= rsqrt(normalLength2);
            else
                normal = 0.0;
            float4 diffuseMaterial = FixedFlags.y != 0u ?
                input.color : MaterialDiffuse;
            float3 rgb = MaterialEmission.rgb +
                MaterialAmbient.rgb * LightModelAmbient.rgb;
            [unroll]
            for (uint lightIndex = 0u; lightIndex < 8u; ++lightIndex)
            {
                if (LightAmbient[lightIndex].w < 0.5) continue;
                float3 direction;
                if (abs(LightPosition[lightIndex].w) < 0.000001)
                    direction = LightPosition[lightIndex].xyz;
                else
                    direction = LightPosition[lightIndex].xyz - eye.xyz;
                float directionLength2 = dot(direction, direction);
                if (directionLength2 > 0.000000000001)
                    direction *= rsqrt(directionLength2);
                else
                    direction = 0.0;
                float ndotl = max(0.0, dot(normal, direction));
                rgb += MaterialAmbient.rgb * LightAmbient[lightIndex].rgb +
                    diffuseMaterial.rgb * LightDiffuse[lightIndex].rgb * ndotl;
            }
            shaded = float4(saturate(rgb), diffuseMaterial.a);
        }
        output.color = shaded;

        if (FogEnabled != 0u)
        {
            float distanceToEye = abs(eye.z);
            float fogFactor = 1.0;
            if (FixedFlags.z == 0x0801u) // GL_EXP2
            {
                float term = FogParameters.x * distanceToEye;
                fogFactor = exp(-(term * term));
            }
            else if (FixedFlags.z == 0x2601u) // GL_LINEAR
            {
                float denominator = FogParameters.z - FogParameters.y;
                fogFactor = abs(denominator) > 0.000001 ?
                    (FogParameters.z - distanceToEye) / denominator : 1.0;
            }
            else // GL_EXP/default
            {
                fogFactor = exp(-FogParameters.x * distanceToEye);
            }
            output.fog = saturate(fogFactor);
        }
        else
        {
            output.fog = 1.0;
        }
        output.clip = FixedFlags.w != 0u ? dot(ClipEquationEye, eye) : 1.0;
    }
    else
    {
        output.position = input.position;
        output.color = input.color;
        output.fog = input.fog;
        output.clip = input.clip;
    }
    output.uv = input.uv;
    return output;
}
bool AlphaPass(float a)
{
    if (AlphaFunction == 0x0200) return false;       // NEVER
    if (AlphaFunction == 0x0201) return a <  AlphaReference;
    if (AlphaFunction == 0x0202) return a == AlphaReference;
    if (AlphaFunction == 0x0203) return a <= AlphaReference;
    if (AlphaFunction == 0x0204) return a >  AlphaReference;
    if (AlphaFunction == 0x0205) return a != AlphaReference;
    if (AlphaFunction == 0x0206) return a >= AlphaReference;
    return true;                                    // ALWAYS
}
float4 LegacyPS(PSInput input) : SV_Target
{
    float4 result = input.color;
    if (TextureEnabled != 0)
    {
        float4 texel = LegacyTexture.Sample(LegacySampler, input.uv);
        if (TextureEnvMode == 0x1E01)               // REPLACE
            result = texel;
        else if (TextureEnvMode == 0x2101)          // DECAL
        {
            result.rgb = lerp(result.rgb, texel.rgb, texel.a);
        }
        else                                        // MODULATE/default
            result *= texel;
    }
    if (AlphaTestEnabled != 0 && !AlphaPass(result.a)) discard;
    if (FogEnabled != 0)
        result.rgb = lerp(FogColor.rgb, result.rgb, saturate(input.fog));
    /* The RGBA16F compatibility target uses alpha as an explicit foreground
       coverage channel for the presenter. Opaque/non-blended legacy draws
       cover the background completely even when their source texture carries
       incidental alpha values used only by alpha test. */
    if (BlendEnabled < 0.5)
        result.a = 1.0;
    return result;
}
)";

const char *missionSkyShader = R"(
Texture2D<float4> MissionSky : register(t0);
SamplerState MissionSkySampler : register(s0);
cbuffer MissionSkyConstants : register(b0)
{
    float4 ViewToWorldRow0; // xyz inverse view rotation row; w inverse projection X scale
    float4 ViewToWorldRow1; // xyz inverse view rotation row; w inverse projection Y scale
    float4 ViewToWorldRow2; // xyz inverse view rotation row; w fade
};

struct MissionSkyVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

MissionSkyVertexOutput MissionSkyVS(uint vertexId : SV_VertexID)
{
    MissionSkyVertexOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv.x * 2.0 - 1.0,
                             1.0 - uv.y * 2.0,
                             0.0, 1.0);
    return output;
}

float2 DualParaboloidUv(float3 direction, bool frontHemisphere)
{
    const float projectionExtent = 1.0625;
    float denominator = frontHemisphere
        ? (1.0 + direction.z)
        : (1.0 - direction.z);
    float2 plane = direction.xy / max(denominator, 1.0e-5);
    float2 localUv = plane / projectionExtent * 0.5 + 0.5;
    return float2((localUv.x + (frontHemisphere ? 0.0 : 1.0)) * 0.5,
                  localUv.y);
}

float3 SampleDualParaboloid(float3 direction)
{
    /* Each 2048x2048 half includes a 6.25% analytic continuation beyond the
       equator circle. That gives hardware bilinear filtering and BC6H blocks
       real neighboring spherical texels at the hemisphere boundary rather
       than clamp-color garbage. A narrow equator blend averages the two
       mathematically equivalent projections and hides independent block
       endpoint quantization. */
    const float blendBand = 0.035;
    float3 front = max(MissionSky.SampleLevel(
        MissionSkySampler, DualParaboloidUv(direction, true), 0.0).rgb, 0.0);
    float3 back = max(MissionSky.SampleLevel(
        MissionSkySampler, DualParaboloidUv(direction, false), 0.0).rgb, 0.0);
    if (direction.z >= blendBand)
        return front;
    if (direction.z <= -blendBand)
        return back;
    float frontWeight = smoothstep(-blendBand, blendBand, direction.z);
    return lerp(back, front, frontWeight);
}

float4 MissionSkyPS(MissionSkyVertexOutput input) : SV_Target
{
    /* The compatibility render target is intentionally bottom-up: ordinary
       GL vertices negate clip-space Y and the presenter flips raster V. Draw
       the fullscreen environment with the same convention so camera-up lands
       in the resource rows that present at the top of the screen. */
    float2 ndc = float2(input.uv.x * 2.0 - 1.0,
                        input.uv.y * 2.0 - 1.0);
    float3 viewDirection = normalize(float3(
        ndc.x * ViewToWorldRow0.w,
        ndc.y * ViewToWorldRow1.w,
        -1.0));
    float3 worldDirection = normalize(float3(
        dot(ViewToWorldRow0.xyz, viewDirection),
        dot(ViewToWorldRow1.xyz, viewDirection),
        dot(ViewToWorldRow2.xyz, viewDirection)));
    float3 radiance = SampleDualParaboloid(worldDirection);

    /* Alpha is not visual opacity in the native RGBA16F scene target. It is
       foreground coverage. The environment is the zero-coverage foundation;
       later legacy draws accumulate coverage over it. */
    return float4(radiance * saturate(ViewToWorldRow2.w), 0.0);
}
)";

bool compile(const char *entry, const char *target, ComPtr<ID3DBlob> &blob)
{
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(legacyShader, std::strlen(legacyShader),
                            "LegacyD3D12GL", nullptr, nullptr, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                            blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr))
    {
        if (errors) std::fprintf(stderr, "[NativeRaster] shader compile: %s\n",
                                 static_cast<const char *>(errors->GetBufferPointer()));
        return false;
    }
    return true;
}

bool compileMissionSky(const char *entry, const char *target,
                       ComPtr<ID3DBlob> &blob)
{
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(missionSkyShader, std::strlen(missionSkyShader),
                            "MissionSkyDualParaboloid", nullptr, nullptr, entry,
                            target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                            blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr))
    {
        if (errors) std::fprintf(stderr,
            "[NativeRaster] mission-sky shader compile: %s\n",
            static_cast<const char *>(errors->GetBufferPointer()));
        return false;
    }
    return true;
}

void resourceBarrier(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                     D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (!resource || before == after) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
}

bool createDepth(ID3D12Device *device, unsigned int width, unsigned int height)
{
    r.depth.Reset();
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = std::max(1u, width);
    desc.Height = std::max(1u, height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    /* Typeless storage lets the exact raster depth attachment also be sampled
       as R32_FLOAT by the volumetric presenter. The DSV remains D32_FLOAT. */
    desc.Format = kDepthResourceFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = kDepthDsvFormat;
    clear.DepthStencil.Depth = 1.0f;
    HRESULT hr = device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
        IID_PPV_ARGS(r.depth.GetAddressOf()));
    if (FAILED(hr)) return false;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
    dsv.Format = kDepthDsvFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(r.depth.Get(), &dsv,
        r.dsvHeap->GetCPUDescriptorHandleForHeapStart());
    r.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    r.width = width;
    r.height = height;
    return true;
}

bool ensureTextureGpu(ID3D12Device *device, ID3D12GraphicsCommandList *list,
                      TextureRecord &tex)
{
    if (tex.width == 0 || tex.height == 0 || tex.pixels.empty())
        return false;
    if (!tex.dirty && tex.gpu) return true;

    DXGI_FORMAT format = tex.gpuFormat;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = std::max(1u, tex.width);
    desc.Height = std::max(1u, tex.height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (!tex.gpu || tex.gpu->GetDesc().Width != desc.Width ||
        tex.gpu->GetDesc().Height != desc.Height || tex.gpu->GetDesc().Format != format)
    {
        if (tex.gpu) retainAcrossFlight(tex.gpu);
        tex.gpu.Reset();
        HRESULT hr = device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(tex.gpu.GetAddressOf()));
        if (FAILED(hr)) return false;
        tex.gpuState = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    else
    {
        resourceBarrier(list, tex.gpu.Get(), tex.gpuState,
                        D3D12_RESOURCE_STATE_COPY_DEST);
        tex.gpuState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint,
                                  &rows, &rowBytes, &totalBytes);
    UINT64 uploadOffset = 0;
    if (!reserveTextureUpload(device, totalBytes, uploadOffset)) return false;
    PersistentVertexUpload &upload = r.textureUploads[r.activeSlot];
    footprint.Offset = uploadOffset;
    unsigned char *mapped = upload.mapped;
    std::memset(mapped + uploadOffset, 0, static_cast<size_t>(totalBytes));
    if (tex.compressed)
    {
        const unsigned int blockRows = std::max(1u, (tex.height + 3u) / 4u);
        const size_t srcPitch = static_cast<size_t>(std::max(1u, (tex.width + 3u) / 4u)) * 16u;
        for (unsigned int y = 0; y < std::min<unsigned int>(rows, blockRows); ++y)
            std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                        tex.pixels.data() + static_cast<size_t>(y) * srcPitch,
                        std::min<size_t>(srcPitch, footprint.Footprint.RowPitch));
    }
    else
    {
        const size_t srcPitch = static_cast<size_t>(tex.width) * 4u;
        for (unsigned int y = 0; y < std::min<unsigned int>(rows, tex.height); ++y)
            std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                        tex.pixels.data() + static_cast<size_t>(y) * srcPitch,
                        std::min<size_t>(srcPitch, footprint.Footprint.RowPitch));
    }
    gFrameTextureUploadBytes += totalBytes;
    ++gFrameTextureUploadUpdates;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = tex.gpu.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    resourceBarrier(list, tex.gpu.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    tex.gpuState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = r.srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(tex.descriptorSlot) * r.srvStride;
    device->CreateShaderResourceView(tex.gpu.Get(), &srv, handle);
    tex.dirty = false;
    return true;
}

PipelineKey makePipelineKey(const RasterCommand &command)
{
    uint64_t bits = 0;
    bits |= static_cast<uint64_t>(command.topologyType & 7u);
    bits |= static_cast<uint64_t>(command.state.blend ? 1u : 0u) << 4;
    bits |= static_cast<uint64_t>(command.state.depthTest ? 1u : 0u) << 5;
    bits |= static_cast<uint64_t>(command.state.depthWrite ? 1u : 0u) << 6;
    bits |= static_cast<uint64_t>(command.state.cullFace ? 1u : 0u) << 7;
    bits |= static_cast<uint64_t>(command.state.polygonMode == GL_LINE ? 1u : 0u) << 8;
    bits |= static_cast<uint64_t>(command.state.blendSrc & 0xffffu) << 12;
    bits |= static_cast<uint64_t>(command.state.blendDst & 0xffffu) << 28;
    bits |= static_cast<uint64_t>(command.state.depthFunc & 0xffu) << 44;
    bits |= static_cast<uint64_t>(command.state.cullMode & 0xffu) << 52;
    return {bits};
}

ID3D12PipelineState *pipelineFor(ID3D12Device *device, const RasterCommand &command)
{
    const PipelineKey key = makePipelineKey(command);
    auto found = r.pipelines.find(key);
    if (found != r.pipelines.end()) return found->second.Get();

    ComPtr<ID3DBlob> vs, ps;
    if (!compile("LegacyVS", "vs_5_0", vs) || !compile("LegacyPS", "ps_5_0", ps))
        return nullptr;
    D3D12_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         static_cast<UINT>(offsetof(RasterVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         static_cast<UINT>(offsetof(RasterVertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         static_cast<UINT>(offsetof(RasterVertex, uv)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         static_cast<UINT>(offsetof(RasterVertex, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0,
         static_cast<UINT>(offsetof(RasterVertex, fogFactor)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0,
         static_cast<UINT>(offsetof(RasterVertex, clipDistance)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = r.root.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.InputLayout = {elements, static_cast<UINT>(_countof(elements))};
    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = command.topologyType;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kRasterTargetFormat;
    desc.DSVFormat = kDepthDsvFormat;
    desc.SampleDesc.Count = 1;

    desc.RasterizerState.FillMode = command.state.polygonMode == GL_LINE ?
        D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    if (command.state.cullFace)
    {
        if (command.state.cullMode == GL_FRONT) desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
        else if (command.state.cullMode == GL_BACK) desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    }
    /* Y is flipped into the bottom-up compatibility target, so a GL CCW front
       face becomes clockwise in D3D raster space. */
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthClipEnable = TRUE;

    D3D12_RENDER_TARGET_BLEND_DESC &blend = desc.BlendState.RenderTarget[0];
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.BlendEnable = command.state.blend ? TRUE : FALSE;
    blend.SrcBlend = mapBlend(command.state.blendSrc);
    blend.DestBlend = mapBlend(command.state.blendDst);
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    /* RGB obeys the exact legacy blend function. Alpha is reserved as a
       foreground-coverage channel for the HDR presenter:
         - normal alpha sprites accumulate coverage,
         - additive FX leave existing coverage unchanged.
       This lets the presenter convert only the still-visible HDR sky beneath
       translucent content, eliminating opaque rectangular particle quads. */
    if (command.state.blend && command.state.blendDst == GL_ONE)
    {
        blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
        blend.DestBlendAlpha = D3D12_BLEND_ONE;
    }
    else if (command.state.blend &&
             command.state.blendSrc == GL_SRC_ALPHA &&
             command.state.blendDst == GL_ONE_MINUS_SRC_ALPHA)
    {
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    }
    else
    {
        blend.SrcBlendAlpha = mapBlendAlpha(command.state.blendSrc);
        blend.DestBlendAlpha = mapBlendAlpha(command.state.blendDst);
    }
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    desc.DepthStencilState.DepthEnable = command.state.depthTest ? TRUE : FALSE;
    desc.DepthStencilState.DepthWriteMask = command.state.depthWrite ?
        D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = mapCompare(command.state.depthFunc);
    desc.DepthStencilState.StencilEnable = FALSE;

    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso.GetAddressOf()))))
        return nullptr;
    ID3D12PipelineState *raw = pso.Get();
    r.pipelines.emplace(key, std::move(pso));
    return raw;
}

ID3D12PipelineState *missionSkyPipelineFor(ID3D12Device *device)
{
    if (r.missionSkyPipeline) return r.missionSkyPipeline.Get();

    ComPtr<ID3DBlob> vs, ps;
    if (!compileMissionSky("MissionSkyVS", "vs_5_0", vs) ||
        !compileMissionSky("MissionSkyPS", "ps_5_0", ps))
        return nullptr;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = r.root.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.InputLayout = {nullptr, 0};
    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kRasterTargetFormat;
    desc.DSVFormat = kDepthDsvFormat;
    desc.SampleDesc.Count = 1;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.RasterizerState.MultisampleEnable = FALSE;
    desc.RasterizerState.AntialiasedLineEnable = FALSE;
    desc.RasterizerState.ForcedSampleCount = 0;
    desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_RENDER_TARGET_BLEND_DESC &blend = desc.BlendState.RenderTarget[0];
    blend.BlendEnable = FALSE;
    blend.LogicOpEnable = FALSE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;

    if (FAILED(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(r.missionSkyPipeline.GetAddressOf()))))
        return nullptr;
    return r.missionSkyPipeline.Get();
}

unsigned int samplerIndex(const TextureRecord &tex)
{
    const bool linear = tex.magFilter != GL_NEAREST && tex.minFilter != GL_NEAREST;
    const bool clamp = tex.wrapS != GL_REPEAT || tex.wrapT != GL_REPEAT;
    return (linear ? 2u : 0u) | (clamp ? 1u : 0u);
}

bool ensurePersistentVertexUpload(ID3D12Device *device, UINT64 requiredBytes,
                                  PersistentVertexUpload &upload)
{
    if (requiredBytes == 0) return true;
    if (upload.resource && upload.mapped && upload.capacity >= requiredBytes)
        return true;

    if (upload.resource && upload.mapped)
        upload.resource->Unmap(0, nullptr);
    upload.mapped = nullptr;
    upload.resource.Reset();

    UINT64 newCapacity = std::max<UINT64>(requiredBytes, 1024ull * 1024ull);
    if (upload.capacity > 0)
        newCapacity = std::max<UINT64>(newCapacity, upload.capacity * 2ull);
    newCapacity = (newCapacity + 65535ull) & ~65535ull;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = newCapacity;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(upload.resource.GetAddressOf()))))
        return false;

    D3D12_RANGE noRead = {0, 0};
    if (FAILED(upload.resource->Map(
            0, &noRead, reinterpret_cast<void **>(&upload.mapped))))
    {
        upload.resource.Reset();
        return false;
    }
    upload.capacity = newCapacity;
    return true;
}

struct DrawConstants
{
    uint32_t textureEnabled;
    uint32_t textureEnvMode;
    uint32_t alphaTestEnabled;
    uint32_t alphaFunction;
    float alphaReference;
    uint32_t fogEnabled;
    float padding[2];
    float fogColor[4];
    uint32_t gpuFixedFunctionEnabled;
    float padding1[3];
};
static_assert(sizeof(DrawConstants) == 64, "legacy draw constant size");

} // namespace

namespace hwlegacyd3d12
{
bool initialize(ID3D12Device *device, unsigned int width, unsigned int height)
{
    if (!device) return false;
    if (r.ready) return resize(device, width, height);

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = kSrvCapacity;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(r.srvHeap.GetAddressOf())))) return false;
    r.srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC samplerDesc = {};
    samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerDesc.NumDescriptors = 4;
    samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(r.samplerHeap.GetAddressOf())))) return false;
    r.samplerStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    for (unsigned int index = 0; index < 4; ++index)
    {
        D3D12_SAMPLER_DESC sampler = {};
        sampler.Filter = (index & 2u) ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW =
            (index & 1u) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = r.samplerHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * r.samplerStride;
        device->CreateSampler(&sampler, handle);
    }

    D3D12_DESCRIPTOR_HEAP_DESC one = {};
    /* RTV descriptors are referenced by recorded command lists until their
       flight slot retires.  Give every raster snapshot in each swap-chain
       slot its own descriptor instead of overwriting a single RTV descriptor
       four times before ExecuteCommandLists. */
    one.NumDescriptors = kFrameSlots * 4u;
    one.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(device->CreateDescriptorHeap(&one, IID_PPV_ARGS(r.rtvHeap.GetAddressOf())))) return false;
    r.rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    one.NumDescriptors = 1;
    one.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(device->CreateDescriptorHeap(&one, IID_PPV_ARGS(r.dsvHeap.GetAddressOf())))) return false;

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER params[4] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.Num32BitValues = 16;
    params[2].Constants.ShaderRegister = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].Descriptor.ShaderRegister = 1;
    params[3].Descriptor.RegisterSpace = 0;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 4;
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           signature.GetAddressOf(), error.GetAddressOf())))
        return false;
    if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(r.root.GetAddressOf())))) return false;

    TextureRecord white;
    white.name = 0;
    white.width = white.height = 1;
    white.descriptorSlot = 0;
    white.pixels = {255, 255, 255, 255};
    white.dirty = true;
    gTextures[0] = std::move(white);
    r.ready = createDepth(device, width, height);
    if (r.ready)
    {
        std::fprintf(stderr, "[NativeRaster] D3D12 fixed-function compatibility rasterizer initialized; OpenGL is not used.\n");
        std::fprintf(stderr,
            "[NativeRaster] GPU fixed-function vertex processing %s "
            "(set HW_NATIVE_GPU_FIXED_FUNCTION=0 for CPU parity fallback).\n",
            gpuFixedFunctionRuntimeEnabled() ? "ACTIVE" : "DISABLED");
        std::fprintf(stderr, "[NativeRaster] EXPERIMENTAL parity: framebuffer screenshot readback, FBO paths, line stipple/width, point-size fidelity, and generated texture mip chains are not complete yet.\n");
    }
    return r.ready;
}

void shutdown()
{
    for (unsigned int slot = 0; slot < kFrameSlots; ++slot)
    {
        if (r.vertexUploads[slot].resource && r.vertexUploads[slot].mapped)
            r.vertexUploads[slot].resource->Unmap(0, nullptr);
        r.vertexUploads[slot].mapped = nullptr;
        if (r.indexUploads[slot].resource && r.indexUploads[slot].mapped)
            r.indexUploads[slot].resource->Unmap(0, nullptr);
        r.indexUploads[slot].mapped = nullptr;
        if (r.fixedStateUploads[slot].resource && r.fixedStateUploads[slot].mapped)
            r.fixedStateUploads[slot].resource->Unmap(0, nullptr);
        r.fixedStateUploads[slot].mapped = nullptr;
        if (r.textureUploads[slot].resource && r.textureUploads[slot].mapped)
            r.textureUploads[slot].resource->Unmap(0, nullptr);
        r.textureUploads[slot].mapped = nullptr;
    }
    r = GpuRenderer{};
    for (auto &entry : gTextures) entry.second.gpu.Reset();
}

bool resize(ID3D12Device *device, unsigned int width, unsigned int height)
{
    if (!r.ready || r.width != width || r.height != height)
        return createDepth(device, width, height);
    return true;
}

void beginFrame(unsigned int frameSlot)
{
    r.activeSlot = frameSlot % kFrameSlots;
    r.rtvCursor = r.activeSlot * 4u;
    releaseFrameResources(r.activeSlot);
    r.textureUploadOffsets[r.activeSlot] = 0;
    gFrameTextureUploadBytes = 0;
    gFrameTextureUploadUpdates = 0;
    gFrameTextureUploadArenaGrowths = 0;
}

void releaseFrameResources(unsigned int frameSlot)
{
    r.frameResources[frameSlot % kFrameSlots].clear();
}

bool renderFrameSnapshots(ID3D12Device *device,
                          ID3D12GraphicsCommandList *list,
                          ID3D12Resource *finalTarget,
                          D3D12_RESOURCE_STATES finalBeforeState,
                          D3D12_RESOURCE_STATES finalAfterState,
                          unsigned int width,
                          unsigned int height,
                          size_t finalCommandCount,
                          const SnapshotCopyTarget *copies,
                          size_t copyCount)
{
    if (!r.ready && !initialize(device, width, height)) return false;
    if (!resize(device, width, height)) return false;
    if (!device || !list || !finalTarget) return false;

    if (r.depth && r.depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
    {
        resourceBarrier(list, r.depth.Get(), r.depthState,
                        D3D12_RESOURCE_STATE_DEPTH_WRITE);
        r.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    const auto cpuStarted = std::chrono::steady_clock::now();
    finalCommandCount = std::min(finalCommandCount, gCommands.size());

    resourceBarrier(list, finalTarget, finalBeforeState,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = kRasterTargetFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = r.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    const unsigned int slotBegin = r.activeSlot * 4u;
    const unsigned int slotEnd = slotBegin + 4u;
    if (r.rtvCursor < slotBegin || r.rtvCursor >= slotEnd) r.rtvCursor = slotBegin;
    rtv.ptr += static_cast<SIZE_T>(r.rtvCursor++) * r.rtvStride;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = r.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(finalTarget, &rtvDesc, rtv);
    list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const float black[4] = {0, 0, 0, 0};
    list->ClearRenderTargetView(rtv, black, 0, nullptr);
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    ID3D12DescriptorHeap *heaps[] = {r.srvHeap.Get(), r.samplerHeap.Get()};
    list->SetDescriptorHeaps(2, heaps);
    list->SetGraphicsRootSignature(r.root.Get());

    /* RTX-0061: pack the complete legacy frame exactly once into a persistent
       per-flight-slot upload buffer.  RTX-0058 rebuilt/mapped/copied a fresh
       committed upload resource for every raster snapshot, so the final,
       world, background and occluder captures repeated almost the same CPU
       work four times.  The owning swap-chain slot is fenced before beginFrame
       reuses it, making this persistently mapped ring safe without per-frame
       resource creation. */
    /* RTX-AAA: the complete frame now already lives in one contiguous retained
       CPU arena. Copy it to the persistently mapped flight-slot upload buffer
       in one memcpy instead of walking every command and copying separate
       std::vector allocations. */
    const UINT64 totalVertices = static_cast<UINT64>(gFrameVertices.size());
    const UINT64 totalVertexBytes = totalVertices * sizeof(RasterVertex);
    UINT64 drawCommands = 0;
    UINT64 gpuFixedFunctionVertices = 0;
    for (size_t index = 0; index < finalCommandCount; ++index)
    {
        if (gCommands[index].type == RasterCommand::Draw &&
            gCommands[index].vertexCount != 0)
        {
            ++drawCommands;
            if (gCommands[index].gpuFixedFunctionEnabled != 0)
                gpuFixedFunctionVertices += gCommands[index].vertexCount;
        }
    }

    PersistentVertexUpload &vertexUpload = r.vertexUploads[r.activeSlot];
    if (!ensurePersistentVertexUpload(device, totalVertexBytes, vertexUpload))
        return false;
    if (totalVertexBytes > 0)
        std::memcpy(vertexUpload.mapped, gFrameVertices.data(),
                    static_cast<size_t>(totalVertexBytes));

    const UINT64 totalIndices = static_cast<UINT64>(gFrameIndices.size());
    const UINT64 totalIndexBytes = totalIndices * sizeof(uint32_t);
    PersistentVertexUpload &indexUpload = r.indexUploads[r.activeSlot];
    if (!ensurePersistentVertexUpload(device, totalIndexBytes, indexUpload))
        return false;
    if (totalIndexBytes > 0)
        std::memcpy(indexUpload.mapped, gFrameIndices.data(),
                    static_cast<size_t>(totalIndexBytes));

    const UINT64 fixedStateStride =
        (sizeof(GpuFixedFunctionState) + 255ull) & ~255ull;
    const UINT64 totalFixedStateBytes =
        static_cast<UINT64>(gFrameFixedStates.size()) * fixedStateStride;
    PersistentVertexUpload &fixedStateUpload =
        r.fixedStateUploads[r.activeSlot];
    if (!ensurePersistentVertexUpload(
            device, totalFixedStateBytes, fixedStateUpload))
        return false;
    if (fixedStateUpload.mapped && totalFixedStateBytes > 0)
    {
        std::memset(fixedStateUpload.mapped, 0,
                    static_cast<size_t>(totalFixedStateBytes));
        for (size_t stateIndex = 0;
             stateIndex < gFrameFixedStates.size(); ++stateIndex)
        {
            std::memcpy(fixedStateUpload.mapped +
                    static_cast<UINT64>(stateIndex) * fixedStateStride,
                &gFrameFixedStates[stateIndex],
                sizeof(GpuFixedFunctionState));
        }
    }

    std::vector<unsigned char> copied(copyCount, 0);
    auto copySnapshotsAt = [&](size_t processedCommands)
    {
        bool needsCopy = false;
        for (size_t copyIndex = 0; copyIndex < copyCount; ++copyIndex)
        {
            if (!copied[copyIndex] && copies[copyIndex].target &&
                std::min(copies[copyIndex].commandCount, finalCommandCount) == processedCommands)
            {
                needsCopy = true;
                break;
            }
        }
        if (!needsCopy) return;

        resourceBarrier(list, finalTarget,
                        D3D12_RESOURCE_STATE_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_COPY_SOURCE);
        for (size_t copyIndex = 0; copyIndex < copyCount; ++copyIndex)
        {
            if (copied[copyIndex] || !copies[copyIndex].target ||
                std::min(copies[copyIndex].commandCount, finalCommandCount) != processedCommands)
                continue;
            if (copies[copyIndex].target == finalTarget)
            {
                copied[copyIndex] = 1;
                continue;
            }
            resourceBarrier(list, copies[copyIndex].target,
                            copies[copyIndex].beforeState,
                            D3D12_RESOURCE_STATE_COPY_DEST);
            list->CopyResource(copies[copyIndex].target, finalTarget);
            resourceBarrier(list, copies[copyIndex].target,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            copies[copyIndex].afterState);
            copied[copyIndex] = 1;
        }
        resourceBarrier(list, finalTarget,
                        D3D12_RESOURCE_STATE_COPY_SOURCE,
                        D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    };

    copySnapshotsAt(0);
    for (size_t index = 0; index < finalCommandCount; ++index)
    {
        const RasterCommand &command = gCommands[index];
        if (command.type == RasterCommand::Clear)
        {
            D3D12_RECT rect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
            if (command.state.scissor)
            {
                const GLint *sbox = command.state.scissorBox;
                rect.left = std::max<LONG>(0, sbox[0]);
                rect.right = std::min<LONG>(static_cast<LONG>(width), sbox[0] + sbox[2]);
                rect.top = std::max<LONG>(0, sbox[1]);
                rect.bottom = std::min<LONG>(static_cast<LONG>(height), sbox[1] + sbox[3]);
            }
            if (command.clearMask & GL_COLOR_BUFFER_BIT)
                list->ClearRenderTargetView(rtv, command.clearColor, 1, &rect);
            if (command.clearMask & GL_DEPTH_BUFFER_BIT)
                list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
                    command.clearDepth, 0, 1, &rect);
        }
        else if (command.type == RasterCommand::MissionSky)
        {
            ID3D12PipelineState *pso = missionSkyPipelineFor(device);
            auto foundTexture = gTextures.find(command.state.texture);
            if (pso && foundTexture != gTextures.end())
            {
                TextureRecord &tex = foundTexture->second;
                if (ensureTextureGpu(device, list, tex))
                {
                    list->SetPipelineState(pso);
                    D3D12_VIEWPORT viewport = {};
                    viewport.TopLeftX = static_cast<float>(command.state.viewport[0]);
                    viewport.TopLeftY = static_cast<float>(command.state.viewport[1]);
                    viewport.Width = static_cast<float>(std::max(1, command.state.viewport[2]));
                    viewport.Height = static_cast<float>(std::max(1, command.state.viewport[3]));
                    viewport.MinDepth = 0.0f;
                    viewport.MaxDepth = 1.0f;
                    list->RSSetViewports(1, &viewport);
                    D3D12_RECT scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
                    if (command.state.scissor)
                    {
                        scissor.left = std::max<LONG>(0, command.state.scissorBox[0]);
                        scissor.right = std::min<LONG>(static_cast<LONG>(width), command.state.scissorBox[0] + command.state.scissorBox[2]);
                        scissor.top = std::max<LONG>(0, command.state.scissorBox[1]);
                        scissor.bottom = std::min<LONG>(static_cast<LONG>(height), command.state.scissorBox[1] + command.state.scissorBox[3]);
                    }
                    list->RSSetScissorRects(1, &scissor);
                    D3D12_GPU_DESCRIPTOR_HANDLE srv = r.srvHeap->GetGPUDescriptorHandleForHeapStart();
                    srv.ptr += static_cast<UINT64>(tex.descriptorSlot) * r.srvStride;
                    D3D12_GPU_DESCRIPTOR_HANDLE sampler = r.samplerHeap->GetGPUDescriptorHandleForHeapStart();
                    sampler.ptr += static_cast<UINT64>(samplerIndex(tex)) * r.samplerStride;
                    list->SetGraphicsRootDescriptorTable(0, srv);
                    list->SetGraphicsRootDescriptorTable(1, sampler);
                    list->SetGraphicsRoot32BitConstants(2, 12, command.skyConstants, 0);
                    list->IASetVertexBuffers(0, 0, nullptr);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                }
            }
        }
        else if (command.vertexCount != 0)
        {
            ID3D12PipelineState *pso = pipelineFor(device, command);
            if (pso)
            {
                list->SetPipelineState(pso);

                D3D12_VIEWPORT viewport = {};
                viewport.TopLeftX = static_cast<float>(command.state.viewport[0]);
                viewport.TopLeftY = static_cast<float>(command.state.viewport[1]);
                viewport.Width = static_cast<float>(std::max(1, command.state.viewport[2]));
                viewport.Height = static_cast<float>(std::max(1, command.state.viewport[3]));
                viewport.MinDepth = 0.0f;
                viewport.MaxDepth = 1.0f;
                list->RSSetViewports(1, &viewport);
                D3D12_RECT scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
                if (command.state.scissor)
                {
                    scissor.left = std::max<LONG>(0, command.state.scissorBox[0]);
                    scissor.right = std::min<LONG>(static_cast<LONG>(width), command.state.scissorBox[0] + command.state.scissorBox[2]);
                    scissor.top = std::max<LONG>(0, command.state.scissorBox[1]);
                    scissor.bottom = std::min<LONG>(static_cast<LONG>(height), command.state.scissorBox[1] + command.state.scissorBox[3]);
                }
                list->RSSetScissorRects(1, &scissor);

                TextureRecord *texPtr = &texture(command.state.texture2D ? command.state.texture : 0);
                if (texPtr->width == 0 || texPtr->height == 0 || texPtr->pixels.empty())
                    texPtr = &texture(0);
                TextureRecord &tex = *texPtr;
                if (ensureTextureGpu(device, list, tex))
                {
                    D3D12_GPU_DESCRIPTOR_HANDLE srv = r.srvHeap->GetGPUDescriptorHandleForHeapStart();
                    srv.ptr += static_cast<UINT64>(tex.descriptorSlot) * r.srvStride;
                    D3D12_GPU_DESCRIPTOR_HANDLE sampler = r.samplerHeap->GetGPUDescriptorHandleForHeapStart();
                    sampler.ptr += static_cast<UINT64>(samplerIndex(tex)) * r.samplerStride;
                    list->SetGraphicsRootDescriptorTable(0, srv);
                    list->SetGraphicsRootDescriptorTable(1, sampler);
                    DrawConstants constants = {};
                    constants.textureEnabled = command.state.texture2D ? 1u : 0u;
                    constants.textureEnvMode = static_cast<uint32_t>(command.state.texEnvMode);
                    constants.alphaTestEnabled = command.state.alphaTest ? 1u : 0u;
                    constants.alphaFunction = static_cast<uint32_t>(command.state.alphaFunc);
                    constants.alphaReference = command.state.alphaRef;
                    constants.fogEnabled = command.state.fog ? 1u : 0u;
                    constants.padding[0] = command.state.blend ? 1.0f : 0.0f;
                    std::memcpy(constants.fogColor, command.state.fogColor, sizeof(constants.fogColor));
                    constants.gpuFixedFunctionEnabled =
                        command.gpuFixedFunctionEnabled;
                    list->SetGraphicsRoot32BitConstants(2, 16, &constants, 0);
                    if (fixedStateUpload.resource)
                    {
                        const size_t safeStateIndex =
                            command.gpuFixedFunctionEnabled != 0 ?
                                command.fixedStateIndex : 0u;
                        list->SetGraphicsRootConstantBufferView(
                            3, fixedStateUpload.resource->GetGPUVirtualAddress() +
                               static_cast<UINT64>(safeStateIndex) *
                                   fixedStateStride);
                    }

                    const UINT64 bytes = static_cast<UINT64>(command.vertexCount) * sizeof(RasterVertex);
                    if (vertexUpload.resource && bytes > 0)
                    {
                        D3D12_VERTEX_BUFFER_VIEW view = {};
                        view.BufferLocation = vertexUpload.resource->GetGPUVirtualAddress() +
                            static_cast<UINT64>(command.firstVertex) * sizeof(RasterVertex);
                        view.SizeInBytes = static_cast<UINT>(bytes);
                        view.StrideInBytes = sizeof(RasterVertex);
                        list->IASetVertexBuffers(0, 1, &view);
                        list->IASetPrimitiveTopology(command.topology);
                        if (command.indexCount != 0 && indexUpload.resource)
                        {
                            D3D12_INDEX_BUFFER_VIEW indexView = {};
                            indexView.BufferLocation =
                                indexUpload.resource->GetGPUVirtualAddress() +
                                static_cast<UINT64>(command.firstIndex) *
                                    sizeof(uint32_t);
                            indexView.SizeInBytes = static_cast<UINT>(
                                command.indexCount * sizeof(uint32_t));
                            indexView.Format = DXGI_FORMAT_R32_UINT;
                            list->IASetIndexBuffer(&indexView);
                            list->DrawIndexedInstanced(
                                static_cast<UINT>(command.indexCount), 1, 0, 0, 0);
                        }
                        else
                        {
                            list->IASetIndexBuffer(nullptr);
                            list->DrawInstanced(
                                static_cast<UINT>(command.vertexCount), 1, 0, 0);
                        }
                    }
                }
            }
        }

        copySnapshotsAt(index + 1);
    }

    /* Defensive completion for a snapshot whose count was clamped to the final
       stream length. */
    copySnapshotsAt(finalCommandCount);
    resourceBarrier(list, finalTarget,
                    D3D12_RESOURCE_STATE_RENDER_TARGET, finalAfterState);

    static unsigned int telemetryFrames = 0;
    static double telemetryCpuMs = 0.0;
    static unsigned long long telemetryDraws = 0;
    static unsigned long long telemetryVertices = 0;
    static unsigned long long telemetryBytes = 0;
    static unsigned long long telemetryIndexBytes = 0;
    static unsigned long long telemetryCopies = 0;
    static unsigned long long telemetryFixedStateBytes = 0;
    static unsigned long long telemetryGpuFixedFunctionVertices = 0;
    static unsigned long long telemetryTextureUploadBytes = 0;
    static unsigned long long telemetryTextureUpdates = 0;
    static unsigned long long telemetryTextureArenaGrowths = 0;
    telemetryCpuMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cpuStarted).count();
    telemetryDraws += drawCommands;
    telemetryVertices += totalVertices;
    telemetryBytes += totalVertexBytes;
    telemetryIndexBytes += totalIndexBytes;
    telemetryCopies += copyCount;
    telemetryFixedStateBytes += totalFixedStateBytes;
    telemetryGpuFixedFunctionVertices += gpuFixedFunctionVertices;
    telemetryTextureUploadBytes += gFrameTextureUploadBytes;
    telemetryTextureUpdates += gFrameTextureUploadUpdates;
    telemetryTextureArenaGrowths += gFrameTextureUploadArenaGrowths;
    ++telemetryFrames;
    if (telemetryFrames >= 600)
    {
        const double divisor = static_cast<double>(telemetryFrames);
        std::fprintf(stderr,
            "[NativeRaster] single-pass telemetry (%u frames): CPU replay %.3f ms/frame, "
            "%.1f draws/frame, %.0f vertices/frame, %.1f%% GPU fixed-function, "
            "%.2f MiB vertex upload/frame, %.2f MiB index upload/frame, "
            "%.3f MiB fixed-state/frame, %.2f MiB texture staging/frame, "
            "%.1f texture updates/frame, %.3f upload-arena growths/frame, "
            "%.2f snapshot copies/frame.\n",
            telemetryFrames,
            telemetryCpuMs / divisor,
            static_cast<double>(telemetryDraws) / divisor,
            static_cast<double>(telemetryVertices) / divisor,
            telemetryVertices != 0 ?
                100.0 * static_cast<double>(telemetryGpuFixedFunctionVertices) /
                    static_cast<double>(telemetryVertices) : 0.0,
            static_cast<double>(telemetryBytes) / divisor / (1024.0 * 1024.0),
            static_cast<double>(telemetryIndexBytes) / divisor /
                (1024.0 * 1024.0),
            static_cast<double>(telemetryFixedStateBytes) / divisor /
                (1024.0 * 1024.0),
            static_cast<double>(telemetryTextureUploadBytes) / divisor /
                (1024.0 * 1024.0),
            static_cast<double>(telemetryTextureUpdates) / divisor,
            static_cast<double>(telemetryTextureArenaGrowths) / divisor,
            static_cast<double>(telemetryCopies) / divisor);
        telemetryFrames = 0;
        telemetryCpuMs = 0.0;
        telemetryDraws = telemetryVertices = telemetryBytes = telemetryCopies = 0;
        telemetryIndexBytes = 0;
        telemetryFixedStateBytes = 0;
        telemetryGpuFixedFunctionVertices = 0;
        telemetryTextureUploadBytes = 0;
        telemetryTextureUpdates = 0;
        telemetryTextureArenaGrowths = 0;
    }
    return true;
}

ID3D12Resource *depthResource()
{
    return r.depth.Get();
}

bool prepareDepthForSampling(ID3D12GraphicsCommandList *list)
{
    if (!list || !r.depth) return false;
    if (r.depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    {
        resourceBarrier(list, r.depth.Get(), r.depthState,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        r.depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    return true;
}
} // namespace hwlegacyd3d12

extern "C" void hwglResetAll(void)
{
    g = FixedState{};
    gCommands.clear();
    gFrameVertices.clear();
    gFrameIndices.clear();
    gFrameFixedStates.clear();
    gFrameFixedStates.resize(1);
    gImmediate.clear();
    gImmediateIndices.clear();
    gBuffers.clear();
    gTextures.clear();
    gTransientTextures.clear();
    gNextTexture = 1;
    gNextBuffer = 1;
    gSnapshots[0] = gSnapshots[1] = gSnapshots[2] = gSnapshots[3] = 0;
    gFrameRecording = false;
    gDetachedDirty = false;
}

extern "C" void hwglBeginFrame(unsigned int width, unsigned int height, unsigned int frameSlot)
{
    gFrameWidth = std::max(1u, width);
    gFrameHeight = std::max(1u, height);
    gFrameSlot = frameSlot % kFrameSlots;
    /* Standalone legacy loops issue their draw calls before rndFlush.  If that
       happened after the previous frame ended, keep those detached commands
       when the DXGI frame is opened at flush time. */
    if (!gDetachedDirty)
    {
        gCommands.clear();
        gFrameVertices.clear();
        gFrameIndices.clear();
        gFrameFixedStates.clear();
        gFrameFixedStates.resize(1);
    }
    if (gFrameFixedStates.empty()) gFrameFixedStates.resize(1);
    gImmediate.clear();
    gImmediateIndices.clear();
    gInBegin = false;
    gSnapshots[0] = gSnapshots[1] = gSnapshots[2] = gSnapshots[3] = 0;
    gFrameRecording = true;
    gDetachedDirty = false;

    /* RTX-0060: OpenGL normally acquires a full-window viewport as part of
       context creation/resizing.  The native backend has no GL context, so
       retaining FixedState's historical 640x480 bootstrap viewport made every
       raster snapshot occupy only a 640x480 rectangle inside a 2560x1440
       target.  DXR still rendered at full resolution, which is why the rest of
       the screen showed almost-black path-traced silhouettes/normals while a
       miniature correctly textured game appeared in the lower-left corner.

       Begin every owning DXGI frame with the real swap-chain extent, exactly as
       a normal glViewport(0,0,width,height) setup would.  Specialized passes
       (ShipView, UI clipping, etc.) remain free to call glViewport/glScissor
       later in the frame and their recorded draw commands retain those values. */
    g.draw.viewport[0] = 0;
    g.draw.viewport[1] = 0;
    g.draw.viewport[2] = static_cast<GLint>(gFrameWidth);
    g.draw.viewport[3] = static_cast<GLint>(gFrameHeight);

    /* Keep the default scissor rectangle coherent with the same framebuffer
       extent.  This only changes the stored box; GL_SCISSOR_TEST still controls
       whether it is active, and later region code may replace it normally. */
    g.draw.scissorBox[0] = 0;
    g.draw.scissorBox[1] = 0;
    g.draw.scissorBox[2] = static_cast<GLint>(gFrameWidth);
    g.draw.scissorBox[3] = static_cast<GLint>(gFrameHeight);

    hwlegacyd3d12::beginFrame(gFrameSlot);
}

extern "C" int hwglRecordMissionSky(GLuint textureName, GLfloat fade)
{
    if (!gFrameRecording || textureName == 0) return 0;
    auto found = gTextures.find(textureName);
    if (found == gTextures.end()) return 0;
    const TextureRecord &tex = found->second;
    if (!tex.compressed || tex.gpuFormat != DXGI_FORMAT_BC6H_UF16 ||
        tex.height == 0 || tex.width != tex.height * 2u)
        return 0;
    if (std::fabs(g.projection.m[0]) <= 1.0e-7f ||
        std::fabs(g.projection.m[5]) <= 1.0e-7f)
        return 0;

    RasterCommand command;
    command.type = RasterCommand::MissionSky;
    command.state = g.draw;
    command.state.texture2D = true;
    command.state.texture = textureName;
    command.state.blend = false;
    command.state.depthTest = false;
    command.state.depthWrite = false;
    command.state.cullFace = false;

    /* OpenGL's model-view matrix is column-major world->view. For a sky ray,
       translation is irrelevant and the inverse camera rotation is simply the
       transpose of the orthonormal upper 3x3. These rows intentionally match
       the long-standing god-ray world-direction projection math. */
    command.skyConstants[0] = g.modelview.m[0];
    command.skyConstants[1] = g.modelview.m[1];
    command.skyConstants[2] = g.modelview.m[2];
    command.skyConstants[3] = 1.0f / g.projection.m[0];
    command.skyConstants[4] = g.modelview.m[4];
    command.skyConstants[5] = g.modelview.m[5];
    command.skyConstants[6] = g.modelview.m[6];
    command.skyConstants[7] = 1.0f / g.projection.m[5];
    command.skyConstants[8] = g.modelview.m[8];
    command.skyConstants[9] = g.modelview.m[9];
    command.skyConstants[10] = g.modelview.m[10];
    command.skyConstants[11] = std::max(0.0f, std::min(fade, 1.0f));
    gCommands.push_back(std::move(command));
    return 1;
}


extern "C" void *hwglGetMissionSkyD3D12Resource(GLuint textureName)
{
    if (textureName == 0) return nullptr;
    auto found = gTextures.find(textureName);
    if (found == gTextures.end()) return nullptr;
    const TextureRecord &tex = found->second;
    if (!tex.gpu || !tex.compressed ||
        tex.gpuFormat != DXGI_FORMAT_BC6H_UF16 ||
        tex.height == 0 || tex.width != tex.height * 2u)
    {
        return nullptr;
    }
    return tex.gpu.Get();
}

extern "C" int hwglPrepareMissionSkyForCompute(GLuint textureName,
                                                   void *commandList)
{
    if (textureName == 0 || commandList == nullptr) return 0;
    auto found = gTextures.find(textureName);
    if (found == gTextures.end()) return 0;
    TextureRecord &tex = found->second;
    if (!tex.gpu || !tex.compressed ||
        tex.gpuFormat != DXGI_FORMAT_BC6H_UF16 ||
        tex.height == 0 || tex.width != tex.height * 2u)
    {
        return 0;
    }
    ID3D12GraphicsCommandList *list =
        static_cast<ID3D12GraphicsCommandList *>(commandList);
    const D3D12_RESOURCE_STATES readableState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (tex.gpuState != readableState)
    {
        resourceBarrier(list, tex.gpu.Get(), tex.gpuState, readableState);
        tex.gpuState = readableState;
    }
    return 1;
}

extern "C" void hwglMarkSnapshot(int snapshot)
{
    if (snapshot >= 0 && snapshot < 4) gSnapshots[snapshot] = gCommands.size();
}
extern "C" size_t hwglSnapshotCommandCount(int snapshot)
{
    return snapshot >= 0 && snapshot < 4 ? gSnapshots[snapshot] : 0;
}
extern "C" void hwglEndFrame(void)
{
    gFrameRecording = false;
    gDetachedDirty = false;
    for (GLuint id : gTransientTextures)
    {
        auto found = gTextures.find(id);
        if (found != gTextures.end())
        {
            retainAcrossFlight(found->second.gpu);
            gTextures.erase(found);
        }
    }
    gTransientTextures.clear();
    gCommands.clear();
    gFrameVertices.clear();
    gFrameIndices.clear();
    gFrameFixedStates.clear();
    gFrameFixedStates.resize(1);
    gImmediate.clear();
    gImmediateIndices.clear();
    gInBegin = false;
    gSnapshots[0] = gSnapshots[1] = gSnapshots[2] = gSnapshots[3] = 0;
}

extern "C" void hwglAlphaFunc(GLenum func, GLclampf ref) { g.draw.alphaFunc = func; g.draw.alphaRef = ref; }
extern "C" void hwglBegin(GLenum mode) { if (gInBegin) { setError(GL_INVALID_OPERATION); return; } gInBegin = true; gImmediateMode = mode; gImmediate.clear(); }
extern "C" void hwglEnd(void) { if (!gInBegin) { setError(GL_INVALID_OPERATION); return; } recordDraw(gImmediateMode, gImmediate); gImmediate.clear(); gInBegin = false; }
extern "C" void hwglVertex2f(GLfloat x, GLfloat y) { if (gInBegin) gImmediate.push_back(makeVertex(x, y, 0.0f, 1.0f)); }
extern "C" void hwglVertex3f(GLfloat x, GLfloat y, GLfloat z) { if (gInBegin) gImmediate.push_back(makeVertex(x, y, z, 1.0f)); }
extern "C" void hwglVertex3fv(const GLfloat *v) { if (v) hwglVertex3f(v[0], v[1], v[2]); }
extern "C" void hwglVertex4fv(const GLfloat *v) { if (v && gInBegin) gImmediate.push_back(makeVertex(v[0], v[1], v[2], v[3])); }
extern "C" void hwglColor3f(GLfloat r0, GLfloat g0, GLfloat b0) { hwglColor4f(r0, g0, b0, 1.0f); }
extern "C" void hwglColor4f(GLfloat r0, GLfloat g0, GLfloat b0, GLfloat a0) { g.currentColor[0]=r0; g.currentColor[1]=g0; g.currentColor[2]=b0; g.currentColor[3]=a0; }
extern "C" void hwglColor3ub(GLubyte r0, GLubyte g0, GLubyte b0) { hwglColor4ub(r0,g0,b0,255); }
extern "C" void hwglColor4ub(GLubyte r0, GLubyte g0, GLubyte b0, GLubyte a0) { hwglColor4f(r0/255.0f,g0/255.0f,b0/255.0f,a0/255.0f); }
extern "C" void hwglNormal3f(GLfloat x, GLfloat y, GLfloat z) { g.currentNormal[0]=x; g.currentNormal[1]=y; g.currentNormal[2]=z; }
extern "C" void hwglNormal3fv(const GLfloat *v) { if(v) hwglNormal3f(v[0],v[1],v[2]); }
extern "C" void hwglTexCoord2f(GLfloat s, GLfloat t) { g.currentTexcoord[0]=s; g.currentTexcoord[1]=t; }

extern "C" void hwglEnable(GLenum cap)
{
    if (cap == GL_TEXTURE_2D) g.draw.texture2D = true;
    else if (cap == GL_BLEND) g.draw.blend = true;
    else if (cap == GL_ALPHA_TEST) g.draw.alphaTest = true;
    else if (cap == GL_DEPTH_TEST) g.draw.depthTest = true;
    else if (cap == GL_CULL_FACE) g.draw.cullFace = true;
    else if (cap == GL_SCISSOR_TEST) g.draw.scissor = true;
    else if (cap == GL_FOG) g.draw.fog = true;
    else if (cap == GL_LIGHTING) g.lighting = true;
    else if (cap == GL_NORMALIZE) g.normalize = true;
#ifdef GL_RESCALE_NORMAL
    else if (cap == GL_RESCALE_NORMAL) g.normalize = true;
#endif
    else if (cap == GL_COLOR_MATERIAL) g.colorMaterial = true;
    else if (cap == GL_CLIP_PLANE0) g.clipPlane0 = true;
    else if (cap >= GL_LIGHT0 && cap <= GL_LIGHT7) g.lights[cap - GL_LIGHT0].enabled = true;
}
extern "C" void hwglDisable(GLenum cap)
{
    if (cap == GL_TEXTURE_2D) g.draw.texture2D = false;
    else if (cap == GL_BLEND) g.draw.blend = false;
    else if (cap == GL_ALPHA_TEST) g.draw.alphaTest = false;
    else if (cap == GL_DEPTH_TEST) g.draw.depthTest = false;
    else if (cap == GL_CULL_FACE) g.draw.cullFace = false;
    else if (cap == GL_SCISSOR_TEST) g.draw.scissor = false;
    else if (cap == GL_FOG) g.draw.fog = false;
    else if (cap == GL_LIGHTING) g.lighting = false;
    else if (cap == GL_NORMALIZE) g.normalize = false;
#ifdef GL_RESCALE_NORMAL
    else if (cap == GL_RESCALE_NORMAL) g.normalize = false;
#endif
    else if (cap == GL_COLOR_MATERIAL) g.colorMaterial = false;
    else if (cap == GL_CLIP_PLANE0) g.clipPlane0 = false;
    else if (cap >= GL_LIGHT0 && cap <= GL_LIGHT7) g.lights[cap - GL_LIGHT0].enabled = false;
}
extern "C" GLboolean hwglIsEnabled(GLenum cap)
{
    if (cap == GL_TEXTURE_2D) return g.draw.texture2D;
    if (cap == GL_BLEND) return g.draw.blend;
    if (cap == GL_ALPHA_TEST) return g.draw.alphaTest;
    if (cap == GL_DEPTH_TEST) return g.draw.depthTest;
    if (cap == GL_CULL_FACE) return g.draw.cullFace;
    if (cap == GL_SCISSOR_TEST) return g.draw.scissor;
    if (cap == GL_FOG) return g.draw.fog;
    if (cap == GL_LIGHTING) return g.lighting;
    if (cap == GL_NORMALIZE) return g.normalize;
#ifdef GL_RESCALE_NORMAL
    if (cap == GL_RESCALE_NORMAL) return g.normalize;
#endif
    if (cap == GL_COLOR_MATERIAL) return g.colorMaterial;
    if (cap == GL_CLIP_PLANE0) return g.clipPlane0;
    if (cap >= GL_LIGHT0 && cap <= GL_LIGHT7) return g.lights[cap - GL_LIGHT0].enabled;
    return GL_FALSE;
}
extern "C" void hwglBlendFunc(GLenum s, GLenum d) { g.draw.blendSrc=s; g.draw.blendDst=d; }
extern "C" void hwglDepthFunc(GLenum f) { g.draw.depthFunc=f; }
extern "C" void hwglDepthMask(GLboolean flag) { g.draw.depthWrite=flag != GL_FALSE; }
extern "C" void hwglCullFace(GLenum mode) { g.draw.cullMode=mode; }
extern "C" void hwglPolygonMode(GLenum, GLenum mode) { g.draw.polygonMode=mode; }
extern "C" void hwglShadeModel(GLenum mode) { g.shadeModel=mode; }
extern "C" void hwglLineWidth(GLfloat width) { g.lineWidth=width; }
extern "C" void hwglPointSize(GLfloat size) { g.pointSize=size; }
extern "C" void hwglLineStipple(GLint, GLushort) {}

extern "C" void hwglClearColor(GLclampf r0, GLclampf g0, GLclampf b0, GLclampf a0) { g.clearColor[0]=r0;g.clearColor[1]=g0;g.clearColor[2]=b0;g.clearColor[3]=a0; }
extern "C" void hwglClearDepth(GLclampd depth) { g.clearDepth=depth; }
extern "C" void hwglClearDepthf(GLclampf depth) { g.clearDepth=depth; }
extern "C" void hwglClear(GLbitfield mask)
{
    if (!gFrameRecording) gDetachedDirty = true;
    RasterCommand c; c.type=RasterCommand::Clear; c.clearMask=mask; c.clearDepth=static_cast<float>(g.clearDepth); c.state=g.draw; std::memcpy(c.clearColor,g.clearColor,sizeof(c.clearColor)); gCommands.push_back(std::move(c));
}
extern "C" void hwglFlush(void) {}

extern "C" void hwglMatrixMode(GLenum mode) { g.matrixMode=mode; }
extern "C" void hwglLoadIdentity(void) { currentMatrix()=identityMatrix(); }
extern "C" void hwglLoadMatrixf(const GLfloat *m) { if(m) std::memcpy(currentMatrix().m,m,sizeof(float)*16); }
extern "C" void hwglMultMatrixf(const GLfloat *m) { if(m) { Mat4 b; std::memcpy(b.m,m,sizeof(b.m)); currentMatrix()=multiply(currentMatrix(),b); } }
extern "C" void hwglPushMatrix(void) { currentMatrixStack().push_back(currentMatrix()); }
extern "C" void hwglPopMatrix(void) { auto &s=currentMatrixStack(); if(s.empty()) {setError(GL_STACK_UNDERFLOW);return;} currentMatrix()=s.back();s.pop_back(); }
extern "C" void hwglTranslatef(GLfloat x, GLfloat y, GLfloat z) { Mat4 t=identityMatrix();t.m[12]=x;t.m[13]=y;t.m[14]=z;currentMatrix()=multiply(currentMatrix(),t); }
extern "C" void hwglScalef(GLfloat x, GLfloat y, GLfloat z) { Mat4 s=identityMatrix();s.m[0]=x;s.m[5]=y;s.m[10]=z;currentMatrix()=multiply(currentMatrix(),s); }
extern "C" void hwglRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float axis[3]={x,y,z};normalize3(axis);const float rad=angle*3.14159265358979323846f/180.0f;const float c=std::cos(rad),s=std::sin(rad),ic=1-c;Mat4 m=identityMatrix();
    m.m[0]=axis[0]*axis[0]*ic+c;m.m[4]=axis[0]*axis[1]*ic-axis[2]*s;m.m[8]=axis[0]*axis[2]*ic+axis[1]*s;
    m.m[1]=axis[1]*axis[0]*ic+axis[2]*s;m.m[5]=axis[1]*axis[1]*ic+c;m.m[9]=axis[1]*axis[2]*ic-axis[0]*s;
    m.m[2]=axis[2]*axis[0]*ic-axis[1]*s;m.m[6]=axis[2]*axis[1]*ic+axis[0]*s;m.m[10]=axis[2]*axis[2]*ic+c;currentMatrix()=multiply(currentMatrix(),m);
}
extern "C" void hwglOrtho(GLdouble l, GLdouble rr, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    Mat4 m={};m.m[0]=static_cast<float>(2.0/(rr-l));m.m[5]=static_cast<float>(2.0/(t-b));m.m[10]=static_cast<float>(-2.0/(f-n));m.m[12]=static_cast<float>(-(rr+l)/(rr-l));m.m[13]=static_cast<float>(-(t+b)/(t-b));m.m[14]=static_cast<float>(-(f+n)/(f-n));m.m[15]=1;currentMatrix()=multiply(currentMatrix(),m);
}
extern "C" void hwglOrthof(GLfloat l, GLfloat rr, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { hwglOrtho(l,rr,b,t,n,f); }
extern "C" void hwglFrustum(GLdouble l, GLdouble rr, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    Mat4 m={};m.m[0]=static_cast<float>((2*n)/(rr-l));m.m[5]=static_cast<float>((2*n)/(t-b));m.m[8]=static_cast<float>((rr+l)/(rr-l));m.m[9]=static_cast<float>((t+b)/(t-b));m.m[10]=static_cast<float>(-(f+n)/(f-n));m.m[11]=-1;m.m[14]=static_cast<float>(-(2*f*n)/(f-n));currentMatrix()=multiply(currentMatrix(),m);
}
extern "C" void hwglFrustumf(GLfloat l, GLfloat rr, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { hwglFrustum(l,rr,b,t,n,f); }

extern "C" void hwglViewport(GLint x, GLint y, GLsizei w, GLsizei h) { g.draw.viewport[0]=x;g.draw.viewport[1]=y;g.draw.viewport[2]=w;g.draw.viewport[3]=h; }
extern "C" void hwglScissor(GLint x, GLint y, GLsizei w, GLsizei h) { g.draw.scissorBox[0]=x;g.draw.scissorBox[1]=y;g.draw.scissorBox[2]=w;g.draw.scissorBox[3]=h; }
extern "C" void hwglPushAttrib(GLbitfield) { g.attribStack.push_back(g.draw); }
extern "C" void hwglPopAttrib(void) { if(!g.attribStack.empty()){g.draw=g.attribStack.back();g.attribStack.pop_back();} }

extern "C" void hwglVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { g.vertexArray.size=size;g.vertexArray.type=type;g.vertexArray.stride=stride;g.vertexArray.pointer=static_cast<const unsigned char*>(p);g.vertexArray.buffer=g.arrayBuffer; }
extern "C" void hwglColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { g.colorArray.size=size;g.colorArray.type=type;g.colorArray.stride=stride;g.colorArray.pointer=static_cast<const unsigned char*>(p);g.colorArray.buffer=g.arrayBuffer; }
extern "C" void hwglTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { g.texcoordArray.size=size;g.texcoordArray.type=type;g.texcoordArray.stride=stride;g.texcoordArray.pointer=static_cast<const unsigned char*>(p);g.texcoordArray.buffer=g.arrayBuffer; }
extern "C" void hwglNormalPointer(GLenum type, GLsizei stride, const GLvoid *p) { g.normalArray.size=3;g.normalArray.type=type;g.normalArray.stride=stride;g.normalArray.pointer=static_cast<const unsigned char*>(p);g.normalArray.buffer=g.arrayBuffer; }
extern "C" void hwglEnableClientState(GLenum a) { if(a==GL_VERTEX_ARRAY)g.vertexArray.enabled=true;else if(a==GL_COLOR_ARRAY)g.colorArray.enabled=true;else if(a==GL_TEXTURE_COORD_ARRAY)g.texcoordArray.enabled=true;else if(a==GL_NORMAL_ARRAY)g.normalArray.enabled=true; }
extern "C" void hwglDisableClientState(GLenum a) { if(a==GL_VERTEX_ARRAY)g.vertexArray.enabled=false;else if(a==GL_COLOR_ARRAY)g.colorArray.enabled=false;else if(a==GL_TEXTURE_COORD_ARRAY)g.texcoordArray.enabled=false;else if(a==GL_NORMAL_ARRAY)g.normalArray.enabled=false; }
extern "C" void hwglArrayElement(GLint i) { loadArrayElement(i,true); }
extern "C" void hwglDrawArrays(GLenum mode, GLint first, GLsizei count) { gImmediate.clear();if(count>0&&gImmediate.capacity()<static_cast<size_t>(count))gImmediate.reserve(static_cast<size_t>(count));for(GLint i=0;i<count;++i)loadArrayElement(first+i,true);recordDraw(mode,gImmediate);gImmediate.clear(); }
extern "C" void hwglDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (count <= 0) return;
    size_t indexSize = 0;
    if (type == GL_UNSIGNED_SHORT) indexSize = sizeof(GLushort);
    else if (type == GL_UNSIGNED_BYTE) indexSize = sizeof(GLubyte);
    else if (type == GL_UNSIGNED_INT) indexSize = sizeof(GLuint);
    else { setError(GL_INVALID_ENUM); return; }

    const unsigned char *base = static_cast<const unsigned char *>(indices);
    if (g.elementBuffer)
    {
        auto it = gBuffers.find(g.elementBuffer);
        if (it == gBuffers.end()) return;
        const size_t offset = reinterpret_cast<size_t>(indices);
        const size_t countBytes = static_cast<size_t>(count) * indexSize;
        if (offset > it->second.bytes.size() ||
            countBytes > it->second.bytes.size() - offset)
            return;
        base = it->second.bytes.data() + offset;
    }
    if (!base) return;

    gImmediateIndices.clear();
    if (gImmediateIndices.capacity() < static_cast<size_t>(count))
        gImmediateIndices.reserve(static_cast<size_t>(count));
    for (GLsizei i = 0; i < count; ++i)
    {
        uint32_t index = 0;
        if (type == GL_UNSIGNED_SHORT)
            index = reinterpret_cast<const GLushort *>(base)[i];
        else if (type == GL_UNSIGNED_BYTE)
            index = reinterpret_cast<const GLubyte *>(base)[i];
        else
            index = reinterpret_cast<const GLuint *>(base)[i];
        gImmediateIndices.push_back(index);
    }
    recordIndexedDraw(mode, gImmediateIndices.data(), gImmediateIndices.size());
    gImmediateIndices.clear();
}
extern "C" void hwglLockArrays(GLint, GLsizei) {}
extern "C" void hwglUnlockArrays(void) {}
extern "C" void hwglLockArraysEXT(GLint f, GLsizei c) { hwglLockArrays(f,c); }
extern "C" void hwglUnlockArraysEXT(void) { hwglUnlockArrays(); }

extern "C" void hwglGenBuffers(GLsizei n, GLuint *out) { if(!out)return;for(GLsizei i=0;i<n;++i){out[i]=gNextBuffer++;gBuffers[out[i]]={};} }
extern "C" void hwglDeleteBuffers(GLsizei n, const GLuint *ids) { if(!ids)return;for(GLsizei i=0;i<n;++i)gBuffers.erase(ids[i]); }
extern "C" void hwglBindBuffer(GLenum target, GLuint id) { if(target==GL_ARRAY_BUFFER)g.arrayBuffer=id;else if(target==GL_ELEMENT_ARRAY_BUFFER)g.elementBuffer=id; }
extern "C" void hwglBufferData(GLenum target, ptrdiff_t size, const GLvoid *data, GLenum)
{
    GLuint id = boundBufferForTarget(target);
    if (!id) return;
    auto &b = gBuffers[id].bytes;
    const size_t byteCount = size > 0 ? static_cast<size_t>(size) : 0u;
    b.resize(byteCount);
    if (data != nullptr && byteCount > 0) std::memcpy(b.data(), data, byteCount);
}
extern "C" void hwglBufferSubData(GLenum target, ptrdiff_t offset, ptrdiff_t size, const GLvoid *data)
{
    GLuint id = boundBufferForTarget(target);
    if (!id || data == nullptr || offset < 0 || size < 0) return;
    auto &b = gBuffers[id].bytes;
    const size_t begin = static_cast<size_t>(offset);
    const size_t byteCount = static_cast<size_t>(size);
    if (byteCount > (std::numeric_limits<size_t>::max)() - begin) return;
    const size_t end = begin + byteCount;
    if (end > b.size()) b.resize(end);
    if (byteCount > 0) std::memcpy(b.data() + begin, data, byteCount);
}

extern "C" void hwglGenTextures(GLsizei n, GLuint *out) { if(!out)return;for(GLsizei i=0;i<n;++i){out[i]=gNextTexture++;TextureRecord &t=texture(out[i]);t.descriptorSlot=std::min<unsigned int>(out[i],kSrvCapacity-1);} }
extern "C" void hwglDeleteTextures(GLsizei n, const GLuint *ids)
{
    if (ids == nullptr) return;
    for (GLsizei i = 0; i < n; ++i)
    {
        if (ids[i] == 0) continue;
        auto found = gTextures.find(ids[i]);
        if (found != gTextures.end())
        {
            retainAcrossFlight(found->second.gpu);
            gTextures.erase(found);
        }
    }
}
extern "C" void hwglBindTexture(GLenum target, GLuint id) { if(target==GL_TEXTURE_2D){g.draw.texture=id;texture(id);} }
extern "C" void hwglTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint, GLenum format, GLenum type, const GLvoid *pixels) { if(target!=GL_TEXTURE_2D||level!=0)return;TextureRecord &t=texture(g.draw.texture);t.internalFormat=internalformat;t.sourceFormat=format;convertPixelsToRgba(t,w,h,format,type,pixels);t.dirty=true; }
extern "C" void hwglTexSubImage2D(GLenum target, GLint level, GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid *pixels)
{
    if(target!=GL_TEXTURE_2D||level!=0||!pixels||type!=GL_UNSIGNED_BYTE)return;TextureRecord &t=texture(g.draw.texture);if(t.compressed||t.width==0||t.height==0)return;TextureRecord sub;convertPixelsToRgba(sub,w,h,format,type,pixels);for(GLint y=0;y<h;++y){if(yo+y<0||static_cast<unsigned int>(yo+y)>=t.height)continue;for(GLint x=0;x<w;++x){if(xo+x<0||static_cast<unsigned int>(xo+x)>=t.width)continue;std::memcpy(t.pixels.data()+((static_cast<size_t>(yo+y)*t.width)+(xo+x))*4,sub.pixels.data()+((static_cast<size_t>(y)*w)+x)*4,4);}}t.dirty=true;
}
extern "C" void hwglTexParameteri(GLenum target, GLenum pname, GLint param) { if(target!=GL_TEXTURE_2D)return;TextureRecord &t=texture(g.draw.texture);if(pname==GL_TEXTURE_MIN_FILTER)t.minFilter=param;else if(pname==GL_TEXTURE_MAG_FILTER)t.magFilter=param;else if(pname==GL_TEXTURE_WRAP_S)t.wrapS=param;else if(pname==GL_TEXTURE_WRAP_T)t.wrapT=param; }
extern "C" void hwglTexParameterf(GLenum target, GLenum pname, GLfloat param) { hwglTexParameteri(target,pname,static_cast<GLint>(param)); }
extern "C" void hwglTexEnvi(GLenum target, GLenum pname, GLint param) { if(target==GL_TEXTURE_ENV&&pname==GL_TEXTURE_ENV_MODE)g.draw.texEnvMode=param; }
extern "C" void hwglGetTexEnviv(GLenum target, GLenum pname, GLint *params) { if(params&&target==GL_TEXTURE_ENV&&pname==GL_TEXTURE_ENV_MODE)*params=g.draw.texEnvMode; }
extern "C" void hwglGenerateMipmap(GLenum) {}
extern "C" void hwglTexStorage2D(GLenum target, GLsizei, GLenum internalformat, GLsizei w, GLsizei h) { if(target==GL_TEXTURE_2D){TextureRecord &t=texture(g.draw.texture);t.internalFormat=internalformat;t.width=w;t.height=h;t.pixels.assign(static_cast<size_t>(w)*h*4,0);t.dirty=true;} }
extern "C" void hwglCompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLsizei w, GLsizei h, GLint, GLsizei imageSize, const GLvoid *data)
{
    if (target != GL_TEXTURE_2D || level < 0 || data == nullptr || imageSize <= 0 || w <= 0 || h <= 0) return;
    TextureRecord &t = texture(g.draw.texture);
    if (static_cast<size_t>(level) >= t.compressedMips.size())
        t.compressedMips.resize(static_cast<size_t>(level) + 1u);
    CompressedMip &mip = t.compressedMips[static_cast<size_t>(level)];
    mip.width = static_cast<unsigned int>(w);
    mip.height = static_cast<unsigned int>(h);
    mip.bytes.assign(static_cast<const unsigned char *>(data),
                     static_cast<const unsigned char *>(data) + imageSize);
    t.internalFormat = internalFormat;
    t.compressed = true;
    /* ARB_texture_compression_bptc: 0x8E8F is unsigned-float BC6H.
       Keep BC7 for ordinary display-referred compressed textures. */
    t.gpuFormat = internalFormat == 0x8E8Fu ?
        DXGI_FORMAT_BC6H_UF16 : DXGI_FORMAT_BC7_UNORM;
    if (level == 0)
    {
        t.width = static_cast<unsigned int>(w);
        t.height = static_cast<unsigned int>(h);
        t.pixels = mip.bytes;
        t.dirty = true;
    }
}
extern "C" void hwglGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
    if (pixels == nullptr || target != GL_TEXTURE_2D || level < 0 || type != GL_UNSIGNED_BYTE) return;
    TextureRecord &t = texture(g.draw.texture);
    unsigned char *dst = static_cast<unsigned char *>(pixels);
    if (t.compressed)
    {
        if (static_cast<size_t>(level) >= t.compressedMips.size() || format != GL_RGBA)
        {
            setError(GL_INVALID_VALUE);
            return;
        }
        /* CPU readback exists only for the legacy BC7 analysis/debug path.
           Mission BC6H lighting metadata is generated from the float source,
           so there is deliberately no lossy BC6H -> 8-bit readback here. */
        if (t.gpuFormat != DXGI_FORMAT_BC7_UNORM)
        {
            setError(GL_INVALID_OPERATION);
            return;
        }
        std::vector<unsigned char> decoded;
        if (!decodeBc7Mode6Mip(t.compressedMips[static_cast<size_t>(level)], decoded))
        {
            setError(GL_INVALID_OPERATION);
            return;
        }
        std::memcpy(dst, decoded.data(), decoded.size());
        return;
    }
    if (level != 0)
    {
        setError(GL_INVALID_VALUE);
        return;
    }
    if (format == GL_RGBA) std::memcpy(dst, t.pixels.data(), t.pixels.size());
    else if (format == GL_RGB)
    {
        for (size_t i = 0, p = 0; i < t.pixels.size(); i += 4)
        {
            dst[p++] = t.pixels[i + 0];
            dst[p++] = t.pixels[i + 1];
            dst[p++] = t.pixels[i + 2];
        }
    }
    else setError(GL_INVALID_ENUM);
}

extern "C" void hwglPixelStorei(GLenum pname, GLint param) { if(pname==GL_PACK_ALIGNMENT)g.packAlignment=param;else if(pname==GL_UNPACK_ALIGNMENT)g.unpackAlignment=param;
#ifdef GL_PACK_ROW_LENGTH
else if(pname==GL_PACK_ROW_LENGTH)g.packRowLength=param;
#endif
#ifdef GL_UNPACK_ROW_LENGTH
else if(pname==GL_UNPACK_ROW_LENGTH)g.unpackRowLength=param;
#endif
}
extern "C" void hwglReadBuffer(GLenum) {}
extern "C" void hwglReadPixels(GLint, GLint, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels) { if(!pixels||type!=GL_UNSIGNED_BYTE)return;const size_t components=format==GL_RGB?3u:4u;std::memset(pixels,0,static_cast<size_t>(std::max(0,w))*std::max(0,h)*components); }

extern "C" void hwglRasterPos2f(GLfloat x, GLfloat y) { auto eye=transform(g.modelview,x,y,0,1);auto clip=transform(g.projection,eye[0],eye[1],eye[2],eye[3]);g.rasterPos[0]=clip[0];g.rasterPos[1]=clip[1];g.rasterPos[2]=clip[2];g.rasterPos[3]=clip[3]; }
extern "C" void hwglRasterPos2i(GLint x, GLint y) { hwglRasterPos2f(static_cast<float>(x),static_cast<float>(y)); }
extern "C" void hwglDrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid *pixels)
{
    if(w<=0||h<=0||!pixels)return;GLuint id=gNextTexture++;gTransientTextures.push_back(id);TextureRecord &t=texture(id);convertPixelsToRgba(t,w,h,format,type,pixels);t.dirty=true;const float invW=g.rasterPos[3]!=0?1.0f/g.rasterPos[3]:1.0f;const float ndcX=g.rasterPos[0]*invW;const float ndcY=g.rasterPos[1]*invW;const float dx=2.0f*w/std::max(1u,gFrameWidth);const float dy=2.0f*h/std::max(1u,gFrameHeight);std::vector<RasterVertex> v(4);for(auto &rv:v){rv.color[0]=rv.color[1]=rv.color[2]=rv.color[3]=1;rv.fogFactor=1;rv.clipDistance=1;rv.position[2]=0;rv.position[3]=1;}v[0].position[0]=ndcX;v[0].position[1]=-ndcY;v[0].uv[0]=0;v[0].uv[1]=0;v[1]=v[0];v[1].position[1]-=dy;v[1].uv[1]=1;v[2]=v[0];v[2].position[0]+=dx;v[2].uv[0]=1;v[3]=v[2];v[3].position[1]-=dy;v[3].uv[1]=1;RasterDrawState old=g.draw;g.draw.texture2D=true;g.draw.texture=id;g.draw.depthTest=false;recordDraw(GL_TRIANGLE_STRIP,v,true);g.draw=old;
}

extern "C" void hwglFogf(GLenum pname, GLfloat param) { if(pname==GL_FOG_DENSITY)g.fogDensity=param;else if(pname==GL_FOG_START)g.fogStart=param;else if(pname==GL_FOG_END)g.fogEnd=param; }
extern "C" void hwglFogi(GLenum pname, GLint param) { if(pname==GL_FOG_MODE)g.fogMode=param; }
extern "C" void hwglFogfv(GLenum pname, const GLfloat *p) { if(p&&pname==GL_FOG_COLOR)std::memcpy(g.draw.fogColor,p,sizeof(float)*4); }
extern "C" void hwglLightModelf(GLenum pname, GLfloat param) { if(pname==GL_LIGHT_MODEL_TWO_SIDE){(void)param;} }
extern "C" void hwglLightModelfv(GLenum pname, const GLfloat *p) { if(p&&pname==GL_LIGHT_MODEL_AMBIENT)std::memcpy(g.lightModelAmbient,p,sizeof(float)*4); }
extern "C" void hwglLightfv(GLenum light, GLenum pname, const GLfloat *p)
{
    if(!p||light<GL_LIGHT0||light>GL_LIGHT7)return;LightState &l=g.lights[light-GL_LIGHT0];if(pname==GL_AMBIENT)std::memcpy(l.ambient,p,sizeof(float)*4);else if(pname==GL_DIFFUSE)std::memcpy(l.diffuse,p,sizeof(float)*4);else if(pname==GL_SPECULAR)std::memcpy(l.specular,p,sizeof(float)*4);else if(pname==GL_POSITION){auto eye=transform(g.modelview,p[0],p[1],p[2],p[3]);for(int i=0;i<4;++i)l.position[i]=eye[i];}
}
extern "C" void hwglMaterialfv(GLenum, GLenum pname, const GLfloat *p) { if(!p)return;if(pname==GL_AMBIENT)std::memcpy(g.materialAmbient,p,sizeof(float)*4);else if(pname==GL_DIFFUSE)std::memcpy(g.materialDiffuse,p,sizeof(float)*4);else if(pname==GL_EMISSION)std::memcpy(g.materialEmission,p,sizeof(float)*4);else if(pname==GL_AMBIENT_AND_DIFFUSE){std::memcpy(g.materialAmbient,p,sizeof(float)*4);std::memcpy(g.materialDiffuse,p,sizeof(float)*4);} }
extern "C" void hwglClipPlane(GLenum plane, const GLdouble *equation)
{
    if (plane != GL_CLIP_PLANE0 || equation == nullptr) return;
    Mat4 inv;
    if (!inverseMatrix(g.modelview, inv)) return;
    float p[4] = {static_cast<float>(equation[0]), static_cast<float>(equation[1]),
                  static_cast<float>(equation[2]), static_cast<float>(equation[3])};
    for (int row = 0; row < 4; ++row)
    {
        g.clipEquationEye[row] = inv.m[row * 4 + 0] * p[0] +
                                 inv.m[row * 4 + 1] * p[1] +
                                 inv.m[row * 4 + 2] * p[2] +
                                 inv.m[row * 4 + 3] * p[3];
    }
}
extern "C" void hwglClipPlanef(GLenum plane, const GLfloat *equation)
{
    if (equation == nullptr) return;
    GLdouble d[4] = {equation[0], equation[1], equation[2], equation[3]};
    hwglClipPlane(plane, d);
}

extern "C" void hwglGetBooleanv(GLenum pname, GLboolean *p) { if(!p)return;if(pname==GL_DEPTH_WRITEMASK)*p=g.draw.depthWrite?GL_TRUE:GL_FALSE;else *p=hwglIsEnabled(pname); }
extern "C" void hwglGetFloatv(GLenum pname, GLfloat *p)
{
    if(!p)return;if(pname==GL_MODELVIEW_MATRIX)std::memcpy(p,g.modelview.m,sizeof(float)*16);else if(pname==GL_PROJECTION_MATRIX)std::memcpy(p,g.projection.m,sizeof(float)*16);else if(pname==GL_COLOR_CLEAR_VALUE)std::memcpy(p,g.clearColor,sizeof(float)*4);else if(pname==GL_LINE_WIDTH)*p=g.lineWidth;else if(pname==GL_POINT_SIZE)*p=g.pointSize;else if(pname==GL_LIGHT_MODEL_AMBIENT)std::memcpy(p,g.lightModelAmbient,sizeof(float)*4);else *p=0;
}
extern "C" void hwglGetIntegerv(GLenum pname, GLint *p)
{
    if(!p)return;if(pname==GL_VIEWPORT)std::memcpy(p,g.draw.viewport,sizeof(GLint)*4);else if(pname==GL_SCISSOR_BOX)std::memcpy(p,g.draw.scissorBox,sizeof(GLint)*4);else if(pname==GL_MATRIX_MODE)*p=g.matrixMode;else if(pname==GL_TEXTURE_BINDING_2D)*p=static_cast<GLint>(g.draw.texture);else if(pname==GL_MAX_TEXTURE_SIZE)*p=16384;else if(pname==GL_SHADE_MODEL)*p=g.shadeModel;else if(pname==GL_POLYGON_MODE){p[0]=p[1]=g.draw.polygonMode;}else *p=0;
}
extern "C" GLenum hwglGetError(void) { GLenum e=g.error;g.error=GL_NO_ERROR;return e; }
extern "C" const GLubyte *hwglGetString(GLenum name)
{
    static const GLubyte vendor[]="Homeworld Modern";static const GLubyte renderer[]="Native D3D12 fixed-function compatibility rasterizer";static const GLubyte version[]="D3D12 native raster 1.0";static const GLubyte extensions[]="GL_ARB_vertex_buffer_object GL_EXT_compiled_vertex_array GL_EXT_texture_edge_clamp GL_ARB_texture_non_power_of_two GL_ARB_texture_compression_bptc";if(name==GL_VENDOR)return vendor;if(name==GL_RENDERER)return renderer;if(name==GL_VERSION)return version;if(name==GL_EXTENSIONS)return extensions;return reinterpret_cast<const GLubyte*>("");
}

extern "C" void hwglGenFramebuffers(GLsizei n, GLuint *out) { if(!out)return;for(GLsizei i=0;i<n;++i)out[i]=gNextFramebuffer++; }
extern "C" void hwglDeleteFramebuffers(GLsizei, const GLuint *) {}
extern "C" void hwglBindFramebuffer(GLenum, GLuint) {}
extern "C" void hwglFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint) {}

#endif /* _WIN32 && HW_ENABLE_D3D12_NATIVE_RASTER */
