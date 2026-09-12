#include "ModernFrameGeneration.h"

#include <cstdio>
#include <windows.h>
#include <ffx_api.hpp>
#include <dx12/ffx_api_dx12.hpp>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>

namespace hwmodern
{
namespace
{
ffx::Context swapchainContext = nullptr;
ffx::Context frameContext = nullptr;
bool runtimeAvailable = false;
bool requested = false;
bool active = false;
unsigned int configuredWidth = 0;
unsigned int configuredHeight = 0;

void destroyFrameContext()
{
    if (frameContext) ffx::DestroyContext(frameContext);
    frameContext = nullptr;
    configuredWidth = configuredHeight = 0;
    active = false;
}
}

bool fsrFrameGenerationWrapSwapchain(IDXGISwapChain4 **swapchain,
                                     ID3D12CommandQueue *queue)
{
    runtimeAvailable = GetModuleHandleW(L"amd_fidelityfx_framegeneration_dx12.dll") != nullptr ||
        LoadLibraryW(L"amd_fidelityfx_framegeneration_dx12.dll") != nullptr;
    if (!runtimeAvailable || !swapchain || !*swapchain || !queue) return false;
    ffxCreateContextDescFrameGenerationSwapChainWrapDX12 wrap{};
    wrap.swapchain = swapchain;
    wrap.gameQueue = queue;
    ffxCreateContextDescFrameGenerationSwapChainVersionDX12 version{};
    version.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
    const auto result = ffx::CreateContext(swapchainContext, nullptr, wrap, version);
    runtimeAvailable = result == ffx::ReturnCode::Ok;
    std::fprintf(stderr, "[ModernGraphics] AMD FSR frame-generation swapchain %s (result %d).\n",
                 runtimeAvailable ? "ready" : "unavailable", static_cast<int>(result));
    return runtimeAvailable;
}

void fsrFrameGenerationRelease(void)
{
    destroyFrameContext();
    if (swapchainContext) ffx::DestroyContext(swapchainContext);
    swapchainContext = nullptr;
}

void fsrFrameGenerationSetEnabled(bool enabled)
{
    requested = enabled;
    if (!enabled) active = false;
}

bool fsrFrameGenerationPrepare(ID3D12GraphicsCommandList *commands,
                               IDXGISwapChain4 *swapchain,
                               ID3D12Resource *hudlessColor,
                               ID3D12Resource *depth,
                               ID3D12Resource *motion,
                               unsigned int renderWidth,
                               unsigned int renderHeight,
                               unsigned int displayWidth,
                               unsigned int displayHeight,
                               float jitterX, float jitterY,
                               float fovRadians, float deltaMilliseconds,
                               bool reset, unsigned long long frameId)
{
    active = false;
    if (!requested || !runtimeAvailable || !swapchainContext || !commands ||
        !swapchain || !hudlessColor || !depth || !motion) return false;
    if (!frameContext || configuredWidth != displayWidth || configuredHeight != displayHeight)
    {
        destroyFrameContext();
        ffxCreateContextDescFrameGeneration create{};
        create.flags = FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE |
                       FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
        create.displaySize = {displayWidth, displayHeight};
        create.maxRenderSize = {displayWidth, displayHeight};
        create.backBufferFormat = ffxApiGetSurfaceFormatDX12(DXGI_FORMAT_R16G16B16A16_FLOAT);
        ffxCreateContextDescFrameGenerationVersion version{};
        version.version = FFX_FRAMEGENERATION_VERSION;
        ffx::CreateBackendDX12Desc backend{};
        ID3D12Device *device = nullptr;
        if (FAILED(commands->GetDevice(IID_PPV_ARGS(&device)))) return false;
        backend.device = device;
        const auto created = ffx::CreateContext(frameContext, nullptr, create, backend, version);
        device->Release();
        if (created != ffx::ReturnCode::Ok)
            return false;
        configuredWidth = displayWidth;
        configuredHeight = displayHeight;
    }
    ffxConfigureDescFrameGeneration config{};
    config.swapChain = swapchain;
    config.frameGenerationEnabled = true;
    config.allowAsyncWorkloads = false;
    config.HUDLessColor = ffxApiGetResourceDX12(hudlessColor, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    config.generationRect.left = 0;
    config.generationRect.top = 0;
    config.generationRect.width = displayWidth;
    config.generationRect.height = displayHeight;
    config.frameID = frameId;
    if (ffx::Configure(frameContext, config) != ffx::ReturnCode::Ok) return false;

    ffxDispatchDescFrameGenerationPrepareV2 prepare{};
    prepare.commandList = commands;
    prepare.frameID = frameId;
    prepare.renderSize = {renderWidth, renderHeight};
    prepare.jitterOffset = {jitterX, jitterY};
    prepare.motionVectorScale = {1.0f, 1.0f};
    prepare.frameTimeDelta = deltaMilliseconds;
    prepare.reset = reset;
    prepare.cameraNear = 1.0f;
    prepare.cameraFar = 1000000.0f;
    prepare.cameraFovAngleVertical = fovRadians;
    prepare.viewSpaceToMetersFactor = 1.0f;
    prepare.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    prepare.motionVectors = ffxApiGetResourceDX12(motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    prepare.cameraUp[1] = 1.0f;
    prepare.cameraRight[0] = 1.0f;
    prepare.cameraForward[2] = -1.0f;
    active = ffx::Dispatch(frameContext, prepare) == ffx::ReturnCode::Ok;
    return active;
}

bool fsrFrameGenerationAvailable(void) { return runtimeAvailable && swapchainContext; }
bool fsrFrameGenerationActive(void) { return active; }
}
