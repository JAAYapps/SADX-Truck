// sadxtruck: SADX mod that runs Cabela's 4x4 Off-Road Adventure 3 truck
// physics inside sonic.exe.
//   Step 0: load EngineDll.dll from the mod folder.
//   Step 1: build the Stalker the way truckprobe does and step it once per
//           SADX frame.
//   Step 2: ground from SADX (GetShadowPos) instead of the Cabela's map.
//   Step 3: Y puts the truck under Sonic and him in it; triggers and stick
//           drive it.  The truck itself is not drawn yet.
//
// Output goes through SADX's PrintDebug, which the mod loader sends to its
// debug console and to mods/.modloader/SADXModLoader.log.
//
// No C runtime: kernel32 only.
// Addresses printed as "ghidra 0x..." match REA/Ghidra (DLL base 0x10000000);
// EngineDll is usually moved somewhere else inside sonic.exe.

typedef unsigned int u32;

typedef struct { float x, y, z; } tVect3;
typedef struct { float m[16]; } m4x4;

typedef struct {
    u32 code, flags;
    void *rec, *addr;
    u32 nparams;
    u32 info[15];
} EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *rec; void *ctx; } EXCEPTION_POINTERS;

__declspec(dllimport) void *__stdcall LoadLibraryExA(const char *name, void *file, u32 flags);
__declspec(dllimport) void *__stdcall GetProcAddress(void *mod, const char *name);
__declspec(dllimport) u32 __stdcall GetFullPathNameA(const char *name, u32 n, char *buf, char **part);
__declspec(dllimport) u32 __stdcall GetLastError(void);
__declspec(dllimport) void *__stdcall VirtualAlloc(void *addr, u32 size, u32 type, u32 prot);
__declspec(dllimport) int __stdcall VirtualProtect(void *addr, u32 size, u32 prot, u32 *old);
__declspec(dllimport) void *__stdcall AddVectoredExceptionHandler(u32 first, long (__stdcall *fn)(EXCEPTION_POINTERS *));

int _fltused = 1;

// SADX's own functions and data.  testhost.c defines its own stand-ins to
// run this file outside the game.
//   PrintDebug    SADXFunctions.h      0x401000   debug print
//   GetShadowPos  SADXFunctionsNew.h   0x49E920   ground Y below a point
//   player_task   SADXVariablesNew.h   0x3B42E10  playertwp[0], Sonic's taskwk
//                                                 (0 outside a level)
#ifndef TEST_EXE
#define PrintDebug ((void (__cdecl *)(const char *, ...))0x401000)
#define GetShadowPos ((float (__cdecl *)(float, float, float, int *))0x49E920)
#define player_task (*(char **)0x3B42E10)
//   game_mode     SADXVariablesNew.h   0x3B22DE4  ssGameMode, 15 = playing
//                                                 (16 paused; enum MD_GAME)
#define game_mode (*(short *)0x3B22DE4)
#endif
// Controls and Sonic's speed.
//   pad_raw       SADXVariablesNew.h   0x3B0E77C  per[0], Player 1's pad.  Not
//                 cleared by PadReadOff (sonic.exe 0x40FE0B only stops the
//                 copy into perG), so the truck still reads it.
//   PadReadOn     SADXFunctionsNew.h   0x40EF40   Sonic listens to the pad
//   PadReadOff    SADXFunctionsNew.h   0x40EF50   Sonic ignores the pad
//   player_motion SADXVariablesNew.h   0x3B36DD0  playermwp[0], spd at byte 4
//   player_work   SADXVariablesNew.h   0x3B3CDF0  playerpwp[0], spd at byte 56
// Pad layout (SADXStructs.h ControllerData): HeldButtons at byte 8, right and
// left trigger at 24 and 26 (0 to 255), stick X at 28.
#ifndef TEST_EXE
#define pad_raw (*(char **)0x3B0E77C)
#define PadReadOn ((void (__cdecl *)(void))0x40EF40)
#define PadReadOff ((void (__cdecl *)(void))0x40EF50)
// perG[0]  SADXVariablesNew.h  0x3B0E7F0  the pad copy the game reads
//          (PDS_PERIPHERAL: on 8, off 12, press 16, release 20, r and l
//          triggers 24 and 26, sticks 28 to 35).  Skipped while PadReadOff.
#define pad_game ((char *)0x3B0E7F0)
#define player_motion (*(char **)0x3B36DD0)
#define player_work (*(char **)0x3B3CDF0)
// Player 2 (Tails following Sonic): the next entry of the same arrays
// (SADXVariablesNew.h playertwp, playermwp, playerpwp: 8 pointers each).
#define player2_task (*(char **)0x3B42E14)
#define player2_motion (*(char **)0x3B36DD4)
#define player2_work (*(char **)0x3B3CDF4)
#endif
// Sitting: Twinkle Park's cart seat.  SetInputP(0, 18) (PL_OP_PLACEWITHCART,
// SADXEnumsNew.h) puts Sonic in MD_SONIC_S3A2_CART, mode 45, sitting
// animation 65 (sonic.exe 0x4961A8).  Mode 45 does nothing each frame but
// wait for input 28, PL_OP_GETOUTOFCART, which makes him hop out (mode 9,
// 0x499289), so seat_sonic can keep placing him.
//   SetInputP     SADXFunctionsNew.h   0x441260
#define PL_OP_PLACEWITHCART 18
#define PL_OP_GETOUTOFCART 28
#ifndef TEST_EXE
#define SetInputP ((void (__cdecl *)(unsigned char, signed char))0x441260)
#endif
// Drawing the wheel markers (all from the SDK headers in sadx/programming).
//   CreateElementalTask  SADXFunctionsNew.h  0x40B860  new task; byte 20 of
//                        the task is its display function, byte 24 its
//                        "being freed" function (struct task)
//   njSetTexture         SADXFunctions.h     0x403070
//   OBJ_REGULAR_TEXLIST  SADXVariables.h     0x8B1CD8  textures for rings
//   object_obj_ring_ring SADXModelsNew.h     0x8B4834  the ring model
//   dsDrawObject         SADXFunctionsNew.h  0x408530
//   njPushMatrixEx       SADXFunctions.h     0x781C80
//   njPopMatrixEx        SADXFunctions.h     0x7816E0
//   njTranslate          SADXFunctions.h     0x784BE0  thiscall, matrix 0 = current
//   njRotateY            SADXFunctions.h     0x784E20  fastcall, matrix 0 = current
#ifndef TEST_EXE
#define CreateElementalTask ((char *(__cdecl *)(unsigned short, int, void (__cdecl *)(char *)))0x40B860)
#define njSetTexture ((int (__cdecl *)(void *))0x403070)
#define OBJ_REGULAR_TEXLIST ((void *)0x8B1CD8)
#define ring_model ((void *)0x8B4834)
#define dsDrawObject ((void (__cdecl *)(void *))0x408530)
#define njPushMatrixEx ((void (__cdecl *)(void))0x781C80)
#define njPopMatrixEx ((void (__cdecl *)(void))0x7816E0)
#define njTranslate ((void (__thiscall *)(void *, float, float, float))0x784BE0)
#define njRotateY ((void (__fastcall *)(void *, int))0x784E20)
#endif
// SADXEnums.h ButtonBits
#define BUTTON_B (1 << 1)
#define BUTTON_A (1 << 2)
#define BUTTON_Y (1 << 9)
#define BUTTON_START (1 << 3)
#define BUTTON_LEFT (1 << 6)
#define BUTTON_RIGHT (1 << 7)
#define BUTTON_R (1 << 16)
#define BUTTON_L (1 << 17)

// SADXModInfo.h: ModLoaderVer is 16.  Init is left 0 so the loader calls
// the exported Init function instead.
typedef struct {
    int version;
    void *init;
    const void *patches;  int patch_count;
    const void *jumps;    int jump_count;
    const void *calls;    int call_count;
    const void *pointers; int pointer_count;
} ModInfo;

__declspec(dllexport) ModInfo SADXModInfo = { 16 };

// Traction (GameDll cObjCarCora::SetTraction 0x100271E0): 1 = rear
// wheels driven (2x4), 2 = all four (4x4), 3 = all four, stronger and
// slower (4x4 Low; test host, flat road, full gas: top speed 63 mph
// against 71).  The start mode; D-pad down while driving cycles them.
#ifndef TRACTION_MODE
#define TRACTION_MODE 1
#endif
static int traction_mode = TRACTION_MODE;
static const u32 traction_type[4] = { 0, 18, 36, 40 };   // SetTractionType values
static const char *const traction_name[4] = { "", "2x4", "4x4", "4x4 Low" };
static const char *const traction_hud[4] = { "", "2X4", "4X4", "4X4 LO" };   // the gauges' "T:"
static float odo_miles[14];              // each truck's odometer (saved in the damage files)
// Fuel, Cabela's cObjCarCora::UpdateCarFuel (0x100204A0): every 2
// seconds the tank loses 2 s x RPM x tank / (minutes x 60 x 3000), RPM
// held to 600..7000, "minutes" = CARS.LUA SetTechData's last three
// numbers, one per traction mode (stored by GameDll 0x1005B2F0 at +0x4C,
// +0x50, +0x54).  So a full tank lasts "minutes" at 3000 RPM.  Empty:
// the engine stops and won't start; a repair fills it.  0 to 1, full at
// start, then from the truck's damage file (SaveDamage on).
static const unsigned char fuel_minutes[14][3] = {
    { 40, 35, 25 }, { 40, 25, 25 }, { 40, 35, 25 }, { 40, 35, 25 }, { 40, 35, 25 }, { 35, 25, 20 }, { 35, 25, 20 },
    { 25, 25, 20 }, { 35, 25, 20 }, { 35, 25, 25 }, { 25, 20, 15 }, { 40, 35, 25 }, { 40, 35, 25 }, { 40, 35, 25 } };
static float fuel[14] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
// Mod setting Driving / UseFuel (on by default).  Off: no fuel burns, the
// tank never runs dry and the gauges leave out the fuel bar; the saved
// fuel is kept as it is for when it's turned back on.
static int use_fuel = 1;
#define OUT_OF_FUEL (use_fuel && fuel[truck_now] <= 0)

// All 14 Cabela's trucks: models, physics numbers and texture numbers,
// made by skin_to_c.py from Cora3.arc and EnhPack\Scripts\cars.lua (see
// trucks_model.h).  `picked` is the truck the D-pad shows, `truck_now` the
// one built at the last spawn; `car` is the built truck's numbers.
#include "trucks_model.h"
static int picked = 11, truck_now = 11;  // 11 = Car12, the Stalker

// trucks.bin in the mod folder (layout: skin_to_c.py write_bin): read whole
// into one block, then the tables above point into it.  The points stay
// writable (dents move them).  0 = missing or not this mod's layout; the
// log says which.
static int trucks_bin_load(const char *path, u32 n)
{
    char name[400];
    const char *f = "\\trucks.bin";
    u32 k = 0;
    for (; k < n && k < 300; k++) name[k] = path[k];
    while (*f) name[k++] = *f++;
    name[k] = 0;
    void *kl = LoadLibraryExA("kernel32.dll", 0, 0);
    void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *) = kl ? (void *)GetProcAddress(kl, "CreateFileA") : 0;
    int (__stdcall *rf)(void *, void *, u32, u32 *, void *) = kl ? (void *)GetProcAddress(kl, "ReadFile") : 0;
    u32 (__stdcall *fs)(void *, u32 *) = kl ? (void *)GetProcAddress(kl, "GetFileSize") : 0;
    int (__stdcall *cl)(void *) = kl ? (void *)GetProcAddress(kl, "CloseHandle") : 0;
    if (!cf || !rf || !fs || !cl) return 0;
    void *h = cf(name, 0x80000000u, 1, 0, 3, 0, 0);       // GENERIC_READ, share read, OPEN_EXISTING
    if (h == (void *)-1) {
        PrintDebug("[SADX Truck] %s not found: run setup.exe in the mod folder first. Truck off.\n", name);
        return 0;
    }
    u32 size = fs(h, 0), got = 0;
    unsigned char *b = size > 32 && size < (64u << 20) ? VirtualAlloc(0, size, 0x3000, 4) : 0;
    int ok = b && rf(h, b, size, &got, 0) && got == size;
    cl(h);
    const u32 *hd = (const u32 *)b;
    if (!ok || hd[0] != 0x42545853u /* "SXTB" */ || hd[1] != TRUCK_BIN_VERSION || hd[2] != TRUCK_MODELS ||
        hd[3] != TRUCK_DRAWS || hd[4] != TRUCKS || hd[5] != TRUCK_WAVS || hd[6] != TRUCK_REC || hd[7] != SOUND_REC ||
        sizeof(struct truck_data) != TRUCK_REC || sizeof(struct truck_sound) != SOUND_REC) {
        PrintDebug("[SADX Truck] %s is not the shape this sadxtruck.dll reads (%u bytes): run setup.exe again. Truck off.\n",
                   name, size);
        return 0;
    }
    // The fixed part, then each model's arrays; every step checked against the size.
    u32 at = 32 + TRUCK_MODELS * 32;
    const unsigned char *mt = b + 32;
    truck_draws = (const void *)(b + at);
    at += TRUCK_DRAWS * 8;
    trucks = (const void *)(b + at);
    at += TRUCKS * TRUCK_REC;
    truck_sounds = (const void *)(b + at);
    at += TRUCKS * SOUND_REC;
    for (int i = 0; i < TRUCK_WAVS; i++) {
        truck_wavs[i] = (const char *)(b + at + i * 28);
        truck_wav_seconds[i] = *(const float *)(b + at + i * 28 + 24);
    }
    at += TRUCK_WAVS * 28;
    font_glyph = (const void *)(b + at);
    at += 95 * 5 + 1;
    for (int i = 0; i < TRUCK_MODELS; i++) {
        struct truck_model *m = &truck_models[i];
        const int *mi = (const int *)(mt + i * 32);
        const float *mf = (const float *)(mt + i * 32 + 16);
        m->npoints = mi[0]; m->ntris = mi[1]; m->tex = mi[2]; m->lamp = mi[3];
        m->center[0] = mf[0]; m->center[1] = mf[1]; m->center[2] = mf[2]; m->radius = mf[3];
        u32 np = (u32)m->npoints, nt = (u32)m->ntris;
        u32 need = np * 24 + nt * 6 + (nt & 1) * 2 + nt * 12;
        if (np > 65535 || nt > 65535 || at + need > size) {
            PrintDebug("[SADX Truck] %s ends early (model %d): run setup.exe again. Truck off.\n", name, i);
            return 0;
        }
        m->points = (void *)(b + at);
        m->normals = (void *)(b + at + np * 12);
        at += np * 24;
        m->tris = (void *)(b + at);
        at += nt * 6 + (nt & 1) * 2;
        m->uv = (void *)(b + at);
        at += nt * 12;
    }
    if (at != size) {
        PrintDebug("[SADX Truck] %s has %u bytes, expected %u: run setup.exe again. Truck off.\n", name, size, at);
        return 0;
    }
    PrintDebug("[SADX Truck] trucks.bin loaded: %d trucks, %d models, %d sounds (%u bytes)\n",
               TRUCKS, TRUCK_MODELS, TRUCK_WAVS, size);
    return 1;
}
#define car (trucks[truck_now])

static u32 dll_base;
static void *manager, *truck;
static int frame;
static int riding;                       // 1 = Sonic is in the truck
static int map_menu;                     // 1 = the map menu is open (Station Square, on foot)
static void map_menu_picture(void);
// Snorkel fitted on each truck: 0 none, 1 Watermaster (Snork1), 2
// Watermaster Ultra (Snork2).  Kept between rides; D-pad down on foot.
static int snorkel[TRUCKS];
static void draw_hud(void);              // the damage HUD (Engine section)
static void draw_gauges(void);           // tach, speedo, bars, traction
static int piece_shown(int frame, int model, int part, int lamp);  // lamps and loose pieces (Engine section)
static void falling_draw(void);
static void falling_start(const m4x4 tms[5]);
static void dent_restore(void);              // dents out (Damage section)
static void dent_enemy(const float pw[3], const char *why);   // an enemy hit's dent
static void dent_save(void);                 // a truck's dents to its slot
static void dent_load(void);                 // and back
static int dent_truck, keep_dents;
static void damage_file_save(int t);        // K2: a truck's damage to its file
static int save_damage = 1;                 // mod setting Damage / SaveDamage
static int land_strength = 1;               // mod setting Damage / LandingStrength, 1 to 50
static int truck_volume = 100;              // mod setting Sound / TruckVolume, 0 to 100
static void damage_files_init(const char *path, unsigned n);
static void free_cam_patch(void);
static void people_patch(void);             // Past echidnas run from the truck
static void radio_pick(int pick);           // radio: D-pad left / right while driving
static int radio_text(const char **name, const char **title);
static void sound_settings(const char *path, unsigned n);
static void cart_pad_patch(void);
static void missile_patch(void);
static float dent_strength;

static int (__thiscall *ManagerUpdate)(void *, float);
static m4x4 *(__thiscall *GetWheelTM)(void *, m4x4 *, u32);
static float (__thiscall *GetRPM)(void *);
static void (__thiscall *SetPosition)(void *, const tVect3 *);
static void (__thiscall *SetOrientation)(void *, const float *);   // 3x3, 9 floats
static void (__thiscall *SetLinearSpeed)(void *, float, float, float);
static void (__thiscall *SetAngularSpeed)(void *, float, float, float);
static int (__thiscall *GetCurGear)(void *);
static void (__thiscall *PressAcceleration)(void *, float);
static void (__thiscall *PressBrake)(void *, float);
static void (__thiscall *SetSteerAngle)(void *, float);
static void (__thiscall *SetTorqueScale)(void *, float);
static void (__thiscall *SetTractionType)(void *, u32);
static void (__thiscall *GearUp)(void *);
// EngineDll takes objects at rest off its active list (cPhysicsObject::
// TestStop, RemoveObjectFromActiveList); the manager's Update then skips
// them: no movement, RPM frozen.  GameDll wakes the truck; here OnFrame
// does it while Sonic rides.
static int (__thiscall *IsInActiveList)(void *);
static void (__thiscall *ActivateObject)(void *);
static void (__thiscall *GearDown)(void *);

// Print crashes that happen inside EngineDll, with ghidra addresses.
// Everything else (and C++ exceptions, 0xE06D7363) is passed on untouched.
static long __stdcall on_crash(EXCEPTION_POINTERS *p)
{
    u32 code = p->rec->code;
    u32 at = (u32)p->rec->addr;
    if (code == 0xE06D7363u || (code & 0xF0000000u) != 0xC0000000u)
        return 0;
    if (at < dll_base || at >= dll_base + 0x400000u)
        return 0;
    PrintDebug("[SADX Truck] CRASH in EngineDll, frame %d: code %u (0x%08X) at ghidra 0x%08X\n",
               frame, code, code, at - dll_base + 0x10000000u);
    if (code == 0xC0000005u && p->rec->nparams >= 2)
        PrintDebug("[SADX Truck]   %s address %u (0x%08X)\n", p->rec->info[0] ? "writing" : "reading",
                   p->rec->info[1], p->rec->info[1]);
    // EngineDll doesn't keep EBP frames, so scan the stack (x86 CONTEXT:
    // Esp at byte 196) for values pointing into EngineDll's code.  Most are
    // return addresses of the callers, nearest first; a few can be leftovers.
    u32 *esp = *(u32 **)((char *)p->ctx + 196);
    for (int n = 0, shown = 0; n < 1024 && shown < 16; n++) {
        u32 r = esp[n];
        if (r >= dll_base + 0x1000u && r < dll_base + 0x2a0000u) {
            PrintDebug("[SADX Truck]   stack word %4d  ghidra 0x%08X\n", n, r - dll_base + 0x10000000u);
            shown++;
        }
    }
    return 0;
}

static void *need(const char *mangled, const char *shortname)
{
    void *f = GetProcAddress((void *)dll_base, mangled);
    if (!f) PrintDebug("[SADX Truck] MISSING %s\n", shortname);
    return f;
}

// In-memory patch: make the function at `at` (ghidra address) return 0 at
// once, popping `pops` bytes of arguments (0 for cdecl, the N of the
// function's own "ret N" otherwise).  The DLL file on disk is not changed.
// `first` is the function's first byte, checked so a wrong address is
// caught instead of patched.
static int return_zero(u32 at, unsigned char first, u32 pops, const char *what)
{
    unsigned char *p = (unsigned char *)(at - 0x10000000u + dll_base);
    u32 old;
    if (p[0] != first) {
        PrintDebug("[SADX Truck] %s at ghidra 0x%08X: unexpected first byte, not patched\n", what, at);
        return 0;
    }
    VirtualProtect(p, 5, 0x40, &old);
    p[0] = 0x33; p[1] = 0xC0;                            // xor eax, eax
    if (pops) { p[2] = 0xC2; p[3] = pops & 255; p[4] = pops >> 8; }   // ret pops
    else      { p[2] = 0xC3; }                                       // ret
    VirtualProtect(p, 5, old, &old);
    return 1;
}

// In-memory patch: make the function at `at` (ghidra address) jump to `fn`.
static int jump_to(u32 at, unsigned char first, void *fn, const char *what)
{
    unsigned char *p = (unsigned char *)(at - 0x10000000u + dll_base);
    u32 old, rel = (u32)fn - ((u32)p + 5);
    if (p[0] != first) {
        PrintDebug("[SADX Truck] %s at ghidra 0x%08X: unexpected first byte, not patched\n", what, at);
        return 0;
    }
    VirtualProtect(p, 5, 0x40, &old);
    p[0] = 0xE9;                                         // jmp fn
    p[1] = rel & 255; p[2] = (rel >> 8) & 255; p[3] = (rel >> 16) & 255; p[4] = rel >> 24;
    VirtualProtect(p, 5, old, &old);
    return 1;
}

// SADX ground.  The truck lives in meters; SADX in units, 10 per meter
// (Sonic's collision height is 10 units, sonic.exe 0x9154E8, and he is
// 1 meter tall).  Truck point (0, 0, 0) sits at `origin`, a SADX point.
#define UNITS_PER_METER 10.0f
#define NO_GROUND (-100000.0f)
static tVect3 origin;

// Ground height below a truck point, in truck meters, or NO_GROUND.
// GetShadowPos (SADXFunctionsNew.h, 0x49E920): ground Y below a SADX point.
static float ground_m(float x, float y, float z)
{
    int ang[3];
    float g = GetShadowPos(origin.x + x * UNITS_PER_METER, origin.y + y * UNITS_PER_METER,
                           origin.z + z * UNITS_PER_METER, ang);
    if (g < origin.y - 100000.0f)
        return NO_GROUND;
    return (g - origin.y) / UNITS_PER_METER;
}

// Square root with the x87 fsqrt instruction (no C runtime for sqrtf).
static float sqrt_f(float v) { __asm__("fsqrt" : "+t"(v)); return v; }
// The compiler calls memcpy for some copies (the dent code's); there is no
// C library here (/nodefaultlib), so the mod has its own.
void *memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

// atan2 with the x87 fpatan instruction.
static float sin_f(float v) { __asm__("fsin" : "+t"(v)); return v; }
static float cos_f(float v) { __asm__("fcos" : "+t"(v)); return v; }

static float atan2_f(float y, float x)
{
    float r;
    __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

// Level geometry: the landtable, the level's collision pieces
// (SADXStructsNew.h _OBJ_LANDTABLE, _OBJ_LANDENTRY; ninja.h NJS_OBJECT,
// NJS_MODEL_SADX, NJS_MESHSET_SADX).  pObjLandTable, SADXVariablesNew.h,
// 0x3B2F718.  Byte offsets used:
//   landtable  piece count (short) 0, piece list 12
//   piece      center 0, radius 12, NJS_OBJECT 24, flags 32 (1 << 0 = solid),
//              36 bytes each
//   NJS_OBJECT flags 0, model 4, position 8, angle 20 (65536 = one turn),
//              scale 32, child 44, sibling 48
//   model      points 0, point count 8, meshsets 12, meshset count 20
//   meshset    type (top 2 bits of the short at 0: 0 triangles, 1 quads,
//              2 and 3 strips), polygon count 2, index list 4, 28 bytes each
// Each frame, before the truck steps, the triangles of the solid pieces
// within 6 m (about 20 feet) of the truck are copied here, in truck meters.
// In the game, gather takes SADX's own nearby ground list instead (see
// ListGroundForCollision there): the landtable plus objects' own ground.
#ifndef TEST_EXE
#define land_table (*(char **)0x3B2F718)
#endif
#define MAX_TRIS 4096
typedef struct { float a[3], e1[3], e2[3], n[3]; } tri;
static tri tris[MAX_TRIS];
static int tri_count, tri_dropped, pieces_near, pieces_rotated, mobile_near;
static const char *own_solid;            // the parked truck's solid box (solid_obj), set when added
// Water: landtable pieces with flag (1 << 1) (SADXEnums.h ColFlags_Water),
// gathered the same way into their own list.
#define MAX_WATER_TRIS 1024
static tri water_tris[MAX_WATER_TRIS];
static int water_count;
static tri *tri_dst = tris;              // where add_tri writes
static int *tri_n = &tri_count, tri_cap = MAX_TRIS;
static float near_c[3], near_r;          // gather sphere, SADX units

// 3x4 transform: p' = R p + t, R rows in m[0] to m[8], t in m[9] to m[11].
static void xf_mul(float o[12], const float a[12], const float b[12])
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            o[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
        o[9 + i] = a[i * 3] * b[9] + a[i * 3 + 1] * b[10] + a[i * 3 + 2] * b[11] + a[9 + i];
    }
}
static void xf_point(float o[3], const float m[12], const float p[3])
{
    for (int i = 0; i < 3; i++)
        o[i] = m[i * 3] * p[0] + m[i * 3 + 1] * p[1] + m[i * 3 + 2] * p[2] + m[9 + i];
}
// m = m * (rotation about axis 0, 1 or 2 by a SADX angle)
static void xf_rot(float m[12], int axis, int ang)
{
    if (!ang) return;
    float a = ang * (6.2831853f / 65536.0f), c = cos_f(a), s = sin_f(a), o[12];
    float r[12] = { 1, 0, 0,  0, 1, 0,  0, 0, 1,  0, 0, 0 };
    int i = (axis + 1) % 3, j = (axis + 2) % 3;
    r[i * 3 + i] = c; r[i * 3 + j] = -s; r[j * 3 + i] = s; r[j * 3 + j] = c;
    xf_mul(o, m, r);
    for (int k = 0; k < 12; k++) m[k] = o[k];
}

static void add_tri(const float *p, const float *q, const float *r)
{
    for (int i = 0; i < 3; i++) {
        float lo = p[i], hi = p[i];
        if (q[i] < lo) lo = q[i];
        if (q[i] > hi) hi = q[i];
        if (r[i] < lo) lo = r[i];
        if (r[i] > hi) hi = r[i];
        if (lo > near_c[i] + near_r || hi < near_c[i] - near_r) return;
    }
    if (*tri_n >= tri_cap) { tri_dropped++; return; }
    tri *t = &tri_dst[*tri_n];
    const float o[3] = { origin.x, origin.y, origin.z };
    for (int i = 0; i < 3; i++) {
        t->a[i] = (p[i] - o[i]) / UNITS_PER_METER;
        t->e1[i] = (q[i] - p[i]) / UNITS_PER_METER;
        t->e2[i] = (r[i] - p[i]) / UNITS_PER_METER;
    }
    float nx = t->e1[1] * t->e2[2] - t->e1[2] * t->e2[1];
    float ny = t->e1[2] * t->e2[0] - t->e1[0] * t->e2[2];
    float nz = t->e1[0] * t->e2[1] - t->e1[1] * t->e2[0];
    float l = nx * nx + ny * ny + nz * nz;
    if (l < 1e-10f) return;                             // no area
    l = 1.0f / sqrt_f(l);
    t->n[0] = nx * l; t->n[1] = ny * l; t->n[2] = nz * l;
    (*tri_n)++;
}
static void add_tri_ix(const float *pts, int np, const float m[12], int a, int b, int c)
{
    if ((unsigned)a >= (unsigned)np || (unsigned)b >= (unsigned)np || (unsigned)c >= (unsigned)np) return;
    float p[3], q[3], r[3];
    xf_point(p, m, pts + a * 3); xf_point(q, m, pts + b * 3); xf_point(r, m, pts + c * 3);
    add_tri(p, q, r);
}
static void gather_model(const char *mdl, const float m[12])
{
    const float *pts = *(const float *const *)mdl;
    int np = *(const int *)(mdl + 8);
    const char *sets = *(const char *const *)(mdl + 12);
    int ns = *(const unsigned short *)(mdl + 20);
    if (!pts || !sets) return;
    for (int k = 0; k < ns; k++) {
        const char *set = sets + k * 28;
        int type = *(const unsigned short *)set >> 14;
        int n = *(const unsigned short *)(set + 2);
        const short *ix = *(const short *const *)(set + 4);
        if (!ix) continue;
        for (int p = 0; p < n; p++) {
            if (type == 0) {                            // triangle
                add_tri_ix(pts, np, m, ix[0], ix[1], ix[2]);
                ix += 3;
            } else if (type == 1) {                     // quad, strip order
                add_tri_ix(pts, np, m, ix[0], ix[1], ix[2]);
                add_tri_ix(pts, np, m, ix[2], ix[1], ix[3]);
                ix += 4;
            } else {                                    // strip: count, then indices
                int cnt = *ix++ & 0x3fff;               // (1 << 15) = flip, not a count
                for (int j = 0; j + 2 < cnt; j++)
                    add_tri_ix(pts, np, m, ix[j], ix[j + 1], ix[j + 2]);
                ix += cnt;
            }
        }
    }
}
// One NJS_OBJECT, its children, and (below the root) their siblings.
// Rotation order: X, then Y, then Z (Ninja's default); with flag (1 << 5)
// Z, Y, X, which is my guess, not checked.
static void gather_obj(const char *obj, const float parent[12], int depth, int siblings)
{
    while (obj && depth < 16) {
        u32 ev = *(const u32 *)obj;
        const float *pos = (const float *)(obj + 8), *scl = (const float *)(obj + 32);
        const int *ang = (const int *)(obj + 20);
        float m[12];
        for (int k = 0; k < 12; k++) m[k] = parent[k];
        // A collision piece's own object (depth 0) is always placed at its
        // position and turned by its angle: SADX's collision does (its list
        // builder adds the position unconditionally, 0x43AD21, and turns by
        // the angle, 0x43AD3E), whatever the drawing flags say.  The bridge
        // to the Master Emerald has flags 0x17, "don't move, don't turn,
        // don't scale" for drawing, and was read at the world's center.
        if (depth == 0) ev &= ~(u32)((1 << 0) | (1 << 1));
        if (!(ev & (1 << 0))) {
            m[9] += m[0] * pos[0] + m[1] * pos[1] + m[2] * pos[2];
            m[10] += m[3] * pos[0] + m[4] * pos[1] + m[5] * pos[2];
            m[11] += m[6] * pos[0] + m[7] * pos[1] + m[8] * pos[2];
        }
        if (!(ev & (1 << 1))) {
            if (ev & (1 << 5)) { xf_rot(m, 2, ang[2]); xf_rot(m, 1, ang[1]); xf_rot(m, 0, ang[0]); }
            else               { xf_rot(m, 0, ang[0]); xf_rot(m, 1, ang[1]); xf_rot(m, 2, ang[2]); }
        }
        if (!(ev & (1 << 2)))
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) m[i * 3 + j] *= scl[j];
        const char *model = *(const char *const *)(obj + 4);
        if (model) gather_model(model, m);
        const char *child = *(const char *const *)(obj + 44);
        if (child) gather_obj(child, m, depth + 1, 1);
        if (!siblings) return;
        obj = *(const char *const *)(obj + 48);
    }
}
// Collect the triangles near truck point c (truck meters).
static void gather(const float c[3])
{
    tri_count = tri_dropped = pieces_near = pieces_rotated = water_count = mobile_near = 0;
    const char *lt = land_table;
    if (!lt) return;
    const char *list = *(const char *const *)(lt + 12);
    int n = *(const unsigned short *)lt;
    if (!list) return;
    near_c[0] = origin.x + c[0] * UNITS_PER_METER;
    near_c[1] = origin.y + c[1] * UNITS_PER_METER;
    near_c[2] = origin.z + c[2] * UNITS_PER_METER;
    near_r = 6.0f * UNITS_PER_METER;
    static const float id[12] = { 1, 0, 0,  0, 1, 0,  0, 0, 1,  0, 0, 0 };
#ifndef TEST_EXE
    // The same ground SADX stands on: ListGroundForCollision (0x43ACD0,
    // SADXFunctionsNew.h) fills SADX's nearby list (0x3B2F720, 12 bytes
    // each: flags 0, NJS_OBJECT 4, task 8; count, a short, 0x3B32724) from
    // three lists: objects' own ground (0x3B32728, count 0x3B36D38: the
    // wooden bridge to the Master Emerald is there), the landtable, and
    // moving collisions (MobileEntry).  GetShadowPos (0x49E920) builds it
    // the same way and takes every entry but water (1 << 1) and (1 << 22)
    // (0x49EA0A); so does the truck, without its own parked solid box.
    {
        // Landtable: every solid piece near the truck, as before the bridge
        // work (walls and poles too; SADX's nearby list below is a ground
        // and shadow list, and left a street pole out, so the truck drove
        // through it).
        for (int k = 0; k < n && k < 16384; k++) {
            const char *col = list + k * 36;
            if (!(*(const u32 *)(col + 32) & (1 << 0))) continue;   // not solid
            const float *cc = (const float *)col;
            float dx = cc[0] - near_c[0], dy = cc[1] - near_c[1], dz = cc[2] - near_c[2];
            float reach = cc[3] + near_r;
            if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
            const char *obj = *(const char *const *)(col + 24);
            if (!obj) continue;
            pieces_near++;
            gather_obj(obj, id, 0, 0);
        }
        // Objects' own ground from SADX's nearby list: entries with a task
        // (landtable entries there are taken above already).
        ((void (__cdecl *)(float, float, float, float))0x43ACD0)(near_c[0], near_c[1], near_c[2], near_r);
        int nl = *(const short *)0x3B32724;
        for (int k = 0; k < nl && k < 1024; k++) {
            const char *e = (const char *)0x3B2F720 + k * 12;
            if (!*(const char *const *)(e + 8)) continue;               // a landtable piece
            if (*(const u32 *)e & ((1u << 1) | (1u << 22))) continue;   // water, or not ground
            const char *obj = *(const char *const *)(e + 4);
            if (!obj || obj == own_solid) continue;
            mobile_near++;
            gather_obj(obj, id, 0, 0);
        }
        // Moving collisions again, straight from MobileEntry (0x3B32D30, 12
        // bytes each, count 0x3B2D510), any not already taken above.  The
        // bridge to the Master Emerald is two of them (its task 0x533880
        // adds them at 0x533728 and 0x53378A, flags (1 << 29) and (1 << 29)
        // | (1 << 12), no solid flag); SADX's nearby list picks moving pieces
        // by their model's center and radius, and left the deck out, so
        // even Sonic's shadow (GetShadowPos) is missing on it.  Here every
        // triangle is checked against the box around the truck instead
        // (add_tri), whatever the model's radius says.
        int nm = *(const short *)0x3B2D510;
        for (int k = 0; k < nm && k < 256; k++) {
            const char *e = (const char *)0x3B32D30 + k * 12;
            if (*(const u32 *)e & ((1u << 1) | (1u << 22))) continue;
            const char *obj = *(const char *const *)(e + 4);
            if (!obj || obj == own_solid) continue;
            int taken = 0;
            for (int j = 0; j < nl && j < 1024 && !taken; j++)
                if (*(const char *const *)((const char *)0x3B2F720 + j * 12 + 4) == obj) taken = 1;
            if (taken) continue;
            int before = tri_count;
            gather_obj(obj, id, 0, 0);
            if (tri_count > before) mobile_near++;
        }
    }
#else
    for (int k = 0; k < n && k < 16384; k++) {
        const char *col = list + k * 36;
        if (!(*(const u32 *)(col + 32) & (1 << 0))) continue;   // not solid
        const float *cc = (const float *)col;
        float dx = cc[0] - near_c[0], dy = cc[1] - near_c[1], dz = cc[2] - near_c[2];
        float reach = cc[3] + near_r;
        if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
        const char *obj = *(const char *const *)(col + 24);
        if (!obj) continue;
        pieces_near++;
        const int *ang = (const int *)(obj + 20);
        if (ang[0] || ang[1] || ang[2]) pieces_rotated++;
        gather_obj(obj, id, 0, 0);
    }
#endif
    // Water pieces, into water_tris.
    tri_dst = water_tris; tri_n = &water_count; tri_cap = MAX_WATER_TRIS;
    for (int k = 0; k < n && k < 16384; k++) {
        const char *col = list + k * 36;
        if (!(*(const u32 *)(col + 32) & (1 << 1))) continue;   // not water
        const float *cc = (const float *)col;
        float dx = cc[0] - near_c[0], dy = cc[1] - near_c[1], dz = cc[2] - near_c[2];
        float reach = cc[3] + near_r;
        if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
        const char *obj = *(const char *const *)(col + 24);
        if (obj) gather_obj(obj, id, 0, 0);
    }
    tri_dst = tris; tri_n = &tri_count; tri_cap = MAX_TRIS;
}

// Nearest level triangle along s + d * t, 0 <= t <= len (both sides of a
// triangle count).  Normal turned to face the ray's start.
static int ray_list(const tri *list, int count, const tVect3 *s, const tVect3 *d, float len, float *t_out, float n_out[3])
{
    float best = len;
    int hit = -1;
    for (int k = 0; k < count; k++) {
        const tri *t = &list[k];
        float px = d->y * t->e2[2] - d->z * t->e2[1];
        float py = d->z * t->e2[0] - d->x * t->e2[2];
        float pz = d->x * t->e2[1] - d->y * t->e2[0];
        float det = t->e1[0] * px + t->e1[1] * py + t->e1[2] * pz;
        if (det > -1e-12f && det < 1e-12f) continue;    // ray along the triangle
        float inv = 1.0f / det;
        float tx = s->x - t->a[0], ty = s->y - t->a[1], tz = s->z - t->a[2];
        float u = (tx * px + ty * py + tz * pz) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        float qx = ty * t->e1[2] - tz * t->e1[1];
        float qy = tz * t->e1[0] - tx * t->e1[2];
        float qz = tx * t->e1[1] - ty * t->e1[0];
        float v = (d->x * qx + d->y * qy + d->z * qz) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        float tt = (t->e2[0] * qx + t->e2[1] * qy + t->e2[2] * qz) * inv;
        if (tt < 0.0f || tt > best) continue;
        best = tt;
        hit = k;
    }
    if (hit < 0) return 0;
    const float *n = list[hit].n;
    float sign = n[0] * d->x + n[1] * d->y + n[2] * d->z > 0.0f ? -1.0f : 1.0f;
    n_out[0] = n[0] * sign; n_out[1] = n[1] * sign; n_out[2] = n[2] * sign;
    *t_out = best;
    return 1;
}

static int ray_tris(const tVect3 *s, const tVect3 *d, float len, float *t_out, float n_out[3])
{
    return ray_list(tris, tri_count, s, d, len, t_out, n_out);
}

// Water surface height above the truck body's origin (meters; negative =
// the surface is below it), or NO_WATER.  Cabela's 3 compares the same
// two heights (GameDll UpdateDamageByWater 0x10020B10: body+0x2C against
// the surface EngineDll stores at body+0x150).  A ray straight up from 3 m
// (about 10 feet) below the body origin, 30 m long.
#define NO_WATER -1000.0f
static float water_above_body(void)
{
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *pos = (const float *)(body + 0x28);
    tVect3 s = { pos[0], pos[1] - 3.0f, pos[2] }, d = { 0, 1, 0 };
    float t, n[3];
    if (!ray_list(water_tris, water_count, &s, &d, 30.0f, &t, n)) return NO_WATER;
    return t - 3.0f;
}

// Stands in for cBspTree::HitCache (0x10201160): the wheel ray.
// thiscall with 4 stack arguments = fastcall with an unused EDX; both pop
// 16 bytes.  Ray from `s` along `d` (length 1) for `len` meters.  Fills the
// hit info the way the real one does for terrain: type 7 at byte 0, -1 at
// byte 20, hit point at bytes 8, 12, 16, ground normal at bytes 40, 44, 48.
// The ground is treated as flat under the ray start, tilted by the slope
// measured 0.2 m (8 inches) to the side and ahead.
// Diagnostics for the fall-through: after the truck first lands, log each
// wheel ray that misses (and each hit on a slope steeper than 45 degrees),
// up to 60 lines per spawn.  `landed` and `logged` are reset by spawn().
static int landed, logged;
static int lost_ground;                  // 1 = just lost it, 2 = already shown
static void ray_log(const char *what, const tVect3 *s, const tVect3 *d, float len, float g, float t)
{
    if (!landed || logged >= 60)
        return;
    logged++;
    PrintDebug("[SADX Truck]   frame %4d ray %-18s start %7.3f %7.3f %7.3f  dir %6.3f %6.3f %6.3f  len %.3f  ground %.3f  t %.3f\n",
               frame, what, (double)s->x, (double)s->y, (double)s->z, (double)d->x, (double)d->y, (double)d->z,
               (double)len, (double)g, (double)t);
}

static int rays_now, hits_now;            // wheel rays this frame, and how many hit
static int box_rays_now, box_hits_now;    // body box corner rays this frame, and how many hit
static int backup_now;                    // hits this frame that came from the GetShadowPos backup
// The hardest body contact this frame (for hit damage): speed into the
// surface (m/s) and where (truck meters, the physics' world).
// The velocity is the body's at the start of the frame (vel_before, saved
// in OnFrame before the physics update).  Body velocity: byte 52 (0x34;
// EngineDll's TestStop 0x100E5062 measures speed from 0x34 to 0x3C);
// byte 100 (0x64) is a unit direction, the forward axis (it read 1.00 into
// every wall in the test host).
static float contact_speed, contact_pt[3], contact_n[3], vel_before[3];
static void note_contact(int box, float px, float py, float pz, const float n[3])
{
    const float *vel = vel_before;
    // A wheel ray on ground under the truck is the suspension, not a hit:
    // skip wheel contacts whose surface faces the truck's underside.
    if (!box) {
        const float *up = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x4c) + 3;
        if (n[0] * up[0] + n[1] * up[1] + n[2] * up[2] > 0.7f) return;
    }
    // n faces the truck (the ray's start); moving into the surface = -(v . n)
    float into = -(vel[0] * n[0] + vel[1] * n[1] + vel[2] * n[2]);
#ifdef TH_CONTACTS
    if (into > 0.5f) PrintDebug("[contact] frame %d into %.2f n %.2f %.2f %.2f at %.2f %.2f %.2f\n", frame, (double)into, (double)n[0], (double)n[1], (double)n[2], (double)px, (double)py, (double)pz);
#endif
    if (into > contact_speed) {
        contact_speed = into;
        contact_pt[0] = px; contact_pt[1] = py; contact_pt[2] = pz;
        contact_n[0] = n[0]; contact_n[1] = n[1]; contact_n[2] = n[2];
    }
}
// A wheel whose tire came off (part black, level 4) rides on its rim: that
// corner sits lower by the tire's thickness.  The wheel rays come here, so
// for that wheel the ground is reported one tire thickness further along
// the ray; the spring stretches by that much and the corner drops.  Past
// the ray's end it counts as no ground (the rim hangs; the body box rests
// on the ground).  Thickness: outer minus inner radius of the truck's tire
// pieces (texture 14), measured once per truck.
static int part_level(int k);
static float tire_thick = -1.0f;
static int tire_thick_truck = -1;
static float tire_thickness(void)
{
    if (tire_thick_truck == truck_now) return tire_thick;
    tire_thick_truck = truck_now;
    // lo = the rim's outer edge (wheel 0's other pieces), hi = the tire's.
    float lo = 0.0f, hi = 0.0f;
    for (int k = 0; k < car.draws; k++) {
        const unsigned short *d = truck_draws[car.first_draw + k];
        if (d[1] != 1) continue;                                    // wheel 0
        const struct truck_model *m = &truck_models[d[0]];
        int tire = m->tex == 14;
        for (int i = 0; i < m->npoints; i++) {
            float r = sqrt_f(m->points[i][1] * m->points[i][1] + m->points[i][2] * m->points[i][2]);
            if (tire && r > hi) hi = r;
            if (!tire && r > lo) lo = r;
        }
    }
    tire_thick = hi > lo ? (hi - lo) / UNITS_PER_METER : 0.0f;
    PrintDebug("[SADX Truck] %s wheel: rim edge %.2f m, tire edge %.2f m from the hub: %.2f m (%.1f inches) of rubber\n", car.name,
               (double)(lo / UNITS_PER_METER), (double)(hi / UNITS_PER_METER), (double)tire_thick, (double)(tire_thick * 39.37f));
    return tire_thick;
}
// Called for a wheel ray that hit: 1 = still a hit (info moved), 0 = now a miss.
static int rim_drop(const tVect3 *s, const tVect3 *d, float len, char *info)
{
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    float r[3] = { s->x - pos[0], s->y - pos[1], s->z - pos[2] };
    float x = r[0] * rot[0] + r[1] * rot[1] + r[2] * rot[2];
    float z = r[0] * rot[6] + r[1] * rot[7] + r[2] * rot[8];
    int w = (z < 0 ? 2 : 0) + (x < 0 ? 0 : 1);     // the damage parts' wheel order
#ifdef TH_RIMLOG
    if (frame % 20 == 0) PrintDebug("[rim] frame %d ray x %+.2f z %+.2f -> wheel %d level %d\n", frame, (double)x, (double)z, w, part_level(w));
#endif
    if (part_level(w) < 4) return 1;
    float drop = tire_thickness();
    float *f = (float *)info;
    float t = (f[2] - s->x) * d->x + (f[3] - s->y) * d->y + (f[4] - s->z) * d->z + drop;
    if (t > len) return 0;
    f[2] = s->x + d->x * t; f[3] = s->y + d->y * t; f[4] = s->z + d->z * t;
    return 1;
}

static int __fastcall sadx_hit(void *bsp, void *edx, const tVect3 *s, const tVect3 *d, float len, char *info)
{
    (void)bsp; (void)edx;
    // Who is asking: the body box test (0x102294a0 to 0x10229b30) casts a
    // ray from a box's center to each corner, length 1 = the full corner
    // offset; everything else here is a wheel ray (unit direction, len m).
    u32 from = (u32)__builtin_return_address(0) - dll_base + 0x10000000u;
    int box = from >= 0x102294a0u && from < 0x10229b30u;
    if (box) box_rays_now++; else rays_now++;
#ifdef CALLERS
    {
        static u32 who[16], count[16], hit_from[16];
        int k;
        for (k = 0; k < 16 && who[k] && who[k] != from; k++) {}
        if (k < 16) { who[k] = from; count[k]++; }
        if (frame == 400 && !hit_from[0]) {
            hit_from[0] = 1;
            for (k = 0; k < 16 && who[k]; k++)
                PrintDebug("[callers] HitCache called from ghidra 0x%08X  %u times\n", who[k], count[k]);
        }
    }
#endif
    {
        // The level's own triangles first: floors, walls, ledge sides.
        float t, n[3];
        int hit = ray_tris(s, d, len, &t, n);
        if (hit) {
            if (box) box_hits_now++;
            else { landed = 1; hits_now++; }
            float *f = (float *)info;
            *(int *)info = 7;
            *(int *)(info + 20) = -1;
            f[2] = s->x + d->x * t; f[3] = s->y + d->y * t; f[4] = s->z + d->z * t;
            f[10] = n[0]; f[11] = n[1]; f[12] = n[2];
            if (!box && !rim_drop(s, d, len, info)) return 0;
            note_contact(box, f[2], f[3], f[4], n);
            return 1;
        }
    }
    // Backup when no triangle is hit: the floor height from GetShadowPos
    // (also catches a ray that starts under the floor, and moving platforms).
    if (d->y > -0.01f) {                                 // not pointing down
        if (!box) ray_log("not pointing down", s, d, len, 0.0f, 0.0f);
        return 0;
    }
    // Floor under the ray's END (a box corner can be 2 m to the side of
    // the box center), searched from 1 m (about 3 feet) above the ray
    // start: when the truck leans hard, a start can dip under the floor
    // and a search starting there finds nothing.
    float ex = s->x + d->x * len, ez = s->z + d->z * len;
    float g = ground_m(ex, s->y + 1.0f, ez);
    if (g == NO_GROUND) {
        if (!box) {
            if (landed && !lost_ground) lost_ground = 1;
            ray_log("no ground", s, d, len, g, 0.0f);
        }
        return 0;
    }
    float t = (g - s->y) / d->y;
    if (t < 0.0f) {
        // Start under the floor: report a hit right at the start (spring
        // fully pressed), which pushes that corner back up.
        if (!box) ray_log("start under ground", s, d, len, g, t);
        t = 0.0f;
        g = s->y;
    }
    if (t > len) {
        if (!box) ray_log("ground too far", s, d, len, g, t);
        return 0;
    }
    float hx = s->x + d->x * t, hz = s->z + d->z * t;
    float gx = ground_m(hx + 0.2f, s->y + 1.0f, hz), gz = ground_m(hx, s->y + 1.0f, hz + 0.2f);
    float nx = gx == NO_GROUND ? 0.0f : -(gx - g) / 0.2f;
    float nz = gz == NO_GROUND ? 0.0f : -(gz - g) / 0.2f;
    if (nx * nx + nz * nz > 1.0f && !box)
        ray_log("hit, steep slope", s, d, len, g, t);
    if (box) box_hits_now++;
    else { landed = 1; hits_now++; }
    backup_now++;
    float k = 1.0f / sqrt_f(nx * nx + 1.0f + nz * nz);
    float *f = (float *)info;
    *(int *)info = 7;
    *(int *)(info + 20) = -1;
    f[2] = hx; f[3] = g; f[4] = hz;
    f[10] = nx * k; f[11] = k; f[12] = nz * k;
    if (!box && !rim_drop(s, d, len, info)) return 0;
    note_contact(box, f[2], f[3], f[4], f + 10);
    return 1;
}

// The truck asks the map (g_bsp) about the world every step, from
// cPhysVehicle::TestCollisions and the wheel ray (FUN_100dec40).  There is
// no map in SADX:
//   cBspTree::SetHitCache   0x10201090  thiscall, ret 8   -> does nothing
//                                         (collects map pieces near the truck)
//   cBspTree::HitCache      0x10201160  thiscall, ret 16  -> sadx_hit
//   GetLeaf call inside the body box test, 0x1022954e  -> "leaf 0"
//     (GetLeaf 0x1007ad00 reads the map; the box test gives up when it
//     returns -1.  Only this one call is replaced: 5 bytes "call GetLeaf"
//     become "pop eax" (takes the pushed 0 back off the stack, so eax = 0)
//     and 4 do-nothing bytes.)
// The body box test (0x102294a0) then runs as in the game: a ray from each
// collision box's center to each of its 8 corners through HitCache, and a
// corner under the floor becomes a contact.
// The first bytes checked are push ebx (83), sub esp (129), call (232).
static int sadx_world(void)
{
    unsigned char *p = (unsigned char *)(0x1022954eu - 0x10000000u + dll_base);
    u32 old;
    if (p[0] != 232) {
        PrintDebug("[SADX Truck] GetLeaf call at ghidra 0x1022954E: unexpected first byte, not patched\n");
        return 0;
    }
    VirtualProtect(p, 5, 0x40, &old);
    p[0] = 0x58; p[1] = p[2] = p[3] = p[4] = 0x90;      // pop eax, 4 x nop
    VirtualProtect(p, 5, old, &old);
    return return_zero(0x10201090u, 83, 8, "SetHitCache") &
           jump_to(0x10201160u, 129, (void *)sadx_hit, "HitCache");
}

// Experiment: the body's center of mass is its origin, 1.05 m (about 3 1/2
// feet) above the wheel centers.  Moving every truck point up by COM_UP
// meters puts the center of mass that much lower relative to the wheels.
// Before cabelas_steering, 0.0 tipped in a 16 mph turn and 0.4 (about 16
// inches) did not.  With cabelas_steering both stay upright, so 0.0 (the
// real Cabela's truck) is the default.
#ifndef COM_UP
#define COM_UP 0.0f
#endif
static int build_truck(void)
{
    int (__cdecl *PhysWorldInit)(int) = need("?PhysWorldInit@@YAHH@Z", "PhysWorldInit");
    int (__cdecl *PhysSetWorldParam)(const char *, const char *) = need("?PhysSetWorldParam@@YAHPAD0@Z", "PhysSetWorldParam");
    void (__thiscall *ManagerCtor)(void *, int, int) = need("??0cPhysManager@@QAE@HH@Z", "manager constructor");
    void (__thiscall *AddObject)(void *, void *, int) = need("?AddObject@cPhysManager@@UAEXPAVcPhysicsObject@@H@Z", "manager AddObject");
    void (__thiscall *ctor)(void *) = need("??0cPhysVehicle@@QAE@XZ", "constructor");
    void (__thiscall *SetNumWheels)(void *, u32) = need("?SetNumWheels@cPhysVehicle@@UAEXI@Z", "SetNumWheels");
    void (__thiscall *Create)(void *) = need("?Create@cPhysVehicle@@UAEXXZ", "Create");
    SetPosition = need("?SetPosition@cPhysVehicle@@UAEXABUtVect3@@@Z", "SetPosition");
    SetOrientation = need("?SetOrientation@cPhysicsObject@@UAEXABUm3x3@@@Z", "SetOrientation");
    PressAcceleration = need("?PressAcceleration@cPhysVehicle@@UAEXM@Z", "PressAcceleration");
    PressBrake = need("?PressBrake@cPhysVehicle@@UAEXM@Z", "PressBrake");
    SetSteerAngle = need("?SetSteerAngle@cPhysVehicle@@QAEXM@Z", "SetSteerAngle");
    void (__thiscall *TestAllColisions)(void *) = need("?TestAllColisions@cPhysicsObject@@UAEXXZ", "TestAllColisions");
    void (__thiscall *SolveAllColisions)(void *) = need("?SolveAllColisions@cPhysicsObject@@UAEXXZ", "SolveAllColisions");
    SetLinearSpeed = need("?SetLinearSpeed@cPhysicsObject@@UAEXMMM@Z", "SetLinearSpeed");
    SetAngularSpeed = need("?SetAngularSpeed@cPhysicsObject@@UAEXMMM@Z", "SetAngularSpeed");
    void (__thiscall *SetBodyFrictionCoef)(void *, float) = need("?SetBodyFrictionCoef@cPhysicsObject@@UAEXM@Z", "SetBodyFrictionCoef");
    void (__thiscall *SetPosSpring)(void *, u32, tVect3 *) = need("?SetPosSpring@cPhysVehicle@@QAEXIAAUtVect3@@@Z", "SetPosSpring");
    void (__thiscall *SetPosTire)(void *, u32, tVect3 *) = need("?SetPosTire@cPhysVehicle@@QAEXIAAUtVect3@@@Z", "SetPosTire");
    void (__thiscall *SetSuspConst)(void *, float, float) = need("?SetSuspConst@cPhysVehicle@@QAEXMM@Z", "SetSuspConst");
    void (__thiscall *SetSuspInitLength)(void *, float, float, float) = need("?SetSuspInitLength@cPhysVehicle@@QAEXMMM@Z", "SetSuspInitLength");
    void (__thiscall *SetCollBoxes)(void *, void *, int) = need("?SetCollBoxes@cPhysVehicle@@QAEXPAU_tBox@@H@Z", "SetCollBoxes");
    void (__thiscall *SetMass)(void *, float) = need("?SetMass@cPhysicsObject@@UAEXM@Z", "SetMass");
    void (__thiscall *SetMaxGearboxRpm)(void *, float) = need("?SetMaxGearboxRpm@cPhysVehicle@@QAEXM@Z", "SetMaxGearboxRpm");
    void (__thiscall *SetMinGearboxRpm)(void *, float) = need("?SetMinGearboxRpm@cPhysVehicle@@QAEXM@Z", "SetMinGearboxRpm");
    void (__thiscall *SetGearboxRedLine)(void *, float) = need("?SetGearboxRedLine@cPhysVehicle@@QAEXM@Z", "SetGearboxRedLine");
    SetTorqueScale = need("?SetTorqueScale@cPhysVehicle@@QAEXM@Z", "SetTorqueScale");
    GearUp = need("?GearUp@cPhysVehicle@@QAEXXZ", "GearUp");
    GearDown = need("?GearDown@cPhysVehicle@@QAEXXZ", "GearDown");
    IsInActiveList = need("?IsInActiveList@cPhysicsObject@@UAEHXZ", "IsInActiveList");
    ActivateObject = need("?ActivateObject@cPhysicsObject@@UAEXXZ", "ActivateObject");
    void (__thiscall *SetAxleState)(void *, int, int) = need("?SetAxleState@cPhysVehicle@@QAEXHH@Z", "SetAxleState");
    void (__thiscall *SetGearRatios)(void *, float *) = need("?SetGearRatios@cPhysVehicle@@QAEXPAM@Z", "SetGearRatios");
    void (__thiscall *SetAcceleratorSpd)(void *, float) = need("?SetAcceleratorSpd@cPhysVehicle@@QAEXM@Z", "SetAcceleratorSpd");
    SetTractionType = need("?SetTractionType@cPhysVehicle@@QAEXI@Z", "SetTractionType");
    void (__thiscall *SetAxleParams)(void *, int, float, float) = need("?SetAxleParams@cPhysVehicle@@QAEXHMM@Z", "SetAxleParams");
    ManagerUpdate = need("?Update@cPhysManager@@UAEHM@Z", "manager Update");
    GetWheelTM = need("?GetWheelTM@cPhysVehicle@@QAE?AUm4x4@@I@Z", "GetWheelTM");
    GetRPM = need("?GetRPM@cPhysVehicle@@QBEMXZ", "GetRPM");
    GetCurGear = need("?GetCurGear@cPhysVehicle@@QBEHXZ", "GetCurGear");
    if (!PhysWorldInit || !PhysSetWorldParam || !ManagerCtor || !AddObject || !ctor || !SetNumWheels ||
        !Create || !SetPosition || !PressAcceleration || !TestAllColisions || !SolveAllColisions ||
        !SetLinearSpeed || !SetAngularSpeed || !SetBodyFrictionCoef || !SetPosSpring || !SetPosTire ||
        !SetSuspConst || !SetSuspInitLength || !SetCollBoxes || !SetMass || !SetMaxGearboxRpm ||
        !SetMinGearboxRpm || !SetGearboxRedLine || !SetTorqueScale || !GearUp || !GearDown || !IsInActiveList || !ActivateObject || !SetAxleState || !SetGearRatios || !SetAxleParams ||
        !SetAcceleratorSpd || !SetTractionType || !ManagerUpdate || !GetWheelTM || !GetRPM || !GetCurGear ||
        !PressBrake || !SetSteerAngle || !SetOrientation)
        return 0;

    // World (once) and manager: same calls and numbers as Engine.exe at
    // 0x414018.  Every spawn builds a new manager and truck, so nothing from
    // a crashed or rolled truck (tilt, wheel spin, RPM) carries over.  The
    // old ones are left unused: about 8 KB each, plus EngineDll's own.
    static int world_done;
    if (!world_done) {
        PhysWorldInit(400);
        PhysSetWorldParam("gravity", "9.81f");
        if (!sadx_world())
            return 0;
        world_done = 1;
    }
    manager = VirtualAlloc(0, 4096, 0x3000, 4);
    ManagerCtor(manager, 400, 400);

    // Truck: same order as truckprobe step 6 (GameDll FUN_10025b90, then
    // FUN_10024b50).  See truckprobe.c for where each number comes from.
    truck = VirtualAlloc(0, 4096, 0x3000, 4);
    ctor(truck);
    SetNumWheels(truck, 4);
    *((unsigned char *)truck + 0x10c) |= (1 << 3);       // automatic gearbox
    Create(truck);

    char *t = (char *)truck;
    char *body = *(char **)(t + 4);
    u32 nw = *(u32 *)(t + 0xe8);
    for (u32 i = 0; i < nw; i++)
        *(float *)(((char **)*(void **)(t + 0xcc))[i] + 0xf4) = 1.0f;
    TestAllColisions(truck);
    SolveAllColisions(truck);
    *(float *)(body + 0x168) = -0.009f;
    SetLinearSpeed(truck, 0, 0, 0);
    SetAngularSpeed(truck, 0, 0, 0);
    SetBodyFrictionCoef(truck, 0.1f);
    *(u32 *)(body + 0xbc) |= (1 << 4) | (1 << 5);
    *(float *)(body + 0x154) = 0.3f;
    *(float *)(body + 0x178) = 1.0f;
    *(float *)(body + 0x180) = 1.0f;
    *(float *)(body + 0x174) = 0.98f;
    *(float *)(body + 0x17c) = 0.989f;

    for (u32 i = 0; i < 4; i++) {
        tVect3 s = car.spring[i], w = car.tire[i];
        s.y += COM_UP; w.y += COM_UP;
        SetPosSpring(truck, i, &s);
        SetPosTire(truck, i, &w);
    }
    SetSuspConst(truck, 125.0f, 15.0f);
    SetSuspInitLength(truck, car.spring_to_tire, 0.5f, 0.75f);

    static float boxes[3][70];
    for (int b = 0; b < 3; b++) {
        float *x = boxes[b];
        x[0] = car.dummy[b][3]; x[1] = car.dummy[b][4] + COM_UP; x[2] = car.dummy[b][5];
        x[3] = car.dummy[b][0] * 0.5f; x[4] = car.dummy[b][1] * 0.5f; x[5] = car.dummy[b][2] * 0.5f;
        x[6] = 1; x[7] = 0; x[8] = 0;
        x[9] = 0; x[10] = 1; x[11] = 0;
        x[12] = 0; x[13] = 0; x[14] = 1;
    }
    SetCollBoxes(truck, boxes, 3);

    float *axle = *(float **)(t + 0x104);
    axle[0] = car.axle[0];
    axle[1] = car.axle[1];
    float *pivot = *(float **)(t + 0x100);
    pivot[0] = car.pivot[0];
    pivot[1] = car.pivot[1];
    *(u32 *)(t + 0x84) = 80;
    *(u32 *)(t + 0x174) |= (1 << 0) | (1 << 1) | (1 << 2) | (1 << 4);

    SetMass(truck, car.mass);
    for (u32 i = 0; i < nw; i++)
        *(float *)(((char **)*(void **)(t + 0xcc))[i] + 0xf4) = car.adherence;
    SetMaxGearboxRpm(truck, car.max_rpm);
    SetMinGearboxRpm(truck, car.min_rpm);
    SetGearboxRedLine(truck, car.red_rpm);
    *(float *)(t + 0xf4) = car.steering;
    SetTorqueScale(truck, 1.0f);
    SetAxleState(truck, car.axle_state[0], car.axle_state[1]);
    // An independent side (0) gets SetAxleParams(side, pitch scale, start
    // pitch), cars.lua SetPhysDrivingTrain values 3 and 5 for the front,
    // 4 and 6 for the rear (truckprobe step 6 did the front this way).
    if (car.axle_state[0] == 0) SetAxleParams(truck, 0, car.axle_params[0], car.axle_params[1]);
    if (car.axle_state[1] == 0) SetAxleParams(truck, 1, car.axle_params[2], car.axle_params[3]);
    static float gears[10];
    for (int g = 0; g < 6; g++)
        gears[g] = car.gears[g];
    SetGearRatios(truck, gears);
    SetSuspInitLength(truck, car.init_len, car.min_len, car.max_len);
    SetSuspConst(truck, car.ksusp, car.kdump);
    *(float *)(t + 0x110) = car.rolling * 0.5f;
    *(float *)(t + 0xf8) = 1.0f;
    SetAcceleratorSpd(truck, 1.0f);
    SetTractionType(truck, traction_type[traction_mode]);
    PressAcceleration(truck, 0.0f);

#ifdef AIDS
    {
        // Experiment: the driving aids Cabela's shows as "ETC engaged" and
        // "HDC engaged".  SetETC sets truck byte 269 bit (1 << 4).
        void (__thiscall *SetETC)(void *, int) = need("?SetETC@cPhysVehicle@@QAEXH@Z", "SetETC");
        void (__thiscall *SetHDC)(void *, int) = need("?SetHDC@cPhysVehicle@@QAEXH@Z", "SetHDC");
        if (SetETC) SetETC(truck, 1);
        if (SetHDC) SetHDC(truck, 1);
    }
#endif
    AddObject(manager, truck, 1);
    return 1;
}

static int spawn_angle;                  // Sonic's angle when the truck spawned

// The trucks' look: each truck's own model from Cabela's, converted by
// skin_to_c.py into trucks_model.h (truck_models: 478 pieces, the ones two
// trucks share stored once; truck_draws: which pieces a truck draws and
// with which matrix).  Each piece is drawn with the body's or its wheel's
// matrix from the physics.
//
// SADX (Ninja) model structures, ninja.h:
//   NJS_MATERIAL     diffuse, specular, exponent, texture number, flags (20 bytes)
//   NJS_MESHSET_SADX type and material, count, indices, attrs, normals,
//                    colors, texture coordinates, buffer (28 bytes)
//   NJS_MODEL_SADX   points, normals, point count, meshsets, materials,
//                    meshset count, material count, center, radius, buffer
// Drawn with SADX's dynamic model path (njDrawModel_SADX_Dynamic,
// SADXFunctions.h, 0x784AE0): the usual path (0x77EDA0) skips meshsets
// without the vertex buffer the game builds for its own models, and goes to
// 0x784AE0 itself when render flags 0x3D08498 & 6 are set.
// Textures: texture pack SADXTRUCK in the mod's textures folder (14 paints,
// tire, shock, 14 pictures), loaded with LoadPVM (SADXFunctions.h, 0x421180).
//
// Truck picker: on foot, D-pad left / right picks the previous / next
// truck and shows its picture for 2 seconds, drawn with
// njDrawSprite2D_ForcePriority (SADXFunctions.h, 0x425910) in the middle
// near the top of the screen.  Screen size: HorizontalResolution and
// VerticalResolution (SADXVariables.h, 0x892990 and 0x892994); SADX's 2D
// pictures are made for a 640 x 480 screen and scaled by VerticalStretch
// (0x8928C4, height / 480).
#ifndef TEST_EXE
#define njDrawModel_Dynamic ((void (__cdecl *)(void *))0x784AE0)
// late_DrawModel (SADXFunctionsNew.h, 0x4078F0): queued with the current
// matrix and texture list (sonic.exe 0x403F60 copies them), drawn sorted
// after the solid models.  Second argument: LATE flags, 0 = LATE_MAT.
#define late_DrawModel ((void (__cdecl *)(void *, int))0x4078F0)
#define LoadPVM ((void (__cdecl *)(const char *, void *))0x421180)
#define nj_current_matrix (*(float **)0x3D0F818)   // _nj_current_matrix_ptr_
#define level_now (*(short *)0x3B22DCC)            // ssStageNumber, SADXVariablesNew.h
#define act_now (*(short *)0x3B22DEC)              // ssActNumber
#define njDrawSprite2D_ForcePriority ((void (__cdecl *)(void *, int, float, u32))0x425910)
#define screen_w (*(int *)0x892990)
#define screen_h (*(int *)0x892994)
#define stretch_2d (*(float *)0x8928C4)
// SADXFunctions.h / SADXFunctionsNew.h
#define SetMaterialAndSpriteColor_Float ((void (__cdecl *)(float, float, float, float))0x4128A0)
#define njColorBlendingMode ((void (__cdecl *)(int, int))0x4030D0)
#define ResetMaterial ((void (__cdecl *)(void))0x4128F0)
// ___njDrawSprite3D (SADXFunctionsNew.h, 0x407070): a 3D sprite (faces the
// screen) queued with SADX's late drawing; with NJD_SPRITE_ALPHA it does
// not write depth, so glows overlap without cutting holes.  (The first
// game run showed nothing because the glows were too small to see, not
// because of the queue: the textures are mostly dark around a small core.)
#define njDrawSprite3D_Late ((void (__cdecl *)(void *, int, u32))0x407070)
// camera_twp (SADXVariablesNew.h, 0x3B2CBB0): the camera's taskwk,
// position at byte 32.
#define camera_twp (*(char **)0x3B2CBB0)
#endif
typedef struct { u32 diffuse, specular; float exponent; u32 tex, flags; } nj_material;
typedef struct { unsigned short type_mat, n; short *meshes; void *attrs, *normals, *colors; short *uv; void *buffer; } nj_meshset;
typedef struct { void *points, *normals; int npoints; nj_meshset *sets; nj_material *mats; unsigned short nsets, nmats; float center[3], r; void *buffer; } nj_model;
typedef struct { void *name; u32 attr, texaddr; } nj_texname;
static nj_texname truck_texnames[TRUCK_TEXTURES];
static struct { nj_texname *textures; u32 n; } truck_texlist = { truck_texnames, TRUCK_TEXTURES };
static nj_material nj_mats[TRUCK_MODELS];
static nj_meshset nj_sets[TRUCK_MODELS];
static nj_model nj_models[TRUCK_MODELS];
static int truck_textures_tried;
static int picture_frames;               // frames the picker picture stays up

static void make_truck_models(void)
{
    for (int k = 0; k < TRUCK_MODELS; k++) {
        const struct truck_model *p = &truck_models[k];
        nj_material *m = &nj_mats[k];
        m->diffuse = 0xFFB2B2B2u;                         // SADX's usual grey
        m->specular = 0;
        m->exponent = 11.0f;
        m->tex = (u32)p->tex;
        // Texture on, both sides drawn (Cabela's and SADX may disagree on
        // which side is the front), normal alpha blending.
        m->flags = (1u << 21) | (1u << 23) | (1u << 31) | (1u << 28) | (1u << 26);
        if (p->lamp)                                    // lamps: no shading, added light
            m->flags = (1u << 21) | (1u << 23) | (1u << 25) | (1u << 20) | (1u << 31) | (1u << 26);
        if (p->tex >= TRUCK_TEX_GLASS)                  // windows, whole and cracked: the texture's alpha (NJD_FLAG_USE_ALPHA)
            m->flags = (1u << 21) | (1u << 23) | (1u << 20) | (1u << 31) | (1u << 28) | (1u << 26);
        nj_meshset *s = &nj_sets[k];
        s->type_mat = 0;                                // (0 << 14) triangles, material 0
        s->n = (unsigned short)p->ntris;
        s->meshes = p->tris;
        s->uv = &p->uv[0][0];
        nj_model *md = &nj_models[k];
        md->points = p->points; md->normals = p->normals; md->npoints = p->npoints;
        md->sets = s; md->mats = m; md->nsets = 1; md->nmats = 1;
        md->center[0] = p->center[0]; md->center[1] = p->center[1]; md->center[2] = p->center[2];
        md->r = p->radius;
    }
}

// Current matrix = m * current (m first, as njTranslate does: sonic.exe
// 0x784BE0 adds x*row0 + y*row1 + z*row2 to row 3).  Ninja matrices keep
// the axes in rows 0 to 2 and the position in row 3, the same layout as
// EngineDll's m4x4 (GetWheelTM).
static void nj_apply(const float m[16])
{
    float *c = nj_current_matrix, o[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            o[i * 4 + j] = m[i * 4] * c[j] + m[i * 4 + 1] * c[4 + j] + m[i * 4 + 2] * c[8 + j] + m[i * 4 + 3] * c[12 + j];
    for (int k = 0; k < 16; k++) c[k] = o[k];
}

// Textures: SADX frees or reuses texture slots when the level or act
// changes, so check every frame that each slot still holds our texture
// (NJS_TEXMEMLIST, ninja.h: its first field is the texture's global
// number, TRUCK_GBIX0 and up, from skin_to_c.py's index.txt) and load
// again when not.  At most one try per level and act.
static void check_textures(void)
{
    int ok = 1;
    for (int i = 0; i < TRUCK_TEXTURES; i++) {
        const u32 *slot = (const u32 *)truck_texnames[i].texaddr;
        if (!slot || slot[0] != TRUCK_GBIX0 + (u32)i) ok = 0;
    }
    int where = level_now * 256 + act_now;
    // Good slots end the "tried" mark, so a later loss in the same level and
    // act (back from the character select) loads again.
    if (ok) truck_textures_tried = 0;
    if (!ok && truck_textures_tried != where + 1) {
        truck_textures_tried = where + 1;
        for (int i = 0; i < TRUCK_TEXTURES; i++) truck_texnames[i].texaddr = 0;
        LoadPVM("SADXTRUCK", &truck_texlist);
        PrintDebug("[SADX Truck] textures SADXTRUCK (level %d act %d): %s\n", level_now, act_now,
                   truck_texnames[0].texaddr ? "loaded" : "NOT loaded, the truck is drawn without them");
    }
}

// The picked truck's picture (256 x 128, NJS_SPRITE and NJS_TEXANIM in
// ninja.h): centered, its middle a quarter of the way down the screen.
static void draw_picture(void)
{
    static struct { short sx, sy, cx, cy, u1, v1, u2, v2, texid, attr; } anim;
    static struct { float p[3], sx, sy; int ang; void *tlist, *tanim; } sprite;
    anim.sx = 256; anim.sy = 128; anim.cx = 128; anim.cy = 64;
    anim.u1 = 0; anim.v1 = 0; anim.u2 = 255; anim.v2 = 255;
    anim.texid = (short)(TRUCK_TEX_PICTURE + picked); anim.attr = 0;
    sprite.p[0] = screen_w * 0.5f; sprite.p[1] = screen_h * 0.25f; sprite.p[2] = 0.0f;
    sprite.sx = sprite.sy = stretch_2d;
    sprite.ang = 0;
    sprite.tlist = &truck_texlist; sprite.tanim = &anim;
    // NJD_SPRITE_ALPHA (1 << 5), NJD_SPRITE_SCALE (1 << 4).  Priority:
    // sonic.exe 0x425910 adds 22048 to a priority from -3.0 to 12048
    // (constants at 0x7DF190, 0x7DCB68, 0x7DF18C), which puts the picture in
    // front with the other on-screen pictures; below -3.0 it stays where it
    // is, in the world (-500 drew it far away, fogged, behind buildings).
    // The queued sprite keeps the material color and blend settings that
    // are current when it is queued (sonic.exe 0x403F60 copies the color at
    // 0x3AB9864 and the blend settings at 0x3D0F9C8, 0x3D0F9C4), and other
    // objects leave those set to anything: pale, see-through or added
    // light.  Set them the way SADX's own on-screen pictures do, then put
    // the color back.
    SetMaterialAndSpriteColor_Float(1.0f, 1.0f, 1.0f, 1.0f);   // a, r, g, b
    njColorBlendingMode(0, 8);           // source: NJD_COLOR_BLENDING_SRCALPHA
    njColorBlendingMode(1, 6);           // destination: NJD_COLOR_BLENDING_INVSRCALPHA
    njDrawSprite2D_ForcePriority(&sprite, 0, -1.5f, (1u << 5) | (1u << 4));
    ResetMaterial();
}

// Lamp glows (Cabela's coronas).  CARS.LUA SetCoronas, sizes in meters
// (width x height) and colors 0 to 255:
//   headlights  0.75 x 0.55  "whitecoronas"  250
//   brake       0.32 x 0.50  "redcoronas"    180
//   tail        0.11 x 0.17  "redcoronas"    110
//   reverse     0.31 x 0.20  "whitecoronas"  190
// (the Stalker, Car12, changes none of these).  One glow per lamp kind
// and side: the middle of that lamp's pieces on the body, 4 inches
// (0.1 m) further out to the front or back so the lamp glass does not hide
// it.  Lit when its lamp piece is drawn (piece_shown: the same switch and
// damage rule).  Added light: source alpha, destination one.
static const struct { float w, h, c; int tex; } glow_kind[5] = {
    { 0, 0, 0, 0 },
    { 0.75f, 0.55f, 250.0f / 255.0f, TRUCK_TEX_GLOW_WHITE },
    { 0.11f, 0.17f, 110.0f / 255.0f, TRUCK_TEX_GLOW_RED },
    { 0.32f, 0.50f, 180.0f / 255.0f, TRUCK_TEX_GLOW_RED },
    { 0.31f, 0.20f, 190.0f / 255.0f, TRUCK_TEX_GLOW_WHITE },
};
#define MAX_GLOWS 8
#define GLOW_SCALE 5.0f
static struct { float p[3]; int kind, n, draw[8]; } glows[MAX_GLOWS];
static int glow_count, glow_truck = -1;
static void glows_find(void)
{
    float sum[MAX_GLOWS][4] = { { 0 } };
    glow_count = 0;
    glow_truck = truck_now;
    for (int k = 0; k < car.draws; k++) {
        const unsigned short *d = truck_draws[car.first_draw + k];
        if (d[1] != 0 || d[3] == 0 || d[3] > 4) continue;
        const struct truck_model *m = &truck_models[d[0]];
        float c[3] = { 0, 0, 0 };
        for (int i = 0; i < m->npoints; i++) { c[0] += m->points[i][0]; c[1] += m->points[i][1]; c[2] += m->points[i][2]; }
        if (m->npoints) { c[0] /= m->npoints; c[1] /= m->npoints; c[2] /= m->npoints; }
        int side = c[0] < 0, g;
        for (g = 0; g < glow_count; g++)
            if (glows[g].kind == d[3] && (sum[g][3] < 0) == side) break;
        if (g == glow_count) {
            if (glow_count == MAX_GLOWS) continue;
            glows[g].kind = d[3]; glows[g].n = 0;
            sum[g][0] = sum[g][1] = sum[g][2] = 0; sum[g][3] = side ? -1.0f : 1.0f;
            glow_count++;
        }
        float w = (float)m->npoints;
        sum[g][0] += c[0] * w; sum[g][1] += c[1] * w; sum[g][2] += c[2] * w;
        sum[g][3] += (sum[g][3] < 0 ? -w : w);
        if (glows[g].n < 8) glows[g].draw[glows[g].n++] = k;
    }
    for (int g = 0; g < glow_count; g++) {
        float w = sum[g][3] < 0 ? -sum[g][3] - 1.0f : sum[g][3] - 1.0f;
        if (w <= 0) w = 1;
        glows[g].p[0] = sum[g][0] / w; glows[g].p[1] = sum[g][1] / w; glows[g].p[2] = sum[g][2] / w;
        glows[g].p[2] += glows[g].p[2] > 0 ? UNITS_PER_METER * 0.1f : -UNITS_PER_METER * 0.1f;
    }
    PrintDebug("[SADX Truck] %s: %d lamp glows\n", car.name, glow_count);
    for (int g = 0; g < glow_count; g++)
        PrintDebug("[SADX Truck]   glow %d: %s, %s, at %.2f %.2f %.2f m\n", g,
                   glows[g].kind == 1 ? "headlight" : glows[g].kind == 2 ? "tail" : glows[g].kind == 3 ? "brake" : "reverse",
                   glows[g].p[0] < 0 ? "x below 0" : "x above 0",
                   (double)(glows[g].p[0] / UNITS_PER_METER), (double)(glows[g].p[1] / UNITS_PER_METER), (double)(glows[g].p[2] / UNITS_PER_METER));
}
static int glows_drawn;                  // count, for the test host
static void draw_glows(const float body[16])
{
    static struct { short sx, sy, cx, cy, u1, v1, u2, v2, texid, attr; } anim[MAX_GLOWS];
    static struct { float p[3], sx, sy; int ang; void *tlist, *tanim; } sprite[MAX_GLOWS];
    if (glow_truck != truck_now) glows_find();
    int any = 0;
    for (int g = 0; g < glow_count; g++) {
        int lit = 0;
        for (int i = 0; i < glows[g].n && !lit; i++) {
            const unsigned short *d = truck_draws[car.first_draw + glows[g].draw[i]];
            lit = piece_shown(d[1], d[0], d[2], d[3]);
        }
        if (!lit) continue;
        if (!any) {
            njPushMatrixEx();
            nj_apply(body);
            njColorBlendingMode(0, 8);           // source: NJD_COLOR_BLENDING_SRCALPHA
            njColorBlendingMode(1, 10);          // destination: NJD_COLOR_BLENDING_ONE (added)
            any = 1;
        }
        const int k = glows[g].kind;
        // Fade: f = how straight the lamp points at the camera (1 = right
        // at it, 0 = side on).  Front lamps point along the body's
        // forward axis (row 2), back lamps the other way.  Brightness x f,
        // size x (0.5 + 0.5 f); side on or behind, no glow.
        float f = 1.0f;
        if (camera_twp) {
            const float *cam = (const float *)(camera_twp + 32), *p = glows[g].p;
            float w[3], v[3];
            for (int i = 0; i < 3; i++) {
                w[i] = p[0] * body[i] + p[1] * body[4 + i] + p[2] * body[8 + i] + body[12 + i];
                v[i] = cam[i] - w[i];
            }
            float len = sqrt_f(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            float dir = p[2] > 0 ? 1.0f : -1.0f;
            f = len > 0.01f ? dir * (v[0] * body[8] + v[1] * body[9] + v[2] * body[10]) / len : 1.0f;
        }
        if (f <= 0.0f) continue;
        if (f > 1.0f) f = 1.0f;
        // Texture drawn 10 units (1 m) wide, scaled to the Cabela's size.
        anim[g].sx = 10; anim[g].sy = 10; anim[g].cx = 5; anim[g].cy = 5;
        anim[g].u1 = 0; anim[g].v1 = 0; anim[g].u2 = 255; anim[g].v2 = 255;
        anim[g].texid = (short)glow_kind[k].tex; anim[g].attr = 0;
        sprite[g].p[0] = glows[g].p[0]; sprite[g].p[1] = glows[g].p[1]; sprite[g].p[2] = glows[g].p[2];
        // GLOW_SCALE times Cabela's size: the visible core of the texture
        // is about 2/5 of its width, so Cabela's sizes as they are showed
        // nothing (10 times washed the headlights over the hood).
        sprite[g].sx = glow_kind[k].w * GLOW_SCALE * (0.5f + 0.5f * f);
        sprite[g].sy = glow_kind[k].h * GLOW_SCALE * (0.5f + 0.5f * f);
        sprite[g].ang = 0;
        sprite[g].tlist = &truck_texlist; sprite[g].tanim = &anim[g];
        float c = glow_kind[k].c * f;
        SetMaterialAndSpriteColor_Float(1.0f, c, c, c);
        // NJD_SPRITE_ALPHA (1 << 5), NJD_SPRITE_SCALE (1 << 4), NJD_SPRITE_COLOR (1 << 1)
        njDrawSprite3D_Late(&sprite[g], 0, (1u << 5) | (1u << 4) | (1u << 1));
        glows_drawn++;
    }
    if (any) {
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 6);           // back to NJD_COLOR_BLENDING_INVSRCALPHA
        ResetMaterial();
        njPopMatrixEx();
    }
}

// SADX frees the task when the level ends; rings_freed then clears the
// pointer so the next spawn makes a new one.  (The task first drew SADX
// rings at the wheels, hence the names.)
static char *ring_task;
static void __cdecl rings_freed(char *tp) { (void)tp; ring_task = 0; }
// Shadow (Cabela's draws a box shadow under its trucks, cBspTree::BoxShadow
// in EngineDll; the mod's own version): a 5 x 9 grid over the truck's
// footprint (its solid box, x and z), each point dropped straight down
// onto the ground triangles the truck already collects (so it follows
// slopes, and works where SADX's own shadow does not, like the bridge to
// the Master Emerald), 1 inch above them.  Textured with the pack's
// shadow.png (TRUCK_TEX_SHADOW: black, alpha 160 in the middle, soft
// edges), drawn like the windows: SADX's see-through (late) queue, the
// texture's alpha.  (A first try with no texture and only the colour's
// alpha drew nothing.)  Squares with a point over no ground are left out.
// Fades out as the truck rises: full up to 3 feet, gone at 13 feet.
static float solid_lo[3], solid_hi[3];   // the solid box (defined with the parked truck's code)
static int solid_truck;
#define MAX_POSTS 128
static float posts[MAX_POSTS][8];       // hit-shape posts (collect_posts): form, x y z (SADX units), a b c, turn (radians)
static u32 post_exec[MAX_POSTS];         // each post's object code (its task's exec), for the log
static int post_count;
#define SH_NX 5
#define SH_NZ 9
#define SH_PTS (SH_NX * SH_NZ)
#define SH_IDX ((SH_NX - 1) * (SH_NZ - 1) * 6)
static float sh_pts[SH_PTS][3], sh_nrm[SH_PTS][3];
static short sh_idx[SH_IDX], sh_uv[SH_IDX][2];
static nj_material sh_mat;
static nj_meshset sh_set;
static nj_model sh_model;
static void draw_shadow(void)
{
    if (!truck || solid_truck != truck_now) return;
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    tVect3 st = { pos[0], pos[1] + 1.0f, pos[2] }, dn = { 0, -1, 0 };
    float t, n[3];
    if (!ray_tris(&st, &dn, 6.0f, &t, n)) return;
    float fade = 1.0f - (t - 1.0f - 1.0f) / 3.0f;
    if (fade <= 0.0f) return;
    if (fade > 1.0f) fade = 1.0f;
    float cx = (solid_lo[0] + solid_hi[0]) * 0.5f, cz = (solid_lo[2] + solid_hi[2]) * 0.5f;
    float hx = (solid_hi[0] - solid_lo[0]) * 0.55f, hz = (solid_hi[2] - solid_lo[2]) * 0.55f;   // a little past the body
    int ok[SH_PTS];
    for (int iz = 0; iz < SH_NZ; iz++)
        for (int ix = 0; ix < SH_NX; ix++) {
            int p = iz * SH_NX + ix;
            float lx = (cx - hx + 2.0f * hx * ix / (SH_NX - 1)) / UNITS_PER_METER;
            float lz = (cz - hz + 2.0f * hz * iz / (SH_NZ - 1)) / UNITS_PER_METER;
            float wx = pos[0] + lx * rot[0] + lz * rot[6];
            float wy = pos[1] + lx * rot[1] + lz * rot[7];
            float wz = pos[2] + lx * rot[2] + lz * rot[8];
            tVect3 s2 = { wx, wy + 1.5f, wz };
            float tp, np[3];
            ok[p] = ray_tris(&s2, &dn, 8.0f, &tp, np);
            float gy = ok[p] ? s2.y - tp + 0.025f : wy;
            sh_pts[p][0] = origin.x + wx * UNITS_PER_METER;
            sh_pts[p][1] = origin.y + gy * UNITS_PER_METER;
            sh_pts[p][2] = origin.z + wz * UNITS_PER_METER;
            sh_nrm[p][0] = 0; sh_nrm[p][1] = 1; sh_nrm[p][2] = 0;
        }
    int ni = 0;
    for (int iz = 0; iz + 1 < SH_NZ; iz++)
        for (int ix = 0; ix + 1 < SH_NX; ix++) {
            int a = iz * SH_NX + ix, b = a + 1, c = a + SH_NX, d = c + 1;
            if (!ok[a] || !ok[b] || !ok[c] || !ok[d]) continue;
            const int q[6] = { a, c, b, b, c, d };
            for (int j = 0; j < 6; j++) {
                sh_idx[ni] = (short)q[j];
                sh_uv[ni][0] = (short)(255 * (q[j] % SH_NX) / (SH_NX - 1));      // u across (texture's 64)
                sh_uv[ni][1] = (short)(255 * (q[j] / SH_NX) / (SH_NZ - 1));      // v along (texture's 128)
                ni++;
            }
        }
    if (!ni) return;
    sh_mat.diffuse = ((u32)(255.0f * fade) << 24) | 0xFFFFFFu;
    sh_mat.specular = 0; sh_mat.exponent = 0;
    sh_mat.tex = TRUCK_TEX_SHADOW;
    // The windows' flags plus (1 << 25), no lighting.
    sh_mat.flags = (1u << 21) | (1u << 23) | (1u << 25) | (1u << 20) | (1u << 31) | (1u << 28) | (1u << 26);
    sh_set.type_mat = 0; sh_set.n = (unsigned short)(ni / 3); sh_set.meshes = sh_idx;
    sh_set.attrs = 0; sh_set.normals = 0; sh_set.colors = 0; sh_set.uv = &sh_uv[0][0]; sh_set.buffer = 0;
    sh_model.points = sh_pts; sh_model.normals = sh_nrm; sh_model.npoints = SH_PTS;
    sh_model.sets = &sh_set; sh_model.mats = &sh_mat; sh_model.nsets = 1; sh_model.nmats = 1;
    sh_model.center[0] = origin.x + pos[0] * UNITS_PER_METER;
    sh_model.center[1] = origin.y + pos[1] * UNITS_PER_METER;
    sh_model.center[2] = origin.z + pos[2] * UNITS_PER_METER;
    sh_model.r = 60.0f; sh_model.buffer = 0;
    late_DrawModel(&sh_model, 0);                   // the view matrix as it is: points are in world units
}
// Keep the truck out of the posts (collect_posts).  The truck's
// footprint (the solid box, x and z, truck space) against each post's:
// a circle (sphere, cylinder) or a turned rectangle (box), where their
// heights overlap (from 1 foot above the truck's bottom).  The deepest
// overlap is fixed with speed, not by moving the truck (moving it each
// frame made the wheels jump): the speed toward the post is taken away and
// up to 2 m/s (about 4 1/2 mph) away from it is added.  Of the versions
// tried, this one stopped the truck at most posts in play.  The palms in
// Station Square's grass islands have no collision in SADX at all: Sonic
// walks through them too.
// The footprint was only measured when a truck was parked, so a newly
// picked truck drove through posts (and had no shadow) until Sonic got out
// once; it is measured here, the first frame the new truck drives.
static void solid_box(void);
static void push_out_of_posts(void)
{
    if (truck && solid_truck != truck_now) solid_box();
    if (!truck || solid_truck != truck_now || !post_count) return;
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    const float *R = rot, *U = rot + 3, *F = rot + 6;
    float lo[3], hi[3];
    for (int j = 0; j < 3; j++) { lo[j] = solid_lo[j] / UNITS_PER_METER; hi[j] = solid_hi[j] / UNITS_PER_METER; }
    float tcx = (lo[0] + hi[0]) * 0.5f, tcz = (lo[2] + hi[2]) * 0.5f, thx = (hi[0] - lo[0]) * 0.5f, thz = (hi[2] - lo[2]) * 0.5f;
    float best = 0, bx = 0, bz = 0;                  // overlap depth and push direction (truck x, z)
    int best_k = -1;
    for (int k = 0; k < post_count; k++) {
        const float *P = posts[k];
        float r[3] = { (P[1] - origin.x) / UNITS_PER_METER - pos[0], (P[2] - origin.y) / UNITS_PER_METER - pos[1],
                       (P[3] - origin.z) / UNITS_PER_METER - pos[2] };
        float px = r[0] * R[0] + r[1] * R[1] + r[2] * R[2];
        float py = r[0] * U[0] + r[1] * U[1] + r[2] * U[2];
        float pz = r[0] * F[0] + r[1] * F[1] + r[2] * F[2];
        float hh = P[5] / UNITS_PER_METER;
        if (py + hh < lo[1] + 0.3f || py - hh > hi[1]) continue;      // above or below the body
        float depth, ux, uz;
        if (P[0] != 3.0f) {                          // circle
            float rad = P[4] / UNITS_PER_METER;
            float cx = px < lo[0] ? lo[0] : px > hi[0] ? hi[0] : px;
            float cz = pz < lo[2] ? lo[2] : pz > hi[2] ? hi[2] : pz;
            float dx = cx - px, dz = cz - pz, dd = sqrt_f(dx * dx + dz * dz);
            if (dd > 1e-4f) {
                if (dd >= rad) continue;
                depth = rad - dd; ux = dx / dd; uz = dz / dd;
            } else {
                float ex[4] = { px - lo[0], hi[0] - px, pz - lo[2], hi[2] - pz };
                int m = 0;
                for (int j = 1; j < 4; j++) if (ex[j] < ex[m]) m = j;
                depth = ex[m] + rad;
                ux = m == 0 ? 1.0f : m == 1 ? -1.0f : 0.0f;
                uz = m == 2 ? 1.0f : m == 3 ? -1.0f : 0.0f;
            }
        } else {                                     // turned rectangle: separating axes
            // Box axes in the world (heading only), then in truck x, z.
            float ca = cos_f(P[7]), sa = sin_f(P[7]);
            float wa[2][3] = { { ca, 0, -sa }, { sa, 0, ca } };
            float bax[2][2];
            for (int q = 0; q < 2; q++) {
                bax[q][0] = wa[q][0] * R[0] + wa[q][1] * R[1] + wa[q][2] * R[2];
                bax[q][1] = wa[q][0] * F[0] + wa[q][1] * F[1] + wa[q][2] * F[2];
                float l = sqrt_f(bax[q][0] * bax[q][0] + bax[q][1] * bax[q][1]);
                if (l > 1e-4f) { bax[q][0] /= l; bax[q][1] /= l; }
            }
            float bh[2] = { P[4] / UNITS_PER_METER, P[6] / UNITS_PER_METER };
            float axes[4][2] = { { 1, 0 }, { 0, 1 }, { bax[0][0], bax[0][1] }, { bax[1][0], bax[1][1] } };
            float dxc = px - tcx, dzc = pz - tcz;    // box center from the footprint's center
            depth = 1e9f; ux = uz = 0;
            int sep = 0;
            for (int q = 0; q < 4 && !sep; q++) {
                float ax = axes[q][0], az = axes[q][1];
                float rt = thx * (ax < 0 ? -ax : ax) + thz * (az < 0 ? -az : az);
                float d0 = bax[0][0] * ax + bax[0][1] * az, d1 = bax[1][0] * ax + bax[1][1] * az;
                float rb = bh[0] * (d0 < 0 ? -d0 : d0) + bh[1] * (d1 < 0 ? -d1 : d1);
                float dist = dxc * ax + dzc * az;
                float ov = rt + rb - (dist < 0 ? -dist : dist);
                if (ov <= 0) sep = 1;
                else if (ov < depth) { depth = ov; ux = dist > 0 ? -ax : ax; uz = dist > 0 ? -az : az; }
            }
            if (sep) continue;
        }
        if (depth > best) { best = depth; bx = ux; bz = uz; best_k = k; }
    }
    if (best <= 0.0f) return;
    float pd[3] = { R[0] * bx + F[0] * bz, R[1] * bx + F[1] * bz, R[2] * bx + F[2] * bz };
    const float *vel = (const float *)(body + 0x34);
    float along = vel[0] * pd[0] + vel[1] * pd[1] + vel[2] * pd[2];   // + = moving away already
    float want = best * 10.0f;                       // undo the overlap in about 0.1 s
    if (want > 2.0f) want = 2.0f;
    if (along < want) {
        float add = want - along;
        SetLinearSpeed(truck, vel[0] + pd[0] * add, vel[1] + pd[1] * add, vel[2] + pd[2] * add);
        ActivateObject(truck);
    }
    static int quiet, last_k = -1;
    if (quiet > 0) quiet--;
    if (!quiet || best_k != last_k) {
        const float *P = posts[best_k];
        PrintDebug("[SADX Truck] post %s (object code %08X) at SADX %.0f %.0f %.0f, size %.1f %.1f %.1f: %.1f inches into it, %.0f mph into it taken away\n",
                   P[0] == 3.0f ? "box" : P[0] == 1.0f ? "cylinder" : "sphere", post_exec[best_k], (double)P[1], (double)P[2], (double)P[3],
                   (double)P[4], (double)P[5], (double)P[6], (double)(best * 39.37f), (double)((along < 0 ? -along : 0) * 2.237f));
        quiet = 60; last_k = best_k;
    }
}
static void __cdecl rings_draw(char *tp)
{
    m4x4 tm[5];
    (void)tp;
    if (map_menu) map_menu_picture();
    if (!frame && !picture_frames) return;
    check_textures();
    if (picture_frames) {
        picture_frames--;
        draw_picture();
    }
    if (!frame) return;
    if (riding) { draw_hud(); draw_gauges(); }
    // Frame 0: the body (rotation rows at body byte 76, position at byte
    // 40, cPhysicsObject::SetOrientation 0x100BF260 and SetPosition
    // 0x100BED40; GetWheelTM 0x100E3CF0 builds a wheel's matrix the same
    // way).  Frames 1 to 4: the wheels.
    {
        const float *body = *(const float *const *)((const char *)truck + 4);
        const float *rot = (const float *)((const char *)body + 0x4c), *pos = (const float *)((const char *)body + 0x28);
        for (int i = 0; i < 3; i++) {
            tm[0].m[i * 4] = rot[i * 3]; tm[0].m[i * 4 + 1] = rot[i * 3 + 1]; tm[0].m[i * 4 + 2] = rot[i * 3 + 2];
            tm[0].m[i * 4 + 3] = 0.0f;
            tm[0].m[12 + i] = pos[i];
        }
        tm[0].m[15] = 1.0f;
    }
    for (u32 w = 0; w < 4; w++)
        GetWheelTM(truck, &tm[1 + w], w);
    // Meters to SADX units: position only, the model is already in units.
    for (int f = 0; f < 5; f++) {
        tm[f].m[12] = origin.x + tm[f].m[12] * UNITS_PER_METER;
        tm[f].m[13] = origin.y + tm[f].m[13] * UNITS_PER_METER;
        tm[f].m[14] = origin.z + tm[f].m[14] * UNITS_PER_METER;
    }
    njSetTexture(&truck_texlist);
    falling_start(tm);                          // pieces that broke off this frame
    for (int k = 0; k < car.draws; k++) {
        const unsigned short *d = truck_draws[car.first_draw + k];
        if (!piece_shown(d[1], d[0], d[2], d[3])) continue;
        njPushMatrixEx();
        nj_apply(tm[d[1]].m);
        // Windows go in SADX's late queue (sorted, drawn after the solid
        // things, like Cabela's ZWriteDisable glass), so Sonic in his seat
        // still shows through them.
        if (truck_models[d[0]].tex >= TRUCK_TEX_GLASS) late_DrawModel(&nj_models[d[0]], 0);
        else njDrawModel_Dynamic(&nj_models[d[0]]);
        njPopMatrixEx();
    }
    if (snorkel[truck_now]) {
        int s = snorkel[truck_now] - 1;
        for (int k = 0; k < car.snork_draws[s]; k++) {
            const unsigned short *d = truck_draws[car.snork_first[s] + k];
            njPushMatrixEx();
            nj_apply(tm[d[1]].m);
            njDrawModel_Dynamic(&nj_models[d[0]]);
            njPopMatrixEx();
        }
    }
    falling_draw();
    draw_shadow();
    draw_glows(tm[0].m);
}
// SADX's task loop (sonic.exe 0x40B0E0) only calls exec; the display
// function is called by itself only from 0x40B4F0 (paused), so exec draws.
static void __cdecl rings_exec(char *tp) { rings_draw(tp); }

static void make_rings(void)
{
    if (ring_task) return;
    ring_task = CreateElementalTask(0, 2, rings_exec);
    if (!ring_task) return;
    *(void **)(ring_task + 20) = (void *)rings_draw;
    *(void **)(ring_task + 24) = (void *)rings_freed;
}

// Engine sound and starter state (the Engine section further down).
static char snd_dir[320];               // the mod folder + "\sounds\"
enum { ENG_OFF, ENG_CRANK_IN, ENG_CRANK_LOOP, ENG_CATCH, ENG_RUNNING, ENG_STOPPING };
static int eng;                          // ENG_ state
static int eng_level = -1;               // engine damage level; -1 = apply next frame
static float sputter_cut;                // > 0: a damaged engine cutting out
static int reversing;                    // 1 = in reverse
static int headlights, braking;          // lamps: headlights on, brake pedal down
static char break_pending[18];           // parts that turned black: start falling
static int falling_n;                    // pieces lying around (Engine section)
static int trans_broken;                 // transmission level 4: no drive
static int have_prev;                    // hit detection has last frame's velocity
static float air_t;                      // seconds with no wheel on the ground
static float radiator_gauge;             // 0 cold to 1
static int part_lvl_shown[26];           // part level last reported (-1 = not yet)
static void engine_reset(void);

// Put the truck on Sonic: truck point (0, 0, 0) = Sonic's position, the
// truck 2 meters (about 6 1/2 feet) above the ground below him, still.
static int spawn(void)
{
    const float *pos = (const float *)(player_task + 32);   // taskwk pos
    origin.x = pos[0]; origin.y = pos[1]; origin.z = pos[2];
    // Search from 1 m above him: standing, his feet are on the surface and a
    // search starting there can miss it.
    float g = ground_m(0.0f, 1.0f, 0.0f);
    if (g == NO_GROUND) {
        // GetShadowPos misses some floors Sonic stands on (the bridge to the
        // Master Emerald: no shadow there either); the truck's own
        // triangles, 1 m above him down to 30 m below, find them.
        static const float zero3[3] = { 0, 0, 0 };
        gather(zero3);
        tVect3 st = { 0.0f, 1.0f, 0.0f }, dn = { 0.0f, -1.0f, 0.0f };
        float t, n[3];
        if (!ray_tris(&st, &dn, 31.0f, &t, n)) return 0;
        g = 1.0f - t;
        PrintDebug("[SADX Truck] no shadow floor under Sonic; the truck's own triangles found one %.1f ft below him\n", (double)(-g * 3.281f));
    }
    engine_reset();                     // the old truck's sounds stop
    // Same truck again (Y far away): its dents and cracks stay, like its
    // part damage.  Another truck: the old one's models go back to their
    // start shape (trucks share some models, so they can't keep both).
    if (picked != truck_now) { dent_save(); damage_file_save(truck_now); dent_restore(); }
    truck_now = picked;
    if (!build_truck()) {
        PrintDebug("[SADX Truck] truck not built\n");
        return 0;
    }
    PrintDebug("[SADX Truck] truck: %s (Car%d)\n", car.name, truck_now + 1);
    if (dent_truck != truck_now) dent_load();
    // Face the way Sonic faces.  SADX angle: 65536 = one turn, 0 faces +x,
    // and SADX draws players with njRotateY(-ang.y), so his forward is
    // (cos, 0, sin).  The truck's forward is its +z (wheels 0 and 1).
    // Rows: truck x, y, z axes in the world (my guess at EngineDll's
    // layout; the "heading" line below checks it).
    float a = *(const int *)(player_task + 24) * (6.2831853f / 65536.0f);
    float fx = cos_f(a), fz = sin_f(a);
    float rot[9] = { fz, 0, -fx,   0, 1, 0,   fx, 0, fz };
    SetOrientation(truck, rot);
    spawn_angle = *(const int *)(player_task + 24);
    tVect3 start = { 0.0f, g + 2.0f, 0.0f };
    SetPosition(truck, &start);
    SetLinearSpeed(truck, 0, 0, 0);
    SetAngularSpeed(truck, 0, 0, 0);
    landed = logged = lost_ground = 0;
    PrintDebug("[SADX Truck] spawned at Sonic (%.1f, %.1f, %.1f), ground %.2f m below him\n",
               (double)origin.x, (double)origin.y, (double)origin.z, (double)-g);
    // Floor check: level triangles against GetShadowPos at 5 points, a
    // straight-down ray from 1 m above Sonic.  The two should agree.
    {
        static const float at[5][2] = { { 0, 0 }, { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 } };
        const float zero[3] = { 0, 0, 0 };
        gather(zero);
        PrintDebug("[SADX Truck] level: %d solid pieces near, %d of them rotated, %d triangles%s\n",
                   pieces_near, pieces_rotated, tri_count, tri_dropped ? " (list full, some left out)" : "");
        for (int k = 0; k < 5; k++) {
            tVect3 s = { at[k][0], 1.0f, at[k][1] }, d = { 0, -1, 0 };
            float t, n[3], sh = ground_m(s.x, s.y, s.z);
            int hit = ray_tris(&s, &d, 6.0f, &t, n);
            PrintDebug("[SADX Truck]   floor check %+.0f %+.0f m: triangles %s%+.2f, GetShadowPos %+.2f\n",
                       (double)at[k][0], (double)at[k][1], hit ? "" : "none ", (double)(hit ? 1.0f - t : 0.0f),
                       (double)(sh == NO_GROUND ? -99.0f : sh));
        }
    }
    return 1;
}

#ifndef TEST_EXE
static void map_init(const char *path, u32 n, const void *helpers);
#endif
__declspec(dllexport) void __cdecl Init(const char *path, const void *helpers)
{
    char name[400], full[400];
    u32 n = 0;
    const char *file = "\\EngineDll.dll";

    while (path[n] && n < 300) { name[n] = path[n]; n++; }
    for (u32 k = 0; k < n; k++) snd_dir[k] = path[k];
    {
        const char *s = "\\sounds\\";
        u32 k = n;
        while (*s) snd_dir[k++] = *s++;
        snd_dir[k] = 0;
    }
    {
        // Mod Manager settings (configschema.xml -> config.ini).
        char ini[400];
        const char *c = "\\config.ini";
        u32 k = n;
        for (u32 i = 0; i < n; i++) ini[i] = path[i];
        while (*c) ini[k++] = *c++;
        ini[k] = 0;
        void *kl = LoadLibraryExA("kernel32.dll", 0, 0);
        u32 (__stdcall *get_int)(const char *, const char *, int, const char *) =
            kl ? (void *)GetProcAddress(kl, "GetPrivateProfileIntA") : 0;
        u32 (__stdcall *get_str)(const char *, const char *, const char *, char *, u32, const char *) =
            kl ? (void *)GetProcAddress(kl, "GetPrivateProfileStringA") : 0;
        int ds = 1;
        if (get_int && get_str) {
            // The Mod Manager writes on/off as True or False.
            char b[8];
            get_str("Damage", "KeepDents", "True", b, sizeof b, ini);
            keep_dents = !(b[0] == 'F' || b[0] == 'f' || b[0] == '0');
            ds = (int)get_int("Damage", "DentStrength", 1, ini);
            get_str("Damage", "SaveDamage", "True", b, sizeof b, ini);
            save_damage = !(b[0] == 'F' || b[0] == 'f' || b[0] == '0');
            land_strength = (int)get_int("Damage", "LandingStrength", 1, ini);
            truck_volume = (int)get_int("Sound", "TruckVolume", 100, ini);
            get_str("Driving", "UseFuel", "True", b, sizeof b, ini);
            use_fuel = !(b[0] == 'F' || b[0] == 'f' || b[0] == '0');
        }
        if (truck_volume < 0) truck_volume = 0;
        if (truck_volume > 100) truck_volume = 100;
        if (land_strength < 1) land_strength = 1;
        if (land_strength > 50) land_strength = 50;
        if (ds < 1) ds = 1;
        if (ds > 10) ds = 10;
        dent_strength = (float)ds;
        PrintDebug("[SADX Truck] settings: KeepDents = %d, DentStrength = x%d, SaveDamage = %d, LandingStrength = x%d, UseFuel = %d\n", keep_dents, ds, save_damage, land_strength, use_fuel);
    }
    // No trucks.bin: the mod stays off (truck stays 0, so every hook
    // returns early) and SADX plays as normal.
    if (!trucks_bin_load(path, n)) return;
#ifndef TEST_EXE
    map_init(path, n, helpers);         // Cabela's maps, further down
#endif
    damage_files_init(path, n);
    sound_settings(path, n);
    free_cam_patch();
    people_patch();
    cart_pad_patch();
    missile_patch();
    while (*file) name[n++] = *file++;
    name[n] = 0;
    GetFullPathNameA(name, sizeof full, full, 0);

    PrintDebug("[SADX Truck] loading %s\n", full);
    // 8 = LOAD_WITH_ALTERED_SEARCH_PATH, so msvcp60.dll is found next to it
    dll_base = (u32)LoadLibraryExA(full, 0, 8);
    if (!dll_base) {
        PrintDebug("[SADX Truck] LoadLibrary failed, Windows error %u\n", GetLastError());
        return;
    }
    PrintDebug("[SADX Truck] EngineDll loaded at %u (0x%08X)%s\n", dll_base, dll_base,
               dll_base == 0x10000000u ? "" : ", moved from its usual 0x10000000");
    AddVectoredExceptionHandler(1, on_crash);

    if (!build_truck()) {
        truck = 0;
        PrintDebug("[SADX Truck] truck not built\n");
        return;
    }
    make_truck_models();
    PrintDebug("[SADX Truck] Stalker built, press Y in a level to drive\n");
}

static u32 held_before;                  // pad buttons last frame
static float last_center[3];             // truck center one second ago

// Truck center: the middle of the 4 wheels, in truck meters.
static void truck_center(float c[3])
{
    m4x4 tm;
    c[0] = c[1] = c[2] = 0;
    for (u32 w = 0; w < 4; w++) {
        GetWheelTM(truck, &tm, w);
        c[0] += tm.m[12] * 0.25f; c[1] += tm.m[13] * 0.25f; c[2] += tm.m[14] * 0.25f;
    }
}

// Pedals and steering from the raw pad.  Gas = right trigger, R or A;
// brake = left trigger, L or B (A and B are free: Sonic cannot jump while
// riding).  Stick X = steering (full stick = the Stalker's 30).
// Steering the way Cabela's does it (GameDll FUN_10028420, the function
// next to the debug name "cObjCarCora::GetSteer"), every frame:
//   v  = forward speed in m/s (car byte 128 = -(velocity . forward axis),
//        GameDll 0x1002a447; body velocity at byte 100, axis at byte 52)
//   truck byte 244 (max wheel angle, degrees)
//        = (2.3066 * e^(-4v / 12.88265) + 0.75666) / 3 * cars.lua steering
//   truck byte 248 (how far the wheels move toward the target each 1/100 s,
//        EngineDll 0x100e0fad) = (2.3066 * e^(-v / 12.88265) + 0.75666) * 0.01
// Stalker: 30.6 degrees standing, 13.3 at 10 mph, 9.5 at 18 mph, 7.7 at 40.
static float steer_now;                  // -1 to 1, the stick (for the log)
static float frndint_f(float v) { __asm__("frndint" : "+t"(v)); return v; }
static float f2xm1_f(float v) { __asm__("f2xm1" : "+t"(v)); return v; }
static float fscale_f(float v, float by) { __asm__("fscale" : "+t"(v) : "u"(by)); return v; }
static float exp_f(float x)              // e^x with x87: 2^(x * log2 e)
{
    float y = x * 1.4426950f, n = frndint_f(y);
    return fscale_f(f2xm1_f(y - n) + 1.0f, n);
}
static void cabelas_steering(void)
{
    char *t = (char *)truck, *body = *(char **)(t + 4);
    const float *axis = (const float *)(body + 0x34), *vel = (const float *)(body + 0x64);
    float v = axis[0] * vel[0] + axis[1] * vel[1] + axis[2] * vel[2];
    if (v < 0.0f) v = -v;
    *(float *)(t + 0xf4) = (2.3066f * exp_f(-4.0f * v / 12.88265f) + 0.75666f) * (1.0f / 3.0f) * car.steering;
    *(float *)(t + 0xf8) = (2.3066f * exp_f(-v / 12.88265f) + 0.75666f) * 0.01f;
}
// Forward speed in m/s, minus = backward: body velocity (byte 52) . body
// forward axis (byte 100).  GameDll 0x1002A447 stores minus that at car
// byte 128, but in the test host driving forward gives plus here (z
// growing).
static float forward_speed(void)
{
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *axis = (const float *)(body + 0x34), *vel = (const float *)(body + 0x64);
    return axis[0] * vel[0] + axis[1] * vel[1] + axis[2] * vel[2];
}

// Reverse, the usual automatic way: stopped (under 1 mph) with the brake
// held and no gas for 0.4 s -> reverse gear (EngineDll GearDown); then the
// brake pedal drives backward and gas brakes.  Stopped with gas held 0.4 s
// -> forward again (GearUp).
static float rev_t;
static void drive(const char *pad, float *gas, float *brake, float *steer)
{
    u32 held = *(const u32 *)(pad + 8);
    *gas = *(const unsigned short *)(pad + 24) / 255.0f;
    *brake = *(const unsigned short *)(pad + 26) / 255.0f;
    if (held & (BUTTON_R | BUTTON_A)) *gas = 1.0f;
    if (held & (BUTTON_L | BUTTON_B)) *brake = 1.0f;
    // Minus: EngineDll's +steer turns the other way from SADX's stick right
    // (seen in game).  The reason: Cabela's space is a mirror image of
    // SADX's (the licence plate read backwards); skin_to_c.py flips the
    // model, the physics keeps Cabela's sides.
    *steer = -*(const short *)(pad + 28) / 128.0f;
    float v = forward_speed();
    float want = reversing ? *gas : *brake, other = reversing ? *brake : *gas;
    if (eng == ENG_RUNNING && v > -0.45f && v < 0.45f && want > 0.5f && other < 0.1f) rev_t += 1.0f / 60.0f;
    else rev_t = 0;
    if (rev_t > 0.4f) {
        rev_t = 0;
        reversing = !reversing;
        // Gears: 1 and up forward, 0 neutral, below 0 reverse (seen in the
        // test host: one GearDown from 1 gives 0 and the engine revs free).
        for (int k = 0; k < 4 && (reversing ? GetCurGear(truck) >= 0 : GetCurGear(truck) < 1); k++) {
            if (reversing) GearDown(truck); else GearUp(truck);
        }
        PrintDebug("[SADX Truck] %s, gear %d\n", reversing ? "reverse (brake drives back, gas brakes)" : "forward", GetCurGear(truck));
    }
    float go = reversing ? *brake : *gas, stop = reversing ? *gas : *brake;
    braking = stop > 0.1f;
    // engine off or cutting out: no drive
    if (stop > go) PressBrake(truck, stop);
    else PressAcceleration(truck, eng == ENG_RUNNING && sputter_cut <= 0 && !trans_broken ? go : 0.0f);
    // Target = stick * the speed-limited maximum; EngineDll moves the
    // wheels toward it at the speed-limited rate (and clamps to byte 244).
    cabelas_steering();
    steer_now = *steer;
    SetSteerAngle(truck, *steer * *(float *)((char *)truck + 0xf4));
}

// A point in truck space (meters, the model's space) to SADX units, with
// the body's matrix (rotation rows at body byte 76, position at byte 40,
// the same matrix rings_draw draws the body with).
static void truck_point(const float p[3], float out[3])
{
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    for (int i = 0; i < 3; i++)
        out[i] = p[0] * rot[i] + p[1] * rot[3 + i] + p[2] * rot[6 + i] + pos[i];
    out[0] = origin.x + out[0] * UNITS_PER_METER;
    out[1] = origin.y + out[1] * UNITS_PER_METER;
    out[2] = origin.z + out[2] * UNITS_PER_METER;
}

// Truck point (meters, truck space) -> EngineDll world meters.
static void truck_m(const float p[3], float out[3])
{
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    for (int i = 0; i < 3; i++)
        out[i] = p[0] * rot[i] + p[1] * rot[3 + i] + p[2] * rot[6 + i] + pos[i];
}

// Getting out where there is room.  Spots in truck space:
//   0 driver's door: door_0 moved 2 feet (0.6 m) further out, at the
//     seat's front-back position
//   1 passenger door: the same on the other side
//   2 behind the truck: 6 feet (1.8 m) behind the rear axle
//   3 on top: 8 feet (2.5 m) straight up from the truck's middle (world
//     up), used when 0, 1 and 2 are all blocked
// A spot is blocked when a level triangle (the same ones the wheels touch)
// lies between the truck's middle line and the spot, plus 1 foot (0.3 m)
// for Sonic's body, at door height or 2.6 feet (0.8 m) above it.
static const char *const exit_names[4] = { "driver door", "passenger door", "behind the truck", "on top, every side blocked" };
static void exit_local(int k, float p[3], float from[3])
{
    float dx = car.door[0] + (car.door[0] < 0 ? -0.6f : 0.6f);
    p[1] = from[1] = car.door[1];
    if (k == 2) { p[0] = from[0] = 0; p[2] = car.tire[2].z - 1.8f; from[2] = car.tire[2].z; }
    else { p[0] = k == 0 ? dx : -dx; from[0] = 0; p[2] = from[2] = car.seat[2]; }
}
static int exit_clear(int k)
{
    float p[3], from[3];
    exit_local(k, p, from);
    for (int h = 0; h < 2; h++) {
        float a[3] = { from[0], from[1] + 0.8f * h, from[2] }, b[3] = { p[0], p[1] + 0.8f * h, p[2] }, wa[3], wb[3];
        truck_m(a, wa); truck_m(b, wb);
        float d[3] = { wb[0] - wa[0], wb[1] - wa[1], wb[2] - wa[2] };
        float len = sqrt_f(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len < 0.01f) continue;
        tVect3 s = { wa[0], wa[1], wa[2] }, dir = { d[0] / len, d[1] / len, d[2] / len };
        float t, n[3];
        if (ray_tris(&s, &dir, len + 0.3f, &t, n)) return 0;
    }
    return 1;
}
// First free spot in the order 0, 1, 2 (Tails: 1, 0, 2), skipping `taken`
// (Sonic's spot, for Tails); 3 when none is free.
static int exit_pick(int taken)
{
    static const int sonic_order[3] = { 0, 1, 2 }, tails_order[3] = { 1, 0, 2 };
    const int *order = taken < 0 ? sonic_order : tails_order;
    for (int i = 0; i < 3; i++)
        if (order[i] != taken && exit_clear(order[i])) return order[i];
    return 3;
}
static void exit_place(int k, char *task)
{
    float *out = (float *)(task + 32);
    if (k == 3) {
        float c[3];
        truck_center(c);
        out[0] = origin.x + c[0] * UNITS_PER_METER;
        out[1] = origin.y + (c[1] + 2.5f) * UNITS_PER_METER;
        out[2] = origin.z + c[2] * UNITS_PER_METER;
        return;
    }
    float p[3], from[3];
    exit_local(k, p, from);
    truck_point(p, out);
}

// Sonic on the driver's seat (the model's driver1 bone, where Cabela's
// puts its driver), turned and tilted like the truck, speed zeroed so SADX
// does not move him.
//
// Tilt: SonicDisplay (sonic.exe 0x494A0A) turns him by taskwk ang.z
// (byte 28), then ang.x (byte 20), then the heading (byte 24), each a
// right-handed turn (njRotateZ 0x784FB0, njRotateX 0x784EE0).  So his up
// direction is (-sin z cos x, cos z cos x, sin x); set it to the truck's
// up (body row 1).  The heading comes from the truck's forward (body
// row 2) with the tilt taken back off.
#define TO_ANGLE (65536.0f / 6.2831853f)          // radians to SADX angle
static void seat_player(char *task, char *motion, char *work, const float seat[3])
{
    truck_point(seat, (float *)(task + 32));            // taskwk pos

    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *u = (const float *)(body + 0x4c) + 3;  // truck up
    const float *f = (const float *)(body + 0x4c) + 6;  // truck forward
    float ax = atan2_f(u[2], sqrt_f(u[0] * u[0] + u[1] * u[1]));
    float az = atan2_f(-u[0], u[1]);
    // Undo the tilt on the forward direction: turn by -z, then by -x.
    float cz = cos_f(az), sz = sin_f(az), cx = cos_f(ax), sx = sin_f(ax);
    float x1 = f[0] * cz + f[1] * sz, y1 = -f[0] * sz + f[1] * cz, z1 = f[2];
    float z2 = -y1 * sx + z1 * cx;
    *(int *)(task + 20) = (int)(ax * TO_ANGLE);
    *(int *)(task + 28) = (int)(az * TO_ANGLE);
    // SADX heading: 65536 = one full turn, forward (cos, 0, sin), see spawn().
    *(int *)(task + 24) = (int)(atan2_f(z2, x1) * TO_ANGLE);

    float *spd = (float *)(motion + 4);
    spd[0] = spd[1] = spd[2] = 0;
    spd = (float *)(work + 56);
    spd[0] = spd[1] = spd[2] = 0;
}
// Passenger (player 2, Tails): no passenger bone in the models
// (only driver1), so the driver's seat mirrored across the truck's middle.
static int tails_riding;
// Better Tails AI (Sora's mod, SADX-Better-Tails-AI.dll; source on GitHub,
// abilities.cpp AI_SitInCart): while Sonic is in a cart mode (his 45, which
// the truck uses) it sets Tails's mode to 115 and puts him 4.5 units left,
// 4.5 up and 2.5 ahead of Sonic every frame; once Sonic leaves mode 45 it
// gives Tails PL_OP_GETOUTOFCART and mode 1 (ResetMilesAI).  Its task runs
// the frame after Sonic gets out, before Sonic's mode has changed, so it
// moves Tails from the passenger door to beside Sonic.  With that mod
// loaded only, the mod puts Tails back at his door for the next 2 frames.
// Without it (plain SADX Tails) nothing changes.
static int tails_exit_k = -1, tails_exit_frames;
static int btai_seat_ok;            // the mod set Tails's mode 115 this ride
static int btai_loaded(void)
{
    static int known = -1;
    if (known < 0) {
        void *k = LoadLibraryExA("kernel32.dll", 0, 0);
        void *(__stdcall *gmh)(const char *) = k ? (void *)GetProcAddress(k, "GetModuleHandleA") : 0;
        known = gmh && gmh("SADX-Better-Tails-AI.dll") ? 1 : 0;
        PrintDebug("[SADX Truck] Better Tails AI %s\n", known ? "loaded: Tails is put back at his door after getting out" : "not loaded");
    }
    return known;
}
__declspec(dllexport) int __cdecl TruckCharacterDriving(void)
{
    return riding;
}
__declspec(dllexport) int __cdecl TruckTailsRiding(void)
{
    return riding && tails_riding;
}
// Exports of truck state
static int hit_count, last_hit_mph, speed_mph_now;
__declspec(dllexport) int __cdecl TruckSpeedMph(void)    { return riding ? speed_mph_now : 0; }        // updated once a second
__declspec(dllexport) int __cdecl TruckAirTenths(void)   { return riding ? (int)(air_t * 10.0f) : 0; } // 15 = 1.5 seconds in the air
__declspec(dllexport) int __cdecl TruckFuelPercent(void) { return (int)(fuel[truck_now] * 100.0f); }
__declspec(dllexport) int __cdecl TruckHitCount(void)    { return hit_count; }                         // goes up by 1 on every counted hit
__declspec(dllexport) int __cdecl TruckLastHitMph(void)  { return last_hit_mph; }
#define RUN_OVER_MPS 6.7f       // 15 mph
static int run_over, run_check; // Sonic's shape 0 set for running enemies over
static unsigned char run_damage;
static float run_a, run_b;
static char *run_task;          // SADX task (level 6) that re-applies the bits
// Hit-shape posts: SADX objects like Station Square's street lamp ("O
// GAITOU", 0x63AA10) have no ground triangles, only a hit shape: CCL_Init
// (0x63AA3E) puts a cylinder (form 1, radius 1.5 units, half height 20.5)
// in list 4, objects, that pushes players (push byte 119).  The truck sees
// only triangles, so it drove through.  Each frame the run-over task (list
// 6: after the objects have entered their shapes, before SADX's collision
// pass, EntryColliList 0x41C280: list 4 at 0x3B23AA8, taskwk pointers,
// count 0x3B240F4) copies the solid upright ones: cylinders and spheres
// that push others (push byte (1 << 0) to (1 << 2)) and can't be pushed by
// players ((1 << 4)), up to 64.  OnFrame then keeps the truck out of them.
static void collect_posts(void)
{
    post_count = 0;
    // Only shapes near the truck: the level-wide list fills MAX_POSTS in
    // busy places (Station Square) before reaching the one being hit.
    float tx = 0, tz = 0;
    int have_truck = 0;
    if (truck) {
        const float *bp = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x28);
        tx = origin.x + bp[0] * UNITS_PER_METER; tz = origin.z + bp[2] * UNITS_PER_METER;
        have_truck = 1;
    }
    int n = *(const unsigned short *)0x3B240F4;
    for (int k = 0; k < n && k < 1024 && post_count < MAX_POSTS; k++) {
        const char *twp = ((const char *const *)0x3B23AA8)[k];
        if (!twp) continue;
        const char *cwp = *(const char *const *)(twp + 56);
        if (!cwp) continue;
        int ns = *(const unsigned short *)(cwp + 6);
        const unsigned char *c = *(const unsigned char *const *)(cwp + 12);
        if (!c) continue;
        const float *tpos = (const float *)(twp + 32);
        for (int j = 0; j < ns && j < 16 && post_count < MAX_POSTS; j++, c += 0x30) {
            int form = c[1], push = c[2];
            const float *cc = (const float *)(c + 8), *ab = (const float *)(c + 20);
            const char *otask = *(const char *const *)(cwp + 156);              // colliwk.mytask
            if (have_truck) {                                                   // 12 m (39 feet) plus its size
                float dx = tpos[0] + cc[0] - tx, dz = tpos[2] + cc[2] - tz;
                float reach = 120.0f + (ab[0] > ab[2] ? ab[0] : ab[2]);
                if (dx * dx + dz * dz > reach * reach) continue;
            }
            int take = (form == 0 || form == 1 || form == 3)                    // sphere, cylinder, box
                       && (push & 7) && (push & (1 << 4));                      // must push, and not be pushed
            if (!take) continue;
            post_exec[post_count] = otask ? *(const u32 *)(otask + 16) : 0u;
            float *P = posts[post_count++];
            P[0] = (float)form;
            P[1] = tpos[0] + cc[0]; P[2] = tpos[1] + cc[1]; P[3] = tpos[2] + cc[2];
            P[4] = ab[0]; P[5] = form == 0 ? ab[0] : ab[1]; P[6] = form == 3 ? ab[2] : ab[0];
            // A box turns with its object (C CUBE's shape has attr (1 << 13));
            // the heading only (taskwk angle y), as SADX's boxes stand upright.
            P[7] = form == 3 ? *(const int *)(twp + 24) * (6.2831853f / 65536.0f) : 0.0f;
        }
    }
}
static void __cdecl run_task_exec(char *tp)
{
    (void)tp;
    collect_posts();
    if (!run_over || !player_task) return;
    char *cwp = *(char **)(player_task + 56);
    unsigned char *c = cwp && *(unsigned short *)(cwp + 6) ? *(unsigned char **)(cwp + 12) : 0;
    if (c) c[3] = (unsigned char)(run_damage | 0x0F | (1 << 6));
}
static void __cdecl run_task_dest(char *tp)
{
    (void)tp;
    run_task = 0;                // SADX freed it (level change)
}
static int pad_freed;           // riding, but the pad is on for the pause menu
static int seat_frames;                  // frames since getting in
static signed char seat_mode, seat_mode2;   // Sonic's and Tails's mode once seated
// Headroom: Sonic sitting is about 3.4 feet tall from his seat point (the
// Protector, 3.07 feet from seat to roof, showed his spikes about 0.3 foot
// through the roof; the Stalker, 3.59, fits).  Trucks with less than
// SEAT_HEADROOM_M get the seat lowered by the difference.  Headroom: a ray
// straight up from the seat to the first body triangle above it.
#define SEAT_HEADROOM_M (3.5f / 3.281f)
static int seat_drop_truck = -1;
static float seat_drop_m;
static float seat_headroom(void)
{
    const float sx = car.seat[0] * UNITS_PER_METER, sy = car.seat[1] * UNITS_PER_METER, sz = car.seat[2] * UNITS_PER_METER;
    float best = 1e9f;
    for (int k = 0; k < car.draws; k++) {
        const unsigned short *d = truck_draws[car.first_draw + k];
        if (d[1] != 0 || d[3] >= GLASS_WHOLE) continue;
        const struct truck_model *m = &truck_models[d[0]];
        for (int t = 0; t < m->ntris; t++) {
            const float *A = m->points[m->tris[t * 3]];
            const float *B = m->points[m->tris[t * 3 + 1]];
            const float *C = m->points[m->tris[t * 3 + 2]];
            float d1 = (B[0] - A[0]) * (sz - A[2]) - (B[2] - A[2]) * (sx - A[0]);
            float d2 = (C[0] - B[0]) * (sz - B[2]) - (C[2] - B[2]) * (sx - B[0]);
            float d3 = (A[0] - C[0]) * (sz - C[2]) - (A[2] - C[2]) * (sx - C[0]);
            if (!((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0))) continue;
            float den = d1 + d2 + d3;
            if (den > -1e-9f && den < 1e-9f) continue;
            float y = (d2 * A[1] + d3 * B[1] + d1 * C[1]) / den;
            if (y > sy && y - sy < best) best = y - sy;
        }
    }
    return best / UNITS_PER_METER;
}
static void seat_sonic(void)
{
    if (seat_drop_truck != truck_now) {
        seat_drop_truck = truck_now;
        float h = seat_headroom();
        seat_drop_m = h < SEAT_HEADROOM_M ? SEAT_HEADROOM_M - h : 0;
        PrintDebug("[SADX Truck] %s: headroom %.2f feet, seat lowered %.2f feet\n", car.name,
                   (double)(h * 3.281f), (double)(seat_drop_m * 3.281f));
    }
    float p[3] = { car.seat[0], car.seat[1] - seat_drop_m, car.seat[2] };
    seat_player(player_task, player_motion, player_work, p);
    if (tails_riding) {
        if (!player2_task || !player2_motion || !player2_work) { tails_riding = 0; return; }
        float q[3] = { -car.seat[0], car.seat[1] - seat_drop_m, car.seat[2] };
        seat_player(player2_task, player2_motion, player2_work, q);
    }
}
// Better Tails AI moves Tails (its mode 115) to Sonic + (4.5 left, 4.5 up,
// 2.5 ahead) units every frame in its own task (list 0), so his head was
// through the roof.  SADX draws Tails from inside his own task: Tails_Main
// (0x461700) calls Tails_Display (0x460C40, from SADXFunctions.h) directly
// at 0x462456, at the end of his frame (list 1), not through the task's
// draw slot, so OnFrame's seat comes too late.  With Better Tails AI
// loaded only, the mod sends that one call to tails_draw: while Tails
// rides in its mode 115, seat him, then draw him as SADX would.  Any other
// time it only calls Tails_Display, unchanged.
#define player2_tp (*(char **)0x3B42E34)
static void seat_tails_again(void);
static void __cdecl tails_draw(char *tp)
{
    if (tp && tp == player2_tp) seat_tails_again();
    ((void (__cdecl *)(char *))0x460C40)(tp);
}
static void tails_draw_patch(void)
{
    static int done;
    if (done) return;
    done = 1;
    unsigned char *p = (unsigned char *)0x462456;
    u32 old, want = 0x460C40u - 0x46245Bu;
    if (p[0] != 232 || *(const u32 *)(p + 1) != want) {
        PrintDebug("[SADX Truck] Tails draw call at 0x462456: unexpected bytes, not patched\n");
        return;
    }
    VirtualProtect(p, 5, 0x40, &old);
    *(u32 *)(p + 1) = (u32)tails_draw - 0x46245Bu;
    VirtualProtect(p, 5, old, &old);
    PrintDebug("[SADX Truck] Tails draw call patched (Better Tails AI): Tails is seated just before he is drawn\n");
}
static void seat_tails_again(void)
{
    if (!riding || !tails_riding || !player2_task || !player2_motion || !player2_work || !btai_loaded()) return;
    if (!btai_seat_ok || *(const signed char *)player2_task != 115) return;
    float q[3] = { -car.seat[0], car.seat[1] - seat_drop_m, car.seat[2] };
    seat_player(player2_task, player2_motion, player2_work, q);
}
// Better Tails AI: patch Tails's draw call once he first sits in its mode.
static void btai_frame(void)
{
#ifndef TEST_EXE
    if (btai_seat_ok && btai_loaded()) tails_draw_patch();
#endif
}

// ---------------------------------------------------------------------
// Engine: sound, starter and battery.
//
// Sound: the mod plays its own sounds with DirectSound (dsound.dll, loaded
// at first use), apart from SADX's sound.  Files: the mod folder's
// sounds\ (skin_to_c.py copies them; truck_wavs in trucks_model.h).
// COM by hand: a DirectSound object's first field is its function table.
//   IDirectSound        3 CreateSoundBuffer, 6 SetCooperativeLevel
//   IDirectSoundBuffer  9 GetStatus, 11 Lock, 12 Play, 13 SetCurrentPosition,
//                       15 SetVolume, 17 SetFrequency, 18 Stop, 19 Unlock
#define COM(obj, i, type) ((type)((*(void ***)(obj))[i]))
#define PANIC_WAV TRUCK_WAVS                      // the Past's crowd sound (people_frame)
#define SND_SLOTS (TRUCK_WAVS + 1)
static void *ds;                                  // IDirectSound
static void *wav_buf[SND_SLOTS];                 // IDirectSoundBuffer
static u32 wav_rate[SND_SLOTS];                  // samples per second
static char wav_failed[SND_SLOTS];
static int snd_state;                             // 0 not tried, 1 on, -1 off
static unsigned char *snd_scratch;                // 4 MB for reading files
#define SND_SCRATCH (4u << 20)
static void *(__stdcall *k_CreateFileA)(const char *, u32, u32, void *, u32, u32, void *);
static int (__stdcall *k_ReadFile)(void *, void *, u32, u32 *, void *);
static int (__stdcall *k_CloseHandle)(void *);
static u32 (__stdcall *k_GetFileSize)(void *, u32 *);

static float log10_f(float x)
{
    float r;
    __asm__("fldlg2\n\tfxch %%st(1)\n\tfyl2x" : "=t"(r) : "0"(x));
    return r;
}

static void snd_init(void)
{
    if (snd_state) return;
    snd_state = -1;
    void *k = LoadLibraryExA("kernel32.dll", 0, 0), *u = LoadLibraryExA("user32.dll", 0, 0);
    void *d = LoadLibraryExA("dsound.dll", 0, 0);
    if (!k || !u || !d) { PrintDebug("[SADX Truck] sound off: dsound.dll not loaded\n"); return; }
    k_CreateFileA = (void *)GetProcAddress(k, "CreateFileA");
    k_ReadFile = (void *)GetProcAddress(k, "ReadFile");
    k_CloseHandle = (void *)GetProcAddress(k, "CloseHandle");
    k_GetFileSize = (void *)GetProcAddress(k, "GetFileSize");
    void *(__stdcall *get_window)(void) = (void *)GetProcAddress(u, "GetForegroundWindow");
    long (__stdcall *create)(void *, void **, void *) = (void *)GetProcAddress(d, "DirectSoundCreate");
    if (!k_CreateFileA || !k_ReadFile || !k_CloseHandle || !k_GetFileSize || !get_window || !create ||
        create(0, &ds, 0) < 0) {
        PrintDebug("[SADX Truck] sound off: DirectSoundCreate failed\n");
        ds = 0;
        return;
    }
    // 1 = DSSCL_NORMAL
    COM(ds, 6, long (__stdcall *)(void *, void *, u32))(ds, get_window(), 1);
    snd_scratch = VirtualAlloc(0, SND_SCRATCH, 0x3000, 4);
    if (!snd_scratch) return;
    snd_state = 1;
    PrintDebug("[SADX Truck] sound on (DirectSound), files in %s\n", snd_dir);
}

// Load one WAV (RIFF: "fmt " chunk = WAVEFORMATEX, "data" chunk = samples)
// into a DirectSound buffer.
static int snd_load(int i)
{
    if (wav_buf[i]) return 1;
    snd_init();
    if (snd_state != 1 || wav_failed[i]) return 0;
    wav_failed[i] = 1;
    char path[400];
    u32 n = 0;
    for (const char *s = snd_dir; *s && n < 300; s++) path[n++] = *s;
    for (const char *s = i == PANIC_WAV ? "crowd_panic.wav" : truck_wavs[i]; *s && n < 390; s++) path[n++] = *s;
    path[n] = 0;
    // 0x80000000 GENERIC_READ, 1 FILE_SHARE_READ, 3 OPEN_EXISTING
    void *h = k_CreateFileA(path, 0x80000000u, 1, 0, 3, 0, 0);
    if (h == (void *)-1) { PrintDebug("[SADX Truck] sound file missing: %s\n", path); return 0; }
    u32 size = k_GetFileSize(h, 0), got = 0;
    int ok = size <= SND_SCRATCH && k_ReadFile(h, snd_scratch, size, &got, 0) && got == size;
    k_CloseHandle(h);
    if (!ok) return 0;
    const unsigned char *fmt = 0, *data = 0;
    u32 data_n = 0;
    for (u32 p = 12; p + 8 <= size;) {
        u32 len = *(const u32 *)(snd_scratch + p + 4);
        if (snd_scratch[p] == 'f' && snd_scratch[p + 1] == 'm') fmt = snd_scratch + p + 8;
        if (snd_scratch[p] == 'd' && snd_scratch[p + 1] == 'a') { data = snd_scratch + p + 8; data_n = len; }
        p += 8 + len + (len & 1);
    }
    if (!fmt || !data || data + data_n > snd_scratch + size) return 0;
    unsigned short wfx[9];                         // WAVEFORMATEX, 18 bytes
    for (int k = 0; k < 8; k++) wfx[k] = ((const unsigned short *)fmt)[k];
    wfx[8] = 0;
    // DSBUFFERDESC: size, flags, bytes, reserved, format, 3D algorithm GUID.
    // Flags: (1 << 5) frequency, (1 << 7) volume, (1 << 15) keep playing
    // when the window loses focus, (1 << 16) accurate play position.
    u32 desc[9] = { 36, (1u << 5) | (1u << 7) | (1u << 15) | (1u << 16), data_n, 0, (u32)wfx, 0, 0, 0, 0 };
    void *buf = 0;
    if (COM(ds, 3, long (__stdcall *)(void *, void *, void **, void *))(ds, desc, &buf, 0) < 0 || !buf) return 0;
    void *p1, *p2;
    u32 n1, n2;
    if (COM(buf, 11, long (__stdcall *)(void *, u32, u32, void **, u32 *, void **, u32 *, u32))
            (buf, 0, data_n, &p1, &n1, &p2, &n2, 0) < 0) return 0;
    for (u32 k = 0; k < n1; k++) ((unsigned char *)p1)[k] = data[k];
    for (u32 k = 0; k < n2; k++) ((unsigned char *)p2)[k] = data[n1 + k];
    COM(buf, 19, long (__stdcall *)(void *, void *, u32, void *, u32))(buf, p1, n1, p2, n2);
    wav_buf[i] = buf;
    wav_rate[i] = *(const u32 *)(fmt + 4);
    wav_failed[i] = 0;
    return 1;
}

static float snd_master = 0.8f;                 // see snd_set
static unsigned char wav_vol_set[SND_SLOTS];   // snd_set gave it a volume
static void snd_play(int i, int loop)
{
    if (!snd_load(i)) return;
    // Never given a volume: play it at full gain times the master volume
    // (a new DirectSound buffer starts at full volume, 0 dB).
    if (!wav_vol_set[i]) {
        float gain = 100.0f * snd_master;
        long vol = gain <= 0.01f ? -10000 : (long)(2000.0f * log10_f(gain / 100.0f));
        if (vol < -10000) vol = -10000;
        if (vol > 0) vol = 0;
        COM(wav_buf[i], 15, long (__stdcall *)(void *, long))(wav_buf[i], vol);
    }
    COM(wav_buf[i], 13, long (__stdcall *)(void *, u32))(wav_buf[i], 0);
    COM(wav_buf[i], 12, long (__stdcall *)(void *, u32, u32, u32))(wav_buf[i], 0, 0, loop ? 1 : 0);
}
static void snd_stop(int i)
{
    if (wav_buf[i]) COM(wav_buf[i], 18, long (__stdcall *)(void *))(wav_buf[i]);
}
// gain 0 to 100 (Cabela's scale), speed 1 = the file's own rate.
// Cabela's volume: 20 * log10(gain / 100) dB; DirectSound counts 1/100 dB.
// All truck sounds: 0.8 (the mod's own mix) x the TruckVolume setting x
// the Mod Manager's sound effect volume (SEVolume), or 0 with the game's
// sound off (EnableGameSound); set in Init (sound_settings).
static void snd_set(int i, float gain, float speed)
{
    if (!wav_buf[i]) return;
    wav_vol_set[i] = 1;
    gain *= snd_master;
    long vol = gain <= 0.01f ? -10000 : (long)(2000.0f * log10_f(gain / 100.0f));
    if (vol < -10000) vol = -10000;
    if (vol > 0) vol = 0;
    u32 hz = (u32)(wav_rate[i] * speed);
    if (hz < 100) hz = 100;
    if (hz > 100000) hz = 100000;
    COM(wav_buf[i], 15, long (__stdcall *)(void *, long))(wav_buf[i], vol);
    COM(wav_buf[i], 17, long (__stdcall *)(void *, u32))(wav_buf[i], hz);
}

// Pause: stop every playing sound and note it (1 once, 2 looping);
// resume: play those again from where they stopped.  IDirectSoundBuffer
// GetStatus is slot 9 (DSBSTATUS_PLAYING (1 << 0), LOOPING (1 << 2)).
// A level change is a pause too (game mode not 15) that never resumes
// the old truck: engine_reset forgets the noted sounds (snd_pause_forget),
// or the new truck's first frame played the old engine loop again.
static unsigned char snd_was[SND_SLOTS];
static void snd_pause_forget(void)
{
    for (int i = 0; i < SND_SLOTS; i++) snd_was[i] = 0;
}
static void snd_pause(int on)
{
    unsigned char *was = snd_was;
    for (int i = 0; i < SND_SLOTS; i++) {
        if (!wav_buf[i]) continue;
        if (on) {
            u32 st = 0;
            COM(wav_buf[i], 9, long (__stdcall *)(void *, u32 *))(wav_buf[i], &st);
            was[i] = (st & (1 << 0)) ? ((st & (1 << 2)) ? 2 : 1) : 0;
            if (was[i]) snd_stop(i);
        } else if (was[i]) {
            COM(wav_buf[i], 12, long (__stdcall *)(void *, u32, u32, u32))(wav_buf[i], 0, 0, was[i] == 2 ? 1 : 0);
            was[i] = 0;
        }
    }
}

// Cabela's curve: straight lines between the points, the end values outside.
static float curve(const struct snd_curve *c, float x)
{
    if (c->n == 0) return 1.0f;
    if (x <= c->x[0]) return c->y[0];
    for (int k = 1; k < c->n; k++)
        if (x < c->x[k])
            return c->y[k - 1] + (x - c->x[k - 1]) / (c->x[k] - c->x[k - 1]) * (c->y[k] - c->y[k - 1]);
    return c->y[c->n - 1];
}

// Starter, from Cabela's 4x4 Off-Road Adventure (the first game, 2001;
// 4x4 Adventure.exe: key 0x4765A0, start 0x46A430, per frame 0x468B50):
//   X with the engine off, gas and brake let go:
//     more than 17 tries and a roll under tries -> flooded: click, the
//       starter turns but never catches
//     a roll >= engine damage (always when undamaged) -> starts at once
//     otherwise crank: intro, then 0.6 s loops while X is held; each loop
//       catches when a roll >= damage; let go of X -> the starter stops
//   X with gas or brake held: "cannot be started while in gear"
//   X while running: the sound winds down 4000 RPM a second, then stops
//   stalls: damage 1, gear 3 or higher under 1500 RPM with no gas, or
//     upside down for 6 s
// Battery, from Joshua's GTA V engine.cs: cranking drains 0.01 V a frame,
// running charges 0.002 V a frame, the crank sound slows to battery / 12.
// Below 6 V it cannot start at once, below 4 V it cannot catch.
#define BUTTON_X (1 << 10)
// Cold: Ice Cap (SADX level 8) is played like Cabela's 1's cold season
// (season 3, 4x4 Adventure.exe 0x46A4E5): the engine never starts on a
// tap, X must be held to crank.  The cold also slows the crank to 0.8 of
// its speed (the mod's own addition, not in Cabela's), and the tyres grip
// like Cabela's 3's Drive tire on snow (ACCS.LUA RegisterTyre "SpinMasters
// Drive tire set": snow 0.7 of the truck's grip).
#define COLD_LEVEL 8
#define COLD_CRANK 0.8f
#define COLD_GRIP 0.7f
static int cold_here(void) { return level_now == COLD_LEVEL; }
static float eng_t;            // seconds left of the current sound
static float tries;            // start attempts (Cabela's 1, +0x1160)
static float battery = 12.0f;  // volts
static float eng_damage;       // 0 to 1 (from the engine's damage points)
static float run_t, upside_t, gear_msg_t, snd_rpm;
static int flooded, x_before;
static u32 rnd_state = 12345;
static int rnd(int n)
{
    rnd_state = rnd_state * 1103515245u + 12345u;
    return (int)((rnd_state >> 16) % (u32)n);
}
static const char *eng_name(void)
{
    static const char *const names[] = { "off", "cranking", "cranking", "catching", "running", "stopping" };
    return names[eng];
}

static void engine_loops(int on)
{
    const struct truck_sound *ts = &truck_sounds[truck_now];
    for (int k = 0; k < ts->nsamples; k++) {
        if (on) { snd_play(ts->s[k].wav, 1); snd_set(ts->s[k].wav, 0, 1); }
        else snd_stop(ts->s[k].wav);
    }
}

static void engine_reset(void)
{
    const struct truck_sound *ts = &truck_sounds[truck_now];
    engine_loops(0);
    snd_stop(ts->start_wav); snd_stop(ts->stop_wav);
    snd_stop(WAV_CRANK_IN); snd_stop(WAV_CRANK_LOOP);
    eng = ENG_OFF;
    upside_t = 0;
    snd_stop(WAV_WATER); snd_stop(WAV_DAMAGED); snd_stop(WAV_BAD); snd_stop(WAV_STEAM);
    snd_pause_forget();                 // a pause noted them: don't play them again
    eng_level = -1;                     // a new truck: set its power again
    reversing = 0;
    have_prev = 0;
    air_t = 0;
    falling_n = 0;                      // fallen pieces belong to the old truck
    for (int k = 0; k < 18; k++) break_pending[k] = 0;
    for (int k = 0; k < 26; k++) part_lvl_shown[k] = -1;
}

static void engine_stop(const char *why)
{
    eng = ENG_STOPPING;
    snd_rpm = GetRPM(truck);
    PrintDebug("[SADX Truck] engine %s\n", why);
}

// Engine damage, from Cabela's 3 (GameDll):
//   points count down from the truck's SetCarPartsPoints engine value
//   (DamageUpd 0x1001F670, never below 1); level from what is left, per
//   mille (SetDamageLine 950, 750, 500, 250): 0 green, 1 yellow, 2 orange,
//   3 red, 4 black = dead.  Power: TorqueScale = 1 - 0.1 * level
//   (0x10023550).  Level 3 "DAMAGE WARNING!", level 4 "The engine is too
//   damaged to continue!" (engine.lua).
//   Water (UpdateDamageByWater 0x10020B10): every second, when the water
//   surface is 0.1 m (4 inches) or more above the body origin (plus the
//   snorkel's height; none fitted yet), the engine loses 50 points.
//   Sounds (FX.SND): level 2 MediumDamagedEngine (dengine2 loop), level 3
//   and 4 BadDamagedEngine (dengine1 loop + grind1 every 15 to 33 s).
// Sputter at level 3, from Joshua's GTA V engine.cs: every 0.15 s a
// 1 in (per mille / 10 + 2) chance the engine cuts out for 0.3 s; under
// 1200 RPM that stalls it.
// For the starter: damage = 1 - per mille / 1000 (level 4: 1, cannot catch).
// Damage stays with each truck between rides; repair: on foot, hold D-pad
// up for 2 seconds (the picked truck).
#define BUTTON_UP (1 << 4)
#define BUTTON_DOWN (1 << 5)
// Snorkel: Cabela's raises the flooding height by car+0x5D8 = param 0x29
// - 1.0 (GameDll 0x10024B50), param from SetAccessoryData2(
// PHYS_PARAM_SNORKEL, 60 or 100) in ACCS.LUA.  Read in GameDll:
// PHYS_PARAM_SNORKEL is 20 (0x1005CD12); the Lua call stores (20, value)
// in the accessory (0x10050880); fitting it adds value x 0.01 (constant
// 0x10167420) to the truck's accessory parameter 20 (0x100AF745), which
// starts at 1.0 for every truck (0x1005B201, all 21 of them); parameter
// 20 is byte 0xA4 of the truck's block, which 0x10024DEB reads.  So the
// height is value / 100 m: 0.6 m and 1.0 m, about 2 and 3 1/4 feet.
// With Snork1 the Stalker then floods at 0.7 m, which is where its
// snorkel's top is (0.71 m above the body origin).
#define SNORKEL_M (snorkel[truck_now] == 1 ? 0.6f : snorkel[truck_now] == 2 ? 1.0f : 0.0f)
static float water_rel = NO_WATER;       // water above the body origin, m
static float water_tick, sputter_tick, grind_t, steam_t, repair_t;
static int was_wet;

// The 26 parts (Cabela's 3 numbering, SetCarPartsPoints order).  Cabela's
// "left" is its x < 0, which the mirrored model (skin_to_c.py mirror())
// shows on the truck's right, so the names here say what you see.
static const char *const part_names[18] = {
    "front-right wheel", "front-left wheel", "rear-right wheel", "rear-left wheel",
    "front-right body", "front-left body", "rear-right body", "rear-left body",
    "right door", "left door", "front-right light", "front-left light",
    "rear-right light", "rear-left light", "transmission", "engine", "radiator", "rear end",
};
static float part_pts[TRUCKS][26];       // points left, per truck, kept between rides
static char parts_set[TRUCKS];

static void parts_init(int n)
{
    if (parts_set[n]) return;
    for (int k = 0; k < 26; k++) part_pts[n][k] = trucks[n].parts[k];
    parts_set[n] = 1;
}
static float part_permille(int k)
{
    parts_init(truck_now);
    return part_pts[truck_now][k] / (car.parts[k] * 0.001f);
}
// SetDamageLine 950, 750, 500, 250: 0 green, 1 yellow, 2 orange, 3 red, 4 black.
static int damage_level(float permille)
{
    return permille >= 950 ? 0 : permille >= 750 ? 1 : permille >= 500 ? 2 : permille >= 250 ? 3 : 4;
}
static int part_level(int k) { return damage_level(part_permille(k)); }
static void damage_part(int k, float points, const char *why)
{
    if (points <= 0 || k < 0 || k > 17) return;
    parts_init(truck_now);
    part_pts[truck_now][k] -= points;
    if (part_pts[truck_now][k] < 1) part_pts[truck_now][k] = 1;
    PrintDebug("[SADX Truck] %s -%.0f (%s): %.0f of %.0f left\n", part_names[k], (double)points, why,
               (double)part_pts[truck_now][k], (double)car.parts[k]);
}
static void repair_all(int n)
{
    parts_set[n] = 0;
    parts_init(n);
}

// Hits, from Cabela's 3 OnCollisionDeform (GameDll 0x1001EB40):
//   speed = how fast the body moves into the contact surface (the mod
//   notes it in sadx_hit for each body box ray that hits: note_contact)
//   points by speed, m/s (SetDamageParams): under 1 none, under 5.55 25,
//   under 11.11 35, under 16.67 50, under 21 100, faster 350
//   from below (surface facing up) x 0.02, from above (roof) x 2.5
//   (body box rays and wheel rays both count: the wheel rays are what
//   meets a wall at bumper height)
//   a hit within 0.75 s of the last, or a contact still going on from
//   the frame before, counts only if 1 m/s harder than the last hit
//   landing after more than 1 s in the air: every part gets
//   whole(points x 0.02) x 2 x (speed / 21 above 21 m/s, else air time + 2), no zones
// Zones (GameDll 0x1001E010) from the contact point in truck space,
// Cabela's sides (its left = x < 0).  Cabela's has 6 limits per truck
// (not found); here: front / rear past half way to an axle, sides past
// half the track, high above the spring tops (posArc).
//   corner, low:  wheel, light, body     corner, high: light, body
//   middle side:  door                   front centre: both front lights
//   rear centre, high: both rear lights   half each + radiator
//   half each + rear end                  middle centre, low: transmission
static float last_hit_speed, hit_cool, land_vy;
static int touching;                     // a contact of 1 m/s or more last frame

// ---- Dents: Cabela's 3 car deformation (GameDll cMeshDeformatorC) ----
// A hit pushes the body's vertices in near the contact point:
//   collision handler 0x1001EB40 -> Deform 0x100BA450 -> each vertex
//   0x100B93C0.  Force by hit speed, the 3rd column of CARS.LUA
//   SetDamageParams (the same speed bands as the points); times 0.02 from
//   below and 2.5 on the roof, like the points.  Per vertex (meters):
//     only within 0.75 (30 inches) of the hit, along and across the push;
//     inward push = (1 - 4/3 x distance along)^2 x force, sideways push
//     = (2/3 x distance across)^2 x force away from the hit's middle; a
//     vertex sticking out past the hit point gets + force inward and the
//     sideways push doubled; never past 0.15 (6 inches) inward or 0.075
//     (3 inches) sideways from where it started, and never more than 0.15
//     from there in all.
//   Strength = (1 - resistance) x force; SCRIPTS\COMMON\CARDEFORM.SCR
//   gives the rims and steering wheel 1000 (no dents); the body has no
//   setting, taken as 0 here (a guess: the code default was not read).
//   Windows: each adds up its vertices' offsets as they were before the
//   push (Ghidra's reading of 0x100B93C0's result; unsure); past
//   SetDeformBarrier1 ("All", 10) the window cracks (0x100197E0, its
//   cracked _1 copy is drawn).
// Only the body pieces (frame 0) dent; the wheels never.  The truck's
// models are changed in place (one truck exists at a time); the start
// shape is kept and put back by repair or a new truck.  Lighting (normals)
// is left as it was.
#define DENT_MAX_VERTS 60000
#define DENT_MAX_MODELS 512
static float dent_orig[DENT_MAX_VERTS][3];      // start shape, SADX units
static struct { unsigned short model; signed char window; int first; } dent_model[DENT_MAX_MODELS];
static int dent_n, dent_nverts;
static int dent_truck = -1;
static float glass_total[8];
static int glass_cracked[8];
// Shading: the start normals, and each point's start "face direction" (the
// sum of its triangles' facing, length 1).  A dented point's normal = its
// start normal + (face direction now - face direction at start), length 1:
// undented points keep the model's own lighting exactly (hard edges and
// seams too), dented ones turn with the dent.
static float dent_onorm[DENT_MAX_VERTS][3], dent_oface[DENT_MAX_VERTS][3];
static float dent_face_tmp[2048][3];
static void face_dirs(const struct truck_model *m, const float (*pts)[3], float (*out)[3])
{
    for (int i = 0; i < m->npoints; i++) out[i][0] = out[i][1] = out[i][2] = 0;
    for (int t = 0; t < m->ntris; t++) {
        int a = m->tris[t * 3], b = m->tris[t * 3 + 1], c = m->tris[t * 3 + 2];
        float u[3] = { pts[b][0] - pts[a][0], pts[b][1] - pts[a][1], pts[b][2] - pts[a][2] };
        float v[3] = { pts[c][0] - pts[a][0], pts[c][1] - pts[a][1], pts[c][2] - pts[a][2] };
        float n[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
        for (int j = 0; j < 3; j++) { out[a][j] += n[j]; out[b][j] += n[j]; out[c][j] += n[j]; }
    }
    for (int i = 0; i < m->npoints; i++) {
        float l = sqrt_f(out[i][0] * out[i][0] + out[i][1] * out[i][1] + out[i][2] * out[i][2]);
        if (l > 1e-12f) { out[i][0] /= l; out[i][1] /= l; out[i][2] /= l; }
    }
}
static void dent_shade(int k)
{
    const struct truck_model *m = &truck_models[dent_model[k].model];
    if (m->npoints > 2048) return;
    face_dirs(m, (const float (*)[3])m->points, dent_face_tmp);
    const int f = dent_model[k].first;
    for (int i = 0; i < m->npoints; i++) {
        float n[3];
        for (int j = 0; j < 3; j++) n[j] = dent_onorm[f + i][j] + dent_face_tmp[i][j] - dent_oface[f + i][j];
        float l = sqrt_f(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l < 1e-6f) continue;
        for (int j = 0; j < 3; j++) m->normals[i][j] = n[j] / l;
    }
}
static void dent_restore(void)
{
    for (int k = 0; k < dent_n; k++) {
        const struct truck_model *m = &truck_models[dent_model[k].model];
        for (int i = 0; i < m->npoints; i++)
            for (int j = 0; j < 3; j++) {
                m->points[i][j] = dent_orig[dent_model[k].first + i][j];
                m->normals[i][j] = dent_onorm[dent_model[k].first + i][j];
            }
    }
    dent_n = dent_nverts = 0;
    dent_truck = -1;
    for (int w = 0; w < 8; w++) { glass_total[w] = 0; glass_cracked[w] = 0; }
}
static void dent_backup(void)
{
    if (dent_truck == truck_now) return;
    dent_restore();
    dent_truck = truck_now;
    for (int k = 0; k < car.draws; k++) {
        const unsigned short *d = truck_draws[car.first_draw + k];
        if (d[1] != 0) continue;                        // wheels: no dents
        int seen = 0;
        for (int i = 0; i < dent_n && !seen; i++) seen = dent_model[i].model == d[0];
        const struct truck_model *m = &truck_models[d[0]];
        if (seen || dent_n == DENT_MAX_MODELS || dent_nverts + m->npoints > DENT_MAX_VERTS) continue;
        dent_model[dent_n].model = d[0];
        dent_model[dent_n].window = d[3] >= GLASS_WHOLE && d[3] < GLASS_CRACKED ? (signed char)(d[3] - GLASS_WHOLE) : -1;
        dent_model[dent_n].first = dent_nverts;
        for (int i = 0; i < m->npoints; i++)
            for (int j = 0; j < 3; j++) {
                dent_orig[dent_nverts + i][j] = m->points[i][j];
                dent_onorm[dent_nverts + i][j] = m->normals[i][j];
            }
        if (m->npoints <= 2048) face_dirs(m, (const float (*)[3])m->points, &dent_oface[dent_nverts]);
        dent_nverts += m->npoints;
        dent_n++;
    }
}
// Each truck keeps its dents and cracked windows while the game runs
// (mod setting Damage / KeepDents, on by default).  Trucks share some
// models, so only one truck's shape can be on the models at a time: on a
// switch the old truck's dents (offsets from the start shape) are copied to
// its slot, and the new truck's slot, if any, is put back on.
#define DENT_SAVE_VERTS 70000
static int keep_dents = 1;
static float dent_saved[DENT_SAVE_VERTS][3];
static int dent_slot[TRUCKS], dent_slot_n[TRUCKS], dent_slots_used;
static char dent_has[TRUCKS];
static float glass_total_saved[TRUCKS][8];
static int glass_cracked_saved[TRUCKS][8];
static void dent_save(void)
{
    int t = dent_truck;
    if (!keep_dents || t < 0) return;
    if (!dent_slot_n[t]) {
        if (dent_slots_used + dent_nverts > DENT_SAVE_VERTS) return;
        dent_slot[t] = dent_slots_used;
        dent_slot_n[t] = dent_nverts;
        dent_slots_used += dent_nverts;
    }
    if (dent_slot_n[t] != dent_nverts) return;
    for (int k = 0; k < dent_n; k++) {
        const struct truck_model *m = &truck_models[dent_model[k].model];
        for (int i = 0; i < m->npoints; i++)
            for (int j = 0; j < 3; j++)
                dent_saved[dent_slot[t] + dent_model[k].first + i][j] = m->points[i][j] - dent_orig[dent_model[k].first + i][j];
    }
    for (int w = 0; w < 8; w++) { glass_total_saved[t][w] = glass_total[w]; glass_cracked_saved[t][w] = glass_cracked[w]; }
    dent_has[t] = 1;
    PrintDebug("[SADX Truck] %s's dents kept (%d points)\n", trucks[t].name, dent_nverts);
}
static void dent_file_load(int t);
static void dent_load(void)
{
    int t = truck_now;
    if (!dent_has[t]) dent_file_load(t);
    if (!keep_dents || !dent_has[t]) return;
    dent_backup();
    if (dent_slot_n[t] != dent_nverts) return;
    for (int k = 0; k < dent_n; k++) {
        const struct truck_model *m = &truck_models[dent_model[k].model];
        for (int i = 0; i < m->npoints; i++)
            for (int j = 0; j < 3; j++)
                m->points[i][j] = dent_orig[dent_model[k].first + i][j] + dent_saved[dent_slot[t] + dent_model[k].first + i][j];
    }
    for (int k = 0; k < dent_n; k++) dent_shade(k);
    for (int w = 0; w < 8; w++) { glass_total[w] = glass_total_saved[t][w]; glass_cracked[w] = glass_cracked_saved[t][w]; }
    PrintDebug("[SADX Truck] %s's dents put back\n", trucks[t].name);
}
// K2: each truck's damage in a file in the mod folder, damage_Car1.bin to
// damage_Car14.bin, so it survives closing the game (mod setting Damage /
// SaveDamage, on by default; dents need KeepDents too).  Written when
// Sonic gets out, on a truck switch, on a repair and when the game closes
// (OnExit).  Read: part points at Init, dents and windows the first time
// the truck is built.  Layout, 4 bytes each: "SXTD", version 2, truck,
// dent points (0 = none), parts set, part points [26], window damage [8],
// windows cracked [8], fuel (0 to 1), odometer (miles), then the dent offsets (meters, 3 per point, in the
// order dent_backup lists them).  A file whose dent point count differs
// from the truck's (the model changed) keeps its parts, not its dents.
#define DFILE_MAGIC 0x44545853u              // "SXTD"
static char dfile_dir[320];
static u32 dfile_dir_n;
static char dfile_tried[TRUCKS];
static void *(__stdcall *f_create)(const char *, u32, u32, void *, u32, u32, void *);
static int (__stdcall *f_read)(void *, void *, u32, u32 *, void *);
static int (__stdcall *f_write)(void *, const void *, u32, u32 *, void *);
static int (__stdcall *f_close)(void *);
static void dfile_name(int t, char *out)
{
    const char *a = "\\damage_Car", *b = ".bin";
    u32 n = 0;
    for (u32 i = 0; i < dfile_dir_n; i++) out[n++] = dfile_dir[i];
    while (*a) out[n++] = *a++;
    if (t + 1 >= 10) out[n++] = (char)('0' + (t + 1) / 10);
    out[n++] = (char)('0' + (t + 1) % 10);
    while (*b) out[n++] = *b++;
    out[n] = 0;
}
// Opens truck t's file to read; fills the header.  0 if none or not ours.
static void *dfile_open(int t, u32 hdr[5])
{
    char name[340]; u32 got = 0;
    if (!f_create) return 0;
    dfile_name(t, name);
    void *h = f_create(name, 0x80000000u, 1, 0, 3, 0, 0);   // GENERIC_READ, OPEN_EXISTING
    if (h == (void *)-1) return 0;
    if (!f_read(h, hdr, 20, &got, 0) || got != 20 || hdr[0] != DFILE_MAGIC || (hdr[1] != 1 && hdr[1] != 2) || hdr[2] != (u32)t) {
        f_close(h);
        return 0;
    }
    return h;
}
static void damage_files_init(const char *path, u32 n)
{
    for (u32 i = 0; i < n && i < 300; i++) dfile_dir[i] = path[i];
    dfile_dir_n = n < 300 ? n : 300;
    void *k = LoadLibraryExA("kernel32.dll", 0, 0);
    if (!k) return;
    f_create = (void *)GetProcAddress(k, "CreateFileA");
    f_read = (void *)GetProcAddress(k, "ReadFile");
    f_write = (void *)GetProcAddress(k, "WriteFile");
    f_close = (void *)GetProcAddress(k, "CloseHandle");
    if (!f_create || !f_read || !f_write || !f_close) { f_create = 0; return; }
    if (!save_damage) return;
    for (int t = 0; t < TRUCKS; t++) {
        u32 hdr[5], got = 0;
        void *h = dfile_open(t, hdr);
        if (!h) continue;
        float pts[26];
        int read_pts = f_read(h, pts, sizeof pts, &got, 0) && got == sizeof pts;
        if (hdr[4] && read_pts) {
            for (int q = 0; q < 26; q++) part_pts[t][q] = pts[q];
            parts_set[t] = 1;
            PrintDebug("[SADX Truck] %s: part damage loaded from its file\n", trucks[t].name);
        }
        // Version 2: fuel and odometer after the windows (32 + 32 bytes).
        float skip[16], fo[2];
        if (read_pts && hdr[1] >= 2 && f_read(h, skip, sizeof skip, &got, 0) && got == sizeof skip &&
            f_read(h, fo, sizeof fo, &got, 0) && got == sizeof fo) {
            fuel[t] = fo[0] < 0 ? 0 : fo[0] > 1 ? 1 : fo[0];
            odo_miles[t] = fo[1] < 0 ? 0 : fo[1];
            PrintDebug("[SADX Truck] %s: fuel %.0f%%, odometer %.1f miles, from its file\n", trucks[t].name,
                       (double)(fuel[t] * 100.0f), (double)odo_miles[t]);
        }
        f_close(h);
    }
}
static void dent_file_load(int t)
{
    if (!save_damage || !keep_dents || dfile_tried[t]) return;
    dfile_tried[t] = 1;
    u32 hdr[5], got = 0;
    void *h = dfile_open(t, hdr);
    if (!h) return;
    dent_backup();                              // lists the truck's points (dent_nverts)
    float skip[26], gt[8]; int gc[8];
    int ok = f_read(h, skip, sizeof skip, &got, 0) && got == sizeof skip &&
             f_read(h, gt, sizeof gt, &got, 0) && got == sizeof gt &&
             f_read(h, gc, sizeof gc, &got, 0) && got == sizeof gc;
    float fo[2];
    if (ok && hdr[1] >= 2) ok = f_read(h, fo, sizeof fo, &got, 0) && got == sizeof fo;   // fuel, odometer (read at Init)
    if (ok && hdr[3] && hdr[3] != (u32)dent_nverts) {
        PrintDebug("[SADX Truck] %s: dents in its file are for %u points, the truck has %d; dents not loaded\n",
                   trucks[t].name, hdr[3], dent_nverts);
        ok = 0;
    }
    if (ok && hdr[3]) {
        if (!dent_slot_n[t]) {
            if (dent_slots_used + dent_nverts > DENT_SAVE_VERTS) ok = 0;
            else { dent_slot[t] = dent_slots_used; dent_slot_n[t] = dent_nverts; dent_slots_used += dent_nverts; }
        }
        u32 bytes = (u32)dent_nverts * 12u;
        ok = ok && dent_slot_n[t] == dent_nverts && f_read(h, dent_saved[dent_slot[t]], bytes, &got, 0) && got == bytes;
        if (ok) {
            for (int w = 0; w < 8; w++) { glass_total_saved[t][w] = gt[w]; glass_cracked_saved[t][w] = gc[w]; }
            dent_has[t] = 1;
            PrintDebug("[SADX Truck] %s: dents and windows loaded from its file (%d points)\n", trucks[t].name, dent_nverts);
        }
    }
    f_close(h);
}
static void damage_file_save(int t)
{
    if (!save_damage || !f_create || t < 0 || t >= TRUCKS) return;
    char name[340]; u32 put = 0;
    dfile_name(t, name);
    void *h = f_create(name, 0x40000000u, 0, 0, 2, 0, 0);     // GENERIC_WRITE, CREATE_ALWAYS
    if (h == (void *)-1) { PrintDebug("[SADX Truck] could not write %s\n", name); return; }
    int dents = keep_dents && dent_has[t] && dent_slot_n[t];
    u32 hdr[5] = { DFILE_MAGIC, 2, (u32)t, dents ? (u32)dent_slot_n[t] : 0, (u32)parts_set[t] };
    float gt[8] = { 0 }; int gc[8] = { 0 };
    if (dents) for (int w = 0; w < 8; w++) { gt[w] = glass_total_saved[t][w]; gc[w] = glass_cracked_saved[t][w]; }
    int ok = f_write(h, hdr, sizeof hdr, &put, 0) && f_write(h, part_pts[t], 26 * 4, &put, 0) &&
             f_write(h, gt, sizeof gt, &put, 0) && f_write(h, gc, sizeof gc, &put, 0);
    float fo[2] = { fuel[t], odo_miles[t] };
    if (ok) ok = f_write(h, fo, sizeof fo, &put, 0);
    if (ok && dents) ok = f_write(h, dent_saved[dent_slot[t]], (u32)dent_slot_n[t] * 12u, &put, 0);
    f_close(h);
    dfile_tried[t] = 1;                         // what's on the models now is newer than the file
    PrintDebug("[SADX Truck] %s: damage saved to damage_Car%d.bin (%s, %d dent points, fuel %.0f%%, odometer %.1f miles)%s\n",
               trucks[t].name, t + 1, parts_set[t] ? "parts" : "parts full", dents ? dent_slot_n[t] : 0,
               (double)(fuel[t] * 100.0f), (double)odo_miles[t], ok ? "" : ", WRITE FAILED");
}
// One vertex (meters, truck space): v moved, o its start, P the hit, D the
// push direction (unit), s the strength.  Returns 1 if it moved; *amount =
// its offset before the push.
static int dent_vertex(float v[3], const float o[3], const float P[3], const float D[3], float s, float *amount)
{
    float d[3] = { v[0] - o[0], v[1] - o[1], v[2] - o[2] };
    float along = d[0] * D[0] + d[1] * D[1] + d[2] * D[2];
    float q[3] = { d[0] - along * D[0], d[1] - along * D[1], d[2] - along * D[2] };
    float across = sqrt_f(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    *amount = 0;
    if (along > 0.15f || across > 0.075f) return 0;
    float r[3] = { P[0] - v[0], P[1] - v[1], P[2] - v[2] };
    float a = r[0] * D[0] + r[1] * D[1] + r[2] * D[2];
    float rp[3] = { r[0] - a * D[0], r[1] - a * D[1], r[2] - a * D[2] };
    float la = a < 0 ? -a : a, lp = sqrt_f(rp[0] * rp[0] + rp[1] * rp[1] + rp[2] * rp[2]);
    if (la > 0.75f || lp > 0.75f) return 0;
    float k1 = 1.0f - la * (4.0f / 3.0f), k2 = lp * (4.0f / 3.0f) * 0.5f;
    float pa = k1 * k1 * s, pp = k2 * k2 * s;
    if (a > 0) { pa += s; pp += pp; }
    if (pa > 0.15f - along) pa = 0.15f - along;
    if (pp > 0.075f - across) pp = 0.075f - across;
    if (lp > 1e-6f) { rp[0] /= lp; rp[1] /= lp; rp[2] /= lp; }
    for (int j = 0; j < 3; j++) v[j] += D[j] * pa - rp[j] * pp;
    float e[3] = { v[0] - o[0], v[1] - o[1], v[2] - o[2] };
    float e2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    if (e2 > 0.0225f) {
        float k = 0.15f / sqrt_f(e2);
        for (int j = 0; j < 3; j++) v[j] = o[j] + e[j] * k;
    }
    *amount = sqrt_f(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    return 1;
}
// SetDamageParams' force column, by hit speed (m/s).
// Dent strength times this (mod setting Damage / DentStrength, 1 to 10;
// 1 = Cabela's own numbers).
static float dent_strength = 1.0f;
#define DENT_TEST dent_strength
static float dent_force(float speed)
{
    return DENT_TEST * (speed < 1.0f ? 0.0f : speed < 5.55f ? 0.005f : speed < 11.11f ? 0.008f :
           speed < 16.67f ? 0.015f : speed < 21.0f ? 0.021f : 0.100f);
}
// A dent: hit at world point pw (EngineDll meters), pushing along world
// direction dw (the surface normal, facing the truck), strength s.
static void dent_hit(const float pw[3], const float dw[3], float s, const char *why)
{
    if (s < 0.002f) return;                     // Deform skips weaker pushes
    dent_backup();
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    float r[3] = { pw[0] - pos[0], pw[1] - pos[1], pw[2] - pos[2] }, P[3], D[3];
    for (int i = 0; i < 3; i++) {
        P[i] = r[0] * rot[i * 3] + r[1] * rot[i * 3 + 1] + r[2] * rot[i * 3 + 2];
        D[i] = dw[0] * rot[i * 3] + dw[1] * rot[i * 3 + 1] + dw[2] * rot[i * 3 + 2];
    }
    float dl = sqrt_f(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
    if (dl < 1e-6f) return;
    D[0] /= dl; D[1] /= dl; D[2] /= dl;
    int moved = 0;
    float deepest = 0;
    for (int k = 0; k < dent_n; k++) {
        const struct truck_model *m = &truck_models[dent_model[k].model];
        // Skip pieces whose bounding ball is out of reach (units).
        float cx = m->center[0] / UNITS_PER_METER - P[0], cy = m->center[1] / UNITS_PER_METER - P[1], cz = m->center[2] / UNITS_PER_METER - P[2];
        float reach = m->radius / UNITS_PER_METER + 1.1f;
        if (cx * cx + cy * cy + cz * cz > reach * reach) continue;
        int moved_here = moved;
        for (int i = 0; i < m->npoints; i++) {
            float v[3], o[3], amt;
            const float *src = dent_orig[dent_model[k].first + i];
            for (int j = 0; j < 3; j++) { v[j] = m->points[i][j] / UNITS_PER_METER; o[j] = src[j] / UNITS_PER_METER; }
            if (!dent_vertex(v, o, P, D, s, &amt)) continue;
            for (int j = 0; j < 3; j++) m->points[i][j] = v[j] * UNITS_PER_METER;
            moved++;
            float e = sqrt_f((v[0] - o[0]) * (v[0] - o[0]) + (v[1] - o[1]) * (v[1] - o[1]) + (v[2] - o[2]) * (v[2] - o[2]));
            if (e > deepest) deepest = e;
            if (dent_model[k].window >= 0) glass_total[(int)dent_model[k].window] += amt;
        }
        if (moved != moved_here) dent_shade(k);
    }
    if (moved)
        PrintDebug("[SADX Truck] dent (%s): %d points pushed in, deepest now %.1f inches\n", why, moved, (double)(deepest * 39.37f));
    static const char *const wname[8] = { "windshield", "rear window", "left door window", "right door window",
                                          "left side window", "right side window", "left headlight lens", "right headlight lens" };
    for (int w = 0; w < 8; w++)
        if (!glass_cracked[w] && glass_total[w] > 10.0f) {
            glass_cracked[w] = 1;
            PrintDebug("[SADX Truck] %s cracked (dent total %.1f)\n", wname[w], (double)glass_total[w]);
        }
}

// An enemy hit (H1) at world point pw (meters): the attacker's centre can
// be well away from the body, so the dent goes at the body point nearest
// to it, pushed toward the truck's middle, with the 25-37 mph band's force
// (0.015; the same band as its 88 points of part damage).
static void dent_enemy(const float pw[3], const char *why)
{
    dent_backup();
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    float r[3] = { pw[0] - pos[0], pw[1] - pos[1], pw[2] - pos[2] }, a[3];
    for (int i = 0; i < 3; i++) a[i] = r[0] * rot[i * 3] + r[1] * rot[i * 3 + 1] + r[2] * rot[i * 3 + 2];
    float best = 1e30f, q[3] = { 0, 0, 0 };
    for (int k = 0; k < dent_n; k++) {
        const struct truck_model *m = &truck_models[dent_model[k].model];
        for (int i = 0; i < m->npoints; i++) {
            float v[3] = { m->points[i][0] / UNITS_PER_METER - a[0], m->points[i][1] / UNITS_PER_METER - a[1],
                           m->points[i][2] / UNITS_PER_METER - a[2] };
            float d2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (d2 < best) { best = d2; q[0] = v[0] + a[0]; q[1] = v[1] + a[1]; q[2] = v[2] + a[2]; }
        }
    }
    if (best > 1e29f) return;
    float l = sqrt_f(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    if (l < 1e-3f) return;
    float d[3] = { -q[0] / l, -q[1] / l, -q[2] / l }, wq[3], wd[3];
    for (int j = 0; j < 3; j++) {
        wq[j] = pos[j] + q[0] * rot[j] + q[1] * rot[3 + j] + q[2] * rot[6 + j];
        wd[j] = d[0] * rot[j] + d[1] * rot[3 + j] + d[2] * rot[6 + j];
    }
    dent_hit(wq, wd, dent_force(12.0f), why);
}

// ---- The parked truck: solid to stand on, and hit by Sonic's attacks ----
// From the SDK headers in sadx/programming (search for the names):
//   DynamicCOL_Add     SADXFunctions.h     0x43B2E0  a moving solid shape
//   DynamicCOL_Remove  SADXFunctions.h     0x43B380
//   ColFlags           SADXEnums.h         Solid (1 << 0), Dynamic (1 << 27):
//                                          stand on it, ride along when it moves
//   CCL_Init           SADXFunctionsNew.h  0x41CAF0  hit shapes for a task
//   EntryColliList     SADXFunctionsNew.h  0x41C280  enter them this frame
//   CCL_IsHitPlayer    SADXFunctionsNew.h  0x41CBC0  a player touched them
//   CID_ENEMY = 2      SADXEnumsNew.h      the list enemies use (Sonic's
//                                          attacks are checked against it)
// sonic.exe's EnemyCheckDamage (0x4CE030) counts a hit when the taskwk flag
// has (1 << 2) and CCL_IsHitPlayer finds the player; the same test here.
// Solid box: the body's start shape, as an axis box in truck space, its 8
// corners turned with the truck every frame (no rotation flag: the corners
// are already turned).  Both windings of each face, in case SADX's floor
// test only sees one side.  While the truck's own physics runs (OnFrame),
// the box is moved far away so the truck's ground checks (GetShadowPos)
// never find it.  Riding: kept far away (Sonic is inside).
#ifndef TEST_EXE
#define DynamicCOL_Add ((void (__cdecl *)(unsigned, char *, void *))0x43B2E0)
#define DynamicCOL_Remove ((void (__cdecl *)(char *, void *))0x43B380)
#define CCL_Init ((void (__cdecl *)(char *, void *, int, unsigned char))0x41CAF0)
#define EntryColliList ((void (__cdecl *)(char *))0x41C280)
#define CCL_IsHitPlayer ((char *(__cdecl *)(char *))0x41CBC0)
#endif
typedef struct { u32 eval; void *model; float pos[3]; int ang[3]; float scl[3]; void *child, *sibling; } nj_object;
typedef struct { char kind, form, push, damage; u32 attr; float center[3], a, b, c, d; int angx, angy, angz; } ccl_info;
// The solid shape: the body cut into SOLID_SLICES slices front to back, each a
// box as tall and wide as the body is in that slice (hood low, cab high),
// from the body's bottom up.  No bottom faces (a two-sided bottom made a
// ledge Sonic stood on beside the doors).
#define SOLID_SLICES 8
#define SOLID_BANDS 4
// Boxes: SOLID_SLICES front to back x SOLID_BANDS bottom to top, each as wide
// as the body is at that length and height (the Stalker's roof is 2.6 feet
// from the middle, its doors with mirrors 3.5: one width for all heights
// left a roof ledge Sonic stood on in mid-air).  Each face has its own 4
// corners carrying its normal, in both windings.  No bottom faces.
#define SOLID_BOXES (SOLID_SLICES * SOLID_BANDS)
#define SOLID_VERTS (SOLID_BOXES * 5 * 8)
static float solid_loc[SOLID_VERTS][3], solid_lnrm[SOLID_VERTS][3];   // truck space
static float solid_pts[SOLID_VERTS][3], solid_nrm[SOLID_VERTS][3];
static short solid_tris[SOLID_BOXES * 5 * 12];
static int solid_nverts;
static nj_meshset solid_set;
static nj_material solid_mat;
static nj_model solid_model;
static nj_object solid_obj;
static ccl_info solid_ccl[3];
static char *solid_task;
static float solid_lo[3], solid_hi[3];
static int solid_truck = -1, solid_shown;
static float solid_cool;
static void damage_at(const float p[3], float pts, const char *band);
static void solid_box(void)
{
    solid_truck = truck_now;
    static float bl[SOLID_BOXES], bh[SOLID_BOXES], btop[SOLID_BOXES];   // x low, x high, highest point
    for (int j = 0; j < 3; j++) { solid_lo[j] = 1e9f; solid_hi[j] = -1e9f; }
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1)
            for (int q = 0; q < SOLID_BOXES; q++) { bl[q] = 1e9f; bh[q] = -1e9f; btop[q] = -1e9f; }
        for (int k = 0; k < car.draws; k++) {
            const unsigned short *d = truck_draws[car.first_draw + k];
            if (d[1] != 0) continue;
            const struct truck_model *m = &truck_models[d[0]];
            int first = -1;
            for (int q = 0; q < dent_n; q++)
                if (dent_model[q].model == d[0]) { first = dent_model[q].first; break; }
            if (pass == 0) {
                for (int i = 0; i < m->npoints; i++) {
                    const float *v = first >= 0 ? dent_orig[first + i] : m->points[i];   // start shape
                    for (int j = 0; j < 3; j++) {
                        if (v[j] < solid_lo[j]) solid_lo[j] = v[j];
                        if (v[j] > solid_hi[j]) solid_hi[j] = v[j];
                    }
                }
                continue;
            }
            // Points spread over each triangle (big flat panels like the
            // doors have corners only at their edges).
            for (int tr = 0; tr < m->ntris; tr++)
              for (int su = 0; su <= 8; su++)
                for (int sv = 0; sv <= 8 - su; sv++) {
                const float *A, *B, *C;
                {
                    int ia = m->tris[tr * 3], ib = m->tris[tr * 3 + 1], ic = m->tris[tr * 3 + 2];
                    A = first >= 0 ? dent_orig[first + ia] : m->points[ia];
                    B = first >= 0 ? dent_orig[first + ib] : m->points[ib];
                    C = first >= 0 ? dent_orig[first + ic] : m->points[ic];
                }
                float fu = su / 8.0f, fv = sv / 8.0f, v[3];
                for (int j = 0; j < 3; j++) v[j] = A[j] + fu * (B[j] - A[j]) + fv * (C[j] - A[j]);
                int zs = (int)((v[2] - solid_lo[2]) / (solid_hi[2] - solid_lo[2] + 1e-3f) * SOLID_SLICES);
                int ys = (int)((v[1] - solid_lo[1]) / (solid_hi[1] - solid_lo[1] + 1e-3f) * SOLID_BANDS);
                if (zs < 0) zs = 0;
                if (zs >= SOLID_SLICES) zs = SOLID_SLICES - 1;
                if (ys < 0) ys = 0;
                if (ys >= SOLID_BANDS) ys = SOLID_BANDS - 1;
                int q = zs * SOLID_BANDS + ys;
                if (v[0] < bl[q]) bl[q] = v[0];
                if (v[0] > bh[q]) bh[q] = v[0];
                if (v[1] > btop[q]) btop[q] = v[1];
            }
        }
    }
    // top (y high), left (x low), right (x high), back (z low), front (z high)
    static const short faces[5][4] = { { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
    static const float fnorm[5][3] = { { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    int t = 0, nv = 0;
    for (int zs = 0; zs < SOLID_SLICES; zs++)
        for (int ys = 0; ys < SOLID_BANDS; ys++) {
            int q = zs * SOLID_BANDS + ys;
            if (bh[q] < bl[q]) continue;                       // nothing of the body here
            float z0 = solid_lo[2] + (solid_hi[2] - solid_lo[2]) * zs / SOLID_SLICES;
            float z1 = solid_lo[2] + (solid_hi[2] - solid_lo[2]) * (zs + 1) / SOLID_SLICES;
            float y0 = solid_lo[1] + (solid_hi[1] - solid_lo[1]) * ys / SOLID_BANDS;
            float y1 = solid_lo[1] + (solid_hi[1] - solid_lo[1]) * (ys + 1) / SOLID_BANDS;
            // A band with body above it is filled to its top; the highest
            // band with body stops at the body's highest point there.
            int above = 0;
            for (int u = ys + 1; u < SOLID_BANDS; u++) if (bh[zs * SOLID_BANDS + u] >= bl[zs * SOLID_BANDS + u]) above = 1;
            if (!above) y1 = btop[q];
            float corner[8][3];
            for (int i = 0; i < 8; i++) {
                corner[i][0] = i & 1 ? bh[q] : bl[q];
                corner[i][1] = i & 2 ? y1 : y0;
                corner[i][2] = i & 4 ? z1 : z0;
            }
            // One side only, facing out.  SADX's front side is cross(B - A,
            // C - A) of a triangle's corners: in two level files (Extended
            // Levels' Twinkle Park, Iron Jungle's Speed Highway) floors face up
            // that way 2612 to 507 and 4341 to 2192 (the rest: ceilings).
            // Two-sided faces let Sonic land on the walls' back sides (the
            // camera rolled with him).
            for (int f = 0; f < 5; f++) {
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 3; j++) {
                        solid_loc[nv + i][j] = corner[faces[f][i]][j];
                        solid_lnrm[nv + i][j] = fnorm[f][j];
                    }
                short a[6] = { (short)nv, (short)(nv + 1), (short)(nv + 2), (short)nv, (short)(nv + 2), (short)(nv + 3) };
                const float *A = solid_loc[nv], *B = solid_loc[nv + 1], *C = solid_loc[nv + 2];
                float u[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, w[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
                float cr[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
                if (cr[0] * fnorm[f][0] + cr[1] * fnorm[f][1] + cr[2] * fnorm[f][2] < 0) {
                    short x = a[1]; a[1] = a[2]; a[2] = x; x = a[4]; a[4] = a[5]; a[5] = x;
                }
                for (int i = 0; i < 6; i++) solid_tris[t + i] = a[i];
                t += 6; nv += 4;
            }
        }
    solid_nverts = nv;
    solid_set.type_mat = 0; solid_set.n = (unsigned short)(t / 3); solid_set.meshes = solid_tris;
    solid_model.points = solid_pts; solid_model.normals = solid_nrm; solid_model.npoints = nv;
    solid_model.sets = &solid_set; solid_model.mats = &solid_mat; solid_model.nsets = 1; solid_model.nmats = 1;
    float hx = (solid_hi[0] - solid_lo[0]) * 0.5f, hy = (solid_hi[1] - solid_lo[1]) * 0.5f, hz = (solid_hi[2] - solid_lo[2]) * 0.5f;
    solid_model.r = sqrt_f(hx * hx + hy * hy + hz * hz) + 5.0f;
    solid_obj.model = &solid_model;
    solid_obj.scl[0] = solid_obj.scl[1] = solid_obj.scl[2] = 1.0f;
    PrintDebug("[SADX Truck] solid shape for %s (one-sided): %d boxes; half width by height band, middle slice (feet):", car.name, t / 30);
    for (int ys = 0; ys < SOLID_BANDS; ys++) {
        int q = (SOLID_SLICES / 2) * SOLID_BANDS + ys;
        PrintDebug(" %.1f", bh[q] >= bl[q] ? (double)((bh[q] - bl[q]) * 0.5f / UNITS_PER_METER * 3.281f) : 0.0);
    }
    PrintDebug("\n");
}
// Place the shape on the truck (show) or far away (hide).
static void solid_place(int show)
{
    solid_shown = show;
    if (!show || !truck) { solid_obj.pos[0] = 0; solid_obj.pos[1] = -1000000.0f; solid_obj.pos[2] = 0; return; }
    if (solid_truck != truck_now) solid_box();
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    solid_obj.pos[0] = origin.x + pos[0] * UNITS_PER_METER;
    solid_obj.pos[1] = origin.y + pos[1] * UNITS_PER_METER;
    solid_obj.pos[2] = origin.z + pos[2] * UNITS_PER_METER;
    float c[3] = { 0, 0, 0 };
    for (int i = 0; i < solid_nverts; i++) {
        const float *l = solid_loc[i], *n = solid_lnrm[i];
        for (int j = 0; j < 3; j++) {
            solid_pts[i][j] = l[0] * rot[j] + l[1] * rot[3 + j] + l[2] * rot[6 + j];
            solid_nrm[i][j] = n[0] * rot[j] + n[1] * rot[3 + j] + n[2] * rot[6 + j];
            c[j] += solid_pts[i][j] / (solid_nverts ? solid_nverts : 1);
        }
    }
    solid_model.center[0] = c[0]; solid_model.center[1] = c[1]; solid_model.center[2] = c[2];
    // Hit shapes: 3 balls along the truck (front, middle, back), kept below
    // the roof so landing on top is not an attack (a jump counts as one).
    float hz = (solid_hi[2] - solid_lo[2]) * 0.5f, mz = (solid_hi[2] + solid_lo[2]) * 0.5f;
    float h = solid_hi[1] - solid_lo[1], w = solid_hi[0] - solid_lo[0], mx = (solid_hi[0] + solid_lo[0]) * 0.5f;
    float r = (w < h ? w : h) * 0.4f;
    float my = solid_lo[1] + r;
    for (int s = 0; s < 3; s++) {
        float l[3] = { mx, my, mz + (s - 1) * (hz - r) };
        for (int j = 0; j < 3; j++) solid_ccl[s].center[j] = l[0] * rot[j] + l[1] * rot[3 + j] + l[2] * rot[6 + j];
        solid_ccl[s].a = r;
    }
}
static void __cdecl solid_task_dest(char *tp)
{
    DynamicCOL_Remove(tp, &solid_obj);
    solid_task = 0;                     // SADX freed it (level change)
}
// Slide the parked truck sideways by (dx, dz) meters (an enemy's push).
// SetPosition (cPhysVehicle, as at spawn) places the body; speed kept.
static void push_truck(float dx, float dz)
{
    const float *pos = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x28);
    tVect3 to = { pos[0] + dx, pos[1], pos[2] + dz };
    SetPosition(truck, &to);
    ActivateObject(truck);              // wake it, so it settles on the ground
}
static float solid_wrote[3];            // twp pos as solid_task_exec last wrote it
static int solid_wrote_ok;
static void __cdecl solid_task_exec(char *tp)
{
    char *twp = *(char **)(tp + 32);
    if (!twp || !solid_shown || !truck) return;
    float *tpos = (float *)(twp + 32);
    // P2: SADX's push (sonic.exe 0x4195A0) moves twp pos straight, flat
    // (y unchanged), by 0.49 x the overlap.  What moved since this exec
    // last wrote it is the push; move the truck by it.  At most 1.5 feet
    // a frame, in case something else moved the task.
    if (solid_wrote_ok && !riding) {
        float dx = (tpos[0] - solid_wrote[0]) / UNITS_PER_METER, dz = (tpos[2] - solid_wrote[2]) / UNITS_PER_METER;
        float d2 = dx * dx + dz * dz;
        if (d2 > 1e-8f && d2 < 0.46f * 0.46f) {
            static int quiet;
            if (quiet > 0) quiet--;
            if (!quiet) {
                PrintDebug("[SADX Truck] parked truck pushed %.2f inches\n", (double)(sqrt_f(d2) * 39.37f));
                quiet = 60;
            }
            push_truck(dx, dz);
        }
    }
    tpos[0] = solid_obj.pos[0]; tpos[1] = solid_obj.pos[1]; tpos[2] = solid_obj.pos[2];
    solid_wrote[0] = tpos[0]; solid_wrote[1] = tpos[1]; solid_wrote[2] = tpos[2]; solid_wrote_ok = 1;
    char *cwp = *(char **)(twp + 56);
    if (cwp) {
        float hz = (solid_hi[2] - solid_lo[2]) * 0.5f, hy = (solid_hi[1] - solid_lo[1]) * 0.5f, hx = (solid_hi[0] - solid_lo[0]) * 0.5f;
        *(float *)(cwp + 8) = sqrt_f(hx * hx + hy * hy + hz * hz) + 20.0f;   // colli_range
    }
    if (solid_cool > 0) solid_cool -= 1.0f / 60.0f;
    // Hit last frame by a player's attack?
    short *flag = (short *)(twp + 4);
    char *touch = CCL_IsHitPlayer(twp);
    char *pl = (*flag & (1 << 2)) ? touch : 0;
    {
        // Check log (the attack test is unsure): a touch, with the flag
        // and Sonic's attack bits (his shape 0's damage byte, bits 0-1),
        // at most once a second.
        static int quiet;
        if (quiet > 0) quiet--;
        if (touch && !quiet) {
            const char *pcwp = *(const char *const *)(touch + 56);
            const unsigned char *pc = pcwp && *(const unsigned short *)(pcwp + 6) ? *(const unsigned char *const *)(pcwp + 12) : 0;
            PrintDebug("[SADX Truck] hit shape touched: flag %d, (1 << 2) %s, Sonic's attack %d, mode %d\n", *flag,
                       (*flag & (1 << 2)) ? "set" : "not set", pc ? pc[3] & 3 : -1, (int)*(const unsigned char *)touch);
            quiet = 60;
        }
    }
    *flag &= ~(1 << 2);
    if (pl && solid_cool <= 0) {
        solid_cool = 0.5f;
        const float *wp = (const float *)(pl + 32);
        float p[3] = { (wp[0] - origin.x) / UNITS_PER_METER, (wp[1] - origin.y) / UNITS_PER_METER, (wp[2] - origin.z) / UNITS_PER_METER };
        PrintDebug("[SADX Truck] Sonic attacked the truck\n");
        damage_at(p, 88.0f, "Sonic's attack");
        dent_enemy(p, "Sonic's attack");
    }
    // Touched by an enemy last frame?  sonic.exe 0x418E70 writes each
    // touch into colliwk.hit_info (16 slots of 8 bytes at +16: hit_num
    // +1, -1 = empty; hit_twp +4) and EntryColliList clears them, so read
    // them first.  Only a new touch counts (an enemy standing against the
    // truck dents it once), and at most one every 0.5 s.
    {
        // Hit ids: 1 bullets (enemy shots), 3 enemies (Leon, Kiki,
        // Rhinotank).  Not 4 or 5: level objects (sonic.exe 0x420560
        // checks id 3 against them too), which touch a truck parked
        // against them over and over.  A hitter is remembered for 1 s
        // after its last touch, so a flickering touch counts once.
        static char *was[16]; static int was_t[16];
        for (int j = 0; j < 16; j++) if (was[j] && --was_t[j] <= 0) was[j] = 0;
        for (int i = 0; cwp && i < 16; i++) {
            const char *hi = cwp + 16 + i * 8;
            char *o = *(char *const *)(hi + 4);
            if (hi[1] == -1 || !o) continue;
            const char *ocwp = *(const char *const *)(o + 56);
            int id = ocwp ? *(const unsigned short *)ocwp : 0;
            if (id != 1 && id != 3) continue;
            int seen = -1, freej = -1;
            for (int j = 0; j < 16; j++) { if (was[j] == o) seen = j; if (!was[j] && freej < 0) freej = j; }
            if (seen >= 0) { was_t[seen] = 60; continue; }
            if (freej >= 0) { was[freej] = o; was_t[freej] = 60; }
            if (solid_cool > 0) continue;
            solid_cool = 0.5f;
            const float *wp = (const float *)(o + 32);
            float p[3] = { (wp[0] - origin.x) / UNITS_PER_METER, (wp[1] - origin.y) / UNITS_PER_METER, (wp[2] - origin.z) / UNITS_PER_METER };
            const char *otask = *(const char *const *)(ocwp + 156);              // colliwk.mytask
            PrintDebug("[SADX Truck] an enemy (hit id %d, exec %08X) hit the parked truck\n", id,
                       otask ? *(const unsigned *)(otask + 16) : 0u);
            damage_at(p, 88.0f, "enemy hit, parked");
            dent_enemy(p, "enemy hit, parked");
        }
    }
    EntryColliList(twp);
}
static void solid_frame(void)
{
    if (!solid_task) {
        solid_task = CreateElementalTask(2, 3, solid_task_exec);     // 2: with a taskwk (LoadObj_Data1)
        if (!solid_task) return;
        *(void (__cdecl **)(char *))(solid_task + 24) = solid_task_dest;
        // attr (1 << 10): can be hurt (sonic.exe 0x418DE0 only marks a
        // shape hit when its attr has it); damage 0: hurts no one.
        // push (1 << 4): Sonic and Tails can't push it, (1 << 6) objects,
        // (1 << 7) items can't; enemies can (bit 5 clear; sonic.exe 0x418D80).
        for (int s = 0; s < 3; s++) {
            solid_ccl[s].form = 0; solid_ccl[s].a = 10.0f; solid_ccl[s].attr = 1u << 10;
            solid_ccl[s].push = (char)((1u << 4) | (1u << 6) | (1u << 7));
        }
        // id 3 (CID_ENEMY2, like Leon, Kiki, Rhinotank): sonic.exe 0x420560
        // checks id 3 against id 3, id 2 is only checked against the player.
        CCL_Init(solid_task, solid_ccl, 3, 3);
        if (solid_truck != truck_now) solid_box();   // DynamicCOL_Add reads the model's centre (0x43B2F9)
        solid_place(0);
        DynamicCOL_Add((1u << 0) | (1u << 27), solid_task, &solid_obj);
        own_solid = (const char *)&solid_obj;
        PrintDebug("[SADX Truck] solid box and hit shapes made\n");
    }
}

static float hit_points(float speed)
{
    return speed < 1.0f ? 0 : speed < 5.55f ? 25 : speed < 11.11f ? 35 : speed < 16.67f ? 50 : speed < 21.0f ? 100 : 350;
}

static void damage_at(const float p[3], float pts, const char *band);
static void detect_hits(float dt)
{
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *vel = (const float *)(body + 0x34), *rot = (const float *)(body + 0x4c);
    const float *pos = (const float *)(body + 0x28);
    float speed = contact_speed;
    contact_speed = 0;
    if (hit_cool > 0) hit_cool -= dt;
    // Air time, and the falling speed just before the wheels touch.
    // A sleeping truck (parked, out of EngineDll's active list) has no
    // wheel contacts, but it is not in the air.  As Cabela's (GameDll
    // 0x100274E0: car+0x758 bit (1 << 15) only with no wheel touching and
    // the body not in contact, else the air timer car+0x5CC goes back to
    // 0): body contact counts too (box_hits_now, the body corner rays), so
    // a truck sliding on its roof or side is not flying.
    int airborne = hits_now == 0 && box_hits_now == 0 && IsInActiveList(truck);
    float was_air = air_t;
    air_t = airborne ? air_t + dt : 0;
    if (!have_prev) { have_prev = 1; land_vy = 0; return; }
    if (!airborne && was_air > 1.0f) {
        float ls = land_vy > speed ? land_vy : speed;
        // GameDll 0x1001EB40: points x 0.02 (ground below), cut to a whole
        // number (_ftol), then each part loses points x m x 2, where
        // m = speed / 21 m/s above 21, else air seconds + 2.  Then times
        // the LandingStrength setting (x1 = Cabela's; parts only, dents
        // have DentStrength).  Under 25 mph points x 0.02 is still 0.
        float f = ls > 21.0f ? ls / 21.0f : was_air + 2.0f, pts = (float)(int)(hit_points(ls) * 0.02f);
        PrintDebug("[SADX Truck] hard landing after %.1f s in the air, %.1f mph\n", (double)was_air, (double)(ls * 2.23694f));
        if (pts > 0) for (int k = 0; k < 18; k++) damage_part(k, (float)(int)(2.0f * pts * f) * (float)land_strength, "hard landing");
        // Dents at each wheel's hub, pushed up (Cabela's landing calls
        // Deform at each wheel's position, 0x1001DE50).  Force, as in
        // GameDll 0x1001EB40: the band's force x 0.02 (hit from below)
        // x m, the same multiplier as the parts (0x1001EF4E to 0x1001EF5A).
        {
            float lf = dent_force(ls) * 0.02f * f;
            const float *up = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x4c) + 3;
            for (u32 w = 0; w < 4; w++) {
                m4x4 t; GetWheelTM(truck, &t, w);
                float hub[3] = { t.m[12], t.m[13], t.m[14] };
                dent_hit(hub, up, lf, "hard landing");
            }
        }
        land_vy = 0;
        return;
    }
    land_vy = vel[1] < 0 ? -vel[1] : 0;
    float pts = hit_points(speed);
    int was_touching = touching;
    touching = pts > 0;
    if (pts <= 0) return;
    // Scraping: a contact that goes on from last frame, or one within
    // 0.75 s of the last hit, counts only when 1 m/s (about 2 mph) harder
    // than that hit (Station Square log: 37 hits in 2 s from one scrape).
    if ((hit_cool > 0 || was_touching) && speed < last_hit_speed + 1.0f) return;
    // Cabela's repeat rule (GameDll 0x1001EB40, car+0x5D4): a harder hit
    // within 0.75 s of the last one dents with 0.1 more force each time;
    // after 0.75 s it is back to 1.
    static float dent_mult = 1.0f;
    dent_mult = hit_cool > 0 ? dent_mult + 0.1f : 1.0f;
    hit_cool = 0.75f;
    hit_count++;
    last_hit_mph = (int)(speed * 2.23694f);   // m per second -> mph
    last_hit_speed = speed;
    // The surface's facing in truck space (Cabela's: normal y above 0.95 =
    // from below, x 0.02; below -0.3 = on the roof, x 2.5).
    float ny = contact_n[0] * rot[3] + contact_n[1] * rot[4] + contact_n[2] * rot[5];
    float face = ny > 0.95f ? 0.02f : ny < -0.3f ? 2.5f : 1.0f;
    pts *= face;
    dent_hit(contact_pt, contact_n, dent_force(speed) * face * dent_mult, dent_mult > 1.0f ? "repeat hit" : "hit");
    const char *band = speed < 5.55f ? "hit under 12 mph" : speed < 11.11f ? "hit 12-25 mph" :
                       speed < 16.67f ? "hit 25-37 mph" : speed < 21.0f ? "hit 37-47 mph" : "hit over 47 mph";
    damage_at(contact_pt, pts, band);
}

// Damage from a hit at world point p (EngineDll meters): the parts on that
// side of the truck, by Cabela's zones.
static void damage_at(const float p[3], float pts, const char *band)
{
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
    // Point in truck space: rows of rot are the truck's axes.
    float d[3] = { p[0] - pos[0], p[1] - pos[1], p[2] - pos[2] };
    float x = d[0] * rot[0] + d[1] * rot[1] + d[2] * rot[2];
    float y = d[0] * rot[3] + d[1] * rot[4] + d[2] * rot[5];
    float z = d[0] * rot[6] + d[1] * rot[7] + d[2] * rot[8];
    int front = z > car.tire[0].z * 0.5f, rear = z < car.tire[2].z * 0.5f;
    float half = (car.tire[1].x > 0 ? car.tire[1].x : -car.tire[1].x) * 0.5f;
    int side = x < -half ? 0 : x > half ? 1 : -1;            // 0 Cabela's left, 1 right, -1 centre
    int low = y < car.spring[0].y;
    if ((front || rear) && side >= 0) {
        int w = (rear ? 2 : 0) + side;                       // wheel 0-3
        if (low) damage_part(w, pts, band);
        damage_part(10 + w, pts, band);                      // light
        damage_part(4 + w, pts, band);                       // body corner
    } else if (side >= 0) {
        damage_part(8 + side, pts, band);                    // door
    } else if (front || (!rear && !low)) {
        damage_part(10, pts * 0.5f, band); damage_part(11, pts * 0.5f, band);
        damage_part(16, pts, band);                          // radiator
    } else if (rear) {
        if (!low) { damage_part(12, pts * 0.5f, band); damage_part(13, pts * 0.5f, band); damage_part(17, pts, band); }
    } else {
        damage_part(14, pts, band);                          // underneath: transmission
    }
}

// Effects of the parts (Cabela's 3):
//   wheels, level 2 and up: grip = adherence x (1 - (level - 1) x 0.05)
//     (WheelsPenality 0x100236B0); each wheel's grip at its byte 244
//   transmission: level 3 warning, level 4 no drive (0x10023830)
//   radiator: every 2 s the temperature gauge moves toward
//     level x 0.25 - 0.03 (UpdateCarWaterTemp 0x100207A0); above 0.7 the
//     engine loses (gauge - 0.7) x 200 points.  How fast the gauge moves
//     is not known; here 0.05 a tick, and only while the engine runs (it
//     cools toward 0 otherwise).  Steam above 0.7.
//   lights, body, doors, rear end: the HUD only (the mod draws no lamps)
static float radiator_tick;

static void part_effects(float dt)
{
    char *t = (char *)truck;
    for (int w = 0; w < 4; w++) {
        int l = part_level(w);
        float grip = car.adherence * (l >= 4 ? 0.1f : l >= 2 ? 1.0f - (l - 1) * 0.05f : 1.0f) * (cold_here() ? COLD_GRIP : 1.0f);
        *(float *)(((char **)*(void **)(t + 0xcc))[w] + 0xf4) = grip;
    }
    trans_broken = part_level(14) >= 4;
    if (headlights && eng != ENG_RUNNING) {
        battery -= 0.0003f;
        if (battery < 1.0f) battery = 1.0f;
    }
    radiator_tick += dt;
    if (radiator_tick >= 2.01f) {
        radiator_tick = 0;
        float target = eng == ENG_RUNNING ? part_level(16) * 0.25f - 0.03f : 0.0f;
        if (radiator_gauge < target) radiator_gauge += 0.05f;
        else if (radiator_gauge > target) radiator_gauge -= 0.05f;
        if (radiator_gauge > 0.7f && part_level(15) != part_level(16)) {
            damage_part(15, (radiator_gauge - 0.7f) * 200.0f, "overheating");
            if (steam_t <= 0) steam_t = 8.0f;
        }
    }
    for (int k = 0; k < 18; k++) {
        int l = part_level(k);
        if (part_lvl_shown[k] < 0) { part_lvl_shown[k] = l; continue; }
        if (l == 4 && part_lvl_shown[k] < 4 && (k < 4 || k == 8 || k == 9 || k == 16 || k == 17)) {
            break_pending[k] = 1;
            PrintDebug("[SADX Truck] %s %s\n", k == 16 ? "front bumper" : k == 17 ? "rear bumper" : part_names[k],
                       k < 4 ? "lost its tire (that corner: 10% grip)" : "fell off");
        }
        if (l > part_lvl_shown[k] && k != 15) {
            static const char *const color[] = { "green", "yellow", "orange", "red", "black" };
            PrintDebug("[SADX Truck] %s damage level %d (%s)%s\n", part_names[k], l, color[l],
                       k == 14 && l == 3 ? ": DAMAGE WARNING!" : k == 14 && l == 4 ? ": the transmission is broken, no drive" :
                       k < 4 && l >= 2 ? ": less grip" : "");
        }
        part_lvl_shown[k] = l;
    }
}


// Lamps (the WhiteShader lamp pieces, skin_to_c.py PIECE_TAGS):
//   headlights and tail lights: D-pad up while driving turns them on / off
//   brake lights: while braking (in reverse that is the gas pedal)
//   reverse lights: in reverse
//   a light at level 3 (red) or worse stays dark (Cabela's 3,
//   FUN_10023830: lamp flags 0x80 to 0x400 cleared from level 3)
// Headlights with the engine off drain the battery (0.0003 V a frame,
// 12 V to 6 V in about 5 1/2 minutes).
// Loose pieces: doors, front bumper (radiator), rear bumper (rear end)
// fall off when their part turns black (level 4), as in Cabela's
// (GameDll 0x10021650 at level 4), tumble to the ground and lie there.
#define MAX_FALLING 24
static struct { int model, rest, wheel; float m[16], v[3], spin; } falling[MAX_FALLING];

// Texture 14 = the tire (skin_to_c.py TEX_TIRE): a black wheel loses its
// tire, the rim stays on the hub (Cabela's 3, seen in Joshua's video).
#define TEX_TIRE_PIECE 14
static int piece_shown(int frame, int model, int part, int lamp)
{
    if (lamp >= GLASS_WHOLE) {                  // windows: the whole or the cracked copy
        if (glass_cracked[lamp & 7] != (lamp >= GLASS_CRACKED)) return 0;
        return part == 255 || part_level(part) < 4;
    }
    if (frame >= 1 && truck_models[model].tex == TEX_TIRE_PIECE && part_level(frame - 1) >= 4) return 0;
    if (part == 255) return 1;
    if (!lamp) return part_level(part) < 4;
    if (part_level(part) >= 3) return 0;
    return lamp <= 2 ? headlights : lamp == 3 ? braking : reversing;
}

// Pieces whose part turned black this frame start falling from where they
// are on the truck (body matrix, SADX units), pushed outward and up.
// Wheels (parts 0 to 3): at black the tire leaves (Cabela's 3 does this:
// in Joshua's video a black wheel's corner shows only the rim on the hub;
// GameDll 0x10021650 at level 4, not read) from the wheel's own matrix and
// rolls away; the rim stays.  The physics wheel stays (EngineDll cannot
// drop one), with 10% grip (the mod's guess).
static void falling_start(const m4x4 tms[5])
{
    const float *body_tm = tms[0].m;
    const float *vel = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x34);
    for (int k = 0; k < 18; k++) {
        if (!break_pending[k]) continue;
        break_pending[k] = 0;
        for (int i = 0; i < car.draws && falling_n < MAX_FALLING; i++) {
            const unsigned short *d = truck_draws[car.first_draw + i];
            int wheel = k < 4;
            if (wheel ? (d[1] != 1 + k || truck_models[d[0]].tex != TEX_TIRE_PIECE) : (d[2] != k || (d[3] && d[3] < GLASS_WHOLE))) continue;
            if (d[3] >= GLASS_WHOLE && glass_cracked[d[3] & 7] != (d[3] >= GLASS_CRACKED)) continue;
            const float *src = wheel ? tms[1 + k].m : body_tm;
            float cx = wheel ? car.tire[k].x : truck_models[d[0]].center[0];
            float side = cx > 0 ? 1.0f : -1.0f;
            int f = falling_n++;
            for (int j = 0; j < 16; j++) falling[f].m[j] = src[j];
            falling[f].model = d[0];
            falling[f].rest = 0;
            falling[f].wheel = wheel;
            for (int j = 0; j < 3; j++)
                falling[f].v[j] = vel[j] * UNITS_PER_METER + body_tm[j] * side * 15.0f + body_tm[4 + j] * (wheel ? 8.0f : 20.0f);
            falling[f].spin = wheel ? 0.0f : (rnd(2) ? 3.0f : -3.0f);
        }
    }
}

// Move and draw the fallen pieces: gravity 9.81 m/s2 (98.1 units), a
// tumble about the piece's own x axis through its middle, stop on the
// ground (GetShadowPos).
static void falling_draw(void)
{
    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < falling_n; f++) {
        float *m = falling[f].m;
        const float *c = truck_models[falling[f].model].center;
        if (!falling[f].rest) {
            falling[f].v[1] -= 98.1f * dt;
            float a = falling[f].spin * dt, ca = cos_f(a), sa = sin_f(a);
            float before[3], after[3];
            for (int j = 0; j < 3; j++) before[j] = c[1] * m[4 + j] + c[2] * m[8 + j];
            for (int j = 0; j < 3; j++) {
                float r1 = m[4 + j], r2 = m[8 + j];
                m[4 + j] = r1 * ca + r2 * sa;
                m[8 + j] = -r1 * sa + r2 * ca;
            }
            for (int j = 0; j < 3; j++) {
                after[j] = c[1] * m[4 + j] + c[2] * m[8 + j];
                m[12 + j] += before[j] - after[j] + falling[f].v[j] * dt;
            }
            float w[3];
            for (int j = 0; j < 3; j++) w[j] = c[0] * m[j] + c[1] * m[4 + j] + c[2] * m[8 + j] + m[12 + j];
            int ang[3];
            float g = GetShadowPos(w[0], w[1] + 20.0f, w[2], ang);
            if (falling[f].wheel) {
                // A wheel rolls: on the ground it keeps going, slowing a
                // little each frame, turning about its axle (local x) at
                // speed / radius; it stops under 0.5 m/s.
                float r = truck_models[falling[f].model].radius;
                if (r < 2.0f) r = 2.0f;
                if (w[1] <= g + r) {
                    m[13] += g + r - w[1];
                    if (falling[f].v[1] < 0) falling[f].v[1] = 0;
                    falling[f].v[0] *= 0.985f; falling[f].v[2] *= 0.985f;
                    float sp = sqrt_f(falling[f].v[0] * falling[f].v[0] + falling[f].v[2] * falling[f].v[2]);
                    float fwd = falling[f].v[0] * m[8] + falling[f].v[2] * m[10];
                    falling[f].spin = (fwd >= 0 ? -sp : sp) / r;
                    if (sp < 5.0f) falling[f].rest = 1;
                }
            } else if (falling[f].v[1] < 0 && w[1] <= g + 2.0f) falling[f].rest = 1;
        }
        njPushMatrixEx();
        nj_apply(m);
        njDrawModel_Dynamic(&nj_models[falling[f].model]);
        njPopMatrixEx();
    }
}

// Damage HUD, Cabela's 3's own (HUDLOADER.LUA "Body Damage", artwork from
// HUD.TGA, cut up by skin_to_c.py hud_images): a grey panel, the whole
// schematic in green, then each part in its level's colour (SetDamageColor
// in CARS.LUA: green, yellow, orange, red, black).  Place: Cabela's
// SetScreenRect(550, 25, 620, 130) in 640 x 480 units, top right.
// Laid out like Joshua's GTA V DamageHud.cs too: always on while driving.
#define HUD_X0 550
#define HUD_Y0 25
#define HUD_W 70
#define HUD_H 105
static void draw_hud(void)
{
    static const float color[5][3] = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.5f, 0.0f },
                                        { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    static struct { short sx, sy, cx, cy, u1, v1, u2, v2, texid, attr; } anim[20];
    static struct { float p[3], sx, sy; int ang; void *tlist, *tanim; } sprite[20];
    float scale = screen_h / 480.0f;
    float left = screen_w - (640 - HUD_X0) * scale;    // right-aligned like Cabela's
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    for (int i = 0; i < 20; i++) {
        // 0 panel (8 x 8, 4 units bigger all round), 1 schematic, 2-19 parts
        int tw = i == 0 ? 8 : 128, th = i == 0 ? 8 : 256;
        float w = i == 0 ? HUD_W + 8.0f : HUD_W, h = i == 0 ? HUD_H + 8.0f : HUD_H;
        anim[i].sx = (short)tw; anim[i].sy = (short)th; anim[i].cx = (short)(tw / 2); anim[i].cy = (short)(th / 2);
        anim[i].u1 = 0; anim[i].v1 = 0; anim[i].u2 = 255; anim[i].v2 = 255;
        anim[i].texid = (short)(TRUCK_TEX_HUD + i); anim[i].attr = 0;
        sprite[i].p[0] = left + (HUD_W * 0.5f) * scale;
        sprite[i].p[1] = (HUD_Y0 + HUD_H * 0.5f) * scale;
        sprite[i].p[2] = 0;
        sprite[i].sx = w / tw * scale;
        sprite[i].sy = h / th * scale;
        sprite[i].ang = 0;
        sprite[i].tlist = &truck_texlist; sprite[i].tanim = &anim[i];
        if (i == 0) SetMaterialAndSpriteColor_Float(0.45f, 0.55f, 0.55f, 0.6f);      // a, r, g, b
        else {
            int l = i == 1 ? 0 : part_level(i - 2);
            SetMaterialAndSpriteColor_Float(1.0f, color[l][0], color[l][1], color[l][2]);
        }
        njDrawSprite2D_ForcePriority(&sprite[i], 0, -1.5f, (1u << 5) | (1u << 4) | (1u << 1));
    }
    ResetMaterial();
}

// Gauges, Cabela's 3's own (HUDLOADER.LUA; artwork from HUD.TGA, cut by
// skin_to_c.py gauge_images).  Places in 640 x 480 units, right-aligned
// like the damage HUD: "BORD Indicator" (370, 310)-(620, 460) holds the
// tachometer (0 to 7000 RPM) and speedometer (0 to 100 mph); needles at
// SetRPMData's (421, 411), 40 long, 15 wide and SetVelocityData's (543,
// 383), 60 long, 20 wide.  Needle sweep, measured off the art: tach 0 at
// -135 degrees (up = 0, clockwise) to 7000 at +135; speedo 0 at -130 to
// 100 mph at +130.  Bars (440 / 475 / 510, 25)-(+25, 100): fuel, oil
// (the mod: engine health), water (the radiator gauge), icons under them
// (+5, 102)-(+30, 127).  Traction: a digit under the damage panel,
// 2 white = 2x4, 4 green = 4x4, 4 orange = 4x4 Low.
// Text and odometer as in Cabela's (Joshua's screenshot gauges.png,
// measured against the speedometer; that screenshot is 4:3 stretched
// to 16:9, so across 3.51 pixels a unit, down 2.65): "T:" from x 462,
// its value from 483, "G:" from 547, its value from 569, letter middles
// 69 units under the needle's knob (y 452, level with the speedometer's
// bottom tab), letters 9.4 units tall, FontLarge; the odometer's 5
// digits over (520, 420)-(563, 431), under "MPH", the last one tenths of
// a mile.
static void draw_gauges(void)
{
    static struct { short sx, sy, cx, cy, u1, v1, u2, v2, texid, attr; } anim[160];
    static struct { float p[3], sx, sy; int ang; void *tlist, *tanim; } sprite[160];
    float scale = screen_h / 480.0f, right = screen_w - 640.0f * scale;
    int n = 0;
    // One sprite: texture (tw x th pixels) drawn over (x, y)-(x+w, y+h) in
    // 640 units, turned deg degrees about the point (px, py) of the
    // texture (0 to 1).
#define GAUGE(tex, tw, th, x, y, w, h, px, py, deg, a, r, g, b) do {                                  \
        anim[n].sx = (short)(tw); anim[n].sy = (short)(th);                                             \
        anim[n].cx = (short)((px) * (tw)); anim[n].cy = (short)((py) * (th));                           \
        anim[n].u1 = 0; anim[n].v1 = 0; anim[n].u2 = 255; anim[n].v2 = 255;                             \
        anim[n].texid = (short)(tex); anim[n].attr = 0;                                                 \
        sprite[n].p[0] = right + ((x) + (px) * (w)) * scale; sprite[n].p[1] = ((y) + (py) * (h)) * scale; \
        sprite[n].p[2] = 0;                                                                             \
        sprite[n].sx = (w) / (float)(tw) * scale; sprite[n].sy = (h) / (float)(th) * scale;             \
        sprite[n].ang = (int)((deg) * (65536.0f / 360.0f));                                             \
        sprite[n].tlist = &truck_texlist; sprite[n].tanim = &anim[n];                                   \
        SetMaterialAndSpriteColor_Float(a, r, g, b);                                                    \
        njDrawSprite2D_ForcePriority(&sprite[n], 0, -1.5f, (1u << 5) | (1u << 4) | (1u << 1) | (1u << 0)); \
        n++;                                                                                            \
    } while (0)
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    float rpm = eng == ENG_RUNNING ? GetRPM(truck) : eng == ENG_STOPPING ? snd_rpm : 0.0f;
    if (rpm < 0) rpm = -rpm;
    if (rpm > 7000.0f) rpm = 7000.0f;
    float mph = forward_speed() * 2.237f;
    if (mph < 0) mph = -mph;
    if (mph > 105.0f) mph = 105.0f;
    const int G = TRUCK_TEX_GAUGE;
    GAUGE(G + 0, 256, 256, 370.0f, 362.1f, 99.2f, 99.2f, 0.5f, 0.5f, 0.0f, 1, 1, 1, 1);           // tach
    GAUGE(G + 1, 512, 512, 469.2f, 310.0f, 150.8f, 151.3f, 0.5f, 0.5f, 0.0f, 1, 1, 1, 1);         // speedo
    // Needles: the art is 56 x 146 pixels, pivot (29.5, 110) = the knob,
    // tip 108 pixels above it.  Drawn tip length = SetRPMData's 40 / 60.
    GAUGE(G + 2, 64, 256, 421.0f - 7.5f, 411.0f - 40.0f * 110.0f / 108.0f, 15.0f, 40.0f * 146.0f / 108.0f,
          29.5f / 56.0f, 110.0f / 146.0f, -135.0f + 270.0f * rpm / 7000.0f, 1, 1, 1, 1);
    GAUGE(G + 2, 64, 256, 543.0f - 10.0f, 383.0f - 60.0f * 110.0f / 108.0f, 20.0f, 60.0f * 146.0f / 108.0f,
          29.5f / 56.0f, 110.0f / 146.0f, -130.0f + 260.0f * mph / 100.0f, 1, 1, 1, 1);
    // Bars: the inside is pixels 21 to 38 across (of 42) and 2 to 124 down
    // (of 128); the fill grows up from the bottom.
    float level[3] = { fuel[truck_now], 1.0f - eng_damage, radiator_gauge };
    for (int k = use_fuel ? 0 : 1; k < 3; k++) {
        float x = 440.0f + 35.0f * k, L = level[k] < 0 ? 0 : level[k] > 1 ? 1 : level[k];
        GAUGE(G + 3 + k, 64, 128, x, 25.0f, 25.0f, 75.0f, 0.5f, 0.5f, 0.0f, 1, 1, 1, 1);
        if (L > 0.01f) {
            float h = 75.0f * 122.0f / 128.0f * L, bottom = 25.0f + 75.0f * 124.0f / 128.0f;
            GAUGE(G + 6, 16, 32, x + 25.0f * 21.0f / 42.0f, bottom - h, 25.0f * 17.0f / 42.0f, h, 0.5f, 0.5f, 0.0f, 1, 1, 1, 1);
        }
        GAUGE(G + 7 + k, 64, 64, x + 5.0f, 102.0f, 25.0f, 25.0f, 0.5f, 0.5f, 0.0f, 1, 1, 1, 1);
    }
    // Odometer: tenths of a mile, 5 digits (wraps at 9999.9).
    {
        unsigned v = (unsigned)(odo_miles[truck_now] * 10.0f) % 100000u;
        for (int k = 4; k >= 0; k--, v /= 10)
            GAUGE(G + 10 + (int)(v % 10), 32, 32, 520.0f + 8.6f * k, 420.0f, 8.6f, 11.0f, 0.5f, 0.5f, 0.0f, 1, 1, 1, 1);
    }
    // Text: each letter's cell is 21 x 32 font units; drawn 0.6 x 0.47
    // of a 640-unit each (a 15-unit cell, the letter about 9.4 of it).
    {
        char gear_s[4] = { 0 };
        int gr = GetCurGear(truck);
        if (gr < 0) gear_s[0] = 'R';
        else if (gr == 0) gear_s[0] = 'N';
        else gear_s[0] = (char)('0' + (gr > 9 ? 9 : gr));
        const char *line[4] = { "T:", traction_hud[traction_mode], "G:", gear_s };
        const float lx[4] = { 462.0f, 483.0f, 547.0f, 569.0f };
        for (int q = 0; q < 4; q++) {
            float x = lx[q];
            for (const char *c = line[q]; *c && n < 48; c++) {
                if (*c < 32 || *c > 126) continue;
                const unsigned char *gl = font_glyph[*c - 32];
                if (gl[2] > gl[0]) {
                    anim[n].sx = (short)(gl[2] - gl[0]); anim[n].sy = (short)(gl[3] - gl[1]);
                    anim[n].cx = 0; anim[n].cy = 0;
                    anim[n].u1 = gl[0]; anim[n].v1 = gl[1]; anim[n].u2 = gl[2]; anim[n].v2 = gl[3];
                    anim[n].texid = TRUCK_TEX_FONT; anim[n].attr = 0;
                    sprite[n].p[0] = right + x * scale; sprite[n].p[1] = 444.5f * scale; sprite[n].p[2] = 0;
                    sprite[n].sx = 0.6f * scale; sprite[n].sy = 0.47f * scale;
                    sprite[n].ang = 0;
                    sprite[n].tlist = &truck_texlist; sprite[n].tanim = &anim[n];
                    SetMaterialAndSpriteColor_Float(1, 1, 1, 1);
                    njDrawSprite2D_ForcePriority(&sprite[n], 0, -1.5f, (1u << 5) | (1u << 4) | (1u << 1));
                    n++;
                }
                x += (gl[4] + 2) * 0.6f;
            }
        }
    }
    // Radio banner: the station's name and the song, centered near the
    // top, each letter drawn twice (a dark copy 1 unit down-right first).
    {
        const char *rl[2];
        if (radio_text(&rl[0], &rl[1])) {
            const float sz[2] = { 1.0f, 0.7f }, ly[2] = { 40.0f, 62.0f };
            for (int q = 0; q < 2; q++) {
                float w = 0;
                for (const char *c = rl[q]; *c; c++)
                    if (*c >= 32 && *c <= 126) w += (font_glyph[*c - 32][4] + 2) * sz[q];
                for (int pass = 0; pass < 2; pass++) {
                    float x = (screen_w / scale - w) * 0.5f, d = pass ? 0.0f : 1.0f;
                    for (const char *c = rl[q]; *c && n < 160; c++) {
                        if (*c < 32 || *c > 126) continue;
                        const unsigned char *gl = font_glyph[*c - 32];
                        if (gl[2] > gl[0]) {
                            anim[n].sx = (short)(gl[2] - gl[0]); anim[n].sy = (short)(gl[3] - gl[1]);
                            anim[n].cx = 0; anim[n].cy = 0;
                            anim[n].u1 = gl[0]; anim[n].v1 = gl[1]; anim[n].u2 = gl[2]; anim[n].v2 = gl[3];
                            anim[n].texid = TRUCK_TEX_FONT; anim[n].attr = 0;
                            sprite[n].p[0] = (x + d) * scale; sprite[n].p[1] = (ly[q] + d) * scale; sprite[n].p[2] = 0;
                            sprite[n].sx = sz[q] * scale; sprite[n].sy = sz[q] * 0.8f * scale;
                            sprite[n].ang = 0;
                            sprite[n].tlist = &truck_texlist; sprite[n].tanim = &anim[n];
                            if (pass) SetMaterialAndSpriteColor_Float(1, 1, 1, 1);
                            else SetMaterialAndSpriteColor_Float(1, 0, 0, 0);
                            njDrawSprite2D_ForcePriority(&sprite[n], 0, -1.5f, (1u << 5) | (1u << 4) | (1u << 1));
                            n++;
                        }
                        x += (gl[4] + 2) * sz[q];
                    }
                }
            }
        }
    }
#undef GAUGE
    ResetMaterial();
}

// The damaged-engine loops: FX.SND curves on the same sound key.
static const struct snd_curve dmg_gain = { 1, 3, { 700, 1200, 6040, 0 }, { 0, 70, 70, 0 } };
static const struct snd_curve dmg_pitch = { 1, 2, { 700, 6040, 0, 0 }, { 1, 3.421769f, 0, 0 } };

// Each frame.  speed = forward speed, m/s.
static void damage_update(float dt, float speed, float near, float key, int sounding)
{
    detect_hits(dt);
    part_effects(dt);
    float permille = part_permille(15);
    int lvl = damage_level(permille);
    eng_damage = lvl >= 4 ? 1.0f : 1.0f - permille * 0.001f;
    if (eng_damage < 0) eng_damage = 0;
    if (lvl != eng_level) {
        if (eng_level >= 0 && lvl > eng_level) {
            static const char *const color[] = { "green", "yellow", "orange", "red", "black" };
            PrintDebug("[SADX Truck] engine damage level %d (%s)%s\n", lvl, color[lvl],
                       lvl == 3 ? ": DAMAGE WARNING!" : lvl == 4 ? ": The engine is too damaged to continue!" : "");
        }
        if (lvl == 4 && (eng == ENG_RUNNING || eng == ENG_CATCH)) { engine_stop("dead"); steam_t = 8.0f; }
        SetTorqueScale(truck, 1.0f - 0.1f * lvl);
        eng_level = lvl;
    }

    // Water.
    // Cabela's (UpdateDamageByWater 0x10020B10) has two rules: fully
    // under -> 500 a second, partly in -> 50 a second (above).  "Fully
    // under" is EngineDll's water check (0x10225A10, cBspTree::EntInWater
    // 0x1020AF10) finding the water top above a box on the truck, half
    // height truck byte 120; that byte is 0 in the mod's trucks and nothing
    // found in GameDll sets it, so the mod uses the roof instead: the
    // water above the solid box's top (truck space, SADX units, measured
    // per truck; the Stalker's is 8.1 units = 2.7 feet above the body's middle).
    // The snorkel doesn't help under water.
    water_rel = water_above_body();
    {
        int under = water_rel != NO_WATER && solid_truck == truck_now && water_rel >= solid_hi[1] / UNITS_PER_METER;
        if (under || (water_rel != NO_WATER && water_rel >= 0.1f + SNORKEL_M)) {
            water_tick += dt;
            if (water_tick >= 1.0f) {
                water_tick -= 1.0f;
                damage_part(15, under ? 500.0f : 50.0f, under ? "truck under water" : "water in the engine");
            }
        } else water_tick = 0;
    }
    // Wheels wet: the surface above the wheel bottoms (wheel center less
    // 0.4 m, about 16 inches; a guess at the tire radius).
    int wet = water_rel != NO_WATER && water_rel > car.tire[0].y - 0.4f;
    if (wet && !was_wet && (speed > 2.0f || speed < -2.0f)) { snd_play(WAV_SPLASH, 0); snd_set(WAV_SPLASH, 100 * near, 1); }
    if (wet && !was_wet) snd_play(WAV_WATER, 1);
    if (!wet && was_wet) snd_stop(WAV_WATER);
    was_wet = wet;
    if (wet) {
        float a = speed < 0 ? -speed : speed;
        snd_set(WAV_WATER, 90.0f * (a > 4.0f ? 1.0f : a / 4.0f) * near, 1);
    }

    // Sputter.
    if (sputter_cut > 0) sputter_cut -= dt;
    if (eng == ENG_RUNNING && lvl == 3) {
        sputter_tick += dt;
        if (sputter_tick >= 0.15f) {
            sputter_tick = 0;
            if (rnd((int)(permille * 0.1f) + 2) == 0) {
                sputter_cut = 0.3f;
                if (GetRPM(truck) < 1200.0f) engine_stop("stalled (damaged engine)");
            }
        }
    }

    // Damaged-engine sounds.
    int want_medium = sounding && lvl == 2, want_bad = sounding && lvl >= 3;
    static int had_medium, had_bad;
    if (want_medium != had_medium) { if (want_medium) snd_play(WAV_DAMAGED, 1); else snd_stop(WAV_DAMAGED); had_medium = want_medium; }
    if (want_bad != had_bad) { if (want_bad) { snd_play(WAV_BAD, 1); grind_t = 15 + rnd(19); } else snd_stop(WAV_BAD); had_bad = want_bad; }
    if (want_medium) snd_set(WAV_DAMAGED, curve(&dmg_gain, key) * near, curve(&dmg_pitch, key));
    if (want_bad) {
        snd_set(WAV_BAD, curve(&dmg_gain, key) * near, curve(&dmg_pitch, key));
        grind_t -= dt;
        if (grind_t <= 0) { snd_play(WAV_GRIND, 0); snd_set(WAV_GRIND, 100 * near, 1); grind_t = 15 + rnd(19); }
    }
    if (steam_t > 0) {
        if (steam_t == 8.0f) snd_play(WAV_STEAM, 1);
        steam_t -= dt;
        snd_set(WAV_STEAM, 70 * near, 1);
        if (steam_t <= 0) snd_stop(WAV_STEAM);
    }
}

// Each frame while a truck exists.  held = pad buttons (X counts only while
// riding), gas and brake = the pedals 0 to 1, distance = Sonic to the truck
// in meters (the .SND MinDistance 10, MaxDistance 30: full volume up to 10,
// silent at 30).
static void engine_update(int riding_now, u32 held, float gas, float brake, float distance, int paused)
{
    const float dt = 1.0f / 60.0f;
    const struct truck_sound *ts = &truck_sounds[truck_now];
    int x = riding_now && (held & BUTTON_X);
    int x_press = x && !x_before;
    x_before = x;
    if (!paused && truck) {                     // odometer, meters -> miles
        float v = forward_speed();
        odo_miles[truck_now] += (v < 0 ? -v : v) * dt / 1609.34f;
    }
    if (gear_msg_t > 0) gear_msg_t -= dt;
    float crank_speed = battery / 12.0f * (cold_here() ? COLD_CRANK : 1.0f);

    switch (eng) {
    case ENG_OFF:
        if (!x_press) break;
        if (gas > 0.1f || brake > 0.1f) {
            if (gear_msg_t <= 0) {
                PrintDebug("[SADX Truck] Your truck cannot be started while in gear (let go of gas and brake)\n");
                snd_play(WAV_FALSE_START, 0);
                tries += 3;
                gear_msg_t = 2.0f;
            }
            break;
        }
        tries += 1;
        flooded = tries > 17 && rnd(80) < tries;
        if (OUT_OF_FUEL) PrintDebug("[SADX Truck] out of fuel: get out and hold D-pad up 2 seconds to repair and fill the tank\n");
        if (!cold_here() && !flooded && battery >= 6.0f && !OUT_OF_FUEL && rnd(100) * 0.0095f >= eng_damage) {
            battery -= 0.5f;
            snd_play(ts->start_wav, 0);
            eng_t = truck_wav_seconds[ts->start_wav] - 0.25f;
            eng = ENG_CATCH;
        } else {
            if (flooded) { snd_play(WAV_CLICK, 0); PrintDebug("[SADX Truck] engine flooded\n"); }
        else if (cold_here()) PrintDebug("[SADX Truck] cold engine: hold X to crank\n");
            snd_play(WAV_CRANK_IN, 0);
            eng_t = truck_wav_seconds[WAV_CRANK_IN] - 0.12f;
            eng = ENG_CRANK_IN;
        }
        break;
    case ENG_CRANK_IN:
    case ENG_CRANK_LOOP:
        battery -= 0.01f;
        if (battery < 1.0f) battery = 1.0f;
        snd_set(WAV_CRANK_IN, 100, crank_speed);
        snd_set(WAV_CRANK_LOOP, 100, crank_speed);
        if (!x) {
            snd_stop(WAV_CRANK_IN); snd_stop(WAV_CRANK_LOOP);
            snd_play(WAV_CRANK_OUT, 0);
            eng = ENG_OFF;
            break;
        }
        eng_t -= dt * crank_speed;
        if (eng_t > 0) break;
        if (eng == ENG_CRANK_LOOP) {
            tries += 0.5f;
            if (!flooded && battery >= 4.0f && !OUT_OF_FUEL && rnd(100) * 0.01f >= eng_damage) {
                snd_stop(WAV_CRANK_LOOP);
                snd_play(ts->start_wav, 0);
                eng_t = truck_wav_seconds[ts->start_wav] - 0.25f;
                eng = ENG_CATCH;
                break;
            }
        }
        snd_play(WAV_CRANK_LOOP, 0);
        snd_set(WAV_CRANK_LOOP, 100, crank_speed);
        eng_t = truck_wav_seconds[WAV_CRANK_LOOP] - 0.12f;
        eng = ENG_CRANK_LOOP;
        break;
    case ENG_CATCH:
        eng_t -= dt;
        if (eng_t > 0) break;
        eng = ENG_RUNNING;
        run_t = 0;
        engine_loops(1);
        PrintDebug("[SADX Truck] engine running (battery %.1f V, tries %.1f)\n", (double)battery, (double)tries);
        break;
    case ENG_RUNNING: {
        battery += 0.002f;
        if (battery > 12.0f) battery = 12.0f;
        run_t += dt;
        if (run_t >= 10.0f) { run_t = 0; tries -= 0.2f; if (tries < 0) tries = 0; }
        const char *body = *(const char *const *)((const char *)truck + 4);
        float up_y = ((const float *)(body + 0x4c))[4];        // truck up, y part
        upside_t = up_y < 0 ? upside_t + dt : 0;
        if (use_fuel) {
            float r = GetRPM(truck);
            if (r < 0) r = -r;
            r = r < 600.0f ? 600.0f : r > 7000.0f ? 7000.0f : r;
            float before = fuel[truck_now];
            fuel[truck_now] -= dt * r / (fuel_minutes[truck_now][traction_mode - 1] * 180000.0f);
            if (fuel[truck_now] < 0) fuel[truck_now] = 0;
            if (before >= 0.25f && fuel[truck_now] < 0.25f) PrintDebug("[SADX Truck] fuel a quarter left\n");
            if (before >= 0.1f && fuel[truck_now] < 0.1f) PrintDebug("[SADX Truck] fuel low (a tenth left)\n");
        }
        if (x_press) engine_stop("off");
        else if (eng_damage >= 1.0f) engine_stop("dead");
        else if (OUT_OF_FUEL) engine_stop("out of fuel");
        else if (GetCurGear(truck) >= 3 && GetRPM(truck) < 1500.0f && gas < 0.05f) engine_stop("stalled (gear 3 or higher under 1500 RPM)");
        else if (upside_t > 6.0f) engine_stop("stalled (upside down)");
        break;
    }
    case ENG_STOPPING:
        snd_rpm -= 4000.0f * dt;
        if (snd_rpm <= car.min_rpm * 0.5f) {
            engine_loops(0);
            snd_play(ts->stop_wav, 0);
            eng = ENG_OFF;
        }
        break;
    }

    // Running sound: key = A * RPM + B, A = 4000 / (max - min),
    // B = 5000 - A * max (GameDll 0x100879D0): min RPM -> 1000, max -> 5000.
    // PARAM2 = 2 - gas (1 = full gas, 2 = none; a guess, Cabela's 3 leaves
    // it at 0).
    int sounding = eng == ENG_RUNNING || eng == ENG_STOPPING;
    float rpm = eng == ENG_RUNNING ? GetRPM(truck) : snd_rpm;
    float a = 4000.0f / (car.max_rpm - car.min_rpm), key = a * rpm + 5000.0f - a * car.max_rpm;
    float near = distance <= 10.0f ? 1.0f : distance >= 30.0f ? 0.0f : (30.0f - distance) / 20.0f;
    if (paused) near = 0;
    damage_update(dt, forward_speed(), near, key, sounding);
    if (sputter_cut > 0) near *= 0.3f;
    if (sounding) {
        float p2 = 2.0f - (eng == ENG_RUNNING ? gas : 0.0f);
        for (int k = 0; k < ts->nsamples; k++) {
            const struct snd_sample *sm = &ts->s[k];
            float g = curve(&sm->gain[0], sm->gain[0].param == 2 ? p2 : key);
            if (sm->ngain > 1) g *= curve(&sm->gain[1], sm->gain[1].param == 2 ? p2 : key);
            snd_set(sm->wav, g * near, curve(&sm->pitch, key));
        }
    }
}

// Called by the mod loader once per SADX frame.  Y puts the truck under
// Sonic and him in it; Y again lets him out and leaves the truck parked.
// While riding, prints once a second.
// Diagnostic recorder: the last 60 frames (1 second), written to the log
// when the floor disappears or the truck first leans past 30 degrees, then
// 30 more frames as they happen.  Lean: left wheels (0, 2) minus right
// wheels (1, 3); pitch: front wheels (0, 1) minus back wheels (2, 3).
typedef struct {
    int frame, gear, rays, hits, box_rays, box_hits, tris, mobile, backup;
    float gas, brake, steer, wheel, wheel_max, mph, roll, pitch, rpm, cx, cz, height;
} rec_line;
static rec_line rec[60];
static int rec_after, rec_dumps, rec_leaned;
static void rec_print(const rec_line *r)
{
    PrintDebug("[SADX Truck]   f %4d  gas %.2f  brake %.2f  stick %+.2f  wheels %+5.1f of %4.1f deg  %5.1f mph  height %+5.2f m  lean %+4.0f  pitch %+4.0f  gear %d  RPM %6.1f  rays hit %d of %d  box corners hit %d of %d  level triangles %d (moving pieces %d)  backup hits %d\n",
               r->frame, (double)r->gas, (double)r->brake, (double)r->steer, (double)r->wheel, (double)r->wheel_max, (double)r->mph, (double)r->height,
               (double)r->roll, (double)r->pitch, r->gear, (double)r->rpm, r->hits, r->rays, r->box_hits, r->box_rays,
               r->tris, r->mobile, r->backup);
}
static void rec_frame(float gas, float brake, int floor_gone)
{
    m4x4 w[4];
    for (u32 i = 0; i < 4; i++) GetWheelTM(truck, &w[i], i);
    float c[3];
    truck_center(c);
    if (frame <= 2) {                                   // new spawn: forget the old truck
        rec_dumps = rec_after = rec_leaned = 0;
        for (int k = 0; k < 60; k++) rec[k].frame = 0;
    }
    rec_line *r = &rec[frame % 60];
    r->frame = frame; r->gas = gas; r->brake = brake; r->steer = steer_now;
    r->wheel = *(float *)((char *)truck + 0xf0); r->wheel_max = *(float *)((char *)truck + 0xf4);
    // Speed over the last 6 frames: EngineDll moves in 1/100 second steps,
    // so one 1/60 second frame holds 1 or 2 of them and a single frame's
    // distance jumps back and forth.
    r->cx = c[0]; r->cz = c[2];
    // Height of the wheel center point above the SADX floor under it
    // (searched from 3 m, about 10 feet, above it).
    {
        float g = ground_m(c[0], c[1] + 3.0f, c[2]);
        r->height = g == NO_GROUND ? -99.0f : c[1] - g;
    }
    const rec_line *o6 = &rec[(frame + 54) % 60];
    float dx = c[0] - o6->cx, dz = c[2] - o6->cz;
    r->mph = o6->frame != frame - 6 ? 0.0f : sqrt_f(dx * dx + dz * dz) * 10.0f * 2.23694f;
    // Lean over the full circle: 0 = upright, +90 = on its right side
    // (left wheels up), 180 = on its roof.  side = left minus right wheels,
    // fwd = front minus back, up = side x fwd (points up when upright).
    float sx = (w[0].m[12] + w[2].m[12]) - (w[1].m[12] + w[3].m[12]);
    float sy = (w[0].m[13] + w[2].m[13]) - (w[1].m[13] + w[3].m[13]);
    float sz = (w[0].m[14] + w[2].m[14]) - (w[1].m[14] + w[3].m[14]);
    float fx = (w[0].m[12] + w[1].m[12]) - (w[2].m[12] + w[3].m[12]);
    float fy = (w[0].m[13] + w[1].m[13]) - (w[2].m[13] + w[3].m[13]);
    float fz = (w[0].m[14] + w[1].m[14]) - (w[2].m[14] + w[3].m[14]);
    float uy = sz * fx - sx * fz;                       // y of side x fwd
    float ul = sqrt_f((sy * fz - sz * fy) * (sy * fz - sz * fy) + uy * uy + (sx * fy - sy * fx) * (sx * fy - sy * fx));
    float sl = sqrt_f(sx * sx + sy * sy + sz * sz);
    r->roll = atan2_f(sy / sl, uy / ul) * 57.29578f;
    float fwd = ((w[0].m[13] + w[1].m[13]) - (w[2].m[13] + w[3].m[13])) * 0.5f;
    float bx = w[0].m[12] - w[2].m[12], bz = w[0].m[14] - w[2].m[14];
    r->pitch = atan2_f(fwd, sqrt_f(bx * bx + bz * bz)) * 57.29578f;
    r->gear = GetCurGear(truck); r->rpm = GetRPM(truck);
    r->rays = rays_now; r->hits = hits_now;
    r->box_rays = box_rays_now; r->box_hits = box_hits_now;
    r->tris = tri_count; r->mobile = mobile_near; r->backup = backup_now;
    rays_now = hits_now = box_rays_now = box_hits_now = backup_now = 0;

    int leaned = (r->roll > 30.0f || r->roll < -30.0f) && !rec_leaned;
    if (leaned) rec_leaned = 1;
    if (rec_after > 0) {
        rec_print(r);
        rec_after--;
    } else if ((floor_gone || leaned) && rec_dumps < 3) {
        rec_dumps++;
        PrintDebug("[SADX Truck]   --- %s at frame %d: the second before, then 30 frames after ---\n",
                   leaned ? "lean past 30 degrees" : "floor gone", frame);
        for (int k = 1; k <= 60; k++) {
            const rec_line *o = &rec[(frame + k) % 60];
            if (o->frame > 0 && o->frame > frame - 60) rec_print(o);
        }
        rec_after = 30;
    }
}

// Snowboard modes count as one for the take-out check: Sonic's
// MD_SONIC_S8A3_SLID 62 to MD_SONIC_S8A3_CRSH 68 (SADXEnumsNew.h), so
// riding the truck down the snowboard course is not "SADX took him out"
// each time he lands or jumps.  Tails' numbers are his own; unchecked.
static int mode_group(int m)
{
    return m >= 62 && m <= 68 ? 62 : m;
}

// Is a SADX scene walking this player (SetAutoPilotP, move mode 2)?
// taskwk byte 60 is ewp (eventwk *), eventwk byte 36 is move.mode.
static int sadx_walking(const char *task)
{
    if (!task) return 0;
    const char *ewp = *(const char *const *)(task + 60);
    return ewp && ewp[36] == 2;
}

// The pause check (sonic.exe 0x415E05 and 0x415E94) runs in the game loop
// before OnFrame: Start must be in per[0].press (the real pad, that frame)
// and in perG[0].on.  So perG[0] is written in OnControl, which the mod
// loader calls at the end of SADX's pad copy (its hook at 0x40FF00), the
// same frame as the press: empty except Start, Sonic sees no buttons.
// Y stays the truck's button with Better Tails AI loaded: its fly-travel
// button is C with this Mod Loader (installed DLL 3.1.6, 0x100094F6 to
// 0x10009579: C when an "input-mod" DLL is loaded or the loader's API is
// 19 or later and its own input is on, else Y).  Mod Loader v3.7 is API
// 30, so the two never share a button.
__declspec(dllexport) void __cdecl OnControl(void)
{
    static u32 start_before;
    if ((!riding && !map_menu) || pad_freed || !pad_raw || game_mode != 15) { start_before = 0; return; }
    u32 start = *(const u32 *)(pad_raw + 8) & BUTTON_START;
    for (int k = 8; k < 36; k++) pad_game[k] = 0;
    *(u32 *)(pad_game + 8) = start;                         // on
    *(u32 *)(pad_game + 12) = start ? 0 : BUTTON_START;     // off
    *(u32 *)(pad_game + 16) = start & ~start_before;        // press
    *(u32 *)(pad_game + 20) = start_before & ~start;        // release
    start_before = start;
}

// Cabela's maps, step 3: Death Valley as level 11.  maps\bj_a.bin (written by
// map_to_bin.py; layout there): Cabela's ground at every other grid point,
// 16 x 16 blocks of 33 x 33 points, SADX units, already mirrored like the
// trucks.  Texture pack SXMAP_BJ_A: Cabela's 16 color map pictures.
//
// On foot in Station Square, L + R held 2 seconds goes to level 11, whose
// landtable is the map's own (see map_patch); there, again goes back.
// The map's pieces are solid (1 << 0) but not drawn by SADX (no
// (1 << 31)): SADX's usual draw path skips models without the vertex
// buffer it builds for its own (see make_truck_models), so the mod draws
// them itself with njDrawModel_Dynamic, from a task like the rings.
// The map sits MAP_OFF_X / MAP_OFF_Z away from the middle (from step 2,
// when it shared Station Square; kept, harmless).
#ifndef TEST_EXE
#define MAP_NB 16
#define MAP_NP 33
#define MAP_CELLS 32
#define MAP_BLOCKS (MAP_NB * MAP_NB)
#define MAP_PTS (MAP_NP * MAP_NP)
#define MAP_CORNERS (MAP_CELLS * MAP_CELLS * 6)
#define MAP_BLOCK_BYTES (20 + MAP_PTS * 24)
#define MAP_OFF_X 60000.0f
#define MAP_OFF_Z 60000.0f
#define MAP_DRAW 4000.0f                 // draw blocks within 400 m (about 1/4 mile) of the camera
#define MAP_NEAR 2000.0f                 // full detail within 200 m (about 650 feet), beyond that 1/16
#define MAP_FAR_STEP 4                   // far blocks: every 4th point, 8 x 8 cells
#define MAP_FAR_CELLS (MAP_CELLS / MAP_FAR_STEP)
#define MAP_FAR_CORNERS (MAP_FAR_CELLS * MAP_FAR_CELLS * 6)
static unsigned char *map_file;          // maps\<map>.bin of the map picked last
static nj_texname map_texnames[17];     // the 16 color map pictures, then the detail texture
static struct { nj_texname *textures; u32 n; } map_texlist = { map_texnames, 16 };
static u32 map_gbix0, map_det_gbix;
static char map_pack[17];
static nj_material map_mats[16];
static short map_tris[MAP_CORNERS];
static short map_uv[16][MAP_CORNERS][2]; // by the block's place inside its picture (4 x 4)
static nj_meshset map_sets[MAP_BLOCKS];
static nj_model map_models[MAP_BLOCKS];
static nj_object map_objs[MAP_BLOCKS];
// Collision: each block's ground as 4 pieces of 16 x 16 cells (512
// triangles), each with its own 17 x 17 points.  A whole block (2048
// triangles) lost its ground in places, only ever in its second half
// (rows 16 to 31, triangle 1024 on), so SADX seems to handle only about
// 1024 triangles of one piece (seen in the log's grid check, not found in
// sonic.exe's code).  Drawing still uses the whole blocks above.
#define MAP_QCELLS (MAP_CELLS / 2)
#define MAP_QNP (MAP_QCELLS + 1)
#define MAP_QPTS (MAP_QNP * MAP_QNP)
#define MAP_COLS (MAP_BLOCKS * 4 + 4)    // collision pieces: 4 per block, then the 4 walls
static short map_qtris[MAP_QCELLS * MAP_QCELLS * 6];
static float map_qpts[MAP_BLOCKS * 4][MAP_QPTS * 3], map_qnrm[MAP_BLOCKS * 4][MAP_QPTS * 3];
static nj_meshset map_qsets[MAP_BLOCKS * 4];
static nj_model map_qmodels[MAP_BLOCKS * 4];
static nj_object map_qobjs[MAP_BLOCKS * 4];
static short map_far_tris[MAP_FAR_CORNERS];         // far blocks: same points, fewer triangles
static short map_far_uv[16][MAP_FAR_CORNERS][2];
static nj_meshset map_far_sets[MAP_BLOCKS];
static nj_model map_far_models[MAP_BLOCKS];
// Ground detail (Cabela's SHADER_DETAILOP stage, see map_to_bin.py): near
// blocks drawn a second time, same points, the detail texture repeated
// MAP_DET_REPEAT times across a block, blended as 2 x ground x detail
// (mid-gray changes nothing).  Unlit, so the light isn't counted twice.
// SADX turns a Ninja blend value into Direct3D's through one table of 8
// for both source and destination (0x88AE1C, 9,481,756, read at 0x7849C0:
// 0 ZERO, 1 ONE, 2 SRCCOLOR, 3 INVSRCCOLOR, 4 SRCALPHA, 5 INVSRCALPHA,
// 6 DESTALPHA, 7 INVDESTALPHA), so DESTCOLOR can't be asked for (Ninja
// "other color" became SRCCOLOR for the source: tested, far too bright).
// While the detail pass draws, entries 2 and 3 are DESTCOLOR (9) and
// SRCCOLOR (3); the material asks src 2, dst 3: detail x ground + ground x
// detail.  SADX's values (3 and 4) go back right after.  The table is in
// sonic.exe's read-only part: made writable once (map_det_table_ok).
#define MAP_DET_REPEAT 8
static short map_det_uv[MAP_CORNERS][2];
static nj_material map_det_mat;
static nj_meshset map_det_sets[MAP_BLOCKS];
static nj_model map_det_models[MAP_BLOCKS];
#define NJ_BLEND_TABLE ((u32 *)0x88AE1C)
static int map_det_table_ok;             // 1 = writable, -1 = VirtualProtect failed (no detail)
typedef struct { float c[3], r, wy, wz; void *obj; int blockbits, flags; } map_col;
static map_col map_cols_base[MAP_COLS];        // ground quarter blocks, then the 4 walls
static map_col *map_cols = map_cols_base;       // that, or a longer copy with the rocks' shapes after it
// Invisible walls along the map's 4 edges (collision only): a flat
// upright rectangle each, 2 triangles, facing in.
static float map_wall_pts[4][4][3], map_wall_nrm[4][4][3];
static short map_wall_tris[4][6];
static float map_edge[4];                // xlo, xhi, zlo, zhi: where the walls stand
static nj_meshset map_wall_sets[4];
static nj_model map_wall_models[4];
static nj_object map_wall_objs[4];
static struct { short ncol, nanim, attrs, loadflags; float clip; void *col, *anim; const char *texname; void *texlist; int bin, loadfunc; } map_lt;
static float map_spawn[3];
static float map_low, map_high;          // the map's lowest and highest ground (SADX units)
#define MAP_LEVEL 11                     // LevelIDs_B, unused by SADX; see map_patch
static int map_on, map_tex_tried, map_patched;
static char *map_task;

static void map_patch(void);
// Drawing the way SADX draws its own levels.  njDrawModel_Dynamic moves
// every point on the CPU every frame (about 0.07 microseconds a triangle,
// 8 ms for the ground in hilly spots).  SADX's levels instead get a vertex
// buffer per meshset once, at level load: InitLandTable (0x43A260) runs
// InitLandTableObject (0x787140) on each visible piece, which runs
// InitLandTableMeshSet (0x787010; names from SADXFunctions.h) on each
// meshset whose buffer (meshset byte 24) is 0: MeshSetBuffer_Allocate
// (0x794840, the game's heap, linked into a list at 0x38A5E50) and a fill
// function picked by mesh kind, texture coordinates and normals (table
// 0x389D790).  njDrawModel_SADX (0x77EDA0) then draws from the buffers.
// The map does the same, each meshset the first time it is drawn; before
// a map's memory is freed, MeshSetBuffer_Free (0x7948C0, thiscall: unlinks,
// frees, sets byte 24 back to 0) for each, or the list would point at
// freed memory.
#define InitLandTableMeshSet ((void (__cdecl *)(void *, void *))0x787010)
#define njDrawModel_SADX ((void (__cdecl *)(void *))0x77EDA0)
#define Direct3D_EnableZWrite ((void (__cdecl *)(u32))0x77ED20)   // SADXFunctions.h
#define MeshSetBuffer_Free ((void (__thiscall *)(void *))0x7948C0)
static int nj_buffers_made;
static void draw_buffered(nj_model *md)
{
    for (int i = 0; i < md->nsets; i++)
        if (!md->sets[i].buffer) { InitLandTableMeshSet(md, &md->sets[i]); nj_buffers_made++; }
    njDrawModel_SADX(md);
}
static void buffers_free(nj_meshset *st, int n)
{
    for (int i = 0; i < n; i++) {
        if (st[i].buffer) { MeshSetBuffer_Free(&st[i]); nj_buffers_made--; }
        st[i].buffer = 0;
    }
}
// The 4 walls, from the outer points of the edge blocks: block row 0 is the
// map's low-z edge, row 15 the high-z edge; column 0 is high x (x is
// flipped, see map_to_bin.py), column 15 low x.  From 200 units (about 65
// feet) below the lowest ground to 1000 units (about 330 feet) above the
// highest.  Triangles wound so (b - a) x (c - a) points into the map.
static void map_walls(unsigned char *b)
{
    float xlo = 0, xhi = 0, zlo = 0, zhi = 0;
    for (int k2 = 0; k2 < MAP_BLOCKS; k2++) {
        const float *pts = (const float *)(b + 64 + k2 * MAP_BLOCK_BYTES + 20);
        for (int p = 0; p < MAP_PTS; p++) {
            float x = pts[p * 3], z = pts[p * 3 + 2];
            if ((k2 == 0 && p == 0) || x < xlo) xlo = x;
            if ((k2 == 0 && p == 0) || x > xhi) xhi = x;
            if ((k2 == 0 && p == 0) || z < zlo) zlo = z;
            if ((k2 == 0 && p == 0) || z > zhi) zhi = z;
        }
    }
    map_edge[0] = xlo; map_edge[1] = xhi; map_edge[2] = zlo; map_edge[3] = zhi;
    float ylo = map_low - 200.0f, yhi = map_high + 1000.0f;
    // each wall: its two ends along the ground, and the way in
    const float ends[4][2][2] = { { { xlo, zlo }, { xlo, zhi } }, { { xhi, zlo }, { xhi, zhi } },
                                 { { xlo, zlo }, { xhi, zlo } }, { { xlo, zhi }, { xhi, zhi } } };
    const float in[4][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int w = 0; w < 4; w++) {
        float (*q)[3] = map_wall_pts[w];
        for (int e = 0; e < 2; e++) {
            q[e][0] = ends[w][e][0]; q[e][1] = ylo; q[e][2] = ends[w][e][1];          // bottom
            q[e + 2][0] = ends[w][e][0]; q[e + 2][1] = yhi; q[e + 2][2] = ends[w][e][1];  // top
        }
        for (int k = 0; k < 4; k++) for (int i = 0; i < 3; i++) map_wall_nrm[w][k][i] = in[w][i];
        // (a, b, c) = (bottom 0, bottom 1, top 0); flip if it faces out
        float e1[3], e2[3];
        for (int i = 0; i < 3; i++) { e1[i] = q[1][i] - q[0][i]; e2[i] = q[2][i] - q[0][i]; }
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        int out = n[0] * in[w][0] + n[1] * in[w][1] + n[2] * in[w][2] < 0;
        const short fwd[6] = { 0, 1, 2, 1, 3, 2 }, back[6] = { 0, 2, 1, 1, 2, 3 };
        for (int k = 0; k < 6; k++) map_wall_tris[w][k] = out ? back[k] : fwd[k];
        float c[3], r2 = 0;
        for (int i = 0; i < 3; i++) c[i] = (q[0][i] + q[3][i]) / 2;
        for (int i = 0; i < 3; i++) r2 += (q[3][i] - c[i]) * (q[3][i] - c[i]);
        float r = sqrt_f(r2);
        nj_meshset *st = &map_wall_sets[w];
        st->type_mat = 0; st->n = 2; st->meshes = map_wall_tris[w];
        nj_model *md = &map_wall_models[w];
        md->points = q; md->normals = map_wall_nrm[w]; md->npoints = 4;
        md->sets = st; md->mats = &map_mats[0]; md->nsets = 1; md->nmats = 1;
        md->center[0] = c[0]; md->center[1] = c[1]; md->center[2] = c[2]; md->r = r;
        nj_object *o = &map_wall_objs[w];
        o->eval = (1u << 0) | (1u << 1) | (1u << 2);
        o->model = md; o->scl[0] = o->scl[1] = o->scl[2] = 1.0f;
        int k2 = MAP_BLOCKS * 4 + w;
        map_cols[k2].c[0] = c[0]; map_cols[k2].c[1] = c[1]; map_cols[k2].c[2] = c[2]; map_cols[k2].r = r;
        map_cols[k2].obj = o;
        map_cols[k2].flags = 1 << 0;                    // solid, not drawn
    }
    PrintDebug("[SADX Truck] map: walls at x %.0f and %.0f, z %.0f and %.0f, from y %.0f to %.0f\n",
               (double)xlo, (double)xhi, (double)zlo, (double)zhi, (double)ylo, (double)yhi);
}
// The 16 maps, in Cabela's own order and with its names (SCRIPTS\MAPS.LUA
// RegisterMaps; the letter is the region: Baja, National forest, Canyon,
// Swamp, Alaska).  map_to_bin.py uses the same order for texture numbers.
static const char *const map_ids[16] = { "bj_a", "bj_b", "nf_a", "nf_b", "bj_d", "bj_c", "cn_b", "nf_c",
                                         "sw_b", "sw_c", "cn_a", "cn_c", "sw_a", "ak_a", "ak_b", "ak_c" };
static const char *const map_names[16] = { "B. Death Valley", "B. Coyote-Road", "N. National Park", "N. Sequoia Reservation",
                                           "B. Sun-Oasis", "B. Abandoned Mine", "C. Red Rock", "N. Southern Woods",
                                           "S. El-Dorado", "S. Devil-Moon", "C. Grand Canyon", "C. Buffalo Cliffs",
                                           "S. Upper-Swamps", "A. Grizzly Valley", "A. Frozen Cliffs", "A. White Peak" };
static char map_dir[320];                // the mod folder
static u32 map_dir_n;
static const void *map_helpers;
static int map_have[16];                 // maps\<map>.bin is there
static int map_now = -1;                 // the map in map_file
static void map_path(char *name, int k)
{
    u32 i = 0;
    for (; i < map_dir_n; i++) name[i] = map_dir[i];
    const char *f = "\\maps\\";
    while (*f) name[i++] = *f++;
    for (f = map_ids[k]; *f; ) name[i++] = *f++;
    for (f = ".bin"; *f; ) name[i++] = *f++;
    name[i] = 0;
}
static void obj_free(int (__stdcall *vf)(void *, u32, u32));
static void gr_free(int (__stdcall *vf)(void *, u32, u32));
static void bc_free(int (__stdcall *vf)(void *, u32, u32));
static void sky_free(int (__stdcall *vf)(void *, u32, u32));
static void sky_load(int k, void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *),
                     int (__stdcall *rf)(void *, void *, u32, u32 *, void *), u32 (__stdcall *fs)(void *, u32 *),
                     int (__stdcall *cl)(void *));
static void bc_load(int k, void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *),
                    int (__stdcall *rf)(void *, void *, u32, u32 *, void *), u32 (__stdcall *fs)(void *, u32 *),
                    int (__stdcall *cl)(void *));
static void gr_load(int k, void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *),
                    int (__stdcall *rf)(void *, void *, u32, u32 *, void *), u32 (__stdcall *fs)(void *, u32 *),
                    int (__stdcall *cl)(void *));
static void obj_load(int k, void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *),
                     int (__stdcall *rf)(void *, void *, u32, u32 *, void *), u32 (__stdcall *fs)(void *, u32 *),
                     int (__stdcall *cl)(void *));
// Init: which maps are there, and the level 11 patch.  No map is read yet.
static void map_init(const char *path, u32 n, const void *helpers)
{
    map_dir_n = n < 300 ? n : 300;
    for (u32 i = 0; i < map_dir_n; i++) map_dir[i] = path[i];
    map_helpers = helpers;
    void *kl = LoadLibraryExA("kernel32.dll", 0, 0);
    u32 (__stdcall *ga)(const char *) = kl ? (void *)GetProcAddress(kl, "GetFileAttributesA") : 0;
    int count = 0;
    char name[400];
    for (int k = 0; k < 16; k++) {
        map_path(name, k);
        map_have[k] = ga && ga(name) != 0xFFFFFFFFu;
        count += map_have[k];
    }
    if (!count) { PrintDebug("[SADX Truck] map: no maps in %s\\maps, no map menu\n", map_dir); return; }
    map_patch();
    PrintDebug("[SADX Truck] map: %d of 16 maps there; in Station Square on foot hold L + R 2 seconds for the map menu\n", count);
}
// Read map k (maps\<map>.bin) and make its models, walls and start point.
// 1 = done.  The old map's file is freed first.
static int map_open(int k)
{
    char name[400];
    map_path(name, k);
    const void *helpers = map_helpers;
    void *kl = LoadLibraryExA("kernel32.dll", 0, 0);
    void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *) = kl ? (void *)GetProcAddress(kl, "CreateFileA") : 0;
    int (__stdcall *rf)(void *, void *, u32, u32 *, void *) = kl ? (void *)GetProcAddress(kl, "ReadFile") : 0;
    u32 (__stdcall *fs)(void *, u32 *) = kl ? (void *)GetProcAddress(kl, "GetFileSize") : 0;
    int (__stdcall *cl)(void *) = kl ? (void *)GetProcAddress(kl, "CloseHandle") : 0;
    int (__stdcall *vf)(void *, u32, u32) = kl ? (void *)GetProcAddress(kl, "VirtualFree") : 0;
    if (!cf || !rf || !fs || !cl || !vf) return 0;
    buffers_free(map_sets, MAP_BLOCKS);
    buffers_free(map_far_sets, MAP_BLOCKS);
    buffers_free(map_det_sets, MAP_BLOCKS);
    if (map_file) { vf(map_file, 0, 0x8000); map_file = 0; map_now = -1; }   // MEM_RELEASE
    gr_free(vf);
    bc_free(vf);
    sky_free(vf);
    obj_free(vf);
    map_cols = map_cols_base;
    void *h = cf(name, 0x80000000u, 1, 0, 3, 0, 0);
    if (h == (void *)-1) { PrintDebug("[SADX Truck] map: %s not found\n", name); return 0; }
    u32 size = fs(h, 0), got = 0;
    unsigned char *b = size == 64 + MAP_BLOCKS * MAP_BLOCK_BYTES ? VirtualAlloc(0, size, 0x3000, 4) : 0;
    int ok = b && rf(h, b, size, &got, 0) && got == size;
    cl(h);
    const u32 *hd = (const u32 *)b;
    if (!ok || hd[0] != 0x504D5853u /* "SXMP" */ || hd[1] != 1 || hd[2] != MAP_NB || hd[3] != MAP_NP || hd[4] != 16) {
        PrintDebug("[SADX Truck] map: %s is not the shape this sadxtruck.dll reads (%u bytes)\n", name, size);
        if (b) vf(b, 0, 0x8000);
        return 0;
    }
    map_gbix0 = hd[5];
    map_det_gbix = hd[14];                               // byte 56: the detail texture's number, 0 = none
    map_texlist.n = map_det_gbix ? 17 : 16;
    const float *sp = (const float *)(b + 24);
    map_spawn[0] = sp[0] + MAP_OFF_X; map_spawn[1] = sp[1]; map_spawn[2] = sp[2] + MAP_OFF_Z;
    for (int i = 0; i < 16; i++) map_pack[i] = (char)b[40 + i];
    // One triangle list for every block: (a, b, c) and (b, d, c) per cell.
    int t = 0;
    for (int i = 0; i < MAP_CELLS; i++)
        for (int j = 0; j < MAP_CELLS; j++) {
            short a = (short)(i * MAP_NP + j), bb = (short)(a + 1), c = (short)(a + MAP_NP), d = (short)(c + 1);
            const short q[6] = { a, bb, c, bb, d, c };
            for (int e = 0; e < 6; e++) map_tris[t++] = q[e];
        }
    // The same for a quarter block's 17 x 17 points (collision).
    t = 0;
    for (int i = 0; i < MAP_QCELLS; i++)
        for (int j = 0; j < MAP_QCELLS; j++) {
            short a = (short)(i * MAP_QNP + j), bb = (short)(a + 1), c = (short)(a + MAP_QNP), d = (short)(c + 1);
            const short q[6] = { a, bb, c, bb, d, c };
            for (int e = 0; e < 6; e++) map_qtris[t++] = q[e];
        }
    // Far blocks: the same 33 x 33 points, cells of 4 x 4 (1/16 of the triangles).
    t = 0;
    for (int i = 0; i < MAP_CELLS; i += MAP_FAR_STEP)
        for (int j = 0; j < MAP_CELLS; j += MAP_FAR_STEP) {
            short a = (short)(i * MAP_NP + j), bb = (short)(a + MAP_FAR_STEP),
                  c = (short)(a + MAP_FAR_STEP * MAP_NP), d = (short)(c + MAP_FAR_STEP);
            const short q[6] = { a, bb, c, bb, d, c };
            for (int e = 0; e < 6; e++) map_far_tris[t++] = q[e];
        }
    // Texture coordinates per corner (255 = 1.0, the same as the trucks'):
    // a block is a quarter of its picture each way.
    for (int p = 0; p < 16; p++) {
        for (int e = 0; e < MAP_CORNERS; e++) {
            int pt = map_tris[e], i = pt / MAP_NP, j = pt % MAP_NP;
            map_uv[p][e][0] = (short)((255 * ((p % 4) * MAP_CELLS + j) + 64) / 128);
            map_uv[p][e][1] = (short)((255 * ((p / 4) * MAP_CELLS + i) + 64) / 128);
        }
        for (int e = 0; e < MAP_FAR_CORNERS; e++) {
            int pt = map_far_tris[e], i = pt / MAP_NP, j = pt % MAP_NP;
            map_far_uv[p][e][0] = (short)((255 * ((p % 4) * MAP_CELLS + j) + 64) / 128);
            map_far_uv[p][e][1] = (short)((255 * ((p / 4) * MAP_CELLS + i) + 64) / 128);
        }
    }
    for (int e = 0; e < MAP_CORNERS; e++) {
        int pt = map_tris[e], i = pt / MAP_NP, j = pt % MAP_NP;
        map_det_uv[e][0] = (short)(255 * MAP_DET_REPEAT * j / MAP_CELLS);
        map_det_uv[e][1] = (short)(255 * MAP_DET_REPEAT * i / MAP_CELLS);
    }
    // texture (repeated, not clamped), both sides, use alpha (blending on),
    // unlit, src = 2 (1 << 30), dst = 3 ((1 << 27) | (1 << 26)): the
    // table entries swapped in while it draws
    map_det_mat.diffuse = 0xFFFFFFFFu; map_det_mat.specular = 0; map_det_mat.exponent = 11.0f;
    map_det_mat.tex = 16;
    map_det_mat.flags = (1u << 21) | (1u << 23) | (1u << 20) | (1u << 25) | (1u << 30) | (1u << 27) | (1u << 26);
    // Texture on, both sides, clamped at the picture's edges (no seams from
    // the next picture's other side), normal blending.
    for (int m = 0; m < 16; m++) {
        map_mats[m].diffuse = 0xFFB2B2B2u; map_mats[m].specular = 0; map_mats[m].exponent = 11.0f;
        map_mats[m].tex = (u32)m;
        map_mats[m].flags = (1u << 21) | (1u << 23) | (1u << 16) | (1u << 15) | (1u << 31) | (1u << 28) | (1u << 26);
    }
    for (int k2 = 0; k2 < MAP_BLOCKS; k2++) {
        unsigned char *blk = b + 64 + k2 * MAP_BLOCK_BYTES;
        u32 tex = *(u32 *)blk;
        float *c = (float *)(blk + 4), *pts = (float *)(blk + 20), *nrm = pts + MAP_PTS * 3;
        c[0] += MAP_OFF_X; c[2] += MAP_OFF_Z;
        for (int p = 0; p < MAP_PTS; p++) {
            pts[p * 3] += MAP_OFF_X; pts[p * 3 + 2] += MAP_OFF_Z;
            if ((k2 == 0 && p == 0) || pts[p * 3 + 1] < map_low) map_low = pts[p * 3 + 1];
            if ((k2 == 0 && p == 0) || pts[p * 3 + 1] > map_high) map_high = pts[p * 3 + 1];
        }
        int bz = k2 / MAP_NB, bx = k2 % MAP_NB;
        nj_meshset *s = &map_sets[k2];
        s->type_mat = 0; s->n = MAP_CELLS * MAP_CELLS * 2; s->meshes = map_tris;
        s->uv = &map_uv[(bz % 4) * 4 + bx % 4][0][0];
        nj_model *md = &map_models[k2];
        md->points = pts; md->normals = nrm; md->npoints = MAP_PTS;
        md->sets = s; md->mats = &map_mats[tex < 16 ? tex : 0]; md->nsets = 1; md->nmats = 1;
        md->center[0] = c[0]; md->center[1] = c[1]; md->center[2] = c[2]; md->r = c[3];
        nj_meshset *fs2 = &map_far_sets[k2];
        *fs2 = *s; fs2->n = MAP_FAR_CELLS * MAP_FAR_CELLS * 2; fs2->meshes = map_far_tris;
        fs2->uv = &map_far_uv[(bz % 4) * 4 + bx % 4][0][0];
        map_far_models[k2] = *md; map_far_models[k2].sets = fs2;  // drawn only, never collided with
        nj_meshset *ds = &map_det_sets[k2];
        *ds = *s; ds->uv = &map_det_uv[0][0]; ds->buffer = 0;
        map_det_models[k2] = *md; map_det_models[k2].sets = ds; map_det_models[k2].mats = &map_det_mat;
        nj_object *o = &map_objs[k2];
        o->eval = (1u << 0) | (1u << 1) | (1u << 2);   // don't move, turn or scale
        o->model = md; o->scl[0] = o->scl[1] = o->scl[2] = 1.0f;
        // Collision: 4 quarter pieces (see MAP_QCELLS), quarter (qi, qj) =
        // cell rows qi * 16 to qi * 16 + 15 and columns qj * 16 on.
        for (int qi = 0; qi < 2; qi++)
            for (int qj = 0; qj < 2; qj++) {
                int k4 = k2 * 4 + qi * 2 + qj;
                float *qp = map_qpts[k4], *qn = map_qnrm[k4];
                float lo[3], hi[3];
                for (int i = 0; i < MAP_QNP; i++)
                    for (int j = 0; j < MAP_QNP; j++) {
                        int from = ((qi * MAP_QCELLS + i) * MAP_NP + qj * MAP_QCELLS + j) * 3, to = (i * MAP_QNP + j) * 3;
                        for (int e = 0; e < 3; e++) {
                            qp[to + e] = pts[from + e]; qn[to + e] = nrm[from + e];
                            if ((i == 0 && j == 0) || qp[to + e] < lo[e]) lo[e] = qp[to + e];
                            if ((i == 0 && j == 0) || qp[to + e] > hi[e]) hi[e] = qp[to + e];
                        }
                    }
                float qc[3], r2 = 0;
                for (int e = 0; e < 3; e++) { qc[e] = (lo[e] + hi[e]) / 2; r2 += (hi[e] - qc[e]) * (hi[e] - qc[e]); }
                float qr = sqrt_f(r2);
                nj_meshset *qs = &map_qsets[k4];
                *qs = *s; qs->n = MAP_QCELLS * MAP_QCELLS * 2; qs->meshes = map_qtris; qs->uv = 0; qs->buffer = 0;
                nj_model *qm = &map_qmodels[k4];
                *qm = *md; qm->points = qp; qm->normals = qn; qm->npoints = MAP_QPTS; qm->sets = qs;
                qm->center[0] = qc[0]; qm->center[1] = qc[1]; qm->center[2] = qc[2]; qm->r = qr;
                nj_object *qo = &map_qobjs[k4];
                *qo = *o; qo->model = qm;
                map_cols[k4].c[0] = qc[0]; map_cols[k4].c[1] = qc[1]; map_cols[k4].c[2] = qc[2]; map_cols[k4].r = qr;
                map_cols[k4].obj = qo;
                map_cols[k4].flags = 1 << 0;            // solid, not drawn by SADX
            }
    }
    map_walls(b);
    map_lt.ncol = MAP_COLS; map_lt.col = map_cols;
    map_lt.clip = MAP_DRAW; map_lt.texlist = &map_texlist;
    // Sonic's (and every character's) start in level 11 act 0, through the
    // Mod Loader (HelperFunctions: Version, then RegisterStartPosition).
    {
        static struct { short level, act; float pos[3]; int yrot; } st;
        st.level = MAP_LEVEL; st.act = 0;
        st.pos[0] = map_spawn[0]; st.pos[1] = map_spawn[1] + 10.0f; st.pos[2] = map_spawn[2];
        st.yrot = 0;
        void (__cdecl *reg)(unsigned char, const void *) = helpers ? *(void **)((const char *)helpers + 4) : 0;
        if (reg) for (int c = 0; c < 8; c++) reg((unsigned char)c, &st);
    }
    map_file = b;
    map_now = k;
    obj_load(k, cf, rf, fs, cl);
    bc_load(k, cf, rf, fs, cl);
    sky_load(k, cf, rf, fs, cl);
    gr_load(k, cf, rf, fs, cl);
    PrintDebug("[SADX Truck] map: %s (%s) read, start at %.0f %.0f %.0f\n", map_names[k], name,
               (double)map_spawn[0], (double)map_spawn[1], (double)map_spawn[2]);
    return 1;
}

// ---- Rocks and plants (step 5a) ----------------------------------------
// maps\<map>_obj.bin from map_to_bin.py (its comment has the layout):
// Cabela's rock and plant models and where each copy stands.  Drawn by
// map_draw: each copy within reach (bushes, which have no collision shape,
// within OBJ_NEAR; the rest within OBJ_FAR) with its own matrix
// (nj_apply).  Collision: each copy of a model with a collision shape gets
// that shape moved into place once at load (world points, so no turn
// order to agree on with SADX) and its own landtable piece after the
// ground blocks and walls, solid like them.
#define OBJ_NEAR 1000.0f                 // bushes: drawn within 100 m (about 330 feet)
#define OBJ_FAR MAP_DRAW                 // rocks, cactus, trees: as far as the ground (400 m, about 1/4 mile)
#define OBJ_TEX_MAX 32
typedef struct { int first, n, flags; float rad; } obj_model;
typedef struct { float m[16]; float r; int model; } obj_place;
static unsigned char *obj_file, *obj_arena;
static obj_model *obj_models;
static nj_model *obj_meshes;
static obj_place *obj_places;
static int obj_nmodels, obj_nmeshes, obj_nplaces, obj_ncols;
static nj_texname obj_texnames[OBJ_TEX_MAX];
static struct { nj_texname *textures; u32 n; } obj_texlist = { obj_texnames, 0 };
static u32 obj_gbix0;
static char obj_pack[17];
static int obj_tex_tried, obj_drawn;
// Water (step 6): models with flags (1 << 1).  Their meshes' textures are
// frames of the pack SXWATER (792500 + frame, the same for every map),
// changed 16 times a second (Cabela's TextureAnim(2.0, 32 frames), read
// as 32 frames in 2 seconds), drawn after everything else with depth
// writes off, like Cabela's ZWriteDisable().
// Then the map's reflection picture (Cabela's stage 2, StageOp(SHADER_ADD)),
// added and weighted by its alpha (190, the frames' 75%): src alpha (4,
// (1 << 31)), dst one (1, (1 << 26)); unlit.  Not on the tiles' own uv (it
// repeated every tile, 105 feet): one flat square per tile, built at load
// (water_env_*), uv from the world position, the picture every
// WATER_ENV_SPAN units, mirrored (flip u, flip v: (1 << 18), (1 << 17)).
// Pack entries 32 to 37; which one: maps\<map>_col.bin byte 12 (water_env,
// 1 to 6, 0 = none).
#define WATER_GBIX0 792500u
#define WATER_TEX 38
static nj_texname water_texnames[WATER_TEX];
static struct { nj_texname *textures; u32 n; } water_texlist = { water_texnames, WATER_TEX };
static int water_tex_tried, water_drawn;
static u32 water_env;
#define WATER_ENV_SPAN 1280.0f              // 128 m, about 420 feet
static unsigned char *water_env_mem;
static int water_env_quads;
static nj_meshset water_env_set;
static nj_material water_env_mat;
static nj_model water_env_model;
static float water_time;

static void obj_free(int (__stdcall *vf)(void *, u32, u32))
{
    for (int i = 0; i < obj_nmeshes; i++) buffers_free(obj_meshes[i].sets, 1);
    if (obj_file) vf(obj_file, 0, 0x8000);
    if (obj_arena) vf(obj_arena, 0, 0x8000);
    if (water_env_mem) { buffers_free(&water_env_set, 1); vf(water_env_mem, 0, 0x8000); }
    water_env_mem = 0; water_env_quads = 0;
    obj_file = obj_arena = 0;
    obj_nmodels = obj_nmeshes = obj_nplaces = obj_ncols = 0;
    obj_texlist.n = 0;
    water_tex_tried = 0;
}

static void obj_load(int k, void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *),
                     int (__stdcall *rf)(void *, void *, u32, u32 *, void *), u32 (__stdcall *fs)(void *, u32 *),
                     int (__stdcall *cl)(void *))
{
    char name[400];
    u32 e = 0;                           // <mod folder>\maps\<map>_obj.bin
    for (; e < map_dir_n; e++) name[e] = map_dir[e];
    for (const char *t = "\\maps\\"; *t; ) name[e++] = *t++;
    for (const char *t = map_ids[k]; *t; ) name[e++] = *t++;
    for (const char *t = "_obj.bin"; *t; ) name[e++] = *t++;
    name[e] = 0;
    void *h = cf(name, 0x80000000u, 1, 0, 3, 0, 0);
    if (h == (void *)-1) { PrintDebug("[SADX Truck] map: %s not found, no rocks or plants\n", name); return; }
    u32 size = fs(h, 0), got = 0;
    unsigned char *b = size > 40 && size < (64u << 20) ? VirtualAlloc(0, size, 0x3000, 4) : 0;
    int ok = b && rf(h, b, size, &got, 0) && got == size;
    cl(h);
    obj_file = b;                        // freed by the next map_open, whatever happens here
    const u32 *hd = (const u32 *)b;
    if (!ok || hd[0] != 0x424F5853u /* "SXOB" */ || hd[1] != 1 || hd[4] > OBJ_TEX_MAX || hd[2] > 256) {
        PrintDebug("[SADX Truck] map: %s is not the shape this sadxtruck.dll reads, no rocks or plants\n", name);
        return;
    }
    int nm = (int)hd[2], np = (int)hd[3];
    obj_texlist.n = hd[4]; obj_gbix0 = hd[5];
    for (int i = 0; i < 16; i++) obj_pack[i] = (char)b[24 + i];
    obj_pack[16] = 0;
    // Pass 1: count meshes and collision points, find where things start.
    unsigned char *p = b + 40;
    int nmesh = 0, ncolpt = 0;
    unsigned char *mstart = p;
    static int colpts[256];
    for (int i = 0; i < nm && i < 256; i++) {
        u32 nme = *(u32 *)(p + 8);
        p += 12;
        for (u32 j = 0; j < nme; j++) {
            u32 nv = ((u32 *)p)[2], nt = ((u32 *)p)[3];
            p += 16 + nv * 24 + ((nt * 6 + 3) & ~3u) + nt * 12;
        }
        u32 cn = ((u32 *)p)[0], ct = ((u32 *)p)[1];
        colpts[i] = (int)cn;
        p += 8 + cn * 12 + ((ct * 6 + 3) & ~3u);
        nmesh += (int)nme;
    }
    unsigned char *pstart = p;
    if (pstart + np * 52 != b + size) { PrintDebug("[SADX Truck] map: %s: sizes don't add up, no rocks or plants\n", name); return; }
    for (int j = 0; j < np; j++) {
        u32 m = *(u32 *)(pstart + j * 52);
        if (m < (u32)nm && colpts[m]) { obj_ncols++; ncolpt += colpts[m]; }
    }
    // One block for everything made here.
    u32 need = nm * sizeof(obj_model) + nmesh * (sizeof(nj_model) + sizeof(nj_meshset) + sizeof(nj_material))
             + np * sizeof(obj_place)
             + obj_ncols * (sizeof(nj_model) + sizeof(nj_meshset) + sizeof(nj_object)) + ncolpt * 24
             + (MAP_COLS + obj_ncols) * sizeof(map_col) + 64;
    unsigned char *a = VirtualAlloc(0, need, 0x3000, 4);
    if (!a) { PrintDebug("[SADX Truck] map: no memory for the rocks and plants (%u bytes)\n", need); return; }
    obj_arena = a;
    obj_models = (obj_model *)a; a += nm * sizeof(obj_model);
    obj_meshes = (nj_model *)a; a += nmesh * sizeof(nj_model);
    nj_meshset *sets = (nj_meshset *)a; a += nmesh * sizeof(nj_meshset);
    nj_material *mats = (nj_material *)a; a += nmesh * sizeof(nj_material);
    obj_places = (obj_place *)a; a += np * sizeof(obj_place);
    // Pass 2: models.
    static unsigned char *colat[256];
    static u32 colnt[256];
    p = mstart;
    int mi = 0;
    for (int i = 0; i < nm && i < 256; i++) {
        obj_models[i].flags = (int)((u32 *)p)[0];
        obj_models[i].rad = ((float *)p)[1];
        u32 nme = ((u32 *)p)[2];
        obj_models[i].first = mi; obj_models[i].n = (int)nme;
        p += 12;
        for (u32 j = 0; j < nme; j++, mi++) {
            u32 tex = ((u32 *)p)[0], alpha = ((u32 *)p)[1], nv = ((u32 *)p)[2], nt = ((u32 *)p)[3];
            p += 16;
            float *pts = (float *)p; p += nv * 12;
            float *nrm = (float *)p; p += nv * 12;
            short *tris = (short *)p; p += (nt * 6 + 3) & ~3u;
            short *uv = (short *)p; p += nt * 12;
            nj_material *mt = &mats[mi];
            mt->diffuse = 0xFFB2B2B2u; mt->specular = 0; mt->exponent = 11.0f; mt->tex = tex;
            // texture, both sides, src alpha / inverse src alpha; see-through
            // parts (Cabela's alpha test): use the texture's alpha too
            mt->flags = (1u << 21) | (1u << 23) | (1u << 31) | (1u << 28) | (1u << 26) | (alpha ? (1u << 20) : 0);
            if (obj_models[i].flags & (1 << 1)) mt->tex = 0;   // water: the frame, set when drawn
            nj_meshset *st = &sets[mi];
            st->type_mat = 0; st->n = (unsigned short)nt; st->meshes = tris; st->uv = uv;
            st->attrs = st->normals = st->colors = 0; st->buffer = 0;
            nj_model *md = &obj_meshes[mi];
            md->points = pts; md->normals = nrm; md->npoints = (int)nv;
            md->sets = st; md->mats = mt; md->nsets = 1; md->nmats = 1;
            md->center[0] = md->center[1] = md->center[2] = 0; md->r = obj_models[i].rad;
            md->buffer = 0;
        }
        u32 cn = ((u32 *)p)[0], ct = ((u32 *)p)[1];
        colat[i] = p + 8; colnt[i] = ct;
        p += 8 + cn * 12 + ((ct * 6 + 3) & ~3u);
    }
    obj_nmodels = nm; obj_nmeshes = nmesh;
    // Landtable: the fixed part, then one piece per copy with a shape.
    map_col *cols = (map_col *)a; a += (MAP_COLS + obj_ncols) * sizeof(map_col);
    for (int i = 0; i < MAP_COLS; i++) cols[i] = map_cols_base[i];
    int ci = MAP_COLS;
    // Pass 3: copies.
    for (int j = 0; j < np; j++) {
        const unsigned char *q = pstart + j * 52;
        u32 m = *(const u32 *)q;
        const float *r = (const float *)(q + 4);
        obj_place *pl = &obj_places[j];
        pl->model = m < (u32)nm ? (int)m : 0;
        for (int row = 0; row < 3; row++) {
            pl->m[row * 4] = r[row * 3]; pl->m[row * 4 + 1] = r[row * 3 + 1]; pl->m[row * 4 + 2] = r[row * 3 + 2]; pl->m[row * 4 + 3] = 0;
        }
        pl->m[12] = r[9] + MAP_OFF_X; pl->m[13] = r[10]; pl->m[14] = r[11] + MAP_OFF_Z; pl->m[15] = 1;
        float sc = 0;
        for (int row = 0; row < 3; row++) {
            float l = r[row * 3] * r[row * 3] + r[row * 3 + 1] * r[row * 3 + 1] + r[row * 3 + 2] * r[row * 3 + 2];
            if (l > sc) sc = l;
        }
        pl->r = obj_models[pl->model].rad * sqrt_f(sc);
        int cn = colpts[pl->model];
        if (!cn) continue;
        // The shape, moved into place: world = x * row 0 + y * row 1 + z * row 2 + position.
        const float *src = (const float *)colat[pl->model];
        u32 ct = colnt[pl->model];
        const short *tris = (const short *)(colat[pl->model] + cn * 12);
        float *wp = (float *)a; a += cn * 12;
        float *wn = (float *)a; a += cn * 12;
        float lo[3], hi[3];
        for (int v = 0; v < cn; v++) {
            for (int i = 0; i < 3; i++) {
                wp[v * 3 + i] = src[v * 3] * pl->m[i] + src[v * 3 + 1] * pl->m[4 + i] + src[v * 3 + 2] * pl->m[8 + i] + pl->m[12 + i];
                if (!v || wp[v * 3 + i] < lo[i]) lo[i] = wp[v * 3 + i];
                if (!v || wp[v * 3 + i] > hi[i]) hi[i] = wp[v * 3 + i];
                wn[v * 3 + i] = 0;
            }
        }
        // Point normals: the sum of the faces around each point, made 1 long.
        for (u32 tt = 0; tt < ct; tt++) {
            int i0 = tris[tt * 3], i1 = tris[tt * 3 + 1], i2 = tris[tt * 3 + 2];
            if (i0 >= cn || i1 >= cn || i2 >= cn) continue;
            float e1[3], e2[3];
            for (int i = 0; i < 3; i++) { e1[i] = wp[i1 * 3 + i] - wp[i0 * 3 + i]; e2[i] = wp[i2 * 3 + i] - wp[i0 * 3 + i]; }
            float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            for (int i = 0; i < 3; i++) { wn[i0 * 3 + i] += n[i]; wn[i1 * 3 + i] += n[i]; wn[i2 * 3 + i] += n[i]; }
        }
        for (int v = 0; v < cn; v++) {
            float l = wn[v * 3] * wn[v * 3] + wn[v * 3 + 1] * wn[v * 3 + 1] + wn[v * 3 + 2] * wn[v * 3 + 2];
            if (l > 1e-12f) { l = 1.0f / sqrt_f(l); wn[v * 3] *= l; wn[v * 3 + 1] *= l; wn[v * 3 + 2] *= l; }
            else wn[v * 3 + 1] = 1.0f;
        }
        nj_meshset *st = (nj_meshset *)a; a += sizeof(nj_meshset);
        nj_model *md = (nj_model *)a; a += sizeof(nj_model);
        nj_object *o = (nj_object *)a; a += sizeof(nj_object);
        st->type_mat = 0; st->n = (unsigned short)ct; st->meshes = (short *)tris;
        st->attrs = st->normals = st->colors = 0; st->uv = 0; st->buffer = 0;
        float c[3], rr = 0;
        for (int i = 0; i < 3; i++) c[i] = (lo[i] + hi[i]) / 2;
        for (int v = 0; v < cn; v++) {
            float dx = wp[v * 3] - c[0], dy = wp[v * 3 + 1] - c[1], dz = wp[v * 3 + 2] - c[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > rr) rr = d2;
        }
        rr = sqrt_f(rr);
        md->points = wp; md->normals = wn; md->npoints = cn;
        md->sets = st; md->mats = &map_mats[0]; md->nsets = 1; md->nmats = 1;
        md->center[0] = c[0]; md->center[1] = c[1]; md->center[2] = c[2]; md->r = rr; md->buffer = 0;
        o->eval = (1u << 0) | (1u << 1) | (1u << 2);
        o->model = md; o->pos[0] = o->pos[1] = o->pos[2] = 0; o->ang[0] = o->ang[1] = o->ang[2] = 0;
        o->scl[0] = o->scl[1] = o->scl[2] = 1.0f; o->child = o->sibling = 0;
        map_col *mc = &cols[ci++];
        mc->c[0] = c[0]; mc->c[1] = c[1]; mc->c[2] = c[2]; mc->r = rr;
        mc->wy = mc->wz = 0; mc->blockbits = 0;
        mc->obj = o;
        mc->flags = 1 << 0;                             // solid, not drawn by SADX
    }
    obj_nplaces = np;
    // Water reflection: one flat square per water copy, world points.
    int nw = 0;
    for (int j = 0; j < np; j++) if (obj_models[obj_places[j].model].flags & (1 << 1)) nw++;
    if (nw && nw < 10000) {
        u32 wneed = nw * (4 * 12 * 2 + 6 * 2 + 6 * 4) + 64;
        unsigned char *w = VirtualAlloc(0, wneed, 0x3000, 4);
        if (w) {
            water_env_mem = w;
            float *wp = (float *)w, *wn = wp + nw * 12;
            short *wt = (short *)(wn + nw * 12), *wuv = wt + nw * 6;
            float lo[3], hi[3];
            int q = 0;
            for (int j = 0; j < np; j++) {
                const obj_place *pl = &obj_places[j];
                if (!(obj_models[pl->model].flags & (1 << 1))) continue;
                // The tile is 32 x 32 m: corners at +-160 units, flat, y 0.
                const float cx[4] = { -160, 160, 160, -160 }, cz[4] = { -160, -160, 160, 160 };
                float *p4 = wp + q * 12;
                for (int c = 0; c < 4; c++)
                    for (int i = 0; i < 3; i++) {
                        p4[c * 3 + i] = cx[c] * pl->m[i] + cz[c] * pl->m[8 + i] + pl->m[12 + i];
                        if ((!q && !c) || p4[c * 3 + i] < lo[i]) lo[i] = p4[c * 3 + i];
                        if ((!q && !c) || p4[c * 3 + i] > hi[i]) hi[i] = p4[c * 3 + i];
                    }
                for (int c = 0; c < 4; c++) { wn[q * 12 + c * 3] = 0; wn[q * 12 + c * 3 + 1] = 1; wn[q * 12 + c * 3 + 2] = 0; }
                const short tri[6] = { 0, 1, 2, 0, 2, 3 };
                for (int c = 0; c < 6; c++) {
                    int v = tri[c];
                    wt[q * 6 + c] = (short)(q * 4 + v);
                    float u = (p4[v * 3] - MAP_OFF_X) / WATER_ENV_SPAN, vv = (p4[v * 3 + 2] - MAP_OFF_Z) / WATER_ENV_SPAN;
                    wuv[(q * 6 + c) * 2] = (short)(u * 255.0f);
                    wuv[(q * 6 + c) * 2 + 1] = (short)(vv * 255.0f);
                }
                q++;
            }
            water_env_quads = q;
            nj_meshset *s = &water_env_set;
            s->type_mat = 0; s->n = (unsigned short)(q * 2); s->meshes = wt; s->uv = wuv;
            s->attrs = s->normals = s->colors = 0; s->buffer = 0;
            water_env_mat.diffuse = 0xFFFFFFFFu; water_env_mat.specular = 0; water_env_mat.exponent = 11.0f;
            water_env_mat.tex = 32;              // set to the map's picture when drawn
            water_env_mat.flags = (1u << 21) | (1u << 23) | (1u << 20) | (1u << 25) | (1u << 18) | (1u << 17)
                                | (1u << 31) | (1u << 26);
            nj_model *md = &water_env_model;
            md->points = wp; md->normals = wn; md->npoints = q * 4;
            md->sets = s; md->mats = &water_env_mat; md->nsets = 1; md->nmats = 1;
            float r = 0;
            for (int i = 0; i < 3; i++) { md->center[i] = (lo[i] + hi[i]) / 2; if (hi[i] - lo[i] > r) r = hi[i] - lo[i]; }
            md->r = r; md->buffer = 0;
        }
    }
    map_cols = cols;
    map_lt.ncol = (short)ci; map_lt.col = map_cols;
    obj_tex_tried = 0;
    for (int i = 0; i < OBJ_TEX_MAX; i++) obj_texnames[i].texaddr = 0;
    PrintDebug("[SADX Truck] map: rocks and plants: %d models (%d meshes), %d copies, %d of them solid (%d landtable pieces), %u textures\n",
               nm, nmesh, np, obj_ncols, ci, obj_texlist.n);
}

static void obj_textures(void)
{
    if (!obj_texlist.n) return;
    int ok = 1;
    for (u32 i = 0; i < obj_texlist.n; i++) {
        const u32 *slot = (const u32 *)obj_texnames[i].texaddr;
        if (!slot || slot[0] != obj_gbix0 + i) ok = 0;
    }
    if (ok) { obj_tex_tried = 0; return; }
    if (obj_tex_tried) return;
    obj_tex_tried = 1;
    for (u32 i = 0; i < obj_texlist.n; i++) obj_texnames[i].texaddr = 0;
    LoadPVM(obj_pack, &obj_texlist);
    PrintDebug("[SADX Truck] map: rock and plant textures %s: %s\n", obj_pack, obj_texnames[0].texaddr ? "loaded" : "NOT loaded");
}

// Each copy within reach, with its own matrix on top of the camera's.
static void obj_draw(const float *cam)
{
    obj_drawn = 0;
    if (!obj_nplaces || !obj_texlist.n) return;
    obj_textures();
    njSetTexture(&obj_texlist);
    for (int j = 0; j < obj_nplaces; j++) {
        const obj_place *pl = &obj_places[j];
        const obj_model *om = &obj_models[pl->model];
        float dx = pl->m[12] - cam[0], dy = pl->m[13] - cam[1], dz = pl->m[14] - cam[2];
        if (om->flags & (1 << 1)) continue;             // water: water_draw
        float reach = ((om->flags & 1) ? OBJ_FAR : OBJ_NEAR) + pl->r;
        if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
        njPushMatrixEx();
        nj_apply(pl->m);
        for (int i = 0; i < om->n; i++) draw_buffered(&obj_meshes[om->first + i]);
        njPopMatrixEx();
        obj_drawn++;
    }
}

static void water_draw(const float *cam)
{
    water_drawn = 0;
    if (!obj_nplaces) return;
    int ok = 1;
    for (int i = 0; i < WATER_TEX; i++) {
        const u32 *slot = (const u32 *)water_texnames[i].texaddr;
        if (!slot || slot[0] != WATER_GBIX0 + (u32)i) ok = 0;
    }
    if (!ok) {
        if (water_tex_tried) return;
        water_tex_tried = 1;
        for (int i = 0; i < WATER_TEX; i++) water_texnames[i].texaddr = 0;
        LoadPVM("SXWATER", &water_texlist);
        PrintDebug("[SADX Truck] map: water textures SXWATER: %s\n", water_texnames[0].texaddr ? "loaded" : "NOT loaded");
        if (!water_texnames[WATER_TEX - 1].texaddr) return;
    }
    water_time += 1.0f / 60.0f;
    if (water_time >= 2.0f) water_time -= 2.0f;
    u32 frame = (u32)(water_time * 16.0f) & 31u;
    njSetTexture(&water_texlist);
    Direct3D_EnableZWrite(0);
    for (int j = 0; j < obj_nplaces; j++) {
        const obj_place *pl = &obj_places[j];
        const obj_model *om = &obj_models[pl->model];
        if (!(om->flags & (1 << 1))) continue;
        float dx = pl->m[12] - cam[0], dy = pl->m[13] - cam[1], dz = pl->m[14] - cam[2];
        float reach = OBJ_FAR + pl->r;
        if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
        njPushMatrixEx();
        nj_apply(pl->m);
        for (int i = 0; i < om->n; i++) {
            nj_model *md = &obj_meshes[om->first + i];
            md->mats->tex = frame;
            draw_buffered(md);
        }
        njPopMatrixEx();
        water_drawn++;
    }
    if (water_env && water_env_quads && water_drawn) {
        water_env_mat.tex = 31 + water_env;
        njPushMatrixEx();
        draw_buffered(&water_env_model);
        njPopMatrixEx();
    }
    Direct3D_EnableZWrite(1);
}

// ---- Buildings' collision (step 5b) ------------------------------------
// maps\<map>_col.bin from map_to_bin.py (its comment has the layout):
// Cabela's invisible brushes around buildings, bridges, ramps and fences,
// already turned into triangles, in pieces of about 164 feet square.
// Each piece becomes one more solid landtable piece after the rocks'.
// The points stay in the file's memory (the map offset added once here).
static unsigned char *bc_file, *bc_arena;
static int bc_pieces;

static void bc_free(int (__stdcall *vf)(void *, u32, u32))
{
    if (bc_file) vf(bc_file, 0, 0x8000);
    if (bc_arena) vf(bc_arena, 0, 0x8000);
    bc_file = bc_arena = 0;
    water_env = 0;
    bc_pieces = 0;
}

static void bc_load(int k, void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *),
                    int (__stdcall *rf)(void *, void *, u32, u32 *, void *), u32 (__stdcall *fs)(void *, u32 *),
                    int (__stdcall *cl)(void *))
{
    char name[400];
    u32 e = 0;                           // <mod folder>\maps\<map>_col.bin
    for (; e < map_dir_n; e++) name[e] = map_dir[e];
    for (const char *t = "\\maps\\"; *t; ) name[e++] = *t++;
    for (const char *t = map_ids[k]; *t; ) name[e++] = *t++;
    for (const char *t = "_col.bin"; *t; ) name[e++] = *t++;
    name[e] = 0;
    void *h = cf(name, 0x80000000u, 1, 0, 3, 0, 0);
    if (h == (void *)-1) { PrintDebug("[SADX Truck] map: %s not found, buildings are not solid\n", name); return; }
    u32 size = fs(h, 0), got = 0;
    unsigned char *b = size >= 16 && size < (64u << 20) ? VirtualAlloc(0, size, 0x3000, 4) : 0;
    int ok = b && rf(h, b, size, &got, 0) && got == size;
    cl(h);
    bc_file = b;                         // freed by the next map_open, whatever happens here
    const u32 *hd = (const u32 *)b;
    if (!ok || hd[0] != 0x4C435853u /* "SXCL" */ || (hd[1] != 1 && hd[1] != 2) || hd[2] > 30000) {
        PrintDebug("[SADX Truck] map: %s is not the shape this sadxtruck.dll reads, buildings are not solid\n", name);
        return;
    }
    int np = (int)hd[2];
    u32 hw = hd[1] == 2 ? 12 : 8;        // version 2: u32 flags first in each piece
    water_env = hd[1] == 2 && hd[3] <= 6 ? hd[3] : 0;
    // Check the sizes add up before using anything.
    unsigned char *p = b + 16;
    for (int i = 0; i < np; i++) {
        if (p + hw > b + size) { p = 0; break; }
        u32 n = ((u32 *)(p + hw - 8))[0], nt = ((u32 *)(p + hw - 8))[1];
        if (n > 65535 || nt > 65535) { p = 0; break; }
        p += hw + n * 24 + ((nt * 6 + 3) & ~3u);
    }
    if (p != b + size) { PrintDebug("[SADX Truck] map: %s: sizes don't add up, buildings are not solid\n", name); return; }
    int old = map_lt.ncol;
    if (old + np > 32000) { PrintDebug("[SADX Truck] map: too many landtable pieces (%d), buildings are not solid\n", old + np); return; }
    u32 need = np * (sizeof(nj_meshset) + sizeof(nj_model) + sizeof(nj_object)) + (old + np) * sizeof(map_col) + 64;
    unsigned char *a = VirtualAlloc(0, need, 0x3000, 4);
    if (!a) { PrintDebug("[SADX Truck] map: no memory for the buildings' collision (%u bytes)\n", need); return; }
    bc_arena = a;
    map_col *cols = (map_col *)a; a += (old + np) * sizeof(map_col);
    for (int i = 0; i < old; i++) cols[i] = map_cols[i];
    int ci = old, nwater = 0;
    p = b + 16;
    for (int i = 0; i < np; i++) {
        u32 pflags = hw == 12 ? ((u32 *)p)[0] & ((1u << 0) | (1u << 1)) : (1u << 0);   // solid or water only
        u32 n = ((u32 *)(p + hw - 8))[0], nt = ((u32 *)(p + hw - 8))[1];
        float *wp = (float *)(p + hw);
        float *wn = wp + n * 3;
        short *tris = (short *)(wn + n * 3);
        p += hw + n * 24 + ((nt * 6 + 3) & ~3u);
        if (!pflags) continue;
        if (!n || !nt) continue;
        float lo[3], hi[3];
        for (u32 v = 0; v < n; v++) {
            wp[v * 3] += MAP_OFF_X; wp[v * 3 + 2] += MAP_OFF_Z;
            for (int j = 0; j < 3; j++) {
                if (!v || wp[v * 3 + j] < lo[j]) lo[j] = wp[v * 3 + j];
                if (!v || wp[v * 3 + j] > hi[j]) hi[j] = wp[v * 3 + j];
            }
        }
        float c[3], rr = 0;
        for (int j = 0; j < 3; j++) c[j] = (lo[j] + hi[j]) / 2;
        for (u32 v = 0; v < n; v++) {
            float dx = wp[v * 3] - c[0], dy = wp[v * 3 + 1] - c[1], dz = wp[v * 3 + 2] - c[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > rr) rr = d2;
        }
        rr = sqrt_f(rr);
        nj_meshset *st = (nj_meshset *)a; a += sizeof(nj_meshset);
        nj_model *md = (nj_model *)a; a += sizeof(nj_model);
        nj_object *o = (nj_object *)a; a += sizeof(nj_object);
        st->type_mat = 0; st->n = (unsigned short)nt; st->meshes = tris;
        st->attrs = st->normals = st->colors = 0; st->uv = 0; st->buffer = 0;
        md->points = wp; md->normals = wn; md->npoints = (int)n;
        md->sets = st; md->mats = &map_mats[0]; md->nsets = 1; md->nmats = 1;
        md->center[0] = c[0]; md->center[1] = c[1]; md->center[2] = c[2]; md->r = rr; md->buffer = 0;
        o->eval = (1u << 0) | (1u << 1) | (1u << 2);
        o->model = md; o->pos[0] = o->pos[1] = o->pos[2] = 0; o->ang[0] = o->ang[1] = o->ang[2] = 0;
        o->scl[0] = o->scl[1] = o->scl[2] = 1.0f; o->child = o->sibling = 0;
        map_col *mc = &cols[ci++];
        mc->c[0] = c[0]; mc->c[1] = c[1]; mc->c[2] = c[2]; mc->r = rr;
        mc->wy = mc->wz = 0; mc->blockbits = 0;
        mc->obj = o;
        mc->flags = (int)pflags;                        // solid or water, not drawn by SADX
        if (pflags & (1u << 1)) nwater++;
    }
    bc_pieces = ci - old;
    map_cols = cols;
    map_lt.ncol = (short)ci; map_lt.col = map_cols;
    PrintDebug("[SADX Truck] map: buildings' collision and water: %d pieces, %d of them water (%d landtable pieces in all)\n", bc_pieces, nwater, ci);
}

// ---- Sky (step 6) --------------------------------------------------------
// maps\<map>_sky.bin from map_to_bin.py (its comment has the layout and
// what Cabela's does): 6 faces of 8 x 8 patches, each point a direction.
// Here the directions times SKY_R around the camera: every point the same
// distance away, so from the middle it looks like Cabela's cube.  Drawn
// first in map_draw with depth writes off and the depth test on: anything
// nearer covers it (the ground ends at MAP_DRAW + a block, about 4,500),
// whatever order SADX draws in.  Per face: the clouds (opaque, scrolled
// like Cabela's Translate("Time", 0.015, 0, "Time", 0.005, 0): their uv
// rewritten each frame), then the face's picture by its alpha; the bottom
// face: the bottom color.  Unlit, not from SADX's buffers (the uv move).
// SADX's view distance: ___njClipZ (0x403180, SADXFunctionsNew.h; levels
// pass near -1 and far -3000, Station Square's 0x4209D7) set to SKY_CLIP
// each frame on the map, in map_frame (before anything draws, so the
// whole frame uses the same depth range).
// Depth: Direct3D_EnableZWrite (0x77ED20, SADXFunctions.h).
#define SKY_R 9000.0f
#define SKY_CLIP 12000.0f
#define njClipZ ((void (__cdecl *)(float, float))0x403180)
#define SKY_PTS 256
#define SKY_TRIS 128
static unsigned char *sky_file;
static float sky_pts[6][SKY_PTS][3], sky_nrm[6][SKY_PTS][3];
static short sky_tris[SKY_TRIS * 3];
static short sky_pic_uv[6][SKY_TRIS * 3][2], sky_cloud_uv[6][SKY_TRIS * 3][2];
static const float *sky_cloud_src[6];             // the file's cloud u v per point (7 floats a point)
static nj_material sky_mats[6][2];
static nj_meshset sky_sets[6][2];
static nj_model sky_models[6][2];
static int sky_layers[6];                         // 0, 1 or 2 models per face
static nj_texname sky_texnames[8];
static struct { nj_texname *textures; u32 n; } sky_texlist = { sky_texnames, 0 };
static u32 sky_gbix0;
static char sky_pack[17];
static int sky_tex_tried, sky_ok;
static float sky_time;
// Fog (step 6): level 11 has no fog table in SADX (pFogTable_Stg00 to
// Stg10 and Stg12, SADXVariablesNew.h; no Stg11), so it kept the fog of
// the Station Square act it came from.  Each frame on the map: gFog
// (0x3ABDC60: f32 start, f32 end, both negative like SADX's own tables,
// u32 color, u8 on) = Cabela's fog for the map (maps\<map>_sky.bin
// version 2), then ___njFogEnable (0x411AF0), which copies gFog to the
// screen.  The sky is drawn with fog off (___njFogDisable, 0x411B40),
// like Cabela's FogDisable() in its sky shaders.
#define gFog ((float *)0x3ABDC60)
#define njFogEnable ((void (__cdecl *)(void))0x411AF0)
#define njFogDisable ((void (__cdecl *)(void))0x411B40)
static int sky_fog;                       // 1 = the file had a fog
static float sky_fog_start, sky_fog_end;  // SADX units, positive
static u32 sky_fog_color;

static void sky_free(int (__stdcall *vf)(void *, u32, u32))
{
    if (sky_file) vf(sky_file, 0, 0x8000);
    sky_file = 0; sky_ok = 0; sky_texlist.n = 0; sky_fog = 0;
}

static void sky_load(int k, void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *),
                     int (__stdcall *rf)(void *, void *, u32, u32 *, void *), u32 (__stdcall *fs)(void *, u32 *),
                     int (__stdcall *cl)(void *))
{
    char name[400];
    u32 e = 0;                           // <mod folder>\maps\<map>_sky.bin
    for (; e < map_dir_n; e++) name[e] = map_dir[e];
    for (const char *t = "\\maps\\"; *t; ) name[e++] = *t++;
    for (const char *t = map_ids[k]; *t; ) name[e++] = *t++;
    for (const char *t = "_sky.bin"; *t; ) name[e++] = *t++;
    name[e] = 0;
    void *h = cf(name, 0x80000000u, 1, 0, 3, 0, 0);
    if (h == (void *)-1) { PrintDebug("[SADX Truck] map: %s not found, no sky\n", name); return; }
    u32 size = fs(h, 0), got = 0;
    const u32 want = 32 + 6 * (8 + SKY_PTS * 28);    // version 1; version 2: + 12 bytes of fog
    unsigned char *b = size == want || size == want + 12 ? VirtualAlloc(0, size, 0x3000, 4) : 0;
    int ok = b && rf(h, b, size, &got, 0) && got == size;
    cl(h);
    sky_file = b;
    const u32 *hd = (const u32 *)b;
    if (!ok || hd[0] != 0x4B535853u /* "SXSK" */ || hd[1] != (size == want ? 1u : 2u)) {
        PrintDebug("[SADX Truck] map: %s is not the shape this sadxtruck.dll reads, no sky\n", name);
        return;
    }
    sky_gbix0 = hd[2];
    if (hd[1] == 2) {
        const float *fg = (const float *)(b + want);
        sky_fog_start = fg[0]; sky_fog_end = fg[1]; sky_fog_color = ((const u32 *)fg)[2];
        sky_fog = sky_fog_end > sky_fog_start;
    }
    for (int i = 0; i < 16; i++) sky_pack[i] = (char)b[12 + i];
    sky_pack[16] = 0;
    u32 bottom = hd[7];
    int ntex = 1;
    for (int p = 0; p < 64; p++) {
        const short q[6] = { 0, 1, 3, 3, 2, 0 };
        for (int c = 0; c < 6; c++) sky_tris[p * 6 + c] = (short)(p * 4 + q[c]);
    }
    const unsigned char *fp = b + 32;
    for (int f = 0; f < 6; f++, fp += 8 + SKY_PTS * 28) {
        int pic = ((const int *)fp)[0], clouds = (int)((const u32 *)fp)[1];
        const float *src = (const float *)(fp + 8);
        for (int v = 0; v < SKY_PTS; v++)
            for (int i = 0; i < 3; i++) { sky_pts[f][v][i] = src[v * 7 + i] * SKY_R; sky_nrm[f][v][i] = -src[v * 7 + i]; }
        sky_cloud_src[f] = src;
        for (int c = 0; c < SKY_TRIS * 3; c++) {
            int v = sky_tris[c];
            sky_pic_uv[f][c][0] = (short)(src[v * 7 + 3] * 255.0f + 0.5f);
            sky_pic_uv[f][c][1] = (short)(src[v * 7 + 4] * 255.0f + 0.5f);
        }
        int n = 0;
        // texture, both sides, unlit; blending src alpha / inverse src alpha
        const u32 base = (1u << 23) | (1u << 25) | (1u << 31) | (1u << 28) | (1u << 26);
        if (!clouds && pic < 0) {                        // the bottom: one color
            sky_mats[f][0].diffuse = bottom; sky_mats[f][0].flags = base; sky_mats[f][0].tex = 0;
            sky_sets[f][0].uv = 0;
            n = 1;
        }
        if (clouds) {
            sky_mats[f][n].diffuse = 0xFFFFFFFFu; sky_mats[f][n].flags = base | (1u << 21); sky_mats[f][n].tex = 0;
            sky_sets[f][n].uv = &sky_cloud_uv[f][0][0];
            n++;
        }
        if (pic >= 0 && pic < 8) {                       // + the picture's alpha, clamped
            sky_mats[f][n].diffuse = 0xFFFFFFFFu;
            sky_mats[f][n].flags = base | (1u << 21) | (1u << 20) | (1u << 16) | (1u << 15);
            sky_mats[f][n].tex = (u32)pic;
            sky_sets[f][n].uv = &sky_pic_uv[f][0][0];
            if (pic + 1 > ntex) ntex = pic + 1;
            n++;
        }
        for (int l = 0; l < n; l++) {
            sky_mats[f][l].specular = 0; sky_mats[f][l].exponent = 11.0f;
            nj_meshset *s = &sky_sets[f][l];
            s->type_mat = 0; s->n = SKY_TRIS; s->meshes = sky_tris;
            s->attrs = s->normals = s->colors = 0; s->buffer = 0;
            nj_model *md = &sky_models[f][l];
            md->points = sky_pts[f]; md->normals = sky_nrm[f]; md->npoints = SKY_PTS;
            md->sets = s; md->mats = &sky_mats[f][l]; md->nsets = 1; md->nmats = 1;
            md->center[0] = md->center[1] = md->center[2] = 0; md->r = SKY_R * 1.8f; md->buffer = 0;
        }
        sky_layers[f] = n;
    }
    sky_texlist.n = (u32)ntex;
    sky_tex_tried = 0;
    for (int i = 0; i < 8; i++) sky_texnames[i].texaddr = 0;
    sky_ok = 1;
    PrintDebug("[SADX Truck] map: sky: %u textures in %s, bottom color %06X; fog %s, color %06X, from %.0f to %.0f\n",
               sky_texlist.n, sky_pack, bottom & 0xFFFFFFu, sky_fog ? "on" : "off", sky_fog_color & 0xFFFFFFu,
               (double)sky_fog_start, (double)sky_fog_end);
}

static int sky_textures(void)
{
    int ok = 1;
    for (u32 i = 0; i < sky_texlist.n; i++) {
        const u32 *slot = (const u32 *)sky_texnames[i].texaddr;
        if (!slot || slot[0] != sky_gbix0 + i) ok = 0;
    }
    if (ok) { sky_tex_tried = 0; return 1; }
    if (sky_tex_tried) return 0;
    sky_tex_tried = 1;
    for (u32 i = 0; i < 8; i++) sky_texnames[i].texaddr = 0;
    LoadPVM(sky_pack, &sky_texlist);
    PrintDebug("[SADX Truck] map: sky textures %s: %s\n", sky_pack, sky_texnames[0].texaddr ? "loaded" : "NOT loaded");
    return sky_texnames[0].texaddr != 0;
}

static void sky_draw(const float *cam)
{
    if (!sky_ok || !sky_textures()) return;
    sky_time += 1.0f / 60.0f;
    if (sky_time > 2000.0f) sky_time -= 2000.0f;     // 0.015 x 2000 and 0.005 x 2000: whole turns
    float su = sky_time * 0.015f * 2.0f, sv = sky_time * 0.005f * 2.0f;
    su -= (float)(int)su; sv -= (float)(int)sv;
    for (int f = 0; f < 6; f++) {
        const float *src = sky_cloud_src[f];
        for (int c = 0; c < SKY_TRIS * 3; c++) {
            int v = sky_tris[c];
            float u = src[v * 7 + 5] + su, w = src[v * 7 + 6] + sv;
            sky_cloud_uv[f][c][0] = (short)(u * 255.0f + (u < 0 ? -0.5f : 0.5f));
            sky_cloud_uv[f][c][1] = (short)(w * 255.0f + (w < 0 ? -0.5f : 0.5f));
        }
    }
    njSetTexture(&sky_texlist);
    njFogDisable();
    Direct3D_EnableZWrite(0);
    float m[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  cam[0], cam[1], cam[2], 1 };
    njPushMatrixEx();
    nj_apply(m);
    for (int f = 0; f < 6; f++)
        for (int l = 0; l < sky_layers[f]; l++) njDrawModel_SADX(&sky_models[f][l]);
    njPopMatrixEx();
    Direct3D_EnableZWrite(1);
    if (sky_fog) njFogEnable();
}

// ---- Grass (step 5c) ----------------------------------------------------
// maps\<map>_grass.bin from map_to_bin.py (its comment has the layout and
// how Cabela's grows grass: a square of tufts around the camera, where the
// mask says).  Here the ground is cut into fixed cells of GR_T x GR_T tuft
// spots; each cell within reach of the camera is built once (each spot: a
// fixed nudge and turn from a hash of its number, the mask bit, the ground
// height; a tuft is two crossed upright quads) and kept in a cache of
// GR_CACHE cells, drawn from SADX's buffers like the ground.  The oldest
// cell is reused (its buffer freed first).  Cells are world points, so no
// matrix.  The texture is the last one of the map's SXOBJ pack.
#define GR_T 16                          // tuft spots per cell side
#define GR_CACHE 96
#define GR_MAX (GR_T * GR_T)
#define GR_BUILD_PER_FRAME 6             // new cells per frame at most (spreads the work)
typedef struct {
    int cx, cz, stamp, tufts;
    nj_model md; nj_meshset st; nj_material mt;
    float pts[GR_MAX * 8][3];
    short tris[GR_MAX * 4 * 3];
    short uv[GR_MAX * 4 * 3][2];
} gr_cell;
static unsigned char *gr_file;
static gr_cell *gr_cells;
static float gr_step, gr_w, gr_h, gr_reach, gr_mx0, gr_mdx, gr_mz0, gr_mdz;
static u32 gr_tex, gr_mw, gr_mh;
static const unsigned char *gr_bits;
static int gr_stamp, gr_drawn;
static float gr_up[GR_MAX * 8][3];       // every point's normal: straight up

static void gr_free(int (__stdcall *vf)(void *, u32, u32))
{
    if (gr_cells) {
        for (int i = 0; i < GR_CACHE; i++) buffers_free(&gr_cells[i].st, 1);
        vf(gr_cells, 0, 0x8000);
    }
    if (gr_file) vf(gr_file, 0, 0x8000);
    gr_cells = 0; gr_file = 0; gr_bits = 0;
}

static void gr_load(int k, void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *),
                    int (__stdcall *rf)(void *, void *, u32, u32 *, void *), u32 (__stdcall *fs)(void *, u32 *),
                    int (__stdcall *cl)(void *))
{
    char name[400];
    u32 e = 0;
    for (; e < map_dir_n; e++) name[e] = map_dir[e];
    for (const char *t = "\\maps\\"; *t; ) name[e++] = *t++;
    for (const char *t = map_ids[k]; *t; ) name[e++] = *t++;
    for (const char *t = "_grass.bin"; *t; ) name[e++] = *t++;
    name[e] = 0;
    void *h = cf(name, 0x80000000u, 1, 0, 3, 0, 0);
    if (h == (void *)-1) { PrintDebug("[SADX Truck] map: no grass file for %s (Alaska has none)\n", map_names[k]); return; }
    u32 size = fs(h, 0), got = 0;
    unsigned char *b = size > 56 && size < (4u << 20) ? VirtualAlloc(0, size, 0x3000, 4) : 0;
    int ok = b && rf(h, b, size, &got, 0) && got == size;
    cl(h);
    gr_file = b;
    const u32 *hd = (const u32 *)b;
    const float *hf = (const float *)b;
    if (!ok || hd[0] != 0x52475853u /* "SXGR" */ || hd[1] != 1 || 56 + hd[8] * hd[9] / 8 != size || hd[7] >= obj_texlist.n) {
        PrintDebug("[SADX Truck] map: %s is not the shape this sadxtruck.dll reads, no grass\n", name);
        return;
    }
    gr_step = hf[2]; gr_w = hf[4]; gr_h = hf[5]; gr_reach = hf[6];
    gr_tex = hd[7]; gr_mw = hd[8]; gr_mh = hd[9];
    gr_mx0 = hf[10] + MAP_OFF_X; gr_mdx = hf[11]; gr_mz0 = hf[12] + MAP_OFF_Z; gr_mdz = hf[13];
    gr_bits = b + 56;
    gr_cells = VirtualAlloc(0, GR_CACHE * sizeof(gr_cell), 0x3000, 4);
    if (!gr_cells) { PrintDebug("[SADX Truck] map: no memory for grass\n"); return; }
    for (int i = 0; i < GR_CACHE; i++) { gr_cells[i].stamp = -1; gr_cells[i].st.buffer = 0; }
    for (int i = 0; i < GR_MAX * 8; i++) { gr_up[i][0] = 0; gr_up[i][1] = 1; gr_up[i][2] = 0; }
    gr_stamp = 0;
    PrintDebug("[SADX Truck] map: grass: a tuft every %.1f units, %.0f x %.0f, within %.0f of the camera, texture %u, cache %d cells (%u KB)\n",
               (double)gr_step, (double)gr_w, (double)gr_h, (double)gr_reach, gr_tex, GR_CACHE, (u32)(GR_CACHE * sizeof(gr_cell) / 1024));
}

// The ground's height at x z (SADX units), from the map's points: block
// and cell from block 0's first points, then the cell's triangle
// ((a, b, c) or (b, d, c), as drawn).  NO_GROUND off the map.
static float map_ground_y(float x, float z)
{
    if (!map_file) return NO_GROUND;
    const float *p0 = (const float *)(map_file + 64 + 20);
    float dxc = p0[3] - p0[0];                       // x per column (negative: mirrored)
    float dzr = p0[MAP_NP * 3 + 2] - p0[2];           // z per row
    float gc = (x - p0[0]) / dxc, gr = (z - p0[2]) / dzr;
    if (gc < 0 || gr < 0 || gc >= MAP_NB * MAP_CELLS || gr >= MAP_NB * MAP_CELLS) return NO_GROUND;
    int c = (int)gc, r = (int)gr;
    float fj = gc - c, fi = gr - r;
    int bx = c / MAP_CELLS, bz = r / MAP_CELLS, j = c - bx * MAP_CELLS, i = r - bz * MAP_CELLS;
    const float *pts = (const float *)(map_file + 64 + (bz * MAP_NB + bx) * MAP_BLOCK_BYTES + 20);
    float ya = pts[(i * MAP_NP + j) * 3 + 1], yb = pts[(i * MAP_NP + j + 1) * 3 + 1];
    float yc = pts[((i + 1) * MAP_NP + j) * 3 + 1], yd = pts[((i + 1) * MAP_NP + j + 1) * 3 + 1];
    if (fi + fj <= 1.0f) return ya + (yb - ya) * fj + (yc - ya) * fi;
    return yd + (yc - yd) * (1.0f - fj) + (yb - yd) * (1.0f - fi);
}

static u32 gr_hash(int a, int b, int c)
{
    u32 h = (u32)a * 73856093u ^ (u32)b * 19349663u ^ (u32)c * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

static void gr_build(gr_cell *gc, int cx, int cz)
{
    buffers_free(&gc->st, 1);
    gc->cx = cx; gc->cz = cz; gc->tufts = 0;
    int nv = 0, nt = 0;
    for (int a = 0; a < GR_T; a++)
        for (int bb = 0; bb < GR_T; bb++) {
            int ix = cx * GR_T + a, iz = cz * GR_T + bb;
            float x = (ix + (gr_hash(ix, iz, 1) & 1023) / 1024.0f) * gr_step;
            float z = (iz + (gr_hash(ix, iz, 2) & 1023) / 1024.0f) * gr_step;
            int col = (int)((x - gr_mx0) / gr_mdx), row = (int)((z - gr_mz0) / gr_mdz);
            if (col < 0 || row < 0 || col >= (int)gr_mw || row >= (int)gr_mh) continue;
            if (!(gr_bits[row * (gr_mw / 8) + col / 8] >> (col & 7) & 1)) continue;
            float y = map_ground_y(x, z);
            if (y == NO_GROUND) continue;
            float ang = (gr_hash(ix, iz, 3) & 1023) * (6.2831853f / 1024.0f);
            float ca = cos_f(ang) * gr_w * 0.5f, sa = sin_f(ang) * gr_w * 0.5f;
            float yb = y - gr_h * 0.2f, yt = yb + gr_h;     // Cabela's: middle at ground + 0.3 x height
            const float dir[2][2] = { { ca, sa }, { -sa, ca } };
            for (int q = 0; q < 2; q++) {
                float (*v)[3] = &gc->pts[nv];
                v[0][0] = x - dir[q][0]; v[0][1] = yb; v[0][2] = z - dir[q][1];
                v[1][0] = x + dir[q][0]; v[1][1] = yb; v[1][2] = z + dir[q][1];
                v[2][0] = x - dir[q][0]; v[2][1] = yt; v[2][2] = z - dir[q][1];
                v[3][0] = x + dir[q][0]; v[3][1] = yt; v[3][2] = z + dir[q][1];
                static const short corner[6] = { 0, 1, 2, 1, 3, 2 };
                static const short cu[4] = { 0, 255, 0, 255 }, cvv[4] = { 255, 255, 0, 0 };
                for (int e = 0; e < 6; e++) {
                    gc->tris[nt * 3 + e] = (short)(nv + corner[e]);
                    gc->uv[nt * 3 + e][0] = cu[corner[e]]; gc->uv[nt * 3 + e][1] = cvv[corner[e]];
                }
                nv += 4; nt += 2;
            }
            gc->tufts++;
        }
    nj_material *mt = &gc->mt;
    mt->diffuse = 0xFFB2B2B2u; mt->specular = 0; mt->exponent = 11.0f; mt->tex = gr_tex;
    // texture, both sides, the texture's alpha, src alpha / inverse src alpha, clamped
    mt->flags = (1u << 21) | (1u << 23) | (1u << 20) | (1u << 31) | (1u << 28) | (1u << 26) | (1u << 16) | (1u << 15);
    nj_meshset *st = &gc->st;
    st->type_mat = 0; st->n = (unsigned short)nt; st->meshes = gc->tris; st->uv = &gc->uv[0][0];
    st->attrs = st->normals = st->colors = 0; st->buffer = 0;
    nj_model *md = &gc->md;
    md->points = gc->pts; md->normals = gr_up; md->npoints = nv;
    md->sets = st; md->mats = mt; md->nsets = 1; md->nmats = 1;
    md->center[0] = (cx * GR_T + GR_T * 0.5f) * gr_step; md->center[1] = 0; md->center[2] = (cz * GR_T + GR_T * 0.5f) * gr_step;
    md->r = GR_T * gr_step; md->buffer = 0;
}

static void gr_draw(const float *cam)
{
    gr_drawn = 0;
    if (!gr_cells || !obj_texlist.n) return;
    float cs = GR_T * gr_step;
    int c0 = (int)((cam[0] - gr_reach) / cs) - 1, c1 = (int)((cam[0] + gr_reach) / cs) + 1;
    int z0 = (int)((cam[2] - gr_reach) / cs) - 1, z1 = (int)((cam[2] + gr_reach) / cs) + 1;
    float reach = gr_reach + cs * 0.71f;
    int built = 0;
    gr_stamp++;
    njSetTexture(&obj_texlist);
    for (int cz = z0; cz <= z1; cz++)
        for (int cx = c0; cx <= c1; cx++) {
            float dx = (cx + 0.5f) * cs - cam[0], dz = (cz + 0.5f) * cs - cam[2];
            if (dx * dx + dz * dz > reach * reach) continue;
            gr_cell *hit = 0, *old = &gr_cells[0];
            for (int i = 0; i < GR_CACHE; i++) {
                gr_cell *g = &gr_cells[i];
                if (g->stamp >= 0 && g->cx == cx && g->cz == cz) { hit = g; break; }
                if (g->stamp < old->stamp) old = g;
            }
            if (!hit) {
                if (built >= GR_BUILD_PER_FRAME || old->stamp == gr_stamp) continue;
                gr_build(old, cx, cz);
                built++;
                hit = old;
            }
            hit->stamp = gr_stamp;
            if (!hit->tufts) continue;
            draw_buffered(&hit->md);
            gr_drawn += hit->tufts;
        }
}

static void map_textures(void)
{
    int ok = 1;
    for (u32 i = 0; i < map_texlist.n; i++) {
        const u32 *slot = (const u32 *)map_texnames[i].texaddr;
        if (!slot || slot[0] != (i < 16 ? map_gbix0 + i : map_det_gbix)) ok = 0;
    }
    if (ok) { map_tex_tried = 0; return; }
    if (map_tex_tried) return;
    map_tex_tried = 1;
    for (int i = 0; i < 17; i++) map_texnames[i].texaddr = 0;
    LoadPVM(map_pack, &map_texlist);
    PrintDebug("[SADX Truck] map textures %s: %s\n", map_pack, map_texnames[0].texaddr ? "loaded" : "NOT loaded");
}

// Frame time, once a second while the map is on: the time between two
// draws (the whole frame) and the time inside the map's draw, from
// kernel32's QueryPerformanceCounter.
static int (__stdcall *map_qpc)(long long *);
static long long map_qpf, map_t_last, map_frame_sum, map_frame_worst, map_draw_sum, map_draw_worst;
static int map_t_frames, map_t_near, map_t_far;

static void map_time_log(long long start, long long end, int nnear, int nfar)
{
    if (!map_qpc) {
        void *kl = LoadLibraryExA("kernel32.dll", 0, 0);
        int (__stdcall *qf)(long long *) = kl ? (void *)GetProcAddress(kl, "QueryPerformanceFrequency") : 0;
        map_qpc = kl ? (void *)GetProcAddress(kl, "QueryPerformanceCounter") : 0;
        if (!qf || !qf(&map_qpf) || map_qpf <= 0) map_qpc = 0;
        return;
    }
    if (map_t_last) {
        long long f = start - map_t_last;
        map_frame_sum += f;
        if (f > map_frame_worst) map_frame_worst = f;
        map_draw_sum += end - start;
        if (end - start > map_draw_worst) map_draw_worst = end - start;
        map_t_near += nnear; map_t_far += nfar;
        map_t_frames++;
    }
    map_t_last = start;
    if (map_frame_sum >= map_qpf && map_t_frames) {
        double ms = 1000.0 / (double)map_qpf, n = map_t_frames;
        PrintDebug("[SADX Truck] map: %d frames in the last second, frame %.1f ms (worst %.1f), map draw %.1f ms, "
                   "(worst %.1f), blocks %d full + %d far (%d triangles), %d rocks and plants, %d grass tufts, %d buffers, camera at %.0f %.0f %.0f\n",
                   map_t_frames, map_frame_sum * ms / n, map_frame_worst * ms, map_draw_sum * ms / n, map_draw_worst * ms,
                   (int)(map_t_near / n + 0.5), (int)(map_t_far / n + 0.5),
                   (int)((map_t_near * MAP_CELLS * MAP_CELLS * 2 + map_t_far * MAP_FAR_CELLS * MAP_FAR_CELLS * 2) / n + 0.5), obj_drawn, gr_drawn, nj_buffers_made,
                   camera_twp ? (double)((const float *)(camera_twp + 32))[0] : 0.0,
                   camera_twp ? (double)((const float *)(camera_twp + 32))[1] : 0.0,
                   camera_twp ? (double)((const float *)(camera_twp + 32))[2] : 0.0);
        map_frame_sum = map_frame_worst = map_draw_sum = map_draw_worst = 0;
        map_t_frames = map_t_near = map_t_far = 0;
    }
}

static void __cdecl map_draw(char *tp)
{
    (void)tp;
    if (!map_on || !camera_twp) { map_t_last = 0; return; }
    long long t0 = 0, t1 = 0;
    if (map_qpc) map_qpc(&t0);
    map_textures();
    const float *cam = (const float *)(camera_twp + 32);
    sky_draw(cam);
    njSetTexture(&map_texlist);
    int nnear = 0, nfar = 0;
    for (int k = 0; k < MAP_BLOCKS; k++) {
        const float *c = map_models[k].center;
        float dx = c[0] - cam[0], dy = c[1] - cam[1], dz = c[2] - cam[2];
        float d2 = dx * dx + dy * dy + dz * dz, r = map_models[k].r;
        float reach = MAP_DRAW + r, near = MAP_NEAR + r;
        if (d2 > reach * reach) continue;
        int full = d2 <= near * near;
        if (full) nnear++; else nfar++;
        njPushMatrixEx();
        draw_buffered(full ? &map_models[k] : &map_far_models[k]);
        njPopMatrixEx();
    }
    // Detail over the near blocks, after all the ground.
    if (!map_det_table_ok) {
        u32 old;
        map_det_table_ok = VirtualProtect(NJ_BLEND_TABLE, 32, 0x04, &old) ? 1 : -1;   // PAGE_READWRITE
        PrintDebug("[SADX Truck] map: blend table %s (entries 2, 3: %u %u)\n",
                   map_det_table_ok > 0 ? "writable" : "NOT writable, no ground detail",
                   map_det_table_ok > 0 ? NJ_BLEND_TABLE[2] : 0, map_det_table_ok > 0 ? NJ_BLEND_TABLE[3] : 0);
    }
    if (map_det_gbix && map_texnames[16].texaddr && map_det_table_ok > 0 && NJ_BLEND_TABLE[2] == 3 && NJ_BLEND_TABLE[3] == 4) {
        NJ_BLEND_TABLE[2] = 9; NJ_BLEND_TABLE[3] = 3;        // DESTCOLOR, SRCCOLOR
        for (int k = 0; k < MAP_BLOCKS; k++) {
            const float *c = map_models[k].center;
            float dx = c[0] - cam[0], dy = c[1] - cam[1], dz = c[2] - cam[2];
            float near = MAP_NEAR + map_models[k].r;
            if (dx * dx + dy * dy + dz * dz > near * near) continue;
            njPushMatrixEx();
            draw_buffered(&map_det_models[k]);
            njPopMatrixEx();
        }
        NJ_BLEND_TABLE[2] = 3; NJ_BLEND_TABLE[3] = 4;        // SADX's own: SRCCOLOR, INVSRCCOLOR
    }
    obj_draw(cam);
    gr_draw(cam);
    water_draw(cam);
    if (map_qpc) map_qpc(&t1);
    map_time_log(t0, t1, nnear, nfar);
}
static void __cdecl map_exec(char *tp) { map_draw(tp); }
static void __cdecl map_freed(char *tp) { (void)tp; map_task = 0; }

// Level 11 (LevelIDs_B, never used by SADX) is the map's level.
//
//   Station Square, on foot, L + R 2 s  ->  level 11 act 0 (Sonic at "garage")
//   level 11, on foot, L + R 2 s        ->  back to the Station Square act
//                                           and the spot Sonic left from
//
// SADX has nothing for level 11 in any table, and its file loader
// (LoadLevelFiles 0x422AD0) freezes on purpose for it: its switch on the
// level sends 11 to a print of "Nothin' Module Set Cam" and an endless
// loop.  map_patch points the switch's entry for 11 (jump table 0x4237E0,
// entry 11 at 0x42380C) at map_files instead, which does what the end of
// every other case does (0x4b4c70 and 0x4b4bf0 pick each act's camera and
// object lists; byte 0x3ABDF68 = 1) and then gives every act map_cam:
// with no camera list at all, one camera function (0x434690) would read
// nothing, and an empty list left the camera in one spot.  The object lists stay empty; SADX checks for that.
// Sonic's start (RegisterStartPosition) is set in Init; the landtable is
// GeoLists[11 * 8 + 0] = map_lt.
#define SetNextLevelAndAct_CutsceneMode ((void (__cdecl *)(unsigned char, unsigned char))0x4145D0)
// Level 11's camera list: 64-byte header (u32 count), then one 64-byte
// entry (_OBJ_CAMERAENTRY, SADXStructsNew.h), a copy of the first entry of
// Station Square act 1's list (system\CAMSS00S.BIN; Mystic Ruins'
// CAMMR00S.BIN starts the same way): CAMMD_KNUCKLES (4), the adventure
// field camera that swings round behind the player, adjust
// CAMADJ_RELATIVE2C (19), no zone (shape 0, at 0 0 0).  Windy Valley's
// first entry (CAM0200S.BIN) is the same but for CAMMD_FOLLOW (0) and
// adjust 1; that camera only got dragged along and never turned behind
// the truck.
static union { unsigned char b[128]; u32 count; } map_cam;
static void map_cam_build(void)
{
    unsigned char *e = map_cam.b + 64;
    map_cam.count = 1;
    e[0] = 4;                            // scMode: CAMMD_KNUCKLES
    e[1] = 0;                            // scPriority
    e[2] = 19;                           // ucAdjType: CAMADJ_RELATIVE2C
    e[3] = 0;                            // scColType
    *(unsigned short *)(e + 4) = 18;     // xColAng
    *(float *)(e + 28) = 400.0f;         // zColScl
    *(float *)(e + 36) = 220.0f; *(float *)(e + 40) = 110.0f; *(float *)(e + 44) = 220.0f;  // xyzDirPos
    *(float *)(e + 48) = 200.0f; *(float *)(e + 52) = 22.0f;  *(float *)(e + 56) = 220.0f;  // xyzCamPos
}
static float map_back[2][3];             // Sonic's and Tails' places in Station Square
static int map_back_act, map_back_pending, map_going;
static float map_hold;                   // seconds L + R held

static void __cdecl map_files(void)
{
    ((void (__cdecl *)(int))0x4B4C70)(MAP_LEVEL);
    ((void (__cdecl *)(int))0x4B4BF0)(MAP_LEVEL);
    map_cam_build();
    for (int i = 0; i < 7; i++) ((void **)0x3C58180)[i] = &map_cam;
    *(unsigned char *)0x3ABDF68 = 1;
    PrintDebug("[SADX Truck] map: level %d files: no objects, one camera (Station Square's)\n", MAP_LEVEL);
}

static void map_patch(void)
{
    // A 7-byte thunk: call map_files; pop esi; ret (the end of every case
    // pops esi, pushed at the top of LoadLevelFiles).
    unsigned char *th = VirtualAlloc(0, 16, 0x3000, 0x40);
    u32 *entry = (u32 *)0x42380C, old;
    if (!th || *entry != 0x4237D0u) {
        PrintDebug("[SADX Truck] map: LoadLevelFiles entry for level %d is %u, not the freeze; left alone, no map level\n",
                   MAP_LEVEL, *entry);
        return;
    }
    th[0] = 0xE8; *(u32 *)(th + 1) = (u32)map_files - (u32)(th + 5);
    th[5] = 0x5E; th[6] = 0xC3;
    VirtualProtect(entry, 4, 0x40, &old);
    *entry = (u32)th;
    VirtualProtect(entry, 4, old, &old);
    ((void **)0x97DA08)[MAP_LEVEL * 8] = &map_lt;
    map_patched = 1;
}

// The map menu.  Station Square, on foot, L + R held 2 seconds opens it;
// D-pad up / down picks, A goes, B closes.  While it is open the mod
// blanks Sonic's buttons (OnControl, START stays) and the truck's on-foot
// buttons (on_frame).  The names are SADX's debug text (SADXFunctions.h:
// SetDebugFontSize 0x7808C0, SetDebugFontColor 0x7808E0, DisplayDebugString
// 0x7808F0, position NJM_LOCATION(column, row) = column << 16 | row, in
// characters), redrawn every frame.  The picture is the map's 64 x 64
// overview (Cabela's COLORMAP\<MAP>.TGA), texture pack SXMAPMENU, texture
// number 792256 + map.
#define SetDebugFontSize ((void (__cdecl *)(unsigned short))0x7808C0)
#define SetDebugFontColor ((void (__cdecl *)(int))0x7808E0)
#define DisplayDebugString ((void (__cdecl *)(int, const char *))0x7808F0)
#define MAP_MENU_GBIX0 792256u
static int map_sel;
static u32 map_menu_before;
static nj_texname map_menu_texnames[16];
static struct { nj_texname *textures; u32 n; } map_menu_texlist = { map_menu_texnames, 16 };
static int map_menu_tex_tried;

static void map_menu_open(u32 held)
{
    map_menu = 1;
    map_sel = map_now >= 0 ? map_now : 0;
    map_menu_before = held;              // L and R are still down: not presses
    map_menu_tex_tried = 0;
    make_rings();                        // its draw function draws the picture
    PrintDebug("[SADX Truck] map: menu open\n");
}

static void map_go(int k)
{
    if (!map_have[k]) { PrintDebug("[SADX Truck] map: %s is not built (maps\\%s.bin)\n", map_names[k], map_ids[k]); return; }
    if (!map_open(k)) return;
    map_menu = 0;
    map_going = 1;
    for (int i = 0; i < 3; i++) map_back[0][i] = ((float *)(player_task + 32))[i];
    if (player2_task) for (int i = 0; i < 3; i++) map_back[1][i] = ((float *)(player2_task + 32))[i];
    else for (int i = 0; i < 3; i++) map_back[1][i] = map_back[0][i];
    map_back_act = act_now;
    PrintDebug("[SADX Truck] map: going to %s (level %d) from Station Square act %d\n", map_names[k], MAP_LEVEL, act_now);
    SetNextLevelAndAct_CutsceneMode(MAP_LEVEL, 0);
}

// Each frame: 1 = the menu is open (its buttons are not Sonic's or the truck's).
static int map_menu_frame(u32 held, int on_foot)
{
    if (!map_menu) return 0;
    if (level_now != 26 || !on_foot) { map_menu = 0; PrintDebug("[SADX Truck] map: menu closed (left Station Square or got in)\n"); return 0; }
    if (game_mode != 15) return 1;       // paused: the menu waits, not drawn
    u32 press = held & ~map_menu_before;
    map_menu_before = held;
    if (press & BUTTON_UP) map_sel = (map_sel + 15) % 16;
    if (press & BUTTON_DOWN) map_sel = (map_sel + 1) % 16;
    if (press & BUTTON_B) { map_menu = 0; PrintDebug("[SADX Truck] map: menu closed\n"); return 1; }
    if (press & BUTTON_A) { map_go(map_sel); if (!map_menu) return 1; }
    // Text: rows of screen height / 30 (30 rows, 640 x 480 is 16 pixels).
    int size = screen_h / 30;
    if (size < 8) size = 8;
    SetDebugFontSize((unsigned short)size);
    SetDebugFontColor((int)0xFFFFFFFFu);
    DisplayDebugString(3 << 16 | 3, "PICK A MAP");
    for (int k = 0; k < 16; k++) {
        static char line[16][40];
        char *o = line[k];
        const char *t = map_names[k];
        *o++ = k == map_sel ? '>' : ' ';
        *o++ = ' ';
        while (*t) *o++ = *t++;
        if (!map_have[k]) { t = "  (not built)"; while (*t) *o++ = *t++; }
        *o = 0;
        SetDebugFontColor((int)(k == map_sel ? 0xFFFFFF00u : map_have[k] ? 0xFFFFFFFFu : 0xFF808080u));
        DisplayDebugString(3 << 16 | (5 + k), line[k]);
    }
    SetDebugFontColor((int)0xFFFFFFFFu);
    DisplayDebugString(3 << 16 | 23, "D-pad up/down: pick   A: go   B: close");
    return 1;
}

// The picked map's overview, right of the list, 3 times its size on a
// 640 x 480 screen (192 x 192), found by its texture number.
static void map_menu_picture(void)
{
    if (game_mode != 15) return;
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        const u32 *t = (const u32 *)map_menu_texnames[i].texaddr;
        if (t && t[0] == MAP_MENU_GBIX0 + (u32)map_sel) slot = i;
    }
    if (slot < 0) {
        if (map_menu_tex_tried) return;
        map_menu_tex_tried = 1;
        for (int i = 0; i < 16; i++) map_menu_texnames[i].texaddr = 0;
        LoadPVM("SXMAPMENU", &map_menu_texlist);
        PrintDebug("[SADX Truck] map: menu pictures SXMAPMENU %s\n", map_menu_texnames[0].texaddr ? "loaded" : "NOT loaded");
        return;
    }
    static struct { short sx, sy, cx, cy, u1, v1, u2, v2, texid, attr; } anim;
    static struct { float p[3], sx, sy; int ang; void *tlist, *tanim; } sprite;
    anim.sx = 64; anim.sy = 64; anim.cx = 32; anim.cy = 32;
    anim.u1 = 0; anim.v1 = 0; anim.u2 = 255; anim.v2 = 255;
    anim.texid = (short)slot; anim.attr = 0;
    sprite.p[0] = screen_w * 0.75f; sprite.p[1] = screen_h * 0.4f; sprite.p[2] = 0.0f;
    sprite.sx = sprite.sy = stretch_2d * 3.0f;
    sprite.ang = 0;
    sprite.tlist = &map_menu_texlist; sprite.tanim = &anim;
    SetMaterialAndSpriteColor_Float(1.0f, 1.0f, 1.0f, 1.0f);   // the same settings as draw_picture
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njDrawSprite2D_ForcePriority(&sprite, 0, -1.5f, (1u << 5) | (1u << 4));
    ResetMaterial();
}

static void map_frame(u32 held, int on_foot)
{
    if (!map_patched) return;
    map_on = map_file && level_now == MAP_LEVEL && land_table == (char *)&map_lt;
    if (map_on && !map_task) {
        map_task = CreateElementalTask(0, 1, map_exec);
        if (map_task) { *(void **)(map_task + 20) = (void *)map_draw; *(void **)(map_task + 24) = (void *)map_freed; }
        map_tex_tried = 0;
        map_going = 0;
        float *sp = (float *)(player_task + 32);
        PrintDebug("[SADX Truck] map: %s (level %d), Sonic at %.0f %.0f %.0f\n", map_names[map_now], MAP_LEVEL,
                   (double)sp[0], (double)sp[1], (double)sp[2]);
        // The start point is registered again for each map; if the Mod
        // Loader kept an older one, put him at this map's start.
        float dx = sp[0] - map_spawn[0], dz = sp[2] - map_spawn[2];
        if (dx * dx + dz * dz > 100.0f * 100.0f) {
            sp[0] = map_spawn[0]; sp[1] = map_spawn[1] + 10.0f; sp[2] = map_spawn[2];
            PrintDebug("[SADX Truck] map: not at this map's start, moved there\n");
        }
    }
    // View distance for the sky (see Sky, step 6): every frame on the map.
    // DrawDistanceNegative (0x90082C, SADXVariables.h) logged once, to see
    // what level 11 had.
    if (map_on) {
        static int told;
        if (!told) {
            told = 1;
            PrintDebug("[SADX Truck] map: view distance was %.1f %.1f (DrawDistanceNegative), now near -1, far -%.0f\n",
                       (double)((const float *)0x90082C)[0], (double)((const float *)0x90082C)[1], (double)SKY_CLIP);
        }
        njClipZ(-1.0f, -SKY_CLIP);
        static int fog_told;
        if (!fog_told) {
            fog_told = 1;
            PrintDebug("[SADX Truck] map: fog was %.0f to %.0f, color %08X, %s (from Station Square); now the map's\n",
                       (double)gFog[0], (double)gFog[1], ((const u32 *)gFog)[2], ((const unsigned char *)gFog)[12] ? "on" : "off");
        }
        if (sky_fog) {
            gFog[0] = -(sky_fog_start > 1.0f ? sky_fog_start : 1.0f);
            gFog[1] = -sky_fog_end;
            ((u32 *)gFog)[2] = sky_fog_color;
            ((unsigned char *)gFog)[12] = 1;
            njFogEnable();
        } else {
            ((unsigned char *)gFog)[12] = 0;
            njFogDisable();
        }
    }
    // Back in Station Square from the map: where Sonic left from.
    if (level_now == 26 && map_back_pending && game_mode == 15) {
        map_back_pending = 0;
        map_going = 0;
        for (int i = 0; i < 3; i++) ((float *)(player_task + 32))[i] = map_back[0][i];
        if (player2_task) for (int i = 0; i < 3; i++) ((float *)(player2_task + 32))[i] = map_back[1][i];
        PrintDebug("[SADX Truck] map: back in Station Square act %d at %.0f %.0f %.0f\n", act_now,
                   (double)map_back[0][0], (double)map_back[0][1], (double)map_back[0][2]);
    }
    // Sonic and the walls: SADX lists the walls for him (0x43AEF0 takes
    // every landtable piece within its radius + 400 of the player) but
    // doesn't stop him with them; why is not known (the triangles are about
    // 10,000 by 2,400 units, far bigger than any of SADX's own).  So on
    // foot, past a wall, he is put back 10 units inside it.
    if (map_on && on_foot) {
        float *s = (float *)(player_task + 32);
        float lo[2] = { map_edge[0] + 10.0f, map_edge[2] + 10.0f }, hi[2] = { map_edge[1] - 10.0f, map_edge[3] - 10.0f };
        static int told;
        int moved = 0;
        for (int a = 0; a < 2; a++) {
            float *v = &s[a * 2];
            if (*v < lo[a]) { *v = lo[a]; moved = 1; }
            if (*v > hi[a]) { *v = hi[a]; moved = 1; }
        }
        if (moved && !told) { told = 1; PrintDebug("[SADX Truck] map: Sonic stopped at the edge, %.0f %.0f %.0f\n", (double)s[0], (double)s[1], (double)s[2]); }
        if (!moved) told = 0;
    }
    // Under the ground: on foot near the parked truck Sonic has sunk through
    // the ground (seen once, standing beside it; probably the truck's solid
    // boxes pushing him down, not proven).  The ground only stops things
    // from above, so nothing caught him.  Cabela's maps have nothing under
    // the ground, so more than 6 units (about 2 feet) under it, he is put
    // back 1 unit above it with his speed stopped.
    if (map_on && on_foot) {
        float *s = (float *)(player_task + 32);
        float g = map_ground_y(s[0], s[2]);
        static int caught;
        if (g != NO_GROUND && s[1] < g - 6.0f) {
            if (!caught) {
                const float *tp = truck ? (const float *)(*(const char *const *)((const char *)truck + 4) + 0x28) : 0;
                int ang[3];
                float sadx_g = GetShadowPos(s[0], g + 20.0f, s[2], ang);
                PrintDebug("[SADX Truck] map: Sonic under the ground at %.0f %.0f %.0f (ground %.0f, SADX collision %.0f), put back on it; truck %s at %.0f %.0f %.0f\n",
                           (double)s[0], (double)s[1], (double)s[2], (double)g, (double)sadx_g, tp ? "parked" : "none",
                           tp ? (double)(origin.x + tp[0] * UNITS_PER_METER) : 0.0, tp ? (double)(origin.y + tp[1] * UNITS_PER_METER) : 0.0,
                           tp ? (double)(origin.z + tp[2] * UNITS_PER_METER) : 0.0);
            }
            caught = 1;
            s[1] = g + 1.0f;
            float *spd = (float *)(player_motion + 4);
            spd[0] = spd[1] = spd[2] = 0;
            spd = (float *)(player_work + 56);
            spd[0] = spd[1] = spd[2] = 0;
        } else caught = 0;
    }
    // Camera under the ground: SADX's camera collision (0x462B40) pushes a
    // ball of radius 8 out of the ground at the camera's spot, so a camera
    // that drops more than about 8 units (2 1/2 feet) under in one step, on
    // a steep hill, stays under.  On a map, every frame: below the drawn
    // ground plus 6 units (about 2 feet), lift it to there.  Logged once a
    // second at most.
    if (map_on && camera_twp) {
        float *c = (float *)(camera_twp + 32);
        float g = map_ground_y(c[0], c[2]);
        static int lift_frames, lifts;
        if (g != NO_GROUND && c[1] < g + 6.0f) {
            c[1] = g + 6.0f;
            lifts++;
        }
        if (++lift_frames >= 60) {
            if (lifts) PrintDebug("[SADX Truck] map: camera lifted above the ground in %d of the last 60 frames\n", lifts);
            lift_frames = lifts = 0;
        }
    }
    // Fall-out floor: off the map's edge or through the ground, Sonic goes
    // back to the start point.
    if (map_on && on_foot && ((float *)(player_task + 32))[1] < map_low - 500.0f) {
        float *s = (float *)(player_task + 32);
        PrintDebug("[SADX Truck] map: Sonic fell out at %.0f %.0f %.0f, back to the start\n", (double)s[0], (double)s[1], (double)s[2]);
        s[0] = map_spawn[0]; s[1] = map_spawn[1] + 10.0f; s[2] = map_spawn[2];
    }
    // The truck too (driven or parked): below the same floor, it goes back
    // to the start, facing +x, 1 meter (about 3 feet) up, still.  have_prev
    // = 0 so the sudden stop isn't taken for a crash.
    if (map_on && truck) {
        const float *tp = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x28);
        if (origin.y + tp[1] * UNITS_PER_METER < map_low - 500.0f) {
            PrintDebug("[SADX Truck] map: truck fell out at %.0f %.0f %.0f, back to the start\n",
                       (double)(origin.x + tp[0] * UNITS_PER_METER), (double)(origin.y + tp[1] * UNITS_PER_METER),
                       (double)(origin.z + tp[2] * UNITS_PER_METER));
            origin.x = map_spawn[0]; origin.y = map_spawn[1]; origin.z = map_spawn[2];
            float rot[9] = { 0, 0, -1,   0, 1, 0,   1, 0, 0 };
            SetOrientation(truck, rot);
            tVect3 start = { 0.0f, 1.0f, 0.0f };
            SetPosition(truck, &start);
            SetLinearSpeed(truck, 0, 0, 0);
            SetAngularSpeed(truck, 0, 0, 0);
            landed = lost_ground = 0;
            have_prev = 0;
        }
    }
    if (!map_going && on_foot && (map_on || level_now == 26) && game_mode == 15 && (held & BUTTON_L) && (held & BUTTON_R)) {
        float before = map_hold;
        map_hold += 1.0f / 60.0f;
        if (before < 2.0f && map_hold >= 2.0f) {
            if (!map_on) {
                map_menu_open(held);
            } else {
                map_going = 1;
                map_back_pending = 1;
                PrintDebug("[SADX Truck] map: going back to Station Square act %d\n", map_back_act);
                SetNextLevelAndAct_CutsceneMode(26, (unsigned char)map_back_act);
            }
        }
    } else map_hold = 0;
}
#endif

static void on_frame(void)
{
    m4x4 tm;
    if (!truck) return;
    btai_frame();
    if (!player_task || !pad_raw) {
        if (riding) PadReadOn();
        if (frame) PrintDebug("[SADX Truck] Sonic gone, truck stopped\n");
        if (frame) engine_reset();
        riding = frame = tails_riding = pad_freed = run_over = 0;
        return;
    }
    u32 held = *(const u32 *)(pad_raw + 8);
    if (game_mode != 15) held = 0;      // paused: the pause menu's buttons are not the truck's
#ifndef TEST_EXE
    if (map_menu_frame(held, !riding)) { held = 0; held_before = 0; }   // the map menu's buttons
#endif
    int y_pressed = (held & BUTTON_Y) && !(held_before & BUTTON_Y);
    // Better Tails AI moved Tails beside Sonic after getting out: back to
    // his door (see btai_loaded).
    if (tails_exit_frames > 0 && game_mode == 15) {
        tails_exit_frames--;
        if (!riding && player2_task && tails_exit_k >= 0) {
            exit_place(tails_exit_k, player2_task);
            *(int *)(player2_task + 20) = 0;
            *(int *)(player2_task + 28) = 0;
            if (!tails_exit_frames) PrintDebug("[SADX Truck] Tails put back at the %s (Better Tails AI)\n", exit_names[tails_exit_k]);
        }
    }
    // Truck picker, on foot only: D-pad left (1 << 6) / right (1 << 7).
    int pick = ((held & BUTTON_RIGHT) && !(held_before & BUTTON_RIGHT)) -
               ((held & BUTTON_LEFT) && !(held_before & BUTTON_LEFT));
    held_before = held;
    // Snorkel for the picked truck: on foot, D-pad down: none -> Watermaster
    // -> Watermaster Ultra -> none (Car8, the Nitrate, has no Ultra).
    {
        static u32 down_before;
        int press = (held & BUTTON_DOWN) && !(down_before & BUTTON_DOWN);
        down_before = held;
        if (press && riding && truck) {
            traction_mode = traction_mode % 3 + 1;
            SetTractionType(truck, traction_type[traction_mode]);
            PrintDebug("[SADX Truck] traction: %s (a full tank lasts %d minutes at 3000 RPM)\n",
                       traction_name[traction_mode], fuel_minutes[truck_now][traction_mode - 1]);
        }
        if (press && !riding) {
            static const char *const sname[3] = { "no snorkel", "Watermaster Snorkel", "Watermaster Ultra Snorkel" };
            int s = (snorkel[picked] + 1) % 3;
            if (s == 2 && !trucks[picked].snork_draws[1]) s = 0;
            snorkel[picked] = s;
            PrintDebug("[SADX Truck] %s: %s (engine floods with water %.1f m above the body's middle)\n",
                       trucks[picked].name, sname[s], (double)(0.1f + (s == 1 ? 0.6f : s == 2 ? 1.0f : 0.0f)));
        }
    }
#ifndef TEST_EXE
    map_frame(held, !riding);
#endif
    // Repair the picked truck's engine: on foot, D-pad up held 2 seconds.
    if (!riding && (held & BUTTON_UP)) {
        repair_t += 1.0f / 60.0f;
        if (repair_t >= 2.0f && repair_t < 2.0f + 1.0f / 60.0f) {
            repair_all(picked);
            if (picked == truck_now) dent_restore();    // dents out, windows whole
            dent_has[picked] = 0;
            fuel[picked] = 1.0f;                        // and fills the tank (before the save)
            damage_file_save(picked);                   // the file says: like new
            if (picked == truck_now) { eng_level = -1; radiator_gauge = 0; falling_n = 0; }
            battery = 12.0f;                    // a repair charges the battery too
            tries = 0;                          // and clears a flooded engine
            PrintDebug("[SADX Truck] %s repaired (all parts, battery 12.0 V, tank full)\n", trucks[picked].name);
        }
    } else repair_t = 0;
    if (pick && riding) radio_pick(pick);
    if (pick && !riding) {
        picked = (picked + pick + TRUCKS) % TRUCKS;
        picture_frames = 120;                           // 2 seconds
        make_rings();
        PrintDebug("[SADX Truck] picked %s (Car%d), Y to drive it\n", trucks[picked].name, picked + 1);
    }

    // SADX scenes walk Sonic with SetAutoPilotP (sonic.exe 0x440F30): it
    // writes 2 into his eventwk move mode (taskwk byte 60 -> eventwk byte
    // 36), and his own walking code writes 1 when he arrives; the scene
    // waits for that 1 (Ice Cap's gate scene, Obj_Icecap states 4 and 7).
    // His truck seat mode runs no walking code, so the scene would wait
    // forever with the pad off.  So: scene walking him while riding -> he
    // hops out right there, the same as Y; on foot -> Y gives no truck.
    int scene_walk = sadx_walking(player_task);
    int scene_out = riding && scene_walk;
    if (scene_out) PrintDebug("[SADX Truck] SADX scene is walking Sonic: he left the truck\n");
    if (y_pressed && !riding && scene_walk) {
        PrintDebug("[SADX Truck] SADX scene is walking Sonic: no truck now\n");
        y_pressed = 0;
    }
    if (y_pressed && !riding) {
        // Back into the parked truck when Sonic is within 4 m (about 13
        // feet) of its middle and it is still the picked truck; otherwise
        // a new truck under Sonic, as before.
        int back_in = 0;
        if (frame && picked == truck_now) {
            float c[3];
            truck_center(c);
            const float *pos = (const float *)(player_task + 32);
            float dx = (pos[0] - origin.x) / UNITS_PER_METER - c[0];
            float dy = (pos[1] - origin.y) / UNITS_PER_METER - c[1];
            float dz = (pos[2] - origin.z) / UNITS_PER_METER - c[2];
            back_in = dx * dx + dy * dy + dz * dz < 4.0f * 4.0f;
        }
        if (!back_in) {
            if (!spawn()) {
                PrintDebug("[SADX Truck] no ground under Sonic, no truck\n");
                return;
            }
            frame = 1;
        }
        riding = 1;
        seat_frames = 0;
        truck_center(last_center);
        PadReadOff();
        SetInputP(0, PL_OP_PLACEWITHCART);              // sit down
        // Tails (player 2), wherever he is in the level: into the passenger seat.
        tails_riding = player2_task && player2_motion && player2_work;
        if (tails_riding) {
            SetInputP(1, PL_OP_PLACEWITHCART);
            PrintDebug("[SADX Truck] Tails in (passenger seat)\n");
        }
        if (back_in) PrintDebug("[SADX Truck] Sonic back in the parked truck (engine %s)\n", eng_name());
        else PrintDebug("[SADX Truck] Sonic in\n");
        steer_now = 0.0f;
        make_rings();
    } else if ((y_pressed || scene_out) && riding) {
        riding = 0;
        PressAcceleration(truck, 0.0f);
        SetSteerAngle(truck, 0.0f);
        // Out where there is room: the driver's door, else the passenger
        // door, else behind the truck, else on top (exit_pick).
        int out_k = exit_pick(-1);
        exit_place(out_k, player_task);
        *(int *)(player_task + 20) = 0;                 // stand up straight
        *(int *)(player_task + 28) = 0;
        SetInputP(0, PL_OP_GETOUTOFCART);               // hop out
        if (tails_riding && player2_task) {
            // Out of the passenger door, or the next free spot that is not
            // Sonic's.
            int k2 = exit_pick(out_k);
            exit_place(k2, player2_task);
            *(int *)(player2_task + 20) = 0;
            *(int *)(player2_task + 28) = 0;
            SetInputP(1, PL_OP_GETOUTOFCART);
            PrintDebug("[SADX Truck] Tails out (%s)\n", exit_names[k2]);
            if (btai_loaded()) { tails_exit_k = k2; tails_exit_frames = 2; }
        }
        tails_riding = 0;
        PadReadOn();
        PrintDebug("[SADX Truck] Sonic out (%s), truck parked\n", exit_names[out_k]);
        dent_save(); damage_file_save(truck_now);
    }
    // Pause while riding.  PadReadOff stops SADX copying the pad into
    // perG[0] (sonic.exe 0x40FDEF); OnControl writes Start there instead.
    // Paused (game mode not 15): the pad goes back on for the pause menu,
    // and off again after it.
    if (riding && game_mode != 15) {
        if (!pad_freed) { PadReadOn(); pad_freed = 1; }
    } else if (riding) {
        if (pad_freed) { PadReadOff(); pad_freed = 0; }
    } else pad_freed = 0;
    if (!frame) return;                                 // no truck yet
    // Paused (or any game mode but 15, playing): the truck freezes.  No
    // physics step, no damage, engine and battery wait; its sounds stop
    // and carry on from the same spot afterward.
    {
        static int was_paused;
        if (game_mode != 15) {
            if (!was_paused) snd_pause(1);
            was_paused = 1;
            return;
        }
        if (was_paused) snd_pause(0);
        was_paused = 0;
    }
    frame++;

    float gas = 0, brake = 0, steer = 0;
    // Headlights: D-pad up while driving.
    {
        static u32 up_before;
        if (riding && (held & BUTTON_UP) && !(up_before & BUTTON_UP)) {
            headlights = !headlights;
            PrintDebug("[SADX Truck] headlights %s\n", headlights ? "on" : "off");
        }
        up_before = held;
    }
    if (riding) drive(pad_raw, &gas, &brake, &steer);
    else {
        // Parking brake: nobody driving and the truck slower than 5 mph
        // (2.24 m/s, any direction; body velocity at body + 0x34) -> full
        // brake, so a parked truck stays put.  Faster, it coasts as before.
        // The first frame of a ride, drive() takes the pedals over.
        braking = 0;
        const float *v = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x34);
        float sp2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        PressBrake(truck, sp2 < 2.24f * 2.24f ? 1.0f : 0.0f);
    }
    {
        float c[3];
        truck_center(c);
        gather(c);
    }
#ifndef TH_NOWAKE
    if (riding && !IsInActiveList(truck)) {
        static int woke;
        ActivateObject(truck);
        if (woke++ < 20)
            PrintDebug("[SADX Truck]   frame %4d truck was asleep (EngineDll active list), woken: %s\n",
                       frame, IsInActiveList(truck) ? "yes" : "NO");
    }
#endif
    {
        const float *v = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x34);
        vel_before[0] = v[0]; vel_before[1] = v[1]; vel_before[2] = v[2];
    }
    ManagerUpdate(manager, 1.0f / 60.0f);
    push_out_of_posts();
    if (frame == 2) {
        // Check SetOrientation's layout: the truck should face Sonic's way.
        GetWheelTM(truck, &tm, 0);
        float fx = tm.m[12], fz = tm.m[14];
        GetWheelTM(truck, &tm, 2);
        fx -= tm.m[12]; fz -= tm.m[14];
        int truck_angle = (int)(atan2_f(fz, fx) * (65536.0f / 6.2831853f));
        int off = (truck_angle - spawn_angle) & 65535;
        PrintDebug("[SADX Truck] heading: Sonic %d, truck %d, off by %d degrees\n",
                   spawn_angle & 65535, truck_angle & 65535,
                   (int)((off > 32768 ? off - 65536 : off) * (360.0f / 65536.0f)));
    }
    // SADX taking Sonic out: once seated (10 frames after getting in), his
    // mode (taskwk byte 0; Sonic's cart seat is 45, MD_SONIC_S3A2_CART)
    // must stay the same.  Another mode means SADX moved him (Ice Cap's
    // snowboard, a cutscene, getting killed): park the truck and give him
    // the pad back, without moving him.  Tails the same, on his own.
    if (riding) {
        seat_frames++;
        signed char mode = *(const signed char *)player_task;
        if (seat_frames == 10) seat_mode = mode;
        if (seat_frames > 10 && mode_group(mode) != mode_group(seat_mode)) {
            PrintDebug("[SADX Truck] SADX took Sonic out (his mode %d -> %d), truck parked\n", seat_mode, mode);
            dent_save(); damage_file_save(truck_now);
            riding = 0;
            tails_riding = 0;
            PressAcceleration(truck, 0.0f);
            SetSteerAngle(truck, 0.0f);
            *(int *)(player_task + 20) = 0;              // stand up straight
            *(int *)(player_task + 28) = 0;
            PadReadOn();
        }
        // Better Tails AI's seat: when Tails's mode isn't its 115 while
        // Sonic sits in mode 45, it gives Tails 115 and its roller coaster
        // pose (EV_SetAction, action_m_m9002_miles: hands up).  So once
        // Tails sits in his own cart mode (MD_MILES_S3A2_CART 43, as with
        // plain SADX), the mod sets his mode to 115 itself: Better Tails AI
        // then leaves his pose alone.  If it got there first (115 with its
        // pose), the mod clears that pose (EV_ClrAction 0x42FE40, which
        // takes his task) and asks for his own cart pose directly: playerwk
        // byte 0x124 (mj.reqaction) = 89, what Tails's code sets with mode
        // 43 (sonic.exe 0x45CA5C).  Seating him again with
        // PL_OP_PLACEWITHCART did not work: in mode 115 his code reads no
        // actions (the log showed 5 tries, every one still 115).
        if (tails_riding && player2_task && btai_loaded()) {
            signed char *m2p = (signed char *)player2_task;
            if (seat_frames == 1) btai_seat_ok = 0;
            if (*m2p == 43) {
                *m2p = 115;
                if (!btai_seat_ok) PrintDebug("[SADX Truck] Tails seated in his own pose (Better Tails AI kept off it)\n");
                btai_seat_ok = 1;
            } else if (*m2p == 115 && !btai_seat_ok && player2_tp && player2_work) {
                ((void (__cdecl *)(char *))0x42FE40)(player2_tp);    // EV_ClrAction(task *)
                *(unsigned short *)(player2_work + 0x124) = 89;       // his cart pose
                btai_seat_ok = 1;
                PrintDebug("[SADX Truck] Better Tails AI seated Tails first: its pose cleared, his own cart pose set\n");
            }
        }
        if (tails_riding && player2_task) {
            signed char m2 = *(const signed char *)player2_task;
            if (m2 == 115 && btai_loaded()) m2 = 43;          // the mod's own 115 (above): still seated
            if (seat_frames == 10) seat_mode2 = m2;
            if (seat_frames > 10 && mode_group(m2) != mode_group(seat_mode2)) {
                PrintDebug("[SADX Truck] SADX took Tails out (his mode %d -> %d)\n", seat_mode2, m2);
                tails_riding = 0;
                *(int *)(player2_task + 20) = 0;
                *(int *)(player2_task + 28) = 0;
            }
        }
    }
    // Enemies and bosses hitting Sonic in his seat (H1).  SADX's Sonic_Hurt
    // (sonic.exe 0x492D70) sets his blink timer (taskwk byte 6) to 120 and,
    // in seat mode 45, stops there: no knockback, no ring loss.  So the
    // timer jumping from 0 means a hit: the truck takes it instead, 88
    // points (the 12-25 mph band as the log shows it) on the parts facing
    // the attacker.  Attacker: his colliwk's hit_cwp (byte 164) -> its
    // task (byte 156) -> taskwk (task byte 32) -> position (byte 32), the
    // same chain EnemyCheckDamage (0x4CE030) uses; none -> the front.
    {
        static unsigned short blink_before;
        unsigned short blink = riding ? *(const unsigned short *)(player_task + 6) : 0;
        if (riding && blink > 100 && blink_before == 0) {
            float p[3];
            const char *cwp = *(const char *const *)(player_task + 56);
            const char *hc = cwp ? *(const char *const *)(cwp + 164) : 0;
            const char *tk = hc ? *(const char *const *)(hc + 156) : 0;
            const char *tw = tk ? *(const char *const *)(tk + 32) : 0;
            const char *who = "front, attacker unknown";
            if (tw && tw != player_task) {
                const float *wp = (const float *)(tw + 32);
                p[0] = (wp[0] - origin.x) / UNITS_PER_METER;
                p[1] = (wp[1] - origin.y) / UNITS_PER_METER;
                p[2] = (wp[2] - origin.z) / UNITS_PER_METER;
                who = "facing the attacker";
            } else {
                float f[3] = { 0, car.seat[1], car.tire[0].z + 1.0f };
                truck_point(f, p);                       // SADX units -> meters
                p[0] = (p[0] - origin.x) / UNITS_PER_METER;
                p[1] = (p[1] - origin.y) / UNITS_PER_METER;
                p[2] = (p[2] - origin.z) / UNITS_PER_METER;
            }
            PrintDebug("[SADX Truck] Sonic was hit in his seat (%s): the truck takes it\n", who);
            damage_at(p, 88.0f, "enemy hit");
            dent_enemy(p, "enemy hit");
        }
        blink_before = blink;
    }
    // Running enemies over.  Sonic's shape 0 (form 7, size 4 4 0.6, damage
    // 228 = (1 << 7) | (1 << 6) | (1 << 5) | (1 << 2): hurts objects,
    // enemies, players; attack 0, defense 1; logged in the game).  SADX's
    // rule (sonic.exe 0x418DE0): an enemy is hurt by a shape with (1 << 6)
    // whose attack (bits 0-1) beats the enemy's defense (bits 2-3).  So,
    // riding faster than 15 mph: attack 3 and defense 3 (the low 4 bits
    // all on, as SADX does for invincibility at 0x490ADC), and the shape
    // grown to 25 units (2.5 m, about 8 feet) around his seat.  Slower, or
    // out of the truck: his own values back.
    {
        const float *v = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x34);
        float sp2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        int want = riding && sp2 > RUN_OVER_MPS * RUN_OVER_MPS;
        char *cwp = *(char **)(player_task + 56);
        unsigned char *c = cwp && *(unsigned short *)(cwp + 6) ? *(unsigned char **)(cwp + 12) : 0;
        if (c && want != run_over) {
            float *size = (float *)(c + 20);
            if (want) {
                run_damage = c[3]; run_a = size[0]; run_b = size[1];
                c[3] = (unsigned char)(run_damage | 0x0F | (1 << 6));
                size[0] = size[1] = 25.0f;
                run_check = 30;
            } else {
                c[3] = run_damage; size[0] = run_a; size[1] = run_b;
            }
            run_over = want;
            PrintDebug("[SADX Truck] run-over %s (Sonic's shape 0: damage %d, size %.1f)\n",
                       want ? "on" : "off", c[3], (double)size[0]);
        } else if (!c) run_over = 0;
        // Sonic's own update puts attack 0 and defense 1 back every frame
        // (logged: "SADX changed it"), and it runs before the collision
        // pass (sonic.exe 0x413D58 tasks, then 0x420700 collisions), while
        // OnFrame comes after.  So a task on level 6, the last level run
        // before the collision pass (0x40B170 runs 8, then 0 to 6), sets
        // the bits again just before collisions are checked.
        if (riding && !run_task) {
            run_task = CreateElementalTask(0, 6, run_task_exec);
            if (run_task) *(void (__cdecl **)(char *))(run_task + 24) = run_task_dest;
            PrintDebug("[SADX Truck] run-over task %s (level 6)\n", run_task ? "made" : "NOT made");
        }
    }
    // Once per ride: Sonic's collision shapes (colliwk byte 6 = how many,
    // byte 12 = CCL_INFO list, 48 bytes each), for running enemies over.
    if (riding && seat_frames == 10) {
        const char *cwp = *(const char *const *)(player_task + 56);
        int n = cwp ? *(const unsigned short *)(cwp + 6) : 0;
        const char *info = cwp ? *(const char *const *)(cwp + 12) : 0;
        PrintDebug("[SADX Truck] Sonic's collision: id %d, %d shapes\n", cwp ? *(const unsigned short *)cwp : -1, n);
        for (int k = 0; k < n && k < 8 && info; k++) {
            const unsigned char *c = (const unsigned char *)(info + 48 * k);
            const float *f = (const float *)(c + 8);
            PrintDebug("[SADX Truck]   shape %d: kind %d form %d push %d damage %d attr %u  center %.1f %.1f %.1f  size %.1f %.1f %.1f\n",
                       k, c[0], c[1], c[2], c[3], *(const unsigned *)(c + 4),
                       (double)f[0], (double)f[1], (double)f[2], (double)f[3], (double)f[4], (double)f[5]);
        }
    }
    if (riding) seat_sonic();
    {
        float c[3];
        truck_center(c);
        const float *pos = (const float *)(player_task + 32);
        float dx = (pos[0] - origin.x) / UNITS_PER_METER - c[0];
        float dy = (pos[1] - origin.y) / UNITS_PER_METER - c[1];
        float dz = (pos[2] - origin.z) / UNITS_PER_METER - c[2];
        engine_update(riding, held, gas, brake, sqrt_f(dx * dx + dy * dy + dz * dz), game_mode != 15);
    }
    {
        // Diagnostic: every gear change, to line up with the roll.
        static int gear_before;
        int gear = GetCurGear(truck);
        if (frame == 2) gear_before = gear;
        if (gear != gear_before)
            PrintDebug("[SADX Truck]   frame %4d gear %d -> %d  RPM %6.1f  gas %.2f\n",
                       frame, gear_before, gear, (double)GetRPM(truck), (double)gas);
        gear_before = gear;
    }

    rec_frame(gas, brake, lost_ground == 1);
    if (lost_ground == 1) {
        // How far Sonic is from the truck when its floor disappears.
        float *pos = (float *)(player_task + 32);       // taskwk pos
        GetWheelTM(truck, &tm, 0);
        float dx = (pos[0] - origin.x) / UNITS_PER_METER - tm.m[12];
        float dy = (pos[1] - origin.y) / UNITS_PER_METER - tm.m[13];
        float dz = (pos[2] - origin.z) / UNITS_PER_METER - tm.m[14];
        PrintDebug("[SADX Truck]   frame %4d floor gone, Sonic is %.0f m from the truck\n",
                   frame, (double)sqrt_f(dx * dx + dy * dy + dz * dz));
        lost_ground = 2;
    }
    if (riding && frame % 60 == 0) {
        float c[3];
        truck_center(c);
        float dx = c[0] - last_center[0], dz = c[2] - last_center[2];
        float mph = sqrt_f(dx * dx + dz * dz) * 2.23694f;   // m per second -> mph
        speed_mph_now = (int)mph;
        last_center[0] = c[0]; last_center[1] = c[1]; last_center[2] = c[2];
        PrintDebug("[SADX Truck] second %3d  gas %.2f  brake %.2f  steer %+.2f  %5.1f mph  RPM %6.1f  gear %d  engine %s  battery %.1f V  engine %.0f/%.0f (level %d)  water %s%.2f m\n",
                   frame / 60, (double)gas, (double)brake, (double)steer, (double)mph,
                   (double)GetRPM(truck), GetCurGear(truck), eng_name(), (double)battery,
                   (double)part_pts[truck_now][15], (double)car.parts[15], eng_level,
                   water_rel == NO_WATER ? "none " : "", water_rel == NO_WATER ? 0.0 : (double)water_rel);
    }
}

// OnFrame: the parked truck's solid box is moved away while the truck's own
// physics and ground checks run, and put back on the truck after (it must be
// there when the next frame's tasks move Sonic).
// The cart camera (CameraCart 0x46A490, mode 49 CART) doesn't follow
// Sonic: it aims at a cart position and turn kept in globals, 0x3B2CA38
// (x y z) and 0x3B2CA50 (angle x y z), which only a real cart writes
// (sonic.exe 0x79A2C0, the cart's position and angles; it sets Sonic's
// angle y to 0x4000 - the cart's).  No cart: the camera aims at the last
// cart's spot, or 0 0 0 (plain SADX does this on foot too).  Riding the
// truck in Twinkle Park, the mod writes the truck there instead.  On foot
// while the cart camera runs (mode 49, cameraSystemWork + 6) and Sonic is
// not in a real cart (his mode 45 there, and the cart writes it), the mod
// writes Sonic's position, so plain SADX's on-foot camera follows him too.
static void cart_cam_feed(void)
{
#ifndef TEST_EXE
    if (level_now != 3 || !player_task) return;
    float *cp = (float *)0x3B2CA38;
    int *ca = (int *)0x3B2CA50;
    if (riding && truck) {
        const float *pos = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x28);
        cp[0] = origin.x + pos[0] * UNITS_PER_METER;
        cp[1] = origin.y + pos[1] * UNITS_PER_METER;
        cp[2] = origin.z + pos[2] * UNITS_PER_METER;
    } else {
        if (*(const unsigned char *)(0x3B2CAD8 + 6) != 49 || *(const signed char *)player_task == 45) return;
        const float *sp = (const float *)(player_task + 32);
        cp[0] = sp[0]; cp[1] = sp[1]; cp[2] = sp[2];
    }
    ca[0] = 0;
    ca[1] = 0x4000 - *(const int *)(player_task + 24);
    ca[2] = 0;
#endif
}
// Free camera in Twinkle Park act 1 (the cart act).  The level's task,
// Obj_TwinklePark (sonic.exe 0x61D150, name from SADXFunctions.h), calls
// AllowFreeCamera(0) (0x4348A0) every frame in that act, at 0x61D181, so
// the free camera can never run there.  The mod sends that one call to
// tp_free_cam instead.  Camera flags (0x3B2CBA8, SADXVariables.h):
// (1 << 0) free camera on, (1 << 2) free camera allowed; both are needed
// (sonic.exe 0x434DA1).  On foot, not in a real cart (Sonic's mode 45) and
// not in the truck: allow it and switch it on.  Otherwise: the level's own
// "not allowed", and the free camera goes back to the pause menu setting
// (0x3C49BBC) if the mod switched it on.
typedef void (__cdecl *flag_fn)(int);
static int free_cam_forced;
static void __cdecl tp_free_cam(int allow)
{
#ifndef TEST_EXE
    const flag_fn allow_free = (flag_fn)0x4348A0, enable_free = (flag_fn)0x4348F0;
    int on_foot = player_task && !riding && *(const signed char *)player_task != 45;
    if (on_foot) {
        allow_free(1);
        enable_free(1);
        if (!free_cam_forced) PrintDebug("[SADX Truck] free camera on (Sonic on foot in the cart act)\n");
        free_cam_forced = 1;
        return;
    }
    allow_free(allow);
    if (free_cam_forced) {
        enable_free(*(const int *)0x3C49BBC);
        PrintDebug("[SADX Truck] free camera back to the pause menu setting (%d)\n", *(const int *)0x3C49BBC);
        free_cam_forced = 0;
    }
#else
    (void)allow;
#endif
}
// Egg Hornet's missiles vs the parked truck.  A missile (task exec
// EggHornet_Missile_Main 0x574E00, SADXFunctions.h) signs up as hit id 2
// (CCL_Init at 0x574F92), which SADX checks only against the player,
// bullets and chao, so it flew through the truck.  The mod points the
// missile's task at missile_exec instead (the exec is set at 0x574F64
// and 0x5750D0): before SADX's own code runs, the missile's position is
// tested against the truck's solid box (truck space, SADX units), grown
// by the missile's ball (3 units, shape 0x15E8770).  A hit: damage and a
// dent at the missile, as for any enemy hit on the parked truck, and
// the missile's timer (its motion work + 0x28) is set past its end, so
// SADX's own code blows it up this frame (sound, explosion, removed).
static void __cdecl missile_exec(char *tp)
{
#ifndef TEST_EXE
    char *twp = *(char **)(tp + 32), *mwp = *(char **)(tp + 44);
    if (twp && mwp && truck && !riding && solid_shown && solid_truck == truck_now) {
        const char *body = *(const char *const *)((const char *)truck + 4);
        const float *rot = (const float *)(body + 0x4c), *pos = (const float *)(body + 0x28);
        const float *wp = (const float *)(twp + 32);
        float d[3] = { wp[0] - (origin.x + pos[0] * UNITS_PER_METER),
                       wp[1] - (origin.y + pos[1] * UNITS_PER_METER),
                       wp[2] - (origin.z + pos[2] * UNITS_PER_METER) };
        int in = 1;
        for (int k = 0; k < 3; k++) {
            float l = d[0] * rot[k * 3] + d[1] * rot[k * 3 + 1] + d[2] * rot[k * 3 + 2];
            if (l < solid_lo[k] - 3.0f || l > solid_hi[k] + 3.0f) in = 0;
        }
        if (in) {
            float p[3] = { (wp[0] - origin.x) / UNITS_PER_METER, (wp[1] - origin.y) / UNITS_PER_METER, (wp[2] - origin.z) / UNITS_PER_METER };
            PrintDebug("[SADX Truck] Egg Hornet missile hit the parked truck\n");
            damage_at(p, 88.0f, "missile hit, parked");
            dent_enemy(p, "missile hit, parked");
            *(float *)(mwp + 0x28) = 1.0e9f;
        }
    }
#endif
    ((void (__cdecl *)(char *))0x574E00)(tp);
}
static void missile_patch(void)
{
#ifndef TEST_EXE
    unsigned char *a = (unsigned char *)0x574F64, *b = (unsigned char *)0x5750D0;
    u32 old;
    if (a[0] != 0x68 || *(const u32 *)(a + 1) != 0x574E00u ||
        b[0] != 0xC7 || b[1] != 0x40 || b[2] != 0x10 || *(const u32 *)(b + 3) != 0x574E00u) {
        PrintDebug("[SADX Truck] Egg Hornet missile code: unexpected bytes, not patched\n");
        return;
    }
    VirtualProtect(a, 5, 0x40, &old); *(u32 *)(a + 1) = (u32)missile_exec; VirtualProtect(a, 5, old, &old);
    VirtualProtect(b, 7, 0x40, &old); *(u32 *)(b + 3) = (u32)missile_exec; VirtualProtect(b, 7, old, &old);
    PrintDebug("[SADX Truck] Egg Hornet missile patched\n");
#endif
}
// The Mod Manager's sound settings, from its active profile:
// mods\.modloader\profiles\Profiles.json ("ProfileIndex": N, the Nth
// "Filename") -> that file's "EnableGameSound" (true or false) and
// "SEVolume" (0 to 100).  Read once at startup; the Mod Manager writes them
// before the game starts.  Missing or unreadable: 100 and on.  SEVolume
// also lowers SADX's own sound effects with BASS SFX off (heard in the
// game), so the truck stays in step with SADX.
static u32 read_text(const char *path, char *buf, u32 cap)
{
    void *k = LoadLibraryExA("kernel32.dll", 0, 0);
    void *(__stdcall *cf)(const char *, u32, u32, void *, u32, u32, void *) = k ? (void *)GetProcAddress(k, "CreateFileA") : 0;
    int (__stdcall *rf)(void *, void *, u32, u32 *, void *) = k ? (void *)GetProcAddress(k, "ReadFile") : 0;
    int (__stdcall *ch)(void *) = k ? (void *)GetProcAddress(k, "CloseHandle") : 0;
    if (!cf || !rf || !ch) return 0;
    void *h = cf(path, 0x80000000u, 1, 0, 3, 0, 0);
    if (h == (void *)-1) return 0;
    u32 got = 0;
    if (!rf(h, buf, cap - 1, &got, 0)) got = 0;
    ch(h);
    buf[got] = 0;
    return got;
}
// The text right after "key": (spaces skipped), or 0.
static const char *json_after(const char *t, const char *key, int nth)
{
    for (const char *p = t; *p; p++) {
        if (*p != '"') continue;
        const char *a = p + 1, *b = key;
        while (*b && *a == *b) { a++; b++; }
        if (*b || *a != '"') continue;
        a++;
        while (*a == ' ' || *a == '\t') a++;
        if (*a != ':') continue;
        a++;
        while (*a == ' ' || *a == '\t') a++;
        if (nth-- > 0) continue;
        return a;
    }
    return 0;
}
static void sound_settings(const char *path, unsigned n)
{
    static char buf[16384];
    char dir[400];
    int se = 100, game_sound = 1;
    u32 k = 0;
    for (u32 i = 0; i < n && k < 300; i++) dir[k++] = path[i];
    for (const char *s = "\\..\\.modloader\\profiles\\"; *s; s++) dir[k++] = *s;
    dir[k] = 0;
    u32 d = k;
    for (const char *s = "Profiles.json"; *s; s++) dir[k++] = *s;
    dir[k] = 0;
    const char *prof = "Default.json";
    char pname[100];
    if (read_text(dir, buf, sizeof buf)) {
        const char *ip = json_after(buf, "ProfileIndex", 0);
        int idx = 0;
        while (ip && *ip >= '0' && *ip <= '9') idx = idx * 10 + (*ip++ - '0');
        const char *fp = json_after(buf, "Filename", idx);
        if (fp && *fp == '"') {
            u32 j = 0;
            fp++;
            while (*fp && *fp != '"' && j < sizeof pname - 1) pname[j++] = *fp++;
            pname[j] = 0;
            prof = pname;
        }
    }
    k = d;
    for (const char *s = prof; *s && k < 390; s++) dir[k++] = *s;
    dir[k] = 0;
    int found = 0;
    if (read_text(dir, buf, sizeof buf)) {
        const char *sp = json_after(buf, "SEVolume", 0);
        if (sp && *sp >= '0' && *sp <= '9') { se = 0; while (*sp >= '0' && *sp <= '9') se = se * 10 + (*sp++ - '0'); found = 1; }
        const char *gp = json_after(buf, "EnableGameSound", 0);
        if (gp && *gp == 'f') game_sound = 0;
    }
    if (se > 100) se = 100;
    snd_master = game_sound ? 0.8f * (float)truck_volume / 100.0f * (float)se / 100.0f : 0.0f;
    PrintDebug("[SADX Truck] sound: TruckVolume %d, Mod Manager SEVolume %d%s, game sound %s -> truck sounds at %d%%\n",
               truck_volume, se, found ? "" : " (profile not read, 100 used)", game_sound ? "on" : "off",
               (int)(snd_master / 0.8f * 100.0f + 0.5f));
}
// Leaving the cart act on foot (level change, or the act ends) stops
// Obj_TwinklePark's calls, so tp_free_cam never gets to switch the free
// camera back; every later level kept it on.  OnFrame does it here: any
// level or act but Twinkle Park act 1, and the mod still has it forced.
static void free_cam_release(void)
{
#ifndef TEST_EXE
    if (!free_cam_forced) return;
    if (level_now == 3 && *(const short *)0x3B22DEC == 0) return;
    ((flag_fn)0x4348F0)(*(const int *)0x3C49BBC);
    free_cam_forced = 0;
    PrintDebug("[SADX Truck] left the cart act: free camera back to the pause menu setting (%d)\n", *(const int *)0x3C49BBC);
#endif
}
static void free_cam_patch(void)
{
#ifndef TEST_EXE
    unsigned char *p = (unsigned char *)0x61D181;
    u32 old, want = 0x4348A0u - 0x61D186u, rel = (u32)tp_free_cam - 0x61D186u;
    if (p[0] != 232 || *(const u32 *)(p + 1) != want) {
        PrintDebug("[SADX Truck] Twinkle Park free camera call at 0x61D181: unexpected bytes, not patched\n");
        return;
    }
    VirtualProtect(p, 5, 0x40, &old);
    *(u32 *)(p + 1) = rel;
    VirtualProtect(p, 5, old, &old);
    PrintDebug("[SADX Truck] Twinkle Park free camera call patched\n");
#endif
}
// The Past's walking echidnas, "O PEOPLE" (sonic.exe 0x543AF0, Past object
// list).  Each one that walks a path has a "player near" callback at
// awp + 40, 0x5430F0: the vanilla "stop and stand when the player comes
// close".  The mod sends 0x5430F0 to people_near.
// An ancient tribe that sees a truck panics: the truck (driven or parked)
// within 150 feet of any walker -> every walker in the city runs, state 2
// (walk) with the path walker's speed (awp + 44 -> + 16, vanilla 0.5) at
// 1.5, which plays the walk motion 3 times as fast and moves 3 times as
// far each frame.  The panic lasts until no walker has seen the truck for
// 10 seconds.  While it lasts, sounds\crowd_panic.wav loops (if present;
// the mod ships no such file).
// Otherwise the vanilla code runs, from its first 8 bytes copied out
// (mov ecx,[esp+4] / sub esp,12 / push esi) and a jump back to 0x5430F8.
#define PEOPLE_LEVEL 34                    // LevelIDs_Past
#define PEOPLE_SIGHT2 (457.0f * 457.0f)    // 150 feet, in SADX units, squared
#define PEOPLE_CALM_FRAMES 600             // 10 seconds
#define PEOPLE_RUN 1.5f
#define PEOPLE_WALK 0.5f
typedef int (__cdecl *people_fn)(char *tp);
static unsigned char *people_vanilla;      // the copied 8 bytes + jmp 0x5430F8
static float people_truck[3];              // the truck, SADX units
static int people_truck_here;              // the truck is in the Past this frame
static int people_seen;                    // a walker saw the truck this frame
static int people_panic;                   // frames of panic left
static int people_count;                   // walkers running this frame
static int people_running;                 // the most running at once in this panic

static int __cdecl people_near(char *tp)
{
    char *twp = *(char **)(tp + 32), *awp = *(char **)(tp + 44);
    char *walk = awp ? *(char **)(awp + 44) : 0;
    if (twp && walk) {
        const float *p = (const float *)(twp + 32);
        float *speed = (float *)(walk + 16);
        int *state = (int *)awp;
        if (people_truck_here) {
            float dx = p[0] - people_truck[0], dz = p[2] - people_truck[2];
            if (dx * dx + dz * dz < PEOPLE_SIGHT2) people_seen = 1;
        }
        if (people_panic && state[0] != 1 && state[0] != 3) {   // 1 and 3: vanilla leaves those alone too
            *speed = PEOPLE_RUN;
            if (state[0] != 2 && state[1] != 2) {           // blend into walk, like vanilla 0x5431AA
                state[1] = 2;
                *(float *)(awp + 28) = *(float *)(twp + 8);
                *(float *)(awp + 32) = 1.0f / 12.0f;
                *(float *)(twp + 8) = 0.0f;
            }
            people_count++;
            return 0;                                       // 0: no "look at the player"
        }
        if (*speed == PEOPLE_RUN) *speed = PEOPLE_WALK;
    }
    return ((people_fn)people_vanilla)(tp);
}

// OnFrame: where the truck is, and the panic's start and end.
static void people_frame(void)
{
#ifndef TEST_EXE
    int was = people_panic;
    if (people_count > people_running) people_running = people_count;
    people_count = 0;
    if (people_seen) people_panic = PEOPLE_CALM_FRAMES;
    else if (people_panic) people_panic--;
    if (level_now != PEOPLE_LEVEL) people_panic = 0;
    if (people_panic && !was) {
        PrintDebug("[SADX Truck] Past: the tribe saw the truck, panic\n");
        snd_play(PANIC_WAV, 1);
        snd_set(PANIC_WAV, 70.0f, 1.0f);
    }
    if (!people_panic && was) {
        PrintDebug("[SADX Truck] Past: echidnas calm again (%d were running)\n", people_running);
        snd_stop(PANIC_WAV);
    }
    people_seen = 0;
    if (!people_panic) people_running = 0;     // while it lasts: the most seen running at once
    people_truck_here = 0;
    if (!people_vanilla || !truck || !frame || level_now != PEOPLE_LEVEL) return;
    const char *body = *(const char *const *)((const char *)truck + 4);
    const float *pos = (const float *)(body + 0x28);
    people_truck[0] = origin.x + pos[0] * UNITS_PER_METER;
    people_truck[1] = origin.y + pos[1] * UNITS_PER_METER;
    people_truck[2] = origin.z + pos[2] * UNITS_PER_METER;
    people_truck_here = 1;
#endif
}

// Radio, driving only: D-pad left / right picks the station (OFF, then
// the stations below).  SADX's own songs (MusicList, sonic.exe 0x910298,
// files in system\sounddata\bgm\wma\) and Your Music (any files in the
// mod folder's radio\).  The mod plays them on its own BASS channel:
// bass.dll and bass_vgmstream.dll are the Mod Loader's (extlib\BASS),
// loaded by it when "Enable BASS music" is on.  The Mod Loader opens a
// song with BASS_VGMSTREAM_StreamCreate (path, flags), cdecl, and when
// that fails BASS_StreamCreateFile; music volume (MusicVolume 0x909F28 +
// 10000) / 30000 (SADXModLoader.dll 0x10095F30).  The radio does the same.
// While it can be heard, the level's music is paused: PauseMusic 0x40D060
// and ResumeMusic 0x40D0A0, which the Mod Loader sends to its own
// (SADXModLoader.dll 0x100961F0 / 0x10096250).  Those count: each pause
// adds 1, each resume takes 1 off and resumes at 0 (they also pause and
// resume voices).  So the radio pauses once, and once more each time the
// game starts a new song (CurrentSong 0x912698), and gives back exactly
// as many resumes.  On foot the radio is heard from the truck: full
// within 15 feet, fading to nothing at 100 feet, where the level's music
// comes back.
// Stations keep "playing" while nobody listens: a clock runs while the
// game runs, and tuning in skips ahead by the time since the song began.
#define RADIO_MAX_SONGS 64
struct radio_song { const char *file, *title; };
struct radio_station {
    const char *name;
    struct radio_song song[RADIO_MAX_SONGS];
    int n;
    int order[RADIO_MAX_SONGS], at, started;   // shuffled play order, the song now
    float began;                               // radio clock when the song now began
};
#define BGM(f, t) { "system\\sounddata\\bgm\\wma\\" f ".wma", t }
static struct radio_station radio_st[] = {
    { "CRUSH 40 ROCK", { BGM("mainthem", "Open Your Heart"), BGM("thesonic", "It Doesn't Matter"),
                         BGM("themiles", "Believe in Myself"), BGM("theknkls", "Unknown from M.E."),
                         BGM("theamy", "My Sweet Passion"), BGM("thebig", "Lazy Days"),
                         BGM("thee102", "E-102 Gamma") }, 7 },
    { "STATION SQUARE FM", { BGM("s_square", "Station Square"), BGM("mstcln", "Mystic Ruins"),
                             BGM("egcarer1", "Egg Carrier 1"), BGM("egcarer2", "Egg Carrier 2"),
                             BGM("casino1", "Casinopolis 1"), BGM("casino2", "Casinopolis 2") }, 6 },
    { "ACTION STAGE", { BGM("ecoast1", "Emerald Coast"), BGM("wndyvly1", "Windy Valley"),
                        BGM("twnklpk1", "Twinkle Park"), BGM("highway1", "Speed Highway"),
                        BGM("redmntn1", "Red Mountain"), BGM("skydeck1", "Sky Deck"),
                        BGM("icecap1", "Ice Cap"), BGM("lstwrld1", "Lost World"),
                        BGM("finaleg1", "Final Egg"), BGM("sandhill", "Sand Hill"),
                        BGM("circuit", "Twinkle Circuit") }, 11 },
    { "CHAO CHILL", { BGM("chao", "Chao Garden"), BGM("chaohall", "chaohall"), BGM("chaorace", "Chao Race"),
                      BGM("chao_g_iede", "chao_g_iede"), BGM("chao_k_m", "chao_k_m") }, 5 },
    { "YOUR MUSIC", { { 0, 0 } }, 0 },          // filled from the radio\ folder
};
#undef BGM
#define RADIO_N (int)(sizeof radio_st / sizeof radio_st[0])
#define RADIO_YOURS (RADIO_N - 1)
static int radio_on;                           // 0 OFF, else station radio_on - 1
static int radio_state;                        // 0 not tried, 1 ready, -1 no BASS
static u32 radio_h;                            // the BASS channel playing, 0 none
static int radio_paused;                       // radio_h paused for the pause menu
static int radio_music_pauses;                 // PauseMusic calls the radio has not given back
static int radio_seen_song;                    // CurrentSong when the radio last paused
static float radio_clock;                      // seconds the game has run
static int radio_banner;                       // frames the banner still shows
static int radio_dead;                         // the station picked has nothing that opens
static const char *radio_now_title;
static u32 radio_rand = 12345;
static char radio_names[RADIO_MAX_SONGS][64];  // Your Music: paths
static char radio_titles[RADIO_MAX_SONGS][40]; // and titles
typedef unsigned long long u64;
static u32 (__cdecl *vg_create)(const char *, u32);
static u32 (__stdcall *bs_create)(int, const void *, u64, u64, u32);
static int (__stdcall *bs_play)(u32, int);
static int (__stdcall *bs_pause)(u32);
static int (__stdcall *bs_free)(u32);
static int (__stdcall *bs_attr)(u32, u32, float);
static u32 (__stdcall *bs_active)(u32);
static u64 (__stdcall *bs_len)(u32, u32);
static int (__stdcall *bs_setpos)(u32, u64, u32);
static double (__stdcall *bs_b2s)(u32, u64);
static u64 (__stdcall *bs_s2b)(u32, double);

static u32 radio_rnd(void) { radio_rand = radio_rand * 1103515245u + 12345u; return radio_rand >> 8; }

// Your Music: every file in the mod folder's radio\ (snd_dir is
// "<mod>\sounds\"); BASS or vgmstream decide what they can play.
static void radio_scan(void)
{
    void *k = LoadLibraryExA("kernel32.dll", 0, 0);
    void *(__stdcall *ff)(const char *, void *) = (void *)GetProcAddress(k, "FindFirstFileA");
    int (__stdcall *fn)(void *, void *) = (void *)GetProcAddress(k, "FindNextFileA");
    int (__stdcall *fc)(void *) = (void *)GetProcAddress(k, "FindClose");
    char dir[340], pat[350];
    u32 n = 0;
    for (const char *s = snd_dir; *s && n < 300; s++) dir[n++] = *s;
    if (n >= 7) n -= 7;                        // "sounds\" off
    for (const char *s = "radio\\"; *s; s++) dir[n++] = *s;
    dir[n] = 0;
    for (u32 i = 0; i <= n; i++) pat[i] = dir[i];
    pat[n] = '*'; pat[n + 1] = 0;
    struct radio_station *st = &radio_st[RADIO_YOURS];
    unsigned char fd[320];                     // WIN32_FIND_DATAA: attributes at 0, name at 44
    void *h = ff ? ff(pat, fd) : (void *)-1;
    if (h == (void *)-1) { PrintDebug("[SADX Truck] radio: no %s folder, Your Music is empty\n", dir); return; }
    do {
        const char *nm = (const char *)fd + 44;
        if (*(u32 *)fd & (1u << 4)) continue;  // a folder
        u32 a = 0, b = 0, dot = 0;
        for (; dir[a] && a < 63; a++) radio_names[st->n][a] = dir[a];
        for (const char *s = nm; *s && a < 63; s++) radio_names[st->n][a++] = *s;
        radio_names[st->n][a] = 0;
        if (a >= 63) continue;                 // path too long
        for (const char *s = nm; *s && b < 39; s++, b++) { radio_titles[st->n][b] = *s; if (*s == '.') dot = b; }
        radio_titles[st->n][dot ? dot : b] = 0;
        st->song[st->n].file = radio_names[st->n];
        st->song[st->n].title = radio_titles[st->n];
        st->n++;
    } while (st->n < RADIO_MAX_SONGS && fn(h, fd));
    fc(h);
    PrintDebug("[SADX Truck] radio: Your Music has %d file(s) in %s\n", st->n, dir);
}

static void radio_init(void)
{
    if (radio_state) return;
    radio_state = -1;
    void *k = LoadLibraryExA("kernel32.dll", 0, 0);
    void *(__stdcall *gmh)(const char *) = (void *)GetProcAddress(k, "GetModuleHandleA");
    u32 (__stdcall *tick)(void) = (void *)GetProcAddress(k, "GetTickCount");
    void *b = gmh ? gmh("bass.dll") : 0, *v = gmh ? gmh("bass_vgmstream.dll") : 0;
    if (!b) { PrintDebug("[SADX Truck] radio off: bass.dll not loaded (SA Mod Manager: turn on \"Enable BASS music\")\n"); return; }
    vg_create = v ? (void *)GetProcAddress(v, "BASS_VGMSTREAM_StreamCreate") : 0;
    bs_create = (void *)GetProcAddress(b, "BASS_StreamCreateFile");
    bs_play = (void *)GetProcAddress(b, "BASS_ChannelPlay");
    bs_pause = (void *)GetProcAddress(b, "BASS_ChannelPause");
    bs_free = (void *)GetProcAddress(b, "BASS_StreamFree");
    bs_attr = (void *)GetProcAddress(b, "BASS_ChannelSetAttribute");
    bs_active = (void *)GetProcAddress(b, "BASS_ChannelIsActive");
    bs_len = (void *)GetProcAddress(b, "BASS_ChannelGetLength");
    bs_setpos = (void *)GetProcAddress(b, "BASS_ChannelSetPosition");
    bs_b2s = (void *)GetProcAddress(b, "BASS_ChannelBytes2Seconds");
    bs_s2b = (void *)GetProcAddress(b, "BASS_ChannelSeconds2Bytes");
    if (!bs_create || !bs_play || !bs_pause || !bs_free || !bs_attr || !bs_active || !bs_len ||
        !bs_setpos || !bs_b2s || !bs_s2b) { PrintDebug("[SADX Truck] radio off: bass.dll is missing a function\n"); return; }
    radio_rand = tick ? tick() : 12345u;
    radio_scan();
    for (int s = 0; s < RADIO_N; s++) {        // shuffle each station's order
        struct radio_station *st = &radio_st[s];
        for (int i = 0; i < st->n; i++) st->order[i] = i;
        for (int i = st->n - 1; i > 0; i--) {
            int j = (int)(radio_rnd() % (u32)(i + 1)), t = st->order[i];
            st->order[i] = st->order[j]; st->order[j] = t;
        }
    }
    radio_state = 1;
    PrintDebug("[SADX Truck] radio ready (BASS%s)\n", vg_create ? " + vgmstream" : ", no vgmstream: SADX's WMA songs will not open");
}

// Open one song, paused at its start.  A mod that replaces the file
// (GetReplaceablePath, HelperFunctions slot 17) is followed, like SADX's music.
static u32 radio_open(const char *file, float *len)
{
#ifndef TEST_EXE
    const char *(__cdecl *rp)(const char *) = map_helpers ? *(void **)((const char *)map_helpers + 68) : 0;
#else
    const char *(__cdecl *rp)(const char *) = 0;
#endif
    const char *p = rp ? rp(file) : file;
    // Plain BASS first: it knows the length (WMA through Windows' own
    // codecs, MP3, OGG, WAV).  Then vgmstream (ADX and other game formats),
    // whose length reads as unknown (-1): *len 0, played from its start.
    u32 h = bs_create(0, p, 0, 0, 0);
    if (!h && vg_create) h = vg_create(p, 0);
    if (!h) return 0;
    u64 n = bs_len(h, 0);
    *len = n == (u64)-1 ? 0.0f : (float)bs_b2s(h, n);
    if (*len > 36000.0f) *len = 0.0f;           // over 10 hours: unknown too
    return h;
}

// Tune in: the station's song now, at the spot it has reached.
static void radio_tune(void)
{
    struct radio_station *st = &radio_st[radio_on - 1];
    if (!st->n) { PrintDebug("[SADX Truck] radio: %s has no songs\n", st->name); radio_dead = 1; return; }
    int fails = 0;
    for (int tries = 0; tries < 40; tries++) {
        const struct radio_song *sg = &st->song[st->order[st->at]];
        float len;
        u32 h = radio_open(sg->file, &len);
        if (!h) {
            PrintDebug("[SADX Truck] radio: could not open %s, skipped\n", sg->file);
            st->at = (st->at + 1) % st->n;
            st->began = radio_clock;
            if (++fails >= st->n) {                // nothing on this station opens
                radio_dead = 1;
                PrintDebug("[SADX Truck] radio: nothing on %s opens; pick it again to retry\n", st->name);
                return;
            }
            continue;
        }
        if (len < 1.0f) {                      // length unknown: from its start
            st->began = radio_clock;
            st->started = 1;
            len = 0;
        }
        if (!st->started) {                    // first time: as if it had been on a while
            st->at = (int)(radio_rnd() % (u32)st->n);
            st->began = radio_clock - (float)(radio_rnd() % 1000u) / 1000.0f * len * 0.8f;
            st->started = 1;
            bs_free(h);
            continue;
        }
        float off = radio_clock - st->began;
        if (off < 0 || !len) off = 0;
        if (len && off > len - 1.0f) {                // that song ended while nobody listened
            bs_free(h);
            st->began += len;
            st->at = (st->at + 1) % st->n;
            if (tries == 39) st->began = radio_clock;
            continue;
        }
        if (off > 0.5f) bs_setpos(h, bs_s2b(h, (double)off), 0);
        bs_attr(h, 2, (float)(*(const int *)0x909F28 + 10000) / 30000.0f);
        bs_play(h, 0);
        radio_h = h;
        radio_paused = 0;
        radio_now_title = sg->title;
        PrintDebug("[SADX Truck] radio: %s, %s (from %.0f s of %.0f)\n", st->name, sg->title, (double)off, (double)len);
        return;
    }
}

// hold 1: the level's music stays paused (a new song paused too); 0: back.
static void radio_level_music(int hold)
{
    if (hold) {
        int song = *(const int *)0x912698;
        if (!radio_music_pauses || song != radio_seen_song) {
            ((void (*)(void))0x40D060)();      // PauseMusic
            radio_music_pauses++;
            radio_seen_song = song;
        }
    } else {
        while (radio_music_pauses > 0) {
            ((void (*)(void))0x40D0A0)();      // ResumeMusic
            radio_music_pauses--;
        }
    }
}

static void radio_stop(void)
{
    if (radio_h) { bs_free(radio_h); radio_h = 0; }
    radio_paused = 0;
    radio_level_music(0);
}

// From on_frame's input: D-pad left (-1) / right (+1) while driving.
static void radio_pick(int pick)
{
    radio_init();
    if (radio_state != 1) return;
    radio_on = (radio_on + pick + RADIO_N + 1) % (RADIO_N + 1);
    if (radio_h) { bs_free(radio_h); radio_h = 0; }
    radio_dead = 0;
    radio_now_title = 0;
    radio_banner = 150;
    PrintDebug("[SADX Truck] radio: %s\n", radio_on ? radio_st[radio_on - 1].name : "OFF");
}

// The banner's two lines, for draw_gauges; 0 when none.
static int radio_text(const char **name, const char **title)
{
    if (radio_banner <= 0) return 0;
    *name = radio_on ? radio_st[radio_on - 1].name : "RADIO OFF";
    *title = radio_now_title ? radio_now_title : "";
    return 1;
}

// OnFrame.
static void radio_frame(void)
{
    int playing = game_mode == 15;
    if (playing) radio_clock += 1.0f / 60.0f;
    if (radio_banner > 0 && playing) radio_banner--;
    if (radio_state != 1) return;
    if (!radio_on || !frame || !truck) { radio_stop(); return; }
    if (!playing) {                            // pause menu: the radio waits
        if (radio_h && !radio_paused) { bs_pause(radio_h); radio_paused = 1; }
        return;
    }
    if (!radio_h) { if (!radio_dead) radio_tune(); }
    else if (radio_paused) { bs_play(radio_h, 0); radio_paused = 0; }
    else if (bs_active(radio_h) == 0) {        // the song ended: the next one
        struct radio_station *st = &radio_st[radio_on - 1];
        bs_free(radio_h); radio_h = 0;
        st->at = (st->at + 1) % st->n;
        st->began = radio_clock;
        radio_tune();
        radio_banner = 150;
    }
    if (!radio_h) return;
    // On foot: how loud the truck's radio is where Sonic stands.
    float k = 1.0f;
    if (!riding) {
        k = 0.0f;
        if (player_task) {
            const char *body = *(const char *const *)((const char *)truck + 4);
            const float *tp = (const float *)(body + 0x28), *pp = (const float *)(player_task + 32);
            float dx = pp[0] - (origin.x + tp[0] * UNITS_PER_METER);
            float dy = pp[1] - (origin.y + tp[1] * UNITS_PER_METER);
            float dz = pp[2] - (origin.z + tp[2] * UNITS_PER_METER);
            float ft = sqrt_f(dx * dx + dy * dy + dz * dz) / UNITS_PER_METER * 3.281f;
            k = ft <= 15.0f ? 1.0f : ft >= 100.0f ? 0.0f : (100.0f - ft) / 85.0f;
        }
    }
    bs_attr(radio_h, 2, k * (float)(*(const int *)0x909F28 + 10000) / 30000.0f);
    radio_level_music(k > 0.0f);
}

static void people_patch(void)
{
#ifndef TEST_EXE
    static const unsigned char want[8] = { 0x8B, 0x4C, 0x24, 0x04, 0x83, 0xEC, 0x0C, 0x56 };
    unsigned char *p = (unsigned char *)0x5430F0;
    u32 old;
    for (int i = 0; i < 8; i++)
        if (p[i] != want[i]) {
            PrintDebug("[SADX Truck] Past echidna callback at 0x5430F0: unexpected bytes, not patched\n");
            return;
        }
    unsigned char *t = VirtualAlloc(0, 16, 0x3000, 0x40);   // commit + reserve, read/write/run
    if (!t) return;
    for (int i = 0; i < 8; i++) t[i] = p[i];
    u32 back = 0x5430F8u - ((u32)t + 13);
    t[8] = 0xE9; t[9] = back & 255; t[10] = (back >> 8) & 255; t[11] = (back >> 16) & 255; t[12] = back >> 24;
    people_vanilla = t;
    u32 rel = (u32)people_near - (0x5430F0u + 5);
    VirtualProtect(p, 5, 0x40, &old);
    p[0] = 0xE9; p[1] = rel & 255; p[2] = (rel >> 8) & 255; p[3] = (rel >> 16) & 255; p[4] = rel >> 24;
    VirtualProtect(p, 5, old, &old);
    PrintDebug("[SADX Truck] Past echidna callback patched\n");
#endif
}
// Twinkle Park's cart pads, "O DASH PANEL 4 CART" and "O JUMP PANEL 4
// CART" (object names from sonic.exe's object list).  When something hits
// one, its hit code (dash 0x79D370, jump 0x79D6F0) works out the push, in
// SADX units per frame along the pad, and calls 0x79AF90 (task that hit,
// push): Sonic in a cart -> the cart takes the push; Sonic on foot ->
// SetVelocityP.  The mod sends those two calls (at 0x79D413 and 0x79D7A3)
// to cart_pad_hit.  Riding the truck: the truck takes the push instead
// (SetLinearSpeed, m/s: units per frame x 60 / 10); its speed along the
// pad is raised to the pad's, never lowered, and sideways speed is kept.
// Anything else: the original call, unchanged.
typedef void (__cdecl *pad_fn)(char *, float *);
static void __cdecl cart_pad_hit(char *hitter, float *push)
{
#ifndef TEST_EXE
    if (riding && truck && hitter && *(char **)(hitter + 32) == player_task) {
        const float *v = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x34);
        float p[3] = { push[0] * 6.0f, push[1] * 6.0f, push[2] * 6.0f };
        float pl = sqrt_f(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (pl < 0.01f) return;
        float u[3] = { p[0] / pl, p[1] / pl, p[2] / pl };
        float along = v[0] * u[0] + v[1] * u[1] + v[2] * u[2];
        if (along >= pl) return;
        float add = pl - along;
        static int quiet;
        if (quiet > 0) quiet--;
        if (!quiet) {
            PrintDebug("[SADX Truck] cart pad: truck pushed to %.0f mph along the pad (was %.0f mph)\n",
                       (double)(pl * 2.237f), (double)(along * 2.237f));
            quiet = 30;
        }
        SetLinearSpeed(truck, v[0] + add * u[0], v[1] + add * u[1], v[2] + add * u[2]);
        ActivateObject(truck);
        return;
    }
#endif
    ((pad_fn)0x79AF90)(hitter, push);
}
static void cart_pad_patch(void)
{
#ifndef TEST_EXE
    static const u32 at[2] = { 0x79D413, 0x79D7A3 };
    for (int k = 0; k < 2; k++) {
        unsigned char *p = (unsigned char *)at[k];
        u32 old, want = 0x79AF90u - (at[k] + 5), rel = (u32)cart_pad_hit - (at[k] + 5);
        if (p[0] != 232 || *(const u32 *)(p + 1) != want) {
            PrintDebug("[SADX Truck] cart pad call at 0x%06X: unexpected bytes, not patched\n", at[k]);
            continue;
        }
        VirtualProtect(p, 5, 0x40, &old);
        *(u32 *)(p + 1) = rel;
        VirtualProtect(p, 5, old, &old);
    }
    PrintDebug("[SADX Truck] cart pad calls patched\n");
#endif
}
// The Mod Loader calls OnExit when the game closes (SADXModLoader.dll
// lists it with OnFrame).  Not on a crash: the last get-out's save stands.
__declspec(dllexport) void __cdecl OnExit(void)
{
    PrintDebug("[SADX Truck] game closing\n");
    if (truck) { dent_save(); damage_file_save(truck_now); }
}
__declspec(dllexport) void __cdecl OnFrame(void)
{
    if (!trucks) return;                // no trucks.bin: the mod is off
    if (truck && player_task && frame) solid_frame();   // only once the truck is in a level
    solid_place(0);
    on_frame();
    cart_cam_feed();
    free_cam_release();
    people_frame();
    radio_frame();
    solid_place(truck && frame && !riding);
}

int __stdcall DllMainCRTStartup(void *module, u32 reason, void *reserved)
{
    (void)module; (void)reason; (void)reserved;
    return 1;
}
