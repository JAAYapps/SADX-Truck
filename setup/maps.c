// Step 5: Cabela's 16 maps for the mod's map menu, the C copy of
// map_to_bin.py (one, menu, objects, skin_prelit, model_bones,
// shader_table, grass, brushes, brush_faces, water_env, water_pack,
// script_textures, sky).  Included by setup.c after textures.c.
// map_to_bin.py's comments have every file layout and where in Cabela's
// files each thing comes from; this file keeps the same names so the two
// can be read side by side.
//
// The .bin files come out the same as map_to_bin.py's: every sum is done
// in 64-bit floats in the Python's order (same build flags as
// trucks_bin.c), rounded to 32 bits only when written.  Two places can
// still differ in the last bit of a float: cos and sin (Windows' msvcrt
// against the C library Python used), for turned objects and bones.
// PNGs have the same pixels but are packed by png_write, so their bytes
// differ; the ground, detail and menu pictures are written with alpha
// 255 (map_to_bin.py wrote them without alpha).

__declspec(dllimport) int remove(const char *);
__declspec(dllimport) void qsort(void *, u32, u32, int (__cdecl *)(const void *, const void *));

#define MAP_COUNT 16
static const char *const map_ids[MAP_COUNT] = {
    "bj_a", "bj_b", "nf_a", "nf_b", "bj_d", "bj_c", "cn_b", "nf_c",
    "sw_b", "sw_c", "cn_a", "cn_c", "sw_a", "ak_a", "ak_b", "ak_c" };
#define M_GBIX0 792000u
#define M_MENU_GBIX0 (M_GBIX0 + 256)
#define M_DETAIL_GBIX0 (M_GBIX0 + 300)
#define M_WATER_GBIX0 (M_GBIX0 + 500)
#define M_SKY_GBIX0 (M_GBIX0 + 600)
#define M_OBJ_GBIX0 793000u
#define M_OBJ_TEX_MAX 32
#define M_STEP 2
#define M_NB 16
#define M_CELLS 32
#define M_NP (M_CELLS + 1)
#define M_S 0.1
#define BC_CELL 500.0
#define M_PI_180 (3.141592653589793 / 180.0)

static int maps_written;

// ---------------------------------------------------------------- small helpers

static void pad4(struct out *o) { static const u8 z[3]; if (o->n & 3) put(o, z, 4 - (o->n & 3)); }
static void put_name16(struct out *o, const char *s) { char b[16]; memset(b, 0, 16); memcpy(b, s, strlen(s) < 16 ? strlen(s) : 16); put(o, b, 16); }

static void out_file(const char *path, struct out *o)
{
    if (!write_file(path, o->b, o->n)) { printf("  %s\n", path); fail("could not write a map file into the mod folder."); }
    free(o->b);
    o->b = 0; o->n = o->cap = 0;
}

// Python str.lower() for latin-1 text.
static int lc(int c)
{
    c &= 255;
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
    return c;
}
static void lower(char *s) { for (; *s; s++) *s = (char)lc(*s); }
// Python's \s and \w on latin-1 text.
static int is_sp(int c)
{
    c &= 255;
    return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0;
}
static int is_w(int c)
{
    c &= 255;
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == 0xAA || c == 0xB2 || c == 0xB3 || c == 0xB5 || c == 0xB9 || c == 0xBA || (c >= 0xBC && c <= 0xBE) ||
           (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || c >= 0xF8;
}
// Starts with `what` at p (case blind or not)?
static int at(const char *p, const char *end, const char *what, int ci)
{
    for (; *what; what++, p++) {
        if (p >= end) return 0;
        if (ci ? lc(*p) != lc(*what) : *p != *what) return 0;
    }
    return 1;
}
// Python s.replace('\\\\', '\\').replace('/', '\\').lower(), in place.
static void norm_path(char *s, int pairs)
{
    char *o = s;
    for (const char *p = s; *p; p++) {
        if (pairs && p[0] == '\\' && p[1] == '\\') { *o++ = '\\'; p++; continue; }
        *o++ = *p == '/' ? '\\' : *p;
    }
    *o = 0;
    lower(s);
}
static int have(const char *name) { return arc_find(name) >= 0; }
static int strstr_(const char *h, const char *n)
{
    u32 l = strlen(n);
    for (; *h; h++) if (!memcmp(h, n, l)) return 1;
    return 0;
}

static u8 *bsp_get(const char *id, u32 *n)
{
    char name[64];
    sprintf(name, "bsp\\%s.bsp", id);
    return arc_get(name, n);
}

// map_to_obj.py chunks(): the first chunk of each id.
struct chunks { const u8 *p[64]; u32 n[64]; };
static void chunks_of(const u8 *b, u32 n, struct chunks *c)
{
    memset(c, 0, sizeof *c);
    u32 p = 0;
    while (p + 8 <= n) {
        u32 id = rd32(b + p), size = rd32(b + p + 4);
        if (p + 8 + size > n) fail("a Cabela's map file (.BSP) is cut short.");
        if (id < 64 && !c->p[id]) { c->p[id] = b + p + 8; c->n[id] = size; }
        p += 8 + size;
    }
    if (p != n) fail("a Cabela's map file (.BSP) doesn't end where its parts say.");
}

// 3 x 3 matrices, the Python's rot() and mul3() with the same sums.
typedef double m3[3][3];
static void rot(int axis, double deg, m3 m)
{
    double a = deg * M_PI_180, c = cos(a), s = sin(a);
    memset(m, 0, sizeof(m3));
    if (axis == 0) { m[0][0] = 1; m[1][1] = c; m[1][2] = -s; m[2][1] = s; m[2][2] = c; }
    else if (axis == 1) { m[0][0] = c; m[0][2] = s; m[1][1] = 1; m[2][0] = -s; m[2][2] = c; }
    else { m[0][0] = c; m[0][1] = -s; m[1][0] = s; m[1][1] = c; m[2][2] = 1; }
}
static void mul3(m3 a, m3 b, m3 o)
{
    m3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            double s = 0;
            for (int k = 0; k < 3; k++) s += a[i][k] * b[k][j];
            r[i][j] = s;
        }
    memcpy(o, r, sizeof r);
}
// Ry(ang y) * Rx(ang x) * Rz(ang z)
static void turn_yxz(double ax, double ay, double az, m3 m)
{
    m3 a, b, c;
    rot(1, ay, a); rot(0, ax, b); mul3(a, b, m);
    rot(2, az, c); mul3(m, c, m);
}

// ---------------------------------------------------------------- scripts
// Every .script file's functions, found the way the Python's
//   re.finditer(r'function\s+(\w+)\s*\(\)(.*?)\nend', text, re.S | re.I)
// finds them; files in sorted (lower case) name order.

struct sfn { char name[96]; const char *body; u32 len; };
static struct sfn *sfns;
static int sfn_n, sfn_cap;
static char **script_texts;

// Matches in text[0..n) (ci: re.I), each added through add().
static void find_functions(const char *t, u32 n, int ci, void (*add)(const char *, u32, const char *, u32, void *), void *ctx)
{
    const char *end = t + n, *p = t;
    while (p < end) {
        if (!at(p, end, "function", ci)) { p++; continue; }
        const char *q = p + 8, *nm, *ne;
        if (q >= end || !is_sp(*q)) { p++; continue; }
        while (q < end && is_sp(*q)) q++;
        nm = q;
        while (q < end && is_w(*q)) q++;
        ne = q;
        if (ne == nm) { p++; continue; }
        // \w+ then \s*\(\): the regex can also give back name letters, but a
        // shorter name can't be followed by spaces or "(", so greedy is it.
        while (q < end && is_sp(*q)) q++;
        if (!at(q, end, "()", 0)) { p++; continue; }
        q += 2;
        const char *b = q, *e = 0;
        for (const char *s = q; s < end; s++) if (at(s, end, "\nend", ci)) { e = s; break; }
        if (!e) { p++; continue; }
        add(nm, (u32)(ne - nm), b, (u32)(e - b), ctx);
        p = e + 4;
    }
}

static void add_sfn(const char *nm, u32 nl, const char *b, u32 bl, void *ctx)
{
    (void)ctx;
    if (sfn_n == sfn_cap) {
        sfn_cap = sfn_cap ? sfn_cap * 2 : 1024;
        sfns = realloc(sfns, (u32)sfn_cap * sizeof *sfns);
        if (!sfns) fail("out of memory.");
    }
    struct sfn *f = &sfns[sfn_n++];
    if (nl >= sizeof f->name) nl = sizeof f->name - 1;
    memcpy(f->name, nm, nl);
    f->name[nl] = 0;
    lower(f->name);
    f->body = b; f->len = bl;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void scripts_load(void)
{
    char **names = xalloc(arc_n * sizeof *names);
    int nn = 0;
    for (u32 k = 0; k < arc_n; k++) {
        u32 l = strlen(arc_idx[k].name);
        if (l < 7) continue;
        char *s = xalloc(l + 1);
        memcpy(s, arc_idx[k].name, l + 1);
        lower(s);
        if (strcmp(s + l - 7, ".script")) { free(s); continue; }
        names[nn++] = s;
    }
    qsort(names, (u32)nn, sizeof *names, cmp_str);
    script_texts = xalloc((u32)nn * sizeof *script_texts);
    for (int i = 0; i < nn; i++) {
        u32 n;
        script_texts[i] = (char *)arc_get(names[i], &n);
        find_functions(script_texts[i], n, 1, add_sfn, 0);
        free(names[i]);
    }
    free(names);
    printf("  %d shader scripts, %d shaders\n", nn, sfn_n);
}

static const struct sfn *sfn_find(const char *name)        // the first one in file order
{
    for (int i = 0; i < sfn_n; i++) if (!strcmp(sfns[i].name, name)) return &sfns[i];
    return 0;
}

// The k-th texture("...") in a body (spaces: \s* allowed between texture
// and "(", as script_textures has; tight: shader_table's texture\(").
// Copies the name (as written) into out; 0 = no k-th.
static int body_texture(const char *b, u32 n, int k, int spaces, int ci, char *out, u32 cap)
{
    const char *end = b + n;
    for (const char *p = b; p < end; p++) {
        if (!at(p, end, "Texture", ci)) continue;
        const char *q = p + 7;
        if (spaces) while (q < end && is_sp(*q)) q++;
        if (!at(q, end, "(\"", 0)) continue;
        q += 2;
        const char *s = q;
        while (q < end && *q != '"') q++;
        if (q >= end || q == s) continue;
        if (k-- == 0) {
            u32 l = (u32)(q - s);
            if (l >= cap) l = cap - 1;
            memcpy(out, s, l);
            out[l] = 0;
            return 1;
        }
        p = q;                                       // findall goes on after this match
    }
    return 0;
}

static int body_has(const char *b, u32 n, const char *what)
{
    for (const char *p = b; p < b + n; p++) if (at(p, b + n, what, 1)) return 1;
    return 0;
}

// shader_table(): shader -> (texture, alpha test); only textures that exist.
struct shtex { const char *name; char tex[160]; int alpha; };
static struct shtex *shtab;
static int shtab_n;

static void shader_table(void)
{
    shtab = xalloc((u32)(sfn_n ? sfn_n : 1) * sizeof *shtab);
    for (int i = 0; i < sfn_n; i++) {
        char tx[160];
        if (!body_texture(sfns[i].body, sfns[i].len, 0, 0, 1, tx, sizeof tx)) continue;
        norm_path(tx, 1);
        if (!have(tx)) continue;
        int dup = 0;
        for (int j = 0; j < shtab_n && !dup; j++) if (!strcmp(shtab[j].name, sfns[i].name)) dup = 1;
        if (dup) continue;
        struct shtex *s = &shtab[shtab_n++];
        s->name = sfns[i].name;
        memcpy(s->tex, tx, strlen(tx) + 1);
        s->alpha = body_has(sfns[i].body, sfns[i].len, "alphatest");
    }
}

static const struct shtex *shader(const char *name)
{
    char l[96];
    u32 n = strlen(name);
    if (n >= sizeof l) n = sizeof l - 1;
    memcpy(l, name, n);
    l[n] = 0;
    lower(l);
    for (int i = 0; i < shtab_n; i++) if (!strcmp(shtab[i].name, l)) return &shtab[i];
    return 0;
}

// ---------------------------------------------------------------- pictures

static void pack_dir(char *out, const char *pack)
{
    char t[400];
    path_join(t, mod_dir, "textures");
    _mkdir(t);
    path_join(out, t, pack);
    _mkdir(out);
}

// One texture into a pack folder; alpha: -1 = the picture's own, else that value for every pixel.
static void pack_png(const char *dir, const char *file, const char *arc_name, int alpha)
{
    char path[500];
    struct img m = tga(arc_name);
    if (alpha >= 0) for (int i = 0; i < m.w * m.h; i++) m.p[i * 4 + 3] = (u8)alpha;
    path_join(path, dir, file);
    png_write(path, &m);
    free(m.p);
}

static void index_line(struct out *o, u32 gbix, const char *file)
{
    char b[300];
    sprintf(b, "%u,%s\n", gbix, file);
    put(o, b, strlen(b));
}

// basename of a \ path, with ".tga" made ".png" (Python's replace: every ".tga").
static void png_name(char *out, const char *path)
{
    const char *s = strrchr(path, '\\');
    s = s ? s + 1 : path;
    char *o = out;
    while (*s) {
        if (!memcmp(s, ".tga", 4)) { memcpy(o, ".png", 4); o += 4; s += 4; }
        else *o++ = *s++;
    }
    *o = 0;
}

// ---------------------------------------------------------------- the ground: one()

static void ground(int k, const struct chunks *c)
{
    const char *id = map_ids[k];
    u32 gbix0 = M_GBIX0 + 16 * (u32)k;
    const u8 *t = c->p[33];
    if (!t || c->n[33] < 32) fail("a Cabela's map has no ground (chunk 33).");
    u32 n = rd32(t);
    if (n != M_NB * M_CELLS * M_STEP + 1 || c->n[33] < 32 + n * n * 2 + 512) fail("a Cabela's map's ground isn't the size setup.exe knows.");
    double x0 = rdf(t + 8), y0 = rdf(t + 12), z0 = rdf(t + 16), sx = rdf(t + 20), sy = rdf(t + 24), sz = rdf(t + 28);
    const u8 *h = t + 32;
#define H(r, col) ((double)rd16(h + 2 * ((u32)(r) * n + (u32)(col))))

    // spawns(): info_spawn_point entities in the entity text (chunk 9).
    double sp[3] = { 0, 0, 0 };
    char spname[64] = "";
    int found = 0, found_garage = 0;
    if (c->p[9]) {
        const char *e = (const char *)c->p[9], *ee = e + c->n[9];
        for (const char *p = e; p < ee; p++) {
            if (!at(p, ee, "entity(\"info_spawn_point\")", 0)) continue;
            const char *b = p + 26, *be = b;
            while (be < ee && !at(be, ee, "entity(", 0)) be++;
            double v[3];
            int ok = 0;
            for (const char *q = b; q < be && !ok; q++) {
                if (!at(q, be, "\"position\", \"", 0)) continue;
                const char *s = q + 13, *x = s;
                while (x < be && ((*x >= '0' && *x <= '9') || *x == '-' || *x == '.' || *x == 'e' || *x == ' ')) x++;
                if (x == s || x >= be || *x != '"') continue;
                const char *r = s;
                for (int i = 0; i < 3; i++) v[i] = num(r, &r);
                ok = 1;
            }
            if (!ok) fail("a Cabela's spawn point has no position.");
            char tn[64] = "";
            for (const char *q = b; q < be; q++) {
                if (!at(q, be, "\"target_name\", \"", 0)) continue;
                const char *s = q + 16, *x = s;
                while (x < be && *x != '"') x++;
                if (x >= be) continue;
                u32 l = (u32)(x - s) < 63 ? (u32)(x - s) : 63;
                memcpy(tn, s, l); tn[l] = 0;
                break;
            }
            // the first spawn named "garage", else the first spawn
            int garage = !strcmp(tn, "garage");
            if (!found || (garage && !found_garage)) { memcpy(sp, v, sizeof v); memcpy(spname, tn, sizeof tn); }
            if (garage) found_garage = 1;
            found = 1;
            p = be - 1;
        }
    }
    if (!found) fail("a Cabela's map has no spawn point.");
    double spx = -sp[0] * M_S, spy = sp[1] * M_S, spz = sp[2] * M_S;

    // detail texture: the second name after the heights
    u32 q = 32 + n * n * 2;
    char detail[257];
    memcpy(detail, t + q + 256, 256);
    detail[256] = 0;
    norm_path(detail, 0);
    int has_detail = detail[0] && have(detail);
    u32 dgbix = has_detail ? M_DETAIL_GBIX0 + (u32)k : 0;

    char pack[32], up[16];
    sprintf(up, "%s", id);
    for (char *s = up; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
    sprintf(pack, "SXMAP_%s", up);

    struct out o = { 0, 0, 0 };
    put(&o, "SXMP", 4);
    put32(&o, 1); put32(&o, M_NB); put32(&o, M_NP); put32(&o, 16); put32(&o, gbix0);
    putf(&o, spx); putf(&o, spy); putf(&o, spz); putf(&o, sx * M_STEP * M_S);
    put_name16(&o, pack);
    put32(&o, dgbix); put32(&o, 0);
    static double pts[M_NP * M_NP][3], nrm[M_NP * M_NP][3];
    for (int bz = 0; bz < M_NB; bz++)
        for (int bx = 0; bx < M_NB; bx++) {
            for (int i = 0; i < M_NP; i++)
                for (int j = 0; j < M_NP; j++) {
                    int r = (bz * M_CELLS + i) * M_STEP, col = (bx * M_CELLS + j) * M_STEP;
                    double *p = pts[i * M_NP + j], *nn = nrm[i * M_NP + j];
                    p[0] = -(x0 + col * sx) * M_S;
                    p[1] = (y0 + H(r, col) * sy) * M_S;
                    p[2] = (z0 + r * sz) * M_S;
                    int r0 = r - M_STEP > 0 ? r - M_STEP : 0, r1 = r + M_STEP < (int)n - 1 ? r + M_STEP : (int)n - 1;
                    int c0 = col - M_STEP > 0 ? col - M_STEP : 0, c1 = col + M_STEP < (int)n - 1 ? col + M_STEP : (int)n - 1;
                    double dx = (H(r, c1) - H(r, c0)) * sy / ((c1 - c0) * sx);
                    double dz = (H(r1, col) - H(r0, col)) * sy / ((r1 - r0) * sz);
                    double l = root(dx * dx + 1 + dz * dz);
                    nn[0] = dx / l; nn[1] = 1 / l; nn[2] = -dz / l;
                }
            double lo[3], hi[3], ctr[3], rad = 0;
            for (int a = 0; a < 3; a++) {
                lo[a] = hi[a] = pts[0][a];
                for (int v = 1; v < M_NP * M_NP; v++) {
                    if (pts[v][a] < lo[a]) lo[a] = pts[v][a];
                    if (pts[v][a] > hi[a]) hi[a] = pts[v][a];
                }
                ctr[a] = (lo[a] + hi[a]) / 2;
            }
            for (int v = 0; v < M_NP * M_NP; v++) {
                double s = 0;
                for (int a = 0; a < 3; a++) s += (pts[v][a] - ctr[a]) * (pts[v][a] - ctr[a]);
                double d = root(s);
                if (!v || d > rad) rad = d;
            }
            put32(&o, (u32)((bz / 4) * 4 + bx / 4));
            putf(&o, ctr[0]); putf(&o, ctr[1]); putf(&o, ctr[2]); putf(&o, rad);
            for (int v = 0; v < M_NP * M_NP; v++) for (int a = 0; a < 3; a++) putf(&o, pts[v][a]);
            for (int v = 0; v < M_NP * M_NP; v++) for (int a = 0; a < 3; a++) putf(&o, nrm[v][a]);
        }
#undef H
    char path[500], dir[400], f[300], an[300];
    path_join(dir, mod_dir, "maps");
    _mkdir(dir);
    sprintf(path, "%s\\%s.bin", dir, id);
    u32 size = o.n;
    out_file(path, &o);

    pack_dir(dir, pack);
    struct out idx = { 0, 0, 0 };
    for (int i = 0; i < 16; i++) {
        sprintf(an, "colormap\\%s%02d.tga", id, i);
        sprintf(f, "%s%02d.png", id, i);
        pack_png(dir, f, an, 255);
        index_line(&idx, gbix0 + (u32)i, f);
    }
    if (has_detail) {
        png_name(f, detail);
        pack_png(dir, f, detail, 255);
        index_line(&idx, dgbix, f);
    }
    path_join(path, dir, "index.txt");
    out_file(path, &idx);
    printf("  %s.bin: %u bytes; start \"%s\" at %.0f %.0f %.0f; 16 ground pictures%s\n", id, size, spname,
           spx, spy, spz, has_detail ? " + detail" : "");
}

// ---------------------------------------------------------------- skins and bones

struct mvert { double p[3], n[3]; u32 color; double u, v; };
struct mmesh { char shader[96]; char bone[96]; int nv, nt; struct mvert *v; u16 *t; };
struct mlod { int n; struct mmesh *m; };
struct skin { int nlod; struct mlod *lod; };

// skin_prelit(): every level's MeshRigidPrelit meshes.
static int skin_prelit(const u8 *d, u32 len, struct skin *sk)
{
    const u8 *m = findb(d, len, ".model", 6);
    if (!m) return 0;
    u32 p = (u32)(m - d) + 6;
    const char *shaders[256];
    int nsh = 0;
    for (int lst = 0; lst < 2; lst++) {
        if (p + 2 > len) return 0;
        u32 cnt = rd16(d + p); p += 2;
        for (u32 i = 0; i < cnt; i++) {
            if (p >= len) return 0;
            u32 ln = d[p];
            if (lst == 1 && nsh < 256) {
                char *s = xalloc(ln + 1);
                memcpy(s, d + p + 1, ln); s[ln] = 0;
                shaders[nsh++] = s;
            }
            p += 1 + ln;
        }
    }
    if (p + 8 > len || d[p] != 0xE3 || d[p + 1] != 0xEF) return 0;
    u32 nlod = rd16(d + p + 6);
    p += 8;
    sk->nlod = (int)nlod;
    sk->lod = xalloc((nlod ? nlod : 1) * sizeof *sk->lod);
    for (u32 l = 0; l < nlod; l++) {
        if (p + 2 > len) return 0;
        u32 cnt = rd16(d + p); p += 2;
        struct mlod *lo = &sk->lod[l];
        lo->n = (int)cnt;
        lo->m = xalloc((cnt ? cnt : 1) * sizeof *lo->m);
        for (u32 i = 0; i < cnt; i++) {
            struct mmesh *me = &lo->m[i];
            if (p + 1 > len) return 0;
            u32 tn = d[p];
            if (p + 1 + tn > len || tn != 15 || memcmp(d + p + 1, "MeshRigidPrelit", 15)) return 0;
            p += 1 + tn;
            if (p + 7 > len) return 0;
            u32 sh = rd16(d + p), nv = rd16(d + p + 2), nt = rd16(d + p + 4);
            p += 6;
            u32 bl = d[p];
            if (sh >= (u32)nsh || p + 1 + bl > len) return 0;
            memcpy(me->shader, shaders[sh], strlen(shaders[sh]) < 95 ? strlen(shaders[sh]) + 1 : 95);
            me->shader[95] = 0;
            u32 bl2 = bl < 95 ? bl : 95;
            memcpy(me->bone, d + p + 1, bl2); me->bone[bl2] = 0;
            p += 1 + bl;
            if (p + nv * 36 + nt * 6 + 24 > len) return 0;
            me->nv = (int)nv; me->nt = (int)nt;
            me->v = xalloc((nv ? nv : 1) * sizeof *me->v);
            me->t = xalloc((nt ? nt : 1) * 6);
            for (u32 v = 0; v < nv; v++, p += 36) {
                struct mvert *x = &me->v[v];
                for (int a = 0; a < 3; a++) { x->p[a] = rdf(d + p + 4 * a); x->n[a] = rdf(d + p + 12 + 4 * a); }
                x->color = rd32(d + p + 24);
                x->u = rdf(d + p + 28); x->v = rdf(d + p + 32);
            }
            for (u32 t = 0; t < nt * 3; t++, p += 2) me->t[t] = (u16)rd16(d + p);
            p += 24;
        }
    }
    if (p + 2 <= len && d[p] == 0xE7 && d[p + 1] == 0xEF) {
        if (p + 8 > len) return 0;
        p += 8 + 4 * rd16(d + p + 6);
    }
    if (p + 6 != len || d[p] != 0xE0 || d[p + 1] != 0xEF || rd32(d + p + 2)) return 0;
    for (int i = 0; i < nsh; i++) free((void *)shaders[i]);
    return 1;
}

static void skin_free(struct skin *sk)
{
    for (int l = 0; l < sk->nlod; l++) {
        for (int i = 0; i < sk->lod[l].n; i++) { free(sk->lod[l].m[i].v); free(sk->lod[l].m[i].t); }
        free(sk->lod[l].m);
    }
    free(sk->lod);
    sk->nlod = 0; sk->lod = 0;
}

// model_bones(): name -> 9 floats (position, angles, scale); later names win.
struct mbone { char name[96]; double v[9]; };
struct mbones { int n; struct mbone b[64]; };

static void mbones_read(const u8 *d, u32 len, struct mbones *bs)
{
    bs->n = 0;
    const u8 *m = findb(d, len, "BoneSkeleton", 12);
    if (!m) return;
    u32 p = (u32)(m - d) + 12;
    if (p + 6 > len) return;
    u32 cnt = rd16(d + p); p += 6;
    for (u32 i = 0; i < cnt; i++) {
        if (p >= len) return;
        p += 1 + d[p];
        if (p >= len) return;
        u32 nl = d[p];
        char name[96];
        u32 l2 = nl < 95 ? nl : 95;
        if (p + 1 + nl + 2 + 40 > len) return;
        memcpy(name, d + p + 1, l2); name[l2] = 0;
        p += 1 + nl + 2;
        struct mbone *b = 0;
        for (int k = 0; k < bs->n; k++) if (!strcmp(bs->b[k].name, name)) b = &bs->b[k];
        if (!b && bs->n < 64) b = &bs->b[bs->n++];
        if (b) {
            memcpy(b->name, name, l2 + 1);
            for (int a = 0; a < 9; a++) b->v[a] = rdf(d + p + 4 * a);
        }
        p += 40;
    }
}

static const double *mbone(const struct mbones *bs, const char *name)
{
    for (int k = 0; k < bs->n; k++) if (!strcmp(bs->b[k].name, name)) return bs->b[k].v;
    return 0;
}

static void bone_place(const double *v, const double *b, double *o)
{
    if (!b || (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 && b[6] == 1 && b[7] == 1 && b[8] == 1)) {
        memcpy(o, v, 3 * sizeof *o);
        return;
    }
    m3 M;
    turn_yxz(b[3], b[4], b[5], M);
    double s[3] = { v[0] * b[6], v[1] * b[7], v[2] * b[8] }, r[3];
    for (int i = 0; i < 3; i++) {
        double t = 0;
        for (int k = 0; k < 3; k++) t += M[i][k] * s[k];
        r[i] = t + b[i];
    }
    memcpy(o, r, sizeof r);
}

static void bone_turn(const double *n, const double *b, double *o)
{
    if (!b || (b[3] == 0 && b[4] == 0 && b[5] == 0)) { memcpy(o, n, 3 * sizeof *o); return; }
    m3 M;
    turn_yxz(b[3], b[4], b[5], M);
    double r[3];
    for (int i = 0; i < 3; i++) {
        double t = 0;
        for (int k = 0; k < 3; k++) t += M[i][k] * n[k];
        r[i] = t;
    }
    memcpy(o, r, sizeof r);
}

// A file's model bones; none when the file isn't there (Python: KeyError -> {}).
static void bones_of(const char *name, struct mbones *bs)
{
    bs->n = 0;
    if (!have(name)) return;
    u32 n;
    u8 *d = arc_get(name, &n);
    mbones_read(d, n, bs);
    free(d);
}

// "objects/a/b.skin" -> "objects\a\b<suffix>" with ".skin" swapped for `ext`.
static void swap_ext(char *out, const char *path, const char *ext)
{
    u32 l = strlen(path);
    memcpy(out, path, l + 1);
    for (char *s = out; *s; s++) if (*s == '/') *s = '\\';
    if (l > 5 && !strcmp(out + l - 5, ".skin")) memcpy(out + l - 5, ext, strlen(ext) + 1);
}

// ---------------------------------------------------------------- grass()

// The texture list a map's objects use (in texture-number order).
struct texlist { int n; char name[M_OBJ_TEX_MAX + 1][160]; };
static int tex_add(struct texlist *tl, const char *t)
{
    for (int i = 0; i < tl->n; i++) if (!strcmp(tl->name[i], t)) return i;
    if (tl->n > M_OBJ_TEX_MAX) fail("a Cabela's map uses more textures than the mod has room for.");
    memcpy(tl->name[tl->n], t, strlen(t) + 1);
    return tl->n++;
}

struct grass_ctx { const char *want; char tex[160]; int found; };
static void grass_fn(const char *nm, u32 nl, const char *b, u32 bl, void *vctx)
{
    struct grass_ctx *g = vctx;
    char name[96];
    u32 l = nl < 95 ? nl : 95;
    memcpy(name, nm, l); name[l] = 0;
    lower(name);
    if (strcmp(name, g->want)) return;
    char tx[160];
    if (!body_texture(b, bl, 0, 1, 0, tx, sizeof tx)) return;
    norm_path(tx, 0);
    memcpy(g->tex, tx, strlen(tx) + 1);
    g->found = 1;                                    // the last match wins, as in the Python's loop
}

// -1 = no grass file (and an old one removed).
static int grass(int k, const struct chunks *c, struct texlist *tl)
{
    char path[500];
    sprintf(path, "%s\\maps\\%s_grass.bin", mod_dir, map_ids[k]);
    remove(path);
    const u8 *d = c->p[34];
    if (!d) return -1;
    if (c->n[34] < 532) fail("a Cabela's map's grass part is cut short.");
    char sh[257], mask[257];
    memcpy(sh, d + 4, 256); sh[256] = 0; lower(sh);
    memcpy(mask, d + 260, 256); mask[256] = 0;
    double edge = rdf(d + 516), w = rdf(d + 524), h = rdf(d + 528);
    int parts = (int)rd32(d + 520);
    u32 n;
    char *t = (char *)arc_get("shaders\\grass_shaders.script", &n);
    struct grass_ctx g = { sh, "", 0 };
    find_functions(t, n, 0, grass_fn, &g);
    free(t);
    for (char *s = mask; *s; s++) if (*s == '/') *s = '\\';
    if (!have(mask)) return -1;
    u8 *md = arc_get(mask, &n);
    if (n < 10) fail("a Cabela's grass mask is cut short.");
    u32 mw = rd32(md + 2), mh = rd32(md + 6), nb = mw * mh / 8;
    if (10 + nb > n) fail("a Cabela's grass mask is cut short.");
    int any = 0;
    for (u32 i = 0; i < nb && !any; i++) any = md[10 + i] != 0;
    if (!g.found || !any) { free(md); return -1; }
    const u8 *t33 = c->p[33];
    u32 gn = rd32(t33);
    double x0 = rdf(t33 + 8), z0 = rdf(t33 + 16), sx = rdf(t33 + 20), sz = rdf(t33 + 28);
    parts &= ~7;
    // texs.append(tex): always added, even when a mesh has the same picture
    if (tl->n > M_OBJ_TEX_MAX) fail("a Cabela's map uses more textures than the mod has room for.");
    memcpy(tl->name[tl->n], g.tex, strlen(g.tex) + 1);
    int ti = tl->n++;
    struct out o = { 0, 0, 0 };
    put(&o, "SXGR", 4);
    put32(&o, 1);
    putf(&o, edge / parts * 100 * M_S);
    put32(&o, 0);
    putf(&o, w * 100 * M_S); putf(&o, h * 100 * M_S); putf(&o, edge / 2 * 100 * M_S);
    put32(&o, (u32)ti); put32(&o, mw); put32(&o, mh);
    putf(&o, -x0 * M_S);
    putf(&o, -(double)(gn - 1) * sx / mw * M_S);
    putf(&o, z0 * M_S);
    putf(&o, (double)(gn - 1) * sz / mh * M_S);
    put(&o, md + 10, nb);
    free(md);
    out_file(path, &o);
    return ti;
}

// ---------------------------------------------------------------- objects()

struct mplace { u32 model; double pos[3], ang[3], sc[3], box[6]; };

static int cmp_u32(const void *a, const void *b) { u32 x = *(const u32 *)a, y = *(const u32 *)b; return x < y ? -1 : x > y; }

static void brushes(int k, const struct chunks *c, char (*paths)[257], const struct mplace *pl, u32 ni);

static void objects(int k, const struct chunks *c)
{
    const char *id = map_ids[k];
    u32 gbix0 = M_OBJ_GBIX0 + M_OBJ_TEX_MAX * (u32)k;
    char pack[32], up[16];
    sprintf(up, "%s", id);
    for (char *s = up; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
    sprintf(pack, "SXOBJ_%s", up);
    const u8 *d = c->p[41];
    u32 dn = c->n[41];
    if (!d || dn < 8) fail("a Cabela's map has no object list (chunk 41).");
    u32 nn = rd32(d);
    if (nn > 256 || 4 + nn * 256 + 4 > dn) fail("a Cabela's map's object list isn't the shape setup.exe knows.");
    char (*paths)[257] = xalloc(nn ? nn * 257 : 1);
    for (u32 i = 0; i < nn; i++) { memcpy(paths[i], d + 4 + i * 256, 256); paths[i][256] = 0; lower(paths[i]); }
    u32 p = 4 + nn * 256, ni = rd32(d + p);
    p += 4;
    if (p + ni * 80 > dn) fail("a Cabela's map's object list is cut short.");
    struct mplace *pl = xalloc(ni ? ni * sizeof *pl : 1);
    for (u32 j = 0; j < ni; j++) {
        const u8 *r = d + p + j * 80;
        pl[j].model = rd32(r) & 255;
        for (int a = 0; a < 3; a++) { pl[j].pos[a] = rdf(r + 4 + 4 * a); pl[j].ang[a] = rdf(r + 16 + 4 * a); pl[j].sc[a] = rdf(r + 28 + 4 * a); }
        for (int a = 0; a < 6; a++) pl[j].box[a] = rdf(r + 52 + 4 * a);
    }
    // keep: Cabela's model number -> ours (first appearance of each path)
    int keep[256], order[256], norder = 0;
    for (u32 i = 0; i < nn; i++) {
        int f = -1;
        for (int j = 0; j < norder; j++) if (!strcmp(paths[order[j]], paths[i])) f = j;
        if (f < 0) { f = norder; order[norder++] = (int)i; }
        keep[i] = f;
    }
    struct texlist *tl = xalloc(sizeof *tl);
    tl->n = 0;
    struct out models = { 0, 0, 0 };
    int skipped = 0;
    char name[300];
    for (int oi = 0; oi < norder; oi++) {
        const char *path = paths[order[oi]];
        struct skin sk = { 0, 0 }, col = { 0, 0 };
        swap_ext(name, path, ".skin");
        u32 n;
        u8 *raw = arc_get(name, &n);
        if (!skin_prelit(raw, n, &sk)) { printf("  %s\n", name); fail("a Cabela's model isn't the shape setup.exe reads."); }
        free(raw);
        struct mbones bones, cbones;
        swap_ext(name, path, ".model");
        bones_of(name, &bones);
        for (int l = 0; l < sk.nlod; l++)
            for (int i = 0; i < sk.lod[l].n; i++) {
                struct mmesh *me = &sk.lod[l].m[i];
                const double *b = mbone(&bones, me->bone);
                for (int v = 0; v < me->nv; v++) {
                    bone_place(me->v[v].p, b, me->v[v].p);
                    bone_turn(me->v[v].n, b, me->v[v].n);
                }
            }
        int plant = strstr_(path, "/rocks/") || strstr_(path, "/veget/");
        int has_col = 0;
        if (plant) {
            swap_ext(name, path, "_col.skin");
            if (have(name)) {
                raw = arc_get(name, &n);
                if (!skin_prelit(raw, n, &col)) { printf("  %s\n", name); fail("a Cabela's collision model isn't the shape setup.exe reads."); }
                free(raw);
                swap_ext(name, path, "_col.model");
                bones_of(name, &cbones);
                for (int l = 0; l < col.nlod; l++)
                    for (int i = 0; i < col.lod[l].n; i++) {
                        struct mmesh *me = &col.lod[l].m[i];
                        const double *b = mbone(&cbones, me->bone);
                        for (int v = 0; v < me->nv; v++) bone_place(me->v[v].p, b, me->v[v].p);
                    }
                has_col = col.nlod > 0;
            }
        }
        int water = !strcmp(path, "objects/water/water.skin");
        // the meshes that are drawn
        struct out body = { 0, 0, 0 };
        double rad = 0;
        int nme = 0;
        struct mlod *l0 = sk.nlod ? &sk.lod[0] : 0;
        for (int i = 0; l0 && i < l0->n; i++) {
            struct mmesh *me = &l0->m[i];
            const struct shtex *st = 0;
            char shl[96];
            memcpy(shl, me->shader, sizeof shl); lower(shl);
            if (water) { if (strcmp(shl, "water") || !me->nt) { skipped++; continue; } }
            else { st = shader(me->shader); if (!st || !me->nt) { skipped++; continue; } }
            nme++;
            int ti = st ? tex_add(tl, st->tex) : 0;
            put32(&body, (u32)ti); put32(&body, water ? 1 : (u32)st->alpha);
            put32(&body, (u32)me->nv); put32(&body, (u32)me->nt);
            for (int v = 0; v < me->nv; v++) { putf(&body, -me->v[v].p[0] * 10); putf(&body, me->v[v].p[1] * 10); putf(&body, me->v[v].p[2] * 10); }
            for (int v = 0; v < me->nv; v++) { putf(&body, -me->v[v].n[0]); putf(&body, me->v[v].n[1]); putf(&body, me->v[v].n[2]); }
            for (int v = 0; v < me->nv; v++) {
                const double *x = me->v[v].p;
                double r = root(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) * 10;
                if (r > rad) rad = r;
            }
            for (int t = 0; t < me->nt; t++) { put16(&body, me->t[t * 3]); put16(&body, me->t[t * 3 + 2]); put16(&body, me->t[t * 3 + 1]); }
            pad4(&body);
            for (int t = 0; t < me->nt; t++) {
                const int cs[3] = { me->t[t * 3], me->t[t * 3 + 2], me->t[t * 3 + 1] };
                for (int cc = 0; cc < 3; cc++) {
                    int u = rnd(me->v[cs[cc]].u * 255), vv = rnd(me->v[cs[cc]].v * 255);
                    u = u < -32768 ? -32768 : u > 32767 ? 32767 : u;
                    vv = vv < -32768 ? -32768 : vv > 32767 ? 32767 : vv;
                    put16(&body, (u32)u & 0xFFFF); put16(&body, (u32)vv & 0xFFFF);
                }
            }
        }
        // collision
        struct out cb = { 0, 0, 0 };
        u32 ncp = 0, nct = 0;
        if (has_col)
            for (int i = 0; i < col.lod[0].n; i++) { ncp += (u32)col.lod[0].m[i].nv; nct += (u32)col.lod[0].m[i].nt; }
        put32(&cb, ncp); put32(&cb, nct);
        if (has_col) {
            for (int i = 0; i < col.lod[0].n; i++)
                for (int v = 0; v < col.lod[0].m[i].nv; v++) {
                    const double *x = col.lod[0].m[i].v[v].p;
                    putf(&cb, -x[0] * 10); putf(&cb, x[1] * 10); putf(&cb, x[2] * 10);
                }
            u32 base = 0;
            for (int i = 0; i < col.lod[0].n; i++) {
                struct mmesh *me = &col.lod[0].m[i];
                for (int t = 0; t < me->nt; t++) {
                    u32 a = base + me->t[t * 3], b = base + me->t[t * 3 + 1], cc = base + me->t[t * 3 + 2];
                    if (a > 65535 || b > 65535 || cc > 65535) fail("a Cabela's collision model is too big.");
                    put16(&cb, a); put16(&cb, cc); put16(&cb, b);
                }
                base += (u32)me->nv;
            }
            pad4(&cb);
        }
        u32 flags = (has_col || !plant ? 1u << 0 : 0) | (water ? 1u << 1 : 0);
        put32(&models, flags); putf(&models, rad); put32(&models, (u32)nme);
        put(&models, body.b, body.n);
        put(&models, cb.b, cb.n);
        free(body.b); free(cb.b);
        skin_free(&sk); skin_free(&col);
    }
    int grass_tex = grass(k, c, tl);
    if (tl->n > M_OBJ_TEX_MAX) fail("a Cabela's map uses more textures than the mod has room for.");
    // placements
    struct out places = { 0, 0, 0 };
    u32 count = 0;
    m3 F = { { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    for (u32 j = 0; j < ni; j++) {
        if (pl[j].model >= nn) continue;
        m3 M, A;
        turn_yxz(pl[j].ang[0], pl[j].ang[1], pl[j].ang[2], M);
        for (int i = 0; i < 3; i++) for (int jj = 0; jj < 3; jj++) M[i][jj] = M[i][jj] * pl[j].sc[jj];
        mul3(F, M, A);
        mul3(A, F, A);
        put32(&places, (u32)keep[pl[j].model]);
        for (int cc = 0; cc < 3; cc++) for (int i = 0; i < 3; i++) putf(&places, A[i][cc]);
        putf(&places, -pl[j].pos[0] * 0.1); putf(&places, pl[j].pos[1] * 0.1); putf(&places, pl[j].pos[2] * 0.1);
        count++;
    }
    struct out o = { 0, 0, 0 };
    put(&o, "SXOB", 4);
    put32(&o, 1); put32(&o, (u32)norder); put32(&o, count); put32(&o, (u32)tl->n); put32(&o, gbix0);
    put_name16(&o, pack);
    put(&o, models.b, models.n);
    put(&o, places.b, places.n);
    free(models.b); free(places.b);
    char path2[500], dir[400], f[300];
    sprintf(path2, "%s\\maps\\%s_obj.bin", mod_dir, id);
    u32 size = o.n;
    out_file(path2, &o);
    pack_dir(dir, pack);
    struct out idx = { 0, 0, 0 };
    for (int i = 0; i < tl->n; i++) {
        png_name(f, tl->name[i]);
        pack_png(dir, f, tl->name[i], -1);
        index_line(&idx, gbix0 + (u32)i, f);
    }
    path_join(path2, dir, "index.txt");
    out_file(path2, &idx);
    printf("  %s_obj.bin: %u bytes, %d models, %u placements, %d textures%s, %d meshes without a texture left out\n",
           id, size, norder, count, tl->n, grass_tex >= 0 ? " (with the grass)" : "", skipped);
    free(tl);
    brushes(k, c, paths, pl, ni);
    free(paths);
    free(pl);
}

// ---------------------------------------------------------------- brushes()

struct dvec { double *v; u32 n, cap; };          // growable list of doubles
static void dpush(struct dvec *a, double x)
{
    if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 4096; a->v = realloc(a->v, a->cap * sizeof *a->v); if (!a->v) fail("out of memory."); }
    a->v[a->n++] = x;
}
struct uvec { u32 *v; u32 n, cap; };
static void upush(struct uvec *a, u32 x)
{
    if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 4096; a->v = realloc(a->v, a->cap * sizeof *a->v); if (!a->v) fail("out of memory."); }
    a->v[a->n++] = x;
}

#define BF_MAX 512                                 // corners of one face while cutting
// brush_faces(): one side's face; the corners into poly (count returned, 0 = none).
static int brush_face(const float *planes, const u32 *sides, u32 ns, u32 si, double (*poly)[3])
{
    static double tmp[BF_MAX][3];
    const double eps = 0.01, B = 1e6;
    u32 i = sides[si];
    double n[3] = { planes[i * 5], planes[i * 5 + 1], planes[i * 5 + 2] }, dd = planes[i * 5 + 3];
    int a = 0;
    for (int k = 1; k < 3; k++) if (__builtin_fabs(n[k]) > __builtin_fabs(n[a])) a = k;
    double u[3] = { 0, 0, 0 };
    u[(a + 1) % 3] = 1.0;
    double dot = 0;
    for (int k = 0; k < 3; k++) dot += u[k] * n[k];
    for (int k = 0; k < 3; k++) u[k] = u[k] - n[k] * dot;
    double l = 0;
    for (int k = 0; k < 3; k++) l += u[k] * u[k];
    l = root(l);
    for (int k = 0; k < 3; k++) u[k] = u[k] / l;
    double v[3] = { n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0] };
    double o[3] = { n[0] * dd, n[1] * dd, n[2] * dd };
    static const int sw[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    int np = 4;
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 3; k++) poly[c][k] = o[k] + (sw[c][0] * u[k] + sw[c][1] * v[k]) * B;
    for (u32 sj = 0; sj < ns; sj++) {
        u32 j = sides[sj];
        if (j == i) continue;
        double m[3] = { planes[j * 5], planes[j * 5 + 1], planes[j * 5 + 2] }, e = planes[j * 5 + 3];
        int nn = 0;
        for (int q = 0; q < np; q++) {
            const double *P = poly[q], *Q = poly[(q + 1) % np];
            double dp = 0, dq = 0;
            for (int k = 0; k < 3; k++) dp += m[k] * P[k];
            dp -= e;
            for (int k = 0; k < 3; k++) dq += m[k] * Q[k];
            dq -= e;
            if (dp <= eps) { if (nn >= BF_MAX) fail("a Cabela's brush has too many corners."); memcpy(tmp[nn++], P, sizeof tmp[0]); }
            if ((dp < -eps && dq > eps) || (dp > eps && dq < -eps)) {
                double tt = dp / (dp - dq);
                if (nn >= BF_MAX) fail("a Cabela's brush has too many corners.");
                for (int k = 0; k < 3; k++) tmp[nn][k] = P[k] + (Q[k] - P[k]) * tt;
                nn++;
            }
        }
        memcpy(poly, tmp, (u32)nn * sizeof tmp[0]);
        np = nn;
        if (np < 3) break;
    }
    return np >= 3 ? np : 0;
}

// water_env(): 1 + the place of the map's reflection picture in WATER_ENVS.
static const char *const water_envs[6] = { "waterenv", "waterenvn", "waterenvnw", "waterenvr", "waterenvw", "waterenvy" };
static u32 water_env(const char *id)
{
    u32 n;
    char *t = (char *)arc_get("shaders\\cars\\cars.script", &n);
    char want[64], *o = want;
    o += sprintf(o, "/perturbance/");
    for (const char *s = id; *s; s++) if (*s != '_') *o++ = *s;
    sprintf(o, "/");
    u32 res = 0;
    const char *end = t + n;
    for (const char *ln = t; ln < end && !res;) {
        const char *le = ln;
        while (le < end && *le != '\n' && *le != '\r') le++;
        if (at(ln, le, "envBumpWaterTable[", 0)) {
            int has = 0;
            for (const char *p = ln; p < le && !has; p++) has = at(p, le, want, 1);
            if (has) {
                // the 2nd quoted string
                const char *q = ln;
                int quotes = 0;
                while (q < le && quotes < 3) if (*q++ == '"') quotes++;
                const char *s = q;
                while (q < le && *q != '"') q++;
                char env[200];
                u32 l = (u32)(q - s) < 199 ? (u32)(q - s) : 199;
                memcpy(env, s, l); env[l] = 0;
                {   // replace('\\\\', '\\') then lower(): no / change here
                    char *w = env;
                    for (const char *r = env; *r; r++) { if (r[0] == '\\' && r[1] == '\\') r++; *w++ = *r; }
                    *w = 0;
                    lower(env);
                }
                char *b = strrchr(env, '\\');
                b = b ? b + 1 : env;
                char *dot = b;
                for (char *x = b; *x; x++) if (!memcmp(x, ".tga", 4)) { dot = x; break; } else dot = x + 1;
                *dot = 0;
                for (int i = 0; i < 6; i++) if (!strcmp(b, water_envs[i])) res = (u32)i + 1;
                if (!res) fail("a Cabela's water reflection picture setup.exe doesn't know.");
            }
        }
        ln = le;
        // Python splitlines(): \r\n is one break
        if (ln < end && *ln == '\r' && ln + 1 < end && ln[1] == '\n') ln += 2;
        else if (ln < end) ln++;
    }
    free(t);
    return res;
}

struct bkey { int kx, kz; u32 i; };
static int cmp_bkey(const void *a, const void *b)
{
    const struct bkey *x = a, *y = b;
    if (x->kx != y->kx) return x->kx < y->kx ? -1 : 1;
    if (x->kz != y->kz) return x->kz < y->kz ? -1 : 1;
    return x->i < y->i ? -1 : x->i > y->i;
}

// Python int(math.floor(x)).
static int ifloor(double x) { return (int)floor(x); }

static void piece_out(struct out *o, u32 flags, struct dvec *pts, struct dvec *nrm, struct uvec *tris)
{
    u32 np = pts->n / 3;
    if (np >= 65536) fail("a piece of a Cabela's map's collision is too big.");
    put32(o, flags); put32(o, np); put32(o, tris->n / 3);
    for (u32 i = 0; i < pts->n; i++) putf(o, pts->v[i]);
    for (u32 i = 0; i < nrm->n; i++) putf(o, nrm->v[i]);
    for (u32 i = 0; i < tris->n; i++) put16(o, tris->v[i]);
    pad4(o);
    pts->n = nrm->n = tris->n = 0;
}

static void brushes(int k, const struct chunks *c, char (*paths)[257], const struct mplace *pl, u32 ni)
{
    const char *id = map_ids[k];
    const u8 *p0 = c->p[0], *d = c->p[39];
    if (!p0 || !d) fail("a Cabela's map has no brushes (chunks 0 and 39).");
    u32 npl = rd32(p0);
    if (4 + npl * 20 > c->n[0]) fail("a Cabela's map's planes are cut short.");
    float *planes = xalloc(npl * 5 * sizeof(float) + 4);
    for (u32 i = 0; i < npl; i++) for (int a = 0; a < 5; a++) planes[i * 5 + a] = rdf(p0 + 4 + i * 20 + 4 * a);
    u32 n = rd32(d);
    if (4 + n * 40 > c->n[39]) fail("a Cabela's map's brushes are cut short.");
    // water squares
    struct dvec wp = { 0, 0, 0 }, wn = { 0, 0, 0 };
    struct bkey *wk = xalloc((ni ? ni : 1) * sizeof *wk);
    double (*wq)[4][3] = xalloc((ni ? ni : 1) * sizeof *wq);
    u32 nwater = 0;
    int *plant_of = xalloc((ni ? ni : 1) * sizeof *plant_of);
    for (u32 j = 0; j < ni; j++) {
        const char *pth = pl[j].model < 256 ? paths[pl[j].model] : "";
        plant_of[j] = strstr_(pth, "/rocks/") || strstr_(pth, "/veget/");
        if (strcmp(pth, "objects/water/water.skin")) continue;
        m3 M;
        turn_yxz(pl[j].ang[0], pl[j].ang[1], pl[j].ang[2], M);
        static const int cxz[4][2] = { { -16, -16 }, { 16, -16 }, { 16, 16 }, { -16, 16 } };
        double P[4][3];
        for (int cc = 0; cc < 4; cc++) {
            double v[3] = { cxz[cc][0] * pl[j].sc[0] * 100, 0.0, cxz[cc][1] * pl[j].sc[2] * 100 }, w[3];
            for (int i = 0; i < 3; i++) {
                double s = 0;
                for (int kk = 0; kk < 3; kk++) s += M[i][kk] * v[kk];
                w[i] = pl[j].pos[i] + s;
            }
            P[cc][0] = -w[0] * 0.1; P[cc][1] = w[1] * 0.1; P[cc][2] = w[2] * 0.1;
        }
        double mx = 0, mz = 0;
        for (int cc = 0; cc < 4; cc++) mx += P[cc][0];
        for (int cc = 0; cc < 4; cc++) mz += P[cc][2];
        mx /= 4; mz /= 4;
        double e1[3], e2[3];
        for (int a = 0; a < 3; a++) { e1[a] = P[1][a] - P[0][a]; e2[a] = P[2][a] - P[0][a]; }
        if (e1[2] * e2[0] - e1[0] * e2[2] < 0)
            for (int cc = 0; cc < 2; cc++) { double t[3]; memcpy(t, P[cc], sizeof t); memcpy(P[cc], P[3 - cc], sizeof t); memcpy(P[3 - cc], t, sizeof t); }
        memcpy(wq[nwater], P, sizeof P);
        wk[nwater].kx = ifloor(mx / BC_CELL); wk[nwater].kz = ifloor(mz / BC_CELL); wk[nwater].i = nwater;
        nwater++;
    }
    // brushes: which are used, and their cells
    u32 *side_at = xalloc((n ? n : 1) * sizeof *side_at);
    struct bkey *bk = xalloc((n ? n : 1) * sizeof *bk);
    u32 q = 4 + n * 40, used = 0, plants = 0, world = 0, nbk = 0;
    for (u32 i = 0; i < n; i++) {
        const u8 *b = d + 4 + i * 40;
        u32 flags = rd32(b), ns = rd32(b + 4);
        side_at[i] = q;
        q += 4 * ns;
        if (q > c->n[39]) fail("a Cabela's map's brushes are cut short.");
        if (flags & (1 << 10)) { world++; continue; }
        double mid[3];
        for (int a = 0; a < 3; a++) mid[a] = ((double)rdf(b + 16 + 4 * a) + rdf(b + 28 + 4 * a)) / 2;
        int inside = 0, all = 1;
        for (u32 j = 0; j < ni; j++) {
            const double *bx = pl[j].box;
            if (bx[0] - 50 <= mid[0] && mid[0] <= bx[3] + 50 && bx[1] - 50 <= mid[1] && mid[1] <= bx[4] + 50 &&
                bx[2] - 50 <= mid[2] && mid[2] <= bx[5] + 50) {
                inside = 1;
                if (!plant_of[j]) { all = 0; break; }
            }
        }
        if (inside && all) { plants++; continue; }
        used++;
        bk[nbk].kx = ifloor(-mid[0] * 0.1 / BC_CELL);
        bk[nbk].kz = ifloor(mid[2] * 0.1 / BC_CELL);
        bk[nbk].i = i;
        nbk++;
    }
    qsort(bk, nbk, sizeof *bk, cmp_bkey);
    qsort(wk, nwater, sizeof *wk, cmp_bkey);
    struct out body = { 0, 0, 0 };
    struct dvec pts = { 0, 0, 0 }, nrm = { 0, 0, 0 };
    struct uvec tris = { 0, 0, 0 };
    static double poly[BF_MAX][3];
    u32 ncells = 0, nwcells = 0;
    for (u32 g = 0; g < nbk;) {
        u32 ge = g;
        while (ge < nbk && bk[ge].kx == bk[g].kx && bk[ge].kz == bk[g].kz) ge++;
        for (u32 x = g; x < ge; x++) {
            u32 i = bk[x].i;
            u32 ns = rd32(d + 4 + i * 40 + 4);
            u32 *sides = xalloc(ns ? ns * 4 : 4);
            for (u32 s = 0; s < ns; s++) {
                sides[s] = rd32(d + side_at[i] + 4 * s);
                if (sides[s] >= npl) fail("a Cabela's brush names a plane that isn't there.");
            }
            for (u32 s = 0; s < ns; s++) {
                int np = brush_face(planes, sides, ns, s, poly);
                if (!np) continue;
                u32 pi = sides[s];
                double N[3] = { -(double)planes[pi * 5], planes[pi * 5 + 1], planes[pi * 5 + 2] };
                double P[BF_MAX][3];
                for (int v = 0; v < np; v++) { P[v][0] = -poly[v][0] * 0.1; P[v][1] = poly[v][1] * 0.1; P[v][2] = poly[v][2] * 0.1; }
                double sum = 0;
                for (int kk = 1; kk < np - 1; kk++) {
                    double e1[3], e2[3];
                    for (int a = 0; a < 3; a++) { e1[a] = P[kk][a] - P[0][a]; e2[a] = P[kk + 1][a] - P[0][a]; }
                    double cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
                    double t = 0;
                    for (int a = 0; a < 3; a++) t += cr[a] * N[a];
                    sum += t;
                }
                if (sum < 0)
                    for (int v = 0; v < np / 2; v++) { double t[3]; memcpy(t, P[v], sizeof t); memcpy(P[v], P[np - 1 - v], sizeof t); memcpy(P[np - 1 - v], t, sizeof t); }
                u32 base = pts.n / 3;
                for (int v = 0; v < np; v++) for (int a = 0; a < 3; a++) { dpush(&pts, P[v][a]); dpush(&nrm, N[a]); }
                for (int kk = 1; kk < np - 1; kk++) {
                    double e1[3], e2[3];
                    for (int a = 0; a < 3; a++) { e1[a] = P[kk][a] - P[0][a]; e2[a] = P[kk + 1][a] - P[0][a]; }
                    double cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
                    double t = 0;
                    for (int a = 0; a < 3; a++) t += cr[a] * N[a];
                    if (t > 0.01) { upush(&tris, base); upush(&tris, base + (u32)kk); upush(&tris, base + (u32)kk + 1); }
                }
            }
            free(sides);
        }
        piece_out(&body, 1u << 0, &pts, &nrm, &tris);
        ncells++;
        g = ge;
    }
    for (u32 g = 0; g < nwater;) {
        u32 ge = g;
        while (ge < nwater && wk[ge].kx == wk[g].kx && wk[ge].kz == wk[g].kz) ge++;
        for (u32 x = g; x < ge; x++) {
            u32 base = pts.n / 3;
            for (int cc = 0; cc < 4; cc++) for (int a = 0; a < 3; a++) { dpush(&pts, wq[wk[x].i][cc][a]); dpush(&nrm, a == 1 ? 1.0 : 0.0); }
            upush(&tris, base); upush(&tris, base + 1); upush(&tris, base + 2);
            upush(&tris, base); upush(&tris, base + 2); upush(&tris, base + 3);
        }
        piece_out(&body, 1u << 1, &pts, &nrm, &tris);
        nwcells++;
        g = ge;
    }
    struct out o = { 0, 0, 0 };
    put(&o, "SXCL", 4);
    put32(&o, 2); put32(&o, ncells + nwcells); put32(&o, water_env(id));
    put(&o, body.b, body.n);
    free(body.b); free(pts.v); free(nrm.v); free(tris.v); free(wp.v); free(wn.v);
    char path[500];
    sprintf(path, "%s\\maps\\%s_col.bin", mod_dir, id);
    u32 size = o.n;
    out_file(path, &o);
    printf("  %s_col.bin: %u bytes, %u brushes in %u pieces (%u left to rocks and plants, %u of the box around the map left out), %u water tiles in %u pieces\n",
           id, size, used, ncells, plants, world, nwater, nwcells);
    free(planes); free(wk); free(wq); free(plant_of); free(side_at); free(bk);
}

// ---------------------------------------------------------------- sky()

static void sky(int k, const struct chunks *c)
{
    const char *id = map_ids[k];
    const u8 *s = c->p[17];
    if (!s || c->n[17] != 6 * 11304) fail("a Cabela's map's sky isn't the shape setup.exe knows.");
    u32 gbix0 = M_SKY_GBIX0 + 8 * (u32)k;
    char pack[32], up[16];
    sprintf(up, "%s", id);
    for (char *x = up; *x; x++) if (*x >= 'a' && *x <= 'z') *x -= 32;
    sprintf(pack, "SXSKY_%s", up);
    char texs[8][160];
    int ntex = 0;
    struct out faces = { 0, 0, 0 };
    for (int f = 0; f < 6; f++) {
        const u8 *b = s + f * 11304;
        char sh[25];
        memcpy(sh, b, 24); sh[24] = 0;
        lower(sh);
        // script_textures(): every texture of the first function of that name, that exists
        char tx[2][160];
        int nt = 0;
        const struct sfn *fn = sfn_find(sh);
        for (int i = 0; fn && nt < 2; i++) {
            char t[160];
            if (!body_texture(fn->body, fn->len, i, 1, 1, t, sizeof t)) break;
            norm_path(t, 1);
            if (have(t)) memcpy(tx[nt++], t, strlen(t) + 1);
        }
        int clouds = nt >= 1, pic = -1;
        if (clouds && (!ntex || strcmp(texs[0], tx[0]))) {
            if (ntex) fail("a Cabela's sky has two cloud pictures.");
            memcpy(texs[ntex++], tx[0], strlen(tx[0]) + 1);
        }
        if (nt >= 2) {
            for (int i = 0; i < ntex; i++) if (!strcmp(texs[i], tx[1])) pic = i;
            if (pic < 0) { if (ntex >= 8) fail("a Cabela's sky has too many pictures."); pic = ntex; memcpy(texs[ntex++], tx[1], strlen(tx[1]) + 1); }
        }
        put32(&faces, (u32)pic); put32(&faces, clouds ? 1 : 0);
        for (int pt = 0; pt < 64; pt++)
            for (int v = 0; v < 4; v++) {
                const u8 *x = b + 40 + pt * 176 + v * 40;
                double X = rdf(x), Y = rdf(x + 4), Z = rdf(x + 8);
                double l = root(X * X + Y * Y + Z * Z);
                putf(&faces, -X / l); putf(&faces, Y / l); putf(&faces, Z / l);
                putf(&faces, rdf(x + 24)); putf(&faces, rdf(x + 28));
                putf(&faces, (double)rdf(x + 32) * 2); putf(&faces, (double)rdf(x + 36) * 2);
            }
    }
    char dir[400], path[500], f[300];
    pack_dir(dir, pack);
    struct out idx = { 0, 0, 0 };
    u32 rs = 0, gs = 0, bs = 0, nb = 0;            // at most 8 x 512 pixels x 255: fits
    for (int i = 0; i < ntex; i++) {
        struct img m = tga(texs[i]);
        if (i == 0) for (int p = 0; p < m.w * m.h; p++) m.p[p * 4 + 3] = 255;
        else {
            const u8 *last = m.p + (m.h - 1) * m.w * 4;
            for (int x = 0; x < m.w; x++) { rs += last[x * 4]; gs += last[x * 4 + 1]; bs += last[x * 4 + 2]; nb++; }
        }
        // <folder>_<file>.png
        char tmp[160];
        memcpy(tmp, texs[i], strlen(texs[i]) + 1);
        char *b = strrchr(tmp, '\\');
        char base[160];
        png_name(base, tmp);
        *b = 0;
        char *fo = strrchr(tmp, '\\');
        sprintf(f, "%s_%s", fo ? fo + 1 : tmp, base);
        path_join(path, dir, f);
        png_write(path, &m);
        free(m.p);
        index_line(&idx, gbix0 + (u32)i, f);
    }
    path_join(path, dir, "index.txt");
    out_file(path, &idx);
    u32 bottom = 0xFF000000u | (nb ? (u32)(rs / nb) << 16 | (u32)(gs / nb) << 8 | (u32)(bs / nb) : 0x808080u);
    // fog from the entity text
    double fs = 0, fe = 0;
    u32 fogc = 0;
    int got = 0;
    if (c->p[9]) {
        const char *e = (const char *)c->p[9], *ee = e + c->n[9];
        static const char *const keys[3] = { "fog color", "fog start", "fog end" };
        for (int kk = 0; kk < 3; kk++) {
            char pat[64];
            sprintf(pat, "property(\"%s\", \"", keys[kk]);
            for (const char *p = e; p < ee; p++) {
                if (!at(p, ee, pat, 0)) continue;
                const char *v = p + strlen(pat), *r = v;
                if (kk == 0) {
                    u32 parts[4];
                    for (int i = 0; i < 4; i++) parts[i] = (u32)rnd(num(r, &r));
                    fogc = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
                } else if (kk == 1) fs = num(v, 0);
                else fe = num(v, 0);
                got++;
                break;
            }
        }
    }
    if (got != 3) fail("a Cabela's map has no fog settings.");
    struct out o = { 0, 0, 0 };
    put(&o, "SXSK", 4);
    put32(&o, 2); put32(&o, gbix0);
    put_name16(&o, pack);
    put32(&o, bottom);
    put(&o, faces.b, faces.n);
    free(faces.b);
    putf(&o, fs * 10); putf(&o, fe * 10); put32(&o, fogc);
    sprintf(path, "%s\\maps\\%s_sky.bin", mod_dir, id);
    out_file(path, &o);
    printf("  %s_sky.bin: %d sky pictures, fog %u %u %u from %.0f to %.0f m\n", id, ntex,
           (fogc >> 16) & 255, (fogc >> 8) & 255, fogc & 255, fs, fe);
}

// ---------------------------------------------------------------- water_pack(), menu()

static void water_pack(void)
{
    char dir[400], path[500], f[64], an[200];
    pack_dir(dir, "SXWATER");
    struct out idx = { 0, 0, 0 };
    for (int i = 0; i < 32; i++) {
        sprintf(an, "textures\\water\\water_%02d.tga", i);
        sprintf(f, "water_%02d.png", i);
        pack_png(dir, f, an, -1);
        index_line(&idx, M_WATER_GBIX0 + (u32)i, f);
    }
    for (int i = 0; i < 6; i++) {
        sprintf(an, "textures\\water\\%s.tga", water_envs[i]);
        sprintf(f, "%s.png", water_envs[i]);
        pack_png(dir, f, an, 190);
        index_line(&idx, M_WATER_GBIX0 + 32 + (u32)i, f);
    }
    path_join(path, dir, "index.txt");
    out_file(path, &idx);
    printf("  SXWATER: 32 water frames, 6 reflection pictures\n");
}

static void menu(void)
{
    char dir[400], path[500], f[64], an[200];
    pack_dir(dir, "SXMAPMENU");
    struct out idx = { 0, 0, 0 };
    for (int k = 0; k < MAP_COUNT; k++) {
        sprintf(an, "colormap\\%s.tga", map_ids[k]);
        sprintf(f, "%s.png", map_ids[k]);
        pack_png(dir, f, an, 255);
        index_line(&idx, M_MENU_GBIX0 + (u32)k, f);
    }
    path_join(path, dir, "index.txt");
    out_file(path, &idx);
    printf("  SXMAPMENU: %d menu pictures\n", MAP_COUNT);
}

// ---------------------------------------------------------------- all of it

static void write_maps(void)
{
    scripts_load();
    shader_table();
    for (int k = 0; k < MAP_COUNT; k++) {
        printf(" %s\n", map_ids[k]);
        u32 n;
        u8 *b = bsp_get(map_ids[k], &n);
        struct chunks c;
        chunks_of(b, n, &c);
        ground(k, &c);
        objects(k, &c);
        sky(k, &c);
        free(b);
        maps_written++;
    }
    water_pack();
    menu();
}
