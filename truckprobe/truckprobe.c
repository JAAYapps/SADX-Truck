// truckprobe: does Cabela's 4x4 Off-Road Adventure 3 truck physics run
// outside the game?  Loads EngineDll.dll from the current folder, sets up
// the physics world the way Engine.exe does (0x414018), builds one
// cPhysVehicle, adds it to a cPhysManager and steps the manager.  Prints one line per step, so the last
// line printed is the last step that worked.
//
// No C runtime: kernel32 for everything, msvcrt only for sprintf.
// Addresses printed as "ghidra 0x..." match REA/Ghidra (DLL base 0x10000000).

typedef unsigned int u32;
typedef void *HANDLE;

typedef struct { float x, y, z; } tVect3;
typedef struct { float m[16]; } m4x4;

typedef struct {
    u32 code, flags;
    void *rec, *addr;
    u32 nparams;
    u32 info[15];
} EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *rec; void *ctx; } EXCEPTION_POINTERS;

__declspec(dllimport) HANDLE __stdcall GetStdHandle(u32 which);
__declspec(dllimport) int __stdcall WriteFile(HANDLE h, const void *buf, u32 n, u32 *done, void *ov);
__declspec(dllimport) void *__stdcall LoadLibraryExA(const char *name, void *file, u32 flags);
__declspec(dllimport) void *__stdcall GetProcAddress(void *mod, const char *name);
__declspec(dllimport) u32 __stdcall GetFullPathNameA(const char *name, u32 n, char *buf, char **part);
__declspec(dllimport) u32 __stdcall GetLastError(void);
__declspec(dllimport) void *__stdcall VirtualAlloc(void *addr, u32 size, u32 type, u32 prot);
__declspec(dllimport) int __stdcall VirtualProtect(void *addr, u32 size, u32 prot, u32 *old);
__declspec(dllimport) void *__stdcall AddVectoredExceptionHandler(u32 first, long (__stdcall *fn)(EXCEPTION_POINTERS *));
__declspec(dllimport) void __stdcall ExitProcess(u32 code);
__declspec(dllimport) int __cdecl sprintf(char *buf, const char *fmt, ...);

int _fltused = 1;

// The game's data folder (holds Cora3.arc), as Wine sees it.  Only read.
// Traction mode the probe uses (the game reads it from the garage save):
// 1 = rear wheels driven, 2 and 3 = all four (GameDll FUN_100271e0,
// EngineDll SetTractionType).  Proposal: 1.
#define TRACTION_MODE 1

// Cabela's own install folder; for another place build with
// -DCORA3_DATA="\"D:\\\\Games\\\\Cora3\\\\Cora3\\\\\"" (ends with a backslash).
#ifndef CORA3_DATA
#define CORA3_DATA "C:\\Program Files (x86)\\Activision Value\\FUNLabs\\Cora3\\Cora3\\"
#endif

// Which truck: 0 = Car1 (Predator), 1 = Car12 (Stalker, the fresh-save truck).
#define CAR_STALKER 1

// Everything that differs per truck.  Model numbers come from
// OBJECTS\CARS\CARn\CARn_0.BONEDUMP and CARn_0.MODEL in Cora3.arc; the rest
// from the truck's block in EnhPack\Scripts\cars.lua.
typedef struct {
    const char *name;
    tVect3 spring[4];          // posArc_0..3
    tVect3 tire[4];            // wheel_0..3
    float spring_to_tire;      // posArc_0.y - wheel_0.y
    float dummy[3][6];         // colDummy_0..2: scale x y z, position x y z
    float axle[2];             // truck byte 260: weighCenter to wheel_0 / wheel_2
    float pivot[2];            // truck byte 256: posArc_0.y / posArc_2.y - weighCenter.y
    float mass, adherence, steering;
    int axle_state[2];         // SetPhysDrivingTrain 1st and 2nd value (0 = independent)
    float axle_params[2];      // SetAxleParams for an independent front
    float gears[6];            // SetPhysGearRatiosAutomatic, reverse first
    float init_len, min_len, max_len, ksusp, kdump, rolling;
} car_data;

static const car_data cars[2] = {
    {   "Car1 (Predator)",
        { { -0.832351f, -0.298530f,  1.448670f }, {  0.832264f, -0.298530f,  1.448670f },
          { -0.832351f, -0.298531f, -1.453193f }, {  0.832264f, -0.298531f, -1.453193f } },
        { { -0.832351f, -1.040751f,  1.448670f }, {  0.832264f, -1.040751f,  1.448670f },
          { -0.832351f, -1.040751f, -1.453193f }, {  0.832264f, -1.040751f, -1.453193f } },
        0.742221f,
        { { 1.934787f, 0.865330f, 4.349574f,   -0.001151f, -0.483604f, -0.141837f },
          { 1.242713f, 0.555866f, 2.913910f,   -0.001151f,  0.264836f, -0.681215f },
          { 1.061837f, 0.764360f, 4.638214f,   -0.001151f, -0.478286f, -0.141837f } },
        { 0.894519f, 0.835196f },
        { 0.421385f, 0.742223f },
        8.64f, 0.34f, 45.0f,
        { 0, 1 },
        { 0.95f, -9.0f },
        { -2.72f, 2.84f, 1.55f, 1.0f, 0.7f, 0.56f },
        1.08f, 0.5f, 0.80f, 40.0f, 20.0f, 1.0f },
    {   "Car12 (Stalker)",
        { { -0.825917f, -0.198364f,  1.578075f }, {  0.825917f, -0.198364f,  1.578075f },
          { -0.825917f, -0.198364f, -1.571012f }, {  0.825917f, -0.198364f, -1.571012f } },
        { { -0.825917f, -1.054080f,  1.578075f }, {  0.825917f, -1.054080f,  1.578075f },
          { -0.825917f, -1.054081f, -1.571012f }, {  0.825917f, -1.054081f, -1.571012f } },
        0.855716f,
        { { 1.896323f, 0.815282f, 4.419996f,   -0.001151f, -0.489169f, -0.089936f },
          { 1.279263f, 0.807284f, 3.022841f,   -0.001151f,  0.402172f, -0.822203f },
          { 0.818269f, 0.887496f, 4.515710f,   -0.001151f, -0.489169f, -0.106361f } },
        { 0.931534f, 0.821027f },
        { 0.424891f, 0.855717f },
        8.71f, 0.35f, 30.0f,
        { 1, 1 },
        { 0.0f, 0.0f },                                  // not used: both axles solid
        { -2.72f, 3.96f, 2.08f, 1.39f, 1.0f, 0.86f },
        0.9f, 0.7f, 1.0f, 105.56f, 151.96f, 1.0f },
};
static const car_data *car = &cars[CAR_STALKER];

static char line[512];
static u32 dll_base;
static int step;
static int tick;

static void out(void)
{
    u32 n = 0, done;
    while (line[n]) n++;
    WriteFile(GetStdHandle((u32)-11), line, n, &done, 0);
}

static void say(const char *what)
{
    sprintf(line, "step %2d  %s\r\n", step, what);
    out();
}

// C++ exceptions (0xE06D7363) and debug prints are normal inside the DLL,
// so only real crashes stop the probe.
static long __stdcall on_crash(EXCEPTION_POINTERS *p)
{
    u32 code = p->rec->code;
    u32 at = (u32)p->rec->addr;
    if (code == 0xE06D7363u || code == 0x40010006u || code == 0x4001000Au || code == 0x406D1388u)
        return 0;
    sprintf(line, "CRASH    in step %d, tick %d: code %u (0x%08X) at ghidra 0x%08X", step, tick, code, code,
            at - dll_base + 0x10000000u);
    out();
    if (code == 0xC0000005u && p->rec->nparams >= 2) {
        sprintf(line, ", %s address %u (0x%08X)", p->rec->info[0] ? "writing" : "reading",
                p->rec->info[1], p->rec->info[1]);
        out();
    }
    // x86 CONTEXT: Esp at byte 196.  For a small function that crashed
    // before pushing anything, the top of the stack is the caller.
    u32 esp = *(u32 *)((char *)p->ctx + 196);
    u32 ret = *(u32 *)esp;
    sprintf(line, "\r\n         called from ghidra 0x%08X (top of stack)\r\n", ret - dll_base + 0x10000000u);
    out();
    // Walk the saved-EBP chain (x86 CONTEXT: Ebp at byte 180) for callers
    // further up.  Only addresses inside EngineDll are printed.
    u32 ebp = *(u32 *)((char *)p->ctx + 180);
    for (int n = 0; n < 12 && ebp > 65536 && ebp < 0x80000000u; n++) {
        u32 r = ((u32 *)ebp)[1];
        if (r >= dll_base && r < dll_base + 0x400000u) {
            sprintf(line, "         caller %2d  ghidra 0x%08X\r\n", n, r - dll_base + 0x10000000u);
            out();
        }
        ebp = ((u32 *)ebp)[0];
    }
    ExitProcess(2);
    return 0;
}

static void *need(const char *mangled, const char *shortname)
{
    void *f = GetProcAddress((void *)dll_base, mangled);
    sprintf(line, "         %-26s %s\r\n", shortname, f ? "found" : "MISSING");
    out();
    if (!f) ExitProcess(1);
    return f;
}

// In-memory patch: turn the 6-byte conditional jump at `at` into an
// always-taken jump to `target` (both ghidra addresses).  The DLL file on
// disk is not changed.
static void always_jump(u32 at, u32 target)
{
    unsigned char *p = (unsigned char *)(at - 0x10000000u + dll_base);
    u32 old;
    VirtualProtect(p, 6, 0x40, &old);
    if (p[0] != 0x0F || p[1] != 0x84) {
        sprintf(line, "         patch at 0x%08X: unexpected bytes, not patched\r\n", at);
        out();
        ExitProcess(1);
    }
    u32 rel = target - (at + 5);
    p[0] = 0xE9;
    p[1] = rel & 255; p[2] = (rel >> 8) & 255; p[3] = (rel >> 16) & 255; p[4] = rel >> 24;
    p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    sprintf(line, "         patched 0x%08X: always jump to 0x%08X\r\n", at, target);
    out();
}

// In-memory patch: replace the 5-byte call at `at` (ghidra address) with
// do-nothing bytes.
static void skip_call(u32 at)
{
    unsigned char *p = (unsigned char *)(at - 0x10000000u + dll_base);
    u32 old;
    VirtualProtect(p, 5, 0x40, &old);
    if (p[0] != 0xE8) {
        sprintf(line, "         patch at 0x%08X: unexpected bytes, not patched\r\n", at);
        out();
        ExitProcess(1);
    }
    for (int i = 0; i < 5; i++) p[i] = 0x90;
    VirtualProtect(p, 5, old, &old);
    sprintf(line, "         patched 0x%08X: call removed\r\n", at);
    out();
}

// Ground experiment (build with -DGROUND=n):
//   0 = the real map (cBspTree::HitCache)
//   1 = the mod's wheel ray (same code as sadxtruck's sadx_hit) on a flat
//       floor; SetHitCache and the body box test switched off, like the mod
//   2 = the mod's wheel ray on the real terrain height; box test kept
#ifndef GROUND
#define GROUND 0
#endif
#ifndef STRAIGHT
#define STRAIGHT 0
#endif
static float floor_y;
static void *terrain;
static float (__fastcall *ter_height)(void *, float, float);
static float probe_ground(float x, float z)
{
    return GROUND == 1 ? floor_y : ter_height(terrain, x, z);
}
static float sqrt_f(float v) { __asm__("fsqrt" : "+t"(v)); return v; }
static int __fastcall probe_hit(void *bsp, void *edx, const tVect3 *s, const tVect3 *d, float len, char *info)
{
    (void)bsp; (void)edx;
    if (d->y > -0.01f) return 0;
    float g = probe_ground(s->x, s->z);
    float t = (g - s->y) / d->y;
    if (t < 0.0f) { t = 0.0f; g = s->y; }
    if (t > len) return 0;
    float gx = probe_ground(s->x + 0.2f, s->z), gz = probe_ground(s->x, s->z + 0.2f);
    float nx = -(gx - g) / 0.2f, nz = -(gz - g) / 0.2f;
    float k = 1.0f / sqrt_f(nx * nx + 1.0f + nz * nz);
    float *f = (float *)info;
    *(int *)info = 7;
    *(int *)(info + 20) = -1;
    f[2] = s->x + d->x * t; f[3] = g; f[4] = s->z + d->z * t;
    f[10] = nx * k; f[11] = k; f[12] = nz * k;
    return 1;
}
static void patch5(u32 at, unsigned char first, const unsigned char *bytes, const char *what)
{
    unsigned char *p = (unsigned char *)(at - 0x10000000u + dll_base);
    u32 old;
    if (p[0] != first) {
        sprintf(line, "         %s at 0x%08X: unexpected first byte, not patched\r\n", what, at);
        out();
        ExitProcess(1);
    }
    VirtualProtect(p, 5, 0x40, &old);
    for (int i = 0; i < 5; i++) p[i] = bytes[i];
    VirtualProtect(p, 5, old, &old);
    sprintf(line, "         patched %s at 0x%08X\r\n", what, at);
    out();
}

void __cdecl mainCRTStartup(void)
{
    char path[400];
    AddVectoredExceptionHandler(1, on_crash);

    step = 1;
    GetFullPathNameA("EngineDll.dll", sizeof path, path, 0);
    say(path);
    // 8 = LOAD_WITH_ALTERED_SEARCH_PATH, so msvcp60.dll is found next to it
    dll_base = (u32)LoadLibraryExA(path, 0, 8);
    if (!dll_base) {
        sprintf(line, "step  1  LoadLibrary failed, Windows error %u\r\n", GetLastError());
        out();
        ExitProcess(1);
    }
    sprintf(line, "step  1  loaded at %u (0x%08X)\r\n", dll_base, dll_base);
    out();

    step = 2;
    say("looking up functions");
    void (__thiscall *ctor)(void *) = need("??0cPhysVehicle@@QAE@XZ", "constructor");
    void (__thiscall *SetNumWheels)(void *, u32) = need("?SetNumWheels@cPhysVehicle@@UAEXI@Z", "SetNumWheels");
    void (__thiscall *Create)(void *) = need("?Create@cPhysVehicle@@UAEXXZ", "Create");
    void (__thiscall *SetPosition)(void *, const tVect3 *) = need("?SetPosition@cPhysVehicle@@UAEXABUtVect3@@@Z", "SetPosition");
    int (__thiscall *EngineIsStarted)(void *) = need("?EngineIsStarted@cPhysVehicle@@QBE?BHXZ", "EngineIsStarted");
    float (__thiscall *GetRPM)(void *) = need("?GetRPM@cPhysVehicle@@QBEMXZ", "GetRPM");
    int (__thiscall *GetCurGear)(void *) = need("?GetCurGear@cPhysVehicle@@QBEHXZ", "GetCurGear");
    void (__thiscall *PressAcceleration)(void *, float) = need("?PressAcceleration@cPhysVehicle@@UAEXM@Z", "PressAcceleration");
    m4x4 *(__thiscall *GetWheelTM)(void *, m4x4 *, u32) = need("?GetWheelTM@cPhysVehicle@@QAE?AUm4x4@@I@Z", "GetWheelTM");
    void (__thiscall *SetSteerAngle)(void *, float) = need("?SetSteerAngle@cPhysVehicle@@QAEXM@Z", "SetSteerAngle");
    void (__thiscall *PressBrake)(void *, float) = need("?PressBrake@cPhysVehicle@@UAEXM@Z", "PressBrake");
    int (__cdecl *PhysWorldInit)(int) = need("?PhysWorldInit@@YAHH@Z", "PhysWorldInit");
    int (__cdecl *PhysSetWorldParam)(const char *, const char *) = need("?PhysSetWorldParam@@YAHPAD0@Z", "PhysSetWorldParam");
    void (__thiscall *ManagerCtor)(void *, int, int) = need("??0cPhysManager@@QAE@HH@Z", "manager constructor");
    void (__thiscall *AddObject)(void *, void *, int) = need("?AddObject@cPhysManager@@UAEXPAVcPhysicsObject@@H@Z", "manager AddObject");
    int (__thiscall *ManagerUpdate)(void *, float) = need("?Update@cPhysManager@@UAEHM@Z", "manager Update");
    void **g_bsp = need("?g_bsp@@3PAVcBspTree@@A", "g_bsp (world collision)");

    // Map: same sequence as Engine.exe at 0x40c59e to 0x40c6ac.
    void (__thiscall *BspCtor)(void *) = need("??0cBspTree@@QAE@XZ", "map constructor");
    int (__thiscall *BspLoad)(void *, const char *, int, int) = need("?Load@cBspTree@@QAEHPADHH@Z", "map Load");
    void (__thiscall *BspScale)(void *, float) = need("?Scale@cBspTree@@QAEXM@Z", "map Scale");
    int (__thiscall *RayHit)(void *, const tVect3 *, const tVect3 *, float, void *, int) =
        need("?RayHit@cBspTree@@QAEHABUtVect3@@0MAAUtRayHitInfo@@H@Z", "map RayHit");

    // Same calls and numbers as Engine.exe at 0x414018 to 0x414093.
    step = 3;
    sprintf(line, "step  3  PhysWorldInit(400) returned %d\r\n", PhysWorldInit(400));
    out();
    sprintf(line, "step  3  PhysSetWorldParam(gravity, 9.81f) returned %d\r\n",
            PhysSetWorldParam("gravity", "9.81f"));
    out();
    // Engine.exe allocates 416 bytes (push 0x1a0) and passes 400, 400.
    void *manager = VirtualAlloc(0, 4096, 0x3000, 4);
    say("manager constructor(400, 400)");
    ManagerCtor(manager, 400, 400);

    // Engine.exe allocates 755212 bytes (push 0xb860c); 1 MB leaves room.
    // Last Load argument: Engine.exe passes 1 (read from Cora3.arc through
    // fileManager); 0 reads a plain file instead (cDiskFile, 0x1022a3d0).
    // The map loader builds graphics buffers through the global `render`.
    // EngineDll ships a renderer that draws nothing (cRenderNULL); use it.
    // Its size isn't known yet, so give it 64 KB of zeroed memory.
    step = 4;
    void (__thiscall *NullRenderCtor)(void *) = need("??0cRenderNULL@@QAE@XZ", "null renderer constructor");
    void **render = need("?render@@3PAVcRender@@A", "render (global)");
    void *nullrender = VirtualAlloc(0, 1 << 16, 0x3000, 4);
    say("null renderer constructor, put in render");
    NullRenderCtor(nullrender);
    *render = nullrender;
    // Creates the render interface, shader, texture, light and model
    // managers (constructors only, 0x102616a0).  The map loader uses them.
    void (__cdecl *InitRenderInterface)(void) = need("?InitRenderInterface@@YAXXZ", "InitRenderInterface");
    say("InitRenderInterface");
    InitRenderInterface();

    // Map models (rocks, trees, buildings) come from Cora3.arc through the
    // global fileManager.  Same calls as Engine.exe: InitPakFileManager(".zip", 2)
    // at 0x41cc6c, then AddHomeDirectory(<install>\Cora3\, 0) at 0x41c594.
    void (__cdecl *InitPakFileManager)(const char *, int) = need("?InitPakFileManager@@YAXPADH@Z", "InitPakFileManager");
    void (__thiscall *AddHomeDirectory)(void *, const char *, int) = need("?AddHomeDirectory@cFileManager@@QAEXPADH@Z", "AddHomeDirectory");
    void **fileManager = need("?fileManager@@3PAVcFileManager@@A", "fileManager (global)");
    // Shaders are Lua scripts; Engine.exe starts Lua first (0x41cc60).
    void (__cdecl *InitScript)(void) = need("?InitScript@@YAXXZ", "InitScript");
    say("InitScript");
    InitScript();
    say("InitPakFileManager(.zip, 2)");
    InitPakFileManager(".zip", 2);
    say("AddHomeDirectory(" CORA3_DATA ", 0)");
    AddHomeDirectory(*fileManager, CORA3_DATA, 0);

    void *bsp = VirtualAlloc(0, 1 << 20, 0x3000, 4);
    say("map constructor");
    BspCtor(bsp);
    *g_bsp = bsp;
    // Grass is drawing-only and needs the shader manager, which we don't
    // have: skip it (cBspTree::Load, grass check at 0x1022aa61).
    always_jump(0x1022aa61, 0x1022b2c8);
    // ShaderReport is a debug printout that crashes on the shaders Lua
    // couldn't load (call at the end of cBspTree::Load).
    skip_call(0x1022b367);
    say("map Load(Bsp\\BJ_A.bsp, 767, 0)");
    int loaded = BspLoad(bsp, "Bsp\\BJ_A.bsp", 767, 0);
    sprintf(line, "step  4  map Load returned %d (0 means failed)\r\n", loaded);
    out();
    if (!loaded) ExitProcess(1);
    // Engine.exe scales by 0.01 (0x3c23d70a) right after loading.
    say("map Scale(0.01)");
    BspScale(bsp, 0.01f);

    // Find the ground.  RayHit(start, direction, length, info, flags)
    // (0x10202090): the ray ends at start + length * direction.  Returns
    // above 0 on a hit; hit point at info bytes 8, 12, 16; surface normal at
    // bytes 40, 44, 48.  Flags 0x1078bd is what CommonDll passes (0x1000feca).
    // Straight down from 1000 m up, over a 41 x 41 grid 100 m apart, Y up
    // (the earlier free fall showed Y is up).
    // Find the ground from the terrain itself.  The terrain object is at
    // map byte 1652 (0x674, read in cBspTree::RayHit).  Its first values are
    // the low corner (-512, -24.03, -512) and 1025 points per side, so it
    // covers about -512 to +512 meters in X and Z (seen in a test run).
    // cBspTree::RayHit returned -1 before asking the terrain, so ask the
    // terrain directly: GetHeight(x, z), and RayHit(start, end, info) with
    // the hit point at info bytes 8, 12, 16 (0x10202090 copies it from there).
    step = 5;
    float (__thiscall *TerMin)(void *) = need("?GetMinHeight@cTerrain@@QAEMXZ", "terrain GetMinHeight");
    float (__thiscall *TerMax)(void *) = need("?GetMaxHeight@cTerrain@@QAEMXZ", "terrain GetMaxHeight");
    // QAI = member function using __fastcall: this in ECX, floats on the stack.
    float (__fastcall *TerHeight)(void *, float, float) = need("?GetHeight@cTerrain@@QAIMMM@Z", "terrain GetHeight");
    int (__thiscall *TerRayHit)(void *, const tVect3 *, const tVect3 *, void *) =
        need("?RayHit@cTerrain@@QAEHABUtVect3@@0AAUtRayHitInfo@@@Z", "terrain RayHit");
    void *ter = *(void **)((char *)bsp + 0x674);
    if (!ter) {
        say("map has no terrain");
        ExitProcess(1);
    }
    sprintf(line, "step  5  terrain height from %.3f to %.3f\r\n", (double)TerMin(ter), (double)TerMax(ter));
    out();
    static char hitinfo[1024];
    static const float spots[5][2] = { { 0, 0 }, { 100, 0 }, { 0, 100 }, { -200, 150 }, { 300, -300 } };
    for (int k = 0; k < 5; k++) {
        float x = spots[k][0], z = spots[k][1];
        tVect3 top = { x, 1000.0f, z }, bottom = { x, -1000.0f, z };
        for (int n = 0; n < 1024; n++) hitinfo[n] = 0;
        int r = TerRayHit(ter, &top, &bottom, hitinfo);
        float *f = (float *)hitinfo;
        sprintf(line, "step  5  at x %.0f z %.0f: GetHeight %.3f, RayHit returned %d, hit point %.3f %.3f %.3f\r\n",
                (double)x, (double)z, (double)TerHeight(ter, x, z), r, (double)f[2], (double)f[3], (double)f[4]);
        out();
    }
    float best_a = 0, best_b = 0, hi = TerHeight(ter, 0.0f, 0.0f);
    floor_y = hi;
    terrain = ter;
    ter_height = TerHeight;
    if (GROUND != 0) {
        u32 rel = (u32)probe_hit - (dll_base + 0x201160u + 5);
        unsigned char jmp[5] = { 0xE9, rel & 255, (rel >> 8) & 255, (rel >> 16) & 255, rel >> 24 };
        patch5(0x10201160u, 129, jmp, "HitCache -> probe_hit");
    }
    if (GROUND == 1) {
        static const unsigned char ret8[5] = { 0x33, 0xC0, 0xC2, 8, 0 }, ret0[5] = { 0x33, 0xC0, 0xC3, 0x90, 0x90 };
        patch5(0x10201090u, 83, ret8, "SetHitCache -> nothing");
        patch5(0x102294a0u, 85, ret0, "box test -> 0 contacts");
    }
    sprintf(line, "step  5  GROUND mode %d, STRAIGHT %d, floor %.3f\r\n", GROUND, STRAIGHT, (double)floor_y);
    out();

    // The game allocates 380 bytes (push 0x17c before operator new).
    // 4096 zeroed bytes leaves room in case that number is wrong.
    step = 6;
    void *truck = VirtualAlloc(0, 4096, 0x3000, 4);
    say("truck constructor");
    ctor(truck);
    say("SetNumWheels(4)");
    SetNumWheels(truck, 4);
    // Truck byte 268 bit (1 << 3): set when the game car's flag (1 << 6)
    // is set, which picks the automatic gear ratios (FUN_10025b90 and
    // FUN_10024b50).  Automatic here.
    *((unsigned char *)truck + 0x10c) |= (1 << 3);
    say("Create");
    Create(truck);

    // Truck shape and settings, copied from the game's own truck setup
    // (GameDll.dll FUN_10025b90).  Positions are the truck's model points
    // from its BONEDUMP in Cora3.arc (meters), see cars[] at the top.
    step = 6;
    say(car->name);
    say("truck setup, same order as GameDll FUN_10025b90");
    {
        void (__thiscall *TestAllColisions)(void *) = need("?TestAllColisions@cPhysicsObject@@UAEXXZ", "TestAllColisions");
        void (__thiscall *SolveAllColisions)(void *) = need("?SolveAllColisions@cPhysicsObject@@UAEXXZ", "SolveAllColisions");
        void (__thiscall *SetLinearSpeed)(void *, float, float, float) = need("?SetLinearSpeed@cPhysicsObject@@UAEXMMM@Z", "SetLinearSpeed");
        void (__thiscall *SetAngularSpeed)(void *, float, float, float) = need("?SetAngularSpeed@cPhysicsObject@@UAEXMMM@Z", "SetAngularSpeed");
        void (__thiscall *SetBodyFrictionCoef)(void *, float) = need("?SetBodyFrictionCoef@cPhysicsObject@@UAEXM@Z", "SetBodyFrictionCoef");
        void (__thiscall *SetPosSpring)(void *, u32, tVect3 *) = need("?SetPosSpring@cPhysVehicle@@QAEXIAAUtVect3@@@Z", "SetPosSpring");
        void (__thiscall *SetPosTire)(void *, u32, tVect3 *) = need("?SetPosTire@cPhysVehicle@@QAEXIAAUtVect3@@@Z", "SetPosTire");
        void (__thiscall *SetSuspConst)(void *, float, float) = need("?SetSuspConst@cPhysVehicle@@QAEXMM@Z", "SetSuspConst");
        void (__thiscall *SetSuspInitLength)(void *, float, float, float) = need("?SetSuspInitLength@cPhysVehicle@@QAEXMMM@Z", "SetSuspInitLength");
        void (__thiscall *SetCollBoxes)(void *, void *, int) = need("?SetCollBoxes@cPhysVehicle@@QAEXPAU_tBox@@H@Z", "SetCollBoxes");

        char *t = (char *)truck;
        char *body = *(char **)(t + 4);
        u32 nw = *(u32 *)(t + 0xe8);
        for (u32 i = 0; i < nw; i++)                     // each wheel: byte 244 = 1.0
            *(float *)(((char **)*(void **)(t + 0xcc))[i] + 0xf4) = 1.0f;
        TestAllColisions(truck);
        SolveAllColisions(truck);
        *(float *)(body + 0x168) = -0.009f;              // 0xbc1374bc
        SetLinearSpeed(truck, 0, 0, 0);
        SetAngularSpeed(truck, 0, 0, 0);
        SetBodyFrictionCoef(truck, 0.1f);
        *(u32 *)(body + 0xbc) |= (1 << 4) | (1 << 5);
        *(float *)(body + 0x154) = 0.3f;
        *(float *)(body + 0x178) = 1.0f;
        *(float *)(body + 0x180) = 1.0f;
        *(float *)(body + 0x174) = 0.98f;                // 0x3f7ae148
        *(float *)(body + 0x17c) = 0.989f;               // 0x3f7d2f1b

        // posArc_N (spring top) and wheel_N (tire).
        for (u32 i = 0; i < 4; i++) {
            tVect3 s = car->spring[i], w = car->tire[i];
            SetPosSpring(truck, i, &s);
            SetPosTire(truck, i, &w);
        }
        SetSuspConst(truck, 125.0f, 15.0f);
        // Wheel 0 spring-to-tire distance.
        SetSuspInitLength(truck, car->spring_to_tire, 0.5f, 0.75f);

        // Collision boxes from colDummy_0..2.  A box is 280 bytes: center
        // (3 floats), half size (3 floats), then the X, Y and Z axes; the
        // game fills only those (0x1002640c onward), SetCollBoxes the rest.
        // Half size = the bone's scale / 2.
        const float (*dummy)[6] = car->dummy;
        static float boxes[3][70];
        for (int b = 0; b < 3; b++) {
            float *x = boxes[b];
            x[0] = dummy[b][3]; x[1] = dummy[b][4]; x[2] = dummy[b][5];
            x[3] = dummy[b][0] * 0.5f; x[4] = dummy[b][1] * 0.5f; x[5] = dummy[b][2] * 0.5f;
            x[6] = 1; x[7] = 0; x[8] = 0;
            x[9] = 0; x[10] = 1; x[11] = 0;
            x[12] = 0; x[13] = 0; x[14] = 1;
        }
        SetCollBoxes(truck, boxes, 3);

        // Per-axle values, measured from the model nodes weighCenter_front
        // and weighCenter_back (CARn_0.MODEL, read with model_bones.py):
        //   Predator front  0.002623 -0.719915  1.455583
        //   Predator back   0.002845 -1.040754 -1.453193
        //   Stalker  front  0.000000 -0.623255  1.580712
        //   Stalker  back  -0.004890 -1.054081 -1.571012
        // Truck byte 260 (0x104): distance from weighCenter to wheel_0 /
        // wheel_2 (0x10026010 to 0x100260e5).
        // Truck byte 256 (0x100): posArc_0.y / posArc_2.y minus weighCenter.y
        // (0x10026258 to 0x100262ac).
        float *axle = *(float **)(t + 0x104);
        axle[0] = car->axle[0];
        axle[1] = car->axle[1];
        float *pivot = *(float **)(t + 0x100);
        pivot[0] = car->pivot[0];
        pivot[1] = car->pivot[1];
        *(u32 *)(t + 0x84) = 80;                         // 0x100261a7
        *(float *)(t + 0x110) = 0.2f;                    // 0x3e4ccccd, replaced below
        *(u32 *)(t + 0x174) |= (1 << 0) | (1 << 1) | (1 << 2) | (1 << 4);
        say("truck setup done");
    }

    // cars.lua values for the chosen truck, applied the way the game
    // does when it puts the truck in the world (GameDll FUN_10024b50,
    // called right after the shape setup above).  The game passes each
    // value as cars.lua value * tuning multiplier * garage slider; for a
    // stock truck both are 1.0 (SetDataForCar sets the multipliers to 1.0,
    // the scripts set SLIDER_VALUE to "1.0").
    step = 6;
    say("cars.lua values, same order as GameDll FUN_10024b50");
    {
        void (__thiscall *SetMass)(void *, float) = need("?SetMass@cPhysicsObject@@UAEXM@Z", "SetMass");
        void (__thiscall *SetMaxGearboxRpm)(void *, float) = need("?SetMaxGearboxRpm@cPhysVehicle@@QAEXM@Z", "SetMaxGearboxRpm");
        void (__thiscall *SetMinGearboxRpm)(void *, float) = need("?SetMinGearboxRpm@cPhysVehicle@@QAEXM@Z", "SetMinGearboxRpm");
        void (__thiscall *SetGearboxRedLine)(void *, float) = need("?SetGearboxRedLine@cPhysVehicle@@QAEXM@Z", "SetGearboxRedLine");
        void (__thiscall *SetTorqueScale)(void *, float) = need("?SetTorqueScale@cPhysVehicle@@QAEXM@Z", "SetTorqueScale");
        void (__thiscall *SetAxleState)(void *, int, int) = need("?SetAxleState@cPhysVehicle@@QAEXHH@Z", "SetAxleState");
        void (__thiscall *SetAxleParams)(void *, int, float, float) = need("?SetAxleParams@cPhysVehicle@@QAEXHMM@Z", "SetAxleParams");
        void (__thiscall *SetGearRatios)(void *, float *) = need("?SetGearRatios@cPhysVehicle@@QAEXPAM@Z", "SetGearRatios");
        void (__thiscall *SetSuspInitLength)(void *, float, float, float) = need("?SetSuspInitLength@cPhysVehicle@@QAEXMMM@Z", "SetSuspInitLength");
        void (__thiscall *SetSuspConst)(void *, float, float) = need("?SetSuspConst@cPhysVehicle@@QAEXMM@Z", "SetSuspConst");
        void (__thiscall *SetAcceleratorSpd)(void *, float) = need("?SetAcceleratorSpd@cPhysVehicle@@QAEXM@Z", "SetAcceleratorSpd");
        void (__thiscall *SetTractionType)(void *, u32) = need("?SetTractionType@cPhysVehicle@@QAEXI@Z", "SetTractionType");

        char *t = (char *)truck;
        u32 nw = *(u32 *)(t + 0xe8);
        // (The game first restores body byte 272 from its own copy when body
        // flag (1 << 2) is set, and clears it again at the end.  The probe
        // never sets that flag, so both steps are left out.)
        SetMass(truck, car->mass);                       // SetMass
        for (u32 i = 0; i < nw; i++)                     // SetTireAdherence
            *(float *)(((char **)*(void **)(t + 0xcc))[i] + 0xf4) = car->adherence;
        SetMaxGearboxRpm(truck, 7000.0f);                // SetMaxRpm
        SetMinGearboxRpm(truck, 1000.0f);                // SetMinRpm
        SetGearboxRedLine(truck, 5800.0f);               // SetRedRpm
        *(float *)(t + 0xf4) = car->steering;            // SetSteering
        SetTorqueScale(truck, 1.0f);                     // EngPower multiplier (no nitro)
        // SetPhysDrivingTrain: 1st and 2nd value = front and rear axle,
        // 0 = independent, 1 = solid.  Predator (0, 1, 0.95, 0, -9.0, 0.0,
        // 0.24, 0), Stalker (1, 1).  Only an independent side gets
        // SetAxleParams(side, 3rd or 4th value, 5th or 6th value).
        SetAxleState(truck, car->axle_state[0], car->axle_state[1]);
        if (car->axle_state[0] == 0)
            SetAxleParams(truck, 0, car->axle_params[0], car->axle_params[1]);
        // SetPhysGearRatiosAutomatic, reverse first.  The game copies them
        // into a 10-float buffer.
        static float gears[10];
        for (int g = 0; g < 6; g++)
            gears[g] = car->gears[g];
        // SetGearRatios copies as many as the gearbox (truck byte 300) says
        // it has: its function table slot 16 + slot 20 (low byte each).
        {
            void **gb = *(void ***)(t + 300);
            u32 (__thiscall *slot16)(void *) = ((void **)gb[0])[4];
            u32 (__thiscall *slot20)(void *) = ((void **)gb[0])[5];
            sprintf(line, "step  6  gearbox counts: slot 16 = %u, slot 20 = %u (6 ratios in cars.lua)\r\n",
                    slot16(gb) & 255, slot20(gb) & 255);
            out();
        }
        SetGearRatios(truck, gears);
        SetSuspInitLength(truck, car->init_len, car->min_len, car->max_len);
        SetSuspConst(truck, car->ksusp, car->kdump);     // SetKSusp, SetKDump
        *(float *)(t + 0x110) = car->rolling * 0.5f;     // SetPhysRollingCoefs * 0.5
        *(float *)(t + 0xf8) = 1.0f;                     // steer speed, 1.0 with keyboard
        SetAcceleratorSpd(truck, 1.0f);                  // 1.0 with keyboard

        // Traction comes from the garage save (byte 1888 of the player's
        // truck entry), applied by GameDll FUN_100271e0 right after.
        // 1 -> SetTractionType(18), 2 -> 36, 3 -> 40; then PressAcceleration(0).
        static const u32 traction_type[4] = { 0, 18, 36, 40 };
        SetTractionType(truck, traction_type[TRACTION_MODE]);
        PressAcceleration(truck, 0.0f);
        say("cars.lua values done");
    }

    // 3 meters (about 10 feet) above the ground.
    step = 7;
    tVect3 start = { best_a, hi + 3.0f, best_b };
    sprintf(line, "step  7  SetPosition(%.3f, %.3f, %.3f)\r\n",
            (double)start.x, (double)start.y, (double)start.z);
    out();
    SetPosition(truck, &start);

    // Same call the save loader uses (0x100da8b9): AddObject(truck, 1).
    step = 8;
    say("manager AddObject(truck, 1)");
    AddObject(manager, truck, 1);
    sprintf(line, "step  8  engine started = %d, RPM = %.1f, gear = %d\r\n",
            EngineIsStarted(truck), (double)GetRPM(truck), GetCurGear(truck));
    out();

    // Same script as sadxtruck's testhost: 1 second still, 5 seconds full
    // throttle, 2 seconds throttle + full right lock, 2 seconds brake.
    // 60 ticks of 1/60 second = one second; the manager runs fixed 0.01
    // second steps inside each call.
    step = 9;
    say("manager Update(1/60) x 600: still, gas, gas + full lock, brake");
    float last_x = 0, last_z = 0;
    for (tick = 1; tick <= 600; tick++) {
        m4x4 tm;
        // Held key: the game presses the pedal again every frame.
        if (tick >= 61 && tick <= 480) PressAcceleration(truck, 1.0f);
        if (tick > 480) PressBrake(truck, 1.0f);
        SetSteerAngle(truck, !STRAIGHT && tick > 360 && tick <= 480 ? car->steering : 0.0f);
        ManagerUpdate(manager, 1.0f / 60.0f);
        GetWheelTM(truck, &tm, 0);
        if (tick % 15 == 0 && tick > 300) {
            // Lean: left side (wheels 0, 2) minus right side (wheels 1, 3)
            // height, as an angle; speed from wheel 0 over the last 15 ticks.
            m4x4 w1;
            GetWheelTM(truck, &w1, 1);
            float dy = tm.m[13] - w1.m[13], dx = tm.m[12] - w1.m[12], dz = tm.m[14] - w1.m[14];
            float across = dx * dx + dz * dz;
            float mph = 0;
            if (last_x || last_z) {
                float mx = tm.m[12] - last_x, mz = tm.m[14] - last_z;
                float d2 = mx * mx + mz * mz, r = d2;
                for (int k = 0; k < 20 && r > 0; k++) r = 0.5f * (r + d2 / r);   // square root
                mph = r * 4.0f * 2.23694f;
            }
            last_x = tm.m[12]; last_z = tm.m[14];
            (void)across;
            // Wheels are 1.652 m apart; small-angle lean in degrees.
            sprintf(line, "    tick %3d  %s  %5.1f mph  left side %+.3f m higher than right (about %+.0f degrees)  gear %d\r\n",
                    tick, tick <= 360 ? "gas      " : tick <= 480 ? "gas+lock " : "brake    ",
                    (double)mph, (double)dy, (double)(dy / 1.652f * 57.3f), GetCurGear(truck));
            out();
        }
        if (tick % 30 == 0 || tick <= 5) {
            m4x4 tm3;
            GetWheelTM(truck, &tm3, 3);
            float g0 = TerHeight(ter, tm.m[12], tm.m[14]), g3 = TerHeight(ter, tm3.m[12], tm3.m[14]);
            sprintf(line, "    tick %3d  RPM %6.1f  gear %d  wheel 0 %7.3f %7.3f %7.3f (%.3f above ground)  wheel 3 %7.3f %7.3f %7.3f (%.3f above ground)\r\n",
                    tick, (double)GetRPM(truck), GetCurGear(truck),
                    (double)tm.m[12], (double)tm.m[13], (double)tm.m[14], (double)(tm.m[13] - g0),
                    (double)tm3.m[12], (double)tm3.m[13], (double)tm3.m[14], (double)(tm3.m[13] - g3));
            out();
        }
    }

    step = 10;
    say("done, no crash");
    ExitProcess(0);
}
