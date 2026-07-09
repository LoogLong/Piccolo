#include "rhi_frame_retire.h"

#include "rhi.h"

namespace Piccolo
{
    void RHIFrameRetireQueue::retireBuffer(uint8_t slot, RHIBuffer*& buffer, RHIDeviceMemory*& memory)
    {
        if (buffer == nullptr && memory == nullptr)
        {
            return;
        }

        m_slots[normalizeSlot(slot)].buffers.push_back({buffer, memory});
        buffer = nullptr;
        memory = nullptr;
    }

    void RHIFrameRetireQueue::retireImage(uint8_t slot,
                                          RHIImage*& image,
                                          RHIImageView*& image_view,
                                          RHIDeviceMemory*& memory)
    {
        if (image == nullptr && image_view == nullptr && memory == nullptr)
        {
            return;
        }

        m_slots[normalizeSlot(slot)].images.push_back({image, image_view, memory});
        image      = nullptr;
        image_view = nullptr;
        memory     = nullptr;
    }

    void RHIFrameRetireQueue::retireAccelerationStructure(uint8_t slot,
                                                          RHIAccelerationStructure*& acceleration_structure)
    {
        if (acceleration_structure == nullptr)
        {
            return;
        }

        m_slots[normalizeSlot(slot)].acceleration_structures.push_back({acceleration_structure});
        acceleration_structure = nullptr;
    }

    void RHIFrameRetireQueue::flushRetiredResources(RHI* rhi, uint8_t slot)
    {
        if (rhi == nullptr)
        {
            return;
        }

        SlotBucket& bucket = m_slots[normalizeSlot(slot)];

        for (RHIFrameRetireBufferEntry& entry : bucket.buffers)
        {
            if (entry.buffer != nullptr)
            {
                rhi->destroyBuffer(entry.buffer);
            }
            if (entry.memory != nullptr)
            {
                rhi->freeMemory(entry.memory);
            }
        }
        bucket.buffers.clear();

        for (RHIFrameRetireImageEntry& entry : bucket.images)
        {
            if (entry.image_view != nullptr)
            {
                rhi->destroyImageView(entry.image_view);
            }
            if (entry.image != nullptr)
            {
                rhi->destroyImage(entry.image);
            }
            if (entry.memory != nullptr)
            {
                rhi->freeMemory(entry.memory);
            }
        }
        bucket.images.clear();

        for (RHIFrameRetireAccelerationStructureEntry& entry : bucket.acceleration_structures)
        {
            if (entry.acceleration_structure != nullptr)
            {
                rhi->destroyAccelerationStructure(entry.acceleration_structure);
            }
        }
        bucket.acceleration_structures.clear();
    }

    void RHIFrameRetireQueue::flushAllRetiredResources(RHI* rhi)
    {
        for (uint8_t slot = 0; slot < k_rhi_frame_retire_slot_count; ++slot)
        {
            flushRetiredResources(rhi, slot);
        }
    }
} // namespace Piccolo
