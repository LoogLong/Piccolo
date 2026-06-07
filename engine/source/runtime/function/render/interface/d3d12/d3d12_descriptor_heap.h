#pragma once

#include <cstdint>

#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>

namespace Piccolo
{
    class D3D12DescriptorHeapAllocator final
    {
    public:
        D3D12DescriptorHeapAllocator() = default;
        ~D3D12DescriptorHeapAllocator() = default;

        D3D12DescriptorHeapAllocator(const D3D12DescriptorHeapAllocator&) = delete;
        D3D12DescriptorHeapAllocator& operator=(const D3D12DescriptorHeapAllocator&) = delete;

        D3D12DescriptorHeapAllocator(D3D12DescriptorHeapAllocator&&) noexcept = default;
        D3D12DescriptorHeapAllocator& operator=(D3D12DescriptorHeapAllocator&&) noexcept = default;

        void initialize(ID3D12Device* device,
                        D3D12_DESCRIPTOR_HEAP_TYPE type,
                        uint32_t count,
                        bool shader_visible);

        uint32_t allocate(uint32_t count = 1);

        D3D12_CPU_DESCRIPTOR_HANDLE cpu(uint32_t index) const;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu(uint32_t index) const;

        ID3D12DescriptorHeap* heap() const;
        uint32_t descriptorSize() const;

        void reset();

    private:
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
        uint32_t                                     m_descriptor_size {0};
        uint32_t                                     m_capacity {0};
        uint32_t                                     m_next {0};
        bool                                         m_shader_visible {false};
    };
} // namespace Piccolo
#endif
