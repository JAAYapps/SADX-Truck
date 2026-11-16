// trucks.bin from the player's Cabela's files: the C copy of
// skin_to_c.py's data half (Arc, read_meshes, model_bones, read_bonedump,
// lua_blocks, truck_names, physics, truck_parts, snorkel_parts, mirror,
// engine_sounds, font_glyphs, write_bin).  Included by setup.c.
//
// It has to come out byte for byte the same as skin_to_c.py's, so every
// sum is done in 64-bit floats in the same order as the Python, rounded
// to 32 bits only when written, and round() rounds halves to even as
// Python's does.  Build with -msse2 -mfpmath=sse (plain 64-bit floats, not
// the x87's 80-bit ones).

#define TRUCKS 14
#define UNITS 10.0
#define TEX_TIRE 14
#define TEX_SHOCK 15
#define TEX_COLORS 50
#define TEX_GLASS 53
#define TEX_GLASS_DMG 54
#define GLASS_WHOLE 16
#define GLASS_CRACKED 24
#define TRUCK_REC 452
#define SOUND_REC 652

__declspec(dllimport) void *realloc(void *, u32);

static int inexact;                     // numbers the simple text reader can't do exactly

static void *xalloc(u32 n)
{
    void *p = malloc(n ? n : 1);
    if (!p) fail("out of memory.");
    return p;
}

// ---------------------------------------------------------------- numbers

// Python float(text): digits as a whole number, then one multiply or
// divide by a power of ten (exact up to 10^22), so one rounding, the
// same as Python's.  Anything outside that counts in `inexact`.
static double num(const char *p, const char **end)
{
    static const double p10[23] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    int neg = 0, any = 0, e = 0, digits = 0;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    long long m = 0;
    for (; *p >= '0' && *p <= '9'; p++, any = 1) {
        if (digits < 17) { m = m * 10 + (*p - '0'); if (m) digits++; }
        else { e++; inexact++; }
    }
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++, any = 1) {
            if (digits < 17) { m = m * 10 + (*p - '0'); if (m) digits++; e--; }
            else if (*p != '0') inexact++;
        }
    if (any && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        int en = 0, x = 0;
        if (*q == '-' || *q == '+') en = *q++ == '-';
        if (*q >= '0' && *q <= '9') {
            for (; *q >= '0' && *q <= '9'; q++) if (x < 1000) x = x * 10 + (*q - '0');
            e += en ? -x : x;
            p = q;
        }
    }
    if (end) *end = p;
    if (!any) { inexact++; return 0; }
    if (m >= (1LL << 53) || e > 22 || e < -22) inexact++;
    double d = (double)m;
    while (e > 22) { d *= 1e22; e -= 22; }
    while (e < -22) { d /= 1e22; e += 22; }
    d = e < 0 ? d / p10[-e] : d * p10[e];
    return neg ? -d : d;
}

// Python round(): halves go to the even neighbour.
static int rnd(double x)
{
    int i = (int)x;                     // toward zero
    double fr = x - i;
    if (x >= 0) { if (fr > 0.5 || (fr == 0.5 && (i & 1))) i++; }
    else if (fr < -0.5 || (fr == -0.5 && (i & 1))) i--;
    return i;
}

static double sq(double d) { return d * d; }
static double root(double d) { return __builtin_sqrt(d); }

static float rdf(const u8 *p) { float f; memcpy(&f, p, 4); return f; }

static const char *find(const char *s, const char *end, const char *what)
{
    u32 n = strlen(what);
    for (; s + n <= end; s++) if (!memcmp(s, what, n)) return s;
    return 0;
}

static const u8 *findb(const u8 *s, u32 len, const char *what, u32 wn)
{
    for (u32 i = 0; i + wn <= len; i++) if (!memcmp(s + i, what, wn)) return s + i;
    return 0;
}

// ---------------------------------------------------------------- Cora3.arc
// Footer "ARC\0" (version, directory block, size, packed size, count;
// blocks are 2048 bytes), directory of "ARCH" entries (block, blocks,
// size, packed size, -1, name length, name).

struct arc_entry { const char *name; u32 block, size, packed; };
static FILE *arc_f;
static struct arc_entry *arc_idx;
static u32 arc_n;

static u8 *zlib_unpack(const u8 *in, u32 n, u32 size)
{
    u8 *out = xalloc(size + 1);
    if (n < 2 || inflate(in + 2, n - 2, out, 0, size) != (int)size) { free(out); return 0; }
    out[size] = 0;
    return out;
}

static int arc_open(const char *path)
{
    arc_f = fopen(path, "rb");
    if (!arc_f) return 0;
    fseek(arc_f, 0, 2);
    long len = ftell(arc_f);
    u32 tail = len > 65536 ? 65536 : (u32)len;
    u8 *t = xalloc(tail);
    fseek(arc_f, len - (long)tail, 0);
    fread(t, 1, tail, arc_f);
    u32 i = tail >= 24 ? tail - 24 : 0;
    while (i > 0 && memcmp(t + i, "ARC\0", 4)) i--;
    if (memcmp(t + i, "ARC\0", 4)) return 0;
    u32 blk = rd32(t + i + 8), usz = rd32(t + i + 12), csz = rd32(t + i + 16), cnt = rd32(t + i + 20);
    free(t);
    u8 *c = xalloc(csz);
    fseek(arc_f, (long)blk * 2048, 0);
    if (fread(c, 1, csz, arc_f) != csz) return 0;
    u8 *dr = zlib_unpack(c, csz, usz);
    free(c);
    if (!dr) return 0;
    arc_idx = xalloc(cnt * sizeof *arc_idx);
    u32 p = 0;
    for (u32 k = 0; k < cnt && p + 28 <= usz; k++) {
        u32 nl = rd32(dr + p + 24);
        if (p + 28 + nl > usz || !nl) return 0;
        arc_idx[k].block = rd32(dr + p + 4);
        arc_idx[k].size = rd32(dr + p + 12);
        arc_idx[k].packed = rd32(dr + p + 16);
        arc_idx[k].name = (const char *)dr + p + 28;     // ends in its 0 byte
        arc_n = k + 1;
        p += 28 + nl;
    }
    return arc_n == cnt;
}

static int arc_find(const char *name)
{
    int found = -1;
    for (u32 k = 0; k < arc_n; k++) if (!_stricmp(arc_idx[k].name, name)) found = (int)k;
    return found;
}

static u8 *arc_get(const char *name, u32 *size)
{
    int k = arc_find(name);
    if (k < 0) {
        printf("  %s is not in Cora3.arc\n", name);
        fail("Cora3.arc is missing a file setup.exe needs.");
    }
    struct arc_entry *e = &arc_idx[k];
    u32 n = e->packed > e->size ? e->packed : e->size;
    u8 *raw = xalloc(n + 1);
    fseek(arc_f, (long)e->block * 2048, 0);
    u32 got = fread(raw, 1, n, arc_f);
    u8 *out;
    if (got >= 1 && raw[0] == 'x' && e->packed != e->size) {
        out = got >= e->packed ? zlib_unpack(raw, e->packed, e->size) : 0;
        free(raw);
        if (!out) { printf("  %s\n", name); fail("a file inside Cora3.arc is damaged."); }
    } else {
        if (got < e->size) { printf("  %s\n", name); fail("a file inside Cora3.arc is cut short."); }
        out = raw;
        out[e->size] = 0;
    }
    *size = e->size;
    return out;
}

// ---------------------------------------------------------------- bones

struct bone { char name[64]; double pos[3]; };
struct bones { struct bone b[400]; int n; };

static void bone_set(struct bones *bs, const char *name, u32 nl, const double *pos)
{
    char nm[64];
    if (nl >= sizeof nm) nl = sizeof nm - 1;
    memcpy(nm, name, nl);
    nm[nl] = 0;
    for (int i = 0; i < bs->n; i++)
        if (!strcmp(bs->b[i].name, nm)) { memcpy(bs->b[i].pos, pos, sizeof bs->b[i].pos); return; }
    if (bs->n >= 400) fail("too many bones in a model.");
    memcpy(bs->b[bs->n].name, nm, nl + 1);
    memcpy(bs->b[bs->n].pos, pos, sizeof bs->b[bs->n].pos);
    bs->n++;
}

static const double *bone(const struct bones *bs, const char *name)
{
    for (int i = 0; i < bs->n; i++) if (!strcmp(bs->b[i].name, name)) return bs->b[i].pos;
    printf("  bone %s\n", name);
    fail("a truck model is missing a bone.");
    return 0;
}

// model_bones.py: "Bone", length, name (letters, digits, _), 2 bytes,
// position (3 floats), rotation, scale.
static void model_bones(const u8 *d, u32 len, struct bones *bs)
{
    bs->n = 0;
    const u8 *at = findb(d, len, "Bone", 4);
    while (at) {
        u32 o = (u32)(at - d);
        u32 n = o + 4 < len ? d[o + 4] : 0;
        int ok = n > 0, alnum = 0;
        for (u32 k = 0; k < n && ok; k++) {
            u8 c = o + 5 + k < len ? d[o + 5 + k] : 0;
            if (o + 5 + k >= len || c >= 128) ok = 0;
            else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) alnum = 1;
            else if (c != '_') ok = 0;
        }
        if (!ok || !alnum) { at = o + 4 < len ? findb(d + o + 4, len - o - 4, "Bone", 4) : 0; continue; }
        u32 p = o + 5 + n + 2;
        if (p + 36 > len) break;
        double pos[3] = { rdf(d + p), rdf(d + p + 4), rdf(d + p + 8) };
        bone_set(bs, (const char *)d + o + 5, n, pos);
        at = findb(d + p + 36, len - p - 36, "Bone", 4);
    }
}

// .bonedump text: Bone 'name', then 12 numbers (3x3, then position).
struct dump { char name[64]; double diag[3], pos[3]; };
static int read_bonedump(const char *t, struct dump *out, int max)
{
    int n = 0;
    const char *end = t + strlen(t);
    for (const char *s = find(t, end, "Bone '"); s; s = find(s + 1, end, "Bone '")) {
        const char *nm = s + 6, *q = nm;
        while (q < end && *q != '\'') q++;
        if (q >= end || q == nm) continue;
        const char *p = q + 1;
        double v[12];
        int k = 0;
        for (; k < 12; k++) {
            const char *w = p;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '\f' || *p == '\v') p++;
            if (p == w) break;                      // \s+ needs at least one
            const char *tok = p;
            while ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.') p++;
            if (p == tok) break;
            char buf[64];
            u32 tl = (u32)(p - tok) < 63 ? (u32)(p - tok) : 63;
            memcpy(buf, tok, tl);
            buf[tl] = 0;
            v[k] = num(buf, 0);
        }
        if (k < 12) continue;
        u32 nl = (u32)(q - nm) < 63 ? (u32)(q - nm) : 63;
        int i = 0;
        for (; i < n; i++) if (strlen(out[i].name) == nl && !memcmp(out[i].name, nm, nl)) break;
        if (i == n) {
            if (n >= max) fail("too many bones in a bonedump.");
            memcpy(out[n].name, nm, nl);
            out[n].name[nl] = 0;
            n++;
        }
        out[i].diag[0] = v[0]; out[i].diag[1] = v[4]; out[i].diag[2] = v[8];
        out[i].pos[0] = v[9]; out[i].pos[1] = v[10]; out[i].pos[2] = v[11];
        s = p - 1;
    }
    return n;
}

static const struct dump *dump_get(const struct dump *d, int n, const char *name)
{
    for (int i = 0; i < n; i++) if (!strcmp(d[i].name, name)) return &d[i];
    printf("  %s\n", name);
    fail("a truck's bonedump is missing a bone.");
    return 0;
}

// ---------------------------------------------------------------- meshes

struct mesh { int rigid; char shader[64], bone[64]; u32 nv, nt; const u8 *v, *t; };

static int read_meshes(const u8 *d, u32 len, int strict, struct mesh *out, int max)
{
    const u8 *m = findb(d, len, ".model", 6);
    if (!m) fail("a .skin file has no .model name.");
    u32 p = (u32)(m - d) + 6;
    char shaders[64][64];
    int ns = 0;
    for (int lst = 0; lst < 2; lst++) {
        u32 cnt = rd16(d + p);
        p += 2;
        for (u32 k = 0; k < cnt; k++) {
            u32 ln = d[p];
            if (lst == 1 && ns < 64) {
                u32 l = ln < 63 ? ln : 63;
                memcpy(shaders[ns], d + p + 1, l);
                shaders[ns++][l] = 0;
            }
            p += 1 + ln;
        }
    }
    const u8 *b1 = findb(d, len, "\x0bMeshBlended", 12), *b2 = findb(d, len, "\x09MeshRigid", 10);
    const u8 *first = !b1 ? b2 : !b2 ? b1 : b1 < b2 ? b1 : b2;
    if (!first) fail("a .skin file has no meshes.");
    p = (u32)(first - d);
    u32 count = rd16(d + p - 2);
    if ((int)count > max) fail("too many meshes in a .skin file.");
    for (u32 i = 0; i < count; i++) {
        struct mesh *o = &out[i];
        u32 n = d[p];
        int blended = n == 11 && !memcmp(d + p + 1, "MeshBlended", 11);
        int rigid = n == 9 && !memcmp(d + p + 1, "MeshRigid", 9);
        if (!blended && !rigid) fail("a .skin file has an unknown mesh type.");
        p += 1 + n;
        u32 shader, nb, nx;
        if (blended) {
            shader = rd16(d + p); nb = rd16(d + p + 2); o->nv = rd16(d + p + 4); o->nt = rd16(d + p + 6); nx = rd16(d + p + 8);
            p += 10;
            o->bone[0] = 0;
            for (u32 k = 0; k < nb; k++) {
                u32 ln = d[p];
                if (k == 0) { u32 l = ln < 63 ? ln : 63; memcpy(o->bone, d + p + 1, l); o->bone[l] = 0; }
                p += 1 + ln;
            }
            p += o->nv * 8;
        } else {
            shader = rd16(d + p); o->nv = rd16(d + p + 2); o->nt = rd16(d + p + 4);
            p += 6;
            u32 ln = d[p], l = ln < 63 ? ln : 63;
            memcpy(o->bone, d + p + 1, l);
            o->bone[l] = 0;
            p += 1 + ln;
            nx = 0; nb = 1;
        }
        if ((int)shader >= ns) fail("a mesh uses a shader the .skin file doesn't list.");
        memcpy(o->shader, shaders[shader], 64);
        o->rigid = rigid;
        o->v = d + p;
        p += o->nv * 32;
        o->t = d + p;
        p += o->nt * 6 + nx * 8 + nb * 24;
        if (p > len) fail("a .skin file is cut short.");
    }
    if (strict && len - p != 6) fail("a .skin file has the wrong number of bytes after its meshes.");
    return (int)count;
}

// ---------------------------------------------------------------- pieces

struct piece { int frame, tex, lamp, part, kind; u32 nv, nt; double (*v)[8]; u16 (*t)[3]; };
static struct piece *pieces;
static int npieces, cap_pieces;

static void shader_texture(const char *name, int n, int *tex, int *lamp)
{
    char b[64];
    *lamp = 0;
    sprintf(b, "Car%dShader", n);
    if (!strcmp(name, b)) { *tex = n - 1; return; }
    sprintf(b, "NoEnvShaderCar%d", n);
    if (!strcmp(name, b)) { *tex = n - 1; return; }
    sprintf(b, "NoMShaderCar%d", n);
    if (!strcmp(name, b)) { *tex = n - 1; return; }
    sprintf(b, "WhiteShaderCar%d", n);
    if (!strcmp(name, b)) { *tex = n - 1; *lamp = 1; return; }
    if (!strcmp(name, "ShocksShader")) { *tex = TEX_SHOCK; return; }
    if (!strcmp(name, "DT") || !strcmp(name, "DT_J")) { *tex = TEX_TIRE; return; }
    if (!strcmp(name, "ColorsShaderNoLight")) { *tex = TEX_COLORS; return; }
    sprintf(b, "GlassShaderCar%d", n);
    if (!strcmp(name, b)) { *tex = TEX_GLASS; return; }
    sprintf(b, "GlassBShaderCar%d", n);
    if (!strcmp(name, b)) { *tex = TEX_GLASS_DMG + n - 1; return; }
    printf("  Car%d: shader %s\n", n, name);
    fail("a truck uses a shader setup.exe has no texture for.");
}

// shift(): off = 0 for unshifted pieces still adds 0.0 (turning -0.0 into
// 0.0, as the Python does); tires pass no offset at all.
static void add_piece(int frame, int tex, int lamp, const struct mesh *m, const double *off, int part, int kind)
{
    if (npieces == cap_pieces) {
        cap_pieces = cap_pieces ? cap_pieces * 2 : 256;
        pieces = realloc(pieces, cap_pieces * sizeof *pieces);
        if (!pieces) fail("out of memory.");
    }
    struct piece *p = &pieces[npieces++];
    p->frame = frame; p->tex = tex; p->lamp = lamp; p->part = part; p->kind = kind;
    p->nv = m->nv; p->nt = m->nt;
    p->v = xalloc(m->nv * sizeof *p->v);
    p->t = xalloc(m->nt * sizeof *p->t);
    for (u32 i = 0; i < m->nv; i++) {
        for (int k = 0; k < 8; k++) p->v[i][k] = rdf(m->v + 32 * i + 4 * k);
        if (off) for (int k = 0; k < 3; k++) p->v[i][k] = p->v[i][k] + off[k];
    }
    for (u32 i = 0; i < m->nt; i++)
        for (int k = 0; k < 3; k++) p->t[i][k] = (u16)rd16(m->t + 6 * i + 2 * k);
}

static void piece_tag(const char *bone, int lamp, int *part, int *kind)
{
    static const struct { const char *b; int part, kind; } tags[] = {
        { "door_0", 8, 0 }, { "door_1", 9, 0 }, { "frontBar", 16, 0 }, { "rearBar", 17, 0 },
        { "light0", 10, 1 }, { "light1", 11, 1 }, { "posL0", 12, 2 }, { "posL1", 13, 2 },
        { "brakeL0", 12, 3 }, { "brakeL1", 13, 3 }, { "backL0", 12, 4 }, { "backL1", 13, 4 },
    };
    *part = 255; *kind = 0;
    for (u32 i = 0; i < sizeof tags / sizeof tags[0]; i++)
        if (!strcmp(bone, tags[i].b)) {
            if (tags[i].kind && !lamp) return;          // Car4's rear lamps: always drawn
            *part = tags[i].part; *kind = tags[i].kind;
            return;
        }
}

static struct mesh meshes[512];
static void picture(int n, const char *name, const struct piece *ps, int np, const double (*tire)[3]);

static void truck_parts(int n, const struct bones *bp)
{
    static const double zero[3] = { 0, 0, 0 };
    char name[200];
    u32 len;
    int tex, lamp, part, kind;
    sprintf(name, "Objects\\Cars\\Car%d\\Car%d_0.skin", n, n);
    u8 *d = arc_get(name, &len);
    int c = read_meshes(d, len, 1, meshes, 512);
    for (int i = 0; i < c; i++) {
        const struct mesh *m = &meshes[i];
        shader_texture(m->shader, n, &tex, &lamp);
        int frame = 0;
        const double *off = zero;
        part = 255; kind = 0;
        if (m->rigid) {
            if (!memcmp(m->bone, "disk_", 5)) {
                frame = 1 + (int)num(m->bone + 5, 0);
            } else {
                off = bone(bp, m->bone);
                piece_tag(m->bone, lamp, &part, &kind);
            }
        }
        add_piece(frame, tex, lamp, m, off, part, kind);
    }
    free(d);
    sprintf(name, "Objects\\Cars\\Car%d\\Acc\\Hood1\\Hood1_0.skin", n);
    d = arc_get(name, &len);
    c = read_meshes(d, len, 1, meshes, 512);
    for (int i = 0; i < c; i++) {
        shader_texture(meshes[i].shader, n, &tex, &lamp);
        add_piece(0, tex, lamp, &meshes[i], bone(bp, "hood1"), 255, 0);
    }
    free(d);
    static const char *const windows[8] = { "frontscreen", "backscreen", "screen0", "screen1", "screen2", "screen3",
                                            "lightscreen0", "lightscreen1" };
    for (int w = 0; w < 8; w++) {
        int bi = -1;
        for (int i = 0; i < bp->n; i++) if (!_stricmp(bp->b[i].name, windows[w])) bi = i;
        if (bi < 0) continue;
        int wpart = w == 2 ? 8 : w == 3 ? 9 : w == 6 ? 10 : w == 7 ? 11 : 255;
        for (int v = 0; v < 2; v++) {
            sprintf(name, "Objects\\Cars\\Car%d\\Acc\\Screens\\%s_%d.SKIN", n, windows[w], v);
            if (arc_find(name) < 0) continue;
            d = arc_get(name, &len);
            c = read_meshes(d, len, 0, meshes, 512);
            for (int i = 0; i < c; i++) {
                shader_texture(meshes[i].shader, n, &tex, &lamp);
                add_piece(0, tex, lamp, &meshes[i], bp->b[bi].pos, wpart, (v ? GLASS_CRACKED : GLASS_WHOLE) + w);
            }
            free(d);
        }
    }
    sprintf(name, "Objects\\Cars\\Car%d\\Acc\\Dtire\\Dtire_0.skin", n);
    d = arc_get(name, &len);
    for (int w = 0; w < 4; w++) {
        c = read_meshes(d, len, 1, meshes, 512);
        for (int i = 0; i < c; i++) {
            shader_texture(meshes[i].shader, n, &tex, &lamp);
            add_piece(1 + w, tex, lamp, &meshes[i], 0, 255, 0);
        }
    }
    // the tire pieces point into d: copied already by add_piece
    free(d);
}

static void snorkel_parts(int n, int s, const struct bones *bp)
{
    char name[200];
    u32 len;
    int tex, lamp;
    sprintf(name, "Objects\\Cars\\Car%d\\Acc\\Snork%d\\Snork%d_0.skin", n, s, s);
    if (arc_find(name) < 0) return;
    u8 *d = arc_get(name, &len);
    int c = read_meshes(d, len, 1, meshes, 512);
    for (int i = 0; i < c; i++) {
        shader_texture(meshes[i].shader, n, &tex, &lamp);
        add_piece(0, tex, lamp, &meshes[i], bone(bp, "snork1"), 255, 0);
    }
    free(d);
}

// mirror(): body pieces flip x, wheel pieces z; triangles turn round.
static void mirror(struct piece *p)
{
    int a = p->frame == 0 ? 0 : 2;
    for (u32 i = 0; i < p->nv; i++) { p->v[i][a] = -p->v[i][a]; p->v[i][3 + a] = -p->v[i][3 + a]; }
    for (u32 i = 0; i < p->nt; i++) { u16 t = p->t[i][1]; p->t[i][1] = p->t[i][2]; p->t[i][2] = t; }
}

// ---------------------------------------------------------------- cars.lua

struct lua_car { int set; const char *arg[16]; int alen[16]; };
static const char *const lua_funcs[16] = {
    "SetMass", "SetTireAdherence", "SetSteering", "SetPhysDrivingTrain", "SetPhysGearRatiosAutomatic",
    "SetInitLength", "SetMinLength", "SetMaxLength", "SetKSusp", "SetKDump", "SetPhysRollingCoefs",
    "SetMaxRpm", "SetMinRpm", "SetRedRpm", "SetCarPartsPoints", 0 };
enum { L_MASS, L_ADH, L_STEER, L_TRAIN, L_GEARS, L_INIT, L_MIN, L_MAX, L_KSUSP, L_KDUMP, L_ROLL,
       L_MAXRPM, L_MINRPM, L_REDRPM, L_PARTS };
static struct lua_car lua[TRUCKS + 1];

static int is_word(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

// lua_blocks(): comments out, split at SetDataForCar("CarN"), then in each
// block the first call of each function: word, spaces, (no brackets).
static void lua_blocks(const char *src, u32 n)
{
    char *t = xalloc(n + 1);
    u32 k = 0;
    for (u32 i = 0; i < n;) {
        if (src[i] == '-' && i + 1 < n && src[i + 1] == '-') { while (i < n && src[i] != '\n') i++; continue; }
        t[k++] = src[i++];
    }
    t[k] = 0;
    const char *end = t + k, *mark = "SetDataForCar(\"Car";
    const char *s = find(t, end, mark);
    while (s) {
        const char *q = s + strlen(mark), *dg = q;
        while (*q >= '0' && *q <= '9') q++;
        if (q == dg || q[0] != '"' || q[1] != ')') { s = find(s + 1, end, mark); continue; }
        int car = (int)num(dg, 0);
        const char *body = q + 2, *next = body;
        for (next = find(body, end, mark); next; next = find(next + 1, end, mark)) {
            const char *r = next + strlen(mark), *r0 = r;
            while (*r >= '0' && *r <= '9') r++;
            if (r != r0 && r[0] == '"' && r[1] == ')') break;
        }
        const char *bend = next ? next : end;
        struct lua_car L;
        memset(&L, 0, sizeof L);
        for (const char *p = body; p < bend;) {
            if (!is_word(*p)) { p++; continue; }
            const char *w = p;
            while (p < bend && is_word(*p)) p++;
            const char *x = p;
            while (x < bend && (*x == ' ' || *x == '\t' || *x == '\r' || *x == '\n' || *x == '\f' || *x == '\v')) x++;
            if (x >= bend || *x != '(') { p = w + 1; continue; }
            const char *a = x + 1, *z = a;
            while (z < bend && *z != '(' && *z != ')') z++;
            if (z >= bend || *z != ')') { p = w + 1; continue; }
            for (int f = 0; lua_funcs[f]; f++)
                if (!L.arg[f] && strlen(lua_funcs[f]) == (u32)(p - w) && !memcmp(lua_funcs[f], w, (u32)(p - w))) {
                    L.arg[f] = a;
                    L.alen[f] = (int)(z - a);
                }
            p = z + 1;
        }
        if (car >= 1 && car <= TRUCKS) { L.set = 1; lua[car] = L; }
        s = next;
    }
    // t stays allocated: lua[] points into it
}

// The arguments of one call: split at commas, spaces and quotes off.
static int lua_nums(int car, int f, double *out, int max)
{
    const struct lua_car *L = &lua[car];
    if (!L->arg[f]) { printf("  Car%d %s\n", car, lua_funcs[f]); fail("cars.lua is missing a setting."); }
    int n = 0;
    const char *a = L->arg[f], *end = a + L->alen[f];
    while (a <= end) {
        const char *c = a;
        while (c < end && *c != ',') c++;
        const char *s = a, *e = c;
        while (s < e && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == '\f' || *s == '\v')) s++;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n' || e[-1] == '\f' || e[-1] == '\v')) e--;
        while (s < e && *s == '"') s++;
        while (e > s && e[-1] == '"') e--;
        char buf[64];
        u32 l = (u32)(e - s) < 63 ? (u32)(e - s) : 63;
        memcpy(buf, s, l);
        buf[l] = 0;
        if (n >= max) fail("cars.lua has a setting with too many numbers.");
        const char *stop;
        out[n++] = num(buf, &stop);
        if (*stop) inexact++;
        a = c + 1;
    }
    return n;
}

static double lua_num(int car, int f)
{
    double v[32];
    lua_nums(car, f, v, 32);
    return v[0];
}

// ---------------------------------------------------------------- sounds

struct curve { int param, n; double x[4], y[4]; };
struct sample { int wav, ngain; struct curve gain[2], pitch; };
struct tsound { int nsamples; struct sample s[5]; int start, stop; };
static struct tsound tsounds[TRUCKS];
static char wav_name[128][24];
static double wav_secs[128];
static int nwavs;
static int crank[11];

static double wav_seconds(const u8 *d, u32 n)
{
    u32 p = 12, rate = 0, size = 0;
    while (p + 8 <= n) {
        u32 c = rd32(d + p + 4);
        if (!memcmp(d + p, "fmt ", 4)) rate = rd32(d + p + 16);
        else if (!memcmp(d + p, "data", 4)) size = c;
        p += 8 + c + (c & 1);
    }
    if (!rate) fail("a sound file has no format chunk.");
    return (double)size / (double)rate;
}

// Step 3: each sound goes into the mod's sounds folder the first time its
// name comes up (the same files skin_to_c.py copies).
static char sound_dir[400];
static int sounds_written;

static int add_wav(const char *name, const u8 *data, u32 n)
{
    for (int i = 0; i < nwavs; i++) if (!strcmp(wav_name[i], name)) return i;
    if (nwavs >= 128 || strlen(name) >= 24) fail("too many sound files, or a sound name too long.");
    memcpy(wav_name[nwavs], name, strlen(name) + 1);
    wav_secs[nwavs] = wav_seconds(data, n);
    char path[500];
    sprintf(path, "%s\\%s", sound_dir, name);
    if (!write_file(path, data, n)) { printf("  %s\n", path); fail("could not write a sound file into the mod's sounds folder."); }
    sounds_written++;
    return nwavs++;
}

// No Cabela's 1: silence of the same length as its sound (22050 Hz, mono,
// 16-bit), so the mod's crank timing stays the same.
static u8 *silent_wav(u32 data_bytes, u32 *size)
{
    u8 *w = xalloc(44 + data_bytes);
    memset(w, 0, 44 + data_bytes);
    memcpy(w, "RIFF", 4);
    u32 v = 36 + data_bytes;
    memcpy(w + 4, &v, 4);
    memcpy(w + 8, "WAVEfmt ", 8);
    v = 16; memcpy(w + 16, &v, 4);
    u16 h = 1; memcpy(w + 20, &h, 2);                    // PCM
    h = 1; memcpy(w + 22, &h, 2);                        // mono
    v = 22050; memcpy(w + 24, &v, 4);
    v = 44100; memcpy(w + 28, &v, 4);                    // bytes per second
    h = 2; memcpy(w + 32, &h, 2);
    h = 16; memcpy(w + 34, &h, 2);
    memcpy(w + 36, "data", 4);
    memcpy(w + 40, &data_bytes, 4);
    *size = 44 + data_bytes;
    return w;
}

// curves(): "SF_KEY, PARAMk, count, x, y, ..." once or more.
static int curves(const char *a, const char *end, struct curve *out, int max)
{
    char tok[64][32];
    int nt = 0;
    while (a < end) {
        const char *c = a;
        while (c < end && *c != ',') c++;
        const char *s = a, *e = c;
        while (s < e && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == '\f' || *s == '\v')) s++;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n' || e[-1] == '\f' || e[-1] == '\v')) e--;
        if (e > s) {
            if (nt >= 64) fail("a sound curve is too long.");
            u32 l = (u32)(e - s) < 31 ? (u32)(e - s) : 31;
            memcpy(tok[nt], s, l);
            tok[nt++][l] = 0;
        }
        a = c + 1;
    }
    int n = 0;
    for (int i = 0; i < nt;) {
        if (strcmp(tok[i], "SF_KEY") || i + 2 >= nt || n >= max) fail("a sound curve is not SF_KEY, PARAM, count.");
        struct curve *cv = &out[n++];
        memset(cv, 0, sizeof *cv);
        cv->param = (int)num(tok[i + 1] + 5, 0);
        cv->n = (int)num(tok[i + 2], 0);
        if (cv->n > 4 || i + 3 + 2 * cv->n > nt) fail("a sound curve has more than 4 points.");
        for (int k = 0; k < cv->n; k++) {
            cv->x[k] = num(tok[i + 3 + 2 * k], 0);
            cv->y[k] = num(tok[i + 4 + 2 * k], 0);
        }
        i += 3 + 2 * cv->n;
    }
    return n;
}

// Gain\(([^;]*)\); : the first "Gain(" whose text up to the next ';' ends in ')'.
static int call_args(const char *s, const char *end, const char *fn, const char **a, const char **b)
{
    for (const char *p = find(s, end, fn); p; p = find(p + 1, end, fn)) {
        const char *q = p + strlen(fn), *semi = q;
        while (semi < end && *semi != ';') semi++;
        if (semi < end && semi > q && semi[-1] == ')') { *a = q; *b = semi - 1; return 1; }
    }
    return 0;
}

struct res_entry { char name[256]; u32 off, packed, size; };
static u8 *res_d;
static u32 res_len, res_n;
static struct res_entry *res_idx;

static int res_open(const char *path)
{
    res_d = read_file(path, &res_len);
    if (!res_d || res_len < 8) return 0;
    u32 off = rd32(res_d + 4);
    if (off + 12 > res_len) return 0;
    u32 cnt = rd32(res_d + off + 4), cs = rd32(res_d + off + 8);
    if (off + 12 + cs > res_len) return 0;
    // The directory unpacks to more than cnt entries (743 entries, 238960
    // bytes in the English data1.res); only the first cnt are used.
    u32 cap = cs * 64 + (1u << 20);
    u8 *t = xalloc(cap);
    int got = cs >= 2 ? inflate(res_d + off + 12 + 2, cs - 2, t, 0, cap) : -1;
    if (got < (int)(cnt * 268)) { free(t); return 0; }
    res_idx = xalloc(cnt * sizeof *res_idx);
    for (u32 i = 0; i < cnt; i++) {
        memcpy(res_idx[i].name, t + i * 268, 256);
        res_idx[i].name[255] = 0;
        res_idx[i].off = rd32(t + i * 268 + 256);
        res_idx[i].packed = rd32(t + i * 268 + 260);
        res_idx[i].size = rd32(t + i * 268 + 264);
    }
    res_n = cnt;
    free(t);
    return 1;
}

static u8 *res_get(const char *name, u32 *size)
{
    int f = -1;
    for (u32 i = 0; i < res_n; i++) if (!_stricmp(res_idx[i].name, name)) f = (int)i;
    if (f < 0) { printf("  %s\n", name); fail("Cabela's 1 data1.res is missing a sound."); }
    struct res_entry *e = &res_idx[f];
    u8 *out = e->off + e->packed <= res_len ? zlib_unpack(res_d + e->off, e->packed, e->size) : 0;
    if (!out) { printf("  %s\n", name); fail("a sound inside Cabela's 1 data1.res is damaged."); }
    *size = e->size;
    return out;
}

static void engine_sounds(void)
{
    char name[200];
    u32 n;
    for (int c = 1; c <= TRUCKS; c++) {
        sprintf(name, "Sound\\Cars\\Car%d\\Car%d.snd", c, c);
        u8 *d = arc_get(name, &n);
        const char *t = (const char *)d, *end = t + n;
        const char *bp = find(t, end, "BeginPlayProcedure");
        if (bp) end = bp;
        struct tsound *ts = &tsounds[c - 1];
        ts->nsamples = 0;
        const char *blk = find(t, end, "BeginSample();");
        while (blk) {
            blk += strlen("BeginSample();");
            const char *bend = find(blk, end, "BeginSample();");
            if (!bend) bend = end;
            const char *w = 0, *we = 0;
            for (const char *p = find(blk, bend, "WaveName(\""); p && !w; p = find(p + 1, bend, "WaveName(\"")) {
                const char *q = p + 10, *r = q;
                while (r < bend && *r != '"') r++;
                if (r < bend && r > q) { w = q; we = r; }
            }
            const char *ga, *gb, *pa, *pb;
            if (!w || !call_args(blk, bend, "Gain(", &ga, &gb) || !call_args(blk, bend, "Pitch(", &pa, &pb))
                fail("a truck's .snd file has a sample without WaveName, Gain or Pitch.");
            char wave[200];
            u32 k = 0;
            for (const char *p = w; p < we && k < sizeof wave - 1; p++) {
                if (p[0] == '\\' && p + 1 < we && p[1] == '\\') p++;      // "\\" -> "\"
                wave[k++] = *p;
            }
            wave[k] = 0;
            if (ts->nsamples >= 5) fail("a truck has more than 5 engine samples.");
            struct sample *s = &ts->s[ts->nsamples++];
            memset(s, 0, sizeof *s);
            s->ngain = curves(ga, gb, s->gain, 2);
            struct curve pc[2];
            if (curves(pa, pb, pc, 2) != 1) fail("a truck's engine sample has more than one pitch curve.");
            s->pitch = pc[0];
            char *base = strrchr(wave, '\\');
            char low[64];
            const char *b0 = base ? base + 1 : wave;
            u32 l = 0;
            for (; b0[l] && l < 63; l++) low[l] = b0[l] >= 'A' && b0[l] <= 'Z' ? b0[l] + 32 : b0[l];
            low[l] = 0;
            u32 wn;
            u8 *wd = arc_get(wave, &wn);
            s->wav = add_wav(low, wd, wn);
            free(wd);
            blk = bend < end ? bend : 0;
        }
        free(d);
        for (int k = 0; k < 2; k++) {
            char file[64];
            sprintf(name, "Sound\\Fx\\ignition\\car%d_%s.wav", c, k ? "sp" : "st");
            sprintf(file, "car%d_%s.wav", c, k ? "sp" : "st");
            u8 *wd = arc_get(name, &n);
            int w = add_wav(file, wd, n);
            free(wd);
            if (k) ts->stop = w; else ts->start = w;
        }
    }
    // Data sizes of the Cabela's 1 sounds (for the silent stand-ins).
    static const u32 crank_bytes[5] = { 16908, 26554, 17002, 6180, 78920 };
    static const char *const crank_files[5][2] = {
        { "crank_in.wav", ".\\data\\sound\\cars\\start_stop\\false_start_intro1.wav" },
        { "crank_loop.wav", ".\\data\\sound\\cars\\start_stop\\false_start_loop1.wav" },
        { "crank_out.wav", ".\\data\\sound\\cars\\start_stop\\false_start_out1.wav" },
        { "click.wav", ".\\data\\sound\\cars\\start_stop\\ignition.wav" },
        { "false_start.wav", ".\\data\\sound\\cars\\out\\pickup1\\false_start.wav" },
    };
    for (int k = 0; k < 5; k++) {
        u8 *wd = res_d ? res_get(crank_files[k][1], &n) : silent_wav(crank_bytes[k], &n);
        crank[k] = add_wav(crank_files[k][0], wd, n);
        free(wd);
    }
    static const char *const fx[6] = { "twater", "splash", "dengine2", "dengine1", "grind1", "steam" };
    for (int k = 0; k < 6; k++) {
        char file[64];
        sprintf(name, "Sound\\Fx\\%s.wav", fx[k]);
        sprintf(file, "%s.wav", fx[k]);
        u8 *wd = arc_get(name, &n);
        crank[5 + k] = add_wav(file, wd, n);
        free(wd);
    }
}

// ---------------------------------------------------------------- font

static u8 glyph[95][5];

static void font_glyphs(void)
{
    u32 n;
    u8 *f = arc_get("FONTS\\FONTLARGE.FNT", &n);
    int have[95] = { 0 };
    for (u32 o = 37; o + 25 <= n; o += 25) {
        int c = f[o];
        double v[6];
        int ok = 1;
        for (int k = 0; k < 6; k++) {
            v[k] = rdf(f + o + 1 + 4 * k);
            if (!(v[k] == v[k] && v[k] >= -1 && v[k] <= 512)) ok = 0;
        }
        if (!ok || c < 32 || c > 126) continue;
        int x = rnd(v[0] * 256 + v[4]);
        int x2 = x + rnd(v[5]), y2 = rnd(v[3] * 256);
        glyph[c - 32][0] = (u8)x;
        glyph[c - 32][1] = (u8)rnd(v[1] * 256);
        glyph[c - 32][2] = (u8)(x2 < 255 ? x2 : 255);
        glyph[c - 32][3] = (u8)(y2 < 255 ? y2 : 255);
        glyph[c - 32][4] = (u8)rnd(v[5]);
        have[c - 32] = 1;
    }
    for (int c = 0; c < 95; c++)
        if (!have[c]) { memset(glyph[c], 0, 5); if (c == 0) glyph[c][4] = 8; }
    free(f);
}

// ---------------------------------------------------------------- output

struct out { u8 *b; u32 n, cap; };
static void put(struct out *o, const void *p, u32 n)
{
    if (o->n + n > o->cap) {
        while (o->n + n > o->cap) o->cap = o->cap ? o->cap * 2 : 1 << 20;
        o->b = realloc(o->b, o->cap);
        if (!o->b) fail("out of memory.");
    }
    memcpy(o->b + o->n, p, n);
    o->n += n;
}
static void put32(struct out *o, u32 v) { put(o, &v, 4); }
static void putf(struct out *o, double v) { float f = (float)v; put(o, &f, 4); }
static void put16(struct out *o, u32 v) { u16 s = (u16)v; put(o, &s, 2); }

static void put_curve(struct out *o, const struct curve *c)
{
    put32(o, (u32)c->param);
    put32(o, (u32)c->n);
    for (int k = 0; k < 4; k++) putf(o, k < c->n ? c->x[k] : 0.0);
    for (int k = 0; k < 4; k++) putf(o, k < c->n ? c->y[k] : 0.0);
}

// Same model: same texture, lamp, points, triangles (-0.0 counts as 0.0,
// as in Python's tuple compare).
static int same_model(const struct piece *a, const struct piece *b)
{
    if (a->tex != b->tex || a->lamp != b->lamp || a->nv != b->nv || a->nt != b->nt) return 0;
    for (u32 i = 0; i < a->nv; i++)
        for (int k = 0; k < 8; k++) if (a->v[i][k] != b->v[i][k]) return 0;
    for (u32 i = 0; i < a->nt; i++)
        for (int k = 0; k < 3; k++) if (a->t[i][k] != b->t[i][k]) return 0;
    return 1;
}

// The whole of trucks.bin; returns its size.
static u32 build_trucks_bin(const char *cars_lua, u32 cars_lua_n, const char *out_path, double red_rpm)
{
    static const int side_swap[14] = { 0, 1, 2, 3, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12 };
    u32 n;
    lua_blocks(cars_lua, cars_lua_n);
    // Truck names: RegisterCar("CarN", "Name" in Scripts\ACCS.LUA.
    char names[TRUCKS + 1][16];
    memset(names, 0, sizeof names);
    {
        u8 *t = arc_get("Scripts\\ACCS.LUA", &n);
        const char *s = (const char *)t, *end = s + n, *mk = "RegisterCar(\"Car";
        for (const char *p = find(s, end, mk); p; p = find(p + 1, end, mk)) {
            const char *q = p + strlen(mk), *d0 = q;
            while (*q >= '0' && *q <= '9') q++;
            if (q == d0 || *q != '"') continue;
            int car = (int)num(d0, 0);
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n' || *q == '\f' || *q == '\v') q++;
            if (*q != ',') continue;
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n' || *q == '\f' || *q == '\v') q++;
            if (*q != '"') continue;
            const char *nm = ++q;
            while (q < end && *q != '"') q++;
            if (q >= end || q == nm) continue;
            if (car >= 1 && car <= TRUCKS) {
                u32 l = (u32)(q - nm);
                if (l > 15) fail("a truck name is longer than 15 letters.");
                memset(names[car], 0, 16);
                memcpy(names[car], nm, l);
            }
        }
        free(t);
    }

    struct out o = { 0, 0, 0 };
    struct out models = { 0, 0, 0 }, draws = { 0, 0, 0 }, recs = { 0, 0, 0 }, data = { 0, 0, 0 };
    struct piece *model[2048];
    int nmodels = 0, ndraws = 0;
    static struct bones bp;
    static struct dump dump[400];
    char name[200];

    for (int c = 1; c <= TRUCKS; c++) {
        if (!lua[c].set || !names[c][0]) { printf("  Car%d\n", c); fail("a truck is missing from cars.lua or ACCS.LUA."); }
        sprintf(name, "Objects\\Cars\\Car%d\\Car%d_0.model", c, c);
        u8 *md = arc_get(name, &n);
        model_bones(md, n, &bp);
        free(md);
        sprintf(name, "Objects\\Cars\\Car%d\\Car%d_0.bonedump", c, c);
        u8 *bd = arc_get(name, &n);
        int nd = read_bonedump((const char *)bd, dump, 400);
        free(bd);

        // Pieces: body, snorkel 1, snorkel 2.
        int first[3], count[3];
        for (int g = 0; g < 3; g++) {
            npieces = 0;
            if (g == 0) {
                truck_parts(c, &bp);
                // The picker picture, from the pieces before the mirror (as skin_to_c.py).
                double tp[4][3];
                for (int i = 0; i < 4; i++) {
                    char b[32];
                    sprintf(b, "wheel_%d", i);
                    memcpy(tp[i], dump_get(dump, nd, b)->pos, sizeof tp[i]);
                }
                picture(c, names[c], pieces, npieces, (const double (*)[3])tp);
            }
            else snorkel_parts(c, g, &bp);
            first[g] = ndraws;
            count[g] = npieces;
            for (int i = 0; i < npieces; i++) {
                struct piece *p = &pieces[i];
                mirror(p);
                if (p->frame == 0 && p->part < 14) p->part = side_swap[p->part];
                int m = 0;
                for (; m < nmodels; m++) if (same_model(model[m], p)) break;
                if (m == nmodels) {
                    if (nmodels >= 2048) fail("too many truck pieces.");
                    struct piece *keep = xalloc(sizeof *keep);
                    *keep = *p;
                    model[nmodels++] = keep;
                } else {
                    free(p->v);
                    free(p->t);
                }
                put16(&draws, (u32)m);
                put16(&draws, (u32)p->frame);
                put16(&draws, (u32)p->part);
                put16(&draws, (u32)p->kind);
                ndraws++;
            }
        }

        // Physics record (struct truck_data).
        double spring[4][3], tire[4][3];
        for (int i = 0; i < 4; i++) {
            char b[32];
            sprintf(b, "posArc_%d", i);
            memcpy(spring[i], dump_get(dump, nd, b)->pos, sizeof spring[i]);
            sprintf(b, "wheel_%d", i);
            memcpy(tire[i], dump_get(dump, nd, b)->pos, sizeof tire[i]);
        }
        const double *wf = bone(&bp, "weighCenter_front"), *wb = bone(&bp, "weighCenter_back");
        double train[8] = { 0 }, gears[16], parts[64];
        int ntrain = lua_nums(c, L_TRAIN, train, 8);
        (void)ntrain;
        if (lua_nums(c, L_GEARS, gears, 16) != 6) fail("cars.lua: SetPhysGearRatiosAutomatic needs 6 numbers.");
        if (lua_nums(c, L_PARTS, parts, 64) != 26) fail("cars.lua: SetCarPartsPoints needs 26 numbers.");
        u8 nm[16];
        memcpy(nm, names[c], 16);
        put(&recs, nm, 16);
        put32(&recs, (u32)first[0]); put32(&recs, (u32)count[0]);
        put32(&recs, (u32)first[1]); put32(&recs, (u32)first[2]);
        put32(&recs, (u32)count[1]); put32(&recs, (u32)count[2]);
        for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) putf(&recs, spring[i][k]);
        for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) putf(&recs, tire[i][k]);
        putf(&recs, spring[0][1] - tire[0][1]);
        for (int i = 0; i < 3; i++) {
            char b[32];
            sprintf(b, "colDummy_%d", i);
            const struct dump *dd = dump_get(dump, nd, b);
            for (int k = 0; k < 3; k++) putf(&recs, dd->diag[k]);
            for (int k = 0; k < 3; k++) putf(&recs, dd->pos[k]);
        }
        putf(&recs, root(((0 + sq(wf[0] - tire[0][0])) + sq(wf[1] - tire[0][1])) + sq(wf[2] - tire[0][2])));
        putf(&recs, root(((0 + sq(wb[0] - tire[2][0])) + sq(wb[1] - tire[2][1])) + sq(wb[2] - tire[2][2])));
        putf(&recs, spring[0][1] - wf[1]);
        putf(&recs, spring[2][1] - wb[1]);
        putf(&recs, lua_num(c, L_MASS));
        putf(&recs, lua_num(c, L_ADH));
        putf(&recs, lua_num(c, L_STEER));
        put32(&recs, (u32)(int)train[0]);
        put32(&recs, (u32)(int)train[1]);
        putf(&recs, train[2]); putf(&recs, train[4]); putf(&recs, train[3]); putf(&recs, train[5]);
        for (int k = 0; k < 6; k++) putf(&recs, gears[k]);
        putf(&recs, lua_num(c, L_INIT));
        putf(&recs, lua_num(c, L_MIN));
        putf(&recs, lua_num(c, L_MAX));
        putf(&recs, lua_num(c, L_KSUSP));
        putf(&recs, lua_num(c, L_KDUMP));
        putf(&recs, lua_num(c, L_ROLL));
        putf(&recs, lua_num(c, L_MAXRPM));
        putf(&recs, lua_num(c, L_MINRPM));
        putf(&recs, red_rpm > 0 ? red_rpm : lua_num(c, L_REDRPM));
        const double *dr = bone(&bp, "driver1"), *door = bone(&bp, "door_0");
        putf(&recs, -dr[0]); putf(&recs, dr[1]); putf(&recs, dr[2]);
        putf(&recs, -door[0]); putf(&recs, door[1]); putf(&recs, door[2]);
        for (int k = 0; k < 26; k++) putf(&recs, parts[k]);
        if (recs.n != (u32)c * TRUCK_REC) fail("internal: truck record size.");
        printf("  Car%-2d %-10s %3d pieces\n", c, names[c], count[0]);
    }

    // Models: header table, then their arrays.
    for (int m = 0; m < nmodels; m++) {
        const struct piece *p = model[m];
        double lo[3], hi[3], ctr[3], r = 0;
        for (int k = 0; k < 3; k++) {
            lo[k] = hi[k] = p->v[0][k];
            for (u32 i = 1; i < p->nv; i++) {
                if (p->v[i][k] < lo[k]) lo[k] = p->v[i][k];
                if (p->v[i][k] > hi[k]) hi[k] = p->v[i][k];
            }
            ctr[k] = (lo[k] + hi[k]) / 2;
        }
        for (u32 i = 0; i < p->nv; i++) {
            double d = root(((0 + sq(p->v[i][0] - ctr[0])) + sq(p->v[i][1] - ctr[1])) + sq(p->v[i][2] - ctr[2]));
            if (i == 0 || d > r) r = d;
        }
        put32(&models, p->nv); put32(&models, p->nt); put32(&models, (u32)p->tex); put32(&models, p->lamp ? 1 : 0);
        for (int k = 0; k < 3; k++) putf(&models, ctr[k] * UNITS);
        putf(&models, r * UNITS);
        for (u32 i = 0; i < p->nv; i++) for (int k = 0; k < 3; k++) putf(&data, p->v[i][k] * UNITS);
        for (u32 i = 0; i < p->nv; i++) for (int k = 3; k < 6; k++) putf(&data, p->v[i][k]);
        for (u32 i = 0; i < p->nt; i++) for (int k = 0; k < 3; k++) put16(&data, p->t[i][k]);
        if (p->nt & 1) put16(&data, 0);
        for (u32 i = 0; i < p->nt; i++)
            for (int k = 0; k < 3; k++) {
                const double *v = p->v[p->t[i][k]];
                for (int j = 6; j < 8; j++) {
                    double x = v[j] * 255;
                    int q = x > 32767 ? 32767 : x < -32768 ? -32768 : rnd(x);
                    put16(&data, (u32)q);
                }
            }
    }

    // Sounds and the font.
    struct out snd = { 0, 0, 0 };
    for (int c = 0; c < TRUCKS; c++) {
        const struct tsound *ts = &tsounds[c];
        static const struct curve empty;
        put32(&snd, (u32)ts->nsamples);
        for (int k = 0; k < 5; k++) {
            if (k < ts->nsamples) {
                const struct sample *s = &ts->s[k];
                put32(&snd, (u32)s->wav);
                put32(&snd, (u32)s->ngain);
                put_curve(&snd, s->ngain > 0 ? &s->gain[0] : &empty);
                put_curve(&snd, s->ngain > 1 ? &s->gain[1] : &empty);
                put_curve(&snd, &s->pitch);
            } else {
                put32(&snd, 0); put32(&snd, 0);
                put_curve(&snd, &empty); put_curve(&snd, &empty); put_curve(&snd, &empty);
            }
        }
        put32(&snd, (u32)ts->start);
        put32(&snd, (u32)ts->stop);
    }
    if (snd.n != TRUCKS * SOUND_REC) fail("internal: sound record size.");

    put(&o, "SXTB", 4);
    put32(&o, 1);
    put32(&o, (u32)nmodels);
    put32(&o, (u32)ndraws);
    put32(&o, TRUCKS);
    put32(&o, (u32)nwavs);
    put32(&o, TRUCK_REC);
    put32(&o, SOUND_REC);
    put(&o, models.b, models.n);
    put(&o, draws.b, draws.n);
    put(&o, recs.b, recs.n);
    put(&o, snd.b, snd.n);
    for (int i = 0; i < nwavs; i++) {
        u8 b[24];
        memset(b, 0, 24);
        memcpy(b, wav_name[i], strlen(wav_name[i]));
        put(&o, b, 24);
        putf(&o, wav_secs[i]);
    }
    put(&o, glyph, sizeof glyph);
    put(&o, "", 1);
    put(&o, data.b, data.n);
    if (!write_file(out_path, o.b, o.n)) fail("could not write trucks.bin into the mod folder.");
    printf("  %d models, %d draws, %d sounds\n", nmodels, ndraws, nwavs);
    return o.n;
}
