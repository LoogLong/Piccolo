#include "runtime/function/render/interface/d3d12/d3d12_debug_layer.h"

#ifdef _WIN32

#include "runtime/core/base/macro.h"

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Piccolo::d3d12_detail
{
namespace
{
    DWORD  g_message_callback_cookie {0};
    bool   g_debug_layer_logging_active {false};

    bool isDebugLayerEnabledByEnvironment()
    {
        char debug_layer_env[16] {};
        const DWORD debug_layer_env_length =
            GetEnvironmentVariableA("PICCOLO_D3D12_DEBUG_LAYER",
                                    debug_layer_env,
                                    static_cast<DWORD>(sizeof(debug_layer_env)));
        return debug_layer_env_length > 0 && debug_layer_env_length < sizeof(debug_layer_env) &&
               (debug_layer_env[0] == '1' || debug_layer_env[0] == 't' || debug_layer_env[0] == 'T' ||
                debug_layer_env[0] == 'y' || debug_layer_env[0] == 'Y');
    }

    void CALLBACK d3d12DebugLayerMessageCallback(D3D12_MESSAGE_CATEGORY category,
                                                 D3D12_MESSAGE_SEVERITY severity,
                                                 D3D12_MESSAGE_ID id,
                                                 LPCSTR description,
                                                 void* context)
    {
        (void)category;
        (void)context;
        if (description == nullptr)
        {
            return;
        }

        const uint32_t message_id = static_cast<uint32_t>(id);
        switch (severity)
        {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            case D3D12_MESSAGE_SEVERITY_ERROR:
                LOG_ERROR("[D3D12] ERROR #{}: {}", message_id, description);
                break;
            case D3D12_MESSAGE_SEVERITY_WARNING:
                LOG_WARN("[D3D12] WARNING #{}: {}", message_id, description);
                break;
            default:
                break;
        }
    }
} // namespace

bool setupD3D12DebugLayerLogging(ID3D12Device* device)
{
    if (device == nullptr || !isDebugLayerEnabledByEnvironment())
    {
        return false;
    }

    ComPtr<ID3D12InfoQueue1> info_queue1;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&info_queue1))) || info_queue1 == nullptr)
    {
        LOG_WARN("ID3D12InfoQueue1 unavailable; D3D12 validation messages will not be logged to file");
        return false;
    }

    if (g_debug_layer_logging_active && g_message_callback_cookie != 0)
    {
        return true;
    }

    if (FAILED(info_queue1->RegisterMessageCallback(d3d12DebugLayerMessageCallback,
                                                    D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,
                                                    nullptr,
                                                    &g_message_callback_cookie)) ||
        g_message_callback_cookie == 0)
    {
        LOG_WARN("RegisterMessageCallback failed; D3D12 validation messages will not be logged to file");
        return false;
    }

    g_debug_layer_logging_active = true;
    LOG_INFO("D3D12 InfoQueue message callback registered");
    return true;
}

void shutdownD3D12DebugLayerLogging(ID3D12Device* device)
{
    if (!g_debug_layer_logging_active || device == nullptr || g_message_callback_cookie == 0)
    {
        return;
    }

    ComPtr<ID3D12InfoQueue1> info_queue1;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info_queue1))) && info_queue1 != nullptr)
    {
        info_queue1->UnregisterMessageCallback(g_message_callback_cookie);
    }

    g_message_callback_cookie      = 0;
    g_debug_layer_logging_active   = false;
}

} // namespace Piccolo::d3d12_detail

#endif
