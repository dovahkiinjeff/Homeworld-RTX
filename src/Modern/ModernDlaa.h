#ifndef HW_MODERN_DLAA_H
#define HW_MODERN_DLAA_H

#if defined(_WIN32)

#include <d3d12.h>
#include <dxgi1_6.h>

namespace hwmodern
{

void dlaaEarlyInitialize(void);
void dlaaSetMode(int mode);
void dlaaSetRayReconstructionEnabled(bool enabled);
bool dlaaSetDevice(ID3D12Device *device, IDXGIAdapter1 *adapter);
void dlaaReleaseDevice(void);
bool dlaaCreateOutput(ID3D12DescriptorHeap *shaderVisibleHeap,
                      unsigned int descriptorSize,
                      unsigned int outputSrvIndex,
                      unsigned int width, unsigned int height);
bool dlaaGetOptimalRenderSize(int mode,
                              unsigned int outputWidth,
                              unsigned int outputHeight,
                              unsigned int *renderWidth,
                              unsigned int *renderHeight);
void dlaaReleaseOutput(void);
bool dlaaEvaluate(ID3D12GraphicsCommandList *commands,
                  ID3D12Resource *inputColor,
                  unsigned int inputWidth,
                  unsigned int inputHeight,
                  DXGI_FORMAT inputFormat,
                  ID3D12Resource *depth,
                  ID3D12Resource *motion,
                  float verticalFieldOfViewDegrees,
                  float aspectRatio,
                  bool resetHistory);
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
                  bool resetHistory);
bool dlaaAvailable(void);
bool dlaaActive(void);
bool dlaaRayReconstructionAvailable(void);
}

#endif

#endif
