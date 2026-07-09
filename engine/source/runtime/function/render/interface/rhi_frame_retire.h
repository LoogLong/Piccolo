#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Piccolo
{
    class RHI;
    class RHIBuffer;
    class RHIDeviceMemory;
    class RHIImage;
    class RHIImageView;
    class RHIAccelerationStructure;

    static constexpr uint8_t k_rhi_frame_retire_slot_count = 3;

    struct RHIFrameRetireBufferEntry
    {
        RHIBuffer*       buffer {nullptr};
        RHIDeviceMemory* memory {nullptr};
    };

    struct RHIFrameRetireImageEntry
    {
        RHIImage*        image {nullptr};
        RHIImageView*    image_view {nullptr};
        RHIDeviceMemory* memory {nullptr};
    };

    struct RHIFrameRetireAccelerationStructureEntry
    {
        RHIAccelerationStructure* acceleration_structure {nullptr};
    };

    // Per in-flight frame slot retirement queue. Resources are destroyed only when
    // waitForFences succeeds for the matching slot (RDG physical release timing).
    class RHIFrameRetireQueue
    {
    public:
        void retireBuffer(uint8_t slot, RHIBuffer*& buffer, RHIDeviceMemory*& memory);
        void retireImage(uint8_t slot,
                         RHIImage*& image,
                         RHIImageView*& image_view,
                         RHIDeviceMemory*& memory);
        void retireAccelerationStructure(uint8_t slot, RHIAccelerationStructure*& acceleration_structure);

        void flushRetiredResources(RHI* rhi, uint8_t slot);
        void flushAllRetiredResources(RHI* rhi);

    private:
        struct SlotBucket
        {
            std::vector<RHIFrameRetireBufferEntry>               buffers;
            std::vector<RHIFrameRetireImageEntry>                images;
            std::vector<RHIFrameRetireAccelerationStructureEntry> acceleration_structures;
        };

        static uint8_t normalizeSlot(uint8_t slot) { return static_cast<uint8_t>(slot % k_rhi_frame_retire_slot_count); }

        std::array<SlotBucket, k_rhi_frame_retire_slot_count> m_slots {};
    };
} // namespace Piccolo
