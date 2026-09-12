#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

namespace hwmodern
{
bool fsrFrameGenerationWrapSwapchain(IDXGISwapChain4 **swapchain,
                                     ID3D12CommandQueue *queue);
void fsrFrameGenerationRelease(void);
void fsrFrameGenerationSetEnabled(bool enabled);
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
                               bool reset, unsigned long long frameId);
bool fsrFrameGenerationAvailable(void);
bool fsrFrameGenerationActive(void);
}
