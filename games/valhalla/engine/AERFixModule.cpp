//#include "AERFixModule.h"
//#include "engine/memory/offsets.h"
//#include "sdk/ACVRGfxContext.h"
//#include <ModConfig.h>
//#include <ModSettings.h>
//#include <_deps/directxtk12-src/Src/d3dx12.h>
//#include <mods/VR.hpp>
//#include <safetyhook/easy.hpp>
//#include <utility/ScopeProfiler.h>
//
//void AERFixModule::InstallHooks() {
//    spdlog::info("Installing AERFixModule hooks");
//    auto onTAAPassFn = memory::on_taa_pass_addr();
//    m_onTAAPassHook  = safetyhook::create_inline((void*)onTAAPassFn, (void*)&AERFixModule::onTAAPass);
//    if (!m_onTAAPassHook) {
//        spdlog::error("Failed to hook m_onTAAPassHook");
//    }
//
//    auto onSetResourceBarriersFn = memory::on_set_resource_barriers_addr();
//    m_onSetResourceBarriersHook  = safetyhook::create_inline((void*)onSetResourceBarriersFn, (void*)&AERFixModule::onSetResourceBarriers);
//    if (!m_onSetResourceBarriersHook) {
//        spdlog::error("Failed to hook m_onSetResourceBarriersHook");
//    }
//
//    auto onCopyGfxContextFn = memory::on_gfx_context_copy();
//    m_onCopyGfxContext      = safetyhook::create_inline((void*)onCopyGfxContextFn, (void*)&AERFixModule::onCopyGfxContext);
//    if (!m_onCopyGfxContext) {
//        spdlog::error("Failed to hook m_onCopyGfxContext");
//    }
//
//}
//
//
//thread_local bool inside_taa = false;
//
//uintptr_t AERFixModule::onTAAPass(sdk::TaaPass* pass, sdk::TaaContext* context, uintptr_t a3)
//{
//    SCOPE_PROFILER();
//    static auto instance = Get();
//    inside_taa = true;
//    auto result =  instance->m_onTAAPassHook.call<uintptr_t>(pass, context, a3);
//    inside_taa = false;
//    return result;
//}
//
//void AERFixModule::onSetResourceBarriers(sdk::GlobalResourceManager* resourceManager, sdk::RenderContext* context) {
//    static auto instance = Get();
//    if(inside_taa) {
//        auto pBarriers = resourceManager->pBarriers;
//        auto count = resourceManager->numBarriers & 0x7FFF;
//        if(count == 4 && false) {
//            SCOPE_PROFILER();
//            instance->SwapBuffer(context->pCommandList, pBarriers[1].Transition.pResource, (D3D12_RESOURCE_STATES)pBarriers[1].Transition.StateBefore, 0);
//            instance->SwapBuffer(context->pCommandList, pBarriers[2].Transition.pResource, (D3D12_RESOURCE_STATES)pBarriers[2].Transition.StateBefore, 1);
//            instance->SwapBuffer(context->pCommandList, pBarriers[3].Transition.pResource, (D3D12_RESOURCE_STATES)pBarriers[3].Transition.StateBefore, 2);
//        }
////        for(UINT i = 0; i < count; i++) {
////            if(pBarriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
////                auto res = pBarriers[i].Transition.pResource;
////                auto state_before = (unsigned int)pBarriers[i].Transition.StateBefore;
////                auto state_after =(unsigned int) pBarriers[i].Transition.StateAfter;
////                spdlog::info(" Barrier[{}] Resource[{}] StateBefore[{:x}] StateAfter[{:x}]", i, fmt::ptr(res), state_before, state_after);
////            } else if(pBarriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING) {
////                auto res_before = pBarriers[i].Aliasing.pResourceBefore;
////                auto res_after = pBarriers[i].Aliasing.pResourceAfter;
////                spdlog::info(" Barrier[{}] Aliasing ResourceBefore[{}] ResourceAfter[{}]", i, fmt::ptr(res_before), fmt::ptr(res_after));
////            } else if(pBarriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_UAV) {
////                auto res = pBarriers[i].UAV.pResource;
////                spdlog::info(" Barrier[{}] UAV Resource[{}]", i, fmt::ptr(res));
////            }
////        }
//    }
//
//    instance->m_onSetResourceBarriersHook.call<void>(resourceManager, context);
//}
//
//
//uintptr_t AERFixModule::onCopyGfxContext(sdk::GfxContext* context, glm::mat4* viewMatrix)
//{
//    SCOPE_PROFILER();
//    static auto instance = Get();
//    static auto vr = VR::get();
//    static glm::mat4 copies[2]{glm::mat4{1.0},glm::mat4{1.0}};
//    auto result =  instance->m_onCopyGfxContext.call<uintptr_t>(context, viewMatrix);
//
//    if(false) {
//        auto frame = vr->m_presenter_frame_count;
//        copies[frame % 2] = context->pastViewMatrix;
//        context->pastViewMatrix = copies[(frame - 1) % 2];
//    }
//
//    return result;
//}
//
//
//bool AERFixModule::ValidateResource(ID3D12Resource* source, ComPtr<ID3D12Resource> pastBuffer[4])
//{
//    if (source == nullptr) {
//        return false;
//    }
//    D3D12_RESOURCE_DESC desc = source->GetDesc();
//    if (pastBuffer[0] != nullptr) {
//        D3D12_RESOURCE_DESC desc2 = pastBuffer[0]->GetDesc();
//        if (desc.Width != desc2.Width || desc.Height != desc2.Height || desc.Format != desc2.Format) {
//            spdlog::info("Resource size mismatch {} {} {} {} {} {} {} {}", fmt::ptr(source), desc.Width, desc.Height, desc.Format, fmt::ptr(pastBuffer[0].Get()), desc2.Width,
//                         desc2.Height, desc2.Format);
//            pastBuffer[0].Reset();
//            pastBuffer[1].Reset();
//            pastBuffer[2].Reset();
//            pastBuffer[3].Reset();
//        }
//    }
//    auto device = g_framework->get_d3d12_hook()->get_device();
//    if (device == nullptr) {
//        return false;
//    }
//    if (pastBuffer[0] == nullptr || pastBuffer[1] == nullptr) {
//        D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
//        for (int i = 0; i < 4; i++) {
//            if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
//                                                       IID_PPV_ARGS(pastBuffer[i].GetAddressOf()))))
//            {
//                spdlog::error("[VR] Failed to create resource copy copy for AFR backbuffer {}.", i);
//                return false;
//            }
//        }
//    }
//    return true;
//}
//
//
//void AERFixModule::CopyResource(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* pSrcResource, ID3D12Resource* pDstResource, D3D12_RESOURCE_STATES srcState,
//                                D3D12_RESOURCE_STATES dstState)
//{
//
//    D3D12_RESOURCE_BARRIER barriers[2] = { CD3DX12_RESOURCE_BARRIER::Transition(pSrcResource, srcState, D3D12_RESOURCE_STATE_COPY_SOURCE),
//                                           CD3DX12_RESOURCE_BARRIER::Transition(pDstResource, dstState, D3D12_RESOURCE_STATE_COPY_DEST) };
//    cmdList->ResourceBarrier(2, barriers);
//    cmdList->CopyResource(pDstResource, pSrcResource);
//    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
//    barriers[0].Transition.StateAfter  = srcState;
//    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
//    barriers[1].Transition.StateAfter  = dstState;
//    cmdList->ResourceBarrier(2, barriers);
//}
//
//void AERFixModule::SwapBuffer(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* originalBuffer, D3D12_RESOURCE_STATES state, int index) {
//    static auto vr = VR::get();
//
//    if (!vr->is_hmd_active() || !ValidateResource(originalBuffer, m_pastBuffer[index])) {
//        return;
//    }
//
//    auto cur_slot = vr->m_render_frame_count % 2;
//    auto past_slot = (vr->m_render_frame_count - 1) % 2;
//
//    auto saveBuffer       = m_pastBuffer[index][cur_slot].Get();
//    auto alterFrameBuffer = m_pastBuffer[index][past_slot].Get();
//    CopyResource(cmdList, originalBuffer, saveBuffer, state, D3D12_RESOURCE_STATE_GENERIC_READ);
//    CopyResource(cmdList, alterFrameBuffer, originalBuffer, D3D12_RESOURCE_STATE_GENERIC_READ, state);
//}
