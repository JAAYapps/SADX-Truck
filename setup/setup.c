// setup.exe for SADX Truck: builds the mod's Cabela's files from the
// player's own copies, so the mod itself ships none of them.
//
// Double-click it (in the mod folder): it looks in the usual install
// places, asks with a folder or file picker for anything it can't find,
// and ends with a message box.  Everything it does goes into
// setup_log.txt next to it.
//
// usage with no boxes (for testing):
//        setup.exe [Cabela's 3 folder] [Enhancement Pack zip] [Cabela's 1 folder]
//   Cabela's 3 folder  default C:\Program Files (x86)\Activision Value\FUNLabs\Cora3
//                      (then the same under C:\Program Files)
//   Enhancement Pack   default cora3_enhancement_pack.zip next to setup.exe
//   Cabela's 1 folder  default C:\Program Files (x86)\Activision Value\
//                      Cabela's 4x4 Off-road Adventure (then Program Files)
//   Writes into the folder setup.exe is in (the mod folder).
//
// Step 1: the Enhancement Pack zip -> data1.cab inside it ->
// Bin\EngineDll.dll and EnhPack\Scripts\cars.lua.  The pack's own
// installer is a 16-bit Windows 3.1 program that 64-bit Windows can't run,
// so the files are read straight out of the zip.
// Step 2: trucks.bin (trucks_bin.c), from Cora3.arc, that cars.lua (red
// RPM set to 5800, the mod's tuning) and Cabela's 1 Data\data1.res.
// Step 3: the sounds folder (88 .wav files).  Cabela's 1 is optional:
// without it its 5 starter-crank sounds are written as silence of the
// same lengths.
// Step 4: the texture pack textures\SADXTRUCK (textures.c, 90 PNGs).
// Step 5: Cabela's 16 maps (maps.c, the C copy of map_to_bin.py): maps\
// (ground, objects, collision, grass, sky files) and their texture packs.
// Step 6: message boxes, pickers and the log (this file's main part).
//
// Build (from ProjectSADXTruck):
//   clang --target=i686-pc-windows-msvc -O1 -msse2 -mfpmath=sse -fno-math-errno -fno-builtin -fno-stack-protector -mno-stack-arg-probe -c setup/setup.c -o setup/setup.obj
//   lld-link /nodefaultlib /entry:start /subsystem:windows setup/setup.obj setup/kernel32.lib setup/msvcrt.lib /out:setup/setup.exe

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef struct FILE FILE;
int _fltused;                           // the compiler asks for it when floats are used

__declspec(dllimport) FILE *fopen(const char *, const char *);
__declspec(dllimport) u32 fread(void *, u32, u32, FILE *);
__declspec(dllimport) u32 fwrite(const void *, u32, u32, FILE *);
__declspec(dllimport) int fclose(FILE *);
__declspec(dllimport) int fseek(FILE *, long, int);
__declspec(dllimport) long ftell(FILE *);
__declspec(dllimport) void *malloc(u32);
__declspec(dllimport) void free(void *);
__declspec(dllimport) int printf(const char *, ...);
__declspec(dllimport) int sprintf(char *, const char *, ...);
__declspec(dllimport) void *memcpy(void *, const void *, u32);
__declspec(dllimport) void *memset(void *, int, u32);
__declspec(dllimport) int memcmp(const void *, const void *, u32);
__declspec(dllimport) u32 strlen(const char *);
__declspec(dllimport) int _stricmp(const char *, const char *);
__declspec(dllimport) int strcmp(const char *, const char *);
__declspec(dllimport) int _mkdir(const char *);
__declspec(dllimport) char *strrchr(const char *, int);
__declspec(dllimport) u32 __stdcall GetModuleFileNameA(void *, char *, u32);
__declspec(dllimport) char *__stdcall GetCommandLineA(void);
__declspec(dllimport) void __stdcall ExitProcess(u32);
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *);
__declspec(dllimport) void *__stdcall GetProcAddress(void *, const char *);
__declspec(dllimport) int _vsnprintf(char *, u32, const char *, __builtin_va_list);
__declspec(dllimport) void *realloc(void *, u32);

// Everything printed goes to the console (when there is one) and into a
// buffer that ends up in setup_log.txt.
static char *log_b;
static u32 log_n, log_cap;
static int say(const char *fmt, ...)
{
    char b[2048];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = _vsnprintf(b, sizeof b - 1, fmt, ap);
    __builtin_va_end(ap);
    if (n < 0) n = sizeof b - 1;
    b[n] = 0;
    printf("%s", b);
    if (log_n + (u32)n + 1 > log_cap) {
        log_cap = (log_n + (u32)n + 1) * 2 + 4096;
        log_b = realloc(log_b, log_cap);
        if (!log_b) ExitProcess(3);
    }
    memcpy(log_b + log_n, b, (u32)n);
    log_n += (u32)n;
    return n;
}
#define printf say

// The Enhancement Pack's EngineDll.dll (the one the mod is built against)
// and the 2003 disc one (the mod crashes with it).
#define ENH_DLL_SIZE 4374528u
#define ENH_DLL_CRC 2552330242u
#define DISC_DLL_SIZE 4370432u
#define DISC_DLL_CRC 2543876354u

// ---------------------------------------------------------------- files

static u8 *read_file(const char *name, u32 *size)
{
    FILE *f = fopen(name, "rb");
    if (!f) return 0;
    fseek(f, 0, 2);
    long n = ftell(f);
    fseek(f, 0, 0);
    u8 *b = n >= 0 ? malloc((u32)n + 1) : 0;
    if (b && fread(b, 1, (u32)n, f) != (u32)n) { free(b); b = 0; }
    fclose(f);
    if (b) { b[n] = 0; *size = (u32)n; }
    return b;
}

static int write_file(const char *name, const void *b, u32 size)
{
    FILE *f = fopen(name, "wb");
    if (!f) return 0;
    int ok = fwrite(b, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static u32 rd16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static u32 rd32(const u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static u32 crc_table[256];
static u32 crc32(const u8 *p, u32 n)
{
    if (!crc_table[1])
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    u32 c = 0xFFFFFFFFu;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------- inflate
// Adapted from puff.c by Mark Adler (zlib's contrib/puff, version 2.3):
// bits(), build() (puff's construct()), decode(), codes() and inflate()
// follow puff's slow decode path, changed (renamed, shortened, output
// into a caller's buffer, no stored-block length check).  This is an
// altered version, not the original.  puff's notice:
//
//   Copyright (C) 2002-2013 Mark Adler, all rights reserved
//   version 2.3, 21 Jan 2013
//
//   This software is provided 'as-is', without any express or implied
//   warranty.  In no event will the author be held liable for any damages
//   arising from the use of this software.
//
//   Permission is granted to anyone to use this software for any purpose,
//   including commercial applications, and to alter it and redistribute it
//   freely, subject to the following restrictions:
//
//   1. The origin of this software must not be misrepresented; you must not
//      claim that you wrote the original software. If you use this software
//      in a product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//   2. Altered source versions must be plainly marked as such, and must not be
//      misrepresented as being the original software.
//   3. This notice may not be removed or altered from any source distribution.
//
//   Mark Adler    madler@alumni.caltech.edu
//
// Raw deflate (RFC 1951), as used inside zip files and InstallShield
// cabinets.  Canonical Huffman decoding, one bit at a time.

struct inf {
    const u8 *in, *in_end;
    u32 bitbuf, bitcnt;
    u8 *out, *out_start, *out_end;
    int err;
};
struct huff { short count[16], symbol[320]; };

static u32 bits(struct inf *s, int n)
{
    while (s->bitcnt < (u32)n) {
        if (s->in >= s->in_end) { s->err = 1; return 0; }
        s->bitbuf |= (u32)*s->in++ << s->bitcnt;
        s->bitcnt += 8;
    }
    u32 v = s->bitbuf & ((1u << n) - 1);
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

static void build(struct huff *h, const u8 *len, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h->count[l];
    for (int i = 0; i < n; i++) if (len[i]) h->symbol[offs[len[i]]++] = (short)i;
}

static int decode(struct inf *s, const struct huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= (int)bits(s, 1);
        int c = h->count[l];
        if (code - c < first) return h->symbol[index + (code - first)];
        index += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    s->err = 1;
    return 0;
}

static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const u8 lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const u8 dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void codes(struct inf *s, const struct huff *lit, const struct huff *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (s->err) return;
        if (sym < 256) {
            if (s->out >= s->out_end) { s->err = 2; return; }
            *s->out++ = (u8)sym;
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) { s->err = 3; return; }
            u32 len = (u32)lbase[sym] + bits(s, lext[sym]);
            int ds = decode(s, dist);
            if (ds >= 30) { s->err = 3; return; }
            u32 d = (u32)dbase[ds] + bits(s, dext[ds]);
            if (s->err || d > (u32)(s->out - s->out_start) || len > (u32)(s->out_end - s->out)) { s->err = 3; return; }
            for (u32 k = 0; k < len; k++, s->out++) *s->out = s->out[-(int)d];
        }
    }
}

// Inflates in[0..in_n) to the end of out (out_len already used); returns
// the new used length, or -1.
static int inflate(const u8 *in, u32 in_n, u8 *out, u32 out_len, u32 out_cap)
{
    static const u8 order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    struct inf s = { in, in + in_n, 0, 0, out + out_len, out + out_len, out + out_cap, 0 };
    static struct huff lit, dist;
    int last;
    do {
        last = (int)bits(&s, 1);
        int type = (int)bits(&s, 2);
        if (type == 0) {                                 // stored
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.in + 4 > s.in_end) return -1;
            u32 n = rd16(s.in);
            s.in += 4;
            if (s.in + n > s.in_end || s.out + n > s.out_end) return -1;
            memcpy(s.out, s.in, n);
            s.in += n; s.out += n;
        } else if (type == 1) {                          // fixed codes
            u8 len[288];
            int i = 0;
            for (; i < 144; i++) len[i] = 8;
            for (; i < 256; i++) len[i] = 9;
            for (; i < 280; i++) len[i] = 7;
            for (; i < 288; i++) len[i] = 8;
            build(&lit, len, 288);
            for (i = 0; i < 30; i++) len[i] = 5;
            build(&dist, len, 30);
            codes(&s, &lit, &dist);
        } else if (type == 2) {                          // dynamic codes
            u8 len[320];
            int nlen = (int)bits(&s, 5) + 257, ndist = (int)bits(&s, 5) + 1, ncode = (int)bits(&s, 4) + 4;
            if (nlen > 286 || ndist > 30) return -1;
            memset(len, 0, sizeof len);
            for (int i = 0; i < ncode; i++) len[order[i]] = (u8)bits(&s, 3);
            build(&lit, len, 19);
            for (int i = 0; i < nlen + ndist && !s.err;) {
                int sym = decode(&s, &lit);
                if (sym < 16) { len[i++] = (u8)sym; continue; }
                u8 v = 0;
                int rep;
                if (sym == 16) { if (!i) return -1; v = len[i - 1]; rep = 3 + (int)bits(&s, 2); }
                else if (sym == 17) rep = 3 + (int)bits(&s, 3);
                else rep = 11 + (int)bits(&s, 7);
                if (i + rep > nlen + ndist) return -1;
                while (rep--) len[i++] = v;
            }
            build(&lit, len, nlen);
            build(&dist, len + nlen, ndist);
            codes(&s, &lit, &dist);
        } else {
            return -1;
        }
        if (s.err) return -1;
    } while (!last);
    return (int)(s.out - out);
}

// ---------------------------------------------------------------- zip
// One member of a zip file, stored or deflated; 0 when missing or bad.

static u8 *zip_get(const u8 *z, u32 zn, const char *want, u32 *size)
{
    u32 e = zn >= 22 ? zn - 22 : 0;
    while (e > 0 && rd32(z + e) != 0x06054b50u) e--;          // end of central directory
    if (rd32(z + e) != 0x06054b50u) return 0;
    u32 count = rd16(z + e + 10), p = rd32(z + e + 16);
    for (u32 i = 0; i < count && p + 46 <= zn; i++) {
        u32 method = rd16(z + p + 10), crc = rd32(z + p + 16), csize = rd32(z + p + 20), usize = rd32(z + p + 24);
        u32 nlen = rd16(z + p + 28), xlen = rd16(z + p + 30), clen = rd16(z + p + 32), local = rd32(z + p + 42);
        char name[260];
        if (nlen < sizeof name && p + 46 + nlen <= zn) {
            memcpy(name, z + p + 46, nlen);
            name[nlen] = 0;
            if (!_stricmp(name, want) && local + 30 <= zn) {
                u32 data = local + 30 + rd16(z + local + 26) + rd16(z + local + 28);
                if (data + csize > zn) return 0;
                u8 *out = malloc(usize + 1);
                if (!out) return 0;
                int n = method == 0 && csize == usize ? (memcpy(out, z + data, usize), (int)usize)
                      : method == 8 ? inflate(z + data, csize, out, 0, usize) : -1;
                if (n != (int)usize || crc32(out, usize) != crc) { free(out); return 0; }
                out[usize] = 0;
                *size = usize;
                return out;
            }
        }
        p += 46 + nlen + xlen + clen;
    }
    return 0;
}

// ---------------------------------------------------------------- cabinet
// InstallShield 5 data1.cab with its header inside (no data1.hdr), as
// unshield 1.6.2 reads it:
//   0      "ISc(", version, volume info, descriptor offset (u32 at 12)
//   descriptor + 0x0c  file table offset (from the descriptor)
//   descriptor + 0x1c  directory count,  + 0x28  file count
//   file table: u32 per directory (name offset), then u32 per file
//   (descriptor offset), all from the file table's start.
//   file descriptor: name offset, directory number (u32), flags (u16),
//   size, packed size (u32), 20 bytes, data offset (u32, from the cab's
//   start).  Flag (1 << 2) = packed: blocks of u16 length + raw deflate.

static u8 *cab_get(const u8 *c, u32 cn, const char *dir, const char *file, u32 *size)
{
    if (cn < 20 || rd32(c) != 0x28635349u) return 0;            // "ISc("
    u32 cd = rd32(c + 12);
    if (cd + 0x30 > cn) return 0;
    u32 ft = cd + rd32(c + cd + 0x0c), ndir = rd32(c + cd + 0x1c), nfile = rd32(c + cd + 0x28);
    if (ft + 4 * (ndir + nfile) > cn) return 0;
    for (u32 i = 0; i < nfile; i++) {
        u32 p = ft + rd32(c + ft + 4 * (ndir + i));
        if (p + 42 > cn) return 0;
        u32 name = ft + rd32(c + p), di = rd32(c + p + 4), flags = rd16(c + p + 8);
        u32 usize = rd32(c + p + 10), csize = rd32(c + p + 14), data = rd32(c + p + 38);
        if (di >= ndir || name >= cn) continue;
        u32 dname = ft + rd32(c + ft + 4 * di);
        if (dname >= cn || _stricmp((const char *)c + dname, dir) || _stricmp((const char *)c + name, file)) continue;
        if (data + csize > cn) return 0;
        u8 *out = malloc(usize + 1);
        if (!out) return 0;
        int n = 0;
        if (flags & (1 << 2)) {
            for (u32 q = data; q < data + csize && n >= 0;) {
                u32 len = rd16(c + q);
                if (q + 2 + len > data + csize) { n = -1; break; }
                n = inflate(c + q + 2, len, out, (u32)n, usize);
                q += 2 + len;
            }
        } else if (csize == usize) {
            memcpy(out, c + data, usize);
            n = (int)usize;
        } else {
            n = -1;
        }
        if (n != (int)usize) { free(out); return 0; }
        out[usize] = 0;
        *size = usize;
        return out;
    }
    return 0;
}

// ---------------------------------------------------------------- main

static char mod_dir[300], cabelas[300], zip_path[300], cabelas1[300];
static int gui;                         // double-clicked: boxes and pickers
static int writing;                     // mod files are being written

static void path_join(char *out, const char *dir, const char *name) { sprintf(out, "%s\\%s", dir, name); }

static void write_log(void)
{
    char name[400];
    path_join(name, mod_dir, "setup_log.txt");
    FILE *f = fopen(name, "wb");
    if (f) { fwrite(log_b, 1, log_n, f); fclose(f); }
}

// Windows' own boxes, loaded by name (user32, shell32, comdlg32).
#define MB_OK 0
#define MB_OKCANCEL 1
#define MB_YESNO 4
#define MB_ICONERROR 16
#define MB_ICONQUESTION 32
#define MB_ICONWARNING 48
#define MB_ICONINFORMATION 64
#define IDOK 1
#define IDYES 6
static int box(const char *text, u32 flags)
{
    static int (__stdcall *mb)(void *, const char *, const char *, u32);
    if (!mb) {
        void *u = LoadLibraryA("user32.dll");
        mb = u ? (void *)GetProcAddress(u, "MessageBoxA") : 0;
        if (!mb) return IDOK;
    }
    return mb(0, text, "SADX Truck setup", flags);
}

static int fail(const char *msg)
{
    printf("\nSETUP STOPPED: %s\n", msg);
    write_log();
    if (gui) {
        char b[1200];
        sprintf(b, "Setup stopped:\n\n%s\n\n%s The details are in setup_log.txt.", msg,
                writing ? "Some of the mod's files may be half written: run setup.exe again once this is fixed."
                        : "No mod files were changed.");
        box(b, MB_OK | MB_ICONERROR);
    }
    ExitProcess(1);
    return 0;
}

// Folder picker (SHBrowseForFolder); 0 = cancelled.
static int pick_folder(const char *title, char *out)
{
    struct { void *owner, *root; char *name; const char *title; u32 flags; void *fn; long param; int image; } bi;
    char shown[300];
    void *sh = LoadLibraryA("shell32.dll");
    void *(__stdcall *browse)(void *) = sh ? (void *)GetProcAddress(sh, "SHBrowseForFolderA") : 0;
    int (__stdcall *topath)(void *, char *) = sh ? (void *)GetProcAddress(sh, "SHGetPathFromIDListA") : 0;
    if (!browse || !topath) return 0;
    memset(&bi, 0, sizeof bi);
    bi.name = shown;
    bi.title = title;
    bi.flags = (1 << 0) | (1 << 4);                // folders only, with a typing box
    void *id = browse(&bi);
    return id && topath(id, out) && out[0];
}

// File picker for the zip (GetOpenFileName); 0 = cancelled.
static int pick_zip(char *out)
{
    struct { u32 size; void *owner, *inst; const char *filter; char *custom; u32 ncustom, index; char *file;
             u32 nfile; char *title_out; u32 ntitle; const char *dir, *title; u32 flags; u16 off, ext;
             const char *def; long data; void *hook; const char *tmpl; } of;
    void *cd = LoadLibraryA("comdlg32.dll");
    int (__stdcall *open)(void *) = cd ? (void *)GetProcAddress(cd, "GetOpenFileNameA") : 0;
    if (!open) return 0;
    memset(&of, 0, sizeof of);
    of.size = sizeof of;                            // 76 bytes: the Windows 95 layout every Windows takes
    of.filter = "Enhancement Pack zip (*.zip)\0*.zip\0All files\0*.*\0";
    of.index = 1;
    out[0] = 0;
    of.file = out;
    of.nfile = 300;
    of.title = "Pick cora3_enhancement_pack.zip (don't unzip it)";
    of.flags = 0x1000 | 0x800 | 0x4;                // file must exist, folder must exist, no read-only box
    return open(&of) && out[0];
}

static int exists(const char *dir, const char *file)
{
    char name[400];
    path_join(name, dir, file);
    FILE *f = fopen(name, "rb");
    if (f) fclose(f);
    return f != 0;
}

// A picked folder one level too deep (Cora3\Cora3, or Data) is taken
// back up to the install folder.
static void up_one(char *dir)
{
    char *s = strrchr(dir, '\\');
    if (s && s[1]) *s = 0;
    else if (s) { *s = 0; s = strrchr(dir, '\\'); if (s) *s = 0; }
}

// Splits the command line into up to 3 arguments after the program name.
static int args(char out[][300])
{
    const char *p = GetCommandLineA();
    int n = -1;
    while (*p && n < 3) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char tmp[300];
        u32 k = 0;
        int quoted = 0;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') quoted = !quoted;
            else if (k < sizeof tmp - 1) tmp[k++] = *p;
            p++;
        }
        tmp[k] = 0;
        if (n >= 0) memcpy(out[n], tmp, k + 1);
        n++;
    }
    return n < 0 ? 0 : n;
}

// The mod's red RPM (gearbox shift point) for every truck; the pack's
// cars.lua says 4800.
#define RED_RPM 5800.0
#define PACK_URL "https://archive.org/download/CabelasORA3/cora3_enhancement_pack.zip"

#include "trucks_bin.c"
#include "textures.c"
#include "maps.c"

void __cdecl start(void)
{
    char a[3][300], name[400];
    int na = args(a);
    gui = na == 0;
    GetModuleFileNameA(0, mod_dir, sizeof mod_dir);
    char *slash = strrchr(mod_dir, '\\');
    if (slash) *slash = 0;
    printf("SADX Truck setup\nmod folder: %s\n", mod_dir);
    if (gui && !exists(mod_dir, "sadxtruck.dll") &&
        box("setup.exe should be in the SADX Truck mod folder (next to sadxtruck.dll), and it isn't.\n\n"
            "Go on anyway and put the files next to setup.exe?", MB_YESNO | MB_ICONWARNING) != IDYES)
        fail("setup.exe is not in the mod folder.");

    // Cabela's 3 folder: the argument, the usual install places, or a picker.
    u32 n;
    static const char *const places[] = {
        "C:\\Program Files (x86)\\Activision Value\\FUNLabs\\Cora3",
        "C:\\Program Files\\Activision Value\\FUNLabs\\Cora3",
    };
    if (na >= 1) memcpy(cabelas, a[0], strlen(a[0]) + 1);
    for (int i = 0; i < 2 && !cabelas[0]; i++)
        if (exists(places[i], "Cora3\\Cora3.arc")) memcpy(cabelas, places[i], strlen(places[i]) + 1);
    while (gui && !exists(cabelas, "Cora3\\Cora3.arc")) {
        if (cabelas[0] && exists(cabelas, "Cora3.arc")) { up_one(cabelas); continue; }
        if (box(cabelas[0] ? "That folder doesn't have Cabela's 3 in it (no Cora3\\Cora3.arc).\n\nPick the folder Cabela's 4x4 Off-Road Adventure 3 is installed in (usually ...\\Activision Value\\FUNLabs\\Cora3)."
                           : "Cabela's 4x4 Off-Road Adventure 3 wasn't found in Program Files.\n\nPick the folder it's installed in (usually ...\\Activision Value\\FUNLabs\\Cora3).",
                MB_OKCANCEL | MB_ICONQUESTION) != IDOK || !pick_folder("The Cabela's 3 folder (it has Cora3\\Cora3.arc in it)", cabelas))
            fail("no Cabela's 3 folder picked.");
    }
    if (!exists(cabelas, "Cora3\\Cora3.arc")) fail("Cora3\\Cora3.arc is not in the Cabela's 3 folder given.");
    printf("Cabela's 3:  %s\n", cabelas);

    // The Enhancement Pack zip: the argument, next to setup.exe, or a picker.
    if (na >= 2) memcpy(zip_path, a[1], strlen(a[1]) + 1);
    else path_join(zip_path, mod_dir, "cora3_enhancement_pack.zip");
    u8 *zip = read_file(zip_path, &n);
    if (!zip && gui) {
        if (box("The Cabela's 3 Enhancement Pack (a free patch) is needed: the truck runs on its physics.\n\n"
                "Its official download is gone; a copy is here:\n" PACK_URL "\n\n"
                "Download it (don't unzip it), then press OK and pick the zip.\n"
                "(Or put it next to setup.exe and run setup.exe again.)", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK ||
            !pick_zip(zip_path))
            fail("no Enhancement Pack zip picked. It's at " PACK_URL);
        zip = read_file(zip_path, &n);
    }
    if (!zip) fail("cora3_enhancement_pack.zip not found. Put it next to setup.exe (don't unzip it). It's at " PACK_URL);
    printf("Enhancement Pack: %s (%u bytes)\n", zip_path, n);
    u32 cab_n;
    u8 *cab = zip_get(zip, n, "data1.cab", &cab_n);
    if (!cab) fail("that zip isn't the Enhancement Pack, or it's damaged (no data1.cab inside). Download it again from " PACK_URL);
    free(zip);
    printf("  data1.cab: %u bytes\n", cab_n);

    u32 dll_n, lua_n;
    u8 *dll = cab_get(cab, cab_n, "Bin", "EngineDll.dll", &dll_n);
    u8 *lua = cab_get(cab, cab_n, "EnhPack\\Scripts", "cars.lua", &lua_n);
    free(cab);
    if (!dll || !lua) fail("EngineDll.dll or cars.lua missing inside the zip: is it the Cabela's 3 Enhancement Pack?");
    u32 dll_crc = crc32(dll, dll_n);
    printf("  EngineDll.dll: %u bytes, CRC32 %u\n", dll_n, dll_crc);
    printf("  cars.lua: %u bytes, CRC32 %u\n", lua_n, crc32(lua, lua_n));
    if (dll_n == DISC_DLL_SIZE && dll_crc == DISC_DLL_CRC)
        fail("that zip has the 2003 disc EngineDll.dll, not the Enhancement Pack's.");
    if (dll_n != ENH_DLL_SIZE || dll_crc != ENH_DLL_CRC) {
        printf("  WARNING: unknown EngineDll.dll version; the truck may crash.\n");
        if (gui && box("This Enhancement Pack's EngineDll.dll is a version the mod wasn't made with, so the truck may crash.\n\nGo on anyway?",
                       MB_YESNO | MB_ICONWARNING) != IDYES)
            fail("unknown EngineDll.dll version; stopped.");
    }

    // Cabela's 1 (optional): the argument, the usual install places, or a picker.
    static const char *const places1[] = {
        "C:\\Program Files (x86)\\Activision Value\\Cabela's 4x4 Off-road Adventure",
        "C:\\Program Files\\Activision Value\\Cabela's 4x4 Off-road Adventure",
    };
    if (na >= 3) memcpy(cabelas1, a[2], strlen(a[2]) + 1);
    for (int i = 0; i < 2 && !cabelas1[0] && na < 3; i++)
        if (exists(places1[i], "Data\\data1.res")) memcpy(cabelas1, places1[i], strlen(places1[i]) + 1);
    while (gui && !(cabelas1[0] && exists(cabelas1, "Data\\data1.res"))) {
        if (cabelas1[0] && exists(cabelas1, "data1.res")) { up_one(cabelas1); continue; }
        if (box(cabelas1[0] ? "That folder doesn't have the first Cabela's 4x4 in it (no Data\\data1.res).\n\nPick another folder? (No: the starter cranking is silent.)"
                            : "The first Cabela's 4x4 Off-Road Adventure (2001) wasn't found. It's optional: it gives the truck its starter-cranking sounds.\n\nPick its folder? (No: the cranking is silent; everything else sounds the same.)",
                MB_YESNO | MB_ICONQUESTION) != IDYES ||
            !pick_folder("The first Cabela's 4x4 folder (it has Data\\data1.res in it)", cabelas1)) {
            cabelas1[0] = 0;
            break;
        }
    }
    if (cabelas1[0]) {
        path_join(name, cabelas1, "Data\\data1.res");
        if (!res_open(name)) fail("Data\\data1.res missing or damaged in the Cabela's 1 folder.");
        printf("Cabela's 1:  %s\n", cabelas1);
    } else {
        printf("Cabela's 1:  not used (optional): the 5 starter-crank sounds will be silent.\n");
    }

    // Writing starts here.
    writing = 1;
    path_join(name, mod_dir, "EngineDll.dll");
    if (!write_file(name, dll, dll_n)) fail("could not write EngineDll.dll into the mod folder (is the game running?).");
    printf("wrote %s\n", name);
    // msvcp60.dll (EngineDll needs it) from the player's own Cabela's 3.
    path_join(name, cabelas, "Bin\\msvcp60.dll");
    u8 *mp = read_file(name, &n);
    if (!mp) fail("Bin\\msvcp60.dll missing from the Cabela's 3 folder.");
    path_join(name, mod_dir, "msvcp60.dll");
    if (!write_file(name, mp, n)) fail("could not write msvcp60.dll into the mod folder (is the game running?).");
    printf("wrote %s\n", name);
    free(mp);
    free(dll);

    printf("\nbuilding trucks.bin\n");
    path_join(name, cabelas, "Cora3\\Cora3.arc");
    if (!arc_open(name)) fail("Cora3\\Cora3.arc could not be read.");
    path_join(sound_dir, mod_dir, "sounds");
    _mkdir(sound_dir);
    engine_sounds();
    textures_load();
    printf("wrote %d sound files into %s\n", sounds_written, sound_dir);
    font_glyphs();
    path_join(name, mod_dir, "trucks.bin");
    u32 size = build_trucks_bin((const char *)lua, lua_n, name, RED_RPM);
    free(lua);
    printf("wrote %s (%u bytes)\n", name, size);
    printf("\nwriting the texture pack\n");
    write_textures(mod_dir);
    printf("wrote %d textures and index.txt into %s\n", tex_count, tex_dir);
    printf("\nbuilding the maps\n");
    write_maps();
    printf("wrote %d maps into %s\\maps\n", maps_written, mod_dir);
    if (inexact) printf("  WARNING: %d numbers may be off in their last digit.\n", inexact);
    printf("\nsetup done\n");
    write_log();
    if (gui) {
        char b[800];
        sprintf(b, "SADX Truck is set up.\n\n"
                   "14 trucks, %d sounds, %d textures, %d of Cabela's maps.\n"
                   "Starter cranking sounds: %s\n\n"
                   "Turn on SADX Truck in SA Mod Manager and play. "
                   "(Run setup.exe again any time; it rebuilds the same files.)",
                sounds_written, tex_count, maps_written, cabelas1[0] ? "from the first Cabela's 4x4." : "silent (the first Cabela's 4x4 wasn't used).");
        box(b, MB_OK | MB_ICONINFORMATION);
    }
    ExitProcess(0);
}
