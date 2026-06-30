#include "runtime/function/render/interface/d3d12/d3d12_descriptor_heap.h"

#ifdef _WIN32
#include <stdexcept>

namespace Piccolo
{
    namespace
    {
        void requireInitialized(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& heap)
        {
            if (heap.Get() == nullptr)
            {
                throw std::runtime_error("D3D12 descriptor heap allocator is not initialized");
            }
        }
    } // namespace

    void D3D12DescriptorHeapAllocator::initialize(ID3D12Device* device,
                                                  D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                  uint32_t count,
                                                  bool shader_visible)
    {
        if (device == nullptr)
        {
            throw std::runtime_error("D3D12 descriptor heap allocator requires a valid device");
        }
        if (count == 0)
        {
            throw std::runtime_error("D3D12 descriptor heap allocator requires at least one descriptor");
        }

        D3D12_DESCRIPTOR_HEAP_DESC desc {};
        desc.Type           = type;
        desc.NumDescriptors = count;
        desc.Flags          = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE :
                                               D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask       = 0;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap))))
        {
            throw std::runtime_error("Failed to create D3D12 descriptor heap");
        }

        const uint32_t descriptor_size = device->GetDescriptorHandleIncrementSize(type);
        if (descriptor_size == 0)
        {
            throw std::runtime_error("Failed to query D3D12 descriptor heap increment size");
        }

        m_heap            = heap;
        m_descriptor_size = descriptor_size;
        m_capacity        = count;
        m_next            = 0;
        m_shader_visible  = shader_visible;
    }

    uint32_t D3D12DescriptorHeapAllocator::allocate(uint32_t count)
    {
        requireInitialized(m_heap);

        if (count == 0)
        {
            throw std::runtime_error("D3D12 descriptor heap allocator cannot allocate zero descriptors");
        }
        if (m_next > m_capacity || count > m_capacity - m_next)
        {
            throw std::runtime_error("D3D12 descriptor heap allocator exceeded capacity");
        }

        const uint32_t base = m_next;
        m_next += count;
        return base;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeapAllocator::cpu(uint32_t index) const
    {
        requireInitialized(m_heap);

        if (index >= m_capacity)
        {
            throw std::runtime_error("D3D12 descriptor heap CPU handle index is out of range");
        }

        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptor_size) * index;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorHeapAllocator::gpu(uint32_t index) const
    {
        requireInitialized(m_heap);

        if (!m_shader_visible)
        {
            throw std::runtime_error("D3D12 descriptor heap GPU handle requires a shader-visible heap");
        }
        if (index >= m_capacity)
        {
            throw std::runtime_error("D3D12 descriptor heap GPU handle index is out of range");
        }

        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_heap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(m_descriptor_size) * index;
        return handle;
    }

    ID3D12DescriptorHeap* D3D12DescriptorHeapAllocator::heap() const
    {
        return m_heap.Get();
    }

    uint32_t D3D12DescriptorHeapAllocator::descriptorSize() const
    {
        return m_descriptor_size;
    }

    void D3D12DescriptorHeapAllocator::reset()
    {
        requireInitialized(m_heap);
        m_next = 0;
    }
} // namespace Piccolo
#endif
