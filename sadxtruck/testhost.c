// testhost: runs sadxtruck.c outside SADX, under Wine, so a change can be
// checked without starting the game.  Calls Init with the current folder
// (put it next to EngineDll.dll, like truckprobe/run) and then OnFrame 660
// times (11 seconds at 60 frames per second) with a scripted pad.
// PrintDebug goes to the console.
//
// Stand-ins for SADX: Sonic stands at (100, 50, 200) on flat ground at
// Y 50 (SADX units).

typedef void *HANDLE;
__declspec(dllimport) HANDLE __stdcall GetStdHandle(unsigned int which);
__declspec(dllimport) int __stdcall WriteFile(HANDLE h, const void *buf, unsigned int n, unsigned int *done, void *ov);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int code);
__declspec(dllimport) int __cdecl vsprintf(char *buf, const char *fmt, __builtin_va_list args);

static void __cdecl PrintDebug(const char *fmt, ...)
{
    static char line[512];
    unsigned int n, done;
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    n = (unsigned int)vsprintf(line, fmt, args);
    __builtin_va_end(args);
    WriteFile(GetStdHandle((unsigned int)-11), line, n, &done, 0);
}

static float __cdecl GetShadowPos(float x, float y, float z, int *ang)
{
    (void)x; (void)z;
    ang[0] = ang[1] = ang[2] = 0;
    return y >= 50.0f ? 50.0f : -1000000.0f;
}

// taskwk: position at byte 32.
static float fake_sonic[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 100.0f, 50.0f, 200.0f };
static char *player_task = (char *)fake_sonic;
static float fake_motion[16], fake_work[32];
static char *player_motion = (char *)fake_motion;
static char *player_work = (char *)fake_work;
// Player 2 (Tails): none, or with -DTH_TAILS a stand-in at (110, 50, 200).
#ifdef TH_TAILS
static float fake_tails[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 110.0f, 50.0f, 200.0f };
static float fake_tails_motion[16], fake_tails_work[32];
static char *player2_task = (char *)fake_tails, *player2_motion = (char *)fake_tails_motion, *player2_work = (char *)fake_tails_work;
#else
static char *player2_task, *player2_motion, *player2_work;
#endif

// Pad: HeldButtons at byte 8, triggers at 24 and 26, stick X at 28.
static unsigned int fake_pad[13];
static char *pad_raw = (char *)fake_pad;
static unsigned int fake_pad_game[13];
#define pad_game ((char *)fake_pad_game)
static void __cdecl PadReadOn(void) { PrintDebug("[testhost] PadReadOn\n"); }
static void __cdecl PadReadOff(void) { PrintDebug("[testhost] PadReadOff\n"); }
static void __cdecl SetInputP(unsigned char pno, signed char op) { PrintDebug("[testhost] SetInputP %d %d\n", pno, op); }
static void set_pad(unsigned int held, int gas, int brake, int stick)
{
    char *p = (char *)fake_pad;
    *(unsigned int *)(p + 8) = held;
    *(unsigned short *)(p + 24) = (unsigned short)gas;
    *(unsigned short *)(p + 26) = (unsigned short)brake;
    *(short *)(p + 28) = (short)stick;
}

// Drawing: count the rings drawn.
static int drawn;
static char fake_task[52];
static char *__cdecl CreateElementalTask(unsigned short im, int level, void (__cdecl *exec)(char *))
{
    (void)im; (void)level; (void)exec;
    return fake_task;
}
static int __cdecl njSetTexture(void *t) { (void)t; return 0; }
#define OBJ_REGULAR_TEXLIST ((void *)0)
#define ring_model ((void *)0)
static void __cdecl dsDrawObject(void *o) { (void)o; drawn++; }
static void __cdecl njPushMatrixEx(void) {}
static void __cdecl njPopMatrixEx(void) {}
static float last_ring[3];
static void __fastcall njTranslate_f(void *m, void *edx, float x, float y, float z)
{
    (void)m; (void)edx; last_ring[0] = x; last_ring[1] = y; last_ring[2] = z;
}
#define njTranslate(m, x, y, z) njTranslate_f(m, 0, x, y, z)
static void __fastcall njRotateY(void *m, int a) { (void)m; (void)a; }
// Truck model: count the parts drawn; LoadPVM marks texture 0 loaded.
static int parts_drawn;
static void __cdecl njDrawModel_Dynamic(void *mdl) { (void)mdl; parts_drawn++; }
static int glass_drawn;
static int solid_adds, solid_entries;
static void __cdecl DynamicCOL_Add(unsigned f, char *t, void *o)
{
    // Like sonic.exe 0x43B2F6: read the model's centre (crashes on no model).
    (void)f; (void)t;
    const char *mdl = *(const char *const *)((const char *)o + 4);
    volatile float c = *(const float *)(mdl + 0x18);
    (void)c;
    solid_adds++;
}
static void __cdecl DynamicCOL_Remove(char *t, void *o) { (void)t; (void)o; }
static void __cdecl CCL_Init(char *t, void *i, int n, unsigned char id) { (void)t; (void)i; (void)n; (void)id; }
static void __cdecl EntryColliList(char *t) { (void)t; solid_entries++; }
static char *__cdecl CCL_IsHitPlayer(char *t) { (void)t; return 0; }
static void __cdecl late_DrawModel(void *mdl, int f) { (void)mdl; (void)f; parts_drawn++; glass_drawn++; }
// LoadPVM fills each texture's slot (first field: its global number).
// TH_FREE_AT: frame at which the stand-in "frees" the slots, as SADX does
// on a level change, to test the reload.
static unsigned int th_slots[51][8];      // 51 = TRUCK_TEXTURES
#ifndef TH_LEVEL
#define TH_LEVEL 1
#endif
static short level_now = TH_LEVEL, act_now = 0;
static short game_mode = 15;                  // playing
static void __cdecl LoadPVM(const char *name, void *texlist)
{
    PrintDebug("[testhost] LoadPVM %s\n", name);
    unsigned int *tex = *(unsigned int **)texlist;
    for (int i = 0; i < 51; i++) {
        th_slots[i][0] = 790001u + (unsigned int)i;
        tex[i * 3 + 2] = (unsigned int)th_slots[i];
    }
}
static int pictures_drawn;
static void __cdecl njDrawSprite2D_ForcePriority(void *sp, int n, float pri, unsigned int attr)
{
    (void)sp; (void)n; (void)pri; (void)attr; pictures_drawn++;
}
static void __cdecl SetMaterialAndSpriteColor_Float(float a, float r, float g, float b) { (void)a; (void)r; (void)g; (void)b; }
static void __cdecl njColorBlendingMode(int target, int mode) { (void)target; (void)mode; }
static void __cdecl ResetMaterial(void) {}
static char *camera_twp;                       // -DTH_LAMPS sets a camera
static void __cdecl njDrawSprite3D_Late(void *sp, int n, unsigned int attr) { (void)sp; (void)n; (void)attr; }
static int screen_w = 1920, screen_h = 1080;
static float stretch_2d = 2.25f;
static float th_matrix[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
static float *nj_current_matrix = th_matrix;

// Level: one solid piece, a floor quad at Y 50 (2000 units, 200 m, on a
// side) and, with -DTH_WALL, a wall across the road at Z 400 (20 m, about
// 66 feet, ahead of Sonic, who faces +z), from Y 30 to 80.
static float th_points[8][3] = {
    { -1000, 50, -1000 }, { 1000, 50, -1000 }, { -1000, 50, 1000 }, { 1000, 50, 1000 },
#ifdef TH_EXITWALL
    // -DTH_EXITWALL: the wall starts far below the floor; frame 638 moves
    // it right beside the driver's door.
    { -1000, -900, 400 }, { 1000, -900, 400 }, { -1000, -850, 400 }, { 1000, -850, 400 },
#else
    { -1000, 30, 400 }, { 1000, 30, 400 }, { -1000, 80, 400 }, { 1000, 80, 400 },
#endif
};
static short th_meshes[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#if defined(TH_WALL) || defined(TH_EXITWALL)
#define TH_QUADS 2
#else
#define TH_QUADS 1
#endif
static struct { unsigned short type, n; short *meshes; void *attrs, *normals, *colors, *uv, *buffer; }
    th_set = { 1 << 14, TH_QUADS, th_meshes };
static struct { float *points; float *normals; int np; void *sets; void *mats; unsigned short ns, nm; float c[3], r; void *buffer; }
    th_model = { &th_points[0][0], 0, 8, &th_set, 0, 1, 0, { 0, 50, 0 }, 2000 };
static struct { unsigned int ev; void *model; float pos[3]; int ang[3]; float scl[3]; void *child, *sibling; }
    th_obj = { 0, &th_model, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 }, 0, 0 };
// -DTH_WATER=y: a water quad (flag 1 << 1) at height y over the whole floor.
#ifndef TH_WATER
#define TH_WATER -1000
#endif
static float th_wpoints[4][3] = {
    { -1000, TH_WATER, -1000 }, { 1000, TH_WATER, -1000 }, { -1000, TH_WATER, 1000 }, { 1000, TH_WATER, 1000 },
};
static short th_wmeshes[4] = { 0, 1, 2, 3 };
static struct { unsigned short type, n; short *meshes; void *attrs, *normals, *colors, *uv, *buffer; }
    th_wset = { 1 << 14, 1, th_wmeshes };
static struct { float *points; float *normals; int np; void *sets; void *mats; unsigned short ns, nm; float c[3], r; void *buffer; }
    th_wmodel = { &th_wpoints[0][0], 0, 4, &th_wset, 0, 1, 0, { 0, TH_WATER, 0 }, 2000 };
static struct { unsigned int ev; void *model; float pos[3]; int ang[3]; float scl[3]; void *child, *sibling; }
    th_wobj = { 0, &th_wmodel, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 }, 0, 0 };
static struct { float c[3], r, w2, w3; void *obj; int block; unsigned int attr; }
    th_pieces[2] = { { { 0, 50, 0 }, 2000, 0, 0, &th_obj, 0, 1 },
                     { { 0, TH_WATER, 0 }, 2000, 0, 0, &th_wobj, 0, 1 << 1 } };
static struct { short n, nmot, attr, load; float far; void *pieces; }
    th_land = { 2, 0, 0, 0, 0, th_pieces };
static char *land_table = (char *)&th_land;

#define TEST_EXE
#include "sadxtruck.c"

// Script knobs: gas from frame 60 to TH_GAS_END, stick X from 360 to 480.
#ifndef TH_GAS_END
#define TH_GAS_END 480
#endif
#ifndef TH_STICK
#define TH_STICK 127
#endif
// TH_PICK: D-pad right presses before Y (0 = the Stalker, 1 = Longhorn,
// 2 = Longrun, 3 = Predator, and on round the list).
#ifndef TH_XHOLD
#define TH_XHOLD 1
#endif
#ifndef TH_PICK
#define TH_PICK 0
#endif

void __cdecl mainCRTStartup(void)
{
    Init(".", 0);
    if (!trucks) {                  // no trucks.bin: only the game's hooks, as SADX would call them
        for (int i = 0; i < 120; i++) { set_pad(i == 60 ? (1 << 9) : 0, 0, 0, 0); OnControl(); OnFrame(); }
        OnExit();
        PrintDebug("[testhost] mod off (no trucks.bin): 120 frames of OnControl and OnFrame, no crash\n");
        ExitProcess(0);
    }
#ifdef TH_SNORK
    snorkel[11] = TH_SNORK;                             // Stalker snorkel
#endif
#ifdef TH_DMG
    parts_init(11); part_pts[11][15] = TH_DMG;           // Stalker engine points left
#endif
    *(int *)&fake_sonic[6] = 16384;                 // Sonic faces a quarter turn
    // 1 s nothing, Y, 5 s full gas, 2 s gas + stick right, 2 s brake, Y.
    // -DTH_REVFWD: brake 480-640 (stop, reverse, back up), gas 640-800
    // (brake, stop, forward, drive), Y at 980.
#ifdef TH_REVFWD
#define TH_FRAMES 1000
#define TH_OUT 980
#elif defined(TH_TOP)
// -DTH_TOP: full gas, straight, for 60 s (top speed); no Y out.
#define TH_FRAMES 3660
#define TH_OUT 99999
#elif defined(TH_PARK)
// -DTH_PARK: Y out at 400 while still driving fast (it coasts), parked
// until 1100, Y back in at 1100, full gas from 1110.
#define TH_FRAMES 1400
#define TH_OUT 400
#else
#define TH_FRAMES 660
#define TH_OUT 640
#endif
    for (int i = 0; i < TH_FRAMES; i++) {
        unsigned int y = (i == 60 || i == TH_OUT) ? (1 << 9) : 0;
#ifdef TH_BACKIN
        if (i == TH_OUT + 10) y = 1 << 9;             // Y again beside the door: back in
#endif
        if (i < 60)       set_pad(y | ((i < 2 * TH_PICK && i % 2 == 0) ? BUTTON_RIGHT : 0), 0, 0, 0);
        // X (1 << 10) at frame 62 starts the engine; gas from 66.
        // TH_XHOLD: frames X is held from frame 62 (default 1, a tap).
        else if (i < 480) set_pad(y | (i >= 62 && i < 62 + TH_XHOLD ? (1 << 10) : 0), (i >= 62 + TH_XHOLD + 4 && i < TH_GAS_END) ? 255 : 0, 0, i >= 360 ? TH_STICK : 0);
#ifdef TH_TOP
        else if (1) set_pad(y, 255, 0, 0);
#endif
#ifdef TH_REVFWD
        else if (i >= 640 && i < 800) set_pad(y, 255, 0, 0);
        else if (i >= 800) set_pad(y, 0, 0, 0);
#endif
        else              set_pad(y, 0, 255, 0);
#ifdef TH_WATER
        if (i == 400) PrintDebug("[testhost] solid_truck %d truck_now %d solid lo y %.2f hi y %.2f  water_rel %.2f\n", solid_truck, truck_now, (double)solid_lo[1], (double)solid_hi[1], (double)water_rel);
#endif
#ifdef TH_H78
        if (i == 70) {                          // EngineDll water check half height (truck + 0x78)?
            const char *b = *(const char *const *)((const char *)truck + 4);
            PrintDebug("[testhost] truck+0x78 %.3f  body y %.3f  truck+0x14 %08X\n",
                       (double)*(const float *)((const char *)truck + 0x78), (double)((const float *)(b + 0x28))[1],
                       *(const unsigned *)((const char *)truck + 0x14));
            // cPhysManager::TestColisions (0x100D4260): list at manager+0x70
            // (+4 -> +4 indices, +8 count), entries at manager+4.
            const char *m = (const char *)manager;
            const char *lst = *(const char *const *)(*(const char *const *)(m + 0x70) + 4);
            int cnt = *(const int *)(lst + 8);
            const int *idx = *(const int *const *)(lst + 4);
            for (int k = 0; k < cnt && k < 20; k++) {
                const char *e = (*(const char *const *const *)(m + 4))[idx[k]];
                PrintDebug("[testhost] entry at %08X\n", (unsigned)e); PrintDebug("[testhost] entry %d: obj %08X (truck %08X body %08X) +0x14 %08X type %d +0x78 %.3f\n", k,
                           *(const unsigned *)(e + 4), (unsigned)truck, (unsigned)b, *(const unsigned *)(e + 0x14),
                           *(const int *)(e + 0x18), (double)*(const float *)(e + 0x78));
            }
        }
#endif
        if (i == 300) {                         // level change: slots reused
            for (int k = 0; k < 51; k++) th_slots[k][0] = 12345;
            /* same level and act: the slots must load again */
        }
#ifdef TH_ROLL
        // -DTH_ROLL=1: at frame 200 the truck is turned onto its side
        // (TH_ROLL=2: onto its roof), 1 m up, still; then every 15 frames:
        // wheel and body ray hits and the air timer.
        if (i == 200) {
            const char *b = *(const char *const *)((const char *)truck + 4);
            const float *r = (const float *)(b + 0x4c);
            float m[9];
            for (int j = 0; j < 3; j++) {
                if (TH_ROLL == 1) { m[j] = r[3 + j]; m[3 + j] = -r[j]; }        // right' = up, up' = -right
                else              { m[j] = -r[j];    m[3 + j] = -r[3 + j]; }    // roof: right and up flipped
                m[6 + j] = r[6 + j];
            }
            const float *p = (const float *)(b + 0x28);
            tVect3 to = { p[0], p[1] + 1.0f, p[2] };
            SetOrientation(truck, m);
            SetPosition(truck, &to);
            SetLinearSpeed(truck, 0, 0, 0);
            SetAngularSpeed(truck, 0, 0, 0);
            PrintDebug("[testhost] frame 200: truck turned onto its %s\n", TH_ROLL == 1 ? "side" : "roof");
        }
        if (i > 200 && i % 15 == 0)
            PrintDebug("[testhost] frame %d: wheel rays hit %d of %d, body corners hit %d of %d, air %.2f s, up y %+.2f\n",
                       i, hits_now, rays_now, box_hits_now, box_rays_now, (double)air_t,
                       (double)((const float *)(*(const char *const *)((const char *)truck + 4) + 0x4c))[4]);
#endif
#ifdef TH_SLEEP
        if (i == TH_SLEEP) {                    // EngineDll stops the truck, as at rest
            void (__thiscall *Stop)(void *) = need("?Stop@cPhysicsObject@@UAEXXZ", "Stop");
            Stop(truck);
            PrintDebug("[testhost] frame %d Stop(truck): in active list %d\n", i, IsInActiveList(truck));
        }
#endif
#ifdef TH_PAUSE
        // Start held at frames 300 and 301 while riding; the game pauses
        // (mode 16) from 302 to 399, then plays again.
        if (i == 300 || i == 301) fake_pad[2] |= 1 << 3;
        if (i == 302) game_mode = 16;
        if (i == 400) game_mode = 15;
#endif
#ifdef TH_PARK
        if (i >= TH_OUT) set_pad(i == TH_OUT || i == 1100 ? (1 << 9) : 0, i >= 1110 ? 255 : 0, 0, 0);
        if (i == 1099) {                              // Sonic walks back to the truck
            float c[3]; truck_center(c);
            fake_sonic[8] = origin.x + c[0] * UNITS_PER_METER;
            fake_sonic[9] = origin.y + c[1] * UNITS_PER_METER;
            fake_sonic[10] = origin.z + c[2] * UNITS_PER_METER;
        }
#endif
#ifdef TH_PUSH
        // Frame 500: push the truck 1 foot along x; print the body and
        // wheel 0 before, right after and 30 frames later.
        if (i == 500 || i == 501 || i == 530) {
            const float *bp = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x28);
            m4x4 wt; GetWheelTM(truck, &wt, 0);
            PrintDebug("[testhost] frame %d body %.3f %.3f %.3f  wheel0 %.3f %.3f %.3f\n", i,
                       (double)bp[0], (double)bp[1], (double)bp[2], (double)wt.m[12], (double)wt.m[13], (double)wt.m[14]);
            if (i == 500) push_truck(0.3048f, 0);
        }
#endif
#ifdef TH_HIT
        // Frame 300: Sonic hit in his seat, attacker unknown (no colliwk).
        // Frame 450: hit by an enemy behind the truck's left rear corner.
        if (i == 300) *(unsigned short *)((char *)fake_sonic + 6) = 120;
        if (i == 450) {
            static char fcwp[168], fhc[168], ftask[64], ftw[64];
            float q[3], m[3] = { car.door[0] < 0 ? -1.5f : 1.5f, 1.0f, car.tire[2].z - 1.0f };
            truck_point(m, q);
            ((float *)(ftw + 32))[0] = q[0]; ((float *)(ftw + 32))[1] = q[1]; ((float *)(ftw + 32))[2] = q[2];
            *(char **)(ftask + 32) = ftw; *(char **)(fhc + 156) = ftask; *(char **)(fcwp + 164) = fhc;
            *(char **)((char *)fake_sonic + 56) = fcwp;
            *(unsigned short *)((char *)fake_sonic + 6) = 120;
        }
        if (i != 300 && i != 450) {
            unsigned short *b = (unsigned short *)((char *)fake_sonic + 6);
            if (*b) (*b)--;
        }
#endif
#ifdef TH_RUN
        // Sonic's colliwk with one shape as logged in the game: damage 228,
        // size 4 4 0.6.
        static char rcwp[168]; static unsigned char rinfo[48];
        if (i == 0) {
            rinfo[3] = 228; ((float *)(rinfo + 20))[0] = 4; ((float *)(rinfo + 20))[1] = 4; ((float *)(rinfo + 20))[2] = 0.6f;
            *(unsigned short *)(rcwp + 6) = 1; *(unsigned char **)(rcwp + 12) = rinfo;
            *(char **)((char *)fake_sonic + 56) = rcwp;
        }
        if (i == 659) PrintDebug("[testhost] after the ride: damage %d, size %.1f\n", rinfo[3], (double)((float *)(rinfo + 20))[0]);
        rinfo[3] = (unsigned char)((rinfo[3] & 0xF4) | 4);    // Sonic's update: attack 0, defense 1
        run_task_exec(0);                                     // then the level 6 task
        if (i == 400) PrintDebug("[testhost] frame 400, as the collision pass sees it: damage %d\n", rinfo[3]);
#endif
#ifdef TH_EXITWALL
        if (i == 638) {
            float wx = car.door[0] + (car.door[0] < 0 ? -0.3f : 0.3f);
            float loc[4][3] = { { wx, -1, car.seat[2] - 5 }, { wx, -1, car.seat[2] + 5 }, { wx, 4, car.seat[2] - 5 }, { wx, 4, car.seat[2] + 5 } };
            for (int k = 0; k < 4; k++) truck_point(loc[k], th_points[4 + k]);
            PrintDebug("[testhost] frame 638: wall 1 foot outside the driver's door\n");
        }
#endif
#ifdef TH_LAMPS
        // D-pad up at frame 100: headlights (and tail lights) on.
        if (i == 100) fake_pad[2] |= 1 << 4;
        // Camera: from frame 200, 20 m in front of the truck; from 300, 20 m
        // behind; from 400, off to the side; from 450 none (all full).
        {
            static char fake_cam[64];
            float q[3] = { 0, 1, 0 };
            if (i >= 200 && i < 450) {
                q[0] = i >= 400 ? 20.0f : 0; q[2] = i < 300 ? 20.0f : i < 400 ? -20.0f : 0;
                truck_point(q, (float *)(fake_cam + 32));
                camera_twp = fake_cam;
            } else camera_twp = 0;
            static int last = -1;
            if (i == 250 || i == 350 || i == 425) {
                PrintDebug("[testhost] frame %d camera %s: glows this frame %d\n", i,
                           i == 250 ? "ahead" : i == 350 ? "behind" : "to the side", glows_drawn - last);
            }
            last = glows_drawn;
        }
        if (i == 99 || i == 150 || i == 470 || i == 520 || i == 630) {
            static int before;
            PrintDebug("[testhost] frame %d: glows drawn since the last line %d (headlights %d, braking %d, reversing %d)\n",
                       i, glows_drawn - before, headlights, braking, reversing);
            before = glows_drawn;
        }
#endif
#ifdef TH_BREAK
        if (i == 190 || (i >= 300 && i <= 480 && i % 20 == 0)) {
            m4x4 t; float y[4];
            for (u32 w = 0; w < 4; w++) { GetWheelTM(truck, &t, w); y[w] = t.m[13]; }
            PrintDebug("[testhost] frame %d wheel centers height (m): %.3f %.3f %.3f %.3f\n", i, (double)y[0], (double)y[1], (double)y[2], (double)y[3]);
        }
#endif
#ifdef TH_SAVE
        // Run 1 (-DTH_SAVE): dent the windshield frames 300 to 339, then
        // OnExit saves.  Run 2 (-DTH_SAVE -DTH_SAVE2): no dents; the
        // windshield's point 0 should match run 1's at frame 341.
        if (i == 299 || i == 341) {
            for (int k = 0; k < car.draws; k++) {
                const unsigned short *d = truck_draws[car.first_draw + k];
                if (d[3] == GLASS_WHOLE + 0) PrintDebug("[testhost] frame %d windshield point 0 at %.4f %.4f %.4f, cracked %d, glass %.2f\n", i,
                    (double)truck_models[d[0]].points[0][0], (double)truck_models[d[0]].points[0][1], (double)truck_models[d[0]].points[0][2],
                    glass_cracked[0], (double)glass_total[0]);
            }
        }
#ifndef TH_SAVE2
        if (i >= 300 && i < 340) {
            for (int k = 0; k < car.draws; k++) {
                const unsigned short *d = truck_draws[car.first_draw + k];
                if (d[3] != GLASS_WHOLE + 0) continue;
                const float *c = truck_models[d[0]].center;
                float loc[3] = { c[0] / UNITS_PER_METER, c[1] / UNITS_PER_METER, c[2] / UNITS_PER_METER }, w[3];
                truck_m(loc, w);
                const float *rot = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x4c);
                float back[3] = { -rot[6], -rot[7], -rot[8] };
                dent_hit(w, back, 0.1f, "test, windshield");
                break;
            }
        }
        if (i == 340) damage_part(3, 500.0f, "test");
#endif
#endif
#ifdef TH_DENTS
        // Frames 300 to 339: one 47+ mph hit (force 0.1) a frame on the
        // middle of the windshield, pushing backward.  Frame 400: repair.
        if (i >= 300 && i < 340) {
            for (int k = 0; k < car.draws; k++) {
                const unsigned short *d = truck_draws[car.first_draw + k];
                if (d[3] != GLASS_WHOLE + 0) continue;
                const float *c = truck_models[d[0]].center;
                float loc[3] = { c[0] / UNITS_PER_METER, c[1] / UNITS_PER_METER, c[2] / UNITS_PER_METER }, w[3];
                truck_m(loc, w);
                const float *rot = (const float *)(*(const char *const *)((const char *)truck + 4) + 0x4c);
                float back[3] = { -rot[6], -rot[7], -rot[8] };
                dent_hit(w, back, 0.1f, "test, windshield");
                break;
            }
        }
#ifdef TH_SWITCH
        // Frame 350: switch to the Predator; 360: back to the Stalker.
        if (i == 350 || i == 360) {
            for (int k = 0; k < car.draws; k++) {
                const unsigned short *d = truck_draws[car.first_draw + k];
                if (d[3] == GLASS_WHOLE + 0) PrintDebug("[testhost] frame %d %s windshield point 0 at %.3f %.3f %.3f, cracked %d\n", i, car.name,
                    (double)truck_models[d[0]].points[0][0], (double)truck_models[d[0]].points[0][1], (double)truck_models[d[0]].points[0][2], glass_cracked[0]);
            }
            picked = i == 350 ? 0 : 11;
            PrintDebug("[testhost] spawn returned %d\n", spawn());
            for (int k = 0; k < car.draws; k++) {
                const unsigned short *d = truck_draws[car.first_draw + k];
                if (d[3] == GLASS_WHOLE + 0) PrintDebug("[testhost] now %s windshield point 0 at %.3f %.3f %.3f, cracked %d\n", car.name,
                    (double)truck_models[d[0]].points[0][0], (double)truck_models[d[0]].points[0][1], (double)truck_models[d[0]].points[0][2], glass_cracked[0]);
            }
        }
#endif
        if (i == 341) {
            // Normals: dented windshield point 0, and how many points of
            // undented pieces had their normal changed (should be 0).
            int changed = 0, pieces = 0;
            for (int k = 0; k < dent_n; k++) {
                const struct truck_model *m = &truck_models[dent_model[k].model];
                int dented = 0;
                for (int q = 0; q < m->npoints && !dented; q++)
                    for (int j = 0; j < 3; j++) if (m->points[q][j] != dent_orig[dent_model[k].first + q][j]) dented = 1;
                if (dented) { pieces++; continue; }
                for (int q = 0; q < m->npoints; q++)
                    for (int j = 0; j < 3; j++) if (m->normals[q][j] != dent_onorm[dent_model[k].first + q][j]) { changed++; j = 3; }
            }
            for (int k = 0; k < car.draws; k++) {
                const unsigned short *d = truck_draws[car.first_draw + k];
                if (d[3] == GLASS_WHOLE + 0) PrintDebug("[testhost] dented: windshield normal 0 %.3f %.3f %.3f\n",
                    (double)truck_models[d[0]].normals[0][0], (double)truck_models[d[0]].normals[0][1], (double)truck_models[d[0]].normals[0][2]);
            }
            PrintDebug("[testhost] %d pieces dented; undented pieces with a changed normal: %d points\n", pieces, changed);
        }
        if (i == 400) {
            dent_restore();
            int same = 1;
            for (int k = 0; k < car.draws; k++) {
                const unsigned short *d = truck_draws[car.first_draw + k];
                if (d[3] == GLASS_WHOLE + 0) PrintDebug("[testhost] after repair: windshield point 0 at %.3f %.3f %.3f, normal %.3f %.3f %.3f\n",
                    (double)truck_models[d[0]].points[0][0], (double)truck_models[d[0]].points[0][1], (double)truck_models[d[0]].points[0][2],
                    (double)truck_models[d[0]].normals[0][0], (double)truck_models[d[0]].normals[0][1], (double)truck_models[d[0]].normals[0][2]);
            }
            (void)same;
            PrintDebug("[testhost] after repair: windshield cracked %d, whole copy shown %d\n", glass_cracked[0], 1);
        }
        if (i == 299) {
            for (int k = 0; k < car.draws; k++) {
                const unsigned short *d = truck_draws[car.first_draw + k];
                if (d[3] == GLASS_WHOLE + 0) PrintDebug("[testhost] before: windshield point 0 at %.3f %.3f %.3f, normal %.3f %.3f %.3f\n",
                    (double)truck_models[d[0]].points[0][0], (double)truck_models[d[0]].points[0][1], (double)truck_models[d[0]].points[0][2],
                    (double)truck_models[d[0]].normals[0][0], (double)truck_models[d[0]].normals[0][1], (double)truck_models[d[0]].normals[0][2]);
            }
        }
#endif
#ifdef TH_SCENE
        // A SADX scene walks Sonic from frame 300 (eventwk move mode 2),
        // he "arrives" at 645 (mode 1); Y at 640 and again at 650.
        static char fake_ewp[64];
        if (i == 300) { fake_ewp[36] = 2; *(char **)&fake_sonic[15] = fake_ewp; }
        if (i == 645) fake_ewp[36] = 1;
        if (i == 650) set_pad(1 << 9, 0, 0, 0);
#endif
#ifdef TH_TAKEOUT
        if (i == 300) *(signed char *)fake_sonic = 55;    // SADX puts Sonic on a snowboard
#endif
#ifdef TH_BREAK
        if (i == 200) { parts_init(11); part_pts[11][TH_BREAK] = 1; }   // part turns black
        if (i == 400 && falling_n)
            PrintDebug("[testhost] frame 400: %d pieces fallen, first at %.1f %.1f %.1f, resting %d\n", falling_n,
                       (double)falling[0].m[12], (double)falling[0].m[13], (double)falling[0].m[14], falling[0].rest);
        if (i == 220 && falling_n)
            PrintDebug("[testhost] frame 220: first piece at %.1f %.1f %.1f\n",
                       (double)falling[0].m[12], (double)falling[0].m[13], (double)falling[0].m[14]);
#endif
        OnControl();
        OnFrame();
#ifdef TH_PARK
        if (i % 50 == 0 && i >= 350)
            PrintDebug("[testhost] frame %d speed %.1f mph%s\n", i, (double)(forward_speed() * 2.23694f), riding ? " (riding)" : "");
#endif
#ifdef TH_PAUSE
        if (i == 302 || i == 399 || i == 401) {
            m4x4 t; GetWheelTM(truck, &t, 0);
            PrintDebug("[testhost] frame %d wheel 0 at %.3f %.3f %.3f\n", i, (double)t.m[12], (double)t.m[13], (double)t.m[14]);
        }
        if (i >= 299 && i <= 302 || i == 400 || i == 401)
            PrintDebug("[testhost] frame %d game pad: on %u press %u release %u, trigger %u\n", i,
                       fake_pad_game[2], fake_pad_game[4], fake_pad_game[5], fake_pad_game[6]);
#endif
        if (i == 70)
            for (u32 w = 0; w < 4; w++) {
                m4x4 t; GetWheelTM(truck, &t, w);
                PrintDebug("[testhost] wheel %u rows %+.2f %+.2f %+.2f | %+.2f %+.2f %+.2f | %+.2f %+.2f %+.2f  pos %+.2f %+.2f %+.2f\n", w,
                    (double)t.m[0], (double)t.m[1], (double)t.m[2], (double)t.m[4], (double)t.m[5], (double)t.m[6],
                    (double)t.m[8], (double)t.m[9], (double)t.m[10], (double)t.m[12], (double)t.m[13], (double)t.m[14]);
            }
#ifdef TH_TRACE
        if (i >= 270 && i < 420 && i % 6 == 0)
            PrintDebug("[testhost] f %d eng %d rev %d cut %.2f fwd %+.2f gear %d rpm %.0f\n", i, eng, reversing, (double)sputter_cut, (double)forward_speed(), GetCurGear(truck), (double)GetRPM(truck));
#endif
        if ((i % 15 == 0 && i >= 540 && i < TH_OUT) || i == 300) {
            float c[3]; truck_center(c);
            PrintDebug("[testhost] frame %d forward speed %+.2f m/s  gear %d  RPM %.0f  center z %.2f\n", i, (double)forward_speed(), GetCurGear(truck), (double)GetRPM(truck), (double)c[2]);
        }
        if (i == 100 || i == 101 || i == 639 || i == 640)
            PrintDebug("[testhost] frame %d Sonic at %.1f %.1f %.1f  ang x %d y %d z %d\n", i, (double)fake_sonic[8], (double)fake_sonic[9], (double)fake_sonic[10], *(int *)&fake_sonic[5], *(int *)&fake_sonic[6], *(int *)&fake_sonic[7]);
        void (__cdecl *disp)(char *) = *(void (__cdecl **)(char *))(fake_task + 20);
        if (disp) disp(fake_task);
    }
    PrintDebug("[testhost] truck pieces drawn %d (%d per frame while a truck exists), pictures drawn %d, windows queued %d\n",
               parts_drawn, trucks[truck_now].draws, pictures_drawn, glass_drawn);
    (void)drawn; (void)last_ring;
    PrintDebug("[testhost] Sonic seat %.1f %.1f %.1f  angle %d\n", (double)fake_sonic[8],
               (double)fake_sonic[9], (double)fake_sonic[10], *(int *)&fake_sonic[6]);
#ifdef TH_TAILS
    PrintDebug("[testhost] Tails at %.1f %.1f %.1f (Sonic %.1f %.1f %.1f)\n", (double)fake_tails[8], (double)fake_tails[9], (double)fake_tails[10],
               (double)fake_sonic[8], (double)fake_sonic[9], (double)fake_sonic[10]);
#endif
    OnExit();
    PrintDebug("[testhost] 660 frames, no crash\n");
    ExitProcess(0);
}
