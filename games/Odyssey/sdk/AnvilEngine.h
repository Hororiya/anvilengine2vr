#pragma once

namespace sdk
{
#pragma pack(push, 1)
    struct Matrix4x4
    {
        float m[4][4];
    };

    struct InnerObject
    {
        char       pad_0[0x10]; // 16 bytes padding
        Matrix4x4  pMatrix;     // 0x10 (16)
        char       pad_1[0x8];  // 8 bytes padding
        bool       is_dirty;    // 0x18 (24)
    };

    struct CachedObject
    {
        void**              vftable;      // 0x00
        volatile signed int ref_count;    // 0x08
        char                pad_0[0x1C];  // 28 bytes padding
        InnerObject*        pInnerObject; // 0x20 (32)
    };

    struct CachedResourceHandle
    {
        CachedObject* pObject; // 0x00
        bool          someFlag;  // 0x08
    };

    struct ProjectionParameters
    {
        char  pad_0[0x14];
        float Exposure;
        float AspectRatio;
        float NearPlane;
        float FarPlane;
        float FarShadowDistance;
        float field_40;
        float field_44;
        float FrustumWidth;
        float FrustumHeight;
        char  pad_1[0x8];
        float FarShadowFalloff;
        float field_64;
    };

    struct Camera
    {
        char  pad_0[128];
        float field_128;
        char  pad_1[16];
        float field_148;
    };

#pragma pack(pop)
}
