# Assassin's Creed Origins VR Mod - Development Guide

This document provides instructions for adding VR support to AC Origins based on the existing AC Odyssey implementation. Since both games use the **AnvilNext 2.0 engine**, the porting process should be significantly easier than starting from scratch.

## Overview

Origins and Odyssey share the same engine architecture, meaning:
- **Many byte signatures may match directly** between the two games
- SDK structures (Camera, CameraNode, etc.) are likely identical or very similar
- Hook points for camera/renderer/projection should be the same functions
- Memory offsets within structures may differ slightly

## Directory Structure

Create the following structure (mirroring Odyssey):

```
games/Origins/
├── ModConfig.cpp
├── ModConfig.h
├── engine/
│   ├── EngineCameraModule.cpp
│   ├── EngineCameraModule.h
│   ├── EngineEntry.cpp
│   ├── EngineEntry.h
│   ├── EngineRendererModule.cpp
│   ├── EngineRendererModule.h
│   └── memory/
│       └── offsets.h
└── sdk/
    └── AnvilEngine.h
```

## Step 1: Find Function Signatures

Use IDA Pro or Ghidra to locate the following key functions in Origins. **Start by trying the Odyssey signatures first** - they may work directly or with minor modifications.

### Key Functions to Hook

Reference the Odyssey `offsets.h` patterns and look for matching or similar functions:

| Function | Odyssey Pattern | Purpose |
|----------|-----------------|---------|
| `CalculatePerspectiveProjectionMatrix` | `48 89 E0 53 48 81 EC 90 00 00 00 0F 29 70 E8 48 89 CB F3` | Projection matrix calculation |
| `UpdateCameraMatricesAndFrustum` | `48 89 E0 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 C8` | Main MVP calculation |
| `CameraNode::GetForward` | `0F 28 59 70 48 89` | Camera forward vector |
| `CalcUIViewport` | `55 48 8D 6C 24 A9 48 81 EC E0 00 00 00 48 8B 05 ? ? ? ? 48 31 E0 48 89 45 2F` | UI viewport calculation |

## IDA Pseudocode Reference

Below are the full decompiled function bodies from Odyssey. Use these to identify visually similar functions in Origins - the variable names and addresses will differ, but the structure and SIMD operations should be recognizable:

### CalculatePerspectiveProjectionMatrix
```c++
float *__fastcall CalculatePerspectiveProjectionMatrix(
        __m128 *pOutMatrix,
        float nearPlane,
        float farPlane,
        float fovy,
        float aspectRatio,
        float *pInputVector)
{
  // Local variables omitted - will differ between builds
  
  v7 = *(float *)dword_14407626C;
  v9 = *(__m128 *)&nearPlane;
  v11.m128_u64[1] = v10;
  *(double *)v11.m128_u64 = sdk_arcsin_conceptual();
  result = pInputVector;
  v13 = v11;
  v13.m128_f32[0] = v11.m128_f32[0] * nearPlane;
  v14 = pInputVector[1];
  v11.m128_f32[0] = v14 + pInputVector[3];
  v15 = v14 - v7;
  v16 = _mm_xor_ps(v13, (__m128)xmmword_144076540[0].m128_u32[0]);
  v17 = v13;
  v18 = (float)(*pInputVector + pInputVector[2]) - v7;
  v19 = (float)(*pInputVector - v7) + (float)(*pInputVector - v7);
  v11.m128_f32[0] = v11.m128_f32[0] - v7;
  v20 = (__m128)dword_144076294[0];
  LODWORD(v21) = COERCE_UNSIGNED_INT(v19 * (float)(v16.m128_f32[0] * aspectRatio)) ^ xmmword_144076540[0].m128_i32[0];
  v22 = (float)(v15 + v15) * v13.m128_f32[0];
  v17.m128_f32[0] = (float)(v13.m128_f32[0] * aspectRatio) * (float)(v18 + v18);
  v23 = *(__m128 *)&nearPlane;
  v16.m128_f32[0] = v16.m128_f32[0] * (float)(v11.m128_f32[0] * *(float *)&dword_144076294[11]);
  v23.m128_f32[0] = nearPlane + nearPlane;
  v24 = LODWORD(v22) ^ xmmword_144076540[0].m128_i32[0];
  v25 = *(float *)dword_144076294;
  v26 = v23;
  v27 = *(float *)dword_144076294 / (float)(v17.m128_f32[0] - v21);
  v26.m128_f32[0] = (float)(v9.m128_f32[0] + v9.m128_f32[0]) * v27;
  v17.m128_f32[0] = (float)(v17.m128_f32[0] + v21) * v27;
  *pOutMatrix = _mm_unpacklo_ps(_mm_unpacklo_ps(v26, (__m128)0i64), (__m128)0i64);
  v28 = v25 / (float)(*(float *)&v24 - v16.m128_f32[0]);
  v23.m128_f32[0] = (float)(v9.m128_f32[0] + v9.m128_f32[0]) * v28;
  v16.m128_f32[0] = (float)(v16.m128_f32[0] + *(float *)&v24) * v28;
  v29 = (__m128)flt_144077890;
  pOutMatrix[1] = _mm_unpacklo_ps((__m128)0i64, _mm_unpacklo_ps(v23, (__m128)0i64));
  v20.m128_f32[0] = v20.m128_f32[0] / (float)(v9.m128_f32[0] - farPlane);
  v30 = v20;
  v30.m128_f32[0] = v20.m128_f32[0] * farPlane;
  v9.m128_f32[0] = (float)(v9.m128_f32[0] * farPlane) * v20.m128_f32[0];
  pOutMatrix[2] = _mm_unpacklo_ps(_mm_unpacklo_ps(v17, v30), _mm_unpacklo_ps(v16, v29));
  pOutMatrix[3] = _mm_unpacklo_ps(_mm_unpacklo_ps((__m128)0i64, v9), (__m128)0i64);
  return result;
}
```

### UpdateCameraMatricesAndFrustum
```c++
__int64 __fastcall UpdateCameraMatricesAndFrustum(
        __int64 pCamera,
        _DWORD *a2,
        float *pNormalizedViewportRect,
        __int64 pOutRenderDimensions,
        float *pOutAbsoluteViewport,
        float *pOutScaledViewport,
        float *pOutViewportScale,
        float *viewport,
        Matrix4x4 *pOutViewMatrix,
        Matrix4x4 *pOutViewProjectionMatrix,
        __m128 *pOutProjectionMatrix,
        Matrix4x4 *pOutInverseViewMatrix,
        Vector4 *pOutInverseProjectionMatrix,
        Vector4 *pOutInvViewProjMatrix,
        float *fovY,
        float *pAspectRatio,
        float nearPlane,
        float farPlane,
        __m128 *pFrustum,
        _DWORD *a20)
{
  // Local variables omitted - will differ between builds
  
  v88 = (unsigned __int64)&v72 ^ qword_14568ABC0;
  v83 = pOutInverseViewMatrix;
  v77 = pOutAbsoluteViewport;
  v80 = pOutViewportScale;
  v82 = pOutProjectionMatrix;
  v84 = pOutInverseProjectionMatrix;
  aspectRatio = pAspectRatio;
  v22 = *(_QWORD *)(pCamera + 1104);
  v79 = pOutRenderDimensions;
  v78 = pOutScaledViewport;
  if ( v22 )
  {
    v23 = *(unsigned int *)(*(_QWORD *)(v22 + 96) + 176i64);
    if ( *(_DWORD *)(pCamera + 648) != (_DWORD)v23 )
    {
      *a2 = v23;
      j_REAL::CalculateNormalizedViewportRect(pNormalizedViewportRect, *(_QWORD *)(pCamera + 1104) + 8i64, v23, a20);
    }
  }
  v24 = *(Matrix4x4 **)(pCamera + 576);         // worldMatrixOverride
  if ( v24 )
    *pOutViewMatrix = *v24;
  REAL::CalculateLeftHandedViewMatrix(&v87, pOutViewMatrix);
  j_REAL::CalculateAbsoluteViewportRect(pCamera, (__int64)&v73, pNormalizedViewportRect);
  v25 = *(float *)dword_144076294;
  v26 = fmaxf(*(float *)(pCamera + 1840), *(float *)&dword_1440778B4);
  v27 = *(float *)dword_144076294;
  if ( (unsigned __int8)sub_140BF0590() )
    v28 = dword_1440C134C;
  else
    v28 = dword_1440C136C;
  GraphicsManagerInstance = j_REAL::GetGraphicsManagerInstance();
  DisplayAspectRatio = j_REAL::GetDisplayAspectRatio((__int64)GraphicsManagerInstance);
  if ( DisplayAspectRatio >= *(float *)&dword_1440779DC[66] )
  {
    if ( DisplayAspectRatio > *(float *)&v28 )
      v25 = *(float *)&v28 / DisplayAspectRatio;
  }
  else
  {
    v27 = DisplayAspectRatio * *(float *)&dword_1440C1334;
  }
  v31 = a20[2];
  v32 = v75 * v25;
  v33 = v76 * v27;
  v34 = v75 * v25;
  v35 = v75 * v25;
  v36 = v33;
  v37 = v33;
  v38 = (float)((float)(v75 - (float)(v75 * v25)) * *(float *)dword_14407626C) + v73;
  v39 = (float)((float)(v76 - v33) * *(float *)dword_14407626C) + v74;
  if ( v31 == 2 )
  {
    v35 = (float)(int)a20[54];
    v37 = (float)(int)a20[55];
  }
  else if ( v31 == 1 )
  {
    v34 = (float)(int)a20[54];
    v36 = (float)(int)a20[55];
    v35 = v34;
    v37 = v36;
    v38 = (float)((float)(v75 - v34) * *(float *)dword_14407626C) + v73;
    v39 = (float)((float)(v76 - v36) * *(float *)dword_14407626C) + v74;
  }
  v40 = v77;
  v41 = 0i64;
  v42 = v78;
  poutProjectionMatrix = (Matrix4x4 *)v82;
  *v77 = v38;
  v40[3] = v36;
  v40[2] = v34;
  v40[1] = v39;
  *v42 = v38;
  v42[1] = v39;
  v42[2] = (float)(int)(((int)(float)(v35 * v26) + 1) & 0xFFFFFFFE);
  v44 = v79;
  v42[3] = (float)(int)(((int)(float)(v37 * v26) + 1) & 0xFFFFFFFE);
  *(float *)(v44 + 8) = v32;
  *(float *)(v44 + 12) = v33;
  *(_QWORD *)v44 = 0i64;
  v45 = v80;
  v46 = v42[3] / v40[3];
  *v80 = v42[2] / v40[2];
  v45[1] = v46;
  j_REAL::CalculatePerspectiveProjectionMatrix_stack1(
    pCamera,
    v32,
    v33,
    nearPlane,
    farPlane,
    *(float *)&fovY,
    *(_DWORD *)(pCamera + 616),
    viewport,
    (__m128 *)poutProjectionMatrix,
    aspectRatio);
  v86.c[0] = v87.c[0];
  v86.c[1] = v87.c[1];
  v86.c[2] = v87.c[2];
  v47 = poutProjectionMatrix->c[0];
  v86.c[3] = v87.c[3];
  v48 = poutProjectionMatrix->c[1];
  v85.c[0] = v47;
  v49 = poutProjectionMatrix->c[2];
  v85.c[1] = v48;
  v50 = poutProjectionMatrix->c[3];
  v85.c[2] = v49;
  v85.c[3] = v50;
  j_Matrix4x4_Multiply(pOutViewProjectionMatrix, &v85, &v86);
  v86 = v87;
  j_Matrix4x4_Inverse(&v86);
  v51 = v83;
  v52 = v86.c[1];
  v83->c[0] = v86.c[0];
  v53 = v86.c[2];
  v51->c[1] = v52;
  v54 = v86.c[3];
  v51->c[2] = v53;
  v51->c[3] = v54;
  v55 = poutProjectionMatrix->c[1];
  v86.c[0] = poutProjectionMatrix->c[0];
  v56 = poutProjectionMatrix->c[2];
  v86.c[1] = v55;
  v57 = poutProjectionMatrix->c[3];
  v86.c[2] = v56;
  v86.c[3] = v57;
  j_Matrix4x4_Inverse(&v86);
  v58 = v84;
  v59 = v86.c[0];
  v60 = v86.c[1];
  v61 = v86.c[2];
  v62 = v86.c[3];
  *v84 = v86.c[0];
  v58[1] = v60;
  v58[2] = v61;
  v58[3] = v62;
  v63 = (__m128)v51->c[0];
  v64 = (__m128)v51->c[1];
  v65 = (__m128)v51->c[2];
  v66 = (__m128)v51->c[3];
  v85.c[0] = v59;
  v85.c[1] = v60;
  v85.c[2] = v61;
  v85.c[3] = v62;
  do
  {
    v67 = (__m128)v85.c[v41];
    v86.c[v41++] = (Vector4)_mm_add_ps(
                              _mm_add_ps(
                                _mm_add_ps(
                                  _mm_mul_ps(_mm_shuffle_ps(v67, v67, 85), v64),
                                  _mm_mul_ps(_mm_shuffle_ps(v67, v67, 0), v63)),
                                _mm_mul_ps(_mm_shuffle_ps(v67, v67, 170), v65)),
                              _mm_mul_ps(_mm_shuffle_ps(v67, v67, 255), v66));
  }
  while ( v41 < 4 );
  v68 = v86.c[1];
  *pOutInvViewProjMatrix = v86.c[0];
  v69 = v86.c[2];
  pOutInvViewProjMatrix[1] = v68;
  v70 = v86.c[3];
  pOutInvViewProjMatrix[2] = v69;
  pOutInvViewProjMatrix[3] = v70;
  return REAL::UpdateFrustumPlanesFromVPMatrix(pFrustum, (__m128 *)pOutViewProjectionMatrix);
}
```

### CameraNode::GetForward (Quaternion to Forward Vector)
```c++
__m128 *__fastcall getForwardVector(__m128 *a1, __m128 *a2)
{
  // Local variables omitted
  
  v2 = a1[7];
  result = a2;
  v4 = _mm_sub_ps(
         _mm_mul_ps(_mm_shuffle_ps(v2, v2, 210), (__m128)stru_144076380[0]),
         _mm_mul_ps(_mm_shuffle_ps((__m128)stru_144076380[0], (__m128)stru_144076380[0], 210), v2));
  v5 = _mm_shuffle_ps(v2, v2, 255);
  *a2 = _mm_mul_ps(
          _mm_add_ps(
            _mm_add_ps(
              _mm_mul_ps(_mm_dp_ps((__m128)stru_144076380[0], v2, 127), v2),
              _mm_mul_ps(_mm_shuffle_ps(v4, v4, 210), v5)),
            _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(v5, v5), stru_1440764B0), (__m128)stru_144076380[0])),
          stru_144082670[0]);
  return result;
}
```

## Step 2: Create offsets.h

Create `games/Origins/engine/memory/offsets.h` by copying from Odyssey:

```cpp
#pragma once
#include "memory/memory_mul.h"

namespace memory
{
    // Copy structure from games/Odyssey/engine/memory/offsets.h
    // Use patterns from the table above (Step 1)
    // Set fallback addresses to 0x0 initially, update after finding functions
}
```

See [games/Odyssey/engine/memory/offsets.h](../Odyssey/engine/memory/offsets.h) for the full implementation template.

## Step 3: Tips for Finding Functions

1. **Search for string references** - Look for debug strings like "Camera", "Projection", "Viewport"
2. **Look for SIMD patterns** - Camera/matrix functions use `__m128`, `_mm_*` intrinsics heavily
3. **Cross-reference known structures** - If you find one camera function, xrefs will lead to others
4. **Compare function prologues** - AnvilNext functions often have distinctive stack setup patterns

## Step 4: Verify SDK Structures

Copy Odyssey's `sdk/AnvilEngine.h` and verify struct offsets match Origins:
- `Camera` struct - check offset for worldMatrixOverride (was +576 in Odyssey)
- `CameraNode` struct - quaternion at offset +0x70
- Any renderer-related structures

## Step 5: Copy and Adapt Engine Modules

1. Copy all files from `games/Odyssey/engine/` to `games/Origins/engine/`
2. Update `#include` paths in each file
3. Replace Odyssey-specific patterns in `offsets.h` with Origins patterns
4. Test each hook individually

## Common Issues

- **Pattern not found**: Try shortening the pattern or using wildcards (`?`)
- **Crash on hook**: Verify function signatures match exactly
- **Wrong behavior**: Check struct member offsets - they may have shifted

## Build Configuration

Add Origins target to CMakeLists.txt following the Odyssey pattern. The build system should automatically pick up the new game folder.
