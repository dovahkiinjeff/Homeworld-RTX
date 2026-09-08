#ifndef HW_LEGACY_D3D12_RENDERER_H
#define HW_LEGACY_D3D12_RENDERER_H

#if defined(_WIN32) && defined(HW_ENABLE_D3D12_NATIVE_RASTER)
#include <cstddef>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace hwlegacyd3d12
{
bool initialize(ID3D12Device *device, unsigned int width, unsigned int height);
void shutdown();
bool resize(ID3D12Device *device, unsigned int width, unsigned int height);
void beginFrame(unsigned int frameSlot);
/* Exact native-raster depth used by ships/world geometry. The presenter calls
   prepareDepthForSampling only after raster replay is complete. */
ID3D12Resource *depthResource();
bool prepareDepthForSampling(ID3D12GraphicsCommandList *commandList);
struct SnapshotCopyTarget
{
    ID3D12Resource *target;
    D3D12_RESOURCE_STATES beforeState;
    D3D12_RESOURCE_STATES afterState;
    size_t commandCount;
};
bool renderFrameSnapshots(ID3D12Device *device,
                          ID3D12GraphicsCommandList *commandList,
                          ID3D12Resource *finalTarget,
                          D3D12_RESOURCE_STATES finalBeforeState,
                          D3D12_RESOURCE_STATES finalAfterState,
                          unsigned int width,
                          unsigned int height,
                          size_t finalCommandCount,
                          const SnapshotCopyTarget *copies,
                          size_t copyCount);
void releaseFrameResources(unsigned int frameSlot);
}
#endif

#endif
