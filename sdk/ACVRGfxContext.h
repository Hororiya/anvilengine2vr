#pragma once
#include "d3d12.h"
#include <glm/detail/type_mat4x4.hpp>
#include <glm/detail/type_quat.hpp>

namespace sdk {

#pragma pack(push, 1)


    // Forward declarations
    struct RenderContext;
    struct RenderingManager;

    struct Viewport {
        float left;
        float top;
        float right;
        float bottom;
    };

    struct CameraObject
    {
        struct CameraObject_vtbl *__vftable /*VFT*/;
        void *pSomeResource_8;
        char pad_0[64];
        void *pSomeResource_80;
        char pad_1[92];
        int cameraMode;
        char pad_22[16];
        void *pSubObject_200;
        struct re_SubObject208 *pSubObject_208_duplicate;
        char pad_2[152];
        glm::mat4 view_matrix;
        glm::vec4 interpolated_position;
        glm::vec4 interpolated_rotation_quaternion;
        float interpolated_fov;
        char pad_3[8];
        void *pSomeContainer_480;
        char pad_4[32];
        void *pSomeResource_520;
    };

    struct CameraNode
    {
        void *vftable;
        char data8[88];
        glm::vec4 prevPosition;
        glm::quat prevRotation;
        __int64 unk1[2];
        glm::vec4 position;
        glm::quat rotation;
        char dataB0[780];
        struct TransformProvider *pTransformProvider;
        struct ViewMatrixCalculator *pViewMatrixCalculator;
        struct ProjectionMatrixCalculator *pProjectionMatrixCalculator;
        struct AimUiCameraSub3D8 *pAimUiCameraSub3D8;
        struct ProjectionMatrixProvider *pProjectionMatrixProvider;
        struct UiCameraReticle *pUiCameraReticle;
        struct LensProvider *pLensProvider;
        char data3f8[4096];
    };

    typedef __m128 Vector4; // A 16-byte vector type, common for SIMD operations

    typedef struct {
        Vector4 rows[4];
    } Matrix4x4; // A 4x4 matrix, 64 bytes

    typedef struct {
        float x;
        float y;
        float width;
        float height;
    } ScissorRect; // A 16-byte rectangle for scissoring

    // Forward declaration for a pointer member
    struct ViewportInfo;

    // The reconstructed GfxContext structure
    struct GfxContext {
        // Offset: 0x000 (a1)
        // This is the base of the structure, used as the 'in_viewMatrix' argument.
        glm::mat4 viewMatrix;

        // ... other members not used by this function
        char unknown_padding_0x040[0x200];

        // Offset: 0x240 (context + 576)
        // An optional override for the view matrix.
        Matrix4x4* pOverrideViewMatrix;

        char unknown_padding_0x248[0x8];

        // Offset: 0x250 (a1[37])
        // Contains scale and bias values to normalize texture coordinates.
        Vector4 textureScaleBias;

        // Offset: 0x260 (a1[38])
        // Parameters passed to the projection matrix calculation.
        Vector4 projectionParams;

        // Offset: 0x270 (a1[39])
        // The final scissor rectangle with integer (pixel) coordinates.
        ScissorRect scissorRectInt;

        // Offset: 0x280 (a1[40])
        // The scissor rectangle with floating-point coordinates.
        ScissorRect scissorRect;

        // Offset: 0x290 (a1[41])
        // The viewport definition.
        Viewport viewport;

        // Offset: 0x2A0 (context + 672)
        // A flag or type field checked before creating the projection matrix.
        int projectionTypeFlag; // Corresponds to 'v55' in the code

        // Offset: 0x2A4 (context + 676)
        // The ID of the render target texture.
        int textureId;

        // Offset: 0x2A8
        // The type of matrix to generate (perspective vs. orthographic).
        int matrixType;

        // Offset: 0x2AC
        int unknown_int_2AC;

        char unknown_padding_0x2B0[0x10];

        // Offset: 0x2C0 (context + 704)
        // The initial aspect ratio before adjustments.
        float initialAspectRatio;

        // Offset: 0x2C4 (context + 708)
        // An array of floats, likely FOV or ortho size values, indexed by matrixType.
        float fov_or_ortho_params[4];

        char unknown_padding_0x2D4[0xC];

        // Offset: 0x2E0 (a1[46])
        // Scale factors for the scissor rectangle.
        Vector4 scissorScale;

        // Offset: 0x2F0
        Vector4 unknown_vec_2F0; // The last float of this is the final aspect ratio.
                                 // &a1[47].m128_f32[3] points to 0x2FC

        char unknown_padding_0x300[0x10];

        // --- OUTPUT MATRICES START HERE ---

        // Offset: 0x310 (a1 + 49)
        Matrix4x4 viewProjectionMatrix;

        // Offset: 0x350 (a1 + 53)
        Matrix4x4 projectionMatrix;

        // Offset: 0x390 (a1 + 57)
        Matrix4x4 inverseViewMatrix;

        // Offset: 0x3D0 (a1 + 61)
        Matrix4x4 inverseProjectionMatrix;

        // Offset: 0x410 (a1 + 65)
        Matrix4x4 worldToViewMatrix;

        // Offset: 0x450 (context + 1104)
        // Pointer to another structure containing detailed viewport info.
        ViewportInfo* pViewportInfo;

        char unknown_padding_0x458[0x8];

        // Offset: 0x460 (a1 + 70)
        // The 6 planes of the viewing frustum, calculated at the end.
        Vector4 frustumPlanes[6];

        // ... many more members exist beyond this point
        char unknown_padding_0x4C0[528];
        glm::mat4 pastViewMatrix;

        // Offset: 0x710 (context + 1808)
        // A scaling factor used in the scissor rectangle calculation.
        float renderScaleFactor;

        // ... structure continues
    };

    struct GlobalResourceManager
    {
        void* pSomeArrayBase;                           // Offset 0x0
        char unknown_0[72];
        D3D12_RESOURCE_BARRIER* pBarriers;              // Offset 0x50 (80)
        unsigned int numBarriers;                       // Offset 0x58 (88)
        char unknown_1[1604];                           // Corrected size
        void** ppResourcesToDiscard;                    // Offset 0x6A0 (1696)
        unsigned int numResourcesToDiscard;             // Offset 0x6A8 (1704)
        char unknown_2[420];                            // Corrected size
        unsigned int someIndex;                         // Offset 0x850 (2128)
    };

    struct VertexBuffers
    {
        int startSlotIndex;
        int unk4;
        int numViews;
        D3D12_VERTEX_BUFFER_VIEW buffers[6];
    };

    struct ViewportAndScissorInfo
    {
        char unknown_0[3];
        D3D12_VIEWPORT viewports[14];
        char unknown_1[24];
        unsigned int viewport_count_1;
        char unknown_2[4];
        unsigned int viewport_count_2;
        D3D12_RECT scissor_rects[14];
        char unknown_3[16];
        unsigned int scissor_count_1;
        char unknown_4[4];
        unsigned int scissor_count_2;
    };

    struct RenderTargetViews {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[8];
    };

    struct RenderTargetInfo {
        char unknown_0[688];
        bool setRenderTargets_flag;
        char unknown_1[7];
        RenderTargetViews* pRenderTargetViews;
        char unknown_2[80];
        bool setDepthStencil_flag;
        char unknown_3[7];
        D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilView;
        char unknown_4[80];
    };

    struct RenderingManager
    {
        char unknown_0[168];
        RenderContext **ppContextObject;
        RenderTargetInfo renderTargetInfo;
        bool setIndexBuffer_flag;
        char padding_1[7];
        GlobalResourceManager resourceManager;
        char padding_2[4];
        D3D12_PRIMITIVE_TOPOLOGY primitiveTopology;
        bool setPrimitiveTopology_flag;
        ViewportAndScissorInfo viewportAndScissorInfo;
        unsigned int vertexBuffer_count;
        VertexBuffers vertexBuffers;
        D3D12_INDEX_BUFFER_VIEW indexBufferView;
        char padding_F78[8];
        D3D12_STREAM_OUTPUT_BUFFER_VIEW streamOutputBufferView;
        char padding_F98[76];
        unsigned int stencilRef;
        bool setStencilRef_flag;
        bool setBlendFactor_flag;
        char unknown_final[483];
    };

    struct RenderContext
    {
        char unknown_0[112];
        ID3D12GraphicsCommandList* pCommandList;
        char unknown_1[8];
        bool isDispatch;
    };

    // This class wraps the command list and related objects.
    struct CmdListExecutor
    {
        char unknown_0[592];
        RenderingManager* pRenderingManager;        // Offset 0x250 (592)
        char unknown_2[8];
        RenderContext* pRenderContext;            // Offset 0x260 (608)
        char unknown_padding_1[8];
        ID3D12GraphicsCommandList* pCachedCommandList; // Offset 0x270 (624)
        char unknown_padding_2[16];
        ID3D12CommandSignature* pCommandSignature; // Offset 0x288 (648)
        char unknown_3[24];
        ID3D12Resource* pPredicationResource;     // Offset 0x2A8 (680)
        char unknown_4[48];
        bool bFlag_736;                           // Offset 0x2E0 (736)
        bool bFlag_737;                           // Offset 0x2E1 (737)
    };

    struct TaaPass
    {
        CmdListExecutor* pCmdListExecutor; // Offset 0x0

        // Adding a payload to represent the rest of the object.
        // The access at a1[222] implies a size of at least
        // (222 * 8) + 8 = 1784 bytes.
        char unknown_payload[1776];
    };

#pragma pack(pop)
}
