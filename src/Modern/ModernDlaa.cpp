#include "ModernDlaa.h"

#if defined(_WIN32)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

#include <windows.h>
#include <wrl/client.h>

#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

using Microsoft::WRL::ComPtr;

namespace hwmodern
{
namespace
{
constexpr DXGI_FORMAT dlaaOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr float cameraNearPlane = 0.05f;
constexpr float cameraFarPlane = 500000.0f;

struct DlaaState
{
    bool initialized = false;
    int mode = 1;
    bool deviceReady = false;
    bool supported = false;
    bool outputReadable = false;
    bool loggedEvaluateFailure = false;
    bool rrPluginRequested = false;
    bool rrRequested = true;
    bool rrSupported = false;
    bool loggedRrEvaluateFailure = false;
    bool loggedRrActive = false;
    bool frameGenerationPluginRequested = false;
    bool frameGenerationSupported = false;
    bool frameGenerationRequested = false;
    bool frameGenerationActive = false;
    bool loggedFrameGenerationFailure = false;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int frameIndex = 0;
    bool forceHistoryReset = true;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> output;
};

DlaaState state;

void streamlineLog(sl::LogType type, const char *message)
{
    if (type == sl::LogType::eWarn || type == sl::LogType::eError)
    {
        std::fprintf(stderr, "[Streamline] %s", message != nullptr ? message : "");
    }
}

void setIdentity(sl::float4x4 &matrix)
{
    matrix.row[0] = { 1.0f, 0.0f, 0.0f, 0.0f };
    matrix.row[1] = { 0.0f, 1.0f, 0.0f, 0.0f };
    matrix.row[2] = { 0.0f, 0.0f, 1.0f, 0.0f };
    matrix.row[3] = { 0.0f, 0.0f, 0.0f, 1.0f };
}

void setPerspective(sl::float4x4 &projection, sl::float4x4 &inverse,
                    float fovRadians, float aspect)
{
    const float y = 1.0f / std::tan(fovRadians * 0.5f);
    const float x = y / std::max(aspect, 0.01f);
    const float c = cameraFarPlane / (cameraFarPlane - cameraNearPlane);
    const float d = -cameraNearPlane * cameraFarPlane /
                    (cameraFarPlane - cameraNearPlane);
    projection.row[0] = { x, 0.0f, 0.0f, 0.0f };
    projection.row[1] = { 0.0f, y, 0.0f, 0.0f };
    projection.row[2] = { 0.0f, 0.0f, c, 1.0f };
    projection.row[3] = { 0.0f, 0.0f, d, 0.0f };

    inverse.row[0] = { 1.0f / x, 0.0f, 0.0f, 0.0f };
    inverse.row[1] = { 0.0f, 1.0f / y, 0.0f, 0.0f };
    inverse.row[2] = { 0.0f, 0.0f, 0.0f, 1.0f / d };
    inverse.row[3] = { 0.0f, 0.0f, 1.0f, -c / d };
}

void transition(ID3D12GraphicsCommandList *commands,
                ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after)
{
    if (resource == nullptr || before == after)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commands->ResourceBarrier(1, &barrier);
}

bool runtimeFileBesideExecutable(const wchar_t *fileName)
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return false;
    }
    wchar_t *slash = std::wcsrchr(path, L'\\');
    if (slash == nullptr)
    {
        return false;
    }
    slash[1] = L'\0';
    const size_t directoryLength = std::wcslen(path);
    const size_t fileLength = std::wcslen(fileName);
    if (directoryLength + fileLength + 1 >= MAX_PATH)
    {
        return false;
    }
    std::wmemcpy(path + directoryLength, fileName, fileLength + 1);
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

sl::DLSSMode dlssModeFromSetting(int mode)
{
    switch (mode)
    {
        case 3: return sl::DLSSMode::eMaxQuality;
        case 4: return sl::DLSSMode::eBalanced;
        case 5: return sl::DLSSMode::eMaxPerformance;
        case 6: return sl::DLSSMode::eUltraPerformance;
        default: return sl::DLSSMode::eDLAA;
    }
}
}

void dlaaEarlyInitialize(void)
{
    if (state.initialized)
    {
        return;
    }
    state.rrPluginRequested =
        runtimeFileBesideExecutable(L"sl.dlss_d.dll") &&
        runtimeFileBesideExecutable(L"nvngx_dlssd.dll");
    state.frameGenerationPluginRequested =
        runtimeFileBesideExecutable(L"sl.dlss_g.dll") &&
        runtimeFileBesideExecutable(L"nvngx_dlssg.dll") &&
        runtimeFileBesideExecutable(L"sl.reflex.dll") &&
        runtimeFileBesideExecutable(L"sl.pcl.dll");
    static const sl::Feature dlssFeatures[] = {
        sl::kFeatureDLSS, sl::kFeatureDLSS_RR,
        sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL
    };
    sl::Preferences preferences = {};
    preferences.showConsole = false;
    preferences.logLevel = sl::LogLevel::eDefault;
    preferences.logMessageCallback = streamlineLog;
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking |
                        sl::PreferenceFlags::eDisableDebugText |
                        sl::PreferenceFlags::eUseDXGIFactoryProxy |
                        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = dlssFeatures;
    preferences.numFeaturesToLoad = state.frameGenerationPluginRequested ? 5u :
        (state.rrPluginRequested ? 2u : 1u);
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "HomeworldModern-0.91.2";
    preferences.projectId = "1f070a4e-e397-49ee-86fc-e9f429326b11";
    preferences.renderAPI = sl::RenderAPI::eD3D12;
    const sl::Result result = slInit(preferences);
    state.initialized = result == sl::Result::eOk;
    if (!state.initialized)
    {
        std::fprintf(stderr,
                     "[ModernGraphics] NVIDIA Streamline initialization "
                     "failed (result %d); DLSS/DLAA will remain off.\n",
                     static_cast<int>(result));
    }
    else if (!state.rrPluginRequested)
    {
        std::fprintf(stderr,
            "[ModernGraphics] NVIDIA DLSS Ray Reconstruction runtime is not "
            "beside the executable; DLSS/DLAA remains available and RR will "
            "fall back until sl.dlss_d.dll + nvngx_dlssd.dll are installed.\n");
    }
}

void dlaaSetMode(int mode)
{
    const int clampedMode = std::max(0, std::min(mode, 6));
    if (state.mode != clampedMode)
    {
        state.forceHistoryReset = true;
    }
    state.mode = clampedMode;
    if (state.mode == 0 || state.mode == 2)
    {
        state.outputReadable = false;
    }
    if (state.mode < 3)
    {
    }
}

void dlaaSetRayReconstructionEnabled(bool enabled)
{
    if (state.rrRequested != enabled)
    {
        state.forceHistoryReset = true;
    }
    state.rrRequested = enabled;
    if (!enabled)
    {
    }
}

void dlaaSetFrameGenerationEnabled(bool enabled)
{
    state.frameGenerationRequested = enabled;
    if (!state.initialized || !state.deviceReady ||
        !state.frameGenerationSupported)
    {
        state.frameGenerationActive = false;
        return;
    }
    sl::ReflexOptions reflex = {};
    reflex.mode = enabled ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff;
    sl::Result result = slReflexSetOptions(reflex);
    sl::DLSSGOptions options = {};
    options.mode = enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    options.numFramesToGenerate = 1;
    options.numBackBuffers = 3;
    options.enableUserInterfaceRecomposition = sl::Boolean::eFalse;
    if (result == sl::Result::eOk)
        result = slDLSSGSetOptions(sl::ViewportHandle(0u), options);
    state.frameGenerationActive = enabled && result == sl::Result::eOk;
    if (enabled && !state.frameGenerationActive &&
        !state.loggedFrameGenerationFailure)
    {
        std::fprintf(stderr,
            "[ModernGraphics] NVIDIA DLSS-G activation failed (result %d).\n",
            static_cast<int>(result));
        state.loggedFrameGenerationFailure = true;
    }
    if (state.frameGenerationActive)
        std::fprintf(stderr,
            "[ModernGraphics] NVIDIA DLSS-G 2x frame generation ACTIVE with Reflex Low Latency.\n");
}

bool dlaaFrameGenerationAvailable(void)
{
    return state.frameGenerationSupported;
}

bool dlaaFrameGenerationActive(void)
{
    return state.frameGenerationActive;
}

ID3D12Resource *dlaaOutputResource(void) { return state.output.Get(); }

bool dlaaSetDevice(ID3D12Device *device, IDXGIAdapter1 *adapter)
{
    state.deviceReady = false;
    state.supported = false;
    state.rrSupported = false;
    state.frameGenerationSupported = false;
    state.device = device;
    if (!state.initialized || device == nullptr || adapter == nullptr)
    {
        return false;
    }
    const sl::Result deviceResult = slSetD3DDevice(device);
    if (deviceResult != sl::Result::eOk)
    {
        std::fprintf(stderr,
                     "[ModernGraphics] Streamline rejected the D3D12 device "
                     "(result %d).\n", static_cast<int>(deviceResult));
        return false;
    }
    state.deviceReady = true;

    DXGI_ADAPTER_DESC1 description = {};
    if (FAILED(adapter->GetDesc1(&description)))
    {
        return false;
    }
    sl::AdapterInfo adapterInfo = {};
    adapterInfo.deviceLUID = reinterpret_cast<unsigned char *>(
        &description.AdapterLuid);
    adapterInfo.deviceLUIDSizeInBytes = sizeof(description.AdapterLuid);
    const sl::Result support = slIsFeatureSupported(
        sl::kFeatureDLSS, adapterInfo);
    state.supported = support == sl::Result::eOk;
    if (state.supported)
    {
        std::fprintf(stderr,
                     "[ModernGraphics] NVIDIA DLSS Super Resolution and "
                     "DLAA are available.\n");
    }
    else
    {
        std::fprintf(stderr,
                     "[ModernGraphics] NVIDIA DLSS/DLAA unavailable on this adapter "
                     "(result %d).\n", static_cast<int>(support));
    }
    if (state.supported && state.rrPluginRequested)
    {
        const sl::Result rrSupport = slIsFeatureSupported(
            sl::kFeatureDLSS_RR, adapterInfo);
        state.rrSupported = rrSupport == sl::Result::eOk;
        if (state.rrSupported)
        {
            std::fprintf(stderr,
                "[ModernGraphics] NVIDIA DLSS Ray Reconstruction is available.\n");
        }
        else
        {
            std::fprintf(stderr,
                "[ModernGraphics] NVIDIA DLSS Ray Reconstruction unavailable "
                "on this adapter/runtime (result %d); DLSS SR fallback remains live.\n",
                static_cast<int>(rrSupport));
        }
    }
    if (state.frameGenerationPluginRequested)
    {
        const sl::Result fgSupport = slIsFeatureSupported(
            sl::kFeatureDLSS_G, adapterInfo);
        const sl::Result reflexSupport = slIsFeatureSupported(
            sl::kFeatureReflex, adapterInfo);
        state.frameGenerationSupported =
            fgSupport == sl::Result::eOk && reflexSupport == sl::Result::eOk;
        std::fprintf(stderr,
            "[ModernGraphics] NVIDIA DLSS-G/Reflex %s (FG result %d, Reflex result %d).\n",
            state.frameGenerationSupported ? "available" : "unavailable",
            static_cast<int>(fgSupport), static_cast<int>(reflexSupport));
        if (state.frameGenerationRequested)
            dlaaSetFrameGenerationEnabled(true);
    }
    return state.supported;
}

void dlaaReleaseDevice(void)
{
    dlaaReleaseOutput();
    state.device.Reset();
    state.deviceReady = false;
    state.supported = false;
    state.rrSupported = false;
    state.frameGenerationSupported = false;
    state.frameGenerationActive = false;
}

bool dlaaCreateOutput(ID3D12DescriptorHeap *shaderVisibleHeap,
                      unsigned int descriptorSize,
                      unsigned int outputSrvIndex,
                      unsigned int width, unsigned int height)
{
    dlaaReleaseOutput();
    if ((state.mode == 0 || state.mode == 2) || !state.supported ||
        !state.deviceReady ||
        shaderVisibleHeap == nullptr || descriptorSize == 0)
    {
        return false;
    }
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC texture = {};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = width;
    texture.Height = height;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = dlaaOutputFormat;
    texture.SampleDesc.Count = 1;
    texture.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const HRESULT result = state.device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &texture,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&state.output));
    if (FAILED(result))
    {
        std::fprintf(stderr,
                     "[ModernGraphics] DLAA output allocation failed "
                     "(HRESULT 0x%08lx).\n",
                     static_cast<unsigned long>(result));
        return false;
    }
    state.output->SetName(L"Homeworld NVIDIA DLSS/DLAA full-scene output");
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = dlaaOutputFormat;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        shaderVisibleHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(outputSrvIndex) * descriptorSize;
    state.device->CreateShaderResourceView(state.output.Get(), &srv, handle);
    state.width = width;
    state.height = height;
    state.outputReadable = false;
    state.loggedEvaluateFailure = false;
    state.loggedRrEvaluateFailure = false;
    state.forceHistoryReset = true;
    return true;
}

void dlaaReleaseOutput(void)
{
    state.output.Reset();
    state.width = 0;
    state.height = 0;
    state.outputReadable = false;
}

bool dlaaGetOptimalRenderSize(int mode,
                              unsigned int outputWidth,
                              unsigned int outputHeight,
                              unsigned int *renderWidth,
                              unsigned int *renderHeight)
{
    if (!state.supported || mode < 3 || mode > 6 || outputWidth == 0 ||
        outputHeight == 0 || renderWidth == nullptr || renderHeight == nullptr)
    {
        return false;
    }
    if (state.rrRequested && state.rrSupported)
    {
        sl::DLSSDOptions rrOptions = {};
        rrOptions.mode = dlssModeFromSetting(mode);
        rrOptions.outputWidth = outputWidth;
        rrOptions.outputHeight = outputHeight;
        rrOptions.colorBuffersHDR = sl::Boolean::eTrue;
        rrOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
        rrOptions.dlaaPreset = sl::DLSSDPreset::ePresetE;
        rrOptions.qualityPreset = sl::DLSSDPreset::ePresetE;
        rrOptions.balancedPreset = sl::DLSSDPreset::ePresetE;
        rrOptions.performancePreset = sl::DLSSDPreset::ePresetE;
        rrOptions.ultraPerformancePreset = sl::DLSSDPreset::ePresetE;
        sl::DLSSDOptimalSettings rrSettings = {};
        if (slDLSSDGetOptimalSettings(rrOptions, rrSettings) == sl::Result::eOk &&
            rrSettings.optimalRenderWidth != 0 &&
            rrSettings.optimalRenderHeight != 0)
        {
            *renderWidth = rrSettings.optimalRenderWidth;
            *renderHeight = rrSettings.optimalRenderHeight;
            return true;
        }
    }
    sl::DLSSOptions options = {};
    options.mode = dlssModeFromSetting(mode);
    options.outputWidth = outputWidth;
    options.outputHeight = outputHeight;
    options.qualityPreset = sl::DLSSPreset::ePresetK;
    options.balancedPreset = sl::DLSSPreset::ePresetK;
    options.performancePreset = sl::DLSSPreset::ePresetM;
    options.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
    sl::DLSSOptimalSettings settings = {};
    if (slDLSSGetOptimalSettings(options, settings) != sl::Result::eOk ||
        settings.optimalRenderWidth == 0 || settings.optimalRenderHeight == 0)
    {
        return false;
    }
    *renderWidth = settings.optimalRenderWidth;
    *renderHeight = settings.optimalRenderHeight;
    return true;
}

bool dlaaEvaluate(ID3D12GraphicsCommandList *commands,
                  ID3D12Resource *inputColor,
                  unsigned int inputWidth,
                  unsigned int inputHeight,
                  DXGI_FORMAT inputFormat,
                  ID3D12Resource *depth,
                  ID3D12Resource *motion,
                  float verticalFieldOfViewDegrees,
                  float aspectRatio,
                  float jitterX,
                  float jitterY,
                  bool resetHistory)
{
    if ((state.mode == 0 || state.mode == 2) || !state.supported ||
        !state.output || commands == nullptr || inputColor == nullptr ||
        depth == nullptr || motion == nullptr || inputWidth == 0 ||
        inputHeight == 0)
    {
        state.outputReadable = false;
        return false;
    }
    if (state.outputReadable)
    {
        transition(commands, state.output.Get(),
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        state.outputReadable = false;
    }

    sl::FrameToken *frame = nullptr;
    const unsigned int frameIndex = state.frameIndex++;
    sl::Result result = slGetNewFrameToken(frame, &frameIndex);
    if (result != sl::Result::eOk || frame == nullptr)
    {
        return false;
    }
    const sl::ViewportHandle viewport(0u);
    constexpr float pi = 3.14159265358979323846f;
    const float fovRadians = std::max(verticalFieldOfViewDegrees, 1.0f) *
                             (pi / 180.0f);
    sl::Constants constants = {};
    setPerspective(constants.cameraViewToClip, constants.clipToCameraView,
                   fovRadians, aspectRatio);
    setIdentity(constants.clipToLensClip);
    setIdentity(constants.clipToPrevClip);
    setIdentity(constants.prevClipToClip);
    constants.jitterOffset = { jitterX, jitterY };
    constants.mvecScale = { 1.0f / static_cast<float>(inputWidth),
                            1.0f / static_cast<float>(inputHeight) };
    constants.cameraPinholeOffset = { 0.0f, 0.0f };
    constants.cameraPos = { 0.0f, 0.0f, 0.0f };
    constants.cameraUp = { 0.0f, 1.0f, 0.0f };
    constants.cameraRight = { 1.0f, 0.0f, 0.0f };
    constants.cameraFwd = { 0.0f, 0.0f, 1.0f };
    constants.cameraNear = cameraNearPlane;
    constants.cameraFar = cameraFarPlane;
    constants.cameraFOV = fovRadians;
    constants.cameraAspectRatio = aspectRatio;
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    const bool resetTemporalHistory = resetHistory || state.forceHistoryReset;
    constants.reset = resetTemporalHistory ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    result = slSetConstants(constants, *frame, viewport);
    if (result != sl::Result::eOk)
    {
        return false;
    }
    if (state.frameGenerationActive)
    {
        slPCLSetMarker(sl::PCLMarker::eSimulationStart, *frame);
        slPCLSetMarker(sl::PCLMarker::eSimulationEnd, *frame);
        slPCLSetMarker(sl::PCLMarker::eRenderSubmitStart, *frame);
    }

    sl::DLSSOptions options = {};
    options.mode = dlssModeFromSetting(state.mode);
    options.outputWidth = state.width;
    options.outputHeight = state.height;
    /* The Streamline input is now the complete HUD-less presenter scene. It is
       stored in FP16 for precision, but it is already display-referred by the
       hybrid raster/DXR compositor; do not let DLSS invent exposure from a
       partial signal and do not treat alpha as a temporal content channel. */
    options.colorBuffersHDR = sl::Boolean::eFalse;
    options.useAutoExposure = sl::Boolean::eFalse;
    options.alphaUpscalingEnabled = sl::Boolean::eFalse;
    options.dlaaPreset = sl::DLSSPreset::ePresetK;
    options.qualityPreset = sl::DLSSPreset::ePresetK;
    options.balancedPreset = sl::DLSSPreset::ePresetK;
    options.performancePreset = sl::DLSSPreset::ePresetM;
    options.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
    result = slDLSSSetOptions(viewport, options);
    if (result != sl::Result::eOk)
    {
        return false;
    }

    sl::Extent inputExtent = { 0, 0, inputWidth, inputHeight };
    sl::Extent outputExtent = { 0, 0, state.width, state.height };
    sl::Resource colorIn(sl::ResourceType::eTex2d, inputColor,
                         static_cast<unsigned int>(
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    sl::Resource colorOut(sl::ResourceType::eTex2d, state.output.Get(),
                          static_cast<unsigned int>(
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    sl::Resource depthIn(sl::ResourceType::eTex2d, depth,
                         static_cast<unsigned int>(
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    sl::Resource motionIn(sl::ResourceType::eTex2d, motion,
                          static_cast<unsigned int>(
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    colorIn.width = depthIn.width = motionIn.width = inputWidth;
    colorIn.height = depthIn.height = motionIn.height = inputHeight;
    colorOut.width = state.width;
    colorOut.height = state.height;
    colorIn.nativeFormat = inputFormat;
    colorOut.nativeFormat = dlaaOutputFormat;
    depthIn.nativeFormat = DXGI_FORMAT_R32_FLOAT;
    motionIn.nativeFormat = DXGI_FORMAT_R16G16_FLOAT;

    sl::ResourceTag tags[] = {
        { &colorIn, sl::kBufferTypeScalingInputColor,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
        { &colorOut, sl::kBufferTypeScalingOutputColor,
          sl::ResourceLifecycle::eOnlyValidNow, &outputExtent },
        { &depthIn, sl::kBufferTypeDepth,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
        { &motionIn, sl::kBufferTypeMotionVectors,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent }
    };
    result = slSetTagForFrame(*frame, viewport, tags,
                              static_cast<unsigned int>(_countof(tags)),
                              reinterpret_cast<sl::CommandBuffer *>(commands));
    if (state.frameGenerationActive)
        slPCLSetMarker(sl::PCLMarker::eRenderSubmitEnd, *frame);
    if (result == sl::Result::eOk)
    {
        const sl::BaseStructure *inputs[] = { &viewport };
        result = slEvaluateFeature(
            sl::kFeatureDLSS, *frame, inputs,
            static_cast<unsigned int>(_countof(inputs)),
            reinterpret_cast<sl::CommandBuffer *>(commands));
    }
    if (result != sl::Result::eOk)
    {
        if (!state.loggedEvaluateFailure)
        {
            std::fprintf(stderr,
                         "[ModernGraphics] NVIDIA DLSS/DLAA full-scene evaluation failed "
                         "(result %d); native scene presentation retained.\n",
                         static_cast<int>(result));
            state.loggedEvaluateFailure = true;
        }
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = state.output.Get();
    commands->ResourceBarrier(1, &barrier);
    transition(commands, state.output.Get(),
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    state.outputReadable = true;
    state.forceHistoryReset = false;
    return true;
}

bool dlaaEvaluateRayReconstruction(
                  ID3D12GraphicsCommandList *commands,
                  ID3D12Resource *inputColor,
                  unsigned int inputWidth,
                  unsigned int inputHeight,
                  DXGI_FORMAT inputFormat,
                  ID3D12Resource *depth,
                  ID3D12Resource *motion,
                  ID3D12Resource *diffuseAlbedo,
                  ID3D12Resource *specularAlbedo,
                  ID3D12Resource *normalRoughness,
                  float verticalFieldOfViewDegrees,
                  float aspectRatio,
                  float jitterX,
                  float jitterY,
                  bool resetHistory)
{
    if (!state.rrRequested || !state.rrSupported || state.mode < 3 ||
        state.mode > 6 || !state.output || commands == nullptr ||
        inputColor == nullptr || depth == nullptr || motion == nullptr ||
        diffuseAlbedo == nullptr || specularAlbedo == nullptr ||
        normalRoughness == nullptr || inputWidth == 0 || inputHeight == 0 ||
        inputFormat != DXGI_FORMAT_R16G16B16A16_FLOAT ||
        (inputWidth >= state.width && inputHeight >= state.height))
    {
        return false;
    }
    if (state.outputReadable)
    {
        transition(commands, state.output.Get(),
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        state.outputReadable = false;
    }

    sl::FrameToken *frame = nullptr;
    const unsigned int frameIndex = state.frameIndex++;
    sl::Result result = slGetNewFrameToken(frame, &frameIndex);
    if (result != sl::Result::eOk || frame == nullptr)
    {
        return false;
    }
    const sl::ViewportHandle viewport(0u);
    constexpr float pi = 3.14159265358979323846f;
    const float fovRadians = std::max(verticalFieldOfViewDegrees, 1.0f) *
                             (pi / 180.0f);
    sl::Constants constants = {};
    setPerspective(constants.cameraViewToClip, constants.clipToCameraView,
                   fovRadians, aspectRatio);
    setIdentity(constants.clipToLensClip);
    setIdentity(constants.clipToPrevClip);
    setIdentity(constants.prevClipToClip);
    constants.jitterOffset = { jitterX, jitterY };
    constants.mvecScale = { 1.0f / static_cast<float>(inputWidth),
                            1.0f / static_cast<float>(inputHeight) };
    constants.cameraPinholeOffset = { 0.0f, 0.0f };
    constants.cameraPos = { 0.0f, 0.0f, 0.0f };
    constants.cameraUp = { 0.0f, 1.0f, 0.0f };
    constants.cameraRight = { 1.0f, 0.0f, 0.0f };
    constants.cameraFwd = { 0.0f, 0.0f, 1.0f };
    constants.cameraNear = cameraNearPlane;
    constants.cameraFar = cameraFarPlane;
    constants.cameraFOV = fovRadians;
    constants.cameraAspectRatio = aspectRatio;
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    const bool resetTemporalHistory = resetHistory || state.forceHistoryReset;
    constants.reset = resetTemporalHistory ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    result = slSetConstants(constants, *frame, viewport);
    if (result != sl::Result::eOk)
    {
        return false;
    }

    /* NVIDIA's DLSS-RR contract requires compatible DLSS Super Resolution
       options to be supplied alongside the DLSS-D options for the same
       viewport. Keep these synchronized with the normal full-scene path. */
    sl::DLSSOptions dlssOptions = {};
    dlssOptions.mode = dlssModeFromSetting(state.mode);
    dlssOptions.outputWidth = state.width;
    dlssOptions.outputHeight = state.height;
    /* DLSS Ray Reconstruction only supports HDR color input.  The v20
       integration accidentally advertised the FP16 HUD-less scene as LDR,
       which can cause DLSS-D option/evaluation rejection even when the RR DLLs
       are present. */
    dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
    dlssOptions.useAutoExposure = sl::Boolean::eFalse;
    dlssOptions.alphaUpscalingEnabled = sl::Boolean::eFalse;
    dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetK;
    dlssOptions.qualityPreset = sl::DLSSPreset::ePresetK;
    dlssOptions.balancedPreset = sl::DLSSPreset::ePresetK;
    dlssOptions.performancePreset = sl::DLSSPreset::ePresetM;
    dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
    result = slDLSSSetOptions(viewport, dlssOptions);
    if (result != sl::Result::eOk)
    {
        return false;
    }

    sl::DLSSDOptions options = {};
    options.mode = dlssModeFromSetting(state.mode);
    options.outputWidth = state.width;
    options.outputHeight = state.height;
    options.preExposure = 1.0f;
    options.exposureScale = 1.0f;
    options.colorBuffersHDR = sl::Boolean::eTrue;
    options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
    setIdentity(options.worldToCameraView);
    setIdentity(options.cameraViewToWorld);
    options.alphaUpscalingEnabled = sl::Boolean::eFalse;
    options.dlaaPreset = sl::DLSSDPreset::ePresetE;
    options.qualityPreset = sl::DLSSDPreset::ePresetE;
    options.balancedPreset = sl::DLSSDPreset::ePresetE;
    options.performancePreset = sl::DLSSDPreset::ePresetE;
    options.ultraPerformancePreset = sl::DLSSDPreset::ePresetE;
    result = slDLSSDSetOptions(viewport, options);
    if (result != sl::Result::eOk)
    {
        return false;
    }

    sl::Extent inputExtent = { 0, 0, inputWidth, inputHeight };
    sl::Extent outputExtent = { 0, 0, state.width, state.height };
    sl::Resource colorIn(sl::ResourceType::eTex2d, inputColor,
                         static_cast<unsigned int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    sl::Resource colorOut(sl::ResourceType::eTex2d, state.output.Get(),
                          static_cast<unsigned int>(D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    sl::Resource depthIn(sl::ResourceType::eTex2d, depth,
                         static_cast<unsigned int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    sl::Resource motionIn(sl::ResourceType::eTex2d, motion,
                          static_cast<unsigned int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    sl::Resource diffuseIn(sl::ResourceType::eTex2d, diffuseAlbedo,
                           static_cast<unsigned int>(
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    sl::Resource specularIn(sl::ResourceType::eTex2d, specularAlbedo,
                            static_cast<unsigned int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    sl::Resource normalRoughnessIn(sl::ResourceType::eTex2d, normalRoughness,
                                   static_cast<unsigned int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    colorIn.width = depthIn.width = motionIn.width = diffuseIn.width =
        specularIn.width = normalRoughnessIn.width = inputWidth;
    colorIn.height = depthIn.height = motionIn.height = diffuseIn.height =
        specularIn.height = normalRoughnessIn.height = inputHeight;
    colorOut.width = state.width;
    colorOut.height = state.height;
    colorIn.nativeFormat = inputFormat;
    colorOut.nativeFormat = dlaaOutputFormat;
    depthIn.nativeFormat = DXGI_FORMAT_R32_FLOAT;
    motionIn.nativeFormat = DXGI_FORMAT_R16G16_FLOAT;
    diffuseIn.nativeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    specularIn.nativeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    normalRoughnessIn.nativeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    sl::ResourceTag tags[] = {
        { &colorIn, sl::kBufferTypeScalingInputColor,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
        { &colorOut, sl::kBufferTypeScalingOutputColor,
          sl::ResourceLifecycle::eOnlyValidNow, &outputExtent },
        { &depthIn, sl::kBufferTypeDepth,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
        { &motionIn, sl::kBufferTypeMotionVectors,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
        { &diffuseIn, sl::kBufferTypeAlbedo,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
        { &specularIn, sl::kBufferTypeSpecularAlbedo,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
        { &normalRoughnessIn, sl::kBufferTypeNormalRoughness,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
        { &motionIn, sl::kBufferTypeSpecularMotionVectors,
          sl::ResourceLifecycle::eOnlyValidNow, &inputExtent }
    };
    result = slSetTagForFrame(*frame, viewport, tags,
                              static_cast<unsigned int>(_countof(tags)),
                              reinterpret_cast<sl::CommandBuffer *>(commands));
    if (result == sl::Result::eOk)
    {
        const sl::BaseStructure *inputs[] = { &viewport };
        result = slEvaluateFeature(
            sl::kFeatureDLSS_RR, *frame, inputs,
            static_cast<unsigned int>(_countof(inputs)),
            reinterpret_cast<sl::CommandBuffer *>(commands));
    }
    if (result != sl::Result::eOk)
    {
        if (!state.loggedRrEvaluateFailure)
        {
            std::fprintf(stderr,
                "[ModernGraphics] NVIDIA DLSS Ray Reconstruction evaluation "
                "failed (result %d); falling back to normal DLSS SR for this run.\n",
                static_cast<int>(result));
            state.loggedRrEvaluateFailure = true;
        }
        state.rrSupported = false;
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = state.output.Get();
    commands->ResourceBarrier(1, &barrier);
    transition(commands, state.output.Get(),
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    state.outputReadable = true;
    state.forceHistoryReset = false;
    if (!state.loggedRrActive)
    {
        std::fprintf(stderr,
            "[ModernGraphics] NVIDIA DLSS Ray Reconstruction ACTIVE: "
            "FP16 HDR low-resolution scene + depth/motion/diffuse/specular/normal-roughness guides.\n");
        state.loggedRrActive = true;
    }
    return true;
}

bool dlaaAvailable(void)
{
    return state.initialized && state.supported;
}

bool dlaaActive(void)
{
    return state.mode != 0 && state.mode != 2 && state.supported &&
           state.output.Get() != nullptr;
}

bool dlaaRayReconstructionAvailable(void)
{
    return state.initialized && state.supported && state.rrPluginRequested &&
           state.rrSupported;
}

}

#endif
