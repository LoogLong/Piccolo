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
        // Suppress the noisy CreateSampler2 "comparison func with
        // non-comparison filter" warning (D3D12_MESSAGE_ID 1361).
        // The engine creates comparison-capable samplers but uses them
        // as regular filter samplers; the warning is benign but fires
        // for every sampler created (~40 log lines per editor
        // startup).
        if (message_id == 1361)
        {
            return;
        }
        // Suppress DrawInstanced with InstanceCount=0 (D3D12_MESSAGE_ID
        // 1418). The path tracing / particle / debug-draw passes issue
        // empty draw calls when no instances are active (e.g. zero
        // lights to draw). D3D12 considers the draw a no-op; the
        // warning is benign but adds 1000+ lines per second to the
        // log. Add an `if (instanceCount > 0)` guard in the caller
        // separately to actually skip the draw.
        if (message_id == 1418)
        {
            return;
        }
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
    if (device == nullptr)
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

    // Review 2026-07-16: the validation layer's default filter is
    // INFO-severity only, which swallows the BREAKING_BREAK and
    // CORRUPTION / ERROR messages unless we explicitly opt them in.
    // Force-on every category + severity so the next E_INVALIDARG
    // from the path tracing dispatch surfaces the actual D3D12
    // message id + description (e.g. "D3D12 ERROR #540: ... resource
    // state (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) is invalid
    // for the command list's current state") instead of just the
    // generic Close() HRESULT.
    info_queue1->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
    info_queue1->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
    // Suppress the very noisy INFO-severity stream once the callback
    // is registered so the log file doesn't grow megabytes per frame.
    info_queue1->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, false);

    g_debug_layer_logging_active = true;
    LOG_INFO("D3D12 InfoQueue message callback registered (CORRUPTION/ERROR break-on enabled)");
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
