#pragma once

#ifdef _WIN32
struct ID3D12Device;

namespace Piccolo::d3d12_detail
{
bool setupD3D12DebugLayerLogging(ID3D12Device* device);
void shutdownD3D12DebugLayerLogging(ID3D12Device* device);
} // namespace Piccolo::d3d12_detail
#endif
