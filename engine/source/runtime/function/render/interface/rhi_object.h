#pragma once

#include <string>

namespace Piccolo
{
    class RHIObject
    {
    public:
        virtual ~RHIObject() = default;

        void setDebugName(const char* name)
        {
            if (name != nullptr)
            {
                m_debug_name = name;
            }
        }

        const std::string& getDebugName() const { return m_debug_name; }
        const char*        debugNameCStr() const { return m_debug_name.empty() ? nullptr : m_debug_name.c_str(); }

    protected:
        RHIObject() = default;

    private:
        std::string m_debug_name;
    };
} // namespace Piccolo
