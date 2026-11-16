// Made by skin_to_c.py.  Do not edit; run skin_to_c.py again instead.
// Only the layout of trucks.bin (the Cabela's data itself is in that
// file, made on the player's machine from their own Cabela's 3).
// Model units: SADX units (10 per meter), truck space (x right, y up,
// z forward).  Physics units: meters, as Cabela's.

#define TRUCK_BIN_VERSION 1
#define TRUCKS 14
#define TRUCK_MODELS 725
#define TRUCK_DRAWS 809
#define TRUCK_TEXTURES 90
#define TRUCK_TEX_PICTURE 16
#define TRUCK_TEX_HUD 30          // panel, schematic, then parts 0-17
#define TRUCK_TEX_GLOW_WHITE 51
#define TRUCK_TEX_GLOW_RED 52
#define TRUCK_TEX_GLASS 53
#define TRUCK_TEX_GLASS_DMG 54       // + truck number - 1
#define TRUCK_TEX_SHADOW 68
#define TRUCK_TEX_GAUGE 69        // tach, speedo, needle, 3 bars, fill, 3 icons, digits 0-9
#define TRUCK_TEX_FONT 89
#define GLASS_WHOLE 16
#define GLASS_CRACKED 24
#define TRUCK_GBIX0 790001u

// One piece of a truck (points, normals and uv point into trucks.bin).
// Ninja texture coordinates: 255 = 1.0, one pair per triangle corner.
static struct truck_model {
    float (*points)[3], (*normals)[3];
    short *tris, (*uv)[2];
    int npoints, ntris, tex, lamp;
    float center[3], radius;
} truck_models[TRUCK_MODELS];

// FontLarge glyphs, characters 32 to 126: u1, v1, u2, v2 in the
// texture's 0-255 units (2 pixels each), then the width in the same
// units (0 = no glyph; the space is 8 wide).
static const unsigned char (*font_glyph)[5];

// What to draw for each truck: model number, frame (0 body, 1 to 4 wheels),
// damage part it belongs to (255 none; doors, bumpers fall off at black),
// lamp kind (0 none, 1 headlight, 2 tail, 3 brake, 4 reverse).
static const unsigned short (*truck_draws)[4];

struct truck_data {
    char name[16];
    int first_draw, draws;
    int snork_first[2], snork_draws[2];   // Snork1, Snork2 (count 0 = none)
    tVect3 spring[4];          // posArc_0..3 (BONEDUMP)
    tVect3 tire[4];            // wheel_0..3 (BONEDUMP)
    float spring_to_tire;      // posArc_0.y - wheel_0.y
    float dummy[3][6];         // colDummy_0..2: scale x y z, position x y z
    float axle[2];             // truck byte 260
    float pivot[2];            // truck byte 256
    float mass, adherence, steering;
    int axle_state[2];         // SetPhysDrivingTrain 1st, 2nd (0 = independent)
    float axle_params[4];      // front pitch scale, front start pitch, rear, rear
    float gears[6];            // SetPhysGearRatiosAutomatic, reverse first
    float init_len, min_len, max_len, ksusp, kdump, rolling;
    float max_rpm, min_rpm, red_rpm;
    float seat[3], door[3];    // model bones driver1, door_0 (meters)
    float parts[26];           // SetCarPartsPoints (15 = engine)
};
#define TRUCK_REC 452
static const struct truck_data *trucks;

// Engine sounds (files in the mod folder's sounds\).  Curves: x is the
// sound key (PARAM1, from the RPM) or PARAM2; straight lines between the
// points, held at the end values outside them.
#define TRUCK_WAVS 88
static const char *truck_wavs[TRUCK_WAVS];
static float truck_wav_seconds[TRUCK_WAVS];
#define WAV_WATER 82
#define WAV_SPLASH 83
#define WAV_DAMAGED 84
#define WAV_BAD 85
#define WAV_GRIND 86
#define WAV_STEAM 87
#define WAV_CRANK_IN 77
#define WAV_CRANK_LOOP 78
#define WAV_CRANK_OUT 79
#define WAV_CLICK 80
#define WAV_FALSE_START 81
struct snd_curve { int param, n; float x[4], y[4]; };
struct snd_sample { int wav, ngain; struct snd_curve gain[2], pitch; };
struct truck_sound {
    int nsamples; struct snd_sample s[5]; int start_wav, stop_wav;
};
#define SOUND_REC 652
static const struct truck_sound *truck_sounds;
