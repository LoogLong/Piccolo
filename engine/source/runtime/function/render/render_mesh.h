#pragma once

#include "runtime/core/math/vector2.h"
#include "runtime/core/math/vector3.h"
#include "runtime/core/math/vector4.h"

#include <array>
#include "interface/rhi.h"

namespace Piccolo
{
    struct MeshVertex
    {
        struct VertexPosition
        {
            Vector3 position;
        };

        struct VertexVaryingEnableBlending
        {
            Vector3 normal;
            Vector3 tangent;
        };

        struct VertexVarying
        {
            Vector2 texcoord;
        };

        struct VertexJointBinding
        {
            int indices[4];
            Vector4  weights;
        };

        static std::array<RHIVertexInputBindingDescription, 3> getBindingDescriptions()
        {
            std::array<RHIVertexInputBindingDescription, 3> binding_descriptions {};

            // position
            binding_descriptions[0].binding   = 0;
            binding_descriptions[0].stride    = sizeof(VertexPosition);
            binding_descriptions[0].inputRate = RHI_VERTEX_INPUT_RATE_VERTEX;
            // varying blending
            binding_descriptions[1].binding   = 1;
            binding_descriptions[1].stride    = sizeof(VertexVaryingEnableBlending);
            binding_descriptions[1].inputRate = RHI_VERTEX_INPUT_RATE_VERTEX;
            // varying
            binding_descriptions[2].binding   = 2;
            binding_descriptions[2].stride    = sizeof(VertexVarying);
            binding_descriptions[2].inputRate = RHI_VERTEX_INPUT_RATE_VERTEX;
            return binding_descriptions;
        }

        static std::array<RHIVertexInputAttributeDescription, 4> getAttributeDescriptions()
        {
            std::array<RHIVertexInputAttributeDescription, 4> attribute_descriptions {};

            // position
            attribute_descriptions[0].binding  = 0;
            attribute_descriptions[0].location = 0;
            attribute_descriptions[0].format   = RHI_FORMAT_R32G32B32_SFLOAT;
            attribute_descriptions[0].offset   = offsetof(VertexPosition, position);

            // varying blending
            attribute_descriptions[1].binding  = 1;
            attribute_descriptions[1].location = 1;
            attribute_descriptions[1].format   = RHI_FORMAT_R32G32B32_SFLOAT;
            attribute_descriptions[1].offset   = offsetof(VertexVaryingEnableBlending, normal);
            attribute_descriptions[2].binding  = 1;
            attribute_descriptions[2].location = 2;
            attribute_descriptions[2].format   = RHI_FORMAT_R32G32B32_SFLOAT;
            attribute_descriptions[2].offset   = offsetof(VertexVaryingEnableBlending, tangent);

            // varying
            attribute_descriptions[3].binding  = 2;
            attribute_descriptions[3].location = 3;
            attribute_descriptions[3].format   = RHI_FORMAT_R32G32_SFLOAT;
            attribute_descriptions[3].offset   = offsetof(VertexVarying, texcoord);

            return attribute_descriptions;
        }
    };
} // namespace Piccolo
