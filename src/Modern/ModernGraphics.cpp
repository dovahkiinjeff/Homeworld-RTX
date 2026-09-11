#include "ModernGraphics.h"
#include "ModernDlaa.h"
#include "ModernUpscaler.h"
#include "ModernRaytracing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <DirectXTex.h>
#ifndef HW_ENABLE_D3D12_NATIVE_RASTER
#include <GL/gl.h>
#else
#include <SDL2/SDL_opengl.h>
#include "LegacyD3D12GL.h"
#include "LegacyD3D12Renderer.h"
#endif
#include <wrl/client.h>

#ifndef GL_PACK_ROW_LENGTH
#define GL_PACK_ROW_LENGTH 0x0D02
#endif

using Microsoft::WRL::ComPtr;

namespace
{
void logTextureLoadMetrics();
constexpr UINT frameBufferCount = 3;
constexpr UINT maximumVolumetricDustVolumes = 64;
constexpr UINT maximumVolumetricDustLights = 12;
constexpr UINT maximumVolumetricDustWakes = 12;
constexpr UINT sceneDepthDescriptorIndex = 13;
constexpr UINT dustDescriptorBase = 14;
constexpr UINT dustDescriptorsPerFrame = 8;
constexpr UINT dustFroxelNearUavDescriptorIndex =
    dustDescriptorBase + frameBufferCount * dustDescriptorsPerFrame;
constexpr UINT dustFroxelFarUavDescriptorIndex =
    dustFroxelNearUavDescriptorIndex + 1u;
constexpr UINT dustFroxelDescriptorCount = dustFroxelFarUavDescriptorIndex + 1u;
constexpr UINT rrGuideUavDescriptorBase = dustFroxelDescriptorCount;
constexpr UINT rrGuideUavDescriptorCount = 3u;
constexpr UINT rayEnvironmentDescriptorIndex =
    rrGuideUavDescriptorBase + rrGuideUavDescriptorCount;
constexpr UINT presenterDescriptorCount = rayEnvironmentDescriptorIndex + 1u;
/* Shared dust cache storage is world-aligned, not camera/frustum aligned.
   The physical texture is deliberately larger than the active grid so the
   active dimensions can preserve roughly cubic world-space voxels without
   reallocating GPU resources as authored clouds change. */
constexpr UINT dustWorldGridTextureDimension = 160u;
constexpr UINT dustWorldGridActiveDimension = 144u;
/* Preserve the modern frame in FP16 through the Windows compositor. */
constexpr DXGI_FORMAT presenterFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
constexpr DXGI_FORMAT sceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
#else
constexpr DXGI_FORMAT sceneFormat = presenterFormat;
#endif

float halton(unsigned int index, unsigned int base)
{
    float result = 0.0f;
    float fraction = 1.0f;
    while (index != 0u)
    {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

void temporalJitter(unsigned long long frameIndex, float &x, float &y)
{
    /* A centered low-discrepancy sequence supplies the sub-pixel coverage
       DLSS/DLAA expects. Thirty-two phases also avoid a very short repeat in
       the aggressive Performance and Ultra Performance modes. */
    const unsigned int phase =
        static_cast<unsigned int>(frameIndex % 32u) + 1u;
    x = halton(phase, 2u) - 0.5f;
    y = halton(phase, 3u) - 0.5f;
}

const char presenterShader[] = R"(
Texture2D<float4> SourceFrame : register(t0);
Texture2D<float4> RayLighting : register(t1);
Texture2D<float4> WorldFrame : register(t2);
Texture2D<float4> FilteredWorld : register(t3);
Texture2D<float2> MotionVectors : register(t4);
Texture2D<float4> BackgroundFrame : register(t5);
Texture2D<float4> OccluderFrame : register(t7);
Texture2D<float> SceneDepth : register(t8);

struct DustVolume
{
    uint enabled;
    uint shape;
    uint shapeSeed;
    uint flags;
    float3 position;
    float density;
    float3 size;
    float scattering;
    float3 color;
    float absorption;
    float anisotropy;
    float coverage;
    float noiseScale;
    float noiseDetail;
    float shapeVariation;
    float3 rotationDegrees;
    /* RTX sparse/froxel path: CPU-precomputed invariants. Keeping these in the
       structured volume record avoids millions of redundant sin/cos/hash
       evaluations while building the shared 3D field. */
    float2 rotationXSinCos;
    float2 rotationYSinCos;
    float2 rotationZSinCos;
    float2 precomputedPadding;
    float3 seedOffset;
    float seedPadding;
};

struct DustLight
{
    float3 position;
    uint type;
    float3 direction;
    float radius;
    float3 color;
    float intensity;
    float coneCos;
    float edgeCos;
    float2 padding;
};

struct DustWake
{
    float3 position;
    float radius;
    float3 direction;
    float length;
    float strength;
    float bowStrength;
    float recovery;
    float padding;
};

StructuredBuffer<DustVolume> DustVolumes : register(t9);
StructuredBuffer<DustLight> DustLights : register(t10);
StructuredBuffer<DustWake> DustWakes : register(t11);
Texture3D<float4> DustFroxelField : register(t12);
Texture2D<float4> PrimaryAlbedoGuide : register(t13);
Texture3D<float4> DustFarFroxelField : register(t14);
Texture2D<float4> DustScreenField : register(t15);
Texture2D<float4> DustScreenHistory : register(t16);
#ifdef DUST_FROXEL_COMPUTE
RWTexture3D<float4> DustFroxelFieldRW : register(u0);
#endif
SamplerState SourceSampler : register(s0);

cbuffer PresenterConstants : register(b0)
{
    uint RayOverlayEnabled;
    uint AntiAliasingMode;
    uint SceneCompositeEnabled;
    uint FrameIndex;
    float ChromaticAberration;
    float MotionBlur;
    float FilmGrain;
    float GodRays;
    float2 GodRayCenter;
    uint ScenePrepass;
    uint StreamlineResolvedScene;
    float OutputDither;
    float3 GodRaySourceColor;
    uint GodRaySourceExternal;
    float Bloom;
    float2 PresenterPadding;
    uint DustVolumeCount;
    uint DustLightCount;
    uint DustRenderEnabled;
    uint DustWakeCount;
    float4 DustCameraPositionMaxDistance;
    float4 DustRayUv00;
    float4 DustRayUv10;
    float4 DustRayUv01SunStrength;
    float4 DustSunDirectionValid;
    float4 DustSunColor;
    float4 DustAmbientColorStrength;
    /* World-space shared-volume cache.  .w stores active X/Y dimensions;
       PresenterPadding.x stores active Z.  Keeping these at the end preserves
       the existing constants while staying within the D3D12 root DWORD budget. */
    float4 DustGridMinWidth;
    float4 DustGridSizeHeight;
    /* Depth reconstruction reuses otherwise-unused .w components so this
       cbuffer remains at the proven 60-DWORD size.  D3D12 root signatures
       have a 64-DWORD total budget and this shader already uses three
       descriptor tables in addition to the root constants.
       DustRayUv00.w = projection P[10]
       DustRayUv10.w = projection P[14]
       DustSunColor.w = exact native-raster depth available this frame. */
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VertexOutput PresentVS(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0),
                             0.0, 1.0);
    output.texcoord = float2(corner.x, 1.0 - corner.y);
    return output;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float Noise(float2 position)
{
    float value = dot(position + float2(FrameIndex * 17u, FrameIndex * 31u),
                      float2(12.9898, 78.233));
    return frac(sin(value) * 43758.5453);
}

float3 LinearToSrgb(float3 linearRgb)
{
    linearRgb = max(linearRgb, 0.0);
    float3 low = linearRgb * 12.92;
    float3 high = 1.055 * pow(linearRgb, 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(linearRgb, 0.0031308));
}

float3 DisplayToLinear(float3 displayRgb)
{
    displayRgb = max(displayRgb, 0.0);
    float3 low = displayRgb / 12.92;
    float3 high = pow((displayRgb + 0.055) / 1.055, 2.4);
    return lerp(high, low, step(displayRgb, 0.04045));
}

float3 ToneMapSky(float3 radiance)
{
    /* Leave ordinary authored gradients untouched in scene-linear space, then
       roll only high radiance smoothly into the SDR display range. */
    const float shoulderStart = 0.62;
    float3 excess = max(radiance - shoulderStart, 0.0);
    float3 shoulder = shoulderStart + (1.0 - shoulderStart) *
        (1.0 - exp(-excess / (1.0 - shoulderStart)));
    return lerp(radiance, shoulder, step(shoulderStart, radiance));
}

float3 SkyToDisplay(float3 radiance)
{
    return saturate(LinearToSrgb(ToneMapSky(max(radiance, 0.0))));
}

float3 ApplySkyCoverage(float2 uv, float3 raw, float coverage)
{
    uv = saturate(uv);
    float3 backgroundLinear = BackgroundFrame.Sample(SourceSampler, uv).rgb;
    float3 backgroundDisplay = SkyToDisplay(backgroundLinear);
    /* Legacy RGB is already display-referred, while the mission environment
       below it is scene-linear HDR. Alpha in the native world target is now
       explicit foreground coverage. Replace only the uncovered fraction of
       the linear background with its display-space equivalent. This is exact
       for both standard SRC_ALPHA compositing and additive FX (whose alpha
       channel deliberately preserves the destination coverage). */
    return raw + (backgroundDisplay - backgroundLinear) *
                 (1.0 - saturate(coverage));
}

float3 SceneSample(float2 uv)
{
    if (ScenePrepass != 0)
    {
        uint renderWidth;
        uint renderHeight;
        RayLighting.GetDimensions(renderWidth, renderHeight);
        /* Presenter UVs are bottom-up; Streamline and the DXR guidance
           textures use native D3D top-down coordinates. */
        uv += float2(FilmGrain / max(1u, renderWidth),
                     -OutputDither / max(1u, renderHeight));
    }
    uv = saturate(uv);
    float3 raw = (AntiAliasingMode == 1
        ? FilteredWorld.Sample(SourceSampler, uv)
        : WorldFrame.Sample(SourceSampler, uv)).rgb;
    float coverage = WorldFrame.Sample(SourceSampler, uv).a;
    return ApplySkyCoverage(uv, raw, coverage);
}


float3 BackgroundDisplaySample(float2 uv)
{
    return SkyToDisplay(BackgroundFrame.Sample(SourceSampler, saturate(uv)).rgb);
}

float3 BloomExtract(float2 uv)
{
    float3 sampleColor = SceneSample(saturate(uv));
    float luminance = Luminance(sampleColor);
    /* A soft knee keeps ordinary ship paint out of bloom while allowing
       engines, weapon FX, suns and bright sky regions to spread. */
    float contribution = smoothstep(0.62, 0.90, luminance);
    return sampleColor * contribution;
}

float3 BloomScene(float2 uv)
{
    uint width;
    uint height;
    WorldFrame.GetDimensions(width, height);
    float2 texel = 1.0 / float2(max(1u, width), max(1u, height));
    float3 bloom = BloomExtract(uv) * 3.0;
    float weight = 3.0;

    /* Thirteen full-resolution taps approximate a broad bloom kernel without
       introducing another render target or a multi-pass synchronization
       point into the already-fast single-pass presenter. */
    bloom += BloomExtract(uv + texel * float2( 3.0,  0.0)) * 1.50;
    bloom += BloomExtract(uv + texel * float2(-3.0,  0.0)) * 1.50;
    bloom += BloomExtract(uv + texel * float2( 0.0,  3.0)) * 1.50;
    bloom += BloomExtract(uv + texel * float2( 0.0, -3.0)) * 1.50;
    weight += 6.0;

    bloom += BloomExtract(uv + texel * float2( 5.0,  5.0));
    bloom += BloomExtract(uv + texel * float2(-5.0,  5.0));
    bloom += BloomExtract(uv + texel * float2( 5.0, -5.0));
    bloom += BloomExtract(uv + texel * float2(-5.0, -5.0));
    weight += 4.0;

    bloom += BloomExtract(uv + texel * float2( 13.0,  0.0)) * 0.65;
    bloom += BloomExtract(uv + texel * float2(-13.0,  0.0)) * 0.65;
    bloom += BloomExtract(uv + texel * float2( 0.0,  13.0)) * 0.65;
    bloom += BloomExtract(uv + texel * float2( 0.0, -13.0)) * 0.65;
    weight += 2.6;
    return bloom / weight;
}

float3 FxaaScene(float2 uv)
{
    uint width;
    uint height;
    WorldFrame.GetDimensions(width, height);
    float2 texel = 1.0 / float2(max(1u, width), max(1u, height));
    float3 center = SceneSample(uv);
    float3 northwest = SceneSample(uv + texel * float2(-1.0, -1.0));
    float3 northeast = SceneSample(uv + texel * float2(1.0, -1.0));
    float3 southwest = SceneSample(uv + texel * float2(-1.0, 1.0));
    float3 southeast = SceneSample(uv + texel * float2(1.0, 1.0));
    float lumaCenter = Luminance(center);
    float lumaMinimum = min(lumaCenter, min(min(Luminance(northwest),
        Luminance(northeast)), min(Luminance(southwest),
        Luminance(southeast))));
    float lumaMaximum = max(lumaCenter, max(max(Luminance(northwest),
        Luminance(northeast)), max(Luminance(southwest),
        Luminance(southeast))));
    if (lumaMaximum - lumaMinimum < max(0.035, lumaMaximum * 0.10))
    {
        return center;
    }
    float2 direction;
    direction.x = -((Luminance(northwest) + Luminance(northeast)) -
                    (Luminance(southwest) + Luminance(southeast)));
    direction.y = ((Luminance(northwest) + Luminance(southwest)) -
                   (Luminance(northeast) + Luminance(southeast)));
    float reduction = max((Luminance(northwest) + Luminance(northeast) +
        Luminance(southwest) + Luminance(southeast)) * 0.03125, 0.0078125);
    float inverseMinimum = 1.0 / (min(abs(direction.x), abs(direction.y)) +
                                  reduction);
    direction = clamp(direction * inverseMinimum, -8.0, 8.0) * texel;
    float3 blendA = 0.5 * (
        SceneSample(uv + direction * (1.0 / 3.0 - 0.5)) +
        SceneSample(uv + direction * (2.0 / 3.0 - 0.5)));
    float3 blendB = blendA * 0.5 + 0.25 * (
        SceneSample(uv + direction * -0.5) +
        SceneSample(uv + direction * 0.5));
    float lumaB = Luminance(blendB);
    return (lumaB < lumaMinimum || lumaB > lumaMaximum) ? blendA : blendB;
}

float4 RaySourceSample(float2 rayUv)
{
    return AntiAliasingMode >= 3
        ? FilteredWorld.Sample(SourceSampler, saturate(rayUv))
        : RayLighting.Sample(SourceSampler, saturate(rayUv));
}

float4 FilteredRayLighting(float2 displayUv)
{
    uint width;
    uint height;
    if (AntiAliasingMode >= 3)
    {
        FilteredWorld.GetDimensions(width, height);
    }
    else
    {
        RayLighting.GetDimensions(width, height);
    }
    float2 texel = 1.0 / float2(max(1u, width), max(1u, height));
    float2 centerUv = float2(displayUv.x, 1.0 - displayUv.y);
    float4 center = RaySourceSample(centerUv);
    if (center.a < 0.0)
    {
        return center;
    }
    float3 total = center.rgb * 4.0;
    float totalWeight = 4.0;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }
            float2 offset = float2((float)x, (float)-y) * texel;
            float4 sampleValue = RaySourceSample(centerUv + offset);
            float depthTolerance = max(0.025, abs(center.a) * 0.008);
            if (sampleValue.a >= 0.0 &&
                abs(sampleValue.a - center.a) <= depthTolerance)
            {
                float weight = 1.0 / (1.0 + (float)(x * x + y * y));
                total += sampleValue.rgb * weight;
                totalWeight += weight;
            }
        }
    }
    return float4(lerp(center.rgb, total / totalWeight, 0.88), center.a);
}

static const uint DUST_SHAPE_SPHERE = 0u;
static const uint DUST_SHAPE_BOX = 1u;
static const uint DUST_SHAPE_ELLIPSOID = 2u;
static const uint DUST_RECEIVE_MISSION_KEY = 1u;
static const uint DUST_RECEIVE_LOCAL_LIGHTS = 2u;
static const uint DUST_CAST_VOLUMETRIC_SHADOW = 4u;
/* Bit 7 marks the new per-volume wake-strength encoding. Bits 3..6 store a
   4-bit 0..2x value. Legacy records without the marker retain 1.0x wakes. */
static const uint DUST_WAKE_STRENGTH_MARKER = 0x80u;
static const uint DUST_WAKE_STRENGTH_SHIFT = 3u;
static const uint DUST_WAKE_STRENGTH_MASK = 0x0fu;
/* High flag bits are runtime-only relevance masks populated by the CPU.
   Authoring flags remain in the low byte. This avoids walking every wake and
   local light at every raymarch sample for every cloud. */
static const uint DUST_WAKE_MASK_SHIFT = 8u;
static const uint DUST_LOCAL_LIGHT_MASK_SHIFT = 20u;
static const uint DUST_RELEVANCE_MASK = 0x0fffu;
static const float DUST_PI = 3.14159265358979323846;
/* Early termination is only a performance decision. Do not quantize a dark
   cloud to black: the old two-percent snap exposed a moving opacity contour. */
static const float DUST_EARLY_OUT_TRANSMITTANCE = 0.002;

uint DustHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float DustHashCell(int3 cell, uint seed)
{
    uint h = DustHash(seed ^ (asuint(cell.x) * 0x9e3779b9u));
    h = DustHash(h ^ (asuint(cell.y) * 0x85ebca6bu));
    h = DustHash(h ^ (asuint(cell.z) * 0xc2b2ae35u));
    return (float)(h & 0x00ffffffu) / 16777216.0;
}

float DustValueNoise(float3 p, uint seed)
{
    int3 base = int3(floor(p));
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = DustHashCell(base + int3(0,0,0), seed);
    float n100 = DustHashCell(base + int3(1,0,0), seed);
    float n010 = DustHashCell(base + int3(0,1,0), seed);
    float n110 = DustHashCell(base + int3(1,1,0), seed);
    float n001 = DustHashCell(base + int3(0,0,1), seed);
    float n101 = DustHashCell(base + int3(1,0,1), seed);
    float n011 = DustHashCell(base + int3(0,1,1), seed);
    float n111 = DustHashCell(base + int3(1,1,1), seed);
    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);
    return lerp(lerp(nx00, nx10, f.y), lerp(nx01, nx11, f.y), f.z);
}

float3 DustNoiseDomain(float3 p)
{
    /* Fixed non-axis-aligned basis. Sampling trilinear value noise directly
       in authored XYZ exposes its cell planes as crosses and tube-like bands
       when density and lighting contrast are pushed hard. */
    return float3(
        dot(p, float3( 0.00,  0.82,  0.57)),
        dot(p, float3(-0.92,  0.22, -0.32)),
        dot(p, float3(-0.39, -0.52,  0.76)));
}

float3 DustSeedOffset(uint seed)
{
    return float3(
        (float)(DustHash(seed ^ 0xa341316cu) & 1023u),
        (float)(DustHash(seed ^ 0xc8013ea4u) & 1023u),
        (float)(DustHash(seed ^ 0xad90777du) & 1023u)) * (1.0 / 37.0);
}

struct DustRotationContext
{
    float2 xSinCos;
    float2 ySinCos;
    float2 zSinCos;
};

DustRotationContext DustBuildRotationContext(DustVolume volume)
{
    DustRotationContext result;
    result.xSinCos = volume.rotationXSinCos;
    result.ySinCos = volume.rotationYSinCos;
    result.zSinCos = volume.rotationZSinCos;
    return result;
}

float3 DustWorldToLocalVectorFast(DustRotationContext rotation,
                                  float3 worldVector)
{
    float s = rotation.zSinCos.x;
    float c = rotation.zSinCos.y;
    worldVector = float3(worldVector.x*c - worldVector.y*s,
                         worldVector.x*s + worldVector.y*c,
                         worldVector.z);
    s = rotation.ySinCos.x;
    c = rotation.ySinCos.y;
    worldVector = float3(worldVector.x*c + worldVector.z*s,
                         worldVector.y,
                        -worldVector.x*s + worldVector.z*c);
    s = rotation.xSinCos.x;
    c = rotation.xSinCos.y;
    return float3(worldVector.x,
                  worldVector.y*c - worldVector.z*s,
                  worldVector.y*s + worldVector.z*c);
}

float3 DustBoundsHalfSize(DustVolume volume)
{
    float3 halfSize = max(volume.size * 0.5, float3(1.0, 1.0, 1.0));
    if (volume.shape == DUST_SHAPE_SPHERE)
    {
        float radius = max(1.0, (halfSize.x + halfSize.y + halfSize.z) / 3.0);
        halfSize = float3(radius, radius, radius);
    }
    /* Domain-warped density can protrude beyond the unwarped authoring shape.
       Expand only the ray-intersection bounds so the randomized silhouette is
       never sliced off by an invisible axis-aligned plane. */
    float variation = saturate(volume.shapeVariation * 0.75);
    return halfSize * (1.0 + 0.38 * variation);
}

float DustShapeEdge(DustVolume volume, DustRotationContext rotation,
                    float3 seedOffset, float3 worldPosition)
{
    float3 halfSize = max(volume.size * 0.5, float3(1.0, 1.0, 1.0));
    float3 volumeLocalPosition = DustWorldToLocalVectorFast(
        rotation, worldPosition - volume.position);
    float3 local;
    if (volume.shape == DUST_SHAPE_SPHERE)
    {
        float radius = max(1.0, (halfSize.x + halfSize.y + halfSize.z) / 3.0);
        local = volumeLocalPosition / radius;
    }
    else
    {
        local = volumeLocalPosition / halfSize;
    }

    float edge = volume.shape == DUST_SHAPE_BOX
        ? 1.0 - max(abs(local.x), max(abs(local.y), abs(local.z)))
        : 1.0 - length(local);

    float variation = saturate(volume.shapeVariation * 0.75);
    if (variation > 0.0001)
    {
        /* The former sin/cos vector warp had exact zero/crest planes. Under
           strong lighting those planes became a cross, and its small maximum
           displacement still left the underlying ellipsoid plainly visible.

           Treat the selected primitive only as an authoring coordinate system
           and displace its signed boundary with low-frequency, rotated 3D
           value noise. There are no analytic axial planes, while the zero-mean
           displacement preserves the cloud's authored position and scale. */
        float3 boundaryP = DustNoiseDomain(volumeLocalPosition) *
            max(volume.noiseScale * 0.115, 0.000002) + seedOffset * 0.29;
        float boundary0 = DustValueNoise(
            boundaryP, volume.shapeSeed ^ 0x85ebca6bu);
        float boundary1 = DustValueNoise(
            boundaryP * 2.07 + float3(11.3, 23.7, 7.9),
            volume.shapeSeed ^ 0xc2b2ae35u);
        float boundary2 = DustValueNoise(
            boundaryP * 4.19 + float3(31.1, 5.3, 17.7),
            volume.shapeSeed ^ 0x27d4eb2fu);
        float displacement =
            ((boundary0 * 0.57 + boundary1 * 0.29 + boundary2 * 0.14) - 0.5) *
            2.0;
        /* Large enough to erase the mathematical primitive at the high end,
           but smoothly proportional so existing low-variation clouds retain
           their established silhouette. */
        edge += displacement * (0.72 * variation);
    }
    return edge;
}

/* Conservative lower bound on distance to any potentially dense randomized
   core. Unlike DustShapeEdge this intentionally does not evaluate the domain
   warp's sin/cos field. It subtracts the maximum possible warp analytically,
   so a positive result is guaranteed empty enough for contribution testing. */
float DustConservativeOutsideWorld(DustVolume volume,
                                   DustRotationContext rotation,
                                   float3 authoredHalfSize,
                                   float smallestHalfAxis,
                                   float3 worldPosition)
{
    float3 localWorld = DustWorldToLocalVectorFast(
        rotation, worldPosition - volume.position);
    float3 local = localWorld / max(authoredHalfSize, float3(1.0, 1.0, 1.0));
    float variation = saturate(volume.shapeVariation * 0.75);
    /* Must cover the complete signed-boundary displacement used by
       DustShapeEdge so empty-space skipping can never cut off a noisy lobe. */
    float warpAxis = 0.72 * variation;
    float outsideNormalized;
    if (volume.shape == DUST_SHAPE_BOX)
    {
        float maxAxis = max(abs(local.x), max(abs(local.y), abs(local.z)));
        outsideNormalized = max(0.0, maxAxis - (1.0 + warpAxis));
    }
    else
    {
        const float sqrtThree = 1.7320508075688772;
        outsideNormalized = max(0.0,
            length(local) - (1.0 + warpAxis * sqrtThree));
    }
    return outsideNormalized * smallestHalfAxis;
}

float3 DustSafePerpendicular(float3 direction)
{
    float3 reference = abs(direction.y) < 0.85 ? float3(0.0, 1.0, 0.0)
                                                : float3(1.0, 0.0, 0.0);
    return normalize(cross(direction, reference));
}

void DustApplyShipWakes(float3 worldPosition, uint wakeMask,
                        float volumeWakeStrength,
                        out float3 densitySamplePosition,
                        out float densityMultiplier)
{
    densitySamplePosition = worldPosition;
    densityMultiplier = 1.0;
    [loop]
    for (uint wakeIndex = 0u; wakeIndex < DustWakeCount; ++wakeIndex)
    {
        if ((wakeMask & (1u << wakeIndex)) == 0u) continue;
        DustWake wake = DustWakes[wakeIndex];
        float wakeStrength = wake.strength * volumeWakeStrength;
        float bowStrength = wake.bowStrength * volumeWakeStrength;
        if (wakeStrength <= 0.001 || wake.radius <= 1.0 || wake.length <= 1.0)
            continue;

        float3 direction = normalize(wake.direction);
        float3 relative = worldPosition - wake.position;
        float axial = dot(relative, direction);
        if (axial > wake.radius * 2.4 ||
            axial < -(wake.length + wake.radius * 2.0))
            continue;

        float segmentAxial = clamp(axial, -wake.length, 0.0);
        float3 axisPoint = wake.position + direction * segmentAxial;
        float3 radial = worldPosition - axisPoint;
        float radialDistance = length(radial);
        if (radialDistance > wake.radius * 2.4)
            continue;

        float3 radialDirection = radialDistance > 0.001
            ? radial / radialDistance
            : DustSafePerpendicular(direction);
        float headFade = 1.0 - smoothstep(0.0, wake.radius * 1.6, axial);
        float tailFade = smoothstep(
            -(wake.length + wake.radius),
            -(wake.length - wake.radius * 0.35), axial);
        float segmentMask = saturate(headFade * tailFade);
        float core = (1.0 - smoothstep(
            wake.radius * 0.22, wake.radius * 1.10, radialDistance)) *
            segmentMask * wakeStrength;

        /* Advect the sampled density away from the ship path.  This moves the
           existing 3D noise field rather than drawing a second FX sprite. */
        float3 tangentCross = cross(direction, radialDirection);
        float tangentLength = length(tangentCross);
        float3 tangent = tangentLength > 0.001
            ? tangentCross / tangentLength
            : DustSafePerpendicular(direction);
        float swirl = sin(dot(worldPosition, float3(0.0011, 0.0017, 0.0013)) +
                          dot(wake.position, float3(0.0007, 0.0013, 0.0019)));
        densitySamplePosition -= radialDirection * (wake.radius * 0.34 * core);
        densitySamplePosition -= tangent * (wake.radius * 0.11 * core * swirl);

        /* The evacuated centre recovers smoothly as wake.strength decays. */
        densityMultiplier *= 1.0 - 0.84 * saturate(core);

        /* Dust pushed out of the centre builds a soft shoulder around the
           wake, preserving mass visually instead of merely erasing density. */
        float sideBand = 1.0 - saturate(
            abs(radialDistance - wake.radius * 1.20) /
            max(1.0, wake.radius * 0.58));
        densityMultiplier += 0.24 * sideBand * segmentMask * wakeStrength;

        /* A short-lived compressed bow wave sits just ahead of the moving
           hull.  Older trail records lose this much faster than their wake. */
        float3 bowCenter = wake.position + direction * wake.radius * 0.80;
        float bowDistance = length(worldPosition - bowCenter);
        float bowBand = 1.0 - saturate(
            abs(bowDistance - wake.radius * 1.05) /
            max(1.0, wake.radius * 0.52));
        float bowFront = smoothstep(-wake.radius * 0.25,
                                    wake.radius * 1.50, axial);
        densityMultiplier += 0.38 * bowBand * bowFront * bowStrength;
    }
    densityMultiplier = clamp(densityMultiplier, 0.04, 1.75);
}

float DustTargetStepLength(DustVolume volume)
{
    /* RTX-0089: the previous 18-slice march divided every current ray segment
       into 18 pieces.  Rotating the camera changed both the segment length and
       the phase of every sample, so a perfectly static 3D field appeared to
       boil against its editor bounds.  Choose a WORLD-UNIT step from the
       authored volume/noise instead.  It stays constant as the camera moves. */
    float scale = max(volume.noiseScale, 0.000005);
    float fineFeatureSize = 1.0 / max(scale * 1.65, 0.000001);
    float3 authoredSize = max(volume.size, float3(1.0, 1.0, 1.0));
    float minimumAxis = min(authoredSize.x, min(authoredSize.y, authoredSize.z));
    return clamp(min(fineFeatureSize * 0.48, minimumAxis / 18.0),
                 45.0, 220.0);
}

float DustSoftEnvelope(float edge)
{
    /* The authored sphere/box/ellipsoid is a density coordinate system, not
       a clipping primitive. Preserve the familiar interior feather, then add
       a smooth asymptotic exterior tail so density can cross the authoring
       boundary without ever meeting a mathematical wall. */
    float authoredMask = smoothstep(-0.06, 0.20, edge);
    float outside = max(0.0, -edge);
    float exteriorTail = 0.35 * exp(-6.0 * outside * outside);
    return saturate(authoredMask + (1.0 - authoredMask) * exteriorTail);
}

float DustVolumeWakeStrength(uint flags)
{
    if ((flags & DUST_WAKE_STRENGTH_MARKER) == 0u) return 1.0;
    uint code = (flags >> DUST_WAKE_STRENGTH_SHIFT) & DUST_WAKE_STRENGTH_MASK;
    if (code <= 8u) return (float)code / 8.0;
    return 1.0 + (float)(code - 8u) / 7.0;
}

float DustHighCoverageFill(float authoredCoverage)
{
    /* Coverage 1.0 is the established/default response. The upper half of the
       0..2 editor range is an intentional artist override: progressively fill
       noise voids until coverage 2.0 produces a continuous interior medium. */
    float t = saturate(authoredCoverage - 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float DustMaterialCoefficientScale(DustVolume volume)
{
    /* Keep the proven/default range unchanged, but give high-end authoring a
       physically useful optical-depth response. This scales sigmaT and sigmaS
       together, so scattering albedo/hue are preserved while thick materials
       actually become opaque. */
    float densityHigh = max(0.0, volume.density - 1.0);
    float materialExtinction = max(0.0, volume.absorption + volume.scattering);
    float extinctionHigh = max(0.0, materialExtinction - 1.15);
    return (1.0 + densityHigh * 0.30) *
           (1.0 + extinctionHigh * 0.12);
}

float DustDistanceFade(DustVolume volume, float3 cameraPosition)
{
    float fadeDistance = max(0.0, volume.precomputedPadding.x);
    float fadeStrength = max(0.0, volume.precomputedPadding.y);
    if (fadeStrength <= 0.0001 || fadeDistance <= 1.0) return 1.0;
    float3 halfSize = max(volume.size * 0.5, float3(1.0, 1.0, 1.0));
    float supportRadius = length(halfSize);
    float distanceFromSurface = max(0.0,
        length(volume.position - cameraPosition) - supportRadius);
    float beyond = max(0.0, distanceFromSurface - fadeDistance);
    float normalizedDistance = beyond / max(fadeDistance, 1000.0);
    return exp(-fadeStrength * normalizedDistance * normalizedDistance);
}

float DustDensityAt(DustVolume volume, DustRotationContext rotation,
                    float3 seedOffset, uint wakeMask, float3 worldPosition,
                    float sampleStepLength)
{
    float edge = DustShapeEdge(volume, rotation, seedOffset, worldPosition);
    float edgeMask = DustSoftEnvelope(edge);
    /* The integrator ignores density <= 1e-5. Wake modulation is clamped to
       1.75, so if even the maximum possible wake-amplified envelope cannot
       reach that threshold, all wake/noise work is provably dead. */
    if (max(0.0, volume.density) * edgeMask * 1.75 <= 0.00001)
        return 0.0;
    float3 densityPosition = worldPosition;
    float wakeMultiplier = 1.0;
    float volumeWakeStrength = DustVolumeWakeStrength(volume.flags);
    if (wakeMask != 0u && volumeWakeStrength > 0.0001)
    {
        DustApplyShipWakes(worldPosition, wakeMask, volumeWakeStrength,
                           densityPosition, wakeMultiplier);
    }
    float scale = max(volume.noiseScale, 0.000005);
    float3 localDensityPosition = DustNoiseDomain(
        DustWorldToLocalVectorFast(
            rotation, densityPosition - volume.position));

    /* Filter detail that is smaller than the raymarch footprint instead of
       allowing sub-step noise to alias into bright/dark temporal flicker.
       Because sampleStepLength is authored/world-derived, this filter does not
       breathe when the view direction changes. */
    float fineFootprint = sampleStepLength * scale * 1.65;
    float detailStability = 1.0 - smoothstep(0.38, 1.15, fineFootprint);
    float stableDetail = volume.noiseDetail * lerp(0.42, 1.0, detailStability);
    float detailBlend = saturate(0.15 + volume.noiseDetail * 0.30) *
                        lerp(0.18, 1.0, detailStability);
    float coverage = saturate(volume.coverage * 0.5);
    float threshold = lerp(0.70, 0.30, coverage);
    float transitionWidth = lerp(0.235, 0.18, detailStability);

    /* RTX-AAA exact FBM interval pruning. DustValueNoise is a trilinear blend
       of hash values in [0,1], so after octave zero we know rigorous lower and
       upper bounds for every remaining octave and the separate detail noise.
       If that interval is wholly below smoothstep's threshold the density is
       exactly zero; if wholly above its upper edge the cloud term is exactly
       one. This skips two FBM octaves plus detail noise without approximating
       a contributing sample. */
    float3 fbmPosition =
        localDensityPosition * (scale * 0.42) + seedOffset;
    uint fbmSeed = volume.shapeSeed ^ 0x243f6a88u;
    float highFrequency = saturate(stableDetail * 0.75 + 0.25);
    const float octave0Weight = 0.58;
    float octave1Weight = 0.29 * highFrequency;
    float octave2Weight = 0.145 * highFrequency;
    float fbmNorm = max(
        octave0Weight + octave1Weight + octave2Weight, 0.0001);
    float octave0 = DustValueNoise(fbmPosition, fbmSeed);
    float baseMinimum = octave0Weight * octave0 / fbmNorm;
    float baseMaximum = (octave0Weight * octave0 +
        octave1Weight + octave2Weight) / fbmNorm;
    float finalMinimum = lerp(baseMinimum, 0.0, detailBlend);
    float finalMaximum = lerp(baseMaximum, 1.0, detailBlend);

    float cloud;
    if (finalMaximum <= threshold)
    {
        cloud = 0.0;
    }
    else if (finalMinimum >= threshold + transitionWidth)
    {
        cloud = 1.0;
    }
    else
    {
        /* Continue the same rigorous interval test after each octave. Most
           transition samples do not actually need all remaining octaves.
           This is exact pruning: omitted value-noise terms are bounded in
           [0,1], so a sample is rejected/accepted only when no possible
           remaining value can cross the smoothstep interval. */
        float3 octave1Position =
            fbmPosition * 2.03 + float3(13.7, 7.1, 19.3);
        float octave1 = DustValueNoise(
            octave1Position, fbmSeed + 0x68bc21ebu);
        float total = octave0Weight * octave0 + octave1Weight * octave1;
        float base1Minimum = total / fbmNorm;
        float base1Maximum = (total + octave2Weight) / fbmNorm;
        float final1Minimum = lerp(base1Minimum, 0.0, detailBlend);
        float final1Maximum = lerp(base1Maximum, 1.0, detailBlend);
        if (final1Maximum <= threshold)
        {
            cloud = 0.0;
        }
        else if (final1Minimum >= threshold + transitionWidth)
        {
            cloud = 1.0;
        }
        else
        {
            float3 octave2Position =
                octave1Position * 2.03 + float3(13.7, 7.1, 19.3);
            total += octave2Weight * DustValueNoise(
                octave2Position, fbmSeed + 2u * 0x68bc21ebu);
            float baseNoise = total / fbmNorm;

            /* Before paying for fine breakup, bound the final blend using the
               known base value and detail in [0,1]. */
            float final2Minimum = lerp(baseNoise, 0.0, detailBlend);
            float final2Maximum = lerp(baseNoise, 1.0, detailBlend);
            if (final2Maximum <= threshold)
            {
                cloud = 0.0;
            }
            else if (final2Minimum >= threshold + transitionWidth)
            {
                cloud = 1.0;
            }
            else if (detailBlend <= 0.0001)
            {
                cloud = smoothstep(threshold, threshold + transitionWidth,
                                   baseNoise);
            }
            else
            {
                float detailNoise = DustValueNoise(
                    localDensityPosition * (scale * 1.65) +
                        seedOffset * 1.73,
                    volume.shapeSeed ^ 0xb7e15162u);
                float noise = lerp(baseNoise, detailNoise, detailBlend);
                cloud = smoothstep(
                    threshold, threshold + transitionWidth, noise);
            }
        }
    }
    cloud = lerp(cloud, 1.0, DustHighCoverageFill(volume.coverage));
    return max(0.0, volume.density) * edgeMask * cloud * wakeMultiplier;
}

float DustCoarseDensityAt(DustVolume volume, DustRotationContext rotation,
                          float3 seedOffset, float3 worldPosition)
{
    float edge = DustShapeEdge(volume, rotation, seedOffset, worldPosition);
    float edgeMask = DustSoftEnvelope(edge);
    float scale = max(volume.noiseScale * 0.40, 0.000004);
    float noise = DustValueNoise(
        DustNoiseDomain(DustWorldToLocalVectorFast(
            rotation, worldPosition - volume.position)) * scale + seedOffset,
        volume.shapeSeed ^ 0x517cc1b7u);
    float coverage = saturate(volume.coverage * 0.5);
    float threshold = lerp(0.68, 0.31, coverage);
    float cloud = smoothstep(threshold, threshold + 0.22, noise);
    cloud = lerp(cloud, 1.0, DustHighCoverageFill(volume.coverage));
    /* Keep the cheap volumetric-shadow probe independent from transient ship
       wakes. The primary density march carries the disturbance, while this
       avoids multiplying wake work by every key-shadow probe. */
    return max(0.0, volume.density) * edgeMask * cloud;
}

float DustPhaseHG(float anisotropy, float cosineTheta)
{
    float g = clamp(anisotropy, -0.92, 0.92);
    float g2 = g * g;
    float denom = max(0.015, 1.0 + g2 - 2.0 * g * cosineTheta);
    /* denom * sqrt(denom) is exactly denom^1.5 and avoids a general pow. */
    return (1.0 - g2) / (4.0 * DUST_PI * denom * sqrt(denom));
}

float DustKeyTransmittance(DustVolume volume, DustRotationContext rotation,
                           float3 seedOffset, float3 samplePosition,
                           float3 directionToSource)
{
    if ((volume.flags & DUST_CAST_VOLUMETRIC_SHADOW) == 0u) return 1.0;
    float3 halfSize = DustBoundsHalfSize(volume);
    float shadowLength = length(halfSize) * 1.35;
    const uint shadowSteps = 3u;
    float stepLength = shadowLength / (float)shadowSteps;
    float opticalDepth = 0.0;
    [unroll]
    for (uint i = 0u; i < shadowSteps; ++i)
    {
        float3 p = samplePosition + directionToSource *
            (stepLength * ((float)i + 0.75));
        float density = DustCoarseDensityAt(volume, rotation, seedOffset, p);
        opticalDepth += density * stepLength;
    }
    float extinction = max(0.0, volume.absorption + volume.scattering);
    return exp(-opticalDepth * extinction * 0.00022 *
               DustMaterialCoefficientScale(volume));
}

float3 DustLightingAt(DustVolume volume, DustRotationContext rotation,
                      float3 seedOffset, uint localLightMask,
                      float3 samplePosition, float3 cameraRayDirection)
{
    /* Cloud lighting is authored radiance, not a pre-tonemapped glow value.
       Keep source RGB/intensity linear here so F12 changes remain visible and
       hue survives into the medium. The final volumetric contribution gets a
       hue-preserving shoulder after integration instead of crushing each light
       before scattering. */
    float3 lighting = DustAmbientColorStrength.rgb *
                      DustAmbientColorStrength.a * 0.68;
    if ((volume.flags & DUST_RECEIVE_MISSION_KEY) != 0u &&
        DustSunDirectionValid.w > 0.5 &&
        DustRayUv01SunStrength.w > 0.0001)
    {
        float3 toSun = normalize(DustSunDirectionValid.xyz);
        float phase = DustPhaseHG(volume.anisotropy,
                                  dot(toSun, cameraRayDirection));
        float shadow = DustKeyTransmittance(
            volume, rotation, seedOffset, samplePosition, toSun);
        /* Dense clouds still receive a small multiple-scattering/key fill so
           self-shadowing creates depth instead of driving the interior to
           neutral black. This fill carries the authored key color. */
        float effectiveShadow = 0.18 + shadow * 0.82;
        lighting += DustSunColor.rgb * DustRayUv01SunStrength.w *
                    effectiveShadow * (0.18 + phase * 2.60);
        /* Single scattering drives a forward-scattering cloud almost black
           when viewed away from the key light. Real dusty media recycle some
           of that energy through higher-order scattering. This conservative
           isotropic term prevents black cut-outs while retaining directional
           highlights and authored absorption. */
        float scatteringAlbedo = saturate(volume.scattering /
            max(0.001, volume.scattering + volume.absorption));
        lighting += DustSunColor.rgb * DustRayUv01SunStrength.w *
                    (0.20 * scatteringAlbedo * scatteringAlbedo);
    }

    if ((volume.flags & DUST_RECEIVE_LOCAL_LIGHTS) != 0u &&
        localLightMask != 0u)
    {
        [loop]
        for (uint lightIndex = 0u; lightIndex < DustLightCount; ++lightIndex)
        {
            if ((localLightMask & (1u << lightIndex)) == 0u) continue;
            DustLight light = DustLights[lightIndex];
            float3 delta = light.position - samplePosition;
            float distanceSquared = dot(delta, delta);
            float radiusSquared = light.radius * light.radius;
            if (distanceSquared <= 0.000001 || distanceSquared >= radiusSquared)
                continue;
            /* Reject out-of-range lights before paying for sqrt. */
            float distanceToLight = sqrt(distanceSquared);
            float3 toLight = delta / distanceToLight;
            float range = saturate(1.0 - distanceToLight / light.radius);
            float attenuation = range * range;
            if (light.type == 3u)
            {
                float spotCosine = dot(-toLight, light.direction);
                float innerCosine = max(light.coneCos, light.edgeCos);
                float outerCosine = min(light.coneCos, light.edgeCos);
                attenuation *= smoothstep(outerCosine, innerCosine, spotCosine);
            }
            float phase = DustPhaseHG(volume.anisotropy,
                                      dot(toLight, cameraRayDirection));
            /* Preserve the actual authored RGB and intensity. The previous
               per-light compression asymptoted near 1.2, so 2x/4x/8x lights
               became almost indistinguishable inside clouds. */
            float3 emitted = max(light.color, 0.0) * max(light.intensity, 0.0);
            lighting += emitted * attenuation * (0.12 + phase * 1.80);
        }
    }
    return max(lighting, 0.0);
}

float3 DustToneVolumetricContribution(float3 radiance, float transmittance)
{
    radiance = max(radiance, 0.0);
    float opacity = saturate(1.0 - transmittance);
    if (opacity <= 0.000001) return 0.0;

    float peak = max(radiance.r, max(radiance.g, radiance.b));
    if (peak > 0.82)
    {
        /* A scalar shoulder preserves RGB ratios. Bright authored lights can
           still brighten/tint the medium without bleaching it to white. */
        float excess = peak - 0.82;
        float mappedPeak = 0.82 + 0.17 * (1.0 - exp(-excess * 0.72));
        radiance *= mappedPeak / max(peak, 0.000001);
        peak = mappedPeak;
    }

    /* The presenter is already display-referred, so keep the integrated
       medium explicitly premultiplied by its opacity. The old separate
       contribution tone-map could turn a thin/high-phase fog segment into an
       almost-white additive layer over a ship. This hue-preserving cap makes
       the final operation a stable scene*T + medium*(1-T) composite. */
    float maximumPremultipliedPeak = 0.88 * opacity;
    if (peak > maximumPremultipliedPeak && peak > 0.000001)
        radiance *= maximumPremultipliedPeak / peak;
    return radiance;
}

float3 DustWorldGridActiveDimensions()
{
    return max(float3(DustGridMinWidth.w, DustGridSizeHeight.w,
                      PresenterPadding.x), float3(1.0, 1.0, 1.0));
}

float3 DustWorldGridTextureCoordinate(float3 worldPosition)
{
    uint textureWidth, textureHeight, textureDepth;
    DustFroxelField.GetDimensions(textureWidth, textureHeight, textureDepth);
    float3 gridSize = max(DustGridSizeHeight.xyz,
                          float3(0.0001, 0.0001, 0.0001));
    float3 gridUv = saturate(
        (worldPosition - DustGridMinWidth.xyz) / gridSize);
    float3 activeDimensions = DustWorldGridActiveDimensions();

    /* The physical cache is a persistent 160^3 allocation while only the
       active prefix is written this frame. Clamp to the centres of the first
       and last live texels so hardware trilinear filtering can never blend
       with inactive storage at a positive active-grid face. */
    float3 activeCell = gridUv * activeDimensions;
    activeCell = clamp(
        activeCell,
        float3(0.5, 0.5, 0.5),
        max(float3(0.5, 0.5, 0.5),
            activeDimensions - float3(0.5, 0.5, 0.5)));
    return activeCell /
        float3(max(1u, textureWidth), max(1u, textureHeight),
               max(1u, textureDepth));
}

bool DustRayWorldGridInterval(float3 rayOrigin, float3 rayDirection,
                              out float entryDistance,
                              out float exitDistance)
{
    float3 boundsMin = DustGridMinWidth.xyz;
    float3 boundsMax = boundsMin + DustGridSizeHeight.xyz;
    float tMin = 0.0;
    float tMax = 3.0e30;

    [unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        float origin = rayOrigin[axis];
        float direction = rayDirection[axis];
        float minimumValue = boundsMin[axis];
        float maximumValue = boundsMax[axis];
        if (abs(direction) < 0.0000001)
        {
            if (origin < minimumValue || origin > maximumValue)
            {
                entryDistance = 0.0;
                exitDistance = 0.0;
                return false;
            }
            continue;
        }
        float inverseDirection = 1.0 / direction;
        float a = (minimumValue - origin) * inverseDirection;
        float b = (maximumValue - origin) * inverseDirection;
        float nearDistance = min(a, b);
        float farDistance = max(a, b);
        tMin = max(tMin, nearDistance);
        tMax = min(tMax, farDistance);
        if (tMax < tMin)
        {
            entryDistance = 0.0;
            exitDistance = 0.0;
            return false;
        }
    }
    entryDistance = tMin;
    exitDistance = tMax;
    return tMax > max(0.0, tMin);
}

#ifdef DUST_FROXEL_COMPUTE
[numthreads(4, 4, 4)]
void BuildDustFroxelCS(uint3 cell : SV_DispatchThreadID)
{
    uint textureWidth, textureHeight, textureDepth;
    DustFroxelFieldRW.GetDimensions(textureWidth, textureHeight, textureDepth);
    bool buildFarCascade = PresenterPadding.y > 1.5;
    uint3 activeDimensions = buildFarCascade
        ? uint3(textureWidth, textureHeight, textureDepth)
        : uint3(max(1.0, DustGridMinWidth.w),
                max(1.0, DustGridSizeHeight.w),
                max(1.0, PresenterPadding.x));
    activeDimensions = min(activeDimensions,
        uint3(textureWidth, textureHeight, textureDepth));
    if (any(cell >= activeDimensions)) return;

    float3 gridUv = (float3(cell) + 0.5) / float3(activeDimensions);
    float3 nearCenter = DustGridMinWidth.xyz + DustGridSizeHeight.xyz * 0.5;
    float3 gridSize = DustGridSizeHeight.xyz *
                      (buildFarCascade ? 4.0 : 1.0);
    float3 gridMinimum = buildFarCascade
        ? nearCenter - gridSize * 0.5
        : DustGridMinWidth.xyz;
    float3 worldPosition = gridMinimum + gridUv * gridSize;
    float3 voxelSize = gridSize / float3(activeDimensions);
    float sampleStepLength = max(1.0,
        max(voxelSize.x, max(voxelSize.y, voxelSize.z)));
    float3 cameraVector = worldPosition - DustCameraPositionMaxDistance.xyz;
    float cameraVectorLengthSquared = dot(cameraVector, cameraVector);
    float3 cameraRayDirection = cameraVectorLengthSquared > 0.000001
        ? cameraVector * rsqrt(cameraVectorLengthSquared)
        : float3(0.0, 0.0, 1.0);

    float3 scatteringSource = 0.0;
    float extinction = 0.0;
    [loop]
    for (uint volumeIndex = 0u; volumeIndex < DustVolumeCount; ++volumeIndex)
    {
        DustVolume volume = DustVolumes[volumeIndex];
        if (volume.enabled == 0u || volume.density <= 0.00001) continue;

        DustRotationContext rotation = DustBuildRotationContext(volume);
        float volumeDistanceFade = DustDistanceFade(
            volume, DustCameraPositionMaxDistance.xyz);
        if (volumeDistanceFade <= 0.0001) continue;
        float3 authoredHalfSize = max(volume.size * 0.5,
            float3(1.0, 1.0, 1.0));
        if (volume.shape == DUST_SHAPE_SPHERE)
        {
            float radius = max(1.0,
                (authoredHalfSize.x + authoredHalfSize.y + authoredHalfSize.z) /
                3.0);
            authoredHalfSize = float3(radius, radius, radius);
        }
        float smallestHalfAxis = min(authoredHalfSize.x,
            min(authoredHalfSize.y, authoredHalfSize.z));
        float outsideWorldDistance = DustConservativeOutsideWorld(
            volume, rotation, authoredHalfSize, smallestHalfAxis, worldPosition);
        if (outsideWorldDistance > 0.0)
        {
            float envelopeUpper = DustSoftEnvelope(
                -outsideWorldDistance / max(1.0, smallestHalfAxis));
            if (max(0.0, volume.density) * envelopeUpper * 1.75 <= 0.00001)
                continue;
        }

        uint wakeMask = (volume.flags >> DUST_WAKE_MASK_SHIFT) &
                        DUST_RELEVANCE_MASK;
        uint localLightMask = (volume.flags >> DUST_LOCAL_LIGHT_MASK_SHIFT) &
                              DUST_RELEVANCE_MASK;
        float density = DustDensityAt(
            volume, rotation, volume.seedOffset, wakeMask, worldPosition,
            sampleStepLength) * volumeDistanceFade;
        if (density <= 0.00001) continue;

        float coefficientScale = 0.00034 *
            DustMaterialCoefficientScale(volume);
        float sigmaT = density * max(0.00001,
            volume.absorption + volume.scattering) * coefficientScale;
        float sigmaS = density * max(0.0, volume.scattering) * coefficientScale;
        float3 lighting = DustLightingAt(
            volume, rotation, volume.seedOffset, localLightMask,
            worldPosition, cameraRayDirection);
        scatteringSource += lighting * max(volume.color, 0.0) * sigmaS;
        extinction += sigmaT;
    }

    /* A direct procedural continuation now begins at the clipmap exit, so the
       cached medium must remain physically continuous through its last cell.
       Fading the cache itself was the distance-dependent disappearance. */
    DustFroxelFieldRW[cell] = float4(scatteringSource, extinction);
}
#endif

float3 ApplyVolumetricDustFroxel(float2 uv, float3 scene,
                                 float sceneDistance)
{
    float3 rayVector = DustRayUv00.xyz +
        uv.x * (DustRayUv10.xyz - DustRayUv00.xyz) +
        uv.y * (DustRayUv01SunStrength.xyz - DustRayUv00.xyz);
    float3 rayDirection = normalize(rayVector);
    float3 rayOrigin = DustCameraPositionMaxDistance.xyz;

    float entryDistance;
    float exitDistance;
    if (!DustRayWorldGridInterval(rayOrigin, rayDirection,
                                  entryDistance, exitDistance))
        return scene;
    float startDistance = max(0.0, entryDistance);
    float fullEndDistance = exitDistance;
    float endDistance = min(fullEndDistance, sceneDistance);
    if (endDistance <= startDistance) return scene;

    float3 activeDimensions = DustWorldGridActiveDimensions();
    float3 voxelSize = DustGridSizeHeight.xyz / activeDimensions;
    float nominalStepLength = max(1.0,
        min(voxelSize.x, min(voxelSize.y, voxelSize.z)) * 0.90);

    /* IMPORTANT: choose the integration lattice from the full world-cache
       interval, never from opaque-scene depth. Previously a ship shortened the
       span, which changed every sample position/step length along that pixel.
       The cloud therefore changed numerically at the ship silhouette and could
       flip from dark/purple to bright/white with angle. Scene depth may stop
       the lattice, but it no longer defines the lattice. */
    float fullSpan = fullEndDistance - startDistance;
    const uint maximumIntegrationSteps = 192u;
    uint stepCount = max(1u, (uint)ceil(fullSpan / nominalStepLength));
    stepCount = min(stepCount, maximumIntegrationSteps);
    float integrationLength = fullSpan / max(1.0, (float)stepCount);

    float3 accumulated = 0.0;
    float transmittance = 1.0;
    [loop]
    for (uint stepIndex = 0u; stepIndex < stepCount; ++stepIndex)
    {
        float segmentStart = startDistance +
            (float)stepIndex * integrationLength;
        if (segmentStart >= endDistance) break;
        float segmentEnd = min(segmentStart + integrationLength, endDistance);
        float segmentLength = segmentEnd - segmentStart;
        if (segmentLength <= 0.000001) break;
        float sampleDistance = segmentStart + segmentLength * 0.5;
        float3 worldPosition = rayOrigin + rayDirection * sampleDistance;
        float3 textureCoordinate = DustWorldGridTextureCoordinate(worldPosition);
        float4 medium = DustFroxelField.SampleLevel(
            SourceSampler, textureCoordinate, 0.0);
        float extinction = max(0.0, medium.a);
        if (extinction <= 0.00000001) continue;
        float opticalDepth = extinction * segmentLength;
        float stepTransmittance = exp(-opticalDepth);
        float alpha = 1.0 - stepTransmittance;
        float3 sourceFunction = max(medium.rgb, 0.0) /
            max(extinction, 0.00000001);
        accumulated += transmittance * sourceFunction * alpha;
        transmittance *= stepTransmittance;
        if (transmittance < DUST_EARLY_OUT_TRANSMITTANCE) break;
    }
    accumulated = DustToneVolumetricContribution(accumulated, transmittance);
    return max(scene * transmittance + accumulated, 0.0);
}

float3 DustFarGridSize()
{
    return DustGridSizeHeight.xyz * 4.0;
}

float3 DustFarGridMinimum()
{
    float3 nearCenter = DustGridMinWidth.xyz + DustGridSizeHeight.xyz * 0.5;
    return nearCenter - DustFarGridSize() * 0.5;
}

bool DustRayFarGridInterval(float3 rayOrigin, float3 rayDirection,
                            out float entryDistance, out float exitDistance)
{
    float3 boundsMin = DustFarGridMinimum();
    float3 boundsMax = boundsMin + DustFarGridSize();
    float tMin = 0.0;
    float tMax = 3.0e30;
    [unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        if (abs(rayDirection[axis]) < 0.0000001)
        {
            if (rayOrigin[axis] < boundsMin[axis] ||
                rayOrigin[axis] > boundsMax[axis])
            {
                entryDistance = 0.0;
                exitDistance = 0.0;
                return false;
            }
            continue;
        }
        float inverseDirection = 1.0 / rayDirection[axis];
        float a = (boundsMin[axis] - rayOrigin[axis]) * inverseDirection;
        float b = (boundsMax[axis] - rayOrigin[axis]) * inverseDirection;
        tMin = max(tMin, min(a, b));
        tMax = min(tMax, max(a, b));
        if (tMax < tMin)
        {
            entryDistance = 0.0;
            exitDistance = 0.0;
            return false;
        }
    }
    entryDistance = tMin;
    exitDistance = tMax;
    return tMax > max(0.0, tMin);
}

float3 ApplyVolumetricDustFarFroxel(float2 uv, float3 scene,
                                    float sceneDistance,
                                    float minimumDistance)
{
    float3 rayVector = DustRayUv00.xyz +
        uv.x * (DustRayUv10.xyz - DustRayUv00.xyz) +
        uv.y * (DustRayUv01SunStrength.xyz - DustRayUv00.xyz);
    float3 rayDirection = normalize(rayVector);
    float3 rayOrigin = DustCameraPositionMaxDistance.xyz;
    float entryDistance;
    float exitDistance;
    if (!DustRayFarGridInterval(rayOrigin, rayDirection,
                                entryDistance, exitDistance))
        return scene;
    float startDistance = max(minimumDistance, max(0.0, entryDistance));
    float endDistance = min(exitDistance, sceneDistance);
    if (endDistance <= startDistance) return scene;

    uint width, height, depth;
    DustFarFroxelField.GetDimensions(width, height, depth);
    float3 gridSize = DustFarGridSize();
    float3 gridMinimum = DustFarGridMinimum();
    float3 voxelSize = gridSize /
        float3(max(1u, width), max(1u, height), max(1u, depth));
    float stepLength = max(1.0,
        min(voxelSize.x, min(voxelSize.y, voxelSize.z)) * 0.90);
    uint stepCount = min(128u, max(1u,
        (uint)ceil((endDistance - startDistance) / stepLength)));
    float integrationLength = (endDistance - startDistance) /
                              max(1.0, (float)stepCount);
    float3 accumulated = 0.0;
    float transmittance = 1.0;
    [loop]
    for (uint stepIndex = 0u; stepIndex < stepCount; ++stepIndex)
    {
        float sampleDistance = startDistance +
            ((float)stepIndex + 0.5) * integrationLength;
        float3 worldPosition = rayOrigin + rayDirection * sampleDistance;
        float3 gridUv = saturate((worldPosition - gridMinimum) / gridSize);
        float3 dimensions = float3(max(1u, width), max(1u, height),
                                   max(1u, depth));
        float3 cell = clamp(gridUv * dimensions, 0.5, dimensions - 0.5);
        float4 medium = DustFarFroxelField.SampleLevel(
            SourceSampler, cell / dimensions, 0.0);
        float extinction = max(0.0, medium.a);
        if (extinction <= 0.00000001) continue;
        float stepTransmittance = exp(-extinction * integrationLength);
        float alpha = 1.0 - stepTransmittance;
        float3 sourceFunction = max(medium.rgb, 0.0) /
            max(extinction, 0.00000001);
        accumulated += transmittance * sourceFunction * alpha;
        transmittance *= stepTransmittance;
        if (transmittance < DUST_EARLY_OUT_TRANSMITTANCE) break;
    }
    accumulated = DustToneVolumetricContribution(accumulated, transmittance);
    return max(scene * transmittance + accumulated, 0.0);
}

float DustVisibleSceneDistance(float2 uv, float3 rayVector)
{
    if (DustSunColor.w < 0.5) return DustCameraPositionMaxDistance.w;
    uint depthWidth, depthHeight;
    SceneDepth.GetDimensions(depthWidth, depthHeight);
    if (depthWidth == 0u || depthHeight == 0u) return DustCameraPositionMaxDistance.w;
    /* Fullscreen presenter UVs are bottom-up, while the native D3D12 depth
       attachment is top-down. Sampling depth with presenter Y produced a
       vertically mirrored, ship-shaped hole that moved through otherwise
       world-locked dust during camera pitch. */
    float2 depthUv = float2(uv.x, 1.0 - uv.y);
    uint2 pixel = min(uint2(depthUv * float2(depthWidth, depthHeight)),
                      uint2(depthWidth - 1u, depthHeight - 1u));
    float depth = SceneDepth.Load(int3(pixel, 0));
    /* Background domes are deliberately drawn with depth testing disabled,
       but legacy/native state restoration can still leave their extreme-far
       raster depth in a small number of pixels.  Never terminate a volume on
       that far-plane fringe: doing so projects the dome's triangle topology
       into otherwise smooth dust as a large cross.  Real ships, asteroids and
       debris sit comfortably in front of this guard band. */
    if (depth >= 0.99990) return DustCameraPositionMaxDistance.w;

    /* Native raster converts OpenGL clip Z to D3D [0,1] with
       zD3D = 0.5*(zGL+w). Reconstruct the original GL NDC Z and solve the
       exact perspective equation using P[10]/P[14]. This is the actual visible
       raster depth, never DXR/colour/alpha inference. */
    float ndcZ = depth * 2.0 - 1.0;
    float denominator = ndcZ + DustRayUv00.w;
    if (abs(denominator) <= 0.000001) return DustCameraPositionMaxDistance.w;
    float viewDepth = DustRayUv10.w / denominator;
    if (viewDepth <= 0.0 || viewDepth >= DustCameraPositionMaxDistance.w)
        return DustCameraPositionMaxDistance.w;
    float rayDistance = viewDepth * length(rayVector);
    /* Bias termination slightly toward the camera to eliminate bright dust
       halos leaking around an opaque hull/asteroid due to depth quantization. */
    return max(0.0, rayDistance - max(4.0, rayDistance * 0.001));
}

bool DustRayVolumeInterval(DustVolume volume,
                           DustRotationContext rotation,
                           float3 rayOrigin, float3 rayDirection,
                           out float entryDistance,
                           out float exitDistance)
{
    /* DustSoftEnvelope is already below the integrator's useful precision by
       2.6 authored radii.  Intersect that conservative, rotated support so a
       ray begins before every visible tail and finishes after it.  Unlike the
       old iteration watchdog, these endpoints belong to the authored WORLD
       volume and cannot form a camera-facing cutoff plane. */
    const float supportRadius = 2.6;
    float3 halfSize = max(volume.size * 0.5, float3(1.0, 1.0, 1.0));
    if (volume.shape == DUST_SHAPE_SPHERE)
    {
        float radius = max(1.0,
            (halfSize.x + halfSize.y + halfSize.z) / 3.0);
        halfSize = float3(radius, radius, radius);
    }
    float3 localOrigin = DustWorldToLocalVectorFast(
        rotation, rayOrigin - volume.position) / halfSize;
    float3 localDirection = DustWorldToLocalVectorFast(
        rotation, rayDirection) / halfSize;

    if (volume.shape != DUST_SHAPE_BOX)
    {
        float a = dot(localDirection, localDirection);
        float b = dot(localOrigin, localDirection);
        float c = dot(localOrigin, localOrigin) -
                  supportRadius * supportRadius;
        float discriminant = b * b - a * c;
        if (a <= 1.0e-12 || discriminant < 0.0)
        {
            entryDistance = 0.0;
            exitDistance = 0.0;
            return false;
        }
        float root = sqrt(discriminant);
        entryDistance = (-b - root) / a;
        exitDistance = (-b + root) / a;
        return exitDistance > max(0.0, entryDistance);
    }

    float tMin = -3.0e30;
    float tMax = 3.0e30;
    [unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        if (abs(localDirection[axis]) < 1.0e-10)
        {
            if (abs(localOrigin[axis]) > supportRadius)
            {
                entryDistance = 0.0;
                exitDistance = 0.0;
                return false;
            }
            continue;
        }
        float inverseDirection = 1.0 / localDirection[axis];
        float a = (-supportRadius - localOrigin[axis]) * inverseDirection;
        float b = ( supportRadius - localOrigin[axis]) * inverseDirection;
        tMin = max(tMin, min(a, b));
        tMax = min(tMax, max(a, b));
    }
    entryDistance = tMin;
    exitDistance = tMax;
    return tMax > max(0.0, tMin);
}

float3 ApplyVolumetricDust(float2 uv, float3 scene, bool forceDirect,
                           out float outputTransmittance)
{
    outputTransmittance = 1.0;
    if (DustRenderEnabled == 0u || DustVolumeCount == 0u) return scene;

    /* HARD WORLD-LOCK FIX: the three presenter-UV camera-plane vectors are
       rebuilt from the ACTUAL world render view matrix each frame inside
       ModernGraphics.cpp.  The legacy external corner-ray callback may still
       run for compatibility, but it is overwritten before dust presentation. */
    float3 rayVector = DustRayUv00.xyz +
        uv.x * (DustRayUv10.xyz - DustRayUv00.xyz) +
        uv.y * (DustRayUv01SunStrength.xyz - DustRayUv00.xyz);
    float3 rayDirection = normalize(rayVector);
    float3 rayOrigin = DustCameraPositionMaxDistance.xyz;
    /* The hard world lock remains authoritative for where density exists.
       Geometry now affects only the far integration distance, using the exact
       D32 depth attachment produced by the visible native raster pass. */
    float sceneDistance = DustVisibleSceneDistance(uv, rayVector);

    /* The high-resolution clipmap owns the near field. Beyond its ray exit,
       continue with the exact procedural marcher so distant clouds remain
       visible without diluting near-field cache resolution. */
    bool froxelAvailable = PresenterPadding.y > 0.5 && !forceDirect;
    if (froxelAvailable)
    {
        float cacheEntryDistance;
        float cacheExitDistance;
        float nearExitDistance = 0.0;
        if (DustRayWorldGridInterval(rayOrigin, rayDirection,
                                     cacheEntryDistance, cacheExitDistance))
            nearExitDistance = max(0.0, cacheExitDistance);
        float3 farComposite = ApplyVolumetricDustFarFroxel(
            uv, scene, sceneDistance, nearExitDistance);
        return ApplyVolumetricDustFroxel(
            uv, farComposite, sceneDistance);
    }

    float3 accumulated = 0.0;
    float transmittance = 1.0;
    [loop]
    for (uint volumeIndex = 0u; volumeIndex < DustVolumeCount; ++volumeIndex)
    {
        DustVolume volume = DustVolumes[volumeIndex];
        if (volume.enabled == 0u || volume.density <= 0.00001 ||
            transmittance < 0.008) continue;
        /* Rotation and hash-derived phase are invariant for every ray sample
           in this volume. RTX-AAA computes them once instead of repeating
           trigonometry/hash work in density and shadow probes. */
        DustRotationContext rotation = DustBuildRotationContext(volume);
        float volumeDistanceFade = DustDistanceFade(volume, rayOrigin);
        if (volumeDistanceFade <= 0.0001) continue;
        float3 seedOffset = volume.seedOffset;
        uint wakeMask = (volume.flags >> DUST_WAKE_MASK_SHIFT) &
                        DUST_RELEVANCE_MASK;
        uint localLightMask = (volume.flags >> DUST_LOCAL_LIGHT_MASK_SHIFT) &
                              DUST_RELEVANCE_MASK;
        /* Distance-guided boundaryless traversal.

           IMPORTANT: the authored box/sphere/ellipsoid is NEVER intersected
           to obtain a ray entry or exit distance. It is only queried as a
           continuous density/distance field. This keeps the working
           boundaryless behaviour while recovering performance with
           conservative empty-space skipping.

           The ray remains on a deterministic world-space lattice. Far from
           meaningful density we advance by multiple whole lattice cells at
           once; near any span whose maximum possible optical depth could be
           visible we automatically fall back to one cell. No volume-shaped
           surface can therefore become a render cutoff. */
        float3 authoredHalfSize = max(volume.size * 0.5,
            float3(1.0, 1.0, 1.0));
        if (volume.shape == DUST_SHAPE_SPHERE)
        {
            float radius = max(1.0,
                (authoredHalfSize.x + authoredHalfSize.y + authoredHalfSize.z) /
                3.0);
            authoredHalfSize = float3(radius, radius, radius);
        }
        float smallestHalfAxis = min(authoredHalfSize.x,
            min(authoredHalfSize.y, authoredHalfSize.z));

        /* HARD WORLD-LOCK FIX: always use the authored/noise-derived WORLD
           step.  The large-volume 48-sample LOD created very coarse camera-ray
           slice structure on huge clouds; correctness and angular stability take
           priority here over that optimization. */
        float targetStepLength = DustTargetStepLength(volume);
        /* Half-resolution temporal reconstruction resolves sub-step detail;
           a 1.8x screen-pass cadence keeps a typical cloud near 48-80 retained
           samples instead of repeating full-resolution work four times. */
        float baseStepLength = targetStepLength *
            (forceDirect ? 1.80 : 1.0);

        float volumeEntryDistance;
        float volumeExitDistance;
        if (!DustRayVolumeInterval(volume, rotation, rayOrigin, rayDirection,
                                   volumeEntryDistance, volumeExitDistance))
            continue;
        float startDistance = max(0.0, volumeEntryDistance);
        float endDistance = min(sceneDistance, volumeExitDistance);
        if (endDistance <= startDistance) continue;

        /* Cover the complete world-volume interval within the fixed budget.
           The previous marcher kept a fine step but simply stopped after 128
           contributing cells. On deep clouds that exposed a view-aligned
           cross-section—a literal sprite. Increasing the step when necessary
           preserves the whole 3D thickness instead of truncating it. */
        const uint maximumMarchIterations = 160u;
        float intervalLength = endDistance - startDistance;
        baseStepLength = max(baseStepLength,
            intervalLength / (float)maximumMarchIterations);
        uint intervalStepCount = max(1u,
            (uint)ceil(intervalLength / baseStepLength));
        intervalStepCount = min(intervalStepCount, maximumMarchIterations);
        float integrationCellLength = intervalLength /
            max(1.0, (float)intervalStepCount);
        /* A common half-cell phase across every ray creates concentric,
           view-centred shells. Use a smooth world-anchored phase derived from
           the volume entry point; its expected value remains one half. */
        float3 entryWorldPosition = rayOrigin + rayDirection * startDistance;
        float3 entryNoisePosition = DustNoiseDomain(
            DustWorldToLocalVectorFast(
                rotation, entryWorldPosition - volume.position));
        float marchPhase = lerp(0.18, 0.82, DustValueNoise(
            entryNoisePosition * max(volume.noiseScale * 0.19, 0.000002) +
                seedOffset * 0.41,
            volume.shapeSeed ^ 0x6c8e9cf5u));
        float currentDistance = startDistance +
                                integrationCellLength * marchPhase;

        float extinctionCoefficient = max(0.00001,
            volume.absorption + volume.scattering) * 0.00034 *
            DustMaterialCoefficientScale(volume);

        /* Lighting varies much more slowly through a cloud than extinction.
           Density is still integrated at every retained lattice sample, but
           expensive sun-shadow probes and local-light phase evaluation are
           amortized over several neighboring density samples. Local lights
           refresh twice as often as mission-only lighting. */
        float3 cachedLighting = 0.0;
        bool cachedLightingValid = false;
        float nextLightingDistance = -3.0e30;
        float lightingInterval = baseStepLength *
            (localLightMask != 0u ? 2.0 : 4.0);
        lightingInterval = clamp(lightingInterval, baseStepLength, 1200.0);

        [loop]
        for (uint stepIndex = 0u;
             stepIndex < intervalStepCount; ++stepIndex)
        {
            if (currentDistance > endDistance) break;

            float3 samplePosition = rayOrigin +
                rayDirection * currentDistance;

            /* Cheap conservative distance guidance. The previous unbounded
               version still evaluated the full randomized DustShapeEdge --
               including domain-warp trig -- at every skipped lattice point.
               This analytic bound removes the maximum possible warp first,
               so far-field traversal needs only one precomputed rotation. */
            float outsideWorldDistance = DustConservativeOutsideWorld(
                volume, rotation, authoredHalfSize, smallestHalfAxis,
                samplePosition);
            /* The contribution bound below validates the entire skipped
               span, so far-empty traversal can be aggressive without moving
               any visible density boundary. */
            float desiredSkipLength = max(integrationCellLength,
                outsideWorldDistance * 0.85);
            uint skipCells = max(1u,
                (uint)floor(desiredSkipLength / integrationCellLength));
            skipCells = min(skipCells, intervalStepCount - stepIndex);
            float skipLength = integrationCellLength * (float)skipCells;

            /* Bound the densest point anywhere in the skipped span. Distance
               to the warped core can shrink by at most skipLength along a ray.
               If even that worst case is optically invisible, skipping is
               mathematically contribution-safe and cannot form a clip shell. */
            float residualOutside = max(0.0,
                outsideWorldDistance - skipLength);
            float envelopeUpper = outsideWorldDistance > 0.0
                ? DustSoftEnvelope(-residualOutside /
                    max(smallestHalfAxis, 1.0))
                : 1.0;
            float maximumDensity = max(0.0, volume.density) *
                                   envelopeUpper * 1.75;
            float maximumOpticalDepth = maximumDensity *
                extinctionCoefficient * skipLength;
            bool skipExpensiveSample = skipCells > 1u &&
                maximumOpticalDepth < 0.000001;

            if (!skipExpensiveSample)
            {
                /* If this span can contribute visibly, take exactly one
                   stable lattice cell. That prevents adaptive traversal from
                   changing the cloud's resolved density or creating a new
                   view-dependent contour. */
                skipCells = 1u;
                skipLength = integrationCellLength;

                float density = DustDensityAt(
                    volume, rotation, seedOffset, wakeMask, samplePosition,
                    integrationCellLength) * volumeDistanceFade;
                if (density > 0.00001)
                {
                    float integrationLength = min(integrationCellLength,
                        max(1.0, endDistance - currentDistance +
                            integrationCellLength * 0.5));
                    float opticalDepth = density * extinctionCoefficient *
                                         integrationLength;
                    float stepTransmittance = exp(-opticalDepth);
                    float alpha = 1.0 - stepTransmittance;
                    if (!cachedLightingValid ||
                        currentDistance >= nextLightingDistance)
                    {
                        cachedLighting = DustLightingAt(
                            volume, rotation, seedOffset, localLightMask,
                            samplePosition, rayDirection);
                        cachedLightingValid = true;
                        nextLightingDistance = currentDistance +
                                               lightingInterval;
                    }
                    float3 lighting = cachedLighting;
                    float3 scatterTint = max(volume.color, 0.0);
                    float scatteringAlbedo = volume.scattering /
                        max(0.001, volume.scattering + volume.absorption);
                    float3 inScattering = lighting * scatterTint *
                        scatteringAlbedo * alpha;
                    accumulated += transmittance * inScattering;
                    transmittance *= stepTransmittance;
                    if (transmittance < DUST_EARLY_OUT_TRANSMITTANCE) break;
                }
            }

            currentDistance += integrationCellLength * (float)skipCells;
            stepIndex += skipCells - 1u;
        }
    }
    /* The presenter is display-referred. A gentle shoulder keeps bright local
       lights photographic without turning dense dust into a flat white blob. */
    accumulated = DustToneVolumetricContribution(accumulated, transmittance);
    outputTransmittance = transmittance;
    return max(scene * transmittance + accumulated, 0.0);
}

float4 DustScreenPS(VertexOutput input) : SV_Target
{
    float transmittance;
    float3 scattering = ApplyVolumetricDust(
        input.texcoord, 0.0, true, transmittance);
    /* A volume has no single surface depth. Reprojecting it with opaque-scene
       motion vectors produced copied cloud fragments, so temporal reuse stays
       disabled. */
    return float4(scattering, saturate(transmittance));
}

float4 FilteredDustScreen(float2 uv)
{
    uint width, height;
    DustScreenField.GetDimensions(width, height);
    float2 texel = 1.0 / float2(max(1u, width), max(1u, height));
    float4 center = DustScreenField.SampleLevel(SourceSampler, uv, 0.0);
    float4 total = center * 2.0;
    float totalWeight = 2.0;
    const float2 offsets[4] = {
        float2(-1.0, 0.0), float2(1.0, 0.0),
        float2(0.0, -1.0), float2(0.0, 1.0)};
    [unroll]
    for (uint index = 0u; index < 4u; ++index)
    {
        float4 sampleValue = DustScreenField.SampleLevel(
            SourceSampler, saturate(uv + offsets[index] * texel), 0.0);
        float weight = exp(-abs(sampleValue.a - center.a) * 36.0);
        total += sampleValue * weight;
        totalWeight += weight;
    }
    return total / max(totalWeight, 0.0001);
}

float4 PresentPS(VertexOutput input) : SV_Target
{
    /* The Streamline prepass is HUD-less. The display pass samples SourceFrame
       so classic UI remains native-resolution and outside temporal history. */
    float4 source = ScenePrepass != 0
        ? WorldFrame.Sample(SourceSampler, input.texcoord)
        : SourceFrame.Sample(SourceSampler, input.texcoord);
    if (SceneCompositeEnabled != 0)
    {
        float4 world = WorldFrame.Sample(SourceSampler, input.texcoord);
        float4 difference = abs(source - world);
        float largestDifference = max(max(difference.r, difference.g),
                                      max(difference.b, difference.a));
        /* The prepass deliberately renders every scene pixel. The final pass
           preserves pixels changed after the world snapshot as native UI. */
        if (ScenePrepass != 0 || largestDifference <= (1.5 / 255.0))
        {
            /* Streamline resources use native D3D top-down texture
               orientation. The presenter's TEXCOORD convention is inverted
               for legacy/native-raster snapshots, so sample the reconstructed
               Streamline output through the matching top-down UV. */
            float2 streamlineUv = float2(input.texcoord.x,
                                         1.0 - input.texcoord.y);
            float3 scene = StreamlineResolvedScene != 0 && ScenePrepass == 0
                ? FilteredWorld.Sample(SourceSampler, streamlineUv).rgb
                : SceneSample(input.texcoord);
            if (StreamlineResolvedScene == 0 && AntiAliasingMode == 2)
            {
                scene = FxaaScene(input.texcoord);
            }
            if (StreamlineResolvedScene == 0 || ScenePrepass != 0)
            {
            if (MotionBlur > 0.0001 && RayOverlayEnabled != 0)
            {
                uint motionWidth;
                uint motionHeight;
                MotionVectors.GetDimensions(motionWidth, motionHeight);
                /* Motion guidance is native D3D top-down. Convert it to the
                   presenter's legacy bottom-up UV convention only for this
                   raster post effect; Streamline consumes the stored field
                   directly. */
                float2 motionUv = float2(input.texcoord.x,
                                         1.0 - input.texcoord.y);
                float2 motion = MotionVectors.Sample(
                    SourceSampler, motionUv).xy /
                    float2(max(1u, motionWidth), max(1u, motionHeight));
                motion.y = -motion.y;
                motion *= MotionBlur;
                float motionLimit = 32.0 / max(1.0, (float)motionWidth);
                motion = clamp(motion, -motionLimit, motionLimit);
                float3 blurred = 0.0;
                [unroll]
                for (uint sampleIndex = 0; sampleIndex < 7; ++sampleIndex)
                {
                    float along = ((float)sampleIndex / 6.0) - 0.5;
                    blurred += SceneSample(input.texcoord + motion * along);
                }
                scene = lerp(scene, blurred / 7.0, MotionBlur);
            }

            float rayCoverage = 0.0;
            if (RayOverlayEnabled != 0)
            {
                float4 ray = FilteredRayLighting(input.texcoord);
                if (ray.a >= 0.0)
                {
                    rayCoverage = 1.0;
                    /* Preserve texture and team colors while replacing most
                       fixed-function light with visibility-tested DXR. */
                    /* RayLighting is already tone mapped.  Keep its hybrid
                       modulation bounded so indirect variance cannot become a
                       large exposure jump in the native raster image. */
                    float3 unshadowedRaster = scene;
                    float3 rayFactor = 0.06 + 1.58 * ray.rgb;
                    float3 shadowedScene =
                        scene * rayFactor + 0.015 * ray.rgb;

                    /* Foreground additive/emissive FX are raster effects, not
                       shadow receivers.  When a trail/beam is drawn over an
                       opaque ship, the DXR primary ray still hits that ship;
                       blindly applying its shadow factor to the completed
                       raster pixel makes the luminous FX darken with the hull.
                       Compare the raster result against the ray-space primary
                       albedo and protect only energy that sits well above a
                       generously lit opaque-surface ceiling.  Ordinary hull
                       texture/light variation remains DXR-shadowed. */
                    float2 guideUv = float2(input.texcoord.x,
                                             1.0 - input.texcoord.y);
                    float3 primaryAlbedo = PrimaryAlbedoGuide.Sample(
                        SourceSampler, guideUv).rgb;
                    float3 opaqueCeiling = primaryAlbedo * 1.65 + 0.08;
                    float3 fxExcess = max(unshadowedRaster - opaqueCeiling, 0.0);
                    float scenePeak = max(unshadowedRaster.r,
                        max(unshadowedRaster.g, unshadowedRaster.b));
                    float sceneFloor = min(unshadowedRaster.r,
                        min(unshadowedRaster.g, unshadowedRaster.b));
                    float fxPeak = max(fxExcess.r,
                        max(fxExcess.g, fxExcess.b));
                    float fxLuma = Luminance(fxExcess);
                    float sceneLuma = Luminance(unshadowedRaster);
                    float chroma = scenePeak - sceneFloor;
                    float coloredFx =
                        smoothstep(0.08, 0.28, fxPeak) *
                        smoothstep(0.10, 0.30, chroma) *
                        smoothstep(0.52, 0.82, scenePeak);
                    float whiteHotFx =
                        smoothstep(0.12, 0.34, fxLuma) *
                        smoothstep(0.78, 0.96, sceneLuma);
                    float emissiveFxProtection =
                        saturate(max(coloredFx, whiteHotFx));
                    scene = lerp(shadowedScene, unshadowedRaster,
                                 emissiveFxProtection);
                }
            }

            if (GodRays > 0.0001)
            {
                /* The old 32-sample hard march produced visible copies of an
                   occluder along the radial direction.  Keep this pass
                   deterministic, but interleave 48 samples across pixels and
                   attenuate through blocker thickness instead of turning one
                   blocker sample into an almost-binary shadow. */
                const uint shaftSamples = 48;
                float2 rayStep = (GodRayCenter - input.texcoord) /
                                 (float)shaftSamples;
                /* A screen-space dither phase reduced static banding but made
                   moving occluders crawl across the phase pattern.  Midpoint
                   sampling is temporally stable; the denser 48-step march and
                   soft extinction already hide the old stamped silhouettes. */
                const float samplePhase = 0.5;
                float2 sampleUv = input.texcoord + rayStep * samplePhase;
                float3 shafts = 0.0;
                float decay = 1.0;
                float totalWeight = 0.0;
                float transmission = 1.0;
                uint backgroundWidth;
                uint backgroundHeight;
                BackgroundFrame.GetDimensions(backgroundWidth, backgroundHeight);
                float2 backgroundTexel = 1.0 /
                    float2(max(1u, backgroundWidth), max(1u, backgroundHeight));
                float3 sourceColor = GodRaySourceColor;
                float sourceEnergy = 0.0;
                if (GodRaySourceExternal == 0)
                {
                    sourceColor = 0.0;
                    float sourcePeak = 0.0;
                    [unroll]
                    for (int sourceY = -2; sourceY <= 2; ++sourceY)
                    {
                        [unroll]
                        for (int sourceX = -2; sourceX <= 2; ++sourceX)
                        {
                            float3 sourceSample = BackgroundDisplaySample(
                                GodRayCenter + float2(sourceX, sourceY) *
                                backgroundTexel * 3.0);
                            sourceColor += sourceSample;
                            sourcePeak = max(sourcePeak, Luminance(sourceSample));
                        }
                    }
                    sourceColor = min(sourceColor / 25.0, 1.0);
                    /* Do not let a few pixels of centroid error or halo animation
                       cross a hard on/off threshold.  The broad average carries
                       the shaft colour while the brightest source texel keeps a
                       real mission sun active continuously. */
                    float sourceSignal = max(
                        sourcePeak, Luminance(sourceColor) * 1.20);
                    sourceEnergy = smoothstep(0.48, 0.78, sourceSignal);
                }
                else
                {
                    /* The campaign backdrop has already selected exactly one
                       coherent source from the mission sky.  Keep that authored
                       color even while the projected source is just outside the
                       viewport instead of re-gating it from edge-clamped pixels. */
                    sourceColor = SkyToDisplay(sourceColor);
                    sourceEnergy = step(0.0001, max(sourceColor.r,
                        max(sourceColor.g, sourceColor.b)));
                }
                /* A projected sun near/outside the viewport used to clamp every
                   out-of-range sample to the same edge texel.  That turns a
                   radial blur into one enormous triangular wedge.  The
                   mission-owned world source remains locked, but the screen
                   shaft contribution fades at the viewport edge and never
                   edge-clamps its integration samples. */
                float sourceEdge = min(min(GodRayCenter.x, 1.0 - GodRayCenter.x),
                                       min(GodRayCenter.y, 1.0 - GodRayCenter.y));
                float sourceEdgeFade = smoothstep(0.015, 0.10, sourceEdge);
                [loop]
                for (uint shaftIndex = 0; shaftIndex < shaftSamples; ++shaftIndex)
                {
                    sampleUv += rayStep;
                    if (any(sampleUv < 0.0) || any(sampleUv > 1.0))
                    {
                        decay *= 0.963;
                        continue;
                    }
                    float3 background = BackgroundFrame.Sample(
                        SourceSampler, sampleUv).rgb;
                    /* Use the pre-FX occluder capture here, not the completed
                       world frame. Engine trails, beams and particles are
                       luminous/translucent effects and must never cast radial
                       god-ray shadows. Ships/derelicts are covered by the DXR
                       hit mask while planets/world geometry live here. */
                    float3 rasterWorld = OccluderFrame.Sample(
                        SourceSampler, sampleUv).rgb;
                    float3 rasterDelta = abs(rasterWorld - background);
                    float rasterBlocker = smoothstep(
                        0.025, 0.10,
                        max(rasterDelta.r,
                            max(rasterDelta.g, rasterDelta.b)));
                    float rayBlocker =
                        RayOverlayEnabled != 0 &&
                        RaySourceSample(float2(
                            sampleUv.x, 1.0 - sampleUv.y)).a >= 0.0 ?
                        1.0 : 0.0;
                    /* The old 18% attenuation visibly leaked through ships.
                       Combine the DXR hit mask with the complete raster scene
                       delta, which also contains planets and other objects
                       absent from the TLAS, and make blockers effectively
                       opaque to the shaft march. */
                    float blocker = saturate(max(rasterBlocker, rayBlocker));
                    /* Integrate extinction across the apparent thickness of
                       the blocker.  A single sample no longer stamps a 96.5%
                       dark copy of the silhouette; successive samples through
                       real geometry converge to an opaque, soft-edged shaft. */
                    transmission *= lerp(1.0, 0.46, blocker);
                    /* Normalize against the unoccluded kernel. If the
                       denominator also included transmission, a fully blocked
                       ray would divide away its own shadow and remain bright. */
                    float baseWeight = decay;
                    float weight = baseWeight * transmission;
                    /* Only the detected mission-background source seeds the
                       shaft. Sampling arbitrary bright points here repeated
                       stars/nav lights into dotted radial bands at 100%. */
                    shafts += sourceColor * sourceEnergy * weight;
                    totalWeight += baseWeight;
                    /* Match the previous 32-step falloff over the
                       longer 48-step integration distance. */
                    decay *= 0.963;
                }
                /* Keep shafts local to the actual projected source.  The old
                   1.10-screen-radius support could span essentially the whole
                   viewport and made edge cases dominate the frame. */
                float radialFalloff = 1.0 - smoothstep(
                    0.08, 0.72, length(input.texcoord - GodRayCenter));
                float3 currentBackground = BackgroundFrame.Sample(
                    SourceSampler, input.texcoord).rgb;
                float3 currentRasterDelta = abs(world.rgb - currentBackground);
                float rasterCoverage = smoothstep(
                    0.025, 0.10,
                    max(currentRasterDelta.r,
                        max(currentRasterDelta.g, currentRasterDelta.b)));
                /* Never composite atmospheric shafts on a visible hull or
                   planet surface. */
                float geometryAttenuation =
                    1.0 - max(rayCoverage, rasterCoverage);
                scene += shafts / max(totalWeight, 0.001) *
                    (0.115 * GodRays * radialFalloff * sourceEdgeFade *
                     geometryAttenuation);
            }

            if (DustRenderEnabled != 0u && DustVolumeCount > 0u)
            {
                float4 dust = FilteredDustScreen(input.texcoord);
                float transmission = saturate(dust.a);
                float3 medium = max(dust.rgb, 0.0);
                /* Apply extinction to scene-linear radiance. Multiplying the
                   display/gamma encoded BTG directly exaggerates tiny vertex
                   interpolation differences that are invisible in the clean
                   sky and makes its triangulation appear through a cloud. */
                float3 linearScene = DisplayToLinear(scene);
                scene = max(LinearToSrgb(max(linearScene * transmission, 0.0))
                            + medium, 0.0);
            }

            if (Bloom > 0.0001)
            {
                /* Scene-only bloom: this branch runs only for pixels that
                   still match the pre-UI world capture, so menus/HUD text are
                   never blurred or brightened. 100% is intentionally modest;
                   the F12 control can drive the multiplier to 400%. */
                scene += BloomScene(input.texcoord) * (0.34 * Bloom);
            }

            if (ChromaticAberration > 0.0001)
            {
                float2 radial = input.texcoord - 0.5;
                float2 offset = radial * (0.006 * ChromaticAberration);
                float3 separated;
                separated.r = SceneSample(input.texcoord + offset).r;
                separated.g = scene.g;
                separated.b = SceneSample(input.texcoord - offset).b;
                scene = lerp(scene, separated, ChromaticAberration);
            }

            }

            /* Grain/dither are final-display operations and must not enter
               Streamline temporal history. */
            if (ScenePrepass == 0 && FilmGrain > 0.0001)
            {
                float grain = Noise(input.position.xy) - 0.5;
                float luminance = saturate(Luminance(scene));
                float blackProtection = smoothstep(0.015, 0.12, luminance);
                float highlightProtection =
                    1.0 - smoothstep(0.68, 0.98, luminance);
                float midtoneResponse =
                    0.35 + 0.65 * (1.0 - abs(luminance * 2.0 - 1.0));
                float response = blackProtection * highlightProtection *
                                 midtoneResponse;
                scene += grain * (0.075 * FilmGrain * response);
            }
            source.rgb = max(scene, 0.0);
        }
    }
    /* scRGB is linear. The classic/presenter composition is display-referred,
       so linearize exactly once at the final visible boundary. */
    if (ScenePrepass == 0)
        source.rgb = DisplayToLinear(source.rgb);
    return source;
}
)";

struct GpuDustVolume
{
    unsigned int enabled;
    unsigned int shape;
    unsigned int shapeSeed;
    unsigned int flags;
    float position[3];
    float density;
    float size[3];
    float scattering;
    float color[3];
    float absorption;
    float anisotropy;
    float coverage;
    float noiseScale;
    float noiseDetail;
    float shapeVariation;
    float rotationDegrees[3];
    float rotationXSinCos[2];
    float rotationYSinCos[2];
    float rotationZSinCos[2];
    float precomputedPadding[2];
    float seedOffset[3];
    float seedPadding;
};
static_assert(sizeof(GpuDustVolume) == 144,
              "precomputed dust volume GPU layout mismatch");
static_assert(offsetof(GpuDustVolume, rotationXSinCos) == 96,
              "authored dust prefix must remain byte-identical");

struct GpuDustLight
{
    float position[3];
    unsigned int type;
    float direction[3];
    float radius;
    float color[3];
    float intensity;
    float coneCos;
    float edgeCos;
    float padding[2];
};
static_assert(sizeof(GpuDustLight) == 64, "dust light GPU layout mismatch");

struct GpuDustWake
{
    float position[3];
    float radius;
    float direction[3];
    float length;
    float strength;
    float bowStrength;
    float recovery;
    float padding;
};
static_assert(sizeof(GpuDustWake) == 48, "dust wake GPU layout mismatch");
static_assert(sizeof(HWModernVolumetricDustVolume) == 96,
              "dust volume GPU layout mismatch");

struct D3D12FrameContext
{
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12Resource> upload;
    ComPtr<ID3D12Resource> worldUpload;
    ComPtr<ID3D12Resource> backgroundUpload;
    ComPtr<ID3D12Resource> occluderUpload;
    ComPtr<ID3D12Resource> dustVolumeUpload;
    ComPtr<ID3D12Resource> dustLightUpload;
    ComPtr<ID3D12Resource> dustWakeUpload;
    /* RTX-AAA: each swap-chain flight slot owns a tiny timestamp readback.
       Results are consumed only after that slot's existing fence completes,
       so profiling never introduces a new GPU/CPU synchronization point. */
    ComPtr<ID3D12Resource> timestampReadback;
    bool timestampPending = false;
    GpuDustVolume *mappedDustVolumes = nullptr;
    GpuDustLight *mappedDustLights = nullptr;
    GpuDustWake *mappedDustWakes = nullptr;
    unsigned char *mappedUpload = nullptr;
    unsigned char *mappedWorldUpload = nullptr;
    unsigned char *mappedBackgroundUpload = nullptr;
    unsigned char *mappedOccluderUpload = nullptr;
    UINT64 uploadSize = 0;
    UINT64 fenceValue = 0;
};

struct PresenterConstants
{
    unsigned int rayOverlayEnabled;
    unsigned int antiAliasingMode;
    unsigned int sceneCompositeEnabled;
    unsigned int frameIndex;
    float chromaticAberration;
    float motionBlur;
    float filmGrain;
    float godRays;
    float godRayCenter[2];
    unsigned int scenePrepass;
    unsigned int streamlineResolvedScene;
    float outputDither;
    float godRaySourceColor[3];
    unsigned int godRaySourceExternal;
    float bloom;
    float presenterPadding[2];
    unsigned int dustVolumeCount;
    unsigned int dustLightCount;
    unsigned int dustRenderEnabled;
    unsigned int dustWakeCount;
    float dustCameraPositionMaxDistance[4];
    float dustRayUv00[4];
    float dustRayUv10[4];
    float dustRayUv01SunStrength[4];
    float dustSunDirectionValid[4];
    float dustSunColor[4];
    float dustAmbientColorStrength[4];
    float dustGridMinWidth[4];
    float dustGridSizeHeight[4];
};

static_assert(sizeof(PresenterConstants) == 240,
              "presenter constant layout mismatch");

struct D3D12Presenter
{
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Device5> raytracingDevice;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain4> swapchain;
    D3D12FrameContext frames[frameBufferCount];
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12QueryHeap> timestampQueryHeap;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12RootSignature> dustFroxelRootSignature;
    ComPtr<ID3D12PipelineState> presenterPipeline;
    ComPtr<ID3D12PipelineState> scenePrepassPipeline;
    ComPtr<ID3D12PipelineState> dustFroxelPipeline;
    ComPtr<ID3D12PipelineState> dustScreenPipeline;
    ComPtr<ID3D12Resource> backBuffers[frameBufferCount];
    ComPtr<ID3D12Resource> sourceTexture;
    ComPtr<ID3D12Resource> worldTexture;
    ComPtr<ID3D12Resource> backgroundTexture;
    ComPtr<ID3D12Resource> occluderTexture;
    ComPtr<ID3D12Resource> sceneInputTexture;
    ComPtr<ID3D12Resource> dustFroxelTexture;
    ComPtr<ID3D12Resource> dustScreenTextures[2];
    ComPtr<ID3D12Fence> fence;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT uploadFootprint = {};
    HANDLE fenceEvent = nullptr;
    HWND window = nullptr;
    UINT rtvDescriptorSize = 0;
    UINT srvDescriptorSize = 0;
    UINT width = 0;
    UINT height = 0;
    UINT sceneInputWidth = 0;
    UINT sceneInputHeight = 0;
    UINT dustFroxelWidth = 0;
    UINT dustFroxelHeight = 0;
    UINT dustFroxelDepth = 0;
    UINT dustScreenWidth = 0;
    UINT dustScreenHeight = 0;
    UINT dustScreenWriteIndex = 0;
    bool dustScreenShaderReadable[2] = {true, true};
    bool dustScreenHistoryValid = false;
    UINT64 nextFenceValue = 1;
    UINT64 submittedFenceValue = 0;
    UINT64 timestampFrequency = 0;
    UINT activeFrameIndex = 0;
    double gpuRasterMilliseconds = 0.0;
    double gpuDxrMilliseconds = 0.0;
    double gpuPresenterMilliseconds = 0.0;
    double gpuFrameMilliseconds = 0.0;
    unsigned long long gpuTimestampFrames = 0;
    std::chrono::steady_clock::time_point cpuFrameStarted = {};
    double cpuFrameMilliseconds = 0.0;
    unsigned long long cpuFrameSamples = 0;
    bool cpuFrameTimerActive = false;
    unsigned long long frameCount = 0;
    float dustCameraPosition[3] = {0.0f, 0.0f, 0.0f};
    float dustRayUv00[3] = {-1.0f, -1.0f, -1.0f};
    float dustRayUv10[3] = { 1.0f, -1.0f, -1.0f};
    float dustRayUv01[3] = {-1.0f,  1.0f, -1.0f};
    float dustProjectionP10 = 0.0f;
    float dustProjectionP14 = 0.0f;
    bool dustProjectionValid = false;
    bool nativeRasterDepthSampleReady = false;
    bool dustCameraValid = false;
    bool dustCameraExplicitThisFrame = false;
    unsigned int missionSkyTextureName = 0;
    float missionSkyFade = 1.0f;
    bool missionSkyEnvironmentLogged = false;
    /* Streamline camera history is now reset only for real discontinuities.
       Sky misses carry dense camera-rotation motion from the DXR shader, so
       ordinary orbit/pan movement must preserve temporal reconstruction. */
    float streamlineCameraPosition[3] = {0.0f, 0.0f, 0.0f};
    float streamlineRayUv00[3] = {0.0f, 0.0f, 0.0f};
    float streamlineRayUv10[3] = {0.0f, 0.0f, 0.0f};
    float streamlineRayUv01[3] = {0.0f, 0.0f, 0.0f};
    bool streamlineCameraHistoryValid = false;
    float godRayCenterX = 0.5f;
    float godRayCenterY = 0.42f;
    float godRayWorldDirection[3] = {0.0f, 0.0f, -1.0f};
    int godRayMapIdentity = -2147483647;
    unsigned long long godRayLastBackgroundFrame = 0;
    bool godRaySourceLocked = false;
    bool godRaySourceVisible = false;
    float godRaySourceColor[3] = {0.0f, 0.0f, 0.0f};
    bool godRaySourceExternalThisFrame = false;
    bool godRaySourceExternalVisible = false;
    int vsyncMode = 1;
    bool allowTearing = false;
    bool active = false;
    bool visiblePresenter = false;
    bool frameOpen = false;
    bool frameCaptured = false;
    bool worldFrameCaptured = false;
    bool backgroundFrameCaptured = false;
    bool occluderFrameCaptured = false;
    bool sourceTextureShaderReadable = false;
    bool worldTextureShaderReadable = false;
    bool backgroundTextureShaderReadable = false;
    bool occluderTextureShaderReadable = false;
    bool sceneInputTextureShaderReadable = false;
    bool dustFroxelShaderReadable = false;
    bool dustFroxelValidThisFrame = false;
    double backgroundReadbackMilliseconds = 0.0;
    double worldReadbackMilliseconds = 0.0;
    double frameReadbackMilliseconds = 0.0;
    double dxrRecordMilliseconds = 0.0;
    unsigned long long backgroundReadbackCount = 0;
    unsigned long long worldReadbackCount = 0;
    unsigned long long frameReadbackCount = 0;
    unsigned long long dxrRecordCount = 0;
};

D3D12Presenter presenter;
bool raytracingRequested = true;
int requestedAntiAliasingMode = HW_MODERN_AA_DLAA;
bool requestedRayReconstruction = true;
float requestedFxLightingStrength = 1.0f;
unsigned int requestedPathSamples = 1;
unsigned int requestedPathBounces = 3;
float requestedNormalStrength = 1.0f;
float requestedEnvironmentStrength = 1.0f;
float requestedAuthoredLightStrength = 1.0f;
float requestedPrimarySunStrength = 1.0f;
float requestedSkyAmbientStrength = 1.0f;
float requestedSurfaceReflectivity = 1.25f;
float requestedSurfaceRoughness = 0.38f;
float requestedLightingExposure = 1.0f;
float requestedShadowStrength = 1.0f;
float requestedSunShadowAngularRadiusDegrees = 0.35f;
unsigned int requestedSunShadowSamples = 6u;
unsigned int requestedLocalShadowSamples = 2u;
float requestedLocalShadowAngularRadiusDegrees = 0.20f;
float requestedShadowReceiverBias = 0.05f;
float requestedShadowNormalBias = 0.08f;
float requestedContactShadowStrength = 1.0f;
float requestedContactShadowDistance = 1200.0f;
float requestedShadowMaximumDistance = 500000.0f;
float requestedChromaticAberration = 0.0f;
float requestedMotionBlur = 0.0f;
float requestedFilmGrain = 0.0f;
float requestedGodRays = 0.0f;
float requestedBloom = 1.0f;
float requestedOutputDitherStrength = 1.0f;
std::vector<HWModernMapLightEmitter> mapLightEmitters;
std::vector<HWModernMissionLightEmitter> missionAuthoringLightEmitters;
bool missionAuthoringReplacesMapLightsForDust = false;
std::vector<HWModernDynamicLightEmitter> dynamicLightEmitters;
std::vector<HWModernVolumetricDustVolume> volumetricDustVolumes;
float volumetricDustFadeDistance = 65000.0f;
float volumetricDustFadeStrength = 1.0f;
std::vector<HWModernVolumetricDustVolume> volumetricDustScratch;
std::vector<GpuDustLight> volumetricDustLightScratch;
std::vector<HWModernVolumetricDustInteractor> volumetricDustInteractors;
std::vector<GpuDustWake> volumetricDustWakeScratch;

struct VolumetricDustWakeRecord
{
    unsigned long long identity = 0;
    float position[3] = {};
    float direction[3] = {0.0f, 0.0f, -1.0f};
    float radius = 1.0f;
    float length = 1.0f;
    float strength = 1.0f;
    std::chrono::steady_clock::time_point created;
};

struct VolumetricDustInteractorState
{
    float lastPosition[3] = {};
    float lastEmitPosition[3] = {};
    std::chrono::steady_clock::time_point lastSeen;
    std::chrono::steady_clock::time_point lastEmit;
    bool initialized = false;
};

std::vector<VolumetricDustWakeRecord> volumetricDustWakeHistory;
std::unordered_map<unsigned long long, VolumetricDustInteractorState>
    volumetricDustInteractorStates;
bool volumetricDustSimulationPaused = false;
std::chrono::steady_clock::time_point volumetricDustPauseStarted =
    std::chrono::steady_clock::now();
bool dustMissionSunValid = false;
bool dustMissionSkyAvailable = false;
float dustMissionSunDirection[3] = {0.0f, 0.0f, -1.0f};
float dustMissionSunColor[3] = {1.0f, 1.0f, 1.0f};
float dustMissionAmbientColor[3] = {0.08f, 0.08f, 0.10f};

using DynamicLightClock = std::chrono::steady_clock;
struct TimedDynamicLight
{
    HWModernDynamicLightEmitter emitter = {};
    DynamicLightClock::time_point started;
    float lifetimeSeconds = 0.0f;
};
std::vector<TimedDynamicLight> timedDynamicLightEmitters;

struct EmissiveMaterialRecord
{
    std::string meshName;
    std::string materialName;
    std::string textureName;
    unsigned int fullAmbientColors = 0;
    float color[3] = {};
    float intensity = 0.0f;
};

std::vector<EmissiveMaterialRecord> emissiveMaterials;

void logFailure(const char *operation, HRESULT result)
{
    std::fprintf(stderr, "[ModernGraphics] %s failed (HRESULT 0x%08lx).\n",
                 operation, static_cast<unsigned long>(result));
}

void refreshNativeRasterDepthSrv(ID3D12Resource *depthResource)
{
    if (!presenter.device || !presenter.srvHeap) return;
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(sceneDepthDescriptorIndex) *
                  presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(depthResource, &depthSrv, handle);
}

bool createVolumetricDustFrameResources()
{
    if (!presenter.device || !presenter.srvHeap) return false;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    for (UINT slot = 0; slot < frameBufferCount; ++slot)
    {
        D3D12FrameContext &frame = presenter.frames[slot];
        buffer.Width = sizeof(GpuDustVolume) * maximumVolumetricDustVolumes;
        HRESULT result = presenter.device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&frame.dustVolumeUpload));
        if (FAILED(result))
        {
            logFailure("CreateCommittedResource(volumetric dust buffer)", result);
            return false;
        }
        frame.dustVolumeUpload->SetName(L"Homeworld volumetric dust frame data");
        void *mappedVolumes = nullptr;
        if (FAILED(result = frame.dustVolumeUpload->Map(
            0, nullptr, &mappedVolumes)))
        {
            logFailure("Map(volumetric dust buffer)", result);
            return false;
        }
        frame.mappedDustVolumes = static_cast<GpuDustVolume *>(mappedVolumes);
        std::memset(frame.mappedDustVolumes, 0,
                    static_cast<size_t>(buffer.Width));

        buffer.Width = sizeof(GpuDustLight) * maximumVolumetricDustLights;
        if (FAILED(result = presenter.device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&frame.dustLightUpload))))
        {
            logFailure("CreateCommittedResource(volumetric dust light buffer)", result);
            return false;
        }
        frame.dustLightUpload->SetName(L"Homeworld volumetric dust local lights");
        void *mappedLights = nullptr;
        if (FAILED(result = frame.dustLightUpload->Map(0, nullptr, &mappedLights)))
        {
            logFailure("Map(volumetric dust light buffer)", result);
            return false;
        }
        frame.mappedDustLights = static_cast<GpuDustLight *>(mappedLights);
        std::memset(frame.mappedDustLights, 0, static_cast<size_t>(buffer.Width));

        D3D12_SHADER_RESOURCE_VIEW_DESC volumeSrv = {};
        volumeSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        volumeSrv.Format = DXGI_FORMAT_UNKNOWN;
        volumeSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        volumeSrv.Buffer.FirstElement = 0;
        volumeSrv.Buffer.NumElements = maximumVolumetricDustVolumes;
        volumeSrv.Buffer.StructureByteStride = sizeof(GpuDustVolume);
        D3D12_CPU_DESCRIPTOR_HANDLE volumeHandle =
            presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
        volumeHandle.ptr += static_cast<SIZE_T>(
            dustDescriptorBase + slot * dustDescriptorsPerFrame) *
            presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(
            frame.dustVolumeUpload.Get(), &volumeSrv, volumeHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC lightSrv = {};
        lightSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        lightSrv.Format = DXGI_FORMAT_UNKNOWN;
        lightSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        lightSrv.Buffer.FirstElement = 0;
        lightSrv.Buffer.NumElements = maximumVolumetricDustLights;
        lightSrv.Buffer.StructureByteStride = sizeof(GpuDustLight);
        D3D12_CPU_DESCRIPTOR_HANDLE lightHandle = volumeHandle;
        lightHandle.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(
            frame.dustLightUpload.Get(), &lightSrv, lightHandle);

        buffer.Width = sizeof(GpuDustWake) * maximumVolumetricDustWakes;
        if (FAILED(result = presenter.device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&frame.dustWakeUpload))))
        {
            logFailure("CreateCommittedResource(volumetric dust wake buffer)", result);
            return false;
        }
        frame.dustWakeUpload->SetName(L"Homeworld volumetric dust ship wakes");
        void *mappedWakes = nullptr;
        if (FAILED(result = frame.dustWakeUpload->Map(0, nullptr, &mappedWakes)))
        {
            logFailure("Map(volumetric dust wake buffer)", result);
            return false;
        }
        frame.mappedDustWakes = static_cast<GpuDustWake *>(mappedWakes);
        std::memset(frame.mappedDustWakes, 0, static_cast<size_t>(buffer.Width));

        D3D12_SHADER_RESOURCE_VIEW_DESC wakeSrv = {};
        wakeSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        wakeSrv.Format = DXGI_FORMAT_UNKNOWN;
        wakeSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        wakeSrv.Buffer.FirstElement = 0;
        wakeSrv.Buffer.NumElements = maximumVolumetricDustWakes;
        wakeSrv.Buffer.StructureByteStride = sizeof(GpuDustWake);
        D3D12_CPU_DESCRIPTOR_HANDLE wakeHandle = lightHandle;
        wakeHandle.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(
            frame.dustWakeUpload.Get(), &wakeSrv, wakeHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC froxelSrv = {};
        froxelSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        froxelSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        froxelSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        froxelSrv.Texture3D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE froxelHandle = wakeHandle;
        froxelHandle.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(nullptr, &froxelSrv, froxelHandle);

        /* t13 is a ray-space primary albedo guide used only to protect
           foreground emissive FX (engine trails/glows, beams) from being
           multiplied by opaque-surface DXR shadow lighting. Keep a valid null
           descriptor until the first ray dispatch supplies the real guide. */
        D3D12_SHADER_RESOURCE_VIEW_DESC albedoSrv = {};
        albedoSrv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        albedoSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        albedoSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        albedoSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE albedoHandle = froxelHandle;
        albedoHandle.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(nullptr, &albedoSrv,
                                                    albedoHandle);
        D3D12_CPU_DESCRIPTOR_HANDLE farFroxelHandle = albedoHandle;
        farFroxelHandle.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(nullptr, &froxelSrv,
                                                    farFroxelHandle);
        D3D12_SHADER_RESOURCE_VIEW_DESC screenDustSrv = {};
        screenDustSrv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        screenDustSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        screenDustSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        screenDustSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE screenDustHandle = farFroxelHandle;
        screenDustHandle.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(
            nullptr, &screenDustSrv, screenDustHandle);
        screenDustHandle.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(
            nullptr, &screenDustSrv, screenDustHandle);
    }
    return true;
}


float volumetricDustDegreesToCosine(float degrees)
{
    const float pi = 3.14159265358979323846f;
    return std::cos(std::max(0.0f, std::min(degrees, 179.0f)) * pi / 180.0f);
}

unsigned int volumetricDustHash(unsigned int value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

void volumetricDustPrecomputeVolume(
    const HWModernVolumetricDustVolume &source, GpuDustVolume &gpu)
{
    std::memset(&gpu, 0, sizeof(gpu));
    /* The authored layout is deliberately the first 96 bytes of the GPU
       record. Extra data below is runtime-only and never changes map/editor
       serialization. */
    std::memcpy(&gpu, &source, sizeof(HWModernVolumetricDustVolume));
    constexpr float pi = 3.14159265358979323846f;
    const float radians = -pi / 180.0f;
    const float angleX = source.rotationDegrees[0] * radians;
    const float angleY = source.rotationDegrees[1] * radians;
    const float angleZ = source.rotationDegrees[2] * radians;
    gpu.rotationXSinCos[0] = std::sin(angleX);
    gpu.rotationXSinCos[1] = std::cos(angleX);
    gpu.rotationYSinCos[0] = std::sin(angleY);
    gpu.rotationYSinCos[1] = std::cos(angleY);
    gpu.rotationZSinCos[0] = std::sin(angleZ);
    gpu.rotationZSinCos[1] = std::cos(angleZ);
    gpu.precomputedPadding[0] = volumetricDustFadeDistance;
    gpu.precomputedPadding[1] = volumetricDustFadeStrength;
    gpu.seedOffset[0] = static_cast<float>(
        volumetricDustHash(source.shapeSeed ^ 0x9e3779b9u) & 1023u) / 37.0f;
    gpu.seedOffset[1] = static_cast<float>(
        volumetricDustHash(source.shapeSeed ^ 0xc8013ea4u) & 1023u) / 37.0f;
    gpu.seedOffset[2] = static_cast<float>(
        volumetricDustHash(source.shapeSeed ^ 0xad90777du) & 1023u) / 37.0f;
}

float volumetricDustLength3(const float value[3])
{
    return std::sqrt(value[0]*value[0] + value[1]*value[1] + value[2]*value[2]);
}

bool volumetricDustInteractorNearVolume(
    const HWModernVolumetricDustInteractor &interactor)
{
    for (const HWModernVolumetricDustVolume &volume : volumetricDustVolumes)
    {
        if (volume.enabled == 0 || volume.density <= 0.00001f) continue;
        float half[3] = {
            std::max(1.0f, volume.size[0] * 0.5f),
            std::max(1.0f, volume.size[1] * 0.5f),
            std::max(1.0f, volume.size[2] * 0.5f)};
        if (volume.shape == HW_MODERN_DUST_SPHERE)
        {
            const float radius = std::max(1.0f, (half[0] + half[1] + half[2]) / 3.0f);
            half[0] = half[1] = half[2] = radius;
        }
        const float margin = std::max(50.0f, interactor.radius * 2.5f);
        if (std::fabs(interactor.position[0] - volume.position[0]) <= half[0] + margin &&
            std::fabs(interactor.position[1] - volume.position[1]) <= half[1] + margin &&
            std::fabs(interactor.position[2] - volume.position[2]) <= half[2] + margin)
            return true;
    }
    return false;
}

void updateVolumetricDustShipWakes()
{
    /* Editor pause must freeze the medium.  RTX-0087 used wall-clock age for
       wake recovery, so even a paused authoring scene could visibly change
       while the user only rotated the camera. */
    const DynamicLightClock::time_point now = volumetricDustSimulationPaused
        ? volumetricDustPauseStarted : DynamicLightClock::now();
    constexpr float wakeLifetimeSeconds = 6.0f;

    size_t writeIndex = 0;
    for (size_t index = 0; index < volumetricDustWakeHistory.size(); ++index)
    {
        const float age = std::chrono::duration<float>(
            now - volumetricDustWakeHistory[index].created).count();
        if (age < wakeLifetimeSeconds)
            volumetricDustWakeHistory[writeIndex++] = volumetricDustWakeHistory[index];
    }
    volumetricDustWakeHistory.resize(writeIndex);

    for (const HWModernVolumetricDustInteractor &interactor : volumetricDustInteractors)
    {
        if (volumetricDustSimulationPaused) break;
        if (interactor.identity == 0 || interactor.radius <= 0.5f ||
            !volumetricDustInteractorNearVolume(interactor))
            continue;

        VolumetricDustInteractorState &state =
            volumetricDustInteractorStates[interactor.identity];
        if (!state.initialized)
        {
            std::memcpy(state.lastPosition, interactor.position, sizeof(state.lastPosition));
            std::memcpy(state.lastEmitPosition, interactor.position, sizeof(state.lastEmitPosition));
            state.lastSeen = state.lastEmit = now;
            state.initialized = true;
            continue;
        }

        float delta[3] = {
            interactor.position[0] - state.lastPosition[0],
            interactor.position[1] - state.lastPosition[1],
            interactor.position[2] - state.lastPosition[2]};
        const float frameDistance = volumetricDustLength3(delta);
        const float velocityLength = volumetricDustLength3(interactor.velocity);
        const float secondsSinceSeen = std::max(0.0001f,
            std::chrono::duration<float>(now - state.lastSeen).count());
        float direction[3] = {interactor.velocity[0], interactor.velocity[1], interactor.velocity[2]};
        float motionSpeed = velocityLength;
        if (frameDistance > 0.05f)
        {
            direction[0] = delta[0]; direction[1] = delta[1]; direction[2] = delta[2];
            motionSpeed = std::max(motionSpeed, frameDistance / secondsSinceSeen);
        }
        const float directionLength = volumetricDustLength3(direction);

        std::memcpy(state.lastPosition, interactor.position, sizeof(state.lastPosition));
        state.lastSeen = now;
        if (directionLength <= 0.05f || motionSpeed <= 0.5f)
            continue;

        /* Teleports and mission script relocations should not cut a kilometre-
           long tunnel through the medium in one frame. */
        if (frameDistance > std::max(2000.0f, interactor.radius * 12.0f))
        {
            std::memcpy(state.lastEmitPosition, interactor.position,
                        sizeof(state.lastEmitPosition));
            state.lastEmit = now;
            continue;
        }

        float sinceEmit[3] = {
            interactor.position[0] - state.lastEmitPosition[0],
            interactor.position[1] - state.lastEmitPosition[1],
            interactor.position[2] - state.lastEmitPosition[2]};
        const float emitDistance = volumetricDustLength3(sinceEmit);
        const float emitSpacing = std::max(22.0f, interactor.radius * 0.32f);
        const float emitSeconds = std::chrono::duration<float>(now - state.lastEmit).count();
        if (emitDistance < emitSpacing && emitSeconds < 0.12f)
            continue;

        VolumetricDustWakeRecord wake;
        wake.identity = interactor.identity;
        std::memcpy(wake.position, interactor.position, sizeof(wake.position));
        wake.direction[0] = direction[0] / directionLength;
        wake.direction[1] = direction[1] / directionLength;
        wake.direction[2] = direction[2] / directionLength;
        wake.radius = std::max(28.0f, interactor.radius * 0.72f);
        wake.length = std::max(wake.radius * 2.0f,
            std::min(wake.radius * 7.0f, motionSpeed * 0.70f + wake.radius * 1.5f));
        const float relativeSpeed = motionSpeed / std::max(80.0f, interactor.radius * 2.0f);
        wake.strength = std::max(0.22f, std::min(1.35f,
            interactor.strength * (0.50f + relativeSpeed * 0.28f)));
        wake.created = now;
        volumetricDustWakeHistory.push_back(wake);
        if (volumetricDustWakeHistory.size() > 128)
            volumetricDustWakeHistory.erase(volumetricDustWakeHistory.begin(),
                                            volumetricDustWakeHistory.begin() +
                                            (volumetricDustWakeHistory.size() - 128));
        std::memcpy(state.lastEmitPosition, interactor.position,
                    sizeof(state.lastEmitPosition));
        state.lastEmit = now;
    }

    for (auto state = volumetricDustInteractorStates.begin();
         state != volumetricDustInteractorStates.end(); )
    {
        const float unseen = std::chrono::duration<float>(now - state->second.lastSeen).count();
        if (unseen > 3.0f) state = volumetricDustInteractorStates.erase(state);
        else ++state;
    }

    volumetricDustWakeScratch.clear();
    volumetricDustWakeScratch.reserve(maximumVolumetricDustWakes);
    /* Newest records are most important near the ship; older records still
       remain in history and become visible again as newer ones recover. */
    for (auto record = volumetricDustWakeHistory.rbegin();
         record != volumetricDustWakeHistory.rend() &&
         volumetricDustWakeScratch.size() < maximumVolumetricDustWakes; ++record)
    {
        const float age = std::chrono::duration<float>(now - record->created).count();
        const float remaining = std::max(0.0f, 1.0f - age / wakeLifetimeSeconds);
        if (remaining <= 0.0f) continue;
        GpuDustWake gpu = {};
        std::memcpy(gpu.position, record->position, sizeof(gpu.position));
        std::memcpy(gpu.direction, record->direction, sizeof(gpu.direction));
        gpu.radius = record->radius;
        gpu.length = record->length;
        gpu.strength = record->strength * remaining * remaining;
        gpu.bowStrength = record->strength * remaining * remaining * remaining * remaining;
        gpu.recovery = remaining;
        volumetricDustWakeScratch.push_back(gpu);
    }
}

bool streamlineCameraChangedThisFrame()
{
    if (!presenter.dustCameraValid || !presenter.dustCameraExplicitThisFrame)
    {
        presenter.streamlineCameraHistoryValid = false;
        return true;
    }

    const bool firstFrame = !presenter.streamlineCameraHistoryValid;
    bool cameraCut = firstFrame;
    if (!firstFrame)
    {
        const float dx = presenter.dustCameraPosition[0] -
                         presenter.streamlineCameraPosition[0];
        const float dy = presenter.dustCameraPosition[1] -
                         presenter.streamlineCameraPosition[1];
        const float dz = presenter.dustCameraPosition[2] -
                         presenter.streamlineCameraPosition[2];
        const float translation = std::sqrt(dx * dx + dy * dy + dz * dz);

        auto centerDirection = [](const float uv00[3], const float uv10[3],
                                  const float uv01[3], float out[3])
        {
            for (unsigned int channel = 0; channel < 3; ++channel)
                out[channel] = uv00[channel] +
                    0.5f * (uv10[channel] - uv00[channel]) +
                    0.5f * (uv01[channel] - uv00[channel]);
            const float length = std::sqrt(out[0] * out[0] +
                                           out[1] * out[1] +
                                           out[2] * out[2]);
            if (length > 0.000001f)
                for (unsigned int channel = 0; channel < 3; ++channel)
                    out[channel] /= length;
        };
        float currentForward[3] = {};
        float previousForward[3] = {};
        centerDirection(presenter.dustRayUv00, presenter.dustRayUv10,
                        presenter.dustRayUv01, currentForward);
        centerDirection(presenter.streamlineRayUv00,
                        presenter.streamlineRayUv10,
                        presenter.streamlineRayUv01, previousForward);
        const float directionDot =
            currentForward[0] * previousForward[0] +
            currentForward[1] * previousForward[1] +
            currentForward[2] * previousForward[2];
        /* Preserve history during normal Homeworld camera orbit/pan. Only a
           teleport-scale translation or a >60 degree single-frame direction
           jump is treated as a true temporal reset/camera cut. */
        cameraCut = translation > 25000.0f || directionDot < 0.5f;
    }

    const float *currentVectors[4] = {
        presenter.dustCameraPosition, presenter.dustRayUv00,
        presenter.dustRayUv10, presenter.dustRayUv01};
    float *historyVectors[4] = {
        presenter.streamlineCameraPosition, presenter.streamlineRayUv00,
        presenter.streamlineRayUv10, presenter.streamlineRayUv01};
    for (unsigned int vectorIndex = 0; vectorIndex < 4; ++vectorIndex)
        for (unsigned int channel = 0; channel < 3; ++channel)
            historyVectors[vectorIndex][channel] =
                currentVectors[vectorIndex][channel];
    presenter.streamlineCameraHistoryValid = true;
    return cameraCut;
}

bool dustWorldCacheRequested()
{
    /* PERFORMANCE DEFAULT: after the hard world-lock fix, the shared field is
       once again safe to use as the shipping renderer. Its storage axes are
       WORLD X/Y/Z and the camera rays now come from the actual render view
       matrix, so the old camera-facing/billboard failure is not part of this
       path. Direct procedural marching remains available for A/B/debugging. */
    static int cachedMode = -1;
    if (cachedMode >= 0) return cachedMode != 0;

    char directOverride[8] = {};
    const DWORD directLength = GetEnvironmentVariableA(
        "HW_DUST_DIRECT_RAYMARCH", directOverride,
        static_cast<DWORD>(sizeof(directOverride)));
    if (directLength > 0 && directOverride[0] != '0')
    {
        cachedMode = 0;
        return false;
    }

    char cacheOverride[8] = {};
    const DWORD cacheLength = GetEnvironmentVariableA(
        "HW_DUST_WORLD_CACHE", cacheOverride,
        static_cast<DWORD>(sizeof(cacheOverride)));
    if (cacheLength > 0)
    {
        cachedMode = cacheOverride[0] != '0' ? 1 : 0;
        return cachedMode != 0;
    }

    cachedMode = 1;
    return true;
}

void prepareVolumetricDustFrame(PresenterConstants &constants)
{
    constants.dustVolumeCount = 0;
    constants.dustLightCount = 0;
    constants.dustRenderEnabled = 0;
    constants.dustWakeCount = 0;
    constants.presenterPadding[0] = 0.0f;
    std::memset(constants.dustGridMinWidth, 0, sizeof(constants.dustGridMinWidth));
    std::memset(constants.dustGridSizeHeight, 0, sizeof(constants.dustGridSizeHeight));
    if (!presenter.dustCameraValid || volumetricDustVolumes.empty())
    {
        return;
    }

    D3D12FrameContext &frame = presenter.frames[presenter.activeFrameIndex];
    if (frame.mappedDustVolumes == nullptr || frame.mappedDustLights == nullptr)
    {
        return;
    }

    const float cameraX = presenter.dustCameraPosition[0];
    const float cameraY = presenter.dustCameraPosition[1];
    const float cameraZ = presenter.dustCameraPosition[2];

    constants.dustCameraPositionMaxDistance[0] = cameraX;
    constants.dustCameraPositionMaxDistance[1] = cameraY;
    constants.dustCameraPositionMaxDistance[2] = cameraZ;
    /* Filled below from the union of active clouds. This is only the far end
       of the shared camera froxel grid; it is never derived from a ray/shape
       intersection and is placed many radii beyond numerically visible tails. */
    constants.dustCameraPositionMaxDistance[3] = 3.0e30f;
    for (unsigned int channel = 0; channel < 3; ++channel)
    {
        constants.dustRayUv00[channel] = presenter.dustRayUv00[channel];
        constants.dustRayUv10[channel] = presenter.dustRayUv10[channel];
        constants.dustRayUv01SunStrength[channel] = presenter.dustRayUv01[channel];
    }
    /* Reuse unused vector .w channels for exact raster-depth reconstruction
       without growing the 60-DWORD presenter root-constant block. */
    constants.dustRayUv00[3] = presenter.dustProjectionP10;
    constants.dustRayUv10[3] = presenter.dustProjectionP14;
    constants.dustRayUv01SunStrength[3] =
        dustMissionSunValid ? 0.82f * requestedPrimarySunStrength : 0.0f;
    constants.dustSunDirectionValid[0] = dustMissionSunDirection[0];
    constants.dustSunDirectionValid[1] = dustMissionSunDirection[1];
    constants.dustSunDirectionValid[2] = dustMissionSunDirection[2];
    constants.dustSunDirectionValid[3] = dustMissionSunValid ? 1.0f : 0.0f;
    constants.dustSunColor[0] = dustMissionSunColor[0];
    constants.dustSunColor[1] = dustMissionSunColor[1];
    constants.dustSunColor[2] = dustMissionSunColor[2];
    constants.dustSunColor[3] =
        presenter.dustProjectionValid && presenter.nativeRasterDepthSampleReady ? 1.0f : 0.0f;

    float ambient[3] = {
        dustMissionSkyAvailable ? dustMissionAmbientColor[0] * requestedSkyAmbientStrength : 0.025f,
        dustMissionSkyAvailable ? dustMissionAmbientColor[1] * requestedSkyAmbientStrength : 0.025f,
        dustMissionSkyAvailable ? dustMissionAmbientColor[2] * requestedSkyAmbientStrength : 0.030f};

    volumetricDustLightScratch.clear();
    if (volumetricDustLightScratch.capacity() < maximumVolumetricDustLights)
        volumetricDustLightScratch.reserve(maximumVolumetricDustLights);

    const auto normalizeDustLightDirection = [](float direction[3])
    {
        const float lengthSquared = direction[0] * direction[0] +
            direction[1] * direction[1] + direction[2] * direction[2];
        if (lengthSquared > 0.00000001f)
        {
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            direction[0] *= inverseLength;
            direction[1] *= inverseLength;
            direction[2] *= inverseLength;
        }
        else
        {
            direction[0] = 0.0f;
            direction[1] = 0.0f;
            direction[2] = -1.0f;
        }
    };

    for (const HWModernMissionLightEmitter &source : missionAuthoringLightEmitters)
    {
        if (!source.enabled || source.intensity <= 0.0001f) continue;
        if (source.type == HW_MODERN_MISSION_LIGHT_AMBIENT)
        {
            const float strength = source.intensity * requestedAuthoredLightStrength * 0.55f;
            ambient[0] += source.color[0] * strength;
            ambient[1] += source.color[1] * strength;
            ambient[2] += source.color[2] * strength;
            continue;
        }
        if (volumetricDustLightScratch.size() >= maximumVolumetricDustLights) break;
        GpuDustLight light = {};
        light.type = source.type == HW_MODERN_MISSION_LIGHT_SPOT ? 3u : 2u;
        std::memcpy(light.position, source.position, sizeof(light.position));
        std::memcpy(light.direction, source.direction, sizeof(light.direction));
        normalizeDustLightDirection(light.direction);
        std::memcpy(light.color, source.color, sizeof(light.color));
        light.intensity = source.intensity * requestedAuthoredLightStrength;
        light.radius = std::max(1.0f, source.radius);
        light.coneCos = volumetricDustDegreesToCosine(source.coneAngle);
        light.edgeCos = volumetricDustDegreesToCosine(source.coneAngle + source.edgeAngle);
        volumetricDustLightScratch.push_back(light);
    }

    if (!missionAuthoringReplacesMapLightsForDust)
    {
        for (const HWModernMapLightEmitter &source : mapLightEmitters)
        {
            if (volumetricDustLightScratch.size() >= maximumVolumetricDustLights) break;
            if (source.intensity <= 0.0001f || source.type == 0 || source.type == 5)
                continue;
            GpuDustLight light = {};
            light.type = source.type == 2 ? 3u : 2u;
            std::memcpy(light.position, source.renderPosition, sizeof(light.position));
            std::memcpy(light.direction, source.renderDirection, sizeof(light.direction));
            normalizeDustLightDirection(light.direction);
            std::memcpy(light.color, source.color, sizeof(light.color));
            light.intensity = std::max(0.0f, source.intensity) * requestedAuthoredLightStrength;
            light.radius = 250000.0f;
            light.coneCos = volumetricDustDegreesToCosine(source.coneAngle);
            light.edgeCos = volumetricDustDegreesToCosine(source.coneAngle + source.edgeAngle);
            volumetricDustLightScratch.push_back(light);
        }
    }

    for (const HWModernDynamicLightEmitter &source : dynamicLightEmitters)
    {
        if (volumetricDustLightScratch.size() >= maximumVolumetricDustLights) break;
        if (source.intensity <= 0.0001f || source.radius <= 1.0f) continue;
        GpuDustLight light = {};
        light.type = 2u;
        if (source.shape == HW_MODERN_LIGHT_LINE)
        {
            light.position[0] = (source.position[0] + source.endPosition[0]) * 0.5f;
            light.position[1] = (source.position[1] + source.endPosition[1]) * 0.5f;
            light.position[2] = (source.position[2] + source.endPosition[2]) * 0.5f;
        }
        else
        {
            std::memcpy(light.position, source.position, sizeof(light.position));
        }
        std::memcpy(light.color, source.color, sizeof(light.color));
        const bool dedicatedWeaponEndpoint =
            source.source == HW_MODERN_LIGHT_MUZZLE_FLASH ||
            source.source == HW_MODERN_LIGHT_WEAPON_IMPACT;
        light.intensity = std::max(0.0f, source.intensity) * 0.02f *
            (dedicatedWeaponEndpoint ? 1.0f : requestedFxLightingStrength);
        light.radius = std::max(1.0f, source.radius);
        volumetricDustLightScratch.push_back(light);
    }

    constants.dustAmbientColorStrength[0] = std::max(0.0f, ambient[0]);
    constants.dustAmbientColorStrength[1] = std::max(0.0f, ambient[1]);
    constants.dustAmbientColorStrength[2] = std::max(0.0f, ambient[2]);
    constants.dustAmbientColorStrength[3] = 1.0f;

    /* Low-noise proof that live F12 authoring actually reaches the volumetric
       upload path. Emit only when the effective authored lighting changes. */
    {
        float localEnergy[3] = {0.0f, 0.0f, 0.0f};
        for (const GpuDustLight &light : volumetricDustLightScratch)
        {
            localEnergy[0] += std::max(0.0f, light.color[0]) * std::max(0.0f, light.intensity);
            localEnergy[1] += std::max(0.0f, light.color[1]) * std::max(0.0f, light.intensity);
            localEnergy[2] += std::max(0.0f, light.color[2]) * std::max(0.0f, light.intensity);
        }
        static int lastSunValid = -1;
        static unsigned int lastLightCount = ~0u;
        static float lastSun[3] = {-1000.0f, -1000.0f, -1000.0f};
        static float lastAmbient[3] = {-1000.0f, -1000.0f, -1000.0f};
        static float lastLocalEnergy[3] = {-1000.0f, -1000.0f, -1000.0f};
        bool changed = lastSunValid != (dustMissionSunValid ? 1 : 0) ||
                       lastLightCount != static_cast<unsigned int>(volumetricDustLightScratch.size());
        for (unsigned int channel = 0; channel < 3; ++channel)
        {
            changed = changed || std::fabs(lastSun[channel] - dustMissionSunColor[channel]) > 0.01f ||
                                std::fabs(lastAmbient[channel] - ambient[channel]) > 0.01f ||
                                std::fabs(lastLocalEnergy[channel] - localEnergy[channel]) > 0.01f;
        }
        if (changed)
        {
            std::fprintf(stderr,
                "[ModernGraphics] Dust authored lighting live: sunValid=%d "
                "sunRGB=(%.3f,%.3f,%.3f) sunStrength=%.2f "
                "ambientRGB=(%.3f,%.3f,%.3f) localLights=%zu "
                "localRGBxI=(%.3f,%.3f,%.3f).\n",
                dustMissionSunValid ? 1 : 0,
                dustMissionSunColor[0], dustMissionSunColor[1], dustMissionSunColor[2],
                dustMissionSunValid ? 0.82f * requestedPrimarySunStrength : 0.0f,
                ambient[0], ambient[1], ambient[2],
                volumetricDustLightScratch.size(),
                localEnergy[0], localEnergy[1], localEnergy[2]);
            lastSunValid = dustMissionSunValid ? 1 : 0;
            lastLightCount = static_cast<unsigned int>(volumetricDustLightScratch.size());
            for (unsigned int channel = 0; channel < 3; ++channel)
            {
                lastSun[channel] = dustMissionSunColor[channel];
                lastAmbient[channel] = ambient[channel];
                lastLocalEnergy[channel] = localEnergy[channel];
            }
        }
    }

    volumetricDustScratch.clear();
    if (volumetricDustScratch.capacity() < maximumVolumetricDustVolumes)
        volumetricDustScratch.reserve(maximumVolumetricDustVolumes);
    for (const HWModernVolumetricDustVolume &source : volumetricDustVolumes)
    {
        if (!source.enabled || source.density <= 0.00001f) continue;
        volumetricDustScratch.push_back(source);
        if (volumetricDustScratch.size() >= maximumVolumetricDustVolumes) break;
    }
    const auto frontDistance = [cameraX, cameraY, cameraZ](
        const HWModernVolumetricDustVolume &volume)
    {
        float half[3] = {
            std::max(1.0f, volume.size[0] * 0.5f),
            std::max(1.0f, volume.size[1] * 0.5f),
            std::max(1.0f, volume.size[2] * 0.5f)};
        if (volume.shape == HW_MODERN_DUST_SPHERE)
        {
            const float radius = std::max(
                1.0f, (half[0] + half[1] + half[2]) / 3.0f);
            half[0] = half[1] = half[2] = radius;
        }
        const float radius = std::sqrt(
            half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);
        const float dx = volume.position[0] - cameraX;
        const float dy = volume.position[1] - cameraY;
        const float dz = volume.position[2] - cameraZ;
        return std::max(0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) -
                              radius);
    };
    /* Front-to-back volume order improves early transmittance termination when
       several huge clouds overlap. Center-distance sorting could process a
       farther giant volume first simply because its centre happened to be
       closer, multiplying work and producing a less physical composition. */
    std::stable_sort(volumetricDustScratch.begin(), volumetricDustScratch.end(),
        [&frontDistance](const HWModernVolumetricDustVolume &left,
                         const HWModernVolumetricDustVolume &right)
        {
            return frontDistance(left) < frontDistance(right);
        });

    /* Build transient wake data before uploading volumes so every volume can
       carry compact relevance masks. The high flag bits are runtime-only and
       never written back to the authored map/editor state. */
    updateVolumetricDustShipWakes();

    const auto volumeInfluenceRadius = [](const HWModernVolumetricDustVolume &volume)
    {
        float half[3] = {
            std::max(1.0f, volume.size[0] * 0.5f),
            std::max(1.0f, volume.size[1] * 0.5f),
            std::max(1.0f, volume.size[2] * 0.5f)};
        if (volume.shape == HW_MODERN_DUST_SPHERE)
        {
            const float radius = std::max(
                1.0f, (half[0] + half[1] + half[2]) / 3.0f);
            half[0] = half[1] = half[2] = radius;
        }
        const float coreRadius = std::sqrt(
            half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);
        const float largestHalf = std::max(half[0], std::max(half[1], half[2]));
        const float variation = std::max(0.0f, std::min(
            1.0f, volume.shapeVariation * 0.75f));
        /* Deliberately generous: masks may include an irrelevant source, but
           must never reject a source capable of visibly affecting the soft
           exterior tail. */
        return coreRadius * (1.0f + 0.60f * variation) +
               largestHalf * 0.85f;
    };

    const auto pointSegmentDistanceSquared = [](
        const float point[3], const float start[3], const float end[3])
    {
        const float sx = end[0] - start[0];
        const float sy = end[1] - start[1];
        const float sz = end[2] - start[2];
        const float px = point[0] - start[0];
        const float py = point[1] - start[1];
        const float pz = point[2] - start[2];
        const float segmentLengthSquared = sx * sx + sy * sy + sz * sz;
        const float t = segmentLengthSquared > 0.000001f
            ? std::max(0.0f, std::min(1.0f,
                (px * sx + py * sy + pz * sz) / segmentLengthSquared))
            : 0.0f;
        const float dx = point[0] - (start[0] + sx * t);
        const float dy = point[1] - (start[1] + sy * t);
        const float dz = point[2] - (start[2] + sz * t);
        return dx * dx + dy * dy + dz * dz;
    };

    for (HWModernVolumetricDustVolume &volume : volumetricDustScratch)
    {
        const float volumeRadius = volumeInfluenceRadius(volume);
        unsigned int wakeMask = 0u;
        for (unsigned int wakeIndex = 0u;
             wakeIndex < volumetricDustWakeScratch.size() && wakeIndex < 12u;
             ++wakeIndex)
        {
            const GpuDustWake &wake = volumetricDustWakeScratch[wakeIndex];
            float end[3] = {
                wake.position[0] - wake.direction[0] * wake.length,
                wake.position[1] - wake.direction[1] * wake.length,
                wake.position[2] - wake.direction[2] * wake.length};
            const float influence = volumeRadius + wake.radius * 2.4f;
            if (pointSegmentDistanceSquared(
                    volume.position, wake.position, end) <= influence * influence)
            {
                wakeMask |= 1u << wakeIndex;
            }
        }

        unsigned int lightMask = 0u;
        for (unsigned int lightIndex = 0u;
             lightIndex < volumetricDustLightScratch.size() && lightIndex < 12u;
             ++lightIndex)
        {
            const GpuDustLight &light = volumetricDustLightScratch[lightIndex];
            const float dx = light.position[0] - volume.position[0];
            const float dy = light.position[1] - volume.position[1];
            const float dz = light.position[2] - volume.position[2];
            const float influence = volumeRadius + std::max(1.0f, light.radius);
            if (dx * dx + dy * dy + dz * dz <= influence * influence)
            {
                lightMask |= 1u << lightIndex;
            }
        }

        volume.flags = (volume.flags & 0x000000ffu) |
                       ((wakeMask & 0x0fffu) << 8u) |
                       ((lightMask & 0x0fffu) << 20u);
    }

    const unsigned int volumeCount =
        static_cast<unsigned int>(volumetricDustScratch.size());
    const unsigned int lightCount =
        static_cast<unsigned int>(volumetricDustLightScratch.size());
    const unsigned int wakeCount =
        static_cast<unsigned int>(volumetricDustWakeScratch.size());
    /* Runtime GPU records are required by both renderers. The world-cache
       bounds below are not: when direct raymarch is active, skip all of that
       axis-aligned grid preparation and upload only the per-volume invariants. */
    const bool useWorldCache = dustWorldCacheRequested();
    float finestRequestedCellSize = 520.0f;
    for (unsigned int volumeIndex = 0; volumeIndex < volumeCount; ++volumeIndex)
    {
        const HWModernVolumetricDustVolume &volume =
            volumetricDustScratch[volumeIndex];
        volumetricDustPrecomputeVolume(volume, frame.mappedDustVolumes[volumeIndex]);
        if (!useWorldCache) continue;
        const float scale = std::max(volume.noiseScale, 0.000005f);
        const float featureSize = 1.0f / (scale * 1.65f);
        finestRequestedCellSize = std::min(finestRequestedCellSize,
            std::max(240.0f, std::min(520.0f, featureSize * 0.75f)));
    }

    if (useWorldCache && volumeCount > 0)
    {
        /* A global union made six Mission 04 clouds spanning hundreds of
           thousands of units share one 128-ish grid; each 400-unit noise
           feature became a multi-thousand-unit blob. Use a world-snapped,
           camera-local clipmap instead. Overlapping cells retain identical
           world centres as the camera moves, so the field stays stable. */
        const float cellSize = finestRequestedCellSize;
        const UINT activeDimension = dustWorldGridActiveDimension;
        const float gridExtent = cellSize * static_cast<float>(activeDimension);
        float forward[3] = {
            presenter.dustRayUv00[0] + presenter.dustRayUv10[0] +
                presenter.dustRayUv01[0],
            presenter.dustRayUv00[1] + presenter.dustRayUv10[1] +
                presenter.dustRayUv01[1],
            presenter.dustRayUv00[2] + presenter.dustRayUv10[2] +
                presenter.dustRayUv01[2]};
        const float forwardLength = std::sqrt(
            forward[0] * forward[0] + forward[1] * forward[1] +
            forward[2] * forward[2]);
        if (forwardLength > 0.000001f)
            for (float &component : forward) component /= forwardLength;
        float gridMinimum[3] = {};
        for (unsigned int axis = 0; axis < 3; ++axis)
        {
            const float biasedCenter = presenter.dustCameraPosition[axis] +
                forward[axis] * gridExtent * 0.18f;
            const float snappedCenter = std::floor(
                biasedCenter / cellSize + 0.5f) * cellSize;
            gridMinimum[axis] = snappedCenter - gridExtent * 0.5f;
        }

        constants.dustGridMinWidth[0] = gridMinimum[0];
        constants.dustGridMinWidth[1] = gridMinimum[1];
        constants.dustGridMinWidth[2] = gridMinimum[2];
        constants.dustGridMinWidth[3] = static_cast<float>(activeDimension);
        constants.dustGridSizeHeight[0] = gridExtent;
        constants.dustGridSizeHeight[1] = gridExtent;
        constants.dustGridSizeHeight[2] = gridExtent;
        constants.dustGridSizeHeight[3] = static_cast<float>(activeDimension);
        constants.presenterPadding[0] = static_cast<float>(activeDimension);

        static float lastCellSize = -1.0f;
        if (std::fabs(lastCellSize - cellSize) > 0.1f)
        {
            std::fprintf(stderr,
                "[ModernGraphics] World-snapped dust clipmap: %ux%ux%u, "
                "cubic voxel %.1f, extent %.1f world units.\n",
                activeDimension, activeDimension, activeDimension,
                cellSize, gridExtent);
            lastCellSize = cellSize;
        }
    }
    if (lightCount > 0)
        std::memcpy(frame.mappedDustLights, volumetricDustLightScratch.data(),
                    lightCount * sizeof(GpuDustLight));
    if (wakeCount > 0 && frame.mappedDustWakes != nullptr)
        std::memcpy(frame.mappedDustWakes, volumetricDustWakeScratch.data(),
                    wakeCount * sizeof(GpuDustWake));
    constants.dustVolumeCount = volumeCount;
    constants.dustLightCount = lightCount;
    constants.dustWakeCount = wakeCount;
    constants.dustRenderEnabled = volumeCount > 0 ? 1u : 0u;
}

std::wstring shaderCacheDirectory()
{
    wchar_t localAppData[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData,
        static_cast<DWORD>(sizeof(localAppData) / sizeof(localAppData[0])));
    if (length == 0 || length >= sizeof(localAppData) / sizeof(localAppData[0]))
    {
        return std::wstring();
    }
    std::wstring directory(localAppData);
    directory += L"\\HomeworldModern";
    CreateDirectoryW(directory.c_str(), nullptr);
    directory += L"\\ShaderCache";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

std::wstring shaderCachePath(const wchar_t *name)
{
    std::wstring directory = shaderCacheDirectory();
    if (directory.empty()) return directory;
    directory += L"\\";
    directory += name;
    return directory;
}

bool readCacheFile(const std::wstring &path, std::vector<unsigned char> &bytes)
{
    if (path.empty()) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0 &&
              size.QuadPart <= 64ll * 1024ll * 1024ll;
    if (ok)
    {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                      &read, nullptr) != FALSE && read == bytes.size();
    }
    CloseHandle(file);
    if (!ok) bytes.clear();
    return ok;
}

bool writeCacheFile(const std::wstring &path, const void *data, size_t size)
{
    if (path.empty() || data == nullptr || size == 0 || size > 0xffffffffu)
        return false;
    std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, data, static_cast<DWORD>(size), &written,
                        nullptr) != FALSE && written == size;
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok)
    {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

std::uint64_t presenterShaderHash(const char *entryPoint, const char *target)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](const void *source, size_t length)
    {
        const unsigned char *bytes = static_cast<const unsigned char *>(source);
        for (size_t index = 0; index < length; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
    };
    append(presenterShader, sizeof(presenterShader) - 1);
    append(entryPoint, std::strlen(entryPoint));
    append(target, std::strlen(target));
    static const char cacheVersion[] = "HomeworldModern-0.91.2";
    append(cacheVersion, sizeof(cacheVersion) - 1);
    return hash;
}

void copyAdapterName(const wchar_t *source, char *destination,
                     size_t destinationSize)
{
    if (destinationSize == 0)
    {
        return;
    }
    destination[0] = '\0';
    if (source == nullptr)
    {
        return;
    }
    int written = WideCharToMultiByte(CP_UTF8, 0, source, -1, destination,
                                      static_cast<int>(destinationSize),
                                      nullptr, nullptr);
    if (written <= 0)
    {
        std::snprintf(destination, destinationSize, "Unknown adapter");
    }
    destination[destinationSize - 1] = '\0';
}

HRESULT selectHardwareAdapter(IDXGIFactory6 *factory,
                              ComPtr<IDXGIAdapter1> &selected)
{
    if (factory == nullptr)
    {
        return E_INVALIDARG;
    }
    for (UINT index = 0; ; ++index)
    {
        ComPtr<IDXGIAdapter1> candidate;
        HRESULT result = factory->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&candidate));
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            return result;
        }
        if (FAILED(result))
        {
            continue;
        }
        DXGI_ADAPTER_DESC1 description = {};
        if (FAILED(candidate->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(),
                                        D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr)))
        {
            selected = candidate;
            return S_OK;
        }
    }
}

unsigned int queryMaximumFeatureLevel(ID3D12Device *device)
{
    static const D3D_FEATURE_LEVEL requestedLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
    levels.NumFeatureLevels = static_cast<UINT>(
        sizeof(requestedLevels) / sizeof(requestedLevels[0]));
    levels.pFeatureLevelsRequested = requestedLevels;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                           &levels, sizeof(levels))))
    {
        return 0;
    }
    return static_cast<unsigned int>(levels.MaxSupportedFeatureLevel);
}

const char *raytracingTierName(HWModernRaytracingTier tier)
{
    switch (tier)
    {
        case HW_MODERN_RAYTRACING_TIER_1_1: return "1.1";
        case HW_MODERN_RAYTRACING_TIER_1_0: return "1.0";
        default: return "unavailable";
    }
}

bool waitForFenceValue(UINT64 fenceValue)
{
    if (fenceValue == 0 || presenter.fence->GetCompletedValue() >= fenceValue)
    {
        return true;
    }
    HRESULT result = presenter.fence->SetEventOnCompletion(
        fenceValue, presenter.fenceEvent);
    if (FAILED(result))
    {
        logFailure("ID3D12Fence::SetEventOnCompletion", result);
        return false;
    }
    DWORD waited = WaitForSingleObject(presenter.fenceEvent, INFINITE);
    if (waited != WAIT_OBJECT_0)
    {
        std::fprintf(stderr, "[ModernGraphics] Waiting for D3D12 work failed "
                             "(Win32 error %lu).\n", GetLastError());
        return false;
    }
    return true;
}

bool waitForSubmittedWork()
{
    return waitForFenceValue(presenter.submittedFenceValue);
}

void releaseUploadFrames()
{
    for (D3D12FrameContext &frame : presenter.frames)
    {
        if (frame.upload && frame.mappedUpload != nullptr)
        {
            frame.upload->Unmap(0, nullptr);
        }
        if (frame.worldUpload && frame.mappedWorldUpload != nullptr)
        {
            frame.worldUpload->Unmap(0, nullptr);
        }
        if (frame.backgroundUpload && frame.mappedBackgroundUpload != nullptr)
        {
            frame.backgroundUpload->Unmap(0, nullptr);
        }
        if (frame.occluderUpload && frame.mappedOccluderUpload != nullptr)
        {
            frame.occluderUpload->Unmap(0, nullptr);
        }
        frame.mappedUpload = nullptr;
        frame.mappedWorldUpload = nullptr;
        frame.mappedBackgroundUpload = nullptr;
        frame.mappedOccluderUpload = nullptr;
        frame.uploadSize = 0;
        frame.upload.Reset();
        frame.worldUpload.Reset();
        frame.backgroundUpload.Reset();
        frame.occluderUpload.Reset();
    }
}

void releaseSizeDependentResources()
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::upscalerReleaseOutput();
    hwmodern::raytracingReleaseOutput();
#endif
    releaseUploadFrames();
    presenter.sourceTexture.Reset();
    presenter.worldTexture.Reset();
    presenter.backgroundTexture.Reset();
    presenter.occluderTexture.Reset();
    presenter.sceneInputTexture.Reset();
    presenter.dustFroxelTexture.Reset();
    for (ComPtr<ID3D12Resource> &texture : presenter.dustScreenTextures)
        texture.Reset();
    for (ComPtr<ID3D12Resource> &buffer : presenter.backBuffers)
    {
        buffer.Reset();
    }
    presenter.uploadFootprint = {};
    presenter.sourceTextureShaderReadable = false;
    presenter.worldTextureShaderReadable = false;
    presenter.backgroundTextureShaderReadable = false;
    presenter.occluderTextureShaderReadable = false;
    presenter.sceneInputTextureShaderReadable = false;
    presenter.sceneInputWidth = 0;
    presenter.sceneInputHeight = 0;
    presenter.dustFroxelWidth = 0;
    presenter.dustFroxelHeight = 0;
    presenter.dustFroxelDepth = 0;
    presenter.dustFroxelShaderReadable = false;
    presenter.dustFroxelValidThisFrame = false;
    presenter.dustScreenWidth = 0;
    presenter.dustScreenHeight = 0;
    presenter.dustScreenWriteIndex = 0;
    presenter.dustScreenShaderReadable[0] = true;
    presenter.dustScreenShaderReadable[1] = true;
    presenter.dustScreenHistoryValid = false;
}

void releasePresenter()
{
    presenter.active = false;
    presenter.visiblePresenter = false;
    presenter.frameOpen = false;
    presenter.frameCaptured = false;
    presenter.worldFrameCaptured = false;
    presenter.backgroundFrameCaptured = false;
    presenter.occluderFrameCaptured = false;
    releaseSizeDependentResources();
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingShutdown();
    hwmodern::upscalerReleaseDevice();
    hwmodern::dlaaReleaseDevice();
#endif
    presenter.presenterPipeline.Reset();
    presenter.scenePrepassPipeline.Reset();
    presenter.dustFroxelPipeline.Reset();
    presenter.dustScreenPipeline.Reset();
    presenter.dustFroxelRootSignature.Reset();
    presenter.rootSignature.Reset();
    presenter.timestampQueryHeap.Reset();
    presenter.srvHeap.Reset();
    presenter.rtvHeap.Reset();
    presenter.commandList.Reset();
    for (D3D12FrameContext &frame : presenter.frames)
    {
        if (frame.dustVolumeUpload && frame.mappedDustVolumes)
            frame.dustVolumeUpload->Unmap(0, nullptr);
        if (frame.dustLightUpload && frame.mappedDustLights)
            frame.dustLightUpload->Unmap(0, nullptr);
        if (frame.dustWakeUpload && frame.mappedDustWakes)
            frame.dustWakeUpload->Unmap(0, nullptr);
        frame.mappedDustVolumes = nullptr;
        frame.mappedDustLights = nullptr;
        frame.mappedDustWakes = nullptr;
        frame.dustVolumeUpload.Reset();
        frame.dustLightUpload.Reset();
        frame.dustWakeUpload.Reset();
        frame.timestampReadback.Reset();
        frame.timestampPending = false;
        frame.allocator.Reset();
        frame.fenceValue = 0;
    }
    if (presenter.swapchain)
    {
        presenter.swapchain->SetFullscreenState(FALSE, nullptr);
    }
    presenter.swapchain.Reset();
    presenter.queue.Reset();
    presenter.fence.Reset();
    presenter.raytracingDevice.Reset();
    presenter.device.Reset();
    presenter.adapter.Reset();
    presenter.factory.Reset();
    if (presenter.fenceEvent != nullptr)
    {
        CloseHandle(presenter.fenceEvent);
        presenter.fenceEvent = nullptr;
    }
    presenter.window = nullptr;
    presenter.rtvDescriptorSize = 0;
    presenter.srvDescriptorSize = 0;
    presenter.width = 0;
    presenter.height = 0;
    presenter.nextFenceValue = 1;
    presenter.submittedFenceValue = 0;
    presenter.activeFrameIndex = 0;
    presenter.frameCount = 0;
    presenter.dustCameraValid = false;
    presenter.backgroundReadbackMilliseconds = 0.0;
    presenter.worldReadbackMilliseconds = 0.0;
    presenter.frameReadbackMilliseconds = 0.0;
    presenter.dxrRecordMilliseconds = 0.0;
    presenter.backgroundReadbackCount = 0;
    presenter.worldReadbackCount = 0;
    presenter.frameReadbackCount = 0;
    presenter.dxrRecordCount = 0;
    presenter.allowTearing = false;
    presenter.vsyncMode = 1;
    dynamicLightEmitters.clear();
    timedDynamicLightEmitters.clear();
}

bool compileShader(const char *entryPoint, const char *target,
                   ComPtr<ID3DBlob> &bytecode)
{
    const std::uint64_t hash = presenterShaderHash(entryPoint, target);
    wchar_t cacheName[160] = {};
    swprintf_s(cacheName, L"Presenter-%S-%S-%016llx.cso", entryPoint, target,
               static_cast<unsigned long long>(hash));
    const std::wstring cachePath = shaderCachePath(cacheName);
    std::vector<unsigned char> cached;
    bool cachedBytecodeValid = false;
    if (readCacheFile(cachePath, cached) && cached.size() >= 32 &&
        std::memcmp(cached.data(), "DXBC", 4) == 0)
    {
        std::uint32_t declaredSize = 0;
        std::memcpy(&declaredSize, cached.data() + 24,
                    sizeof(declaredSize));
        cachedBytecodeValid = declaredSize == cached.size();
    }
    if (cachedBytecodeValid &&
        SUCCEEDED(D3DCreateBlob(cached.size(), &bytecode)))
    {
        std::memcpy(bytecode->GetBufferPointer(), cached.data(), cached.size());
        std::fprintf(stderr, "[ModernGraphics] Shader cache hit: %s/%s.\n",
                     entryPoint, target);
        return true;
    }
    if (!cached.empty())
    {
        DeleteFileW(cachePath.c_str());
        std::fprintf(stderr,
            "[ModernGraphics] Rejected invalid shader cache entry: %s/%s.\n",
            entryPoint, target);
    }

    ComPtr<ID3DBlob> errors;
    HRESULT result = D3DCompile(presenterShader, sizeof(presenterShader) - 1,
                                "HomeworldModernPresenter.hlsl", nullptr,
                                nullptr, entryPoint, target,
                                D3DCOMPILE_ENABLE_STRICTNESS |
                                D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                0, &bytecode, &errors);
    if (FAILED(result))
    {
        if (errors && errors->GetBufferPointer())
        {
            std::fprintf(stderr, "[ModernGraphics] Presenter shader error: %s\n",
                         static_cast<const char *>(errors->GetBufferPointer()));
        }
        logFailure("D3DCompile", result);
        return false;
    }
    if (writeCacheFile(cachePath, bytecode->GetBufferPointer(),
                       bytecode->GetBufferSize()))
    {
        std::fprintf(stderr, "[ModernGraphics] Shader cache stored: %s/%s.\n",
                     entryPoint, target);
    }
    return true;
}

bool compileDustFroxelShader(ComPtr<ID3DBlob> &bytecode)
{
    D3D_SHADER_MACRO macros[] = {
        { "DUST_FROXEL_COMPUTE", "1" },
        { nullptr, nullptr }
    };
    ComPtr<ID3DBlob> errors;
    HRESULT result = D3DCompile(
        presenterShader, sizeof(presenterShader) - 1,
        "HomeworldModernDustFroxel.hlsl", macros, nullptr,
        "BuildDustFroxelCS", "cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &bytecode, &errors);
    if (FAILED(result))
    {
        if (errors && errors->GetBufferPointer())
        {
            std::fprintf(stderr,
                "[ModernGraphics] Dust froxel compute shader error: %s\n",
                static_cast<const char *>(errors->GetBufferPointer()));
        }
        logFailure("D3DCompile(dust froxel compute)", result);
        return false;
    }
    return true;
}

void resetGodRaySource(int mapIdentity)
{
    presenter.godRayMapIdentity = mapIdentity;
    presenter.godRayCenterX = 0.5f;
    presenter.godRayCenterY = 0.42f;
    presenter.godRayWorldDirection[0] = 0.0f;
    presenter.godRayWorldDirection[1] = 0.0f;
    presenter.godRayWorldDirection[2] = -1.0f;
    presenter.godRaySourceLocked = false;
    presenter.godRaySourceVisible = false;
}
void projectLockedGodRaySource(float verticalFieldOfViewDegrees,
                               float aspectRatio,
                               const float viewMatrix[16])
{
    if (!presenter.godRaySourceLocked || viewMatrix == nullptr)
    {
        presenter.godRaySourceVisible = false;
        return;
    }
    const float wx = presenter.godRayWorldDirection[0];
    const float wy = presenter.godRayWorldDirection[1];
    const float wz = presenter.godRayWorldDirection[2];
    const float viewX = viewMatrix[0] * wx + viewMatrix[4] * wy + viewMatrix[8] * wz;
    const float viewY = viewMatrix[1] * wx + viewMatrix[5] * wy + viewMatrix[9] * wz;
    const float viewZ = viewMatrix[2] * wx + viewMatrix[6] * wy + viewMatrix[10] * wz;
    if (viewZ >= -0.0001f)
    {
        presenter.godRaySourceVisible = false;
        return;
    }

    constexpr float pi = 3.14159265358979323846f;
    const float safeAspect = aspectRatio > 0.01f ? aspectRatio : 1.0f;
    const float tanHalfFov = std::tan(
        std::max(1.0f, std::min(verticalFieldOfViewDegrees, 179.0f)) *
        (pi / 360.0f));
    const float ndcX = viewX / (-viewZ * tanHalfFov * safeAspect);
    const float ndcY = viewY / (-viewZ * tanHalfFov);
    presenter.godRayCenterX = ndcX * 0.5f + 0.5f;
    presenter.godRayCenterY = ndcY * 0.5f + 0.5f;
    presenter.godRaySourceVisible =
        presenter.godRayCenterX >= 0.0f && presenter.godRayCenterX <= 1.0f &&
        presenter.godRayCenterY >= 0.0f && presenter.godRayCenterY <= 1.0f;
}

bool updateDustCameraFromRenderView(float verticalFieldOfViewDegrees,
                                    float aspectRatio,
                                    const float viewMatrix[16])
{
    if (viewMatrix == nullptr) return false;
    for (unsigned int index = 0; index < 16; ++index)
    {
        if (!std::isfinite(viewMatrix[index])) return false;
    }

    constexpr float pi = 3.14159265358979323846f;
    const float safeAspect = aspectRatio > 0.01f ? aspectRatio : 1.0f;
    const float safeFov = std::max(1.0f,
        std::min(verticalFieldOfViewDegrees, 179.0f));
    const float tanHalfFov = std::tan(safeFov * (pi / 360.0f));
    if (!(tanHalfFov > 0.000001f) || !std::isfinite(tanHalfFov)) return false;

    /* OpenGL supplies a column-major rigid world->view matrix.  Recover the
       ACTUAL render-camera origin from -R^T*t instead of trusting a separate
       editor/camera callback. */
    const float tx = viewMatrix[12];
    const float ty = viewMatrix[13];
    const float tz = viewMatrix[14];
    const float eye[3] = {
        -(viewMatrix[0] * tx + viewMatrix[1] * ty + viewMatrix[2] * tz),
        -(viewMatrix[4] * tx + viewMatrix[5] * ty + viewMatrix[6] * tz),
        -(viewMatrix[8] * tx + viewMatrix[9] * ty + viewMatrix[10] * tz)
    };
    if (!std::isfinite(eye[0]) || !std::isfinite(eye[1]) ||
        !std::isfinite(eye[2])) return false;

    /* Store UNNORMALIZED camera-plane vectors for UV (0,0), (1,0), (0,1).
       HLSL linearly interpolates these and normalizes once per pixel.  This is
       exact perspective-ray reconstruction for the same view matrix used to
       draw the world and cannot become a camera-facing billboard basis. */
    const float ndcX[3] = {-1.0f, 1.0f, -1.0f};
    /* Presenter UVs originate at the TOP left. OpenGL camera NDC originates
       at the bottom left, so texture Y must be inverted during ray recovery.
       Leaving this unflipped made yaw appear correct while pitch translated
       the cloud with the screen. */
    const float ndcY[3] = {1.0f, 1.0f, -1.0f};
    float rays[3][3] = {};
    for (unsigned int rayIndex = 0; rayIndex < 3; ++rayIndex)
    {
        const float viewX = ndcX[rayIndex] * safeAspect * tanHalfFov;
        const float viewY = ndcY[rayIndex] * tanHalfFov;
        const float viewZ = -1.0f;
        rays[rayIndex][0] = viewMatrix[0] * viewX +
                            viewMatrix[1] * viewY +
                            viewMatrix[2] * viewZ;
        rays[rayIndex][1] = viewMatrix[4] * viewX +
                            viewMatrix[5] * viewY +
                            viewMatrix[6] * viewZ;
        rays[rayIndex][2] = viewMatrix[8] * viewX +
                            viewMatrix[9] * viewY +
                            viewMatrix[10] * viewZ;
        const float lengthSquared =
            rays[rayIndex][0] * rays[rayIndex][0] +
            rays[rayIndex][1] * rays[rayIndex][1] +
            rays[rayIndex][2] * rays[rayIndex][2];
        if (!(lengthSquared > 1.0e-10f) || !std::isfinite(lengthSquared))
            return false;
    }

    for (unsigned int channel = 0; channel < 3; ++channel)
    {
        presenter.dustCameraPosition[channel] = eye[channel];
        presenter.dustRayUv00[channel] = rays[0][channel];
        presenter.dustRayUv10[channel] = rays[1][channel];
        presenter.dustRayUv01[channel] = rays[2][channel];
    }
    presenter.dustCameraValid = true;
    presenter.dustCameraExplicitThisFrame = true;

    static bool logged = false;
    if (!logged)
    {
        logged = true;
        std::fprintf(stderr,
            "[ModernGraphics] Dust camera hard-locked to the actual render "
            "view matrix; legacy corner-ray callback is no longer authoritative.\n");
    }
    return true;
}

void updateDustDepthProjection(const float projectionMatrix[16])
{
    presenter.dustProjectionValid = false;
    if (projectionMatrix == nullptr) return;
    for (unsigned int index = 0; index < 16; ++index)
        if (!std::isfinite(projectionMatrix[index])) return;
    /* Homeworld uses the conventional OpenGL perspective matrix, including
       its infinite-far variant: clip.w=-view.z, P[11]=-1, P[15]=0. */
    if (std::fabs(projectionMatrix[11] + 1.0f) > 0.01f ||
        std::fabs(projectionMatrix[15]) > 0.01f ||
        std::fabs(projectionMatrix[14]) <= 0.000001f)
        return;
    presenter.dustProjectionP10 = projectionMatrix[10];
    presenter.dustProjectionP14 = projectionMatrix[14];
    presenter.dustProjectionValid = true;
}

bool createPresenterPipeline()
{
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 4;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE motionRange = {};
    motionRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    /* t4 motion, t5 background, t6 reserved/null (retired frame history),
       t7 god-ray occluder, and t8 is the exact native-raster D32 depth sampled
       by volumetric dust. */
    motionRange.NumDescriptors = 5;
    motionRange.BaseShaderRegister = 4;
    motionRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE dustRange = {};
    dustRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    dustRange.NumDescriptors = 8;
    dustRange.BaseShaderRegister = 9;
    dustRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &range;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &motionRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[2].Constants.ShaderRegister = 0;
    rootParameters[2].Constants.Num32BitValues =
        sizeof(PresenterConstants) / sizeof(unsigned int);
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &dustRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC signatureDescription = {};
    signatureDescription.NumParameters = 4;
    signatureDescription.pParameters = rootParameters;
    signatureDescription.NumStaticSamplers = 1;
    signatureDescription.pStaticSamplers = &sampler;
    signatureDescription.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serializedSignature;
    ComPtr<ID3DBlob> signatureErrors;
    HRESULT result = D3D12SerializeRootSignature(
        &signatureDescription, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedSignature, &signatureErrors);
    if (FAILED(result))
    {
        logFailure("D3D12SerializeRootSignature", result);
        return false;
    }
    result = presenter.device->CreateRootSignature(
        0, serializedSignature->GetBufferPointer(),
        serializedSignature->GetBufferSize(),
        IID_PPV_ARGS(&presenter.rootSignature));
    if (FAILED(result))
    {
        logFailure("ID3D12Device::CreateRootSignature", result);
        return false;
    }

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    if (!compileShader("PresentVS", "vs_5_0", vertexShader) ||
        !compileShader("PresentPS", "ps_5_0", pixelShader))
    {
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = {};
    pipeline.pRootSignature = presenter.rootSignature.Get();
    pipeline.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    pipeline.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    pipeline.BlendState.AlphaToCoverageEnable = FALSE;
    pipeline.BlendState.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC defaultBlend = {};
    defaultBlend.BlendEnable = FALSE;
    defaultBlend.LogicOpEnable = FALSE;
    defaultBlend.SrcBlend = D3D12_BLEND_ONE;
    defaultBlend.DestBlend = D3D12_BLEND_ZERO;
    defaultBlend.BlendOp = D3D12_BLEND_OP_ADD;
    defaultBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    defaultBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    defaultBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    defaultBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    defaultBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    for (D3D12_RENDER_TARGET_BLEND_DESC &target : pipeline.BlendState.RenderTarget)
    {
        target = defaultBlend;
    }
    pipeline.SampleMask = 0xffffffffu;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthEnable = FALSE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline.DepthStencilState.StencilEnable = FALSE;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = presenterFormat;
    pipeline.SampleDesc.Count = 1;
    /* Driver pipeline-library blobs are intentionally not persisted. A valid
       blob written by one run can make D3D12Core access invalid memory on a
       later run after the driver/runtime state changes. Presenter bytecode is
       still cached above; creating this single lightweight PSO is negligible
       and keeps startup deterministic. */
    result = presenter.device->CreateGraphicsPipelineState(
        &pipeline, IID_PPV_ARGS(&presenter.presenterPipeline));
    if (FAILED(result))
    {
        logFailure("ID3D12Device::CreateGraphicsPipelineState", result);
        return false;
    }

    /* The Streamline input is a HUD-less full scene rendered at the selected
       internal DLSS/DLAA resolution. It uses FP16 so path lighting, dust and
       bloom stay intact until the reconstruction pass. */
    pipeline.RTVFormats[0] = sceneFormat;
    result = presenter.device->CreateGraphicsPipelineState(
        &pipeline, IID_PPV_ARGS(&presenter.scenePrepassPipeline));
    if (FAILED(result))
    {
        logFailure("ID3D12Device::CreateGraphicsPipelineState(scene prepass)", result);
        return false;
    }

    ComPtr<ID3DBlob> dustScreenShader;
    if (!compileShader("DustScreenPS", "ps_5_0", dustScreenShader))
        return false;
    pipeline.PS = { dustScreenShader->GetBufferPointer(),
                    dustScreenShader->GetBufferSize() };
    result = presenter.device->CreateGraphicsPipelineState(
        &pipeline, IID_PPV_ARGS(&presenter.dustScreenPipeline));
    if (FAILED(result))
    {
        logFailure("CreateGraphicsPipelineState(dust screen)", result);
        return false;
    }
    return true;
}

bool createDustFroxelPipeline()
{
    if (!dustWorldCacheRequested())
    {
        std::fprintf(stderr,
            "[ModernGraphics] Direct procedural dust A/B mode active; "
            "world-space cache disabled by HW_DUST_DIRECT_RAYMARCH/HW_DUST_WORLD_CACHE.\n");
        return true;
    }
    std::fprintf(stderr,
        "[ModernGraphics] World-locked shared dust cache active: 160^3 physical "
        "storage, camera derived from the actual render view matrix.\n");

    D3D12_DESCRIPTOR_RANGE dustSrvRange = {};
    dustSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    dustSrvRange.NumDescriptors = 3;
    dustSrvRange.BaseShaderRegister = 9;
    dustSrvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE froxelUavRange = {};
    froxelUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    froxelUavRange.NumDescriptors = 1;
    froxelUavRange.BaseShaderRegister = 0;
    froxelUavRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[3] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &dustSrvRange;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.Num32BitValues =
        sizeof(PresenterConstants) / sizeof(unsigned int);
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &froxelUavRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC signature = {};
    signature.NumParameters = 3;
    signature.pParameters = parameters;
    signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    HRESULT result = D3D12SerializeRootSignature(
        &signature, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(result))
    {
        logFailure("D3D12SerializeRootSignature(dust froxel)", result);
        return false;
    }
    result = presenter.device->CreateRootSignature(
        0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&presenter.dustFroxelRootSignature));
    if (FAILED(result))
    {
        logFailure("CreateRootSignature(dust froxel)", result);
        return false;
    }

    ComPtr<ID3DBlob> computeShader;
    if (!compileDustFroxelShader(computeShader)) return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = presenter.dustFroxelRootSignature.Get();
    pso.CS = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };
    result = presenter.device->CreateComputePipelineState(
        &pso, IID_PPV_ARGS(&presenter.dustFroxelPipeline));
    if (FAILED(result))
    {
        logFailure("CreateComputePipelineState(dust froxel)", result);
        return false;
    }
    return true;
}

bool createSwapchain(UINT width, UINT height)
{
    BOOL tearing = FALSE;
    if (SUCCEEDED(presenter.factory->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
    {
        presenter.allowTearing = tearing == TRUE;
    }

    DXGI_SWAP_CHAIN_DESC1 description = {};
    description.Width = width;
    description.Height = height;
    description.Format = presenterFormat;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = frameBufferCount;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    description.Flags = presenter.allowTearing ?
        DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapchain1;
    HRESULT result = presenter.factory->CreateSwapChainForHwnd(
        presenter.queue.Get(), presenter.window, &description,
        nullptr, nullptr, &swapchain1);
    if (FAILED(result))
    {
        logFailure("IDXGIFactory::CreateSwapChainForHwnd", result);
        return false;
    }
    result = swapchain1.As(&presenter.swapchain);
    if (FAILED(result))
    {
        logFailure("IDXGISwapChain4 query", result);
        return false;
    }
    UINT colorSpaceSupport = 0;
    result = presenter.swapchain->CheckColorSpaceSupport(
        DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &colorSpaceSupport);
    if (FAILED(result) ||
        (colorSpaceSupport &
         DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0)
    {
        std::fprintf(stderr,
            "[ModernGraphics] FP16 scRGB presentation is unsupported by the "
            "current Windows display path.\n");
        return false;
    }
    result = presenter.swapchain->SetColorSpace1(
        DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    if (FAILED(result))
    {
        logFailure("IDXGISwapChain4::SetColorSpace1(scRGB)", result);
        return false;
    }
    std::fprintf(stderr,
        "[ModernGraphics] FP16 scRGB presenter active; the modern frame "
        "pipeline no longer resolves through an 8-bit render target.\n");
    presenter.factory->MakeWindowAssociation(presenter.window,
                                              DXGI_MWA_NO_ALT_ENTER);
    return true;
}

float raytracingScaleForAntiAliasingMode(int mode)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    if (!hwmodern::upscalerAvailableForMode(mode))
    {
        return 1.0f;
    }
#endif
    switch (mode)
    {
        case HW_MODERN_AA_DLSS_QUALITY: return 2.0f / 3.0f;
        case HW_MODERN_AA_DLSS_BALANCED: return 0.58f;
        case HW_MODERN_AA_DLSS_PERFORMANCE: return 0.50f;
        case HW_MODERN_AA_DLSS_ULTRA_PERFORMANCE: return 1.0f / 3.0f;
        default: return 1.0f;
    }
}

UINT scaledRayDimension(UINT displayDimension, float scale)
{
    UINT dimension = std::max(1u, static_cast<UINT>(
        std::lround(static_cast<float>(displayDimension) * scale)));
    /* Even extents keep the temporal jitter and bilinear reconstruction
       centered on the same half-pixel convention in both axes. */
    if (dimension > 1u)
    {
        dimension &= ~1u;
    }
    return std::max(1u, dimension);
}

void raytracingDimensionsForAntiAliasingMode(int mode,
                                             UINT displayWidth,
                                             UINT displayHeight,
                                             UINT &rayWidth,
                                             UINT &rayHeight)
{
    const float fallbackScale = raytracingScaleForAntiAliasingMode(mode);
    rayWidth = scaledRayDimension(displayWidth, fallbackScale);
    rayHeight = scaledRayDimension(displayHeight, fallbackScale);
#if defined(HW_ENABLE_D3D12_BACKEND)
    unsigned int optimalWidth = 0;
    unsigned int optimalHeight = 0;
    if (hwmodern::upscalerGetRenderSize(
            mode, displayWidth, displayHeight,
            &optimalWidth, &optimalHeight))
    {
        rayWidth = std::max(1u, static_cast<UINT>(optimalWidth));
        rayHeight = std::max(1u, static_cast<UINT>(optimalHeight));
    }
#endif
}

#if defined(HW_ENABLE_D3D12_BACKEND)
bool createSceneInputTexture(UINT width, UINT height)
{
    if (!presenter.device || !presenter.rtvHeap) return false;
    width = std::max(1u, width);
    height = std::max(1u, height);

    presenter.sceneInputTexture.Reset();
    presenter.sceneInputTextureShaderReadable = false;
    presenter.sceneInputWidth = 0;
    presenter.sceneInputHeight = 0;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = sceneFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = sceneFormat;
    HRESULT result = presenter.device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
        IID_PPV_ARGS(&presenter.sceneInputTexture));
    if (FAILED(result))
    {
        logFailure("CreateCommittedResource(full-scene Streamline input)", result);
        return false;
    }
    presenter.sceneInputTexture->SetName(
        L"Homeworld HUD-less full-scene DLSS/DLAA input");
    presenter.sceneInputWidth = width;
    presenter.sceneInputHeight = height;
    presenter.sceneInputTextureShaderReadable = true;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        presenter.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameBufferCount) *
               presenter.rtvDescriptorSize;
    presenter.device->CreateRenderTargetView(
        presenter.sceneInputTexture.Get(), nullptr, rtv);
    return true;
}
#endif

bool createDustFroxelTexture(UINT sceneWidth, UINT sceneHeight)
{
    (void)sceneWidth;
    (void)sceneHeight;
    if (!presenter.device || !presenter.srvHeap) return false;
    presenter.dustFroxelTexture.Reset();
    presenter.dustFroxelShaderReadable = false;
    presenter.dustFroxelValidThisFrame = false;

    /* The world-locked shared cache is the performance default. Direct A/B mode
       keeps a valid t11/u0 descriptor bound but reduces dormant storage to one
       texel. */
    const bool useWorldCache = dustWorldCacheRequested();
    const UINT width = useWorldCache ? dustWorldGridTextureDimension : 1u;
    const UINT height = useWorldCache ? dustWorldGridTextureDimension : 1u;
    const UINT depth = useWorldCache ? dustWorldGridTextureDimension : 1u;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(depth);
    desc.MipLevels = useWorldCache ? 2 : 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HRESULT result = presenter.device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(&presenter.dustFroxelTexture));
    if (FAILED(result))
    {
        logFailure("CreateCommittedResource(shared dust froxel field)", result);
        return false;
    }
    presenter.dustFroxelTexture->SetName(
        L"Homeworld world-space shared volumetric dust cache");
    presenter.dustFroxelWidth = width;
    presenter.dustFroxelHeight = height;
    presenter.dustFroxelDepth = depth;
    presenter.dustFroxelShaderReadable = true;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Texture3D.MostDetailedMip = 0;
    srv.Texture3D.MipLevels = 1;
    for (UINT slot = 0; slot < frameBufferCount; ++slot)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(
            dustDescriptorBase + slot * dustDescriptorsPerFrame + 3u) *
            presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(
            presenter.dustFroxelTexture.Get(), &srv, handle);

        if (useWorldCache)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC farSrv = srv;
            farSrv.Texture3D.MostDetailedMip = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE farHandle = handle;
            farHandle.ptr += static_cast<SIZE_T>(2u) *
                             presenter.srvDescriptorSize;
            presenter.device->CreateShaderResourceView(
                presenter.dustFroxelTexture.Get(), &farSrv, farHandle);
        }
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uav.Texture3D.MipSlice = 0;
    uav.Texture3D.WSize = depth;
    D3D12_CPU_DESCRIPTOR_HANDLE uavHandle =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    uavHandle.ptr += static_cast<SIZE_T>(dustFroxelNearUavDescriptorIndex) *
                     presenter.srvDescriptorSize;
    presenter.device->CreateUnorderedAccessView(
        presenter.dustFroxelTexture.Get(), nullptr, &uav, uavHandle);
    if (useWorldCache)
    {
        uav.Texture3D.MipSlice = 1;
        uav.Texture3D.WSize = std::max(1u, depth / 2u);
        D3D12_CPU_DESCRIPTOR_HANDLE farUavHandle = uavHandle;
        farUavHandle.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateUnorderedAccessView(
            presenter.dustFroxelTexture.Get(), nullptr, &uav, farUavHandle);
    }

    if (useWorldCache)
    {
        std::fprintf(stderr,
            "[ModernGraphics] World-locked dust cache storage: %ux%ux%u cells.\n",
            width, height, depth);
    }
    else
    {
        std::fprintf(stderr,
            "[ModernGraphics] Dust cache compatibility resource: 1x1x1; direct "
            "procedural A/B mode is active.\n");
    }
    return true;
}

bool createDustScreenTextures(UINT sceneWidth, UINT sceneHeight)
{
    if (!presenter.device || !presenter.rtvHeap || !presenter.srvHeap)
        return false;
    const UINT width = std::max(1u, (sceneWidth + 1u) / 2u);
    const UINT height = std::max(1u, (sceneHeight + 1u) / 2u);
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = desc.Format;
    clear.Color[3] = 1.0f;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    for (UINT index = 0; index < 2u; ++index)
    {
        presenter.dustScreenTextures[index].Reset();
        HRESULT result = presenter.device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
            IID_PPV_ARGS(&presenter.dustScreenTextures[index]));
        if (FAILED(result))
        {
            logFailure("CreateCommittedResource(screen dust)", result);
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            presenter.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(frameBufferCount + 1u + index) *
                   presenter.rtvDescriptorSize;
        presenter.device->CreateRenderTargetView(
            presenter.dustScreenTextures[index].Get(), nullptr, rtv);
        presenter.dustScreenShaderReadable[index] = true;
    }
    presenter.dustScreenWidth = width;
    presenter.dustScreenHeight = height;
    presenter.dustScreenWriteIndex = 0;
    presenter.dustScreenHistoryValid = false;
    std::fprintf(stderr,
        "[ModernGraphics] Half-resolution FP16 screen-space dust: %ux%u.\n",
        width, height);
    return true;
}

bool createSizeDependentResources(UINT width, UINT height)
{
    width = width == 0 ? 1 : width;
    height = height == 0 ? 1 : height;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        presenter.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < frameBufferCount; ++index)
    {
        HRESULT result = presenter.swapchain->GetBuffer(
            index, IID_PPV_ARGS(&presenter.backBuffers[index]));
        if (FAILED(result))
        {
            logFailure("IDXGISwapChain::GetBuffer", result);
            return false;
        }
        presenter.device->CreateRenderTargetView(
            presenter.backBuffers[index].Get(), nullptr, rtv);
        rtv.ptr += presenter.rtvDescriptorSize;
    }

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC texture = {};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = width;
    texture.Height = height;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = sceneFormat;
    texture.SampleDesc.Count = 1;
    texture.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const D3D12_RESOURCE_STATES compatibilityInitialState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
#else
    const D3D12_RESOURCE_STATES compatibilityInitialState =
        D3D12_RESOURCE_STATE_COPY_DEST;
#endif

    HRESULT result = presenter.device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texture,
        compatibilityInitialState, nullptr,
        IID_PPV_ARGS(&presenter.sourceTexture));
    if (FAILED(result))
    {
        logFailure("CreateCommittedResource(presenter texture)", result);
        return false;
    }
    #ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    presenter.sourceTexture->SetName(L"Homeworld native D3D12 raster frame");
#else
    presenter.sourceTexture->SetName(L"Homeworld OpenGL compatibility frame");
#endif

    result = presenter.device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texture,
        compatibilityInitialState, nullptr,
        IID_PPV_ARGS(&presenter.worldTexture));
    if (FAILED(result))
    {
        logFailure("CreateCommittedResource(world texture)", result);
        return false;
    }
    #ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    presenter.worldTexture->SetName(L"Homeworld native D3D12 world-only frame");
#else
    presenter.worldTexture->SetName(L"Homeworld OpenGL world-only frame");
#endif

    result = presenter.device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texture,
        compatibilityInitialState, nullptr,
        IID_PPV_ARGS(&presenter.backgroundTexture));
    if (FAILED(result))
    {
        logFailure("CreateCommittedResource(background texture)", result);
        return false;
    }
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    presenter.backgroundTexture->SetName(L"Homeworld native D3D12 background-only frame");
#else
    presenter.backgroundTexture->SetName(L"Homeworld OpenGL background-only frame");
#endif

    result = presenter.device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texture,
        compatibilityInitialState, nullptr,
        IID_PPV_ARGS(&presenter.occluderTexture));
    if (FAILED(result))
    {
        logFailure("CreateCommittedResource(occluder texture)", result);
        return false;
    }
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    presenter.occluderTexture->SetName(L"Homeworld native D3D12 pre-FX god-ray occluder frame");
#else
    presenter.occluderTexture->SetName(L"Homeworld OpenGL pre-FX god-ray occluder frame");
#endif

#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    texture.Flags = D3D12_RESOURCE_FLAG_NONE;
#endif

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = sceneFormat;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    presenter.device->CreateShaderResourceView(
        presenter.sourceTexture.Get(), &srv,
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_CPU_DESCRIPTOR_HANDLE worldSrv =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    worldSrv.ptr += static_cast<SIZE_T>(2) * presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(
        presenter.worldTexture.Get(), &srv, worldSrv);

    D3D12_CPU_DESCRIPTOR_HANDLE backgroundSrv =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    backgroundSrv.ptr += static_cast<SIZE_T>(10) *
                         presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(
        presenter.backgroundTexture.Get(), &srv, backgroundSrv);

    D3D12_CPU_DESCRIPTOR_HANDLE rayEnvironmentSrv =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    rayEnvironmentSrv.ptr +=
        static_cast<SIZE_T>(rayEnvironmentDescriptorIndex) *
        presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(
        presenter.backgroundTexture.Get(), &srv, rayEnvironmentSrv);

    /* t6 belonged to the retired interpolation history path. Keep a valid
       null SRV in that descriptor slot so the existing t4-t8 root range stays
       layout-compatible without allocating a full-resolution history image. */
    D3D12_CPU_DESCRIPTOR_HANDLE retiredHistorySrv =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    retiredHistorySrv.ptr += static_cast<SIZE_T>(11) *
                             presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(nullptr, &srv, retiredHistorySrv);

    D3D12_CPU_DESCRIPTOR_HANDLE occluderSrv =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    occluderSrv.ptr += static_cast<SIZE_T>(12) *
                       presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(
        presenter.occluderTexture.Get(), &srv, occluderSrv);

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSrv =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    sceneDepthSrv.ptr += static_cast<SIZE_T>(sceneDepthDescriptorIndex) *
                         presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(nullptr, &depthSrv, sceneDepthSrv);

    /* t3 falls back to the unfiltered world. A successfully created DLAA
       output replaces this descriptor without changing the presenter table. */
    D3D12_CPU_DESCRIPTOR_HANDLE filteredWorldSrv =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    filteredWorldSrv.ptr += static_cast<SIZE_T>(3) *
                            presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(
        presenter.worldTexture.Get(), &srv, filteredWorldSrv);

    /* The presenter shader always declares t1 even when its runtime branch is
       disabled. Keep that descriptor valid on non-DXR hardware and after a
       runtime disable; the real ray output overwrites this null descriptor. */
    D3D12_CPU_DESCRIPTOR_HANDLE raySrv =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    raySrv.ptr += presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(nullptr, &srv, raySrv);

    D3D12_SHADER_RESOURCE_VIEW_DESC motionSrv = {};
    motionSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    motionSrv.Format = DXGI_FORMAT_R16G16_FLOAT;
    motionSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    motionSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE motionHandle =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    motionHandle.ptr += static_cast<SIZE_T>(9) * presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(nullptr, &motionSrv,
                                                motionHandle);

#if defined(HW_ENABLE_D3D12_BACKEND)
    UINT sceneWidth = width;
    UINT sceneHeight = height;
    raytracingDimensionsForAntiAliasingMode(
        requestedAntiAliasingMode, width, height, sceneWidth, sceneHeight);
    if (hwmodern::raytracingInitialized())
    {
        if (!hwmodern::raytracingCreateOutput(sceneWidth, sceneHeight))
        {
            std::fprintf(stderr, "[ModernGraphics] DXR output unavailable; "
                                 "continuing with D3D12 raster presentation.\n");
        }
        else if (sceneWidth < width || sceneHeight < height)
        {
            std::fprintf(stderr,
                         "[ModernGraphics] Full-scene DLSS input: %ux%u "
                         "-> %ux%u display (DXR + dust + scene post).\n",
                         sceneWidth, sceneHeight, width, height);
        }
    }

    /* The old integration sent only the path-lighting buffer to DLSS while
       the real raster scene and dust remained native resolution. Build a
       complete HUD-less scene at the same dimensions as depth/motion instead. */
    if (!createSceneInputTexture(sceneWidth, sceneHeight))
    {
        return false;
    }
    if (!createDustFroxelTexture(sceneWidth, sceneHeight))
    {
        std::fprintf(stderr,
            "[ModernGraphics] Shared dust froxel field unavailable; "
            "falling back to direct boundaryless raymarch.\n");
    }
    if (!createDustScreenTextures(sceneWidth, sceneHeight))
        return false;

    hwmodern::upscalerCreateOutput(
        presenter.srvHeap.Get(), presenter.srvDescriptorSize, 3,
        width, height);
#endif

#ifndef HW_ENABLE_D3D12_NATIVE_RASTER
    UINT64 totalSize = 0;
    presenter.device->GetCopyableFootprints(
        &texture, 0, 1, 0, &presenter.uploadFootprint,
        nullptr, nullptr, &totalSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC upload = {};
    upload.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload.Width = totalSize;
    upload.Height = 1;
    upload.DepthOrArraySize = 1;
    upload.MipLevels = 1;
    upload.Format = DXGI_FORMAT_UNKNOWN;
    upload.SampleDesc.Count = 1;
    upload.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    for (D3D12FrameContext &frame : presenter.frames)
    {
        result = presenter.device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &upload,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&frame.upload));
        if (FAILED(result))
        {
            logFailure("CreateCommittedResource(frame upload)", result);
            return false;
        }
        void *mapped = nullptr;
        D3D12_RANGE noRead = { 0, 0 };
        result = frame.upload->Map(0, &noRead, &mapped);
        if (FAILED(result))
        {
            logFailure("ID3D12Resource::Map(frame upload)", result);
            return false;
        }
        frame.mappedUpload = static_cast<unsigned char *>(mapped);

        result = presenter.device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &upload,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&frame.worldUpload));
        if (FAILED(result))
        {
            logFailure("CreateCommittedResource(world upload)", result);
            return false;
        }
        mapped = nullptr;
        result = frame.worldUpload->Map(0, &noRead, &mapped);
        if (FAILED(result))
        {
            logFailure("ID3D12Resource::Map(world upload)", result);
            return false;
        }
        frame.mappedWorldUpload = static_cast<unsigned char *>(mapped);

        result = presenter.device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &upload,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&frame.backgroundUpload));
        if (FAILED(result))
        {
            logFailure("CreateCommittedResource(background upload)", result);
            return false;
        }
        mapped = nullptr;
        result = frame.backgroundUpload->Map(0, &noRead, &mapped);
        if (FAILED(result))
        {
            logFailure("ID3D12Resource::Map(background upload)", result);
            return false;
        }
        frame.mappedBackgroundUpload = static_cast<unsigned char *>(mapped);

        result = presenter.device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &upload,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&frame.occluderUpload));
        if (FAILED(result))
        {
            logFailure("CreateCommittedResource(occluder upload)", result);
            return false;
        }
        mapped = nullptr;
        result = frame.occluderUpload->Map(0, &noRead, &mapped);
        if (FAILED(result))
        {
            logFailure("ID3D12Resource::Map(occluder upload)", result);
            return false;
        }
        frame.mappedOccluderUpload = static_cast<unsigned char *>(mapped);
        frame.uploadSize = totalSize;
    }
#else
    presenter.uploadFootprint = {};
#endif

    presenter.width = width;
    presenter.height = height;
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    presenter.sourceTextureShaderReadable = true;
    presenter.worldTextureShaderReadable = true;
    presenter.backgroundTextureShaderReadable = true;
    presenter.occluderTextureShaderReadable = true;
#else
    presenter.sourceTextureShaderReadable = false;
    presenter.worldTextureShaderReadable = false;
    presenter.backgroundTextureShaderReadable = false;
    presenter.occluderTextureShaderReadable = false;
#endif
    return true;
}

bool resizePresenter(UINT width, UINT height)
{
    if (!waitForSubmittedWork())
    {
        return false;
    }
    releaseSizeDependentResources();
    const UINT flags = presenter.allowTearing ?
        DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT result = presenter.swapchain->ResizeBuffers(
        frameBufferCount, width, height, presenterFormat, flags);
    if (FAILED(result))
    {
        logFailure("IDXGISwapChain::ResizeBuffers", result);
        return false;
    }
    if (!createSizeDependentResources(width, height))
    {
        return false;
    }
    std::fprintf(stderr, "[ModernGraphics] Resized D3D12 visible presenter to "
                         "%ux%u.\n", width, height);
    return true;
}

void transition(ID3D12GraphicsCommandList *commands, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commands->ResourceBarrier(1, &barrier);
}
}

extern "C" int hwModernGraphicsQueryCapabilities(
    HWModernGraphicsCapabilities *capabilities)
{
    if (capabilities == nullptr)
    {
        return 0;
    }
    std::memset(capabilities, 0, sizeof(*capabilities));
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
    {
        return 0;
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(selectHardwareAdapter(factory.Get(), adapter)))
    {
        return 0;
    }
    DXGI_ADAPTER_DESC1 description = {};
    ComPtr<ID3D12Device> device;
    if (FAILED(adapter->GetDesc1(&description)) ||
        FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device))))
    {
        return 0;
    }

    capabilities->d3d12Available = 1;
    capabilities->hardwareAdapter = 1;
    capabilities->vendorId = description.VendorId;
    capabilities->deviceId = description.DeviceId;
    capabilities->dedicatedVideoMemoryBytes =
        static_cast<unsigned long long>(description.DedicatedVideoMemory);
    capabilities->maximumFeatureLevel = queryMaximumFeatureLevel(device.Get());
    copyAdapterName(description.Description, capabilities->adapterName,
                    sizeof(capabilities->adapterName));

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                              &options5, sizeof(options5))))
    {
        if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1)
            capabilities->raytracingTier = HW_MODERN_RAYTRACING_TIER_1_1;
        else if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
            capabilities->raytracingTier = HW_MODERN_RAYTRACING_TIER_1_0;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6,
                                              &options6, sizeof(options6))))
    {
        capabilities->variableRateShadingTier =
            static_cast<unsigned int>(options6.VariableShadingRateTier);
        capabilities->additionalShadingRatesSupported =
            options6.AdditionalShadingRatesSupported ? 1 : 0;
    }
    return 1;
}

extern "C" void hwModernGraphicsLogCapabilities(void)
{
    HWModernGraphicsCapabilities capabilities = {};
    if (!hwModernGraphicsQueryCapabilities(&capabilities))
    {
        std::fprintf(stderr, "[ModernGraphics] No hardware D3D12 adapter is "
                             "available.\n");
        return;
    }
    const unsigned long long videoMemoryMiB =
        capabilities.dedicatedVideoMemoryBytes / (1024ull * 1024ull);
    const unsigned int featureMajor =
        (capabilities.maximumFeatureLevel >> 12u) & 0xfu;
    const unsigned int featureMinor =
        (capabilities.maximumFeatureLevel >> 8u) & 0xfu;
    std::fprintf(stderr,
                 "[ModernGraphics] GPU: %s (vendor %04x, device %04x, %llu MiB VRAM)\n",
                 capabilities.adapterName, capabilities.vendorId,
                 capabilities.deviceId, videoMemoryMiB);
    std::fprintf(stderr,
                 "[ModernGraphics] D3D12 feature level %u_%u; DXR tier %s; "
                 "VRS tier %u%s.\n",
                 featureMajor, featureMinor,
                 raytracingTierName(capabilities.raytracingTier),
                 capabilities.variableRateShadingTier,
                 capabilities.additionalShadingRatesSupported ?
                    " with additional shading rates" : "");
}

extern "C" int hwModernGraphicsInitialize(void *nativeWindow,
                                            unsigned int width,
                                            unsigned int height)
{
    hwmodern::dlaaEarlyInitialize();
    hwModernGraphicsShutdown();
    presenter.window = static_cast<HWND>(nativeWindow);
    if (presenter.window == nullptr)
    {
        std::fprintf(stderr, "[ModernGraphics] Cannot create a visible D3D12 "
                             "presenter without a Win32 window.\n");
        return 0;
    }

    hwmodern::dlaaSetMode(requestedAntiAliasingMode);
    hwmodern::dlaaSetRayReconstructionEnabled(requestedRayReconstruction);

    HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&presenter.factory));
    if (FAILED(result) ||
        FAILED(result = selectHardwareAdapter(presenter.factory.Get(),
                                               presenter.adapter)) ||
        FAILED(result = D3D12CreateDevice(presenter.adapter.Get(),
                                          D3D_FEATURE_LEVEL_11_0,
                                          IID_PPV_ARGS(&presenter.device))))
    {
        logFailure("initializing D3D12", result);
        releasePresenter();
        return 0;
    }
    hwmodern::dlaaSetDevice(presenter.device.Get(), presenter.adapter.Get());
    hwmodern::upscalerSetDevice(presenter.device.Get(), presenter.adapter.Get());
    hwmodern::upscalerSetRequestedMode(requestedAntiAliasingMode);

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 raytracingOptions = {};
    if (SUCCEEDED(presenter.device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5, &raytracingOptions,
            sizeof(raytracingOptions))) &&
        raytracingOptions.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
    {
        presenter.device.As(&presenter.raytracingDevice);
    }

    D3D12_COMMAND_QUEUE_DESC queueDescription = {};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    result = presenter.device->CreateCommandQueue(
        &queueDescription, IID_PPV_ARGS(&presenter.queue));
    if (FAILED(result))
    {
        logFailure("ID3D12Device::CreateCommandQueue", result);
        releasePresenter();
        return 0;
    }

    /* Permanent low-overhead GPU pass timing.  Four timestamps per flight
       slot delimit native raster, DXR/AA, and the final presenter. */
    if (FAILED(presenter.queue->GetTimestampFrequency(&presenter.timestampFrequency)))
        presenter.timestampFrequency = 0;
    D3D12_QUERY_HEAP_DESC timestampHeap = {};
    timestampHeap.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    timestampHeap.Count = frameBufferCount * 4u;
    result = presenter.device->CreateQueryHeap(
        &timestampHeap, IID_PPV_ARGS(&presenter.timestampQueryHeap));
    if (FAILED(result))
    {
        logFailure("CreateQueryHeap(GPU timestamps)", result);
        presenter.timestampQueryHeap.Reset();
        presenter.timestampFrequency = 0;
    }

    const UINT safeWidth = width == 0 ? 1 : static_cast<UINT>(width);
    const UINT safeHeight = height == 0 ? 1 : static_cast<UINT>(height);
    if (!createSwapchain(safeWidth, safeHeight))
    {
        releasePresenter();
        return 0;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeap = {};
    rtvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeap.NumDescriptors = frameBufferCount + 3u;
    result = presenter.device->CreateDescriptorHeap(
        &rtvHeap, IID_PPV_ARGS(&presenter.rtvHeap));
    if (FAILED(result))
    {
        logFailure("CreateDescriptorHeap(RTV)", result);
        releasePresenter();
        return 0;
    }
    presenter.rtvDescriptorSize = presenter.device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC srvHeap = {};
    srvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    /* t0-t3 presenter color, u0-u4 DXR outputs/history, t4 motion,
       t5 background-only environment, t6 frame-generation history,
       t7 pre-FX god-ray occluders, t8 exact native raster depth, and
       per-frame t9/t10/t11 structured buffers for volumetric dust/lights/wakes. */
    srvHeap.NumDescriptors = presenterDescriptorCount;
    srvHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    result = presenter.device->CreateDescriptorHeap(
        &srvHeap, IID_PPV_ARGS(&presenter.srvHeap));
    if (FAILED(result))
    {
        logFailure("CreateDescriptorHeap(SRV)", result);
        releasePresenter();
        return 0;
    }
    presenter.srvDescriptorSize =
        presenter.device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!createVolumetricDustFrameResources())
    {
        releasePresenter();
        return 0;
    }

    for (D3D12FrameContext &frame : presenter.frames)
    {
        result = presenter.device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator));
        if (FAILED(result))
        {
            logFailure("CreateCommandAllocator", result);
            releasePresenter();
            return 0;
        }

        if (presenter.timestampQueryHeap)
        {
            D3D12_HEAP_PROPERTIES heap = {};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            heap.CreationNodeMask = 1;
            heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC buffer = {};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = sizeof(UINT64) * 4u;
            buffer.Height = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            result = presenter.device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&frame.timestampReadback));
            if (FAILED(result))
            {
                logFailure("CreateCommittedResource(GPU timestamp readback)", result);
                presenter.timestampQueryHeap.Reset();
                presenter.timestampFrequency = 0;
                for (D3D12FrameContext &other : presenter.frames)
                {
                    other.timestampReadback.Reset();
                    other.timestampPending = false;
                }
                continue;
            }
        }
    }
    result = presenter.device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        presenter.frames[0].allocator.Get(), nullptr,
        IID_PPV_ARGS(&presenter.commandList));
    if (FAILED(result) || FAILED(result = presenter.commandList->Close()))
    {
        logFailure("CreateCommandList", result);
        releasePresenter();
        return 0;
    }
    result = presenter.device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&presenter.fence));
    if (FAILED(result))
    {
        logFailure("CreateFence", result);
        releasePresenter();
        return 0;
    }
    presenter.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (presenter.fenceEvent == nullptr)
    {
        std::fprintf(stderr, "[ModernGraphics] CreateEventW failed "
                             "(Win32 error %lu).\n", GetLastError());
        releasePresenter();
        return 0;
    }

    if (!createPresenterPipeline())
    {
        releasePresenter();
        return 0;
    }
    if (!createDustFroxelPipeline())
    {
        std::fprintf(stderr,
            "[ModernGraphics] Dust froxel compute pipeline unavailable; "
            "direct boundaryless raymarch remains available.\n");
        presenter.dustFroxelPipeline.Reset();
        presenter.dustFroxelRootSignature.Reset();
    }
#if defined(HW_ENABLE_D3D12_BACKEND)
    if (raytracingRequested && presenter.raytracingDevice &&
        !hwmodern::raytracingInitialize(
            presenter.device.Get(), presenter.srvHeap.Get(),
            presenter.srvDescriptorSize, 1, 4,
            rayEnvironmentDescriptorIndex, rrGuideUavDescriptorBase))
    {
        std::fprintf(stderr, "[ModernGraphics] DXR pipeline initialization "
                             "failed; D3D12 presentation remains active.\n");
    }
    hwmodern::raytracingSetFxLightingStrength(
        requestedFxLightingStrength);
    hwmodern::raytracingSetPathSettings(
        requestedPathSamples, requestedPathBounces,
        requestedNormalStrength);
    hwmodern::raytracingSetRayReconstructionEnabled(
        requestedRayReconstruction &&
        requestedAntiAliasingMode >= HW_MODERN_AA_DLSS_QUALITY &&
        requestedAntiAliasingMode <= HW_MODERN_AA_DLSS_ULTRA_PERFORMANCE &&
        hwmodern::dlaaRayReconstructionAvailable());
    hwmodern::raytracingSetLightingTuning(
        requestedEnvironmentStrength, requestedAuthoredLightStrength,
        requestedSurfaceReflectivity, requestedSurfaceRoughness,
        requestedLightingExposure);
    hwmodern::raytracingSetSkyLightingStrengths(
        requestedPrimarySunStrength, requestedSkyAmbientStrength);
#endif
    if (
        !createSizeDependentResources(safeWidth, safeHeight))
    {
        releasePresenter();
        return 0;
    }

    presenter.active = true;
    presenter.visiblePresenter = true;
    std::fprintf(stderr,
                 "[ModernGraphics] D3D12 visible presenter active: %ux%u "
                 "DXGI flip-discard, triple-buffered%s%s.\n",
                 presenter.width, presenter.height,
                 presenter.allowTearing ? ", tearing available" : "",
                 presenter.raytracingDevice ? ", DXR command interface ready" : "");
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    std::fprintf(stderr,
                 "[ModernGraphics] Native D3D12 raster active: legacy fixed-function "
                 "draw vocabulary -> D3D12 command recording -> DXGI present; no "
                 "OpenGL context or framebuffer capture is used.\n");
#else
    std::fprintf(stderr,
                 "[ModernGraphics] Compatibility capture: OpenGL scene -> "
                 "D3D12 upload -> DXGI present. This transfer is transitional; "
                 "native mesh migration remains in progress.\n");
#endif
#if defined(HW_ENABLE_D3D12_BACKEND)
    if (hwmodern::raytracingActive())
    {
        std::fprintf(stderr, "[ModernGraphics] DXR path tracing enabled; "
                             "BLAS/TLAS construction begins with the first "
                             "gameplay frame.\n");
    }
#endif
    return 1;
}

extern "C" void hwModernGraphicsBeginFrame(unsigned int width,
                                             unsigned int height)
{
    const auto cpuFrameStarted = std::chrono::steady_clock::now();
    /* Render callbacks repopulate this from the same transforms used to draw
       the visible legacy frame.  Clearing here prevents deleted effects from
       leaving stale lights in the DXR scene. */
    dynamicLightEmitters.clear();
    volumetricDustInteractors.clear();
    presenter.dustCameraExplicitThisFrame = false;
    presenter.nativeRasterDepthSampleReady = false;
    presenter.dustFroxelValidThisFrame = false;
    presenter.godRaySourceExternalThisFrame = false;
    presenter.godRaySourceExternalVisible = false;
    presenter.missionSkyTextureName = 0;
    presenter.missionSkyFade = 1.0f;
    const DynamicLightClock::time_point now = DynamicLightClock::now();
    size_t retainedCount = 0;
    for (size_t index = 0; index < timedDynamicLightEmitters.size(); ++index)
    {
        TimedDynamicLight retained = timedDynamicLightEmitters[index];
        const float elapsedSeconds =
            std::chrono::duration<float>(now - retained.started).count();
        if (elapsedSeconds >= retained.lifetimeSeconds)
        {
            continue;
        }

        const float remaining = 1.0f -
            elapsedSeconds / retained.lifetimeSeconds;
        HWModernDynamicLightEmitter frameEmitter = retained.emitter;
        /* A squared envelope gives the flash a hard initial strike and a
           short photographic falloff without a single-frame discontinuity. */
        frameEmitter.intensity *= remaining * remaining;
        dynamicLightEmitters.push_back(frameEmitter);
        timedDynamicLightEmitters[retainedCount++] = retained;
    }
    timedDynamicLightEmitters.resize(retainedCount);
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingBeginFrame();
#endif

    if (!presenter.active || presenter.frameOpen)
    {
        return;
    }
    const UINT requestedWidth = width == 0 ? 1 : static_cast<UINT>(width);
    const UINT requestedHeight = height == 0 ? 1 : static_cast<UINT>(height);
    if (requestedWidth != presenter.width || requestedHeight != presenter.height)
    {
        if (!resizePresenter(requestedWidth, requestedHeight))
        {
            presenter.active = false;
            presenter.visiblePresenter = false;
            return;
        }
    }

    presenter.activeFrameIndex = presenter.swapchain->GetCurrentBackBufferIndex();
    D3D12FrameContext &frame = presenter.frames[presenter.activeFrameIndex];
    /* Every back-buffer slot now owns its DXR TLAS and upload resources. Wait
       only for this slot instead of draining the entire GPU every frame. */
    if (!waitForFenceValue(frame.fenceValue))
    {
        presenter.active = false;
        presenter.visiblePresenter = false;
        return;
    }

    /* Consume this slot's previous GPU timestamps after the fence we already
       had to wait for. Profiling therefore adds no frame drain or query wait. */
    if (frame.timestampPending && frame.timestampReadback &&
        presenter.timestampFrequency != 0)
    {
        UINT64 *ticks = nullptr;
        D3D12_RANGE readRange = {0, sizeof(UINT64) * 4u};
        if (SUCCEEDED(frame.timestampReadback->Map(
                0, &readRange, reinterpret_cast<void **>(&ticks))) && ticks)
        {
            const double millisecondsPerTick =
                1000.0 / static_cast<double>(presenter.timestampFrequency);
            if (ticks[1] >= ticks[0] && ticks[2] >= ticks[1] &&
                ticks[3] >= ticks[2])
            {
                presenter.gpuRasterMilliseconds +=
                    static_cast<double>(ticks[1] - ticks[0]) * millisecondsPerTick;
                presenter.gpuDxrMilliseconds +=
                    static_cast<double>(ticks[2] - ticks[1]) * millisecondsPerTick;
                presenter.gpuPresenterMilliseconds +=
                    static_cast<double>(ticks[3] - ticks[2]) * millisecondsPerTick;
                presenter.gpuFrameMilliseconds +=
                    static_cast<double>(ticks[3] - ticks[0]) * millisecondsPerTick;
                ++presenter.gpuTimestampFrames;
            }
            D3D12_RANGE noWrite = {0, 0};
            frame.timestampReadback->Unmap(0, &noWrite);
        }
        frame.timestampPending = false;
    }
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetFrameSlot(presenter.activeFrameIndex);
#endif
    presenter.frameOpen = true;
    presenter.cpuFrameStarted = cpuFrameStarted;
    presenter.cpuFrameTimerActive = true;
    presenter.frameCaptured = false;
    presenter.worldFrameCaptured = false;
    presenter.backgroundFrameCaptured = false;
    presenter.occluderFrameCaptured = false;
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    hwglBeginFrame(presenter.width, presenter.height, presenter.activeFrameIndex);
#endif
}

extern "C" void hwModernGraphicsSetGodRayWorldSource(
    int mapIdentity, int sourceValid, const float direction[3])
{
    const bool mapChanged = presenter.godRayMapIdentity != mapIdentity;
    float normalized[3];
    float length;
    bool directionChanged;
    if (mapChanged)
    {
        resetGodRaySource(mapIdentity);
    }
    if (!sourceValid || direction == nullptr)
    {
        /* A transient missing backdrop sample must not unlock an emitter that
           already belongs to this mission. Only a real mapIdentity change
           resets the lock. */
        if (!presenter.godRaySourceLocked)
        {
            presenter.godRaySourceVisible = false;
        }
        return;
    }
    length = std::sqrt(direction[0] * direction[0] +
                       direction[1] * direction[1] +
                       direction[2] * direction[2]);
    if (length <= 0.000001f)
    {
        presenter.godRaySourceVisible = false;
        return;
    }
    normalized[0] = direction[0] / length;
    normalized[1] = direction[1] / length;
    normalized[2] = direction[2] / length;
    directionChanged = !presenter.godRaySourceLocked ||
        std::fabs(presenter.godRayWorldDirection[0] - normalized[0]) > 0.0001f ||
        std::fabs(presenter.godRayWorldDirection[1] - normalized[1]) > 0.0001f ||
        std::fabs(presenter.godRayWorldDirection[2] - normalized[2]) > 0.0001f;
    if (directionChanged)
    {
        presenter.godRayWorldDirection[0] = normalized[0];
        presenter.godRayWorldDirection[1] = normalized[1];
        presenter.godRayWorldDirection[2] = normalized[2];
        presenter.godRaySourceLocked = true;
        std::fprintf(stderr,
            "[ModernGraphics] God-ray source %s for map %d at direction "
            "(%.4f, %.4f, %.4f).\n",
            mapChanged ? "bound" : "updated",
            mapIdentity,
            presenter.godRayWorldDirection[0],
            presenter.godRayWorldDirection[1],
            presenter.godRayWorldDirection[2]);
    }
}

extern "C" void hwModernGraphicsSetMissionSkyLighting(
    int mapIdentity, int sourceValid, const float direction[3],
    const float sourceColor[3], const float ambientColor[3])
{
    /* Keep the visual shaft source, surface key and volumetric key tied to
       exactly the same mission-owned sky object. */
    hwModernGraphicsSetGodRayWorldSource(
        mapIdentity, sourceValid, direction);
    dustMissionSkyAvailable = ambientColor != nullptr;
    if (ambientColor != nullptr)
    {
        for (unsigned int channel = 0; channel < 3; ++channel)
            dustMissionAmbientColor[channel] =
                std::max(0.0f, std::min(ambientColor[channel], 16.0f));
    }
    dustMissionSunValid = sourceValid != 0 && direction != nullptr && sourceColor != nullptr;
    if (dustMissionSunValid)
    {
        const float length = std::sqrt(direction[0]*direction[0] +
                                       direction[1]*direction[1] +
                                       direction[2]*direction[2]);
        if (length > 0.000001f)
        {
            for (unsigned int channel = 0; channel < 3; ++channel)
            {
                dustMissionSunDirection[channel] = direction[channel] / length;
                dustMissionSunColor[channel] =
                    std::max(0.0f, std::min(sourceColor[channel], 16.0f));
            }
        }
        else
        {
            dustMissionSunValid = false;
        }
    }
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetMissionSkyLighting(
        sourceValid, direction, sourceColor, ambientColor);
#else
    (void)sourceColor;
    (void)ambientColor;
#endif
}

extern "C" void hwModernGraphicsSetVolumetricDustCamera(
    const float eye[3], const float rayUv00[3], const float rayUv10[3],
    const float rayUv01[3])
{
    if (eye == nullptr || rayUv00 == nullptr || rayUv10 == nullptr ||
        rayUv01 == nullptr) return;
    const float *rays[3] = {rayUv00, rayUv10, rayUv01};
    for (unsigned int rayIndex = 0; rayIndex < 3; ++rayIndex)
    {
        const float lengthSquared =
            rays[rayIndex][0] * rays[rayIndex][0] +
            rays[rayIndex][1] * rays[rayIndex][1] +
            rays[rayIndex][2] * rays[rayIndex][2];
        if (!(lengthSquared > 1.0e-10f) || !std::isfinite(lengthSquared))
            return;
    }
    for (unsigned int channel = 0; channel < 3; ++channel)
    {
        presenter.dustCameraPosition[channel] = eye[channel];
        presenter.dustRayUv00[channel] = rayUv00[channel];
        presenter.dustRayUv10[channel] = rayUv10[channel];
        presenter.dustRayUv01[channel] = rayUv01[channel];
    }
    presenter.dustCameraValid = true;
    presenter.dustCameraExplicitThisFrame = true;
}

extern "C" int hwModernGraphicsCaptureOpenGLBackground(
    int mapIdentity, float verticalFieldOfViewDegrees, float aspectRatio,
    const float viewMatrix[16], const float projectionMatrix[16])
{
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    if (!presenter.active || !presenter.visiblePresenter || !presenter.frameOpen)
    {
        return 0;
    }
    updateDustDepthProjection(projectionMatrix);
    /* The view matrix passed here is the matrix that actually rendered the
       world snapshot.  Make it authoritative for volumetrics every frame so
       editor/fleet camera callbacks cannot rotate the medium in screen space. */
    if (viewMatrix == nullptr ||
        !updateDustCameraFromRenderView(verticalFieldOfViewDegrees,
                                        aspectRatio, viewMatrix))
    {
        presenter.dustCameraValid = false;
        presenter.dustCameraExplicitThisFrame = false;
    }
#if defined(HW_ENABLE_D3D12_BACKEND)
    if (!hwmodern::raytracingInitialized() &&
        requestedGodRays <= 0.0001f && requestedBloom <= 0.0001f &&
        volumetricDustVolumes.empty())
    {
        return 0;
    }
#else
    if (requestedGodRays <= 0.0001f && requestedBloom <= 0.0001f &&
        volumetricDustVolumes.empty()) return 0;
#endif
    hwglMarkSnapshot(HW_LEGACY_RASTER_BACKGROUND);
    if (presenter.godRayMapIdentity != mapIdentity)
    {
        resetGodRaySource(mapIdentity);
    }
    presenter.godRayLastBackgroundFrame = presenter.frameCount;
    if (!presenter.godRaySourceExternalThisFrame)
    {
        if (presenter.godRaySourceLocked)
            projectLockedGodRaySource(verticalFieldOfViewDegrees, aspectRatio, viewMatrix);
        else
            presenter.godRaySourceVisible = false;
    }
    presenter.backgroundFrameCaptured = true;
    return 1;
#else
    if (!presenter.active || !presenter.visiblePresenter ||
        !presenter.frameOpen)
    {
        return 0;
    }
    updateDustDepthProjection(projectionMatrix);
    /* The view matrix passed here is the matrix that actually rendered the
       world snapshot.  Make it authoritative for volumetrics every frame so
       editor/fleet camera callbacks cannot rotate the medium in screen space. */
    if (viewMatrix == nullptr ||
        !updateDustCameraFromRenderView(verticalFieldOfViewDegrees,
                                        aspectRatio, viewMatrix))
    {
        presenter.dustCameraValid = false;
        presenter.dustCameraExplicitThisFrame = false;
    }
#if defined(HW_ENABLE_D3D12_BACKEND)
    /* The environment frame is consumed by DXR, post-FX and the volumetric
       media pass.  Avoid the synchronous readback only when none needs it. */
    if (!hwmodern::raytracingInitialized() &&
        requestedGodRays <= 0.0001f && requestedBloom <= 0.0001f &&
        volumetricDustVolumes.empty())
    {
        return 0;
    }
#else
    if (requestedGodRays <= 0.0001f && requestedBloom <= 0.0001f &&
        volumetricDustVolumes.empty())
    {
        return 0;
    }
#endif
    D3D12FrameContext &frame = presenter.frames[presenter.activeFrameIndex];
    if (frame.mappedBackgroundUpload == nullptr)
    {
        return 0;
    }

    const auto readbackStarted = std::chrono::steady_clock::now();
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH,
                  static_cast<GLint>(presenter.uploadFootprint.Footprint.RowPitch / 4));
    glReadPixels(0, 0, static_cast<GLsizei>(presenter.width),
                 static_cast<GLsizei>(presenter.height),
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 frame.mappedBackgroundUpload + presenter.uploadFootprint.Offset);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (glGetError() != GL_NO_ERROR)
    {
        return 0;
    }
    presenter.backgroundReadbackMilliseconds +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - readbackStarted).count();
    ++presenter.backgroundReadbackCount;
    /* The mission backdrop owns source selection. Camera/focus changes must
       never trigger a new search or swap to another star. */
    if (presenter.godRayMapIdentity != mapIdentity)
    {
        resetGodRaySource(mapIdentity);
    }
    presenter.godRayLastBackgroundFrame = presenter.frameCount;
    if (presenter.godRaySourceExternalThisFrame)
    {
        /* ModernMissionBackdrop already projected the exact authored source
           using the live OpenGL matrices.  Keep that screen-space position,
           including modest off-screen coordinates, instead of overwriting it
           with the legacy in-viewport projection gate below. */
    }
    else if (presenter.godRaySourceLocked)
    {
        projectLockedGodRaySource(
            verticalFieldOfViewDegrees, aspectRatio, viewMatrix);
    }
    else
    {
        presenter.godRaySourceVisible = false;
    }
    presenter.backgroundFrameCaptured = true;
    return 1;
#endif
}

extern "C" int hwModernGraphicsCaptureOpenGLOccluders(void)
{
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    if (!presenter.active || !presenter.visiblePresenter || !presenter.frameOpen ||
        requestedGodRays <= 0.0001f)
    {
        return 0;
    }
    hwglMarkSnapshot(HW_LEGACY_RASTER_OCCLUDERS);
    presenter.occluderFrameCaptured = true;
    return 1;
#else
    if (!presenter.active || !presenter.visiblePresenter ||
        !presenter.frameOpen || requestedGodRays <= 0.0001f)
    {
        return 0;
    }
    D3D12FrameContext &frame = presenter.frames[presenter.activeFrameIndex];
    if (frame.mappedOccluderUpload == nullptr)
    {
        return 0;
    }

    /* Capture after planets/world geometry but before the render-list pass.
       This gives the shaft shader a raster occluder image that deliberately
       excludes ship engine trails, beams, particles and other transient FX.
       Opaque ships and ordinary derelicts are supplied by the DXR hit mask. */
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH,
                  static_cast<GLint>(presenter.uploadFootprint.Footprint.RowPitch / 4));
    glReadPixels(0, 0, static_cast<GLsizei>(presenter.width),
                 static_cast<GLsizei>(presenter.height),
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 frame.mappedOccluderUpload + presenter.uploadFootprint.Offset);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (glGetError() != GL_NO_ERROR)
    {
        return 0;
    }
    presenter.occluderFrameCaptured = true;
    return 1;
#endif
}

extern "C" int hwModernGraphicsCaptureOpenGLWorld(void)
{
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    if (!presenter.active || !presenter.visiblePresenter || !presenter.frameOpen)
    {
        return 0;
    }
#if defined(HW_ENABLE_D3D12_BACKEND)
    const bool dxrNeedsWorld = hwmodern::raytracingInitialized();
#else
    const bool dxrNeedsWorld = false;
#endif
    const bool postProcessingNeedsWorld =
        requestedAntiAliasingMode != HW_MODERN_AA_OFF ||
        requestedChromaticAberration > 0.0001f ||
        requestedMotionBlur > 0.0001f ||
        requestedFilmGrain > 0.0001f ||
        requestedGodRays > 0.0001f ||
        requestedBloom > 0.0001f;
    if (!dxrNeedsWorld && !postProcessingNeedsWorld) return 0;
    hwglMarkSnapshot(HW_LEGACY_RASTER_WORLD);
    presenter.worldFrameCaptured = true;
    return 1;
#else
    if (!presenter.active || !presenter.visiblePresenter ||
        !presenter.frameOpen)
    {
        return 0;
    }
#if defined(HW_ENABLE_D3D12_BACKEND)
    const bool dxrNeedsWorld = hwmodern::raytracingInitialized();
#else
    const bool dxrNeedsWorld = false;
#endif
    const bool postProcessingNeedsWorld =
        requestedAntiAliasingMode != HW_MODERN_AA_OFF ||
        requestedChromaticAberration > 0.0001f ||
        requestedMotionBlur > 0.0001f ||
        requestedFilmGrain > 0.0001f ||
        requestedGodRays > 0.0001f ||
        requestedBloom > 0.0001f;
    /* With DXR, AA and post processing all disabled, the final composite can
       be presented directly. This removes a second synchronous GL readback
       from the zero-effects path without changing image quality. */
    if (!dxrNeedsWorld && !postProcessingNeedsWorld)
    {
        return 0;
    }
    D3D12FrameContext &frame = presenter.frames[presenter.activeFrameIndex];
    if (frame.mappedWorldUpload == nullptr)
    {
        return 0;
    }

    const auto readbackStarted = std::chrono::steady_clock::now();
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH,
                  static_cast<GLint>(presenter.uploadFootprint.Footprint.RowPitch / 4));
    glReadPixels(0, 0, static_cast<GLsizei>(presenter.width),
                 static_cast<GLsizei>(presenter.height),
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 frame.mappedWorldUpload + presenter.uploadFootprint.Offset);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (glGetError() != GL_NO_ERROR)
    {
        return 0;
    }
    presenter.worldReadbackMilliseconds +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - readbackStarted).count();
    ++presenter.worldReadbackCount;
    presenter.worldFrameCaptured = true;
    return 1;
#endif
}

extern "C" int hwModernGraphicsCaptureOpenGLFrame(void)
{
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    if (!presenter.active || !presenter.visiblePresenter || !presenter.frameOpen)
    {
        return 0;
    }
    hwglMarkSnapshot(HW_LEGACY_RASTER_FINAL);
    presenter.frameCaptured = true;
    return 1;
#else
    if (!presenter.active || !presenter.visiblePresenter ||
        !presenter.frameOpen)
    {
        return 0;
    }
    D3D12FrameContext &frame = presenter.frames[presenter.activeFrameIndex];
    if (frame.mappedUpload == nullptr)
    {
        return 0;
    }

    const auto readbackStarted = std::chrono::steady_clock::now();
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH,
                  static_cast<GLint>(presenter.uploadFootprint.Footprint.RowPitch / 4));
    glReadPixels(0, 0, static_cast<GLsizei>(presenter.width),
                 static_cast<GLsizei>(presenter.height),
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 frame.mappedUpload + presenter.uploadFootprint.Offset);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        std::fprintf(stderr, "[ModernGraphics] OpenGL compatibility capture "
                             "failed (GL error 0x%04x); reverting to SDL "
                             "presentation.\n", static_cast<unsigned int>(error));
        presenter.active = false;
        presenter.visiblePresenter = false;
        return 0;
    }
    presenter.frameReadbackMilliseconds +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - readbackStarted).count();
    ++presenter.frameReadbackCount;
    presenter.frameCaptured = true;
    return 1;
#endif
}

extern "C" int hwModernGraphicsRenderMissionSky(
    unsigned int textureName, float fade)
{
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    if (!presenter.active || !presenter.visiblePresenter || !presenter.frameOpen)
        return 0;
    const int recorded =
        hwglRecordMissionSky(static_cast<GLuint>(textureName), fade);
    if (recorded)
    {
        presenter.missionSkyTextureName = textureName;
        presenter.missionSkyFade = std::max(0.0f, std::min(fade, 1.0f));
    }
    return recorded;
#else
    (void)textureName;
    (void)fade;
    return 0;
#endif
}

extern "C" int hwModernGraphicsFlushOpenGLFrame(unsigned int width,
                                                   unsigned int height)
{
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    bool standaloneFrame;
    if (!presenter.active || !presenter.visiblePresenter) return 0;
    standaloneFrame = !presenter.frameOpen;
    if (standaloneFrame)
    {
        hwModernGraphicsBeginFrame(width, height);
        if (!presenter.active || !presenter.visiblePresenter) return 0;
        if (!presenter.frameOpen) return 1;
    }
    hwglMarkSnapshot(HW_LEGACY_RASTER_FINAL);
    presenter.frameCaptured = true;
    if (standaloneFrame && presenter.frameOpen) hwModernGraphicsEndFrame();
    return presenter.active && presenter.visiblePresenter ? 1 : 0;
#else
    bool standaloneFrame;
    int captured;

    if (!presenter.active || !presenter.visiblePresenter)
    {
        return 0;
    }

    /* The ordinary gameplay/HorseRace paths already bracket rndFlush with a
       modern frame.  Old FE helpers such as rndClear/TradeMgr do not.  Never
       let those paths fall through to SDL_GL_SwapWindow while DXGI owns the
       visible HWND: mixing the two presentation models can leave the DXGI
       image black/stale until Windows forces an expose via Alt+Tab. */
    standaloneFrame = !presenter.frameOpen;
    if (standaloneFrame)
    {
        hwModernGraphicsBeginFrame(width, height);
        if (!presenter.active || !presenter.visiblePresenter)
        {
            return 0;
        }
        if (!presenter.frameOpen)
        {
            /* DXGI still owns presentation, so suppress a legacy window swap
               even if this bridge frame could not be opened. */
            return 1;
        }
    }

    captured = hwModernGraphicsCaptureOpenGLFrame();
    if (standaloneFrame && presenter.frameOpen)
    {
        hwModernGraphicsEndFrame();
    }

    /* Capture can explicitly disable the presenter after a GL/D3D failure.
       Only then is it safe for the caller to resume SDL presentation. */
    if (!presenter.active || !presenter.visiblePresenter)
    {
        return 0;
    }

    (void)captured;
    return 1;
#endif
}

void recordDustFroxelBuild(PresenterConstants &constants)
{
    constants.presenterPadding[1] = 0.0f;
    if (constants.dustRenderEnabled == 0u || constants.dustVolumeCount == 0u ||
        !presenter.dustFroxelTexture || !presenter.dustFroxelPipeline ||
        !presenter.dustFroxelRootSignature)
    {
        return;
    }
    if (presenter.dustFroxelValidThisFrame)
    {
        constants.presenterPadding[1] = 1.0f;
        return;
    }

    if (presenter.dustFroxelShaderReadable)
    {
        transition(presenter.commandList.Get(), presenter.dustFroxelTexture.Get(),
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        presenter.dustFroxelShaderReadable = false;
    }

    presenter.commandList->SetPipelineState(presenter.dustFroxelPipeline.Get());
    presenter.commandList->SetComputeRootSignature(
        presenter.dustFroxelRootSignature.Get());
    ID3D12DescriptorHeap *heaps[] = { presenter.srvHeap.Get() };
    presenter.commandList->SetDescriptorHeaps(1, heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE dustHandle =
        presenter.srvHeap->GetGPUDescriptorHandleForHeapStart();
    dustHandle.ptr += static_cast<UINT64>(
        dustDescriptorBase + presenter.activeFrameIndex * dustDescriptorsPerFrame) *
        presenter.srvDescriptorSize;
    presenter.commandList->SetComputeRootDescriptorTable(0, dustHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle =
        presenter.srvHeap->GetGPUDescriptorHandleForHeapStart();
    uavHandle.ptr += static_cast<UINT64>(dustFroxelNearUavDescriptorIndex) *
                     presenter.srvDescriptorSize;
    constants.presenterPadding[1] = 0.0f;
    presenter.commandList->SetComputeRoot32BitConstants(
        1, sizeof(constants) / sizeof(unsigned int), &constants, 0);
    presenter.commandList->SetComputeRootDescriptorTable(2, uavHandle);

    const UINT activeWidth = std::max(1u, static_cast<UINT>(
        std::lround(constants.dustGridMinWidth[3])));
    const UINT activeHeight = std::max(1u, static_cast<UINT>(
        std::lround(constants.dustGridSizeHeight[3])));
    const UINT activeDepth = std::max(1u, static_cast<UINT>(
        std::lround(constants.presenterPadding[0])));
    presenter.commandList->Dispatch(
        (activeWidth + 3u) / 4u,
        (activeHeight + 3u) / 4u,
        (activeDepth + 3u) / 4u);

    /* Mip 1 is independent storage for an 80^3 far cascade spanning four
       times the near extent. Reusing the allocation adds only 12.5% memory. */
    const UINT farWidth = std::max(1u, presenter.dustFroxelWidth / 2u);
    const UINT farHeight = std::max(1u, presenter.dustFroxelHeight / 2u);
    const UINT farDepth = std::max(1u, presenter.dustFroxelDepth / 2u);
    constants.presenterPadding[1] = 2.0f;
    presenter.commandList->SetComputeRoot32BitConstants(
        1, sizeof(constants) / sizeof(unsigned int), &constants, 0);
    uavHandle.ptr += presenter.srvDescriptorSize;
    presenter.commandList->SetComputeRootDescriptorTable(2, uavHandle);
    presenter.commandList->Dispatch(
        (farWidth + 3u) / 4u,
        (farHeight + 3u) / 4u,
        (farDepth + 3u) / 4u);
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = presenter.dustFroxelTexture.Get();
    presenter.commandList->ResourceBarrier(1, &uavBarrier);
    transition(presenter.commandList.Get(), presenter.dustFroxelTexture.Get(),
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    presenter.dustFroxelShaderReadable = true;
    presenter.dustFroxelValidThisFrame = true;
    constants.presenterPadding[1] = 1.0f;
}

void recordDustScreenBuild(PresenterConstants &constants)
{
    if (constants.dustRenderEnabled == 0u || constants.dustVolumeCount == 0u ||
        !presenter.dustScreenPipeline || !presenter.dustScreenTextures[0] ||
        !presenter.dustScreenTextures[1])
        return;

    const UINT writeIndex = presenter.dustScreenWriteIndex;
    const UINT historyIndex = writeIndex ^ 1u;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE dustDescriptors =
        presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
    dustDescriptors.ptr += static_cast<SIZE_T>(dustDescriptorBase +
        presenter.activeFrameIndex * dustDescriptorsPerFrame + 6u) *
        presenter.srvDescriptorSize;
    /* t15 is not read by DustScreenPS; keep it on history while the current
       target is writable. t16 is the actual reprojected prior frame. */
    presenter.device->CreateShaderResourceView(
        presenter.dustScreenTextures[historyIndex].Get(), &srv,
        dustDescriptors);
    dustDescriptors.ptr += presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(
        presenter.dustScreenTextures[historyIndex].Get(), &srv,
        dustDescriptors);

    if (presenter.dustScreenShaderReadable[writeIndex])
    {
        transition(presenter.commandList.Get(),
                   presenter.dustScreenTextures[writeIndex].Get(),
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        presenter.dustScreenShaderReadable[writeIndex] = false;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        presenter.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameBufferCount + 1u + writeIndex) *
               presenter.rtvDescriptorSize;
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    presenter.commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
    presenter.commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(presenter.dustScreenWidth);
    viewport.Height = static_cast<float>(presenter.dustScreenHeight);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor = {0, 0,
        static_cast<LONG>(presenter.dustScreenWidth),
        static_cast<LONG>(presenter.dustScreenHeight)};
    presenter.commandList->RSSetViewports(1, &viewport);
    presenter.commandList->RSSetScissorRects(1, &scissor);
    presenter.commandList->SetPipelineState(presenter.dustScreenPipeline.Get());
    presenter.commandList->SetGraphicsRootSignature(presenter.rootSignature.Get());
    ID3D12DescriptorHeap *heaps[] = {presenter.srvHeap.Get()};
    presenter.commandList->SetDescriptorHeaps(1, heaps);
    presenter.commandList->SetGraphicsRootDescriptorTable(
        0, presenter.srvHeap->GetGPUDescriptorHandleForHeapStart());
    D3D12_GPU_DESCRIPTOR_HANDLE motionHandle =
        presenter.srvHeap->GetGPUDescriptorHandleForHeapStart();
    motionHandle.ptr += static_cast<UINT64>(9u) * presenter.srvDescriptorSize;
    presenter.commandList->SetGraphicsRootDescriptorTable(1, motionHandle);
    constants.presenterPadding[1] =
        presenter.dustScreenHistoryValid ? 1.0f : 0.0f;
    presenter.commandList->SetGraphicsRoot32BitConstants(
        2, sizeof(constants) / sizeof(unsigned int), &constants, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE dustHandle =
        presenter.srvHeap->GetGPUDescriptorHandleForHeapStart();
    dustHandle.ptr += static_cast<UINT64>(dustDescriptorBase +
        presenter.activeFrameIndex * dustDescriptorsPerFrame) *
        presenter.srvDescriptorSize;
    presenter.commandList->SetGraphicsRootDescriptorTable(3, dustHandle);
    presenter.commandList->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    presenter.commandList->DrawInstanced(3, 1, 0, 0);
    transition(presenter.commandList.Get(),
               presenter.dustScreenTextures[writeIndex].Get(),
               D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    presenter.dustScreenShaderReadable[writeIndex] = true;

    /* Final presentation in this frame samples the freshly written field. */
    dustDescriptors.ptr -= presenter.srvDescriptorSize;
    presenter.device->CreateShaderResourceView(
        presenter.dustScreenTextures[writeIndex].Get(), &srv,
        dustDescriptors);
    presenter.dustScreenWriteIndex = historyIndex;
    presenter.dustScreenHistoryValid = true;
}

void recordScenePrepass(PresenterConstants &constants)
{
    recordDustScreenBuild(constants);
    if (!presenter.sceneInputTexture || !presenter.scenePrepassPipeline) return;
    if (presenter.sceneInputTextureShaderReadable)
    {
        transition(presenter.commandList.Get(), presenter.sceneInputTexture.Get(),
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        presenter.sceneInputTextureShaderReadable = false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        presenter.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameBufferCount) *
               presenter.rtvDescriptorSize;
    presenter.commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(presenter.sceneInputWidth);
    viewport.Height = static_cast<float>(presenter.sceneInputHeight);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor = { 0, 0,
        static_cast<LONG>(presenter.sceneInputWidth),
        static_cast<LONG>(presenter.sceneInputHeight) };
    presenter.commandList->RSSetViewports(1, &viewport);
    presenter.commandList->RSSetScissorRects(1, &scissor);
    presenter.commandList->SetPipelineState(presenter.scenePrepassPipeline.Get());
    presenter.commandList->SetGraphicsRootSignature(presenter.rootSignature.Get());
    ID3D12DescriptorHeap *heaps[] = { presenter.srvHeap.Get() };
    presenter.commandList->SetDescriptorHeaps(1, heaps);
    presenter.commandList->SetGraphicsRootDescriptorTable(
        0, presenter.srvHeap->GetGPUDescriptorHandleForHeapStart());
    D3D12_GPU_DESCRIPTOR_HANDLE motionHandle =
        presenter.srvHeap->GetGPUDescriptorHandleForHeapStart();
    motionHandle.ptr += static_cast<UINT64>(9) * presenter.srvDescriptorSize;
    presenter.commandList->SetGraphicsRootDescriptorTable(1, motionHandle);
    presenter.commandList->SetGraphicsRoot32BitConstants(
        2, sizeof(constants) / sizeof(unsigned int), &constants, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE dustHandle =
        presenter.srvHeap->GetGPUDescriptorHandleForHeapStart();
    dustHandle.ptr += static_cast<UINT64>(
        dustDescriptorBase + presenter.activeFrameIndex * dustDescriptorsPerFrame) *
        presenter.srvDescriptorSize;
    presenter.commandList->SetGraphicsRootDescriptorTable(3, dustHandle);
    presenter.commandList->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    presenter.commandList->DrawInstanced(3, 1, 0, 0);

    transition(presenter.commandList.Get(), presenter.sceneInputTexture.Get(),
               D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    presenter.sceneInputTextureShaderReadable = true;
}

void recordPresenterPass(UINT backBufferIndex,
                         PresenterConstants &constants)
{
    recordDustFroxelBuild(constants);
    ID3D12Resource *backBuffer = presenter.backBuffers[backBufferIndex].Get();
    transition(presenter.commandList.Get(), backBuffer,
               D3D12_RESOURCE_STATE_PRESENT,
               D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        presenter.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(backBufferIndex) *
               presenter.rtvDescriptorSize;
    presenter.commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(presenter.width);
    viewport.Height = static_cast<float>(presenter.height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor = { 0, 0,
                           static_cast<LONG>(presenter.width),
                           static_cast<LONG>(presenter.height) };
    presenter.commandList->RSSetViewports(1, &viewport);
    presenter.commandList->RSSetScissorRects(1, &scissor);
    presenter.commandList->SetPipelineState(presenter.presenterPipeline.Get());
    presenter.commandList->SetGraphicsRootSignature(presenter.rootSignature.Get());
    ID3D12DescriptorHeap *heaps[] = { presenter.srvHeap.Get() };
    presenter.commandList->SetDescriptorHeaps(1, heaps);
    presenter.commandList->SetGraphicsRootDescriptorTable(
        0, presenter.srvHeap->GetGPUDescriptorHandleForHeapStart());
    D3D12_GPU_DESCRIPTOR_HANDLE motionHandle =
        presenter.srvHeap->GetGPUDescriptorHandleForHeapStart();
    motionHandle.ptr += static_cast<UINT64>(9) * presenter.srvDescriptorSize;
    presenter.commandList->SetGraphicsRootDescriptorTable(1, motionHandle);
    presenter.commandList->SetGraphicsRoot32BitConstants(
        2, sizeof(constants) / sizeof(unsigned int), &constants, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE dustHandle =
        presenter.srvHeap->GetGPUDescriptorHandleForHeapStart();
    dustHandle.ptr += static_cast<UINT64>(
        dustDescriptorBase + presenter.activeFrameIndex * dustDescriptorsPerFrame) *
        presenter.srvDescriptorSize;
    presenter.commandList->SetGraphicsRootDescriptorTable(3, dustHandle);
    presenter.commandList->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    presenter.commandList->DrawInstanced(3, 1, 0, 0);
    transition(presenter.commandList.Get(), backBuffer,
               D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_PRESENT);
}

extern "C" void hwModernGraphicsEndFrame(void)
{
    if (!presenter.active || !presenter.frameOpen)
    {
        return;
    }
    presenter.frameOpen = false;
    if (!presenter.frameCaptured)
    {
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
        hwglEndFrame();
#endif
        return;
    }

    D3D12FrameContext &frame = presenter.frames[presenter.activeFrameIndex];
    HRESULT result = frame.allocator->Reset();
    if (SUCCEEDED(result))
    {
        result = presenter.commandList->Reset(frame.allocator.Get(),
                                               presenter.presenterPipeline.Get());
    }
    if (FAILED(result))
    {
        logFailure("resetting the D3D12 present command list", result);
        presenter.active = false;
        presenter.visiblePresenter = false;
        return;
    }

    const UINT gpuQueryBase = presenter.activeFrameIndex * 4u;
    const bool gpuTimingActive = presenter.timestampQueryHeap &&
        frame.timestampReadback && presenter.timestampFrequency != 0;
    if (gpuTimingActive)
        presenter.commandList->EndQuery(
            presenter.timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            gpuQueryBase + 0u);

    ID3D12Resource *rayEnvironmentResource = presenter.backgroundTexture.Get();
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    hwlegacyd3d12::SnapshotCopyTarget nativeCopies[3] = {};
    size_t nativeCopyCount = 0;
    if (presenter.worldFrameCaptured)
    {
        nativeCopies[nativeCopyCount++] = {
            presenter.worldTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            hwglSnapshotCommandCount(HW_LEGACY_RASTER_WORLD)
        };
    }
    if (presenter.backgroundFrameCaptured)
    {
        nativeCopies[nativeCopyCount++] = {
            presenter.backgroundTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            hwglSnapshotCommandCount(HW_LEGACY_RASTER_BACKGROUND)
        };
    }
    if (presenter.occluderFrameCaptured)
    {
        nativeCopies[nativeCopyCount++] = {
            presenter.occluderTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            hwglSnapshotCommandCount(HW_LEGACY_RASTER_OCCLUDERS)
        };
    }

    /* RTX-0061: replay the legacy command stream once.  The old native path
       replayed the same prefix up to four times to reconstruct final/world/
       background/occluder snapshots.  A real legacy framebuffer only rendered
       once and snapshots observed its state at those boundaries, so copy the
       single D3D12 raster target at the marked command counts instead. */
    if (!hwlegacyd3d12::renderFrameSnapshots(
            presenter.device.Get(), presenter.commandList.Get(),
            presenter.sourceTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            presenter.width, presenter.height,
            hwglSnapshotCommandCount(HW_LEGACY_RASTER_FINAL),
            nativeCopies, nativeCopyCount))
    {
        std::fprintf(stderr, "[NativeRaster] Single-pass D3D12 frame replay failed.\n");
        presenter.active = false;
        presenter.visiblePresenter = false;
        return;
    }
    presenter.sourceTextureShaderReadable = true;
    if (hwlegacyd3d12::prepareDepthForSampling(presenter.commandList.Get()))
    {
        refreshNativeRasterDepthSrv(hwlegacyd3d12::depthResource());
        presenter.nativeRasterDepthSampleReady = true;
    }
    if (presenter.worldFrameCaptured)
        presenter.worldTextureShaderReadable = true;
    if (presenter.backgroundFrameCaptured)
        presenter.backgroundTextureShaderReadable = true;
    if (presenter.occluderFrameCaptured)
        presenter.occluderTextureShaderReadable = true;

    /* Bind DXR environment lighting to the same world-space BC6H mission sky
       used by the visible native backdrop. This removes the old camera-screen
       projection/folding that made secondary bounce energy change while the
       camera orbited a stationary ship. */
    bool rayEnvironmentIsDualParaboloid = false;
    if (presenter.missionSkyTextureName != 0 &&
        hwglPrepareMissionSkyForCompute(
            static_cast<GLuint>(presenter.missionSkyTextureName),
            presenter.commandList.Get()))
    {
        void *rawEnvironment = hwglGetMissionSkyD3D12Resource(
            static_cast<GLuint>(presenter.missionSkyTextureName));
        if (rawEnvironment != nullptr)
        {
            rayEnvironmentResource =
                static_cast<ID3D12Resource *>(rawEnvironment);
            const D3D12_RESOURCE_DESC environmentDesc =
                rayEnvironmentResource->GetDesc();
            D3D12_SHADER_RESOURCE_VIEW_DESC environmentSrv = {};
            environmentSrv.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            environmentSrv.Format = environmentDesc.Format;
            environmentSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            environmentSrv.Texture2D.MipLevels = environmentDesc.MipLevels;
            D3D12_CPU_DESCRIPTOR_HANDLE handle =
                presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(rayEnvironmentDescriptorIndex) *
                          presenter.srvDescriptorSize;
            presenter.device->CreateShaderResourceView(
                rayEnvironmentResource, &environmentSrv, handle);
            rayEnvironmentIsDualParaboloid = true;
            if (!presenter.missionSkyEnvironmentLogged)
            {
                std::fprintf(stderr,
                    "[ModernGraphics] DXR bounce environment: world-space "
                    "dual-paraboloid BC6H mission sky active.\n");
                presenter.missionSkyEnvironmentLogged = true;
            }
        }
    }
    if (!rayEnvironmentIsDualParaboloid)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC environmentSrv = {};
        environmentSrv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        environmentSrv.Format = sceneFormat;
        environmentSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        environmentSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(rayEnvironmentDescriptorIndex) *
                      presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(
            presenter.backgroundTexture.Get(), &environmentSrv, handle);
    }
    hwmodern::raytracingSetEnvironmentMode(
        rayEnvironmentIsDualParaboloid, presenter.missionSkyFade);
#else
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetEnvironmentMode(false, 1.0f);
#endif
    if (presenter.sourceTextureShaderReadable)
    {
        transition(presenter.commandList.Get(), presenter.sourceTexture.Get(),
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
    }

    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = presenter.sourceTexture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = frame.upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = presenter.uploadFootprint;
    presenter.commandList->CopyTextureRegion(&destination, 0, 0, 0,
                                              &source, nullptr);
    transition(presenter.commandList.Get(), presenter.sourceTexture.Get(),
               D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    presenter.sourceTextureShaderReadable = true;

    if (presenter.worldFrameCaptured)
    {
        if (presenter.worldTextureShaderReadable)
        {
            transition(presenter.commandList.Get(), presenter.worldTexture.Get(),
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_COPY_DEST);
        }
        D3D12_TEXTURE_COPY_LOCATION worldDestination = {};
        worldDestination.pResource = presenter.worldTexture.Get();
        worldDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION worldSource = {};
        worldSource.pResource = frame.worldUpload.Get();
        worldSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        worldSource.PlacedFootprint = presenter.uploadFootprint;
        presenter.commandList->CopyTextureRegion(
            &worldDestination, 0, 0, 0, &worldSource, nullptr);
        transition(presenter.commandList.Get(), presenter.worldTexture.Get(),
                   D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        presenter.worldTextureShaderReadable = true;
    }
    else if (!presenter.worldTextureShaderReadable)
    {
        /* t2 remains part of the presenter table even when the runtime branch
           is disabled; keep its resource state valid for D3D12 validation. */
        transition(presenter.commandList.Get(), presenter.worldTexture.Get(),
                   D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        presenter.worldTextureShaderReadable = true;
    }


    if (presenter.backgroundFrameCaptured)
    {
        if (presenter.backgroundTextureShaderReadable)
        {
            transition(presenter.commandList.Get(),
                       presenter.backgroundTexture.Get(),
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_COPY_DEST);
        }
        D3D12_TEXTURE_COPY_LOCATION backgroundDestination = {};
        backgroundDestination.pResource = presenter.backgroundTexture.Get();
        backgroundDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION backgroundSource = {};
        backgroundSource.pResource = frame.backgroundUpload.Get();
        backgroundSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        backgroundSource.PlacedFootprint = presenter.uploadFootprint;
        presenter.commandList->CopyTextureRegion(
            &backgroundDestination, 0, 0, 0, &backgroundSource, nullptr);
        transition(presenter.commandList.Get(), presenter.backgroundTexture.Get(),
                   D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        presenter.backgroundTextureShaderReadable = true;
    }
    else if (!presenter.backgroundTextureShaderReadable)
    {
        transition(presenter.commandList.Get(), presenter.backgroundTexture.Get(),
                   D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        presenter.backgroundTextureShaderReadable = true;
    }

    if (presenter.occluderFrameCaptured)
    {
        if (presenter.occluderTextureShaderReadable)
        {
            transition(presenter.commandList.Get(),
                       presenter.occluderTexture.Get(),
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_COPY_DEST);
        }
        D3D12_TEXTURE_COPY_LOCATION occluderDestination = {};
        occluderDestination.pResource = presenter.occluderTexture.Get();
        occluderDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION occluderSource = {};
        occluderSource.pResource = frame.occluderUpload.Get();
        occluderSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        occluderSource.PlacedFootprint = presenter.uploadFootprint;
        presenter.commandList->CopyTextureRegion(
            &occluderDestination, 0, 0, 0, &occluderSource, nullptr);
        transition(presenter.commandList.Get(), presenter.occluderTexture.Get(),
                   D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        presenter.occluderTextureShaderReadable = true;
    }
    else if (!presenter.occluderTextureShaderReadable)
    {
        /* The shader only samples t7 while god rays are enabled, but keep the
           declared descriptor in a validation-safe state on all other frames. */
        transition(presenter.commandList.Get(), presenter.occluderTexture.Get(),
                   D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        presenter.occluderTextureShaderReadable = true;
    }

#endif

    if (gpuTimingActive)
        presenter.commandList->EndQuery(
            presenter.timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            gpuQueryBase + 1u);

    bool rayOverlayActive = false;
    bool streamlineActiveThisFrame = false;
    float streamlineJitterX = 0.0f;
    float streamlineJitterY = 0.0f;
#if defined(HW_ENABLE_D3D12_BACKEND)
    if (presenter.worldFrameCaptured)
    {
        const auto dxrRecordStarted = std::chrono::steady_clock::now();
        const bool rayReconstructionEligibleThisFrame =
            requestedRayReconstruction &&
            requestedAntiAliasingMode >= HW_MODERN_AA_DLSS_QUALITY &&
            requestedAntiAliasingMode <= HW_MODERN_AA_DLSS_ULTRA_PERFORMANCE &&
            hwmodern::dlaaRayReconstructionAvailable();
        const bool temporalUpscalingRequested =
            requestedAntiAliasingMode != HW_MODERN_AA_OFF &&
            requestedAntiAliasingMode != HW_MODERN_AA_FXAA;
        if (temporalUpscalingRequested)
        {
            temporalJitter(presenter.frameCount,
                           streamlineJitterX, streamlineJitterY);
        }
        hwmodern::raytracingSetJitter(streamlineJitterX,
                                     streamlineJitterY);
        hwmodern::raytracingSetRayReconstructionEnabled(
            rayReconstructionEligibleThisFrame);
        rayOverlayActive = hwmodern::raytracingRecord(
            presenter.commandList.Get(), rayEnvironmentResource,
            mapLightEmitters.empty() ? nullptr : mapLightEmitters.data(),
            static_cast<unsigned int>(mapLightEmitters.size()),
            dynamicLightEmitters.empty() ? nullptr : dynamicLightEmitters.data(),
            static_cast<unsigned int>(dynamicLightEmitters.size()));
        presenter.dxrRecordMilliseconds +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - dxrRecordStarted).count();
        ++presenter.dxrRecordCount;
        if (rayOverlayActive)
        {
            ID3D12Resource *primaryAlbedo =
                hwmodern::raytracingDiffuseAlbedoResource();
            if (primaryAlbedo != nullptr)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC albedoSrv = {};
                albedoSrv.Shader4ComponentMapping =
                    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                albedoSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                albedoSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                albedoSrv.Texture2D.MipLevels = 1;
                D3D12_CPU_DESCRIPTOR_HANDLE albedoHandle =
                    presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
                albedoHandle.ptr += static_cast<SIZE_T>(
                    dustDescriptorBase +
                    presenter.activeFrameIndex * dustDescriptorsPerFrame + 4u) *
                    presenter.srvDescriptorSize;
                presenter.device->CreateShaderResourceView(
                    primaryAlbedo, &albedoSrv, albedoHandle);
                static bool emissiveFxProtectionLogged = false;
                if (!emissiveFxProtectionLogged)
                {
                    std::fprintf(stderr,
                        "[ModernGraphics] Foreground emissive FX shadow protection "
                        "active: primary-albedo guide separates trails/beams/glows "
                        "from opaque DXR shadow receivers.\n");
                    emissiveFxProtectionLogged = true;
                }
            }
        }
        if (rayOverlayActive &&
            requestedAntiAliasingMode != HW_MODERN_AA_OFF &&
            requestedAntiAliasingMode != HW_MODERN_AA_FXAA &&
            presenter.sceneInputTexture)
        {
            /* Real full-scene SR/DLAA input: render the complete HUD-less 3D
               scene (native raster + DXR + dust + scene post) at the same
               internal resolution as the matching depth/motion buffers. UI is
               deliberately absent and is composited at native resolution by
               the final presenter after Streamline reconstruction. */
            PresenterConstants sceneConstants = {};
            sceneConstants.rayOverlayEnabled = 1u;
            sceneConstants.antiAliasingMode =
                static_cast<unsigned int>(HW_MODERN_AA_OFF);
            sceneConstants.sceneCompositeEnabled = 1u;
            sceneConstants.frameIndex = static_cast<unsigned int>(presenter.frameCount);
            sceneConstants.chromaticAberration = requestedChromaticAberration;
            sceneConstants.motionBlur = requestedMotionBlur;
            /* These two post-process slots are deliberately inactive in the
               Streamline prepass, so they carry the matching render-pixel
               jitter without growing the already-full presenter root
               signature. The final pass still receives the real settings. */
            sceneConstants.filmGrain = streamlineJitterX;
            sceneConstants.godRays = presenter.godRaySourceExternalThisFrame ?
                (presenter.godRaySourceExternalVisible ? requestedGodRays : 0.0f) :
                (presenter.godRaySourceLocked && presenter.godRaySourceVisible ?
                    requestedGodRays : 0.0f);
            sceneConstants.godRayCenter[0] = presenter.godRayCenterX;
            sceneConstants.godRayCenter[1] = presenter.godRayCenterY;
            sceneConstants.outputDither = streamlineJitterY;
            sceneConstants.godRaySourceColor[0] = presenter.godRaySourceColor[0];
            sceneConstants.godRaySourceColor[1] = presenter.godRaySourceColor[1];
            sceneConstants.godRaySourceColor[2] = presenter.godRaySourceColor[2];
            sceneConstants.godRaySourceExternal =
                presenter.godRaySourceExternalThisFrame ? 1u : 0u;
            sceneConstants.bloom = requestedBloom;
            sceneConstants.scenePrepass = 1u;
            sceneConstants.streamlineResolvedScene = 0u;
            prepareVolumetricDustFrame(sceneConstants);
            recordScenePrepass(sceneConstants);

            const bool streamlineCameraReset =
                streamlineCameraChangedThisFrame();
            if (rayReconstructionEligibleThisFrame)
            {
                streamlineActiveThisFrame =
                    hwmodern::dlaaEvaluateRayReconstruction(
                        presenter.commandList.Get(), presenter.sceneInputTexture.Get(),
                        presenter.sceneInputWidth, presenter.sceneInputHeight, sceneFormat,
                        hwmodern::raytracingDepthResource(),
                        hwmodern::raytracingMotionResource(),
                        hwmodern::raytracingDiffuseAlbedoResource(),
                        hwmodern::raytracingSpecularAlbedoResource(),
                        hwmodern::raytracingNormalRoughnessResource(),
                        hwmodern::raytracingFieldOfViewDegrees(),
                        hwmodern::raytracingAspectRatio(),
                        streamlineJitterX, streamlineJitterY,
                        hwmodern::raytracingHistoryReset() || streamlineCameraReset);
            }
            if (!streamlineActiveThisFrame)
            {
                /* RR failure never changes presentation/exposure math.  Fall
                   back to the existing DLSS SR path for this frame/run. */
                streamlineActiveThisFrame = hwmodern::upscalerEvaluate(
                    presenter.commandList.Get(), presenter.sceneInputTexture.Get(),
                    presenter.sceneInputWidth, presenter.sceneInputHeight, sceneFormat,
                    hwmodern::raytracingDepthResource(),
                    hwmodern::raytracingMotionResource(),
                    nullptr,
                    hwmodern::raytracingFieldOfViewDegrees(),
                    streamlineJitterX, streamlineJitterY,
                    16.6667f,
                    hwmodern::raytracingHistoryReset() || streamlineCameraReset);
            }
            transition(presenter.commandList.Get(),
                       hwmodern::raytracingMotionResource(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
    }
#endif

    if (gpuTimingActive)
        presenter.commandList->EndQuery(
            presenter.timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            gpuQueryBase + 2u);

    PresenterConstants constants = {};
    constants.rayOverlayEnabled = rayOverlayActive ? 1u : 0u;
    if (requestedAntiAliasingMode == HW_MODERN_AA_OFF ||
        requestedAntiAliasingMode == HW_MODERN_AA_FXAA)
    {
        constants.antiAliasingMode = static_cast<unsigned int>(
            requestedAntiAliasingMode);
    }
    else
    {
        /* DLSS/DLAA needs valid depth and motion. FXAA is the deterministic
           cross-vendor fallback for menus and unsupported adapters. */
        constants.antiAliasingMode = streamlineActiveThisFrame ?
            static_cast<unsigned int>(requestedAntiAliasingMode) :
            static_cast<unsigned int>(HW_MODERN_AA_FXAA);
    }
    constants.sceneCompositeEnabled = presenter.worldFrameCaptured ? 1u : 0u;
    constants.frameIndex = static_cast<unsigned int>(presenter.frameCount);
    constants.chromaticAberration = requestedChromaticAberration;
    constants.motionBlur = requestedMotionBlur;
    constants.filmGrain = requestedFilmGrain;
    constants.godRays = presenter.godRaySourceExternalThisFrame ?
        (presenter.godRaySourceExternalVisible ? requestedGodRays : 0.0f) :
        (presenter.godRaySourceLocked && presenter.godRaySourceVisible ?
            requestedGodRays : 0.0f);
    constants.godRayCenter[0] = presenter.godRayCenterX;
    constants.godRayCenter[1] = presenter.godRayCenterY;
    constants.outputDither = requestedOutputDitherStrength;
    constants.godRaySourceColor[0] = presenter.godRaySourceColor[0];
    constants.godRaySourceColor[1] = presenter.godRaySourceColor[1];
    constants.godRaySourceColor[2] = presenter.godRaySourceColor[2];
    constants.godRaySourceExternal =
        presenter.godRaySourceExternalThisFrame ? 1u : 0u;
    constants.bloom = requestedBloom;
    /* Upload/editor-pack dust only when the final presenter will actually
       raymarch it. With Streamline active the complete dust result already
       lives in the reconstructed HUD-less scene, so repeating CPU volume/light
       filtering here is pure overhead. */
    if (!streamlineActiveThisFrame)
    {
        prepareVolumetricDustFrame(constants);
        recordDustScreenBuild(constants);
    }
    constants.scenePrepass = 0u;
    constants.streamlineResolvedScene = streamlineActiveThisFrame ? 1u : 0u;
    recordPresenterPass(presenter.activeFrameIndex, constants);

    if (gpuTimingActive)
    {
        presenter.commandList->EndQuery(
            presenter.timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            gpuQueryBase + 3u);
        presenter.commandList->ResolveQueryData(
            presenter.timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            gpuQueryBase, 4u, frame.timestampReadback.Get(), 0);
        frame.timestampPending = true;
    }

    result = presenter.commandList->Close();
    if (FAILED(result))
    {
        logFailure("ID3D12GraphicsCommandList::Close", result);
        presenter.active = false;
        presenter.visiblePresenter = false;
        return;
    }
    ID3D12CommandList *lists[] = { presenter.commandList.Get() };
    presenter.queue->ExecuteCommandLists(1, lists);

    const UINT syncInterval = presenter.vsyncMode == 0 ? 0u : 1u;
    const UINT presentFlags = syncInterval == 0 && presenter.allowTearing ?
        DXGI_PRESENT_ALLOW_TEARING : 0u;
    result = presenter.swapchain->Present(syncInterval, presentFlags);
    if (FAILED(result))
    {
        logFailure("IDXGISwapChain::Present", result);
        presenter.active = false;
        presenter.visiblePresenter = false;
        return;
    }
    const UINT64 fenceValue = presenter.nextFenceValue++;
    result = presenter.queue->Signal(presenter.fence.Get(), fenceValue);
    if (FAILED(result))
    {
        logFailure("ID3D12CommandQueue::Signal", result);
        presenter.active = false;
        presenter.visiblePresenter = false;
        return;
    }
    presenter.submittedFenceValue = fenceValue;
    frame.fenceValue = fenceValue;
    if (presenter.cpuFrameTimerActive)
    {
        presenter.cpuFrameMilliseconds +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() -
                presenter.cpuFrameStarted).count();
        ++presenter.cpuFrameSamples;
        presenter.cpuFrameTimerActive = false;
    }
    ++presenter.frameCount;
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    hwglEndFrame();
#endif

    if ((presenter.frameCount % 600u) == 0u)
    {
        const auto average = [](double total, unsigned long long count)
        {
            return count == 0 ? 0.0 : total / static_cast<double>(count);
        };
        std::fprintf(stderr,
            "[ModernGraphics] CPU owning-frame telemetry: %.3f ms/frame "
            "across %llu completed frames (includes legacy recording, "
            "modern command staging and Present).\n",
            average(presenter.cpuFrameMilliseconds, presenter.cpuFrameSamples),
            presenter.cpuFrameSamples);
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
        std::fprintf(stderr,
            "[ModernGraphics] CPU frame telemetry (600 frames): native D3D12 "
            "raster replay active (no OpenGL framebuffer readback); "
            "DXR command staging %.3f ms.\n",
            average(presenter.dxrRecordMilliseconds,
                    presenter.dxrRecordCount));
#else
        std::fprintf(stderr,
            "[ModernGraphics] CPU frame telemetry (600 frames): OpenGL "
            "readback background %.3f ms, world %.3f ms, composite %.3f ms; "
            "DXR command staging %.3f ms.\n",
            average(presenter.backgroundReadbackMilliseconds,
                    presenter.backgroundReadbackCount),
            average(presenter.worldReadbackMilliseconds,
                    presenter.worldReadbackCount),
            average(presenter.frameReadbackMilliseconds,
                    presenter.frameReadbackCount),
            average(presenter.dxrRecordMilliseconds,
                    presenter.dxrRecordCount));
#endif
        if (presenter.gpuTimestampFrames != 0)
        {
            const double gpuDivisor =
                static_cast<double>(presenter.gpuTimestampFrames);
            std::fprintf(stderr,
                "[ModernGraphics] GPU pass telemetry (%llu resolved frames): "
                "raster %.3f ms, DXR/AA %.3f ms, presenter %.3f ms, "
                "recorded frame %.3f ms.\n",
                presenter.gpuTimestampFrames,
                presenter.gpuRasterMilliseconds / gpuDivisor,
                presenter.gpuDxrMilliseconds / gpuDivisor,
                presenter.gpuPresenterMilliseconds / gpuDivisor,
                presenter.gpuFrameMilliseconds / gpuDivisor);
        }
        presenter.gpuRasterMilliseconds = 0.0;
        presenter.gpuDxrMilliseconds = 0.0;
        presenter.gpuPresenterMilliseconds = 0.0;
        presenter.gpuFrameMilliseconds = 0.0;
        presenter.gpuTimestampFrames = 0;
        presenter.cpuFrameMilliseconds = 0.0;
        presenter.cpuFrameSamples = 0;
        presenter.backgroundReadbackMilliseconds = 0.0;
        presenter.worldReadbackMilliseconds = 0.0;
        presenter.frameReadbackMilliseconds = 0.0;
        presenter.dxrRecordMilliseconds = 0.0;
        presenter.backgroundReadbackCount = 0;
        presenter.worldReadbackCount = 0;
        presenter.frameReadbackCount = 0;
        presenter.dxrRecordCount = 0;
    }

    if ((presenter.frameCount % 600u) == 1u &&
        (!mapLightEmitters.empty() || !dynamicLightEmitters.empty()))
    {
        size_t navLightCount = 0;
        size_t weaponLightCount = 0;
        size_t engineLightCount = 0;
        for (const HWModernDynamicLightEmitter &emitter : dynamicLightEmitters)
        {
            if (emitter.source == HW_MODERN_LIGHT_NAV)
                ++navLightCount;
            else if (emitter.source == HW_MODERN_LIGHT_ENGINE)
                ++engineLightCount;
            else if (emitter.source == HW_MODERN_LIGHT_MUZZLE_FLASH ||
                     emitter.source == HW_MODERN_LIGHT_WEAPON ||
                     emitter.source == HW_MODERN_LIGHT_ION_BEAM ||
                     emitter.source == HW_MODERN_LIGHT_EXPLOSION)
                ++weaponLightCount;
        }
        std::fprintf(stderr,
                     "[ModernGraphics] DXR path-traced frame: %zu map, "
                     "%zu dynamic (%zu engine, %zu weapon/beam/explosion, "
                     "%zu nav), %zu emissive material records "
                     "%s.\n",
                     mapLightEmitters.size(), dynamicLightEmitters.size(),
                     engineLightCount, weaponLightCount, navLightCount,
                     emissiveMaterials.size(),
                     rayOverlayActive ? "(DXR shadow rays active)" :
                                        "(awaiting DXR scene geometry)");
    }
}

extern "C" void hwModernGraphicsShutdown(void)
{
    logTextureLoadMetrics();
    presenter.frameOpen = false;
    presenter.frameCaptured = false;
    if (presenter.queue && presenter.fence && presenter.fenceEvent)
    {
        waitForSubmittedWork();
    }
    if (presenter.device && presenter.frameCount > 0)
    {
        std::fprintf(stderr,
                     "[ModernGraphics] D3D12 visible presenter stopped after "
                     "%llu frames.\n", presenter.frameCount);
    }
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    hwlegacyd3d12::shutdown();
    hwglResetAll();
#endif
    releasePresenter();
}

extern "C" int hwModernGraphicsIsActive(void)
{
    return presenter.active ? 1 : 0;
}

extern "C" int hwModernGraphicsIsVisiblePresenterActive(void)
{
    return presenter.active && presenter.visiblePresenter ? 1 : 0;
}

extern "C" void hwModernGraphicsSetVSync(int mode)
{
    presenter.vsyncMode = mode == 0 ? 0 : 1;
    if (mode < 0)
    {
        std::fprintf(stderr, "[ModernGraphics] Adaptive VSync requested; "
                             "DXGI presenter is using ordinary VSync.\n");
    }
}

extern "C" void hwModernGraphicsSetRaytracingEnabled(int enabled)
{
    const bool requested = enabled != 0;
    if (raytracingRequested == requested)
    {
        return;
    }
    raytracingRequested = requested;
#if defined(HW_ENABLE_D3D12_BACKEND)
    if (!presenter.active || !presenter.raytracingDevice)
    {
        return;
    }
    if (!waitForSubmittedWork())
    {
        return;
    }
    if (!raytracingRequested)
    {
        hwmodern::raytracingShutdown();
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
        nullSrv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.Format = presenterFormat;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE raySrv =
            presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
        raySrv.ptr += presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(nullptr, &nullSrv, raySrv);
        D3D12_SHADER_RESOURCE_VIEW_DESC nullMotion = {};
        nullMotion.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullMotion.Format = DXGI_FORMAT_R16G16_FLOAT;
        nullMotion.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullMotion.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE motionSrv =
            presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
        motionSrv.ptr += static_cast<SIZE_T>(9) *
                         presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(nullptr, &nullMotion,
                                                    motionSrv);
        std::fprintf(stderr, "[ModernGraphics] DXR path tracing disabled.\n");
        return;
    }
    if (hwmodern::raytracingInitialize(
            presenter.device.Get(), presenter.srvHeap.Get(),
            presenter.srvDescriptorSize, 1, 4,
            rayEnvironmentDescriptorIndex, rrGuideUavDescriptorBase))
    {
        UINT rayWidth = presenter.width;
        UINT rayHeight = presenter.height;
        raytracingDimensionsForAntiAliasingMode(
            requestedAntiAliasingMode, presenter.width, presenter.height,
            rayWidth, rayHeight);
        if (!hwmodern::raytracingCreateOutput(rayWidth, rayHeight))
        {
            hwmodern::raytracingShutdown();
            std::fprintf(stderr, "[ModernGraphics] DXR output allocation "
                                 "failed; D3D12 presentation remains active.\n");
            return;
        }
        hwmodern::raytracingSetFxLightingStrength(
            requestedFxLightingStrength);
        hwmodern::raytracingSetPathSettings(
            requestedPathSamples, requestedPathBounces,
            requestedNormalStrength);
        hwmodern::raytracingSetRayReconstructionEnabled(
            requestedRayReconstruction &&
            requestedAntiAliasingMode >= HW_MODERN_AA_DLSS_QUALITY &&
            requestedAntiAliasingMode <= HW_MODERN_AA_DLSS_ULTRA_PERFORMANCE &&
            hwmodern::dlaaRayReconstructionAvailable());
        hwmodern::raytracingSetLightingTuning(
            requestedEnvironmentStrength, requestedAuthoredLightStrength,
            requestedSurfaceReflectivity, requestedSurfaceRoughness,
            requestedLightingExposure);
        hwmodern::raytracingSetSkyLightingStrengths(
            requestedPrimarySunStrength, requestedSkyAmbientStrength);
        hwmodern::raytracingSetShadowSettings(
            requestedShadowStrength, requestedSunShadowAngularRadiusDegrees,
            requestedSunShadowSamples, requestedLocalShadowSamples,
            requestedLocalShadowAngularRadiusDegrees,
            requestedShadowReceiverBias, requestedShadowNormalBias,
            requestedContactShadowStrength, requestedContactShadowDistance,
            requestedShadowMaximumDistance);
        std::fprintf(stderr, "[ModernGraphics] DXR path tracing enabled.\n");
    }
    else
    {
        hwmodern::raytracingShutdown();
        std::fprintf(stderr, "[ModernGraphics] DXR could not be enabled; "
                             "D3D12 presentation remains active.\n");
    }
#endif
}

extern "C" void hwModernGraphicsSetFxLightingStrength(float strength)
{
    if (strength < 0.0f)
    {
        strength = 0.0f;
    }
    else if (strength > 2.0f)
    {
        strength = 2.0f;
    }
    requestedFxLightingStrength = strength;
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetFxLightingStrength(strength);
#endif
}

extern "C" void hwModernGraphicsSetPathTracingSettings(
    unsigned int samplesPerPixel, unsigned int maximumBounces,
    float generatedNormalStrength)
{
    requestedPathSamples = std::max(1u, std::min(samplesPerPixel, 4u));
    requestedPathBounces = std::max(1u, std::min(maximumBounces, 3u));
    requestedNormalStrength = std::max(
        0.0f, std::min(generatedNormalStrength, 4.0f));
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetPathSettings(
        requestedPathSamples, requestedPathBounces,
        requestedNormalStrength);
#endif
}

extern "C" void hwModernGraphicsSetLightingEditorSettings(
    float environmentStrength, float authoredLightStrength,
    float surfaceReflectivity, float surfaceRoughness, float exposure)
{
    requestedEnvironmentStrength = std::max(
        0.0f, std::min(environmentStrength, 2.0f));
    requestedAuthoredLightStrength = std::max(
        0.0f, std::min(authoredLightStrength, 2.0f));
    requestedSurfaceReflectivity = std::max(
        0.0f, std::min(surfaceReflectivity, 2.0f));
    requestedSurfaceRoughness = std::max(
        0.0f, std::min(surfaceRoughness, 1.0f));
    requestedLightingExposure = std::max(
        0.5f, std::min(exposure, 2.0f));
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetLightingTuning(
        requestedEnvironmentStrength, requestedAuthoredLightStrength,
        requestedSurfaceReflectivity, requestedSurfaceRoughness,
        requestedLightingExposure);
#endif
}

extern "C" void hwModernGraphicsSetSkyLightingStrengths(
    float primarySunStrength, float skyAmbientStrength)
{
    requestedPrimarySunStrength = std::max(
        0.0f, std::min(primarySunStrength, 2.0f));
    requestedSkyAmbientStrength = std::max(
        0.0f, std::min(skyAmbientStrength, 2.0f));
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetSkyLightingStrengths(
        requestedPrimarySunStrength, requestedSkyAmbientStrength);
#endif
}

extern "C" void hwModernGraphicsSetShadowSettings(
    float shadowStrength, float sunAngularRadiusDegrees,
    unsigned int sunSamples, unsigned int localSamples,
    float localAngularRadiusDegrees, float receiverBias, float normalBias,
    float contactStrength, float contactDistance, float maximumDistance)
{
    requestedShadowStrength = std::max(0.0f, std::min(shadowStrength, 2.0f));
    requestedSunShadowAngularRadiusDegrees = std::max(0.0f, std::min(sunAngularRadiusDegrees, 5.0f));
    requestedSunShadowSamples = std::max(1u, std::min(sunSamples, 16u));
    requestedLocalShadowSamples = std::max(1u, std::min(localSamples, 8u));
    requestedLocalShadowAngularRadiusDegrees = std::max(0.0f, std::min(localAngularRadiusDegrees, 10.0f));
    requestedShadowReceiverBias = std::max(0.001f, std::min(receiverBias, 2.0f));
    requestedShadowNormalBias = std::max(0.0f, std::min(normalBias, 4.0f));
    requestedContactShadowStrength = std::max(0.0f, std::min(contactStrength, 2.0f));
    requestedContactShadowDistance = std::max(0.0f, std::min(contactDistance, 10000.0f));
    requestedShadowMaximumDistance = std::max(1000.0f, std::min(maximumDistance, 1000000.0f));
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetShadowSettings(
        requestedShadowStrength, requestedSunShadowAngularRadiusDegrees,
        requestedSunShadowSamples, requestedLocalShadowSamples,
        requestedLocalShadowAngularRadiusDegrees,
        requestedShadowReceiverBias, requestedShadowNormalBias,
        requestedContactShadowStrength, requestedContactShadowDistance,
        requestedShadowMaximumDistance);
#endif
}

extern "C" void hwModernGraphicsSetAntiAliasingMode(int mode)
{
    mode = std::max(static_cast<int>(HW_MODERN_AA_OFF),
                    std::min(mode,
                             static_cast<int>(HW_MODERN_AA_XESS_ULTRA_PERFORMANCE)));
    if (requestedAntiAliasingMode == mode)
    {
        return;
    }
    requestedAntiAliasingMode = mode;
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::dlaaSetMode(mode);
    hwmodern::upscalerSetRequestedMode(mode);
    hwmodern::dlaaSetRayReconstructionEnabled(requestedRayReconstruction);
    hwmodern::raytracingSetRayReconstructionEnabled(
        requestedRayReconstruction &&
        mode >= HW_MODERN_AA_DLSS_QUALITY &&
        mode <= HW_MODERN_AA_DLSS_ULTRA_PERFORMANCE &&
        hwmodern::dlaaRayReconstructionAvailable());
    if (!presenter.active || !presenter.device || !presenter.srvHeap)
    {
        return;
    }
    if (!waitForSubmittedWork())
    {
        return;
    }
    hwmodern::upscalerReleaseOutput();
    UINT sceneWidth = presenter.width;
    UINT sceneHeight = presenter.height;
    raytracingDimensionsForAntiAliasingMode(
        mode, presenter.width, presenter.height, sceneWidth, sceneHeight);
    if (hwmodern::raytracingInitialized())
    {
        hwmodern::raytracingReleaseOutput();
        if (!hwmodern::raytracingCreateOutput(sceneWidth, sceneHeight))
        {
            std::fprintf(stderr,
                         "[ModernGraphics] Could not resize the DXR target "
                         "for anti-aliasing mode %d.\n", mode);
        }
    }
    if (!createSceneInputTexture(sceneWidth, sceneHeight))
    {
        std::fprintf(stderr,
                     "[ModernGraphics] Could not resize the full-scene "
                     "Streamline input for anti-aliasing mode %d.\n", mode);
    }
    else
    {
        std::fprintf(stderr,
                     "[ModernGraphics] Full-scene AA mode %d: %ux%u internal "
                     "-> %ux%u display.\n",
                     mode, sceneWidth, sceneHeight,
                     presenter.width, presenter.height);
    }
    if (!createDustScreenTextures(sceneWidth, sceneHeight))
        std::fprintf(stderr,
            "[ModernGraphics] Could not resize screen-space dust target.\n");

    if (mode != HW_MODERN_AA_OFF && mode != HW_MODERN_AA_FXAA &&
        hwmodern::upscalerAvailableForMode(mode))
    {
        hwmodern::upscalerCreateOutput(
            presenter.srvHeap.Get(), presenter.srvDescriptorSize, 3,
            presenter.width, presenter.height);
    }
    else
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = sceneFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            presenter.srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(3) * presenter.srvDescriptorSize;
        presenter.device->CreateShaderResourceView(
            presenter.worldTexture.Get(), &srv, handle);
    }
#endif
}
extern "C" void hwModernGraphicsSetFrameGenerationMode(int mode)
{
    const bool requestedEnable = mode != HW_MODERN_FRAME_GENERATION_OFF;
    static bool loggedUnavailable = false;
    if (requestedEnable && !loggedUnavailable)
    {
        std::fprintf(stderr,
            "[ModernGraphics] Frame generation request ignored: the old "
            "cross-vendor interpolation presenter was removed. True DLSS-G "
            "requires the official Streamline DLSS-G + Reflex/PCL runtime "
            "integration and is not exposed as working yet.\n");
        loggedUnavailable = true;
    }
}
extern "C" int hwModernGraphicsIsDlaaActive(void)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    return hwmodern::upscalerActive() && hwmodern::raytracingActive() ? 1 : 0;
#else
    return 0;
#endif
}

extern "C" void hwModernGraphicsSetPostProcessingSettings(
    float chromaticAberration, float motionBlur, float filmGrain,
    float godRays, float bloom)
{
    requestedChromaticAberration = std::max(
        0.0f, std::min(chromaticAberration, 1.0f));
    requestedMotionBlur = std::max(0.0f, std::min(motionBlur, 1.0f));
    requestedFilmGrain = std::max(0.0f, std::min(filmGrain, 1.0f));
    requestedGodRays = std::max(0.0f, std::min(godRays, 1.0f));
    requestedBloom = std::max(0.0f, std::min(bloom, 4.0f));
    std::fprintf(stderr,
                 "[ModernGraphics] Scene post FX: chromatic %.0f%%, motion "
                 "blur %.0f%%, film grain %.0f%%, god rays %.0f%%, "
                 "bloom %.0f%%.\n",
                 requestedChromaticAberration * 100.0f,
                 requestedMotionBlur * 100.0f,
                 requestedFilmGrain * 100.0f,
                 requestedGodRays * 100.0f,
                 requestedBloom * 100.0f);
}

extern "C" void hwModernGraphicsSetGodRaySource(
    float screenX, float screenY, float red, float green, float blue,
    int visible)
{
    /* This is a per-frame projection override only.  Mission identity,
       world-space source locking, sky ambient, and primary-sun lighting stay
       on the existing map-owned path. */
    presenter.godRaySourceExternalThisFrame = true;
    presenter.godRaySourceExternalVisible = visible != 0;
    presenter.godRayCenterX = std::max(-4.0f, std::min(screenX, 5.0f));
    presenter.godRayCenterY = std::max(-4.0f, std::min(screenY, 5.0f));
    if (presenter.godRaySourceExternalVisible)
    {
        presenter.godRaySourceColor[0] = std::max(0.0f, std::min(red, 1.0f));
        presenter.godRaySourceColor[1] = std::max(0.0f, std::min(green, 1.0f));
        presenter.godRaySourceColor[2] = std::max(0.0f, std::min(blue, 1.0f));
    }
    else
    {
        presenter.godRaySourceColor[0] = 0.0f;
        presenter.godRaySourceColor[1] = 0.0f;
        presenter.godRaySourceColor[2] = 0.0f;
    }
}

extern "C" void hwModernGraphicsSetOutputDitherStrength(float strength)
{
    requestedOutputDitherStrength = std::max(0.0f, std::min(strength, 2.0f));
    std::fprintf(stderr,
                 "[ModernGraphics] Output de-banding dither: %.0f%%.\n",
                 requestedOutputDitherStrength * 100.0f);
}

extern "C" void hwModernGraphicsEarlyInitialize(void)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::dlaaEarlyInitialize();
#endif
}

extern "C" void hwModernGraphicsBeginMapLightUpdate(void)
{
    mapLightEmitters.clear();
}

extern "C" void hwModernGraphicsSubmitMapLight(
    const HWModernMapLightEmitter *emitter)
{
    if (emitter != nullptr)
    {
        mapLightEmitters.push_back(*emitter);
    }
}

extern "C" void hwModernGraphicsEndMapLightUpdate(void)
{
    std::fprintf(stderr, "[ModernGraphics] Preserved %zu authored map light "
                         "emitters for the DXR scene.\n",
                 mapLightEmitters.size());
}

extern "C" unsigned int hwModernGraphicsGetMapLightCount(void)
{
    return static_cast<unsigned int>(mapLightEmitters.size());
}

extern "C" int hwModernGraphicsGetMapLight(
    unsigned int index, HWModernMapLightEmitter *outEmitter)
{
    if (outEmitter == nullptr || index >= mapLightEmitters.size())
    {
        return 0;
    }
    *outEmitter = mapLightEmitters[index];
    return 1;
}

extern "C" void hwModernGraphicsSetMissionAuthoringLights(
    const HWModernMissionLightEmitter *emitters, unsigned int count)
{
    missionAuthoringLightEmitters.clear();
    if (emitters != nullptr && count > 0)
        missionAuthoringLightEmitters.assign(emitters, emitters + count);
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetMissionAuthoringLights(emitters, count);
#endif
}

extern "C" void hwModernGraphicsSetMissionAuthoringReplacesMapLights(int replace)
{
    missionAuthoringReplacesMapLightsForDust = replace != 0;
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSetMissionAuthoringReplacesMapLights(replace != 0);
#endif
}

extern "C" void hwModernGraphicsSetVolumetricDustVolumes(
    const HWModernVolumetricDustVolume *volumes, unsigned int count)
{
    volumetricDustVolumes.clear();
    if (volumes == nullptr || count == 0)
    {
        /* A mission/editor reset must not leak recovering wakes into the next
           authored map. */
        volumetricDustWakeHistory.clear();
        volumetricDustInteractorStates.clear();
        return;
    }

    /* Pack renderable volumes BEFORE applying the GPU capacity limit.  The old
       setter truncated the authored array first and filtered disabled/zero-density
       entries later in prepareVolumetricDustFrame().  Dead editor entries could
       therefore consume slots and push valid clouds past the 32-volume cutoff. */
    volumetricDustVolumes.reserve(maximumVolumetricDustVolumes);
    for (unsigned int i = 0; i < count; ++i)
    {
        const HWModernVolumetricDustVolume &volume = volumes[i];
        if (!volume.enabled || volume.density <= 0.00001f) continue;
        volumetricDustVolumes.push_back(volume);
        if (volumetricDustVolumes.size() >= maximumVolumetricDustVolumes) break;
    }

    {
        static unsigned int lastIncomingCount = 0xffffffffu;
        static unsigned int lastPackedCount = 0xffffffffu;
        const unsigned int packedCount =
            static_cast<unsigned int>(volumetricDustVolumes.size());
        if (lastIncomingCount != count || lastPackedCount != packedCount)
        {
            std::fprintf(stderr,
                "[ModernGraphics] RTX dust volume upload: incoming=%u renderable=%u cap=%u.\n",
                count, packedCount, maximumVolumetricDustVolumes);
            lastIncomingCount = count;
            lastPackedCount = packedCount;
        }
    }
}

extern "C" void hwModernGraphicsSetVolumetricDustFade(
    float distance, float strength)
{
    if (!std::isfinite(distance) || !std::isfinite(strength)) return;
    volumetricDustFadeDistance = std::max(1000.0f,
        std::min(distance, 500000.0f));
    volumetricDustFadeStrength = std::max(0.0f,
        std::min(strength, 8.0f));
}

extern "C" void hwModernGraphicsSetVolumetricDustSimulationPaused(int paused)
{
    const bool nextPaused = paused != 0;
    if (nextPaused == volumetricDustSimulationPaused) return;

    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    if (nextPaused)
    {
        volumetricDustPauseStarted = now;
        volumetricDustSimulationPaused = true;
        return;
    }

    /* Move all wake/interactor timestamps forward by the editor pause span so
       their apparent age is unchanged when simulation resumes. */
    const std::chrono::steady_clock::duration pausedFor =
        now - volumetricDustPauseStarted;
    for (VolumetricDustWakeRecord &wake : volumetricDustWakeHistory)
        wake.created += pausedFor;
    for (auto &state : volumetricDustInteractorStates)
    {
        state.second.lastSeen += pausedFor;
        state.second.lastEmit += pausedFor;
    }
    volumetricDustSimulationPaused = false;
}

extern "C" void hwModernGraphicsSetVolumetricDustInteractors(
    const HWModernVolumetricDustInteractor *interactors, unsigned int count)
{
    volumetricDustInteractors.clear();
    if (interactors == nullptr || count == 0) return;
    const unsigned int safeCount = std::min(count, 192u);
    volumetricDustInteractors.assign(interactors, interactors + safeCount);
}

extern "C" void hwModernGraphicsSubmitDynamicLight(
    const HWModernDynamicLightEmitter *emitter)
{
    if (emitter != nullptr && emitter->intensity > 0.0f &&
        emitter->radius > 0.0f)
    {
        dynamicLightEmitters.push_back(*emitter);
    }
}

extern "C" void hwModernGraphicsTriggerDynamicLight(
    const HWModernDynamicLightEmitter *emitter, float lifetimeSeconds)
{
    static bool loggedFirstTimedLight = false;
    if (!presenter.active || emitter == nullptr || emitter->intensity <= 0.0f ||
        emitter->radius <= 0.0f || lifetimeSeconds <= 0.0f)
    {
        return;
    }

    TimedDynamicLight retained;
    retained.emitter = *emitter;
    retained.started = DynamicLightClock::now();
    retained.lifetimeSeconds = lifetimeSeconds;
    if (timedDynamicLightEmitters.size() >= 512)
    {
        timedDynamicLightEmitters.erase(timedDynamicLightEmitters.begin());
    }
    timedDynamicLightEmitters.push_back(retained);

    /* If gameplay fired after BeginFrame, include the pulse immediately as
       well as retaining it for subsequent frames. */
    if (presenter.frameOpen)
    {
        dynamicLightEmitters.push_back(*emitter);
    }
    if (!loggedFirstTimedLight)
    {
        std::fprintf(stderr,
                     "[ModernGraphics] Gun-event timed muzzle lighting active.\n");
        loggedFirstTimedLight = true;
    }
}

extern "C" void hwModernGraphicsRegisterEmissiveMaterial(
    const char *meshName, const char *materialName, const char *textureName,
    unsigned int fullAmbientColors, const float color[3], float intensity)
{
    EmissiveMaterialRecord record;
    record.meshName = meshName != nullptr ? meshName : "";
    record.materialName = materialName != nullptr ? materialName : "";
    record.textureName = textureName != nullptr ? textureName : "";
    record.fullAmbientColors = fullAmbientColors;
    if (color != nullptr)
    {
        record.color[0] = color[0];
        record.color[1] = color[1];
        record.color[2] = color[2];
    }
    record.intensity = intensity;

    for (const EmissiveMaterialRecord &existing : emissiveMaterials)
    {
        if (existing.meshName == record.meshName &&
            existing.materialName == record.materialName &&
            existing.textureName == record.textureName)
        {
            return;
        }
    }
    emissiveMaterials.push_back(record);
}

extern "C" void hwModernGraphicsRegisterSurfaceTexture(
    const void *materialIdentity, unsigned int width, unsigned int height,
    const unsigned int *surfaceRgba, const unsigned int *emissiveRgba,
    const unsigned int *normalRgba, const unsigned int *ormRgba)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingRegisterSurfaceTexture(
        materialIdentity, width, height, surfaceRgba, emissiveRgba, normalRgba,
        ormRgba, nullptr);
#else
    (void)materialIdentity;
    (void)width;
    (void)height;
    (void)surfaceRgba;
    (void)emissiveRgba;
    (void)normalRgba;
    (void)ormRgba;
#endif
}

extern "C" void hwModernGraphicsRegisterSurfaceTextureNamed(
    const void *materialIdentity, const char *textureKey,
    unsigned int width, unsigned int height,
    const unsigned int *surfaceRgba, const unsigned int *emissiveRgba,
    const unsigned int *normalRgba, const unsigned int *ormRgba)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingRegisterSurfaceTexture(
        materialIdentity, width, height, surfaceRgba, emissiveRgba, normalRgba,
        ormRgba, textureKey);
#else
    (void)materialIdentity; (void)textureKey; (void)width; (void)height;
    (void)surfaceRgba; (void)emissiveRgba; (void)normalRgba; (void)ormRgba;
#endif
}

extern "C" int hwModernGraphicsTryAliasSurfaceTexture(
    const void *materialIdentity, const char *textureKey)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    return hwmodern::raytracingTryAliasSurfaceTexture(materialIdentity,
                                                       textureKey) ? 1 : 0;
#else
    (void)materialIdentity; (void)textureKey; return 0;
#endif
}

namespace
{
struct TextureLoadMetrics
{
    unsigned long long decodeCalls = 0;
    unsigned long long decodeBytes = 0;
    double decodeMilliseconds = 0.0;
    unsigned long long sharedHits = 0;
    unsigned long long sharedMisses = 0;
    unsigned long long uploadCalls = 0;
    unsigned long long uploadBytes = 0;
    double uploadMilliseconds = 0.0;
};

TextureLoadMetrics textureLoadMetrics;

void logTextureLoadMetrics()
{
    std::fprintf(stderr,
        "[TexturePerf] DDS decode=%llu calls %.1f MiB %.1f ms; shared=%llu hit/%llu miss; compressed upload=%llu calls %.1f MiB %.1f ms\n",
        textureLoadMetrics.decodeCalls,
        static_cast<double>(textureLoadMetrics.decodeBytes) / (1024.0 * 1024.0),
        textureLoadMetrics.decodeMilliseconds,
        textureLoadMetrics.sharedHits, textureLoadMetrics.sharedMisses,
        textureLoadMetrics.uploadCalls,
        static_cast<double>(textureLoadMetrics.uploadBytes) / (1024.0 * 1024.0),
        textureLoadMetrics.uploadMilliseconds);
}

struct TextureMetricTimer
{
    double *destination;
    std::chrono::steady_clock::time_point started;
    explicit TextureMetricTimer(double *value)
        : destination(value), started(std::chrono::steady_clock::now()) {}
    ~TextureMetricTimer()
    {
        *destination += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    }
};
}

extern "C" int hwModernGraphicsDecodeDds(
    const void *data, unsigned int size, unsigned int *width,
    unsigned int *height, unsigned char **rgba)
{
    TextureMetricTimer metricTimer(&textureLoadMetrics.decodeMilliseconds);
    ++textureLoadMetrics.decodeCalls;
    textureLoadMetrics.decodeBytes += size;
    if (data == nullptr || size == 0 || width == nullptr || height == nullptr ||
        rgba == nullptr)
        return 0;
    *rgba = nullptr;
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage encoded;
    HRESULT result = DirectX::LoadFromDDSMemory(
        static_cast<const std::uint8_t *>(data), size,
        DirectX::DDS_FLAGS_NONE, &metadata, encoded);
    if (FAILED(result) || metadata.width == 0 || metadata.height == 0 ||
        metadata.width > 16384 || metadata.height > 16384)
        return 0;
    const DirectX::Image *source = encoded.GetImage(0, 0, 0);
    if (source == nullptr) return 0;
    DirectX::ScratchImage decoded;
    if (DirectX::IsCompressed(source->format))
        result = DirectX::Decompress(*source, DXGI_FORMAT_R8G8B8A8_UNORM, decoded);
    else
        result = DirectX::Convert(*source, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  DirectX::TEX_FILTER_DEFAULT, 0.0f, decoded);
    if (FAILED(result)) return 0;
    const DirectX::Image *image = decoded.GetImage(0, 0, 0);
    if (image == nullptr) return 0;
    const size_t rowBytes = image->width * 4u;
    const size_t byteCount = rowBytes * image->height;
    unsigned char *pixels = static_cast<unsigned char *>(std::malloc(byteCount));
    if (pixels == nullptr) return 0;
    for (size_t y = 0; y < image->height; ++y)
        std::memcpy(pixels + y * rowBytes, image->pixels + y * image->rowPitch,
                    rowBytes);
    *width = static_cast<unsigned int>(image->width);
    *height = static_cast<unsigned int>(image->height);
    *rgba = pixels;
    return 1;
}

namespace
{
struct SharedDdsEntry
{
    unsigned char *pixels = nullptr;
    unsigned int width = 0;
    unsigned int height = 0;
    size_t bytes = 0;
    unsigned long long age = 0;
};
struct SharedDdsIssued
{
    std::string key;
    bool parkOnFree = false;
    unsigned int width = 0;
    unsigned int height = 0;
};
std::unordered_map<std::string, SharedDdsEntry> sharedDdsCache;
std::unordered_map<void *, SharedDdsIssued> sharedDdsIssued;
size_t sharedDdsCacheBytes = 0;
unsigned long long sharedDdsAge = 0;
constexpr size_t sharedDdsCacheLimit = 128u * 1024u * 1024u;

std::string normalizedDdsKey(const char *value)
{
    std::string key = value != nullptr ? value : "";
    for (char &character : key)
    {
        if (character == '\\') character = '/';
        else if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return key;
}

void trimSharedDdsCache()
{
    while (sharedDdsCacheBytes > sharedDdsCacheLimit &&
           !sharedDdsCache.empty())
    {
        auto oldest = sharedDdsCache.begin();
        for (auto it = sharedDdsCache.begin(); it != sharedDdsCache.end(); ++it)
            if (it->second.age < oldest->second.age) oldest = it;
        sharedDdsCacheBytes -= oldest->second.bytes;
        std::free(oldest->second.pixels);
        sharedDdsCache.erase(oldest);
    }
}
}

extern "C" int hwModernGraphicsDecodeDdsShared(
    const void *data, unsigned int size, const char *cacheKey,
    unsigned int *width, unsigned int *height, unsigned char **rgba)
{
    if (width == nullptr || height == nullptr || rgba == nullptr) return 0;
    const std::string key = normalizedDdsKey(cacheKey);
    auto found = sharedDdsCache.find(key);
    if (!key.empty() && found != sharedDdsCache.end())
    {
        ++textureLoadMetrics.sharedHits;
        SharedDdsEntry entry = found->second;
        sharedDdsCacheBytes -= entry.bytes;
        sharedDdsCache.erase(found);
        *width = entry.width;
        *height = entry.height;
        *rgba = entry.pixels;
        sharedDdsIssued[entry.pixels] = {key, false, entry.width, entry.height};
        return 1;
    }
    ++textureLoadMetrics.sharedMisses;
    if (!hwModernGraphicsDecodeDds(data, size, width, height, rgba)) return 0;
    if (!key.empty())
        sharedDdsIssued[*rgba] = {key, true, *width, *height};
    return 1;
}

extern "C" void hwModernGraphicsFreeDecodedDds(void *rgba)
{
    auto issued = sharedDdsIssued.find(rgba);
    if (issued != sharedDdsIssued.end())
    {
        SharedDdsIssued value = issued->second;
        sharedDdsIssued.erase(issued);
        if (value.parkOnFree)
        {
            const size_t bytes = static_cast<size_t>(value.width) *
                                 value.height * 4u;
            auto previous = sharedDdsCache.find(value.key);
            if (previous != sharedDdsCache.end())
            {
                sharedDdsCacheBytes -= previous->second.bytes;
                std::free(previous->second.pixels);
                sharedDdsCache.erase(previous);
            }
            sharedDdsCache[value.key] = {
                static_cast<unsigned char *>(rgba), value.width, value.height,
                bytes, ++sharedDdsAge};
            sharedDdsCacheBytes += bytes;
            trimSharedDdsCache();
            return;
        }
    }
    std::free(rgba);
}

extern "C" int hwModernGraphicsUploadBoundDds(
    const void *data, unsigned int size)
{
#if defined(HW_ENABLE_D3D12_NATIVE_RASTER)
    TextureMetricTimer metricTimer(&textureLoadMetrics.uploadMilliseconds);
    ++textureLoadMetrics.uploadCalls;
    textureLoadMetrics.uploadBytes += size;
    if (data == nullptr || size == 0) return 0;
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage encoded;
    HRESULT result = DirectX::LoadFromDDSMemory(
        static_cast<const std::uint8_t *>(data), size,
        DirectX::DDS_FLAGS_NONE, &metadata, encoded);
    if (FAILED(result) || metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D ||
        metadata.arraySize != 1 || metadata.format != DXGI_FORMAT_BC7_UNORM ||
        metadata.width == 0 || metadata.height == 0)
        return 0;
    for (size_t level = 0; level < metadata.mipLevels; ++level)
    {
        const DirectX::Image *image = encoded.GetImage(level, 0, 0);
        if (image == nullptr || image->slicePitch > 0x7fffffffu) return 0;
        hwglCompressedTexImage2D(
            GL_TEXTURE_2D, static_cast<GLint>(level), 0x8E8Cu,
            static_cast<GLsizei>(image->width),
            static_cast<GLsizei>(image->height), 0,
            static_cast<GLsizei>(image->slicePitch), image->pixels);
    }
    return 1;
#else
    (void)data;
    (void)size;
    return 0;
#endif
}

extern "C" void hwModernGraphicsUnregisterSurfaceTexture(
    const void *materialIdentity)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingUnregisterSurfaceTexture(materialIdentity);
#else
    (void)materialIdentity;
#endif
}

extern "C" void hwModernGraphicsBeginRaytracingScene(
    float verticalFieldOfViewDegrees, float aspectRatio,
    const float viewMatrix[16])
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingBeginScene(verticalFieldOfViewDegrees, aspectRatio,
                                   viewMatrix);
#else
    (void)verticalFieldOfViewDegrees;
    (void)aspectRatio;
    (void)viewMatrix;
#endif
}

extern "C" void hwModernGraphicsEndRaytracingScene(void)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingEndScene();
#endif
}

extern "C" int hwModernGraphicsIsRaytracingSceneOpen(void)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    return hwmodern::raytracingSceneOpen() ? 1 : 0;
#else
    return 0;
#endif
}

extern "C" void hwModernGraphicsSubmitTriangleGeometryMasked(
    const void *geometryIdentity,
    const float *vertexPositions, unsigned int vertexCount,
    unsigned int vertexStrideBytes,
    const unsigned short *triangleIndices, unsigned int triangleCount,
    unsigned int triangleStrideBytes,
    unsigned int surfaceVariant,
    const HWModernTriangleSurface *triangleSurfaces,
    unsigned int triangleSurfaceStrideBytes,
    const float modelViewMatrix[16],
    const float baseColor[3], unsigned int instanceMask)
{
#if defined(HW_ENABLE_D3D12_BACKEND)
    hwmodern::raytracingSubmitGeometry(
        geometryIdentity, vertexPositions, vertexCount, vertexStrideBytes,
        triangleIndices, triangleCount, triangleStrideBytes, surfaceVariant,
        triangleSurfaces, triangleSurfaceStrideBytes, modelViewMatrix,
        baseColor, instanceMask);
#else
    (void)geometryIdentity; (void)vertexPositions; (void)vertexCount;
    (void)vertexStrideBytes; (void)triangleIndices; (void)triangleCount;
    (void)triangleStrideBytes; (void)surfaceVariant; (void)triangleSurfaces;
    (void)triangleSurfaceStrideBytes; (void)modelViewMatrix; (void)baseColor;
    (void)instanceMask;
#endif
}

extern "C" void hwModernGraphicsSubmitTriangleGeometry(
    const void *geometryIdentity,
    const float *vertexPositions, unsigned int vertexCount,
    unsigned int vertexStrideBytes,
    const unsigned short *triangleIndices, unsigned int triangleCount,
    unsigned int triangleStrideBytes,
    unsigned int surfaceVariant,
    const HWModernTriangleSurface *triangleSurfaces,
    unsigned int triangleSurfaceStrideBytes,
    const float modelViewMatrix[16],
    const float baseColor[3])
{
    hwModernGraphicsSubmitTriangleGeometryMasked(
        geometryIdentity, vertexPositions, vertexCount, vertexStrideBytes,
        triangleIndices, triangleCount, triangleStrideBytes, surfaceVariant,
        triangleSurfaces, triangleSurfaceStrideBytes, modelViewMatrix,
        baseColor, 0x03u);
}
#else

extern "C" int hwModernGraphicsQueryCapabilities(HWModernGraphicsCapabilities *capabilities)
{
    if (capabilities != nullptr)
        std::memset(capabilities, 0, sizeof(*capabilities));
    return 0;
}
extern "C" void hwModernGraphicsLogCapabilities(void)
{
    std::fprintf(stderr, "[ModernGraphics] D3D12 support is Windows-only.\n");
}
extern "C" int hwModernGraphicsInitialize(void *, unsigned int, unsigned int)
{
    return 0;
}
extern "C" void hwModernGraphicsBeginFrame(unsigned int, unsigned int) {}
extern "C" void hwModernGraphicsSetGodRayWorldSource(
    int, int, const float[3]) {}
extern "C" void hwModernGraphicsSetMissionSkyLighting(
    int, int, const float[3], const float[3], const float[3]) {}
extern "C" int hwModernGraphicsRenderMissionSky(unsigned int, float) { return 0; }
extern "C" int hwModernGraphicsCaptureOpenGLBackground(
    int, float, float, const float[16], const float[16]) { return 0; }
extern "C" int hwModernGraphicsCaptureOpenGLOccluders(void) { return 0; }
extern "C" int hwModernGraphicsCaptureOpenGLWorld(void) { return 0; }
extern "C" int hwModernGraphicsCaptureOpenGLFrame(void) { return 0; }
extern "C" int hwModernGraphicsFlushOpenGLFrame(unsigned int, unsigned int)
{
    return 0;
}
extern "C" void hwModernGraphicsEndFrame(void) {}
extern "C" void hwModernGraphicsShutdown(void) {}
extern "C" int hwModernGraphicsIsActive(void) { return 0; }
extern "C" int hwModernGraphicsIsVisiblePresenterActive(void) { return 0; }
extern "C" void hwModernGraphicsSetVSync(int) {}
extern "C" void hwModernGraphicsSetRaytracingEnabled(int) {}
extern "C" void hwModernGraphicsSetFxLightingStrength(float) {}
extern "C" void hwModernGraphicsSetPathTracingSettings(
    unsigned int, unsigned int, float) {}
extern "C" void hwModernGraphicsSetLightingEditorSettings(
    float, float, float, float, float) {}
extern "C" void hwModernGraphicsSetSkyLightingStrengths(float, float) {}
extern "C" void hwModernGraphicsSetShadowSettings(
    float, float, unsigned int, unsigned int, float, float, float,
    float, float, float) {}
extern "C" void hwModernGraphicsSetAntiAliasingMode(int) {}
extern "C" void hwModernGraphicsSetFrameGenerationMode(int) {}
extern "C" int hwModernGraphicsIsDlaaActive(void) { return 0; }
extern "C" void hwModernGraphicsSetPostProcessingSettings(
    float, float, float, float, float) {}
extern "C" void hwModernGraphicsSetGodRaySource(
    float, float, float, float, float, int) {}
extern "C" void hwModernGraphicsSetOutputDitherStrength(float) {}
extern "C" void hwModernGraphicsEarlyInitialize(void) {}
extern "C" void hwModernGraphicsBeginMapLightUpdate(void) {}
extern "C" void hwModernGraphicsSubmitMapLight(const HWModernMapLightEmitter *) {}
extern "C" void hwModernGraphicsEndMapLightUpdate(void) {}
extern "C" void hwModernGraphicsSetMissionAuthoringLights(
    const HWModernMissionLightEmitter *, unsigned int) {}
extern "C" void hwModernGraphicsSetMissionAuthoringReplacesMapLights(int) {}
extern "C" void hwModernGraphicsSetVolumetricDustVolumes(
    const HWModernVolumetricDustVolume *, unsigned int) {}
extern "C" void hwModernGraphicsSetVolumetricDustFade(float, float) {}
extern "C" void hwModernGraphicsSetVolumetricDustCamera(
    const float[3], const float[3], const float[3], const float[3]) {}
extern "C" void hwModernGraphicsSetVolumetricDustSimulationPaused(int) {}
extern "C" void hwModernGraphicsSetVolumetricDustInteractors(
    const HWModernVolumetricDustInteractor *, unsigned int) {}
extern "C" void hwModernGraphicsSubmitDynamicLight(
    const HWModernDynamicLightEmitter *) {}
extern "C" void hwModernGraphicsTriggerDynamicLight(
    const HWModernDynamicLightEmitter *, float) {}
extern "C" void hwModernGraphicsRegisterEmissiveMaterial(
    const char *, const char *, const char *, unsigned int, const float[3],
    float) {}
extern "C" void hwModernGraphicsRegisterSurfaceTexture(
    const void *, unsigned int, unsigned int, const unsigned int *,
    const unsigned int *, const unsigned int *, const unsigned int *) {}
extern "C" void hwModernGraphicsUnregisterSurfaceTexture(const void *) {}
extern "C" void hwModernGraphicsRegisterSurfaceTextureNamed(
    const void *, const char *, unsigned int, unsigned int,
    const unsigned int *, const unsigned int *, const unsigned int *,
    const unsigned int *) {}
extern "C" int hwModernGraphicsTryAliasSurfaceTexture(const void *, const char *)
{ return 0; }
extern "C" void hwModernGraphicsBeginRaytracingScene(
    float, float, const float[16]) {}
extern "C" void hwModernGraphicsEndRaytracingScene(void) {}
extern "C" int hwModernGraphicsIsRaytracingSceneOpen(void) { return 0; }
extern "C" void hwModernGraphicsSubmitTriangleGeometryMasked(
    const void *, const float *, unsigned int, unsigned int,
    const unsigned short *, unsigned int, unsigned int, unsigned int,
    const HWModernTriangleSurface *, unsigned int, const float[16],
    const float[3], unsigned int) {}
extern "C" void hwModernGraphicsSubmitTriangleGeometry(
    const void *, const float *, unsigned int, unsigned int,
    const unsigned short *, unsigned int, unsigned int, unsigned int,
    const HWModernTriangleSurface *, unsigned int, const float[16],
    const float[3]) {}
#endif
