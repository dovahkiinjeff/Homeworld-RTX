RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> RayLighting : register(u0);
RWTexture2D<float4> Accumulation : register(u1);
RWTexture2D<float> SceneDepth : register(u2);
RWTexture2D<float2> SceneMotion : register(u3);
RWTexture2D<float4> AccumulationHistory : register(u4);
RWTexture2D<float4> DiffuseAlbedoGuide : register(u5);
RWTexture2D<float4> SpecularAlbedoGuide : register(u6);
RWTexture2D<float4> NormalRoughnessGuide : register(u7);

struct SceneLight
{
    float3 position;
    uint type;
    float3 direction;
    float radius;
    float3 color;
    float intensity;
    float3 endPosition;
    float coneCos;
    float edgeCos;
    float source;
    float2 padding;
};

struct InstanceMotion
{
    float4 previousRow0;
    float4 previousRow1;
    float4 previousRow2;
};

StructuredBuffer<SceneLight> Lights : register(t1);
StructuredBuffer<uint4> SurfaceTexels : register(t2);
StructuredBuffer<InstanceMotion> InstanceMotions : register(t3);
Texture2D<float4> MissionEnvironment : register(t4);

cbuffer FrameConstants : register(b0)
{
    float TanHalfFieldOfView;
    float AspectRatio;
    uint LightCount;
    float MaximumRayDistance;
    uint AccumulationFrame;
    uint SamplesPerPixel;
    uint MaximumBounces;
    float NormalDetailStrength;
    float NearPlane;
    float FarPlane;
    uint ResetAccumulation;
    uint FrameSeed;
    float EnvironmentStrength;
    float SurfaceReflectivity;
    float SurfaceRoughness;
    float Exposure;
    float ShadowStrength;
    float ShadowReceiverBias;
    float ShadowNormalBias;
    float SunShadowAngularRadius;
    uint SunShadowSamples;
    uint LocalShadowSamples;
    float LocalShadowAngularRadius;
    float ShadowMaximumDistance;
    float ContactShadowStrength;
    float ContactShadowDistance;
    uint RayReconstructionEnabled;
    uint ConstantBufferPadding;
    float4 ViewToWorldRow0;
    float4 ViewToWorldRow1;
    float4 ViewToWorldRow2;
    float4 PreviousWorldToViewRow0;
    float4 PreviousWorldToViewRow1;
    float4 PreviousWorldToViewRow2;
    uint EnvironmentIsDualParaboloid;
    uint ViewHistoryValid;
};

StructuredBuffer<float3> Vertices : register(t0, space1);
StructuredBuffer<uint3> Triangles : register(t1, space1);
struct TriangleSurface
{
    float3 baseColor;
    float opacity;
    float3 emissiveColor;
    float emissiveIntensity;
    float2 uv0;
    float2 uv1;
    float2 uv2;
    uint textureOffset;
    uint textureWidth;
    uint textureHeight;
    uint flags;
};
StructuredBuffer<TriangleSurface> Surfaces : register(t2, space1);

struct RayPayload
{
    float3 lighting;
    uint hit;
    uint depth;
    uint seed;
    float depthValue;
    float2 motionPixels;
    float historyDepth;
    float previousHistoryDepth;
    float3 primaryAlbedo;
    float3 primarySpecularAlbedo;
    float4 primaryNormalRoughness;
};

static const uint LIGHT_AMBIENT = 0;
static const uint LIGHT_DIRECTIONAL = 1;
static const uint LIGHT_SPOT = 3;
static const uint LIGHT_LINE = 4;
static const uint SURFACE_HAS_TEXTURE = 0x80000000u;
static const float PI = 3.14159265358979323846;

float4 UnpackRgba(uint packed)
{
    return float4(float(packed & 0xffu),
                  float((packed >> 8u) & 0xffu),
                  float((packed >> 16u) & 0xffu),
                  float((packed >> 24u) & 0xffu)) / 255.0;
}

uint SurfaceTexelIndex(TriangleSurface surface, int2 coordinate)
{
    int width = (int)surface.textureWidth;
    int height = (int)surface.textureHeight;
    uint x = (uint)((coordinate.x % width + width) % width);
    uint y = (uint)((coordinate.y % height + height) % height);
    return surface.textureOffset + y * surface.textureWidth + x;
}

float4 SampleSurfaceChannel(TriangleSurface surface, float2 uv,
                            uint channel)
{
    if ((surface.flags & SURFACE_HAS_TEXTURE) == 0 ||
        surface.textureWidth == 0 || surface.textureHeight == 0)
    {
        if (channel == 3u)
        {
            /* Alpha zero selects the global legacy material controls. */
            return float4(1.0, 1.0, 0.0, 0.0);
        }
        return channel == 1u
            ? float4(surface.emissiveColor,
                     surface.emissiveIntensity > 0.0 ? 1.0 : 0.0)
            : float4(1.0, 1.0, 1.0, 1.0);
    }
    float2 texelPosition = frac(uv) *
        float2(surface.textureWidth, surface.textureHeight) - 0.5;
    int2 base = int2(floor(texelPosition));
    float2 blend = frac(texelPosition);
    uint4 packed00 = SurfaceTexels[SurfaceTexelIndex(surface, base)];
    uint4 packed10 = SurfaceTexels[SurfaceTexelIndex(surface,
                                                      base + int2(1, 0))];
    uint4 packed01 = SurfaceTexels[SurfaceTexelIndex(surface,
                                                      base + int2(0, 1))];
    uint4 packed11 = SurfaceTexels[SurfaceTexelIndex(surface,
                                                      base + int2(1, 1))];
    float4 sample00 = UnpackRgba(packed00[channel]);
    float4 sample10 = UnpackRgba(packed10[channel]);
    float4 sample01 = UnpackRgba(packed01[channel]);
    float4 sample11 = UnpackRgba(packed11[channel]);
    return lerp(lerp(sample00, sample10, blend.x),
                lerp(sample01, sample11, blend.x), blend.y);
}

float3 SampleGeneratedNormal(TriangleSurface surface, float2 uv,
                             float filterTexels)
{
    if ((surface.flags & SURFACE_HAS_TEXTURE) == 0 ||
        surface.textureWidth == 0 || surface.textureHeight == 0)
    {
        return float3(0.0, 0.0, 1.0);
    }
    /* Ray shaders have no implicit texture derivatives. Filter a symmetric
       projected footprint supplied by ApplyGeneratedNormal so rotating a
       four-times-resolution map cannot walk through subpixel slopes. */
    filterTexels = clamp(filterTexels, 1.0, 16.0);
    float radius = max(0.35, filterTexels * 0.5);
    float filterMix = smoothstep(1.0, 2.5, filterTexels);
    float2 texel = 1.0 / float2(surface.textureWidth, surface.textureHeight);
    float4 centerSample = SampleSurfaceChannel(surface, uv, 2u);
    float2 offsetX = float2(texel.x * radius, 0.0);
    float2 offsetY = float2(0.0, texel.y * radius);
    float2 diagonalA = offsetX + offsetY;
    float2 diagonalB = offsetX - offsetY;
    float4 footprintSample = (centerSample * 4.0 +
        (SampleSurfaceChannel(surface, uv + offsetX, 2u) +
         SampleSurfaceChannel(surface, uv - offsetX, 2u) +
         SampleSurfaceChannel(surface, uv + offsetY, 2u) +
         SampleSurfaceChannel(surface, uv - offsetY, 2u)) * 2.0 +
        SampleSurfaceChannel(surface, uv + diagonalA, 2u) +
        SampleSurfaceChannel(surface, uv - diagonalA, 2u) +
        SampleSurfaceChannel(surface, uv + diagonalB, 2u) +
        SampleSurfaceChannel(surface, uv - diagonalB, 2u)) / 16.0;
    float4 normalSample = lerp(centerSample, footprintSample, filterMix);
    float2 encodedSlope = normalSample.xy * 2.0 - 1.0;
    float3 detail = float3(encodedSlope,
        sqrt(saturate(1.0 - dot(encodedSlope, encodedSlope))));
    /* Treat the stored normal as a height-field slope and cap its tilt.  The
       source art contains high-contrast paint and panel lines that are useful
       as shallow relief, but should never become near-vertical mirrors. */
    float2 slope = detail.xy / max(0.24, detail.z);
    /* The editor value remains intuitive at 100%, while the wider 0-400%
       range can deliberately exaggerate the relief for low-contrast LIFs. */
    slope *= NormalDetailStrength * 1.35;
    float slopeLength = length(slope);
    const float maximumSlope = 0.78; /* approximately 38 degrees */
    if (slopeLength > maximumSlope)
    {
        slope *= maximumSlope / slopeLength;
    }
    return normalize(float3(slope, 1.0));
}

void EvaluateSurface(BuiltInTriangleIntersectionAttributes attributes,
                     out TriangleSurface surface, out float2 uv,
                     out float3 baseColor, out float3 emission,
                     out float opacity)
{
    surface = Surfaces[PrimitiveIndex()];
    float3 barycentric = float3(1.0 - attributes.barycentrics.x -
                                      attributes.barycentrics.y,
                                attributes.barycentrics.x,
                                attributes.barycentrics.y);
    uv = surface.uv0 * barycentric.x +
         surface.uv1 * barycentric.y +
         surface.uv2 * barycentric.z;
    float4 visible = SampleSurfaceChannel(surface, uv, 0u);
    float4 emissive = SampleSurfaceChannel(surface, uv, 1u);
    baseColor = surface.baseColor * visible.rgb;
    emission = emissive.rgb * surface.emissiveIntensity;
    opacity = surface.opacity * visible.a;
}

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float Random01(inout uint seed)
{
    seed = Hash(seed + 0x9e3779b9u);
    return (float)(seed & 0x00ffffffu) / 16777216.0;
}

float3 CosineHemisphere(float3 normal, inout uint seed)
{
    float r1 = Random01(seed);
    float r2 = Random01(seed);
    float radius = sqrt(r1);
    float angle = 2.0 * PI * r2;
    float3 tangent = normalize(abs(normal.z) < 0.999
        ? cross(float3(0.0, 0.0, 1.0), normal)
        : cross(float3(0.0, 1.0, 0.0), normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * (radius * cos(angle)) +
                     bitangent * (radius * sin(angle)) +
                     normal * sqrt(max(0.0, 1.0 - r1)));
}

static const uint INSTANCE_MASK_PRIMARY = 0x01u;
static const uint INSTANCE_MASK_SHADOW = 0x02u;
static const uint INSTANCE_MASK_TRANSPORT = 0x01u;

bool IsVisible(float3 origin, float3 direction, float maximumDistance)
{
    RayDesc shadow;
    shadow.Origin = origin;
    shadow.Direction = direction;
    shadow.TMin = max(0.001, ShadowReceiverBias);
    shadow.TMax = max(shadow.TMin + 0.001,
                      min(maximumDistance, ShadowMaximumDistance));

    RayPayload payload;
    payload.lighting = 0.0;
    payload.hit = 0;
    payload.depth = MaximumBounces;
    payload.seed = 0;
    payload.depthValue = 1.0;
    payload.motionPixels = 0.0;
    payload.historyDepth = -1.0;
    payload.previousHistoryDepth = -1.0;
    TraceRay(Scene,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
             RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES,
             INSTANCE_MASK_SHADOW, 1, 2, 1, shadow, payload);
    return payload.hit == 0;
}

float StableShadowNoise(float3 position, uint lightIndex)
{
    int3 cell = int3(floor(position * 0.03125));
    uint seed = Hash(asuint(cell.x) ^ (asuint(cell.y) * 0x9e3779b9u) ^
                     (asuint(cell.z) * 0x85ebca6bu) ^
                     (lightIndex * 0xc2b2ae35u));
    return (float)(seed & 0x00ffffffu) / 16777216.0;
}

float3 ShadowSampleDirection(float3 direction, float angularRadius,
                             uint sampleIndex, uint sampleCount,
                             float rotation)
{
    if (sampleCount <= 1u || angularRadius <= 0.000001)
        return direction;
    float3 tangent = normalize(abs(direction.z) < 0.999
        ? cross(float3(0.0, 0.0, 1.0), direction)
        : cross(float3(0.0, 1.0, 0.0), direction));
    float3 bitangent = cross(direction, tangent);
    const float goldenAngle = 2.39996322972865332;
    float diskRadius = sqrt(((float)sampleIndex + 0.5) / (float)sampleCount);
    float angle = (float)sampleIndex * goldenAngle + rotation * (2.0 * PI);
    float coneRadius = tan(angularRadius) * diskRadius;
    return normalize(direction + tangent * (cos(angle) * coneRadius) +
                     bitangent * (sin(angle) * coneRadius));
}

float ShadowVisibility(float3 origin, float3 direction, float maximumDistance,
                       uint sampleCount, float angularRadius,
                       float3 hitPosition, uint lightIndex)
{
    uint count = max(1u, sampleCount);
    float rotation = StableShadowNoise(hitPosition, lightIndex);
    float visible = 0.0;
    [loop]
    for (uint sampleIndex = 0; sampleIndex < count; ++sampleIndex)
    {
        float3 sampledDirection = ShadowSampleDirection(
            direction, angularRadius, sampleIndex, count, rotation);
        visible += IsVisible(origin, sampledDirection, maximumDistance) ? 1.0 : 0.0;
    }
    visible /= (float)count;
    if (ContactShadowStrength > 0.0001 && ContactShadowDistance > 0.001)
    {
        float contactDistance = min(maximumDistance, ContactShadowDistance);
        float contact = IsVisible(origin, direction, contactDistance) ? 1.0 : 0.0;
        visible = min(visible, saturate(lerp(1.0, contact, ContactShadowStrength)));
    }
    return saturate(1.0 - (1.0 - visible) * ShadowStrength);
}

float DeviceDepth(float viewZ)
{
    float distance = max(NearPlane, -viewZ);
    return saturate(FarPlane * (distance - NearPlane) /
                    (distance * (FarPlane - NearPlane)));
}

float2 ViewPositionToUv(float3 position)
{
    float inverseDepth = 1.0 / max(NearPlane, -position.z);
    float2 ndc = float2(position.x * inverseDepth /
                            (TanHalfFieldOfView * AspectRatio),
                        position.y * inverseDepth / TanHalfFieldOfView);
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

float3 ViewToWorldDirection(float3 viewDirection)
{
    return normalize(float3(
        dot(ViewToWorldRow0.xyz, viewDirection),
        dot(ViewToWorldRow1.xyz, viewDirection),
        dot(ViewToWorldRow2.xyz, viewDirection)));
}

float3 WorldToPreviousViewDirection(float3 worldDirection)
{
    return normalize(float3(
        dot(PreviousWorldToViewRow0.xyz, worldDirection),
        dot(PreviousWorldToViewRow1.xyz, worldDirection),
        dot(PreviousWorldToViewRow2.xyz, worldDirection)));
}

float2 ViewDirectionToUv(float3 viewDirection)
{
    float inverseDepth = 1.0 / max(0.0001, -viewDirection.z);
    float2 ndc = float2(
        viewDirection.x * inverseDepth / (TanHalfFieldOfView * AspectRatio),
        viewDirection.y * inverseDepth / TanHalfFieldOfView);
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
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

float3 LoadEnvironmentBilinear(float2 uv)
{
    uint width;
    uint height;
    MissionEnvironment.GetDimensions(width, height);
    float2 coordinate = saturate(uv) * float2(width, height) - 0.5;
    int2 p0 = int2(floor(coordinate));
    float2 f = frac(coordinate);
    int2 maximumPixel = int2(max(1u, width) - 1u, max(1u, height) - 1u);
    int2 a = clamp(p0, int2(0, 0), maximumPixel);
    int2 b = clamp(p0 + int2(1, 0), int2(0, 0), maximumPixel);
    int2 c = clamp(p0 + int2(0, 1), int2(0, 0), maximumPixel);
    int2 d = clamp(p0 + int2(1, 1), int2(0, 0), maximumPixel);
    float3 top = lerp(MissionEnvironment.Load(int3(a, 0)).rgb,
                      MissionEnvironment.Load(int3(b, 0)).rgb, f.x);
    float3 bottom = lerp(MissionEnvironment.Load(int3(c, 0)).rgb,
                         MissionEnvironment.Load(int3(d, 0)).rgb, f.x);
    return lerp(top, bottom, f.y);
}

float3 SampleDualParaboloidEnvironment(float3 worldDirection)
{
    const float blendBand = 0.035;
    float3 front = max(LoadEnvironmentBilinear(
        DualParaboloidUv(worldDirection, true)), 0.0);
    float3 back = max(LoadEnvironmentBilinear(
        DualParaboloidUv(worldDirection, false)), 0.0);
    float3 radiance;
    if (worldDirection.z >= blendBand)
        radiance = front;
    else if (worldDirection.z <= -blendBand)
        radiance = back;
    else
        radiance = lerp(back, front,
            smoothstep(-blendBand, blendBand, worldDirection.z));
    return radiance * saturate(ViewToWorldRow2.w);
}

float3 SampleMissionEnvironment(float3 direction)
{
    if (EnvironmentIsDualParaboloid != 0)
    {
        /* The native mission sky is scene-linear BC6H and world locked.  Use
           the same dual-paraboloid mapping as the visible native sky instead
           of projecting transport rays back into the current camera image. */
        return SampleDualParaboloidEnvironment(
            ViewToWorldDirection(normalize(direction)));
    }

    /* Compatibility fallback for maps without a modern HDR sky.  This keeps
       the old screen capture behavior only when no world-space environment is
       available; it is never used for the normal modern campaign sky path. */
    uint width;
    uint height;
    MissionEnvironment.GetDimensions(width, height);
    float3 forwardDirection = normalize(float3(
        direction.x, direction.y, -max(0.08, abs(direction.z))));
    float inverseDepth = 1.0 / max(0.08, -forwardDirection.z);
    float2 ndc = float2(
        forwardDirection.x * inverseDepth /
            (TanHalfFieldOfView * AspectRatio),
        forwardDirection.y * inverseDepth / TanHalfFieldOfView);
    float2 uv = saturate(float2(ndc.x * 0.5 + 0.5,
                               0.5 - ndc.y * 0.5));
    uint2 pixel = uint2(
        min(width - 1u, (uint)(uv.x * width)),
        min(height - 1u, (uint)((1.0 - uv.y) * height)));
    float3 encoded = MissionEnvironment.Load(int3(pixel, 0)).rgb;
    float luminance = dot(encoded, float3(0.2126, 0.7152, 0.0722));
    float sourceWeight = 0.10 + 1.40 * smoothstep(0.28, 0.95, luminance);
    return pow(max(encoded, 0.0), 2.2) * sourceWeight;
}

float3 ApplyGeneratedNormal(float3 geometricNormal,
                            float3 p0, float3 p1, float3 p2,
                            TriangleSurface surface, float2 uv,
                            float rayDistance)
{
    float2 uvEdge0 = surface.uv1 - surface.uv0;
    float2 uvEdge1 = surface.uv2 - surface.uv0;
    float determinant = uvEdge0.x * uvEdge1.y -
                        uvEdge0.y * uvEdge1.x;
    if (abs(determinant) < 0.000001 || NormalDetailStrength <= 0.0001)
    {
        return geometricNormal;
    }
    float inverseDeterminant = 1.0 / determinant;
    float3 edge0 = p1 - p0;
    float3 edge1 = p2 - p0;
    float3 tangentObject = (edge0 * uvEdge1.y - edge1 * uvEdge0.y) *
                           inverseDeterminant;
    float3 tangent = normalize(mul((float3x3)ObjectToWorld3x4(),
                                   tangentObject));
    tangent = normalize(tangent - geometricNormal *
                        dot(tangent, geometricNormal));
    float3 bitangent = normalize(cross(geometricNormal, tangent));
    if (determinant < 0.0)
    {
        bitangent = -bitangent;
    }
    float3 worldEdge0 = mul((float3x3)ObjectToWorld3x4(), edge0);
    float3 worldEdge1 = mul((float3x3)ObjectToWorld3x4(), edge1);
    float2 textureSize = float2(surface.textureWidth, surface.textureHeight);
    float texelsPerWorldUnit = max(
        length(uvEdge0 * textureSize) / max(length(worldEdge0), 0.0001),
        length(uvEdge1 * textureSize) / max(length(worldEdge1), 0.0001));
    float viewportHeight = max(1.0, (float)DispatchRaysDimensions().y);
    float worldPixelFootprint = 2.0 * max(rayDistance, NearPlane) *
                                TanHalfFieldOfView / viewportHeight;
    /* A 1.35-pixel support covers the rotating sample lattice and provides a
       conservative ray-cone approximation at grazing angles. */
    float filterTexels = max(1.0,
        worldPixelFootprint * texelsPerWorldUnit * 1.35);
    float3 detail = SampleGeneratedNormal(surface, uv, filterTexels);
    return normalize(tangent * detail.x + bitangent * detail.y +
                     geometricNormal * detail.z);
}

float GeneratedReliefResponse(float3 shadingNormal,
                              float3 geometricNormal,
                              float3 viewDirection)
{
    /* A normal map can disappear under flat/ambient lighting because its
       conventional Lambert response has no directional contrast to alter.
       Retain a small, bounded micro-cavity response from the difference
       between the generated and geometric normals, then add only the
       view-facing portion of that difference. This keeps panel relief legible
       in neutral light without turning it into transport geometry. */
    float agreement = saturate(dot(shadingNormal, geometricNormal));
    float slopeSignal = saturate((1.0 - agreement) * 7.5);
    float geometricFacing = saturate(dot(geometricNormal, viewDirection));
    float detailedFacing = saturate(dot(shadingNormal, viewDirection));
    float facingContrast = clamp(
        (detailedFacing - geometricFacing) * 0.44, -0.085, 0.085);
    float cavity = slopeSignal * 0.14;
    return clamp(1.0 - cavity + facingContrast, 0.76, 1.14);
}

float3 EvaluateDirectLighting(float3 hitPosition, float3 shadingNormal,
                              float3 geometricNormal, float3 viewDirection,
                              uint bounceDepth, float materialRoughness)
{
    /* Never offset a visibility ray along texture-derived detail.  Doing so
       makes a sub-texel normal change cross the source triangle and produces
       the apparent sparkling/self-shadowing seen while the camera moves. */
    float3 origin = hitPosition + geometricNormal * ShadowNormalBias;
    /* Every mission's captured background is an environment emitter. Bright
       suns and nebulae therefore illuminate geometry even when no legacy HSF
       point/directional light was authored for them. */
    /* Broad hemispheric fill gives tangent-space relief a stable response on
       the side facing away from the authored sun. This is deliberately much
       wider and weaker than a light source: it reveals form under ambient
       illumination without creating a second visible sun or casting fake
       shadows. The direction is world-stable, so camera motion cannot make
       the normal detail swim. */
    const float3 ambientFillDirection = normalize(float3(-0.31, 0.74, 0.60));
    float ambientNormalResponse = lerp(0.72, 1.08,
        saturate(dot(shadingNormal, ambientFillDirection) * 0.5 + 0.5));
    float3 lighting = (float3(0.018, 0.018, 0.018) * ambientNormalResponse +
                      SampleMissionEnvironment(shadingNormal) * 0.32) *
                      EnvironmentStrength;
    /* A stable environment reflection makes generated panel relief readable
       between strong direct lights. It is evaluated only on the primary hit,
       so texture detail cannot steer recursive transport or reintroduce the
       old camera-motion flicker. */
    if (bounceDepth == 0 && SurfaceReflectivity > 0.0001)
    {
        float3 reflectedDirection = reflect(-viewDirection, shadingNormal);
        float reflectionGain = lerp(0.20, 0.055, materialRoughness) *
                               SurfaceReflectivity;
        lighting += SampleMissionEnvironment(reflectedDirection) *
                    EnvironmentStrength * reflectionGain;
    }
    /* Do not silently drop shadow-casting lights on secondary transport.
       A busy battle may contain many simultaneous beam/engine/weapon emitters;
       every uploaded light remains eligible for visibility/shadow testing. */
    uint evaluatedLights = LightCount;

    [loop]
    for (uint index = 0; index < evaluatedLights; ++index)
    {
        SceneLight light = Lights[index];
        bool shipEmissiveProxy = light.source > 5.5 && light.source < 6.5;
        /* Ship-emissive point proxies are only a nearby direct-light
           approximation. The actual emissive mesh still contributes radiance
           whenever transport rays hit it, so proxies do not need to multiply
           recursively across every bounce inside a production bay. */
        if (shipEmissiveProxy && bounceDepth > 0)
        {
            continue;
        }
        if (light.type == LIGHT_AMBIENT)
        {
            lighting += light.color * light.intensity * ambientNormalResponse;
            continue;
        }

        float3 lightVector;
        float maximumDistance;
        float attenuation = 1.0;
        if (light.type == LIGHT_DIRECTIONAL)
        {
            lightVector = normalize(light.direction);
            maximumDistance = MaximumRayDistance;
        }
        else
        {
            float3 lightPosition = light.position;
            if (light.type == LIGHT_LINE)
            {
                float3 segment = light.endPosition - light.position;
                float segmentLengthSquared = dot(segment, segment);
                float along = segmentLengthSquared > 0.0001
                    ? saturate(dot(hitPosition - light.position, segment) /
                               segmentLengthSquared)
                    : 0.0;
                lightPosition += segment * along;
            }
            float3 delta = lightPosition - hitPosition;
            float distanceToLight = length(delta);
            if (distanceToLight <= 0.001 || distanceToLight >= light.radius)
            {
                continue;
            }
            lightVector = delta / distanceToLight;
            maximumDistance = distanceToLight - 0.08;
            float range = saturate(1.0 - distanceToLight / light.radius);
            attenuation = range * range;
            /* Engine glows are tight local emitters.  A steeper smooth cutoff
               prevents a distant asteroid from receiving a large clipped disc
               of engine colour while preserving strong illumination near the
               actual nozzle. source==1 is HW_MODERN_LIGHT_ENGINE. */
            if (light.source > 0.5 && light.source < 1.5)
            {
                attenuation *= range * range;
            }

            if (light.type == LIGHT_SPOT)
            {
                float spotCosine = dot(-lightVector,
                                       normalize(light.direction));
                float innerCosine = max(light.coneCos, light.edgeCos);
                float outerCosine = min(light.coneCos, light.edgeCos);
                attenuation *= smoothstep(outerCosine, innerCosine,
                                          spotCosine);
            }
        }

        float lambert = saturate(dot(shadingNormal, lightVector));
        if (lambert > 0.0001)
        {
            uint shadowSamples = light.type == LIGHT_DIRECTIONAL
                ? SunShadowSamples : LocalShadowSamples;
            float shadowRadius = light.type == LIGHT_DIRECTIONAL
                ? SunShadowAngularRadius : LocalShadowAngularRadius;
            /* The proxy uses one gross-occlusion visibility ray and skips
               the normal local multi-sample/contact-shadow path. This keeps
               nearby bay illumination while bounding worst-case cost. */
            float visibility;
            if (shipEmissiveProxy)
            {
                float visible = IsVisible(origin, lightVector, maximumDistance)
                    ? 1.0 : 0.0;
                visibility = saturate(1.0 - (1.0 - visible) * ShadowStrength);
            }
            else
            {
                visibility = ShadowVisibility(
                    origin, lightVector, maximumDistance, shadowSamples,
                    shadowRadius, hitPosition, index);
            }
            if (visibility <= 0.0001)
                continue;
            float diffuseEnergy = light.intensity * attenuation * lambert * visibility;
            lighting += light.color * diffuseEnergy;
            if (bounceDepth == 0 && SurfaceReflectivity > 0.0001)
            {
                float3 halfVector = normalize(lightVector + viewDirection);
                float exponent = lerp(112.0, 7.0, materialRoughness);
                float normalHighlight = pow(
                    saturate(dot(shadingNormal, halfVector)), exponent);
                float grazing = 1.0 - saturate(
                    dot(shadingNormal, viewDirection));
                float fresnel = 0.16 + 0.84 * grazing * grazing *
                                grazing * grazing * grazing;
                float specular = normalHighlight * fresnel *
                                 SurfaceReflectivity *
                                 (1.0 - materialRoughness * 0.55);
                lighting += light.color *
                    (light.intensity * attenuation * specular * visibility);
            }
        }
    }
    return lighting;
}

/* Streamline DLSS-RR guide recommendation (Ray Tracing Gems, Ch. 32):
   convert the engine's base specular reflectance into the view/roughness-aware
   specular albedo expected by Ray Reconstruction. */
float3 RrSpecularAlbedo(float3 specularColor, float linearRoughness, float noV)
{
    noV = abs(noV);
    float alpha = linearRoughness * linearRoughness;
    float4 X = float4(1.0, noV, noV * noV, noV * noV * noV);
    float4 Y = float4(1.0, alpha, alpha * alpha, alpha * alpha * alpha);
    float2x2 M1 = float2x2(0.99044, -1.28514, 1.29678, -0.755907);
    float3x3 M2 = float3x3(1.0, 2.92338, 59.4188,
                           20.3225, -27.0302, 222.592,
                           121.563, 626.13, 316.627);
    float2x2 M3 = float2x2(0.0365463, 3.32707, 9.0632, -9.04756);
    float3x3 M4 = float3x3(1.0, 3.59685, -1.36772,
                           9.04401, -16.3174, 9.22949,
                           5.56589, 19.7886, -20.2123);
    float bias = dot(mul(M1, X.xy), Y.xy) *
                 rcp(dot(mul(M2, X.xyw), Y.xyw));
    float scale = dot(mul(M3, X.xy), Y.xy) *
                  rcp(dot(mul(M4, X.xzw), Y.xyw));
    bias *= saturate(specularColor.g * 50.0);
    return saturate(specularColor * max(0.0, scale) + max(0.0, bias));
}

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    float2 currentJitter = float2(ViewToWorldRow0.w, ViewToWorldRow1.w);
    float2 previousJitter = float2(PreviousWorldToViewRow0.w,
                                   PreviousWorldToViewRow1.w);
    float2 uv = (float2(pixel) + 0.5 + currentJitter) /
                float2(dimensions);
    float2 screen = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    /* Use a frame-varying stochastic sequence. The previous screen-fixed seed
       repeated the same Monte-Carlo path every frame, so neither the internal
       temporal accumulator nor DLSS Ray Reconstruction could actually average
       independent noise samples. Motion/depth reprojection now keeps this
       temporal sequence stable in object space while allowing real convergence. */
    uint temporalScramble = Hash(FrameSeed * 0x9e3779b9u + 0x68bc21ebu);
    uint seed = Hash((pixel.x + pixel.y * dimensions.x) ^ temporalScramble);
    float3 sampleLighting = 0.0;
    uint primaryHit = 0;
    float primaryDepth = 1.0;
    float2 primaryMotion = 0.0;
    float primaryHistoryDepth = -1.0;
    float primaryPreviousHistoryDepth = -1.0;
    float3 primaryAlbedo = 0.0;
    float3 primarySpecularAlbedo = 0.0;
    float4 primaryNormalRoughness = float4(0.0, 0.0, 1.0, 1.0);
    uint sampleCount = max(1u, SamplesPerPixel);

    [loop]
    for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        RayDesc ray;
        ray.Origin = float3(0.0, 0.0, 0.0);
        ray.Direction = normalize(float3(
            screen.x * TanHalfFieldOfView * AspectRatio,
            screen.y * TanHalfFieldOfView, -1.0));
        ray.TMin = NearPlane;
        ray.TMax = MaximumRayDistance;

        RayPayload payload;
        payload.lighting = 0.0;
        payload.hit = 0;
        payload.depth = 0;
        payload.seed = Hash(seed + sampleIndex * 0x85ebca6bu);
        payload.depthValue = 1.0;
        payload.motionPixels = 0.0;
        payload.historyDepth = -1.0;
        payload.previousHistoryDepth = -1.0;
        payload.primaryAlbedo = 0.0;
        payload.primarySpecularAlbedo = 0.0;
        payload.primaryNormalRoughness = float4(0.0, 0.0, 1.0, 1.0);
        TraceRay(Scene, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES,
                 INSTANCE_MASK_PRIMARY, 0, 2, 0, ray, payload);
        sampleLighting += payload.lighting;
        if (sampleIndex == 0)
        {
            primaryHit = payload.hit;
            primaryDepth = payload.depthValue;
            primaryMotion = payload.motionPixels;
            if (payload.hit == 0)
                primaryMotion *= float2(dimensions);
            primaryHistoryDepth = payload.historyDepth;
            primaryPreviousHistoryDepth = payload.previousHistoryDepth;
            primaryAlbedo = payload.primaryAlbedo;
            primarySpecularAlbedo = payload.primarySpecularAlbedo;
            primaryNormalRoughness = payload.primaryNormalRoughness;
        }
    }

    /* Bound rare high-energy indirect samples before temporal integration.
       This is a radiance clamp, not a post-process grain filter. */
    float4 current = float4(min(sampleLighting / sampleCount, 12.0),
                            primaryHit != 0 ? primaryHistoryDepth : -1.0);
    float4 accumulated = current;
    /* DLSS Ray Reconstruction must receive the current noisy frame rather
       than a signal already temporally filtered by HomeworldModern.  When RR
       is active NVIDIA owns the temporal denoising/reconstruction history. */
    if (RayReconstructionEnabled == 0 &&
        ResetAccumulation == 0 && AccumulationFrame > 0)
    {
        /* Motion vectors intentionally exclude projection jitter for
           Streamline. The path tracer's own history is a physical texture,
           however, so move between this frame's and the previous frame's
           sample lattices when locating its history texel. */
        float2 previousPosition = float2(pixel) + primaryMotion +
                                  currentJitter - previousJitter;
        int2 historyBase = int2(floor(previousPosition));
        bool inBounds = all(historyBase >= int2(0, 0)) &&
                        all(historyBase + int2(1, 1) < int2(dimensions));
        if (inBounds)
        {
            /* History depth is stored in R16G16B16A16_FLOAT, so retain enough
               tolerance for half-float quantization while rejecting nearby
               but unrelated silhouettes.  The previous tolerance expanded
               to several percent at gameplay distances and allowed LOD and
               occlusion edges to carry lighting across surfaces. */
            float depthTolerance = max(
                0.010, abs(primaryPreviousHistoryDepth) * 0.0015);
            float2 historyFraction = frac(previousPosition);
            float historyWeights[4] = {
                (1.0 - historyFraction.x) * (1.0 - historyFraction.y),
                historyFraction.x * (1.0 - historyFraction.y),
                (1.0 - historyFraction.x) * historyFraction.y,
                historyFraction.x * historyFraction.y
            };
            int2 historyOffsets[4] = {
                int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1)
            };
            float4 history = 0.0;
            float acceptedHistoryWeight = 0.0;
            [unroll]
            for (uint historyIndex = 0u; historyIndex < 4u; ++historyIndex)
            {
                float4 historySample =
                    AccumulationHistory[historyBase + historyOffsets[historyIndex]];
                bool depthAgrees = historySample.a >= 0.0 &&
                    primaryPreviousHistoryDepth >= 0.0 &&
                    abs(historySample.a - primaryPreviousHistoryDepth) <
                        depthTolerance;
                float acceptedWeight = depthAgrees
                    ? historyWeights[historyIndex] : 0.0;
                history += historySample * acceptedWeight;
                acceptedHistoryWeight += acceptedWeight;
            }
            bool surfaceAgrees = current.a >= 0.0 &&
                acceptedHistoryWeight > 0.0001;
            if (surfaceAgrees)
            {
                history /= acceptedHistoryWeight;
                float currentLuminance = dot(current.rgb,
                    float3(0.2126, 0.7152, 0.0722));
                float historyLuminance = dot(history.rgb,
                    float3(0.2126, 0.7152, 0.0722));
                /* Clamp history symmetrically.  Only limiting bright history
                   allowed dark accumulated samples to trail behind a newly
                   illuminated or newly revealed surface. */
                float minimumHistory = max(0.0,
                    currentLuminance * 0.40 - 0.04);
                float maximumHistory = currentLuminance * 2.25 + 0.12;
                if (historyLuminance > maximumHistory)
                {
                    history.rgb *= maximumHistory /
                                   max(historyLuminance, 0.0001);
                    historyLuminance = maximumHistory;
                }
                else if (historyLuminance < minimumHistory)
                {
                    /* A completely black history sample cannot be repaired by
                       scaling. Seed it from the current chroma instead so a
                       newly revealed lit surface does not retain a dark trail. */
                    history.rgb = historyLuminance > 0.0001
                        ? history.rgb * (minimumHistory / historyLuminance)
                        : current.rgb * (minimumHistory /
                                         max(currentLuminance, 0.0001));
                    historyLuminance = minimumHistory;
                }
                /* Clamp isolated current-frame fireflies against reprojected
                   history before blending. Stable bright lighting converges
                   normally, while one-frame Monte-Carlo spikes cannot punch
                   through the denoiser as colored surface grain. */
                float maximumCurrent = historyLuminance * 2.75 + 0.24;
                if (currentLuminance > maximumCurrent)
                {
                    current.rgb *= maximumCurrent /
                                   max(currentLuminance, 0.0001);
                }
                float motionLength = length(primaryMotion);
                /* Long history is valuable only when the reprojection is
                   effectively stationary.  During orbit/zoom, retaining
                   86-95% of prior radiance produced multi-frame silhouettes
                   and made subpixel LOD transitions shimmer. */
                float responseLimit = motionLength < 0.10 ? 0.965 :
                                      (motionLength < 1.0 ? 0.90 :
                                      (motionLength < 4.0 ? 0.78 : 0.60));
                /* Depth agreement alone cannot prove that normal-driven
                   lighting is still current during rotation. Reduce history
                   when the reprojected radiance disagrees, preventing an old
                   panel highlight from following the surface for several
                   frames. Stationary convergence remains unchanged. */
                if (motionLength >= 0.10 && NormalDetailStrength > 0.0001)
                {
                    float luminanceScale = max(
                        max(currentLuminance, historyLuminance), 0.08);
                    float lightingDisagreement =
                        abs(currentLuminance - historyLuminance) /
                        luminanceScale;
                    float responsiveHistory = lerp(
                        1.0, 0.42,
                        smoothstep(0.08, 0.55, lightingDisagreement));
                    responseLimit *= responsiveHistory;
                }
                float progressiveWeight = (float)AccumulationFrame /
                    ((float)AccumulationFrame + 1.0);
                accumulated = lerp(current, history,
                    min(progressiveWeight, responseLimit));
            }
        }
    }
    Accumulation[pixel] = accumulated;

    float3 exposed = accumulated.rgb * Exposure;
    float3 toneMapped = exposed / (1.0 + exposed);
    RayLighting[pixel] = float4(saturate(toneMapped),
                                primaryHit != 0 ? primaryHistoryDepth : -1.0);

    /* Streamline consumes the HUD-less full-scene color as a native D3D
       top-down texture. Depth and motion must use that exact same orientation
       and pixel convention; mixing the old bottom-up presenter convention with
       the new full-scene input flips the result and destroys temporal
       correspondence. Motion is already expressed in D3D UV/pixel space. */
    SceneDepth[pixel] = primaryDepth;
    SceneMotion[pixel] = primaryMotion;
    /* The diffuse guide also feeds the hybrid presenter's foreground-emissive
       protection, so keep it current even when DLSS-RR itself is disabled. */
    DiffuseAlbedoGuide[pixel] = float4(saturate(primaryAlbedo), 1.0);
    if (RayReconstructionEnabled != 0)
    {
        SpecularAlbedoGuide[pixel] = float4(saturate(primarySpecularAlbedo), 1.0);
        NormalRoughnessGuide[pixel] = float4(
            normalize(primaryNormalRoughness.xyz),
            saturate(primaryNormalRoughness.w));
    }
}

[shader("miss")]
void PrimaryMiss(inout RayPayload payload)
{
    payload.lighting = SampleMissionEnvironment(WorldRayDirection());
    payload.hit = 0;
    payload.historyDepth = -1.0;
    payload.previousHistoryDepth = -1.0;
    if (payload.depth == 0)
    {
        payload.depthValue = 1.0;
        payload.motionPixels = 0.0;
        if (ViewHistoryValid != 0)
        {
            float3 currentViewDirection = normalize(WorldRayDirection());
            float3 worldDirection = ViewToWorldDirection(currentViewDirection);
            float3 previousViewDirection =
                WorldToPreviousViewDirection(worldDirection);
            if (currentViewDirection.z < -0.0001 &&
                previousViewDirection.z < -0.0001)
            {
                float2 currentUv = ViewDirectionToUv(currentViewDirection);
                float2 previousUv = ViewDirectionToUv(previousViewDirection);
                /* Stored as UV delta in the miss shader; RayGeneration has
                   the dispatch dimensions and converts this miss-only value
                   to the pixel units used by Streamline/history reprojection. */
                payload.motionPixels = previousUv - currentUv;
            }
        }
        payload.primaryAlbedo = 0.0;
        payload.primarySpecularAlbedo = 0.0;
        payload.primaryNormalRoughness = float4(0.0, 0.0, 1.0, 1.0);
    }
}

[shader("miss")]
void ShadowMiss(inout RayPayload payload)
{
    payload.hit = 0;
    payload.historyDepth = -1.0;
    payload.previousHistoryDepth = -1.0;
}

[shader("closesthit")]
void ShadowClosestHit(inout RayPayload payload,
                      in BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1;
}

[shader("anyhit")]
void SurfaceAnyHit(inout RayPayload payload,
                   in BuiltInTriangleIntersectionAttributes attributes)
{
    TriangleSurface surface;
    float2 uv;
    float3 baseColor;
    float3 emission;
    float opacity;
    EvaluateSurface(attributes, surface, uv, baseColor, emission, opacity);
    if (opacity <= 0.05)
    {
        IgnoreHit();
    }
}

[shader("closesthit")]
void PrimaryClosestHit(inout RayPayload payload,
                       in BuiltInTriangleIntersectionAttributes attributes)
{
    uint3 vertexIndices = Triangles[PrimitiveIndex()];
    float3 p0 = Vertices[vertexIndices.x];
    float3 p1 = Vertices[vertexIndices.y];
    float3 p2 = Vertices[vertexIndices.z];
    float3 geometricNormal = normalize(cross(p1 - p0, p2 - p0));
    geometricNormal = normalize(mul((float3x3)ObjectToWorld3x4(),
                                    geometricNormal));
    if (dot(geometricNormal, WorldRayDirection()) > 0.0)
    {
        geometricNormal = -geometricNormal;
    }

    TriangleSurface surface;
    float2 uv;
    float3 baseColor;
    float3 emission;
    float opacity;
    EvaluateSurface(attributes, surface, uv, baseColor, emission, opacity);
    float4 orm = SampleSurfaceChannel(surface, uv, 3u);
    bool authoredOrm = orm.a > 0.5;
    float materialAo = authoredOrm ? saturate(orm.r) : 1.0;
    float materialRoughness = authoredOrm ? saturate(orm.g)
                                          : saturate(SurfaceRoughness);
    float materialMetallic = authoredOrm ? saturate(orm.b) : 0.0;
    /* Generated normals are a primary-surface shading detail, not transport
       geometry.  Secondary hits use the authored mesh normal so tiny texture
       changes cannot redirect an entire indirect-light path. */
    float3 shadingNormal = payload.depth == 0
        ? ApplyGeneratedNormal(geometricNormal, p0, p1, p2, surface, uv,
                               RayTCurrent())
        : geometricNormal;
    if (dot(shadingNormal, WorldRayDirection()) > 0.0)
    {
        shadingNormal = -shadingNormal;
    }

    float3 hitPosition = WorldRayOrigin() +
                         WorldRayDirection() * RayTCurrent();
    float3 lighting = EvaluateDirectLighting(
        hitPosition, shadingNormal, geometricNormal,
        normalize(-WorldRayDirection()), payload.depth, materialRoughness);
    lighting *= lerp(0.35, 1.0, materialAo);
    if (payload.depth == 0 && NormalDetailStrength > 0.0001)
    {
        lighting *= GeneratedReliefResponse(
            shadingNormal, geometricNormal,
            normalize(-WorldRayDirection()));
    }
    lighting += emission;

    if (payload.depth + 1u < max(1u, MaximumBounces))
    {
        float continuation = payload.depth >= 2u
            ? max(0.25, max(baseColor.r, max(baseColor.g, baseColor.b)))
            : 1.0;
        if (Random01(payload.seed) <= continuation)
        {
            RayDesc bounceRay;
            bounceRay.Origin = hitPosition + geometricNormal * 0.08;
            bounceRay.Direction = CosineHemisphere(geometricNormal,
                                                   payload.seed);
            bounceRay.TMin = 0.05;
            bounceRay.TMax = MaximumRayDistance;

            RayPayload bounce;
            bounce.lighting = 0.0;
            bounce.hit = 0;
            bounce.depth = payload.depth + 1u;
            bounce.seed = payload.seed;
            bounce.depthValue = 1.0;
            bounce.motionPixels = 0.0;
            bounce.historyDepth = -1.0;
            bounce.previousHistoryDepth = -1.0;
            bounce.primaryAlbedo = 0.0;
            bounce.primarySpecularAlbedo = 0.0;
            bounce.primaryNormalRoughness = float4(0.0, 0.0, 1.0, 1.0);
            TraceRay(Scene, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES,
                     INSTANCE_MASK_TRANSPORT, 0, 2, 0, bounceRay, bounce);
            /* This is a hybrid over the classic raster image rather than a
               fully energy-calibrated material pipeline.  Bound and attenuate
               diffuse return energy before recursion so a single emissive or
               direct-light sample cannot flash an entire ship. */
            float bounceLuminance = dot(bounce.lighting,
                float3(0.2126, 0.7152, 0.0722));
            float3 boundedBounce = bounce.lighting;
            const float maximumBounceLuminance = 2.25;
            if (bounceLuminance > maximumBounceLuminance)
            {
                boundedBounce *= maximumBounceLuminance /
                                 bounceLuminance;
            }
            const float indirectDiffuseGain = 0.24;
            lighting += baseColor * boundedBounce * indirectDiffuseGain /
                        continuation;
            payload.seed = bounce.seed;
        }
    }

    if (payload.depth == 0)
    {
        /* RR guide data describes the primary material only.  The renderer has
           no per-material metalness channel, so derive a conservative specular
           albedo from the same reflectivity model used by the path shader. */
        const float dielectricF0 = 0.04;
        float reflectivity = saturate(SurfaceReflectivity * 0.5);
        float roughness = materialRoughness;
        float3 viewDirection = normalize(-WorldRayDirection());
        float3 specularColor = saturate(
            lerp(float3(dielectricF0, dielectricF0, dielectricF0),
                 baseColor, max(materialMetallic, reflectivity * 0.35)));
        payload.primaryAlbedo = saturate(baseColor);
        payload.primarySpecularAlbedo = RrSpecularAlbedo(
            specularColor, roughness, dot(shadingNormal, viewDirection));
        payload.primaryNormalRoughness = float4(
            normalize(shadingNormal), roughness);

        float3 barycentric = float3(1.0 - attributes.barycentrics.x -
                                          attributes.barycentrics.y,
                                    attributes.barycentrics.x,
                                    attributes.barycentrics.y);
        float3 localPosition = p0 * barycentric.x +
                               p1 * barycentric.y +
                               p2 * barycentric.z;
        InstanceMotion motion = InstanceMotions[InstanceID()];
        float4 local = float4(localPosition, 1.0);
        float3 previousPosition = float3(dot(motion.previousRow0, local),
                                         dot(motion.previousRow1, local),
                                         dot(motion.previousRow2, local));
        float2 currentUv = ViewPositionToUv(hitPosition);
        float2 previousUv = previousPosition.z < -NearPlane
            ? ViewPositionToUv(previousPosition) : currentUv;
        payload.depthValue = DeviceDepth(hitPosition.z);
        payload.historyDepth = log2(1.0 + max(RayTCurrent(), 0.0));
        payload.previousHistoryDepth = previousPosition.z < -NearPlane
            ? log2(1.0 + length(previousPosition)) : -1.0;
        payload.motionPixels = (previousUv - currentUv) *
                               float2(DispatchRaysDimensions().xy);
    }

    payload.lighting = max(lighting, 0.0);
    payload.hit = 1;
}
