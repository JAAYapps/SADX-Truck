// Step 4: the texture pack textures\SADXTRUCK (90 PNG files + index.txt),
// the C copy of skin_to_c.py's image half (tga, picture, hud_images,
// shadow_image, gauge_images, glow).  Included by setup.c after
// trucks_bin.c.
//
// Same pixels as skin_to_c.py (Pillow 12.1) for all but the 14 picker
// pictures: resizes copy Pillow's Lanczos code (Resample.c: 22-bit fixed
// point weights, horizontal pass then vertical, on alpha-premultiplied
// pixels).  The pictures draw the truck the same way but write its name
// with Cabela's own font (FontLarge) instead of DejaVu Sans, which Windows
// doesn't have.

__declspec(dllimport) double sin(double);
__declspec(dllimport) double cos(double);
__declspec(dllimport) double floor(double);
__declspec(dllimport) double ceil(double);

#define TEX_COUNT 90
#define GBIX0 790001
#define TEX_GLASS_FIRST 53

struct img { int w, h; u8 *p; };                // RGBA, rows top to bottom

static struct img img_new(int w, int h)
{
    struct img m = { w, h, xalloc((u32)(w * h * 4)) };
    memset(m.p, 0, (u32)(w * h * 4));
    return m;
}

// ---------------------------------------------------------------- TGA
// Uncompressed only (type 2): 24-bit BGR or 32-bit BGRA, rows bottom to
// top unless descriptor (1 << 5) says top to bottom.

static struct img tga(const char *name)
{
    u32 n;
    u8 *d = arc_get(name, &n);
    if (n < 18 || d[2] != 2 || d[1] != 0 || (d[16] != 24 && d[16] != 32)) {
        printf("  %s\n", name);
        fail("a Cabela's texture is in a TGA layout setup.exe doesn't read.");
    }
    int w = (int)rd16(d + 12), h = (int)rd16(d + 14), bpp = d[16] / 8, top = d[17] & (1 << 5);
    u32 at = 18 + d[0];
    if (at + (u32)(w * h * bpp) > n) { printf("  %s\n", name); fail("a Cabela's texture is cut short."); }
    struct img m = img_new(w, h);
    for (int y = 0; y < h; y++) {
        const u8 *s = d + at + (u32)((top ? y : h - 1 - y) * w * bpp);
        u8 *o = m.p + y * w * 4;
        for (int x = 0; x < w; x++, s += bpp, o += 4) {
            o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = bpp == 4 ? s[3] : 255;
        }
    }
    free(d);
    return m;
}

static struct img crop(const struct img *a, int x0, int y0, int x1, int y1)
{
    struct img m = img_new(x1 - x0, y1 - y0);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (x >= 0 && y >= 0 && x < a->w && y < a->h)
                memcpy(m.p + ((y - y0) * m.w + (x - x0)) * 4, a->p + (y * a->w + x) * 4, 4);
    return m;
}

// transpose(ROTATE_90): a quarter turn to the left.
static struct img rotate90(const struct img *a)
{
    struct img m = img_new(a->h, a->w);
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++)
            memcpy(m.p + (y * m.w + x) * 4, a->p + (x * a->w + (a->w - 1 - y)) * 4, 4);
    return m;
}

static struct img mirror_lr(const struct img *a)
{
    struct img m = img_new(a->w, a->h);
    for (int y = 0; y < a->h; y++)
        for (int x = 0; x < a->w; x++)
            memcpy(m.p + (y * m.w + x) * 4, a->p + (y * a->w + (a->w - 1 - x)) * 4, 4);
    return m;
}

// ---------------------------------------------------------------- resize
// Pillow's Image.resize(size, LANCZOS) for RGBA: sinc(), lanczos(),
// coeffs(), clip8(), pass() and resize() are a C copy of Pillow's
// src/libImaging/Resample.c (precompute_coeffs, the 22-bit fixed point
// passes), changed to this file's picture type.  Pillow is under the
// MIT-CMU License: Copyright (c) 1997-2011 by Secret Labs AB, Copyright (c)
// 1995-2011 by Fredrik Lundh and contributors, Copyright (c) 2010 by
// Jeffrey 'Alex' Clark and contributors.  The full notice is in
// THIRD_PARTY_NOTICES.md.

#define PRECISION_BITS (32 - 8 - 2)
#define PI 3.14159265358979323846

static double sinc(double x)
{
    if (x == 0.0) return 1.0;
    x = x * PI;
    return sin(x) / x;
}

static double lanczos(double x)
{
    if (-3.0 <= x && x < 3.0) return sinc(x) * sinc(x / 3);
    return 0.0;
}

// precompute_coeffs + normalize_coeffs_8bpc; returns ksize.
static int coeffs(int in_size, int out_size, int **bounds_out, int **kk_out)
{
    double scale = (double)(float)((float)in_size - 0.0f) / out_size, filterscale = scale;
    if (filterscale < 1.0) filterscale = 1.0;
    double support = 3.0 * filterscale;
    int ksize = (int)ceil(support) * 2 + 1;
    int *bounds = xalloc((u32)(out_size * 2) * 4);
    double *kk = xalloc((u32)(out_size * ksize) * 8);
    int *ik = xalloc((u32)(out_size * ksize) * 4);
    for (int xx = 0; xx < out_size; xx++) {
        double center = 0.0 + (xx + 0.5) * scale, ww = 0.0, ss = 1.0 / filterscale;
        int xmin = (int)(center - support + 0.5);
        if (xmin < 0) xmin = 0;
        int xmax = (int)(center + support + 0.5);
        if (xmax > in_size) xmax = in_size;
        xmax -= xmin;
        double *k = &kk[xx * ksize];
        int x = 0;
        for (; x < xmax; x++) {
            double w = lanczos((x + xmin - center + 0.5) * ss);
            k[x] = w;
            ww += w;
        }
        for (x = 0; x < xmax; x++) if (ww != 0.0) k[x] /= ww;
        for (; x < ksize; x++) k[x] = 0;
        bounds[xx * 2] = xmin;
        bounds[xx * 2 + 1] = xmax;
    }
    for (int i = 0; i < out_size * ksize; i++)
        ik[i] = kk[i] < 0 ? (int)(-0.5 + kk[i] * (1 << PRECISION_BITS)) : (int)(0.5 + kk[i] * (1 << PRECISION_BITS));
    free(kk);
    *bounds_out = bounds;
    *kk_out = ik;
    return ksize;
}

static u8 clip8(int in)
{
    if (in >= (1 << PRECISION_BITS << 8)) return 255;
    if (in <= 0) return 0;
    return (u8)(in >> PRECISION_BITS);
}

static struct img pass(const struct img *a, int out_size, int horizontal)
{
    int *bounds, *kk;
    int ksize = coeffs(horizontal ? a->w : a->h, out_size, &bounds, &kk);
    struct img m = img_new(horizontal ? out_size : a->w, horizontal ? a->h : out_size);
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++) {
            int o = horizontal ? x : y, mn = bounds[o * 2], cnt = bounds[o * 2 + 1];
            const int *k = &kk[o * ksize];
            int ss[4] = { 1 << (PRECISION_BITS - 1), 1 << (PRECISION_BITS - 1), 1 << (PRECISION_BITS - 1), 1 << (PRECISION_BITS - 1) };
            for (int i = 0; i < cnt; i++) {
                const u8 *s = horizontal ? a->p + (y * a->w + mn + i) * 4 : a->p + ((mn + i) * a->w + x) * 4;
                for (int c = 0; c < 4; c++) ss[c] += s[c] * k[i];
            }
            u8 *d = m.p + (y * m.w + x) * 4;
            for (int c = 0; c < 4; c++) d[c] = clip8(ss[c]);
        }
    free(bounds);
    free(kk);
    return m;
}

static struct img resize(const struct img *a, int w, int h)
{
    // RGBA -> RGBa (premultiplied: Convert.c rgbA2rgba, MULDIV255)
    struct img pm = img_new(a->w, a->h);
    for (int i = 0; i < a->w * a->h; i++) {
        const u8 *s = a->p + i * 4;
        u8 *d = pm.p + i * 4;
        for (int c = 0; c < 3; c++) { u32 t = s[c] * s[3] + 128; d[c] = (u8)(((t >> 8) + t) >> 8); }
        d[3] = s[3];
    }
    struct img cur = pm;
    if (w != cur.w) { struct img t = pass(&cur, w, 1); free(cur.p); cur = t; }
    if (h != cur.h) { struct img t = pass(&cur, h, 0); free(cur.p); cur = t; }
    // RGBa -> RGBA (Convert.c rgba2rgbA)
    for (int i = 0; i < cur.w * cur.h; i++) {
        u8 *d = cur.p + i * 4;
        u32 al = d[3];
        if (al != 255 && al != 0)
            for (int c = 0; c < 3; c++) { u32 v = 255 * d[c] / al; d[c] = (u8)(v > 255 ? 255 : v); }
    }
    return cur;
}

// ---------------------------------------------------------------- PNG
// RGBA, 8 bits; each row's filter picked by the smallest sum of
// differences; deflate with fixed codes and 32 KB back references.

struct bits_out { struct out *o; u32 buf, n; };
static void wbits(struct bits_out *b, u32 v, int n)
{
    b->buf |= v << b->n;
    b->n += (u32)n;
    while (b->n >= 8) { u8 c = (u8)b->buf; put(b->o, &c, 1); b->buf >>= 8; b->n -= 8; }
}
static u32 rev(u32 v, int n) { u32 r = 0; while (n--) { r = (r << 1) | (v & 1); v >>= 1; } return r; }
static void wsym(struct bits_out *b, int s)
{
    if (s < 144) wbits(b, rev(0x30 + (u32)s, 8), 8);
    else if (s < 256) wbits(b, rev(0x190 + (u32)(s - 144), 9), 9);
    else if (s < 280) wbits(b, rev((u32)(s - 256), 7), 7);
    else wbits(b, rev(0xC0 + (u32)(s - 280), 8), 8);
}

static void deflate_fixed(struct out *o, const u8 *d, u32 n)
{
    static int head[1 << 15], prev[1 << 15];
    struct bits_out b = { o, 0, 0 };
    for (int i = 0; i < (1 << 15); i++) head[i] = -1;
    wbits(&b, 1, 1);                                     // last block
    wbits(&b, 1, 2);                                     // fixed codes
    u32 i = 0;
    while (i < n) {
        int best = 0, bdist = 0;
        if (i + 3 <= n) {
            u32 h = ((u32)d[i] << 10 ^ (u32)d[i + 1] << 5 ^ d[i + 2]) & 0x7FFF;
            int c = head[h], chain = 64;
            while (c >= 0 && i - (u32)c <= 32768 && chain--) {
                int l = 0, max = n - i < 258 ? (int)(n - i) : 258;
                while (l < max && d[c + l] == d[i + l]) l++;
                if (l > best) { best = l; bdist = (int)(i - (u32)c); if (l == 258) break; }
                c = prev[c & 0x7FFF];
            }
            prev[i & 0x7FFF] = head[h];
            head[h] = (int)i;
        }
        if (best >= 3) {
            int ls = 0;
            while (ls < 28 && lbase[ls + 1] <= best) ls++;
            wsym(&b, 257 + ls);
            wbits(&b, (u32)(best - lbase[ls]), lext[ls]);
            int ds = 0;
            while (ds < 29 && dbase[ds + 1] <= bdist) ds++;
            wbits(&b, rev((u32)ds, 5), 5);
            wbits(&b, (u32)(bdist - dbase[ds]), dext[ds]);
            for (int k = 1; k < best; k++) {                 // add the skipped places to the chains
                u32 j = i + (u32)k;
                if (j + 3 <= n) {
                    u32 h = ((u32)d[j] << 10 ^ (u32)d[j + 1] << 5 ^ d[j + 2]) & 0x7FFF;
                    prev[j & 0x7FFF] = head[h];
                    head[h] = (int)j;
                }
            }
            i += (u32)best;
        } else {
            wsym(&b, d[i]);
            i++;
        }
    }
    wsym(&b, 256);
    if (b.n) wbits(&b, 0, 8 - (int)b.n);
}

static void chunk(struct out *o, const char *type, const u8 *d, u32 n)
{
    u8 h[8] = { (u8)(n >> 24), (u8)(n >> 16), (u8)(n >> 8), (u8)n, (u8)type[0], (u8)type[1], (u8)type[2], (u8)type[3] };
    put(o, h, 8);
    u8 *c = xalloc(n + 4);
    memcpy(c, type, 4);
    memcpy(c + 4, d, n);
    u32 crc = crc32(c, n + 4);
    free(c);
    put(o, d, n);
    u8 t[4] = { (u8)(crc >> 24), (u8)(crc >> 16), (u8)(crc >> 8), (u8)crc };
    put(o, t, 4);
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static void png_write(const char *path, const struct img *m)
{
    u32 row = (u32)m->w * 4, raw_n = (row + 1) * (u32)m->h;
    u8 *raw = xalloc(raw_n), *tmp = xalloc(row);
    for (int y = 0; y < m->h; y++) {
        const u8 *cur = m->p + y * row, *up = y ? cur - row : 0;
        u8 *out = raw + (u32)y * (row + 1);
        u32 best_sum = 0xFFFFFFFFu;
        for (int f = 0; f < 5; f++) {
            u32 sum = 0;
            for (u32 x = 0; x < row; x++) {
                int a = x >= 4 ? cur[x - 4] : 0, b = up ? up[x] : 0, c = x >= 4 && up ? up[x - 4] : 0;
                int pr = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : paeth(a, b, c);
                u8 v = (u8)(cur[x] - pr);
                tmp[x] = v;
                sum += v < 128 ? v : 256 - v;
            }
            if (sum < best_sum) { best_sum = sum; out[0] = (u8)f; memcpy(out + 1, tmp, row); }
        }
    }
    struct out z = { 0, 0, 0 };
    u8 zh[2] = { 0x78, 0x01 };
    put(&z, zh, 2);
    deflate_fixed(&z, raw, raw_n);
    u32 s1 = 1, s2 = 0;
    for (u32 i = 0; i < raw_n; i++) { s1 = (s1 + raw[i]) % 65521; s2 = (s2 + s1) % 65521; }
    u32 ad = (s2 << 16) | s1;
    u8 at[4] = { (u8)(ad >> 24), (u8)(ad >> 16), (u8)(ad >> 8), (u8)ad };
    put(&z, at, 4);
    free(raw);
    free(tmp);
    struct out o = { 0, 0, 0 };
    put(&o, "\x89PNG\r\n\x1a\n", 8);
    u8 ih[13] = { (u8)(m->w >> 24), (u8)(m->w >> 16), (u8)(m->w >> 8), (u8)m->w,
                  (u8)(m->h >> 24), (u8)(m->h >> 16), (u8)(m->h >> 8), (u8)m->h, 8, 6, 0, 0, 0 };
    chunk(&o, "IHDR", ih, 13);
    chunk(&o, "IDAT", z.b, z.n);
    chunk(&o, "IEND", 0, 0);
    free(z.b);
    if (!write_file(path, o.b, o.n)) { printf("  %s\n", path); fail("could not write a texture into the mod folder."); }
    free(o.b);
}

// ---------------------------------------------------------------- pictures

static struct img paint_img[TRUCKS], tire_img, shock_img, font_img, picture_img[TRUCKS];
static double fnt[95][6];                       // FontLarge.fnt: u1 v1 u2 v2 offset width, per character
static int fnt_have[95];

static void textures_load(void)
{
    char name[200];
    for (int n = 1; n <= TRUCKS; n++) {
        sprintf(name, "Objects\\Cars\\Car%d\\0.tga", n);
        paint_img[n - 1] = tga(name);
    }
    tire_img = tga("Textures\\Cars\\Tires\\Dtire\\Drive.tga");
    shock_img = tga("Textures\\Cars\\Acc\\shock1.tga");
    font_img = tga("Textures\\Font\\FontLarge.tga");
    u32 n;
    u8 *f = arc_get("FONTS\\FONTLARGE.FNT", &n);
    for (u32 o = 37; o + 25 <= n; o += 25) {
        int c = f[o], ok = 1;
        double v[6];
        for (int k = 0; k < 6; k++) {
            v[k] = rdf(f + o + 1 + 4 * k);
            if (!(v[k] == v[k] && v[k] >= -1 && v[k] <= 512)) ok = 0;
        }
        if (ok && c >= 32 && c <= 126) { memcpy(fnt[c - 32], v, sizeof v); fnt_have[c - 32] = 1; }
    }
    free(f);
}

static void fill_tri(struct img *m, const double *p0, const double *p1, const double *p2, const u8 *col)
{
    double area = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1]);
    if (area == 0) return;
    double mnx = p0[0], mxx = p0[0], mny = p0[1], mxy = p0[1];
    const double *q[3] = { p0, p1, p2 };
    for (int i = 1; i < 3; i++) {
        if (q[i][0] < mnx) mnx = q[i][0];
        if (q[i][0] > mxx) mxx = q[i][0];
        if (q[i][1] < mny) mny = q[i][1];
        if (q[i][1] > mxy) mxy = q[i][1];
    }
    int x0 = (int)floor(mnx), x1 = (int)ceil(mxx), y0 = (int)floor(mny), y1 = (int)ceil(mxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > m->w - 1) x1 = m->w - 1;
    if (y1 > m->h - 1) y1 = m->h - 1;
    double sg = area > 0 ? 1 : -1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            double px = x + 0.5, py = y + 0.5, in = 1;
            for (int e = 0; e < 3 && in; e++) {
                const double *a = q[e], *b = q[(e + 1) % 3];
                double w = ((b[0] - a[0]) * (py - a[1]) - (px - a[0]) * (b[1] - a[1])) * sg;
                if (w < -0.35 * (b[0] - a[0] < 0 ? a[0] - b[0] : b[0] - a[0]) - 0.35 * (b[1] - a[1] < 0 ? a[1] - b[1] : b[1] - a[1])) in = 0;
            }
            if (in) { u8 *d = m->p + (y * m->w + x) * 4; d[0] = col[0]; d[1] = col[1]; d[2] = col[2]; d[3] = 255; }
        }
}

// One FontLarge letter at (x, y), k times its size, colors multiplied by
// tint, laid over the picture.  Returns how far the next letter starts.
static double draw_char(struct img *m, int ch, double x, double y, double k, const u8 *tint)
{
    if (ch < 32 || ch > 126 || !fnt_have[ch - 32]) return 16 * k;
    const double *g = fnt[ch - 32];
    double sx = g[0] * 512 + g[4] * 2, sy = g[1] * 512, gw = g[5] * 2, gh = (g[3] - g[1]) * 512;
    if (ch == ' ') return (8 * 2 + 4) * k;
    int w = (int)ceil(gw * k), h = (int)ceil(gh * k);
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++) {
            double acc[4] = { 0, 0, 0, 0 };
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    int fx = (int)(sx + (dx + (i + 0.5) / 4) / k), fy = (int)(sy + (dy + (j + 0.5) / 4) / k);
                    if (fx < 0 || fy < 0 || fx >= font_img.w || fy >= font_img.h || fx >= sx + gw) continue;
                    const u8 *s = font_img.p + (fy * font_img.w + fx) * 4;
                    for (int c = 0; c < 3; c++) acc[c] += s[c] * s[3] / 255.0;
                    acc[3] += s[3];
                }
            double a = acc[3] / 16 / 255;
            int tx = (int)x + dx, ty = (int)y + dy;
            if (a <= 0 || tx < 0 || ty < 0 || tx >= m->w || ty >= m->h) continue;
            u8 *d = m->p + (ty * m->w + tx) * 4;
            for (int c = 0; c < 3; c++) {
                double v = acc[c] / 16 * tint[c] / 255 + d[c] * (1 - a);
                d[c] = (u8)(v > 255 ? 255 : v + 0.5);
            }
            double na = a * 255 + d[3] * (1 - a);
            d[3] = (u8)(na > 255 ? 255 : na + 0.5);
        }
    return (gw + 4) * k;
}

static double text_width(const char *s, double k)
{
    double w = 0;
    for (; *s; s++) {
        int c = *s;
        w += c == ' ' ? (8 * 2 + 4) * k : c >= 32 && c <= 126 && fnt_have[c - 32] ? (fnt[c - 32][5] * 2 + 4) * k : 16 * k;
    }
    return w;
}

struct poly { double depth, p[3][2]; u8 col[3]; };
static int poly_cmp_far_first(const struct poly *a, const struct poly *b) { return a->depth > b->depth ? -1 : a->depth < b->depth; }

// picture(): a 3/4 view from the front left, each triangle its texture's
// colour at its middle, shaded by its normal, far ones first; the name
// underneath and < > at the top corners.
static void picture(int n, const char *name, const struct piece *ps, int np, const double (*tire)[3])
{
    const int W = 512, H = 256;
    struct img m = img_new(W, H);
    for (int i = 0; i < W * H; i++) { m.p[i * 4] = 20; m.p[i * 4 + 1] = 24; m.p[i * 4 + 2] = 32; m.p[i * 4 + 3] = 215; }
    double yaw = 35 * PI / 180, pitch = 18 * PI / 180;
    double cy = cos(yaw), sy = sin(yaw), cp = cos(pitch), sp = sin(pitch);
    const double light[3] = { 0.4, 0.8, -0.45 };
    int total = 0;
    for (int i = 0; i < np; i++) if (ps[i].tex < TEX_GLASS_FIRST) total += (int)ps[i].nt;
    struct poly *pl = xalloc((u32)total * sizeof *pl);
    int npl = 0;
    for (int i = 0; i < np; i++) {
        const struct piece *q = &ps[i];
        if (q->tex >= TEX_GLASS_FIRST) continue;
        const struct img *im = q->tex == TEX_TIRE ? &tire_img : q->tex == TEX_SHOCK ? &shock_img : &paint_img[n - 1];
        double off[3] = { 0, 0, 0 };
        if (q->frame) memcpy(off, tire[q->frame - 1], sizeof off);
        for (u32 t = 0; t < q->nt; t++) {
            struct poly *o = &pl[npl++];
            double u = 0, v = 0, nr[3] = { 0, 0, 0 }, dsum = 0;
            for (int c = 0; c < 3; c++) {
                const double *vv = q->v[q->t[t][c]];
                double p[3] = { vv[0] + off[0], vv[1] + off[1], vv[2] + off[2] };
                double x = cy * p[0] + sy * p[2], z = -sy * p[0] + cy * p[2];
                o->p[c][0] = -x;
                o->p[c][1] = cp * p[1] - sp * z;
                dsum += -(sp * p[1] + cp * z);
                double nx = cy * vv[3] + sy * vv[5], nz = -sy * vv[3] + cy * vv[5];
                nr[0] += -nx;
                nr[1] += cp * vv[4] - sp * nz;
                nr[2] += -(sp * vv[4] + cp * nz);
                u += vv[6];
                v += vv[7];
            }
            o->depth = dsum / 3;
            u = u / 3; u -= floor(u);
            v = v / 3; v -= floor(v);
            const u8 *px = im->p + ((int)(v * (im->h - 1)) * im->w + (int)(u * (im->w - 1))) * 4;
            double dt = (nr[0] / 3) * light[0] + (nr[1] / 3) * light[1] + (nr[2] / 3) * light[2];
            double shade = 0.45 + 0.55 * (dt < 0 ? -dt : dt);
            for (int c = 0; c < 3; c++) { int cv = (int)(px[c] * shade); o->col[c] = (u8)(cv > 255 ? 255 : cv); }
        }
    }
    double mnx = 1e30, mxx = -1e30, mny = 1e30, mxy = -1e30;
    for (int i = 0; i < npl; i++)
        for (int c = 0; c < 3; c++) {
            double x = pl[i].p[c][0], y = pl[i].p[c][1];
            if (x < mnx) mnx = x;
            if (x > mxx) mxx = x;
            if (y < mny) mny = y;
            if (y > mxy) mxy = y;
        }
    double s = (W - 40) / (mxx - mnx), s2 = (H - 70) / (mxy - mny);
    if (s2 < s) s = s2;
    double ox = W / 2.0 - s * (mxx + mnx) / 2, oy = (H - 50) / 2.0 + 6 + s * (mxy + mny) / 2;
    // far first (stable insertion sort keeps equal depths in order, as Python's sort)
    for (int i = 1; i < npl; i++) {
        struct poly t = pl[i];
        int j = i - 1;
        while (j >= 0 && poly_cmp_far_first(&pl[j], &t) > 0) { pl[j + 1] = pl[j]; j--; }
        pl[j + 1] = t;
    }
    for (int i = 0; i < npl; i++) {
        double a[2] = { ox + pl[i].p[0][0] * s, oy - pl[i].p[0][1] * s };
        double b[2] = { ox + pl[i].p[1][0] * s, oy - pl[i].p[1][1] * s };
        double c[2] = { ox + pl[i].p[2][0] * s, oy - pl[i].p[2][1] * s };
        fill_tri(&m, a, b, c, pl[i].col);
    }
    free(pl);
    static const u8 white[3] = { 255, 255, 255 }, yellow[3] = { 255, 220, 80 };
    const double k = 0.9;
    double x = (W - text_width(name, k)) / 2;
    for (const char *c = name; *c; c++) x += draw_char(&m, (u8)*c, x, H - 60, k, white);
    draw_char(&m, '<', 10, 2, k, yellow);
    draw_char(&m, '>', W - 10 - text_width(">", k) + 4 * k, 2, k, yellow);
    picture_img[n - 1] = resize(&m, 256, 128);
    free(m.p);
}

// ---------------------------------------------------------------- the pack

static const double hud_part_map[18][4] = {
    { 0.928, 0.01, 0.801, 0.069 }, { 0.928, 0.285, 0.801, 0.344 },
    { 0.627, 0.01, 0.499, 0.069 }, { 0.627, 0.285, 0.5, 0.344 },
    { 0.973, 0.033, 0.932, 0.058 }, { 0.973, 0.296, 0.932, 0.32 },
    { 0.496, 0.027, 0.45, 0.039 }, { 0.496, 0.314, 0.45, 0.332 },
    { 0.797, 0.027, 0.631, 0.039 }, { 0.797, 0.314, 0.631, 0.332 },
    { 0.994, 0.058, 0.957, 0.116 }, { 0.994, 0.238, 0.957, 0.295 },
    { 0.45, 0.027, 0.414, 0.068 }, { 0.45, 0.286, 0.414, 0.332 },
    { 0.774, 0.1485, 0.539, 0.205 }, { 0.963, 0.125, 0.774, 0.2285 },
    { 0.987, 0.116, 0.971, 0.237 }, { 0.434, 0.0685, 0.422, 0.286 },
};

static char tex_dir[400], index_txt[8000];
static int tex_count;

static void save_tex(const char *file, struct img m)
{
    char path[500];
    sprintf(path, "%s\\%s", tex_dir, file);
    png_write(path, &m);
    free(m.p);
    sprintf(index_txt + strlen(index_txt), "%d,%s\n", GBIX0 + tex_count, file);
    tex_count++;
}

static struct img hud_turn(const struct img *a, int x0, int y0, int x1, int y1)
{
    struct img c = crop(a, x0, y0, x1, y1), r = rotate90(&c), mi = mirror_lr(&r);
    struct img out = resize(&mi, 128, 256);
    free(c.p); free(r.p); free(mi.p);
    return out;
}

static struct img glow(struct img m)
{
    for (int i = 0; i < m.w * m.h; i++) {
        u8 *p = m.p + i * 4, a = p[0] > p[1] ? p[0] : p[1];
        p[3] = a > p[2] ? a : p[2];
    }
    return m;
}

static struct img gcrop(const struct img *a, double u1, double v1, double u2, double v2)
{
    return crop(a, rnd(u1 * 0.512), rnd(v1 * 0.512), rnd(u2 * 0.512), rnd(v2 * 0.512));
}

static struct img resized(struct img m, int w, int h)
{
    struct img r = resize(&m, w, h);
    free(m.p);
    return r;
}

static void write_textures(const char *mod)
{
    char name[400];
    sprintf(name, "%s\\textures", mod);
    _mkdir(name);
    sprintf(tex_dir, "%s\\textures\\SADXTRUCK", mod);
    _mkdir(tex_dir);
    for (int n = 1; n <= TRUCKS; n++) { sprintf(name, "paint%d.png", n); save_tex(name, paint_img[n - 1]); }
    save_tex("tire.png", tire_img);
    save_tex("shock.png", shock_img);
    for (int n = 1; n <= TRUCKS; n++) { sprintf(name, "picture%d.png", n); save_tex(name, picture_img[n - 1]); }

    // Damage HUD: panel, the whole schematic, the 18 parts.
    struct img atlas = tga("Textures\\HUD\\hud.tga");
    int W = atlas.w;
    int bx0 = rnd(0.414 * W), by0 = rnd(0.010 * W), bx1 = rnd(0.994 * W), by1 = rnd(0.344 * W);
    struct img panel = img_new(8, 8);
    memset(panel.p, 255, 8 * 8 * 4);
    save_tex("hud_panel.png", panel);
    save_tex("hud_all.png", hud_turn(&atlas, bx0, by0, bx1, by1));
    for (int k = 0; k < 18; k++) {
        const double *r = hud_part_map[k];
        int x0 = rnd(r[2] * W), y0 = rnd(r[1] * W), x1 = rnd(r[0] * W), y1 = rnd(r[3] * W);
        struct img part = img_new(atlas.w, atlas.h);
        memcpy(part.p, atlas.p, (u32)(atlas.w * atlas.h * 4));
        for (int y = 0; y < atlas.h; y++)
            for (int x = 0; x < atlas.w; x++)
                if (!(x >= x0 && x < x1 && y >= y0 && y < y1)) part.p[(y * atlas.w + x) * 4 + 3] = 0;
        sprintf(name, "hud_part%d.png", k);
        save_tex(name, hud_turn(&part, bx0, by0, bx1, by1));
        free(part.p);
    }
    save_tex("colors.png", tga("Textures\\Cars\\Acc\\colors.tga"));
    save_tex("glow_white.png", glow(tga("Textures\\Cars\\WhiteCoronas_128.tga")));
    save_tex("glow_red.png", glow(tga("Textures\\Cars\\RedCoronas_64.tga")));
    save_tex("glass.png", tga("Textures\\Cars\\Glass.tga"));
    for (int n = 1; n <= TRUCKS; n++) {
        char src[200];
        sprintf(src, "Objects\\Cars\\Car%d\\glassdmg.tga", n);
        sprintf(name, "glassdmg%d.png", n);
        save_tex(name, tga(src));
    }

    // Shadow: black, alpha 160 in the middle, fading over the outer quarter.
    struct img sh = img_new(64, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 64; x++) {
            double ex = (x + 0.5 < 64 - x - 0.5 ? x + 0.5 : 64 - x - 0.5) / (64 * 0.25);
            double ey = (y + 0.5 < 128 - y - 0.5 ? y + 0.5 : 128 - y - 0.5) / (128 * 0.25);
            double e = ex < ey ? ex : ey;
            if (e > 1.0) e = 1.0;
            if (e < 0.0) e = 0.0;
            e = e * e * (3 - 2 * e);
            sh.p[(y * 64 + x) * 4 + 3] = (u8)(int)(160 * e + 0.5);
        }
    save_tex("shadow.png", sh);

    // Gauges, from hud.tga (HUDLOADER.LUA coordinates in 1000ths).
    save_tex("gauge_tach.png", resized(gcrop(&atlas, 42, 554, 421, 933), 256, 256));
    save_tex("gauge_speedo.png", resized(gcrop(&atlas, 421, 355, 999, 933), 512, 512));
    save_tex("gauge_needle.png", resized(gcrop(&atlas, 289.062, 0, 398.4375, 285.156), 64, 256));
    static const double bars[3][2] = { { 78.125, 160.156 }, { 171.875, 253.906 }, { 259.765, 343.75 } };
    static const char *const bar_names[3] = { "gauge_bar_fuel.png", "gauge_bar_oil.png", "gauge_bar_water.png" };
    for (int b = 0; b < 3; b++) {
        struct img bar = gcrop(&atlas, bars[b][0], 296.875, bars[b][1], 546.875);
        for (int y = 0; y < bar.h; y++)
            for (int x = 19; x < bar.w; x++) {
                u8 *p = bar.p + (y * bar.w + x) * 4;
                if (p[0] > p[1] + 40 && x >= 20) { p[0] = p[1] = p[2] = 23; p[3] = 156; }
            }
        save_tex(bar_names[b], resized(bar, 64, 128));
    }
    struct img fuel = gcrop(&atlas, 78.125, 296.875, 160.156, 546.875);
    save_tex("gauge_fill.png", resized(crop(&fuel, 21, 96, 38, 124), 16, 32));
    free(fuel.p);
    save_tex("gauge_icon_fuel.png", resized(gcrop(&atlas, 189, 1, 281, 94), 64, 64));
    save_tex("gauge_icon_oil.png", resized(gcrop(&atlas, 93, 1, 189, 94), 64, 64));
    save_tex("gauge_icon_water.png", resized(gcrop(&atlas, 1, 1, 91, 94), 64, 64));
    static const int tops[10] = { 194, 220, 245, 271, 297, 324, 349, 376, 401, 427 };
    for (int d = 0; d < 10; d++) {
        sprintf(name, "gauge_digit%d.png", d);
        save_tex(name, resized(crop(&atlas, 0, tops[d] - 3, 18, tops[d] + 23), 32, 32));
    }
    save_tex("font_large.png", font_img);
    free(atlas.p);

    if (tex_count != TEX_COUNT) fail("internal: texture count.");
    sprintf(name, "%s\\index.txt", tex_dir);
    if (!write_file(name, index_txt, strlen(index_txt))) fail("could not write the texture pack's index.txt.");
}
