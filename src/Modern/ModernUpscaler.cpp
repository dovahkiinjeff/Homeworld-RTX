#include "ModernUpscaler.h"
#include "ModernDlaa.h"
#include "ModernGraphics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <wrl/client.h>

#include <ffx_api.hpp>
#include <dx12/ffx_api_dx12.hpp>
#include <ffx_upscale.hpp>
#include <xess/xess.h>
#include <xess/xess_d3d12.h>

using Microsoft::WRL::ComPtr;

namespace hwmodern
{
namespace
{
constexpr DXGI_FORMAT outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
struct State
{
    ID3D12Device *device = nullptr;
    unsigned int vendor = 0;
    int requested = HW_MODERN_AA_AUTO_NATIVE;
    int resolved = HW_MODERN_AA_OFF;
    UpscalerBackend backend = UpscalerBackend::None;
    ComPtr<ID3D12Resource> output;
    unsigned int width = 0, height = 0;
    bool outputReadable = false;
    ffx::Context fsr = nullptr;
    xess_context_handle_t xess = nullptr;
    bool fsrRuntime = false;
    bool xessRuntime = false;
    bool active = false;
} state;

void transition(ID3D12GraphicsCommandList *commands, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (!commands || !resource || before == after) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commands->ResourceBarrier(1, &barrier);
}

bool isDlss(int mode) { return mode == HW_MODERN_AA_DLAA ||
    (mode >= HW_MODERN_AA_DLSS_QUALITY && mode <= HW_MODERN_AA_DLSS_ULTRA_PERFORMANCE); }
bool isFsr(int mode) { return mode >= HW_MODERN_AA_FSR_NATIVE && mode <= HW_MODERN_AA_FSR_ULTRA_PERFORMANCE; }
bool isXess(int mode) { return mode >= HW_MODERN_AA_XESS_AA && mode <= HW_MODERN_AA_XESS_ULTRA_PERFORMANCE; }

int resolveMode(int mode)
{
    /* The signed FidelityFX runtime is also the dependable vendor-neutral
       temporal fallback.  It provides true native-resolution accumulation;
       only fall back to the presenter's FXAA when no temporal runtime exists. */
    if (mode == HW_MODERN_AA_NATIVE_TAA && state.fsrRuntime)
        return HW_MODERN_AA_FSR_NATIVE;
    if (mode != HW_MODERN_AA_AUTO_QUALITY && mode != HW_MODERN_AA_AUTO_NATIVE)
        return mode;
    const bool native = mode == HW_MODERN_AA_AUTO_NATIVE;
    if (state.vendor == 0x10de && dlaaAvailable())
        return native ? HW_MODERN_AA_DLAA : HW_MODERN_AA_DLSS_QUALITY;
    if (state.vendor == 0x8086 && state.xessRuntime)
        return native ? HW_MODERN_AA_XESS_AA : HW_MODERN_AA_XESS_QUALITY;
    if (state.fsrRuntime)
        return native ? HW_MODERN_AA_FSR_NATIVE : HW_MODERN_AA_FSR_QUALITY;
    if (state.xessRuntime)
        return native ? HW_MODERN_AA_XESS_AA : HW_MODERN_AA_XESS_QUALITY;
    return HW_MODERN_AA_NATIVE_TAA;
}

UpscalerBackend backendFor(int mode)
{
    if (isDlss(mode)) return UpscalerBackend::NvidiaDlss;
    if (isFsr(mode)) return UpscalerBackend::AmdFsr;
    if (isXess(mode)) return UpscalerBackend::IntelXess;
    if (mode == HW_MODERN_AA_NATIVE_TAA) return UpscalerBackend::NativeTaa;
    return UpscalerBackend::None;
}

xess_quality_settings_t xessQuality(int mode)
{
    switch (mode) {
    case HW_MODERN_AA_XESS_AA: return XESS_QUALITY_SETTING_AA;
    case HW_MODERN_AA_XESS_BALANCED: return XESS_QUALITY_SETTING_BALANCED;
    case HW_MODERN_AA_XESS_PERFORMANCE: return XESS_QUALITY_SETTING_PERFORMANCE;
    case HW_MODERN_AA_XESS_ULTRA_PERFORMANCE: return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
    default: return XESS_QUALITY_SETTING_QUALITY;
    }
}

unsigned int fsrQuality(int mode)
{
    return static_cast<unsigned int>(std::max(0, mode - HW_MODERN_AA_FSR_NATIVE));
}

void destroyContexts()
{
    if (state.fsr) { ffx::DestroyContext(state.fsr); state.fsr = nullptr; }
    if (state.xess) { xessDestroyContext(state.xess); state.xess = nullptr; }
}
}

void upscalerSetRequestedMode(int mode)
{
    state.requested = mode;
    state.resolved = resolveMode(mode);
    state.backend = backendFor(state.resolved);
    dlaaSetMode(state.resolved);
}

bool upscalerSetDevice(ID3D12Device *device, IDXGIAdapter1 *adapter)
{
    upscalerReleaseDevice();
    state.device = device;
    if (adapter) { DXGI_ADAPTER_DESC1 desc = {}; adapter->GetDesc1(&desc); state.vendor = desc.VendorId; }
    state.fsrRuntime = GetModuleHandleW(L"amd_fidelityfx_loader_dx12.dll") != nullptr ||
                       LoadLibraryW(L"amd_fidelityfx_loader_dx12.dll") != nullptr;
    state.xessRuntime = GetModuleHandleW(L"libxess.dll") != nullptr || LoadLibraryW(L"libxess.dll") != nullptr;
    upscalerSetRequestedMode(state.requested);
    std::fprintf(stderr, "[ModernGraphics] Reconstruction capabilities: DLSS=%s, FSR=%s, XeSS=%s; Auto resolves to %s.\n",
        dlaaAvailable() ? "yes" : "no", state.fsrRuntime ? "yes" : "no",
        state.xessRuntime ? "yes" : "no", upscalerResolvedName());
    return device != nullptr;
}

void upscalerReleaseDevice(void)
{
    destroyContexts(); state.output.Reset(); state.device = nullptr; state.width = state.height = 0;
    state.outputReadable = state.active = false;
}

void upscalerReleaseOutput(void)
{
    destroyContexts(); state.output.Reset(); state.width = state.height = 0;
    state.outputReadable = state.active = false; dlaaReleaseOutput();
}

bool upscalerCreateOutput(ID3D12DescriptorHeap *heap, unsigned int descriptorSize,
                          unsigned int outputSrvIndex, unsigned int width, unsigned int height)
{
    upscalerReleaseOutput(); upscalerSetRequestedMode(state.requested);
    if (state.backend == UpscalerBackend::NvidiaDlss)
        return dlaaCreateOutput(heap, descriptorSize, outputSrvIndex, width, height);
    if ((state.backend != UpscalerBackend::AmdFsr && state.backend != UpscalerBackend::IntelXess) ||
        !state.device || !heap || !descriptorSize) return false;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = std::max(1u, width); rd.Height = std::max(1u, height); rd.DepthOrArraySize = 1;
    rd.MipLevels = 1; rd.Format = outputFormat; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(state.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&state.output)))) return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {}; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = outputFormat; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
    auto handle = heap->GetCPUDescriptorHandleForHeapStart(); handle.ptr += static_cast<SIZE_T>(outputSrvIndex) * descriptorSize;
    state.device->CreateShaderResourceView(state.output.Get(), &srv, handle);
    state.width = width; state.height = height;
    if (state.backend == UpscalerBackend::AmdFsr) {
        ffx::CreateContextDescUpscale create{}; create.maxRenderSize = {width, height}; create.maxUpscaleSize = {width, height};
        create.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        ffx::CreateBackendDX12Desc backend{}; backend.device = state.device;
        ffx::CreateContextDescUpscaleVersion version{}; version.version = FFX_UPSCALER_VERSION;
        if (ffx::CreateContext(state.fsr, nullptr, create, backend, version) != ffx::ReturnCode::Ok) return false;
    } else {
        if (xessD3D12CreateContext(state.device, &state.xess) != XESS_RESULT_SUCCESS) return false;
        xess_d3d12_init_params_t init{}; init.outputResolution = {width, height}; init.qualitySetting = xessQuality(state.resolved);
        init.initFlags = XESS_INIT_FLAG_NONE | XESS_INIT_FLAG_LDR_INPUT_COLOR;
        if (xessD3D12Init(state.xess, &init) != XESS_RESULT_SUCCESS) return false;
    }
    return true;
}

bool upscalerGetRenderSize(int mode, unsigned int ow, unsigned int oh, unsigned int *rw, unsigned int *rh)
{
    const int resolved = resolveMode(mode);
    if (isDlss(resolved)) return dlaaGetOptimalRenderSize(resolved, ow, oh, rw, rh);
    if (isFsr(resolved)) {
        const float ratios[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f}; const float r = ratios[fsrQuality(resolved)];
        *rw = std::max(1u, static_cast<unsigned int>(ow / r)); *rh = std::max(1u, static_cast<unsigned int>(oh / r)); return true;
    }
    if (isXess(resolved) && state.xessRuntime && state.device) {
        xess_context_handle_t context = nullptr; if (xessD3D12CreateContext(state.device, &context) != XESS_RESULT_SUCCESS) return false;
        xess_2d_t out{ow, oh}, in{}; const auto result = xessGetInputResolution(context, &out, xessQuality(resolved), &in);
        xessDestroyContext(context); if (result != XESS_RESULT_SUCCESS) return false; *rw = in.x; *rh = in.y; return true;
    }
    if (resolved == HW_MODERN_AA_NATIVE_TAA) { *rw = ow; *rh = oh; return true; }
    return false;
}

bool upscalerEvaluate(ID3D12GraphicsCommandList *cmd, ID3D12Resource *color, unsigned int iw,
    unsigned int ih, DXGI_FORMAT format, ID3D12Resource *depth, ID3D12Resource *motion,
    ID3D12Resource *reactive, float fov, float jx, float jy, float dt, bool reset)
{
    state.active = false;
    if (state.backend == UpscalerBackend::NvidiaDlss) {
        state.active = dlaaEvaluate(cmd, color, iw, ih, format, depth, motion, fov,
            static_cast<float>(iw) / std::max(1u, ih), jx, jy, reset); return state.active;
    }
    if (!cmd || !state.output || !color || !depth || !motion) return false;
    if (state.outputReadable) transition(cmd, state.output.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (state.backend == UpscalerBackend::AmdFsr && state.fsr) {
        ffx::DispatchDescUpscale d{}; d.commandList = cmd;
        d.color = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.motionVectors = ffxApiGetResourceDX12(motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.reactive = ffxApiGetResourceDX12(reactive, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.output = ffxApiGetResourceDX12(state.output.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        d.jitterOffset = {-jx, -jy}; d.motionVectorScale = {1.0f, 1.0f};
        d.renderSize = {iw, ih}; d.upscaleSize = {state.width, state.height}; d.frameTimeDelta = std::max(0.1f, dt);
        d.preExposure = 1.0f; d.reset = reset; d.cameraNear = 1.0f; d.cameraFar = 1000000.0f;
        d.cameraFovAngleVertical = fov * 0.01745329252f; d.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        state.active = ffx::Dispatch(state.fsr, d) == ffx::ReturnCode::Ok;
    } else if (state.backend == UpscalerBackend::IntelXess && state.xess) {
        xess_d3d12_execute_params_t d{}; d.pColorTexture = color; d.pVelocityTexture = motion;
        d.pDepthTexture = depth; d.pResponsivePixelMaskTexture = reactive; d.pOutputTexture = state.output.Get();
        d.jitterOffsetX = jx; d.jitterOffsetY = -jy; d.exposureScale = 1.0f; d.resetHistory = reset;
        d.inputWidth = iw; d.inputHeight = ih;
        state.active = xessD3D12Execute(state.xess, cmd, &d) == XESS_RESULT_SUCCESS;
    }
    if (state.active) { D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = state.output.Get(); cmd->ResourceBarrier(1, &b);
        transition(cmd, state.output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        state.outputReadable = true; }
    return state.active;
}

bool upscalerAvailableForMode(int mode) { const int r = resolveMode(mode); return isDlss(r) ? dlaaAvailable() : isFsr(r) ? state.fsrRuntime : isXess(r) ? state.xessRuntime : r == HW_MODERN_AA_NATIVE_TAA; }
bool upscalerActive(void) { return state.active || dlaaActive(); }
UpscalerBackend upscalerResolvedBackend(void) { return state.backend; }
const char *upscalerResolvedName(void) { switch (state.backend) { case UpscalerBackend::NvidiaDlss:return "NVIDIA DLSS/DLAA"; case UpscalerBackend::AmdFsr:return "AMD FidelityFX Super Resolution"; case UpscalerBackend::IntelXess:return "Intel XeSS-SR"; case UpscalerBackend::NativeTaa:return "Native TAA"; default:return "spatial/native"; } }
ID3D12Resource *upscalerOutputResource(void) { return state.backend == UpscalerBackend::NvidiaDlss ? dlaaOutputResource() : state.output.Get(); }
}
