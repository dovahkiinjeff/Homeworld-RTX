#ifndef HW_MODERN_GRAPHICS_H
#define HW_MODERN_GRAPHICS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HWModernRaytracingTier
{
    HW_MODERN_RAYTRACING_UNAVAILABLE = 0,
    HW_MODERN_RAYTRACING_TIER_1_0 = 10,
    HW_MODERN_RAYTRACING_TIER_1_1 = 11
} HWModernRaytracingTier;

typedef enum HWModernAntiAliasingMode
{
    HW_MODERN_AA_OFF = 0,
    HW_MODERN_AA_DLAA = 1,
    HW_MODERN_AA_FXAA = 2,
    HW_MODERN_AA_DLSS_QUALITY = 3,
    HW_MODERN_AA_DLSS_BALANCED = 4,
    HW_MODERN_AA_DLSS_PERFORMANCE = 5,
    HW_MODERN_AA_DLSS_ULTRA_PERFORMANCE = 6,
    HW_MODERN_AA_AUTO_QUALITY = 7,
    HW_MODERN_AA_AUTO_NATIVE = 8,
    HW_MODERN_AA_NATIVE_TAA = 9,
    HW_MODERN_AA_FSR_NATIVE = 10,
    HW_MODERN_AA_FSR_QUALITY = 11,
    HW_MODERN_AA_FSR_BALANCED = 12,
    HW_MODERN_AA_FSR_PERFORMANCE = 13,
    HW_MODERN_AA_FSR_ULTRA_PERFORMANCE = 14,
    HW_MODERN_AA_XESS_AA = 15,
    HW_MODERN_AA_XESS_QUALITY = 16,
    HW_MODERN_AA_XESS_BALANCED = 17,
    HW_MODERN_AA_XESS_PERFORMANCE = 18,
    HW_MODERN_AA_XESS_ULTRA_PERFORMANCE = 19
} HWModernAntiAliasingMode;

typedef enum HWModernFrameGenerationMode
{
    HW_MODERN_FRAME_GENERATION_OFF = 0,
    /* Built-in D3D12 interpolation; works on NVIDIA, AMD and Intel hardware. */
    HW_MODERN_FRAME_GENERATION_UNIVERSAL_2X = 1
} HWModernFrameGenerationMode;

typedef struct HWModernGraphicsCapabilities
{
    int d3d12Available;
    int hardwareAdapter;
    unsigned int vendorId;
    unsigned int deviceId;
    unsigned long long dedicatedVideoMemoryBytes;
    unsigned int maximumFeatureLevel;
    HWModernRaytracingTier raytracingTier;
    unsigned int variableRateShadingTier;
    int additionalShadingRatesSupported;
    char adapterName[128];
} HWModernGraphicsCapabilities;

typedef struct HWModernMapLightEmitter
{
    unsigned int type;
    /* Untouched values authored in the HSF file. */
    float position[3];
    float orientation[3];
    /* Position/direction after the legacy LightWave-to-engine conversion.
       This lets DXR reproduce the visible scene while retaining the source. */
    float renderPosition[3];
    float renderDirection[3];
    float coneAngle;
    float edgeAngle;
    float color[3];
    float intensity;
} HWModernMapLightEmitter;

/* RTX-0077: mission-local lights authored live by the Shift+F12 editor.
   These are deliberately separate from immutable HSF map emitters so editing
   a mission never mutates or replaces the retail map-light set. */
typedef enum HWModernMissionLightType
{
    HW_MODERN_MISSION_LIGHT_POINT = 1,
    HW_MODERN_MISSION_LIGHT_SPOT = 2,
    HW_MODERN_MISSION_LIGHT_AMBIENT = 3
} HWModernMissionLightType;

typedef struct HWModernMissionLightEmitter
{
    unsigned int type;
    unsigned int enabled;
    float position[3];
    float direction[3];
    float color[3];
    float intensity;
    float radius;
    float coneAngle;
    float edgeAngle;
} HWModernMissionLightEmitter;

/* RTX-0086: artist-authored real-time volumetric dust.  These values are
   renderer-neutral and are produced by the Shift+F11 map editor.  The D3D12
   presenter raymarches the density field in world space, so clouds remain
   stable while the camera moves and continue rendering after the editor UI is
   closed. */
typedef enum HWModernVolumetricDustShape
{
    HW_MODERN_DUST_SPHERE = 0,
    HW_MODERN_DUST_BOX = 1,
    HW_MODERN_DUST_ELLIPSOID = 2
} HWModernVolumetricDustShape;

enum
{
    HW_MODERN_DUST_RECEIVE_MISSION_KEY = 1u << 0,
    HW_MODERN_DUST_RECEIVE_LOCAL_LIGHTS = 1u << 1,
    HW_MODERN_DUST_CAST_VOLUMETRIC_SHADOW = 1u << 2
};

typedef struct HWModernVolumetricDustVolume
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
} HWModernVolumetricDustVolume;

/* RTX-0087: moving ships disturb volumetric dust in world space.  Identity is
   stable only for the lifetime of a runtime object and is used solely to
   connect consecutive frame samples into a recovering wake. */
typedef struct HWModernVolumetricDustInteractor
{
    unsigned long long identity;
    float position[3];
    float radius;
    float velocity[3];
    float strength;
} HWModernVolumetricDustInteractor;

typedef enum HWModernDynamicLightSource
{
    HW_MODERN_LIGHT_ENGINE = 1,
    HW_MODERN_LIGHT_MUZZLE_FLASH = 2,
    HW_MODERN_LIGHT_WEAPON = 3,
    HW_MODERN_LIGHT_ION_BEAM = 4,
    HW_MODERN_LIGHT_EXPLOSION = 5,
    HW_MODERN_LIGHT_SHIP_EMISSIVE = 6,
    HW_MODERN_LIGHT_NAV = 7,
    HW_MODERN_LIGHT_WEAPON_IMPACT = 8
} HWModernDynamicLightSource;

typedef enum HWModernDynamicLightShape
{
    HW_MODERN_LIGHT_POINT = 1,
    HW_MODERN_LIGHT_LINE = 2,
    HW_MODERN_LIGHT_SURFACE = 3
} HWModernDynamicLightShape;

/* World-space light submitted by a legacy rendering system.  `endPosition`
   is used by line lights (ion/laser beams); point lights use position/radius.
   The values are deliberately renderer-neutral so the same scene contract can
   feed both DXR and a future compute path tracer. */
typedef struct HWModernDynamicLightEmitter
{
    unsigned int source;
    unsigned int shape;
    float position[3];
    float endPosition[3];
    float direction[3];
    float color[3];
    float intensity;
    float radius;
} HWModernDynamicLightEmitter;

/* Per-polygon material data captured from the GEO object. UVs retain all
   three authored corners so the closest-hit shader can sample the real LIF
   alpha/emissive texels using barycentrics instead of an object-wide proxy. */
typedef struct HWModernTriangleSurface
{
    const void *materialIdentity;
    float baseColor[3];
    float emissiveColor[3];
    float emissiveIntensity;
    float opacity;
    float uv[6];
    unsigned int flags;
} HWModernTriangleSurface;

/* Queries the highest-performance hardware adapter that can create a D3D12
   device. Returns non-zero when a usable D3D12 adapter was found. */
int hwModernGraphicsQueryCapabilities(HWModernGraphicsCapabilities *capabilities);

/* Writes a concise, stable capability summary to stderr. */
void hwModernGraphicsLogCapabilities(void);

/* Starts the native D3D12 visible presenter. It owns a hardware direct queue
   and a triple-buffered DXGI flip-model swapchain. The legacy scene is
   captured from OpenGL until its draw systems have migrated to D3D12. */
int hwModernGraphicsInitialize(void *nativeWindow, unsigned int width,
                               unsigned int height);

/* Records one D3D12 frame. A size change recreates the HDR target before the
   new frame begins. These calls are safe no-ops when initialization failed. */
void hwModernGraphicsBeginFrame(unsigned int width, unsigned int height);

/* Captures the completed OpenGL back buffer into the active D3D12 upload
   frame. Called immediately before the old SDL swap point. */
int hwModernGraphicsCaptureOpenGLFrame(void);

/* Owns the complete legacy flush point while the DXGI visible presenter is
   active. If a normal modern frame is already open this captures into it.
   Legacy UI/loading paths that flush outside the main render loop get a
   short standalone modern frame instead of accidentally swapping the hidden
   OpenGL window behind the flip-model swapchain. Returns non-zero when the
   flush was consumed by the modern presenter. */
int hwModernGraphicsFlushOpenGLFrame(unsigned int width, unsigned int height);

/* Captures the completed 3D world before region/UI drawing continues. The
   final presenter compares it with the completed frame and protects changed
   UI pixels from ray-light modulation. */
int hwModernGraphicsCaptureOpenGLWorld(void);

/* Captures the luminous mission dome before planets/world geometry, ships,
   trails, weapons and UI. God rays and environment lighting use this frame so
   planets remain occluders and moving FX cannot become atmospheric sources. */
/* Binds the one mission-owned god-ray source selected from the map sky.
   The source remains immutable until mapIdentity changes. */
void hwModernGraphicsSetGodRayWorldSource(
    int mapIdentity, int sourceValid, const float direction[3]);

/* Records a fullscreen native D3D12 mission-environment draw using the
   currently loaded 2:1 dual-paraboloid BC6H_UF16 texture. */
int hwModernGraphicsRenderMissionSky(unsigned int textureName, float fade);

int hwModernGraphicsCaptureOpenGLBackground(
    int mapIdentity, float verticalFieldOfViewDegrees, float aspectRatio,
    const float viewMatrix[16], const float projectionMatrix[16]);

/* Captures only the mission background plus planets/world geometry, before
   ships render their trails and other transient FX. God rays use this as the
   raster-only blocker layer while DXR supplies opaque ship/derelict hits. */
int hwModernGraphicsCaptureOpenGLOccluders(void);

void hwModernGraphicsEndFrame(void);

/* Waits for outstanding bridge work and releases all D3D12 resources. */
void hwModernGraphicsShutdown(void);

/* Returns non-zero while the D3D12 frame bridge is usable. */
int hwModernGraphicsIsActive(void);

/* Returns non-zero only when D3D12 owns visible window presentation. */
int hwModernGraphicsIsVisiblePresenterActive(void);

/* 0 disables VSync; 1 enables it; -1 currently falls back to ordinary VSync
   because DXGI flip presentation has no portable adaptive interval. */
void hwModernGraphicsSetVSync(int mode);

/* Runtime DXR controls. The enable call is safe before renderer startup and
   while the options menu is open. Strength is expressed as a linear scalar:
   0 disables only the lighting cast by FX/emissive sources, while their
   visible compatibility-renderer artwork remains unchanged. */
void hwModernGraphicsSetRaytracingEnabled(int enabled);
void hwModernGraphicsSetFxLightingStrength(float strength);
void hwModernGraphicsSetPathTracingSettings(unsigned int samplesPerPixel,
                                            unsigned int maximumBounces,
                                            float generatedNormalStrength);
/* Live values used by the Shift+F12 lighting editor.  All values are linear
   scalars except roughness, which is normalized from mirror-like 0 to diffuse
   1. Changes reset only the path-light history, never the game simulation. */
void hwModernGraphicsSetLightingEditorSettings(
    float environmentStrength, float authoredLightStrength,
    float surfaceReflectivity, float surfaceRoughness, float exposure);
/* Sky-derived global lighting controls. Primary sun is the same mission-owned
   coherent bright object that drives god rays; sky ambient is the average
   mission-sky color and never casts directional shadows. */
void hwModernGraphicsSetSkyLightingStrengths(
    float primarySunStrength, float skyAmbientStrength);
/* AAA ray-traced shadow controls used by Shift+F12. Angles are degrees,
   biases/distances are Homeworld world units. */
void hwModernGraphicsSetShadowSettings(
    float shadowStrength, float sunAngularRadiusDegrees,
    unsigned int sunSamples, unsigned int localSamples,
    float localAngularRadiusDegrees, float receiverBias, float normalBias,
    float contactStrength, float contactDistance, float maximumDistance);
void hwModernGraphicsSetMissionSkyLighting(
    int mapIdentity, int sourceValid, const float direction[3],
    const float sourceColor[3], const float ambientColor[3]);
void hwModernGraphicsSetAntiAliasingMode(int mode);
void hwModernGraphicsSetFrameGenerationMode(int mode);
int hwModernGraphicsIsDlaaActive(void);
void hwModernGraphicsSetPostProcessingSettings(
    float chromaticAberration, float motionBlur, float filmGrain,
    float godRays, float bloom);
/* Supplies the projected screen-space position and color of the one
   mission-owned sky emitter selected by ModernMissionBackdrop.  Coordinates
   use the OpenGL capture convention and may lie outside [0,1] while the
   source is just off screen.  This overrides only the per-frame shaft
   projection; the existing map-owned world direction remains authoritative
   for physical sky/key lighting. */
void hwModernGraphicsSetGodRaySource(float screenX, float screenY,
                                     float red, float green, float blue,
                                     int visible);
/* Deterministic final-output dithering used to hide 8-bit sky/lighting
   contouring without introducing animated film grain. */
void hwModernGraphicsSetOutputDitherStrength(float strength);

/* Streamline must install its D3D/DXGI interposer before the first DirectX
   call. Invoke this once at process entry; it remains a safe no-op on systems
   where the NVIDIA DLSS feature is unavailable. */
void hwModernGraphicsEarlyInitialize(void);

/* Preserves every authored HSF map emitter for the DXR scene, even though the
   compatibility OpenGL path can consume only its legacy subset. */
void hwModernGraphicsBeginMapLightUpdate(void);
void hwModernGraphicsSubmitMapLight(const HWModernMapLightEmitter *emitter);
void hwModernGraphicsEndMapLightUpdate(void);
/* RTX-0079: expose the immutable HSF map emitters to the live authoring layer
   so their local point/spot lights can be adopted into editable mission lights. */
unsigned int hwModernGraphicsGetMapLightCount(void);
int hwModernGraphicsGetMapLight(unsigned int index,
                                HWModernMapLightEmitter *outEmitter);

/* Replaces the live per-mission authoring light set atomically. The renderer
   copies the array immediately; callers retain ownership of the input. */
void hwModernGraphicsSetMissionAuthoringLights(
    const HWModernMissionLightEmitter *emitters, unsigned int count);
/* When enabled, local HSF point/spot/area records are replaced by their
   editable mission-light copies. Directional/ambient sky policy is unchanged. */
void hwModernGraphicsSetMissionAuthoringReplacesMapLights(int replace);
/* Replaces the complete authored volumetric-dust set atomically.  The
   renderer copies the array immediately; callers keep ownership. */
void hwModernGraphicsSetVolumetricDustVolumes(
    const HWModernVolumetricDustVolume *volumes, unsigned int count);
/* Global camera-distance attenuation authored from Shift+F11. Distance is the
   full-strength radius in world units; strength 0 disables fading. */
void hwModernGraphicsSetVolumetricDustFade(float distance, float strength);
/* Supplies three exact world-space camera rays reconstructed from the same
   model-view/projection matrices used to draw the visible frame.  rayUv00 is
   the presenter's (0,0) texel corner (screen bottom-left), rayUv10 is
   bottom-right and rayUv01 is top-left.  Interpolating these unnormalized
   near-plane vectors makes the volumetric pass pixel-identical to the raster
   camera and prevents any camera-facing/billboard drift. */
void hwModernGraphicsSetVolumetricDustCamera(
    const float eye[3], const float rayUv00[3], const float rayUv10[3],
    const float rayUv01[3]);
/* Freezes transient dust dynamics (ship-wake aging/recovery) while the game
   simulation is paused. Static authored density is always time invariant. */
void hwModernGraphicsSetVolumetricDustSimulationPaused(int paused);
/* Replaces the current-frame set of physical objects that can push dust. */
void hwModernGraphicsSetVolumetricDustInteractors(
    const HWModernVolumetricDustInteractor *interactors, unsigned int count);

/* The render task clears dynamic lights at the start of every frame.  Every
   visible engine, effect, projectile, beam and explosion re-submits its exact
   current world-space emitter while the legacy scene is drawn. */
void hwModernGraphicsSubmitDynamicLight(
    const HWModernDynamicLightEmitter *emitter);

/* Retains a transient emitter for the requested wall-clock duration. This is
   used for gameplay events such as gun fire that can occur between rendered
   frames; the light is then guaranteed to reach several DXR dispatches even
   when its legacy sprite lasts only one frame. */
void hwModernGraphicsTriggerDynamicLight(
    const HWModernDynamicLightEmitter *emitter, float lifetimeSeconds);

/* Preserve the identity and color of GEO materials authored as self-lit.
   Their actual triangles become emissive geometry when that mesh is migrated
   to DXR; this avoids approximating ship glow maps with unrelated point
   lights. */
void hwModernGraphicsRegisterEmissiveMaterial(
    const char *meshName, const char *materialName, const char *textureName,
    unsigned int fullAmbientColors, const float color[3], float intensity);

/* Registers the actual authored LIF texels for one GEO material. Each texel is
   two packed RGBA8 values: the visible/alpha sample and its emissive sample. */
void hwModernGraphicsRegisterSurfaceTexture(
    const void *materialIdentity, unsigned int width, unsigned int height,
    const unsigned int *surfaceRgba, const unsigned int *emissiveRgba,
    const unsigned int *normalRgba, const unsigned int *ormRgba);
void hwModernGraphicsRegisterSurfaceTextureNamed(
    const void *materialIdentity, const char *textureKey,
    unsigned int width, unsigned int height,
    const unsigned int *surfaceRgba, const unsigned int *emissiveRgba,
    const unsigned int *normalRgba, const unsigned int *ormRgba);
int hwModernGraphicsTryAliasSurfaceTexture(const void *materialIdentity,
                                           const char *textureKey);
/* Decode the top mip of a BC7/BC5/BC4 DDS into tightly packed RGBA8. The
   returned allocation belongs to the modern backend and must be released by
   hwModernGraphicsFreeDecodedDds. */
int hwModernGraphicsDecodeDds(const void *data, unsigned int size,
                              unsigned int *width, unsigned int *height,
                              unsigned char **rgba);
/* Same decode with a bounded one-consumer handoff cache. Two legacy systems
   requesting the same loose path can reuse one decoded allocation. */
int hwModernGraphicsDecodeDdsShared(const void *data, unsigned int size,
                                    const char *cacheKey,
                                    unsigned int *width, unsigned int *height,
                                    unsigned char **rgba);
void hwModernGraphicsFreeDecodedDds(void *rgba);
/* Upload every BC7 mip from a DDS directly into the currently bound native
   D3D12 compatibility texture. Returns zero when direct upload is unavailable
   or the DDS format is not suitable for the raster color path. */
int hwModernGraphicsUploadBoundDds(const void *data, unsigned int size);
/* Drop the current material-pointer -> LIF association. Geometry variants
   already own a copied texture record, so this only prevents a later mesh
   that reuses the same allocation address from inheriting stale texels. */
void hwModernGraphicsUnregisterSurfaceTexture(const void *materialIdentity);

/* Opens the world-only DXR extraction interval. `viewMatrix` is the exact
   OpenGL column-major matrix used for the compatibility frame. Mesh objects
   submitted during this interval therefore preserve hierarchy animation,
   world position, orientation, scale, and camera float exactly. */
void hwModernGraphicsBeginRaytracingScene(
    float verticalFieldOfViewDegrees, float aspectRatio,
    const float viewMatrix[16]);
void hwModernGraphicsEndRaytracingScene(void);
int hwModernGraphicsIsRaytracingSceneOpen(void);

/* Submit one triangle object at the same boundary used by the legacy mesh
   renderer. Vertices are XYZ floats at `vertexStrideBytes`; each triangle is
   three unsigned 16-bit indices at `triangleStrideBytes`. `modelViewMatrix`
   is the exact matrix active for that polygon object. */
void hwModernGraphicsSubmitTriangleGeometry(
    const void *geometryIdentity,
    const float *vertexPositions, unsigned int vertexCount,
    unsigned int vertexStrideBytes,
    const unsigned short *triangleIndices, unsigned int triangleCount,
    unsigned int triangleStrideBytes,
    unsigned int surfaceVariant,
    const HWModernTriangleSurface *triangleSurfaces,
    unsigned int triangleSurfaceStrideBytes,
    const float modelViewMatrix[16],
    const float baseColor[3]);
/* Same submission with an explicit DXR instance mask. The standard path uses
   0x03 (camera/transport + shadow); off-screen solid casters use 0x02 so they
   remain in shadow rays without creating a raster/DXR presentation mismatch. */
void hwModernGraphicsSubmitTriangleGeometryMasked(
    const void *geometryIdentity,
    const float *vertexPositions, unsigned int vertexCount,
    unsigned int vertexStrideBytes,
    const unsigned short *triangleIndices, unsigned int triangleCount,
    unsigned int triangleStrideBytes,
    unsigned int surfaceVariant,
    const HWModernTriangleSurface *triangleSurfaces,
    unsigned int triangleSurfaceStrideBytes,
    const float modelViewMatrix[16],
    const float baseColor[3], unsigned int instanceMask);

/* True only when a DXR device, ray pipeline, acceleration structures, and
   output surface are available. This is stronger than the capability probe. */
#ifdef __cplusplus
}
#endif

#endif
