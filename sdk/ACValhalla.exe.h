#include <stdint.h>
#include <stddef.h>
#include <windows.h>

typedef uint8_t _BYTE;

namespace sdk {
  // Basic vector type for camera calculations
  struct Vector4 {
    float x, y, z, w;
  };
  
  // Reconstructed from Engine_InitializeTaskGraph and Task_* helpers.
  // Observations:
  //  - Name pointer written by Task_SetName / Task_SetNameIfUnnamed at offset +0x1A8 (relative to object start)
  //  - Priority int written at +0x1B0
  //  - Many tasks store a triple (context, compareFn, bodyFn/vtableInvoker) beginning near +0x1C? for BaseTask vs +0x1B8 newData start for Task
  //  - BaseTask appears to be a lighter variant (size 0x1B8) lacking the extended newData block present in Task (0x1E8)
  //  - Virtual SetNextTask used to link dependency edges.
  class BaseTask {
  public:
    virtual __int64 sub_1406E5F30(char a2) {}
    virtual void    sub_14046A040() {}
    virtual __int64 re_BaseTask_SetNextTask(BaseTask* next) {} // set next task
    virtual __int64 sub_1406E6D50(char a2) {}
    virtual bool    sub_140480060() {}
    virtual __int64 sub_1406E9710(unsigned int a2) {}
    virtual char    sub_1406F6780(__int64 a2, __int64 a3) {}
    virtual char    sub_1406EB130(__int64 a2) {}
    virtual __int64 sub_1406C5EA0() {}
    virtual char    sub_14047C940() {}
    virtual __int64 sub_1406E9CE0() {}
    virtual char    sub_14047C940_11() {}
    virtual char    sub_14047C940_12() {}
    virtual __int64 sub_1406EE7F0() {}
    virtual bool    sub_140480060_14() {}
    virtual __int64 sub_1406E8350() {}
    virtual void    sub_1406E8540() {}
    virtual __int64 sub_1406E92E0() {}
    // 0x0008 .. 0x01A7: scheduling / linkage / internal state (unresolved) 
    // Hypothesis: includes adjacency lists, flags (byte at +0x38? +0x56 used by Task_GetPriorityByte accessor returning *(uint8_t*)(this+56)).
    uint8_t  unk_0008[0x38];        // 0x0008 placeholder
    uint8_t  schedulingClass;       // 0x0040? (Need verification) kept as array below until mapped precisely
    uint8_t  unk_0041[0x15];        // room to +0x56
    uint8_t  priorityClassByte;     // 0x0056 read by Task_GetPriorityByte
    uint8_t  unk_0057[0x151];       // up to 0x1A8
    const char* name;               // 0x01A8 set by Task_SetName / Task_SetNameIfUnnamed
    int      priority;              // 0x01B0 set by Task_SetPriority
    uint8_t  unk_01B4[0x4];         // alignment/padding (end of BaseTask: 0x1B8)
  };
  static_assert(sizeof(BaseTask) == 0x1B8);

  class Task : public BaseTask {
  public:
    // Extended data region used for callback triplets:
    // In Engine_InitializeTaskGraph we see patterns like:
    //   *(_OWORD *)&task.newData[8]  = { ctxPtr, bodySetupFn }
    //   *(_OWORD *)&task.newData[24] = { compareFn / 0, ??? }
    //   *(_QWORD *)&task.newData[40] = defaultInvokeFn (often sub_1406A7900)
    // Layout below is provisional until each slot named.
    uint8_t  pad[8];
    struct TaskCallbacks {
      void* pContext;  // Context pointer passed to callbacks
      void* execute;           
      void* func2;       
      void* func3; // nullptr in observed cases
      void* func4; // often sub_1406A7900
    } callbacks;
  };
  static_assert(sizeof(Task) == 0x1E8);

  // Reconstructed from g_engine_frame global (0x1462bde10) and related functions
  // Size: 88 bytes (0x58) - No vtable found (non-virtual class)
  // Evidence from sub_141111860 (constructor) and various accessor functions
  // CORRECTED back to original evidence-based offsets.
  // Use 1-byte packing to preserve exact (even if misaligned) offsets observed in the binary.
  #pragma pack(push, 1)
  struct GlobalContext {
    char pad_00[8];           // 0x00-0x07: Unknown initial data
    
    unsigned char byte_08;    // 0x08: Evidence: *(_BYTE *)(v5 + 8) = 0;
    unsigned char byte_09;    // 0x09: Evidence: *(_BYTE *)(v5 + 9) = 0;
    char pad_0A[2];           // 0x0A-0x0B: Padding
    
    // 0x0C..0x13 and 0x10 overlap in evidence; model with union aliasing
    char pad_0C[4];           // 0x0C-0x0F
    unsigned int dword_10;    // 0x10: Evidence: *(_DWORD *)(g_engine_frame + 16)
    char pad_14[4];           // 0x14-0x17: Padding
    
    void* ptr_18;             // 0x18: Evidence: *(_QWORD *)(v5 + 24) = 0i64;
    void* ptr_20;             // 0x20: Evidence: *(_QWORD *)(v5 + 32) = 0i64;
    void* ptr_28;             // 0x28: Evidence: *(_QWORD *)(v5 + 40) = 0i64;
    void* ptr_30;             // 0x30: Evidence: *(_QWORD *)(v5 + 48) = 0i64;
    
    unsigned char state_38;   // 0x38: Evidence: accessed at g_engine_frame + 56
    unsigned char state_39;   // 0x39: Evidence: accessed at g_engine_frame + 57
    unsigned int dword_3A;    // 0x3A: Evidence: *(_DWORD *)(v5 + 58) = 1;
    char pad_3E[2];           // 0x3E-0x3F: Padding

    HANDLE event_handle_40;   // 0x40: Evidence: sub_14046B770(v5 + 64)
    HANDLE event_handle_48;   // 0x48: Evidence: sub_14046B770(v5 + 72)
    
    unsigned char state_flag_50; // 0x50: Evidence: accessed at g_engine_frame + 80
    unsigned short word_51;   // 0x51: Evidence: *(_WORD *)(v5 + 81) = 0;
    char pad_53[5];           // 0x53-0x57: Padding to 88 bytes
  };
  #pragma pack(pop)
  static_assert(sizeof(GlobalContext) == 0x58, "GlobalContext size mismatch");
   static_assert(offsetof(GlobalContext, byte_08) == 0x08, "byte_08 offset");
   static_assert(offsetof(GlobalContext, byte_09) == 0x09, "byte_09 offset");
   static_assert(offsetof(GlobalContext, dword_10) == 0x10, "dword_10 offset");
   static_assert(offsetof(GlobalContext, ptr_18) == 0x18, "ptr_18 offset");
   static_assert(offsetof(GlobalContext, ptr_20) == 0x20, "ptr_20 offset");
   static_assert(offsetof(GlobalContext, ptr_28) == 0x28, "ptr_28 offset");
   static_assert(offsetof(GlobalContext, ptr_30) == 0x30, "ptr_30 offset");
   static_assert(offsetof(GlobalContext, state_38) == 0x38, "state_38 offset");
   static_assert(offsetof(GlobalContext, state_39) == 0x39, "state_39 offset");
   static_assert(offsetof(GlobalContext, dword_3A) == 0x3A, "dword_3A offset");
   static_assert(offsetof(GlobalContext, pad_3E) == 0x3E, "pad_3E offset");
   static_assert(offsetof(GlobalContext, event_handle_40) == 0x40, "event_handle_40 offset");
   static_assert(offsetof(GlobalContext, event_handle_48) == 0x48, "event_handle_48 offset");
   static_assert(offsetof(GlobalContext, state_flag_50) == 0x50, "state_flag_50 offset");
   static_assert(offsetof(GlobalContext, word_51) == 0x51, "word_51 offset");

  class Engine {
  public:
      bool is_initialized;
      char pad_0[7];
      Task ApplyGates;
      Task ZenStateAllocatorEndFrameCleanup;
      Task BeginFrame;
      Task PropertyBufferCleanup;
      Task BeginGraphicFrame;
      Task ExecuteSingleThreadedGraphicCommands;
      BaseTask ExecuteMultiThreadedGraphicCommands;
      char padD30[16];
      Task PreparePostMultithreadCommands;
      BaseTask ExecutePostMultiThreadedGraphicCommands;
      char pad10E0[16];
      Task GraphicFrame;
      Task EndGraphicFrame;
      Task BeginEngineFrame;
      Task OnEngineStartMT;
      char pad_0x1890[512];
      Task LoadingAdvisorUpdate;
      Task UpdatePilotFacadeNavMeshRequests;
      Task CheckEngineStop;
      char pad_0x2048[96];
      Task CheckEngineStopPreStep;
      char pad22[96];
      BaseTask UniverseLinkStart;
      char pad22F0[8];
      Task GatherUniverseComponentTasks;
      Task UpdateSingleThreadedUniverseComponents;
      char pad_0x2880[8];
      Task UpdateMultiThreadedUniverseComponents;
      char pad_0x2A70[8];
      Task PreStep;
      Task Step;
      Task DominoStep;
      Task ScriptsUpdate;
      Task QuestUpdate;
      Task QuestSystemPostUpdate;
      Task UpdateInput;
      Task UpdateVideoPlayerManagerInput;
      Task UpdateTheater;
      Task UpdateDialogueRunner;
      Task PreUpdateCamera;
      Task UpdateCamera;
      Task UpdateTargets;
      Task UpdateDialogueLighting;
      Task UpdateWhileEngineIsPaused;
      Task UpdateOnline;
      char pad48F8[136];
      BaseTask AfterAIAndSkel;
      char pad4B38[8];
      BaseTask WorldLinkStart;
      char pad4CF8[8];
      Task StepDispatchEvents;
      Task SoundManagerUpdate;
      Task SoundManagerUpdateSoundEmitters;
      char pad52B8[24];
      Task SoundManagerUpdateOcclusion;
      Task SoundManagerComputeOcclusion;
      char pad56A0[8];
      Task GatherManagersForPostWorldComponentUpdate;
      Task UpdateManagersPostWorldComponentUpdate;
      char pad_0x5A78[24];
      Task AccomplishmentManagerUpdate;
      Task EndEngineFrame;
      Task EndFrame;
      char pad6048[0x60A8u-0x6048];
  };
};

static_assert(sizeof(sdk::Engine) == 0x60A8u, "Engine size mismatch"); // real size

// -----------------------------------------------------------------------------
// Evidence-based reconstruction for global at qword_145FB9D90
// Observations (from sub_14062EA60 and xrefs):
//  - Allocated with size 0xA8
//  - dwords at +0, +4, +8 (counts/config)
//  - An array of 10 floats from +0x0C..+0x30 (first 7 populated, rest zeroed)
//  - Three Vector4 blocks at +0x34, +0x44, +0x54 copied from 1.0f / [ +0x88 ]
//  - dwords at +0x64, +0x68, +0x6C
//  - qwords at +0x70, +0x78, +0x80
//  - float at +0x88 (read by callers)
//  - qword at +0x90
//  - floats at +0x98, +0x9C (read/written as floats by callers)
//  - dword at +0xA0, byte at +0xA4
// No vtable writes observed; treat as POD struct.
namespace sdk {
  #pragma pack(push, 1)
  struct Unk_145FB9D90 {
    // 0x00
    uint32_t dword_0;     // set to 0
    uint32_t dword_4;     // set to 6
    uint32_t dword_8;     // set to 7
    // 0x0C .. 0x30
    float    floats_0C_30[10]; // 7 populated with 1.0f / v95[..], remaining zeroed
    // 0x34 .. 0x63
    Vector4  vec_34;      // = 1.0f / *(float*)(this+0x88)
    Vector4  vec_44;      // same as above
    Vector4  vec_54;      // same as above
    // 0x64 .. 0x6C
    uint32_t dword_64;    // copied from vec_34.x bits
    uint32_t dword_68;    // copied from vec_34.x bits
    uint32_t dword_6C;    // copied from vec_34.x bits
    // 0x70 .. 0x87
    void*    ptr_70;      // read by callers, participates in differences with ptr_78
    void*    ptr_78;      // read by callers
    void*    ptr_80;      // read by callers
    // 0x88 .. 0x8B
    float    float_88;    // read by sub_14062BCF0 (movss [rax+88h])
    int      pad_8C;    // padding to align next field
    // 0x90 .. 0x97
    void*    ptr_90;      // set to 0 at init
    // 0x98 .. 0x9F
    float    float_98;    // read/written by callers (movss [rax+98h])
    float    float_9C;    // set to same as float_88 at init
    // 0xA0 .. 0xA7
    uint32_t dword_A0;    // set to 4
    uint8_t  byte_A4;     // set to 0
    uint8_t  pad_A5[3];   // padding to 0xA8
  };
  #pragma pack(pop)

  static_assert(sizeof(Unk_145FB9D90) == 0xA8, "Unk_145FB9D90 size mismatch");
   static_assert(offsetof(Unk_145FB9D90, ptr_70)   == 0x70, "ptr_70 offset");
   static_assert(offsetof(Unk_145FB9D90, ptr_78)   == 0x78, "ptr_78 offset");
   static_assert(offsetof(Unk_145FB9D90, ptr_80)   == 0x80, "ptr_80 offset");
   static_assert(offsetof(Unk_145FB9D90, float_88) == 0x88, "float_88 offset");
   static_assert(offsetof(Unk_145FB9D90, ptr_90)   == 0x90, "ptr_90 offset");
   static_assert(offsetof(Unk_145FB9D90, float_98) == 0x98, "float_98 offset");
   static_assert(offsetof(Unk_145FB9D90, float_9C) == 0x9C, "float_9C offset");
   static_assert(offsetof(Unk_145FB9D90, dword_A0) == 0xA0, "dword_A0 offset");
   static_assert(offsetof(Unk_145FB9D90, byte_A4)  == 0xA4, "byte_A4 offset");
}

namespace sdk {
#pragma pack(push, 1)

  // Defines a rectangular region, likely the main game viewport.
  struct Viewport
  {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
  };

  // Contains a set of 23 graphics quality settings.
  struct QualitySettings
  {
    uint32_t setting_0;
    uint32_t setting_1;
    uint32_t setting_2;
    uint32_t setting_3;
    uint32_t setting_4;
    uint32_t setting_5;
    uint32_t setting_6;
    uint32_t setting_7;
    uint32_t setting_8;
    uint32_t setting_9;
    uint32_t setting_10;
    uint32_t setting_11;
    uint32_t setting_12;
    uint32_t setting_13;
    uint32_t setting_14;
    uint32_t setting_15;
    uint32_t setting_16;
    uint32_t setting_17;
    uint32_t setting_18;
    uint32_t setting_19;
    uint32_t setting_20;
    uint32_t setting_21;
    uint32_t setting_22;
  };

  // Main structure for holding global graphics-related constants and settings.
  struct GlobalGfxConstants
  {
    Viewport viewport;             // (0x0) The main viewport coordinates.
    uint32_t dword_10;                  // (0x10) Unknown.
    uint64_t qword_14;                  // (0x14) Unknown.
    uint32_t resolutionSource;          // (0x1C) A mode selector for the resolution source (0=display, 1=render target, 2=external, 3=special case).
    uint32_t renderTargetId;            // (0x20) An identifier for the current render target.
    QualitySettings quality;       // (0x24) A block of 23 graphics quality settings.
    uint64_t qword_80;                  // (0x80) Unknown.
    uint64_t qword_88;                  // (0x88) Unknown.
    uint64_t qword_90;                  // (0x90) Unknown.
    uint64_t qword_98;                  // (0x98) Unknown.
    uint64_t qword_A0;                  // (0xA0) Unknown.
    uint64_t qword_A8;                  // (0xA8) Unknown.
    uint64_t qword_B0;                  // (0xB0) Unknown.
    uint64_t qword_B8;                  // (0xB8) Unknown.
    uint64_t qword_C0;                  // (0xC0) Unknown.
    uint64_t qword_C8;                  // (0xC8) Unknown.
    uint64_t qword_D0;                  // (0xD0) Unknown.
    uint32_t dword_D8;                  // (0xD8) Unknown.
    uint64_t qword_DC;                  // (0xDC) Unknown.
    uint32_t graphicsSetting_E4;        // (0xE4) A graphics setting, related to brightness.
    char pad_E8[4];
    uint32_t dword_EC;                  // (0xEC) Unknown.
    float renderTargetWidth;            // (0xF0) The width of the render target.
    float renderTargetHeight;           // (0xF4) The height of the render target.
    float displayWidth;                 // (0xF8) The width of the display or window.
    float displayHeight;                // (0xFC) The height of the display or window.
    uint32_t dword_100;                 // (0x100) Unknown.
    uint32_t dword_104;                 // (0x104) Unknown.
    uint32_t float_108;                 // (0x108) Unknown.
    float brightness;                   // (0x10C) The screen brightness level.
    uint32_t dword_110;                 // (0x110) Unknown.
    uint32_t float_114;                 // (0x114) Unknown.
    uint32_t float_118;                 // (0x118) Unknown.
    uint32_t dword_11C;                 // (0x11C) Unknown.
    uint32_t float_120;                 // (0x120) Unknown.
    uint64_t double_124;                // (0x124) Unknown.
    uint32_t float_12C;                 // (0x12C) Unknown.
    uint32_t float_130;                 // (0x130) Unknown.
    uint32_t dword_134;                 // (0x134) Unknown.
    uint32_t dword_138;                 // (0x138) Unknown.
    uint32_t dword_13C;                 // (0x13C) Unknown.
    uint64_t graphicsFlags;             // (0x140) A bitmask of graphics-related flags.
    bool isWidescreen;                  // (0x148) A boolean flag indicating if the display is widescreen.
    char pad_149[3];
    uint32_t dword_14C;                 // (0x14C) Unknown.
    uint32_t dword_150;                 // (0x150) Unknown.
    uint32_t dword_154;                 // (0x154) Unknown.
    char pad_158[260];
    uint32_t dword_25C;                 // (0x25C) Unknown.
    char pad_260[1600];
  };
#pragma pack(pop)
  static_assert(sizeof(GlobalGfxConstants) == 0x8A0, "GlobalGfxConstants size mismatch");
   static_assert(offsetof(GlobalGfxConstants, float_108) == 0x108, "float_108 offset");
   static_assert(offsetof(GlobalGfxConstants, float_114) == 0x114, "float_114 offset");
   static_assert(offsetof(GlobalGfxConstants, float_118) == 0x118, "float_118 offset");
   static_assert(offsetof(GlobalGfxConstants, float_120) == 0x120, "float_120 offset");
   static_assert(offsetof(GlobalGfxConstants, float_12C) == 0x12C, "float_12C offset");
   static_assert(offsetof(GlobalGfxConstants, float_130) == 0x130, "float_130 offset");
   static_assert(offsetof(GlobalGfxConstants, dword_154) == 0x154, "dword_154 offset");

}