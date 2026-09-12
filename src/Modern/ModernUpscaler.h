#ifndef HW_MODERN_UPSCALER_H
#define HW_MODERN_UPSCALER_H

#if defined(_WIN32)

#include <d3d12.h>
#include <dxgi1_6.h>

namespace hwmodern
{

enum class UpscalerBackend
{
    None,
    NativeTaa,
    NvidiaDlss,
    AmdFsr,
    IntelXess
};

void upscalerSetRequestedMode(int mode);
bool upscalerSetDevice(ID3D12Device *device, IDXGIAdapter1 *adapter);
void upscalerReleaseDevice(void);
bool upscalerCreateOutput(ID3D12DescriptorHeap *shaderVisibleHeap,
                          unsigned int descriptorSize,
                          unsigned int outputSrvIndex,
                          unsigned int width, unsigned int height);
void upscalerReleaseOutput(void);
bool upscalerGetRenderSize(int mode, unsigned int outputWidth,
                           unsigned int outputHeight,
                           unsigned int *renderWidth,
                           unsigned int *renderHeight);
bool upscalerEvaluate(ID3D12GraphicsCommandList *commands,
                      ID3D12Resource *inputColor,
                      unsigned int inputWidth,
                      unsigned int inputHeight,
                      DXGI_FORMAT inputFormat,
                      ID3D12Resource *depth,
                      ID3D12Resource *motion,
                      ID3D12Resource *reactiveMask,
                      float verticalFieldOfViewDegrees,
                      float jitterX, float jitterY,
                      float frameTimeMilliseconds,
                      bool resetHistory);
bool upscalerAvailableForMode(int mode);
bool upscalerActive(void);
UpscalerBackend upscalerResolvedBackend(void);
const char *upscalerResolvedName(void);
ID3D12Resource *upscalerOutputResource(void);

}

#endif
#endif
