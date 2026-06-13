/* sspm.c — implementation, voir sspm.h. Format documente dans rythia/SSPM_FORMAT.md. */
#include "sspm.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- lecteurs little-endian --- */
static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}
static uint64_t rd_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}
static float rd_f32(const uint8_t *p) {
    uint32_t u = rd_u32(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

/* Lit une chaine [len u16][octets utf-8] a la position *cur, borne par sz. */
static bool read_varstr(const uint8_t *buf, long sz, long *cur, char *dst, int dstcap) {
    if (*cur + 2 > sz) return false;
    uint16_t len = rd_u16(buf + *cur);
    *cur += 2;
    if (*cur + (long)len > sz) return false;
    if (dst && dstcap > 0) {
        int n = (len < (uint16_t)(dstcap - 1)) ? (int)len : (dstcap - 1);
        memcpy(dst, buf + *cur, n);
        dst[n] = '\0';
    }
    *cur += len;
    return true;
}

static int cmp_note_ms(const void *a, const void *b) {
    float d = ((const SspmNote *)a)->ms - ((const SspmNote *)b)->ms;
    return (d > 0) - (d < 0);
}

/* Lit une chaine terminee par '\n' a la position *cur, bornee par sz.
 * dst peut etre NULL si on veut juste avancer le curseur. */
static void read_nlstr(const uint8_t *buf, long sz, long *cur,
                       char *dst, int dstcap) {
    int n = 0;
    while (*cur < sz) {
        uint8_t b = buf[(*cur)++];
        if (b == '\n') break;
        if (dst && n < dstcap - 1) dst[n++] = (char)b;
    }
    if (dst && dstcap > 0) dst[n] = '\0';
}

/* Concatene un mappeur a `dst` (champ de `cap` octets) en les separant par ", ". */
static void mapper_join(char *dst, size_t cap, const char *add) {
    if (dst[0] == '\0') { snprintf(dst, cap, "%s", add); return; }
    char joined[2 * 256];   /* nos champs font 256 -> pas de troncature ici */
    snprintf(joined, sizeof joined, "%s, %s", dst, add);
    joined[cap - 1] = '\0';
    memcpy(dst, joined, cap);
}

/* ===== scanner JSON minimal (cible le format JSON Rhythia ZIP) ================ */

/* Trouve "key": dans json et retourne un pointeur sur le debut de la valeur. */
static const char *jfind(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pat[300];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    int plen = (int)strlen(pat);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *after = p + plen;
        while (*after==' '||*after=='\t'||*after=='\r'||*after=='\n') after++;
        if (*after == ':') {
            after++;
            while (*after==' '||*after=='\t'||*after=='\r'||*after=='\n') after++;
            return after;
        }
        p++;
    }
    return NULL;
}

/* Extrait un entier JSON. */
static int jint(const char *v, int def) {
    if (!v) return def;
    while (*v==' ') v++;
    char *end; long r = strtol(v, &end, 10);
    return (end != v) ? (int)r : def;
}

/* Extrait un float JSON. */
static float jfloat(const char *v, float def) {
    if (!v) return def;
    while (*v==' ') v++;
    char *end; double r = strtod(v, &end);
    return (end != v) ? (float)r : def;
}

/* Avance apres une chaine JSON (suppose *p == '"'). */
static const char *jskipstr(const char *p) {
    if (!p || *p != '"') return p;
    p++;
    while (*p && *p != '"') { if (*p=='\\' && *(p+1)) p++; p++; }
    return (*p=='"') ? p+1 : p;
}

/* Extrait une chaine JSON vers dst[dstcap]. */
static void jstr(const char *v, char *dst, int dstcap) {
    if (!v || *v != '"' || dstcap <= 0) { if (dstcap>0) dst[0]='\0'; return; }
    v++;
    int n = 0;
    while (*v && *v != '"') {
        char c = *v++;
        if (c == '\\') {
            if (!*v) break;
            c = *v++;
            if (c=='n') c='\n'; else if (c=='t') c='\t';
        }
        if (n < dstcap-1) dst[n++] = c;
    }
    dst[n] = '\0';
}

/* Parse le tableau "Mappers":["a","b",...] et joint les noms par ", ". */
static void jmappers(const char *json, char *dst, int dstcap) {
    dst[0] = '\0';
    const char *v = jfind(json, "Mappers");
    if (!v || *v != '[') return;
    v++;
    while (*v) {
        while (*v==' '||*v=='\t'||*v=='\r'||*v=='\n'||*v==',') v++;
        if (!*v || *v == ']') break;
        if (*v == '"') {
            char tmp[256]; jstr(v, tmp, sizeof tmp);
            mapper_join(dst, (size_t)dstcap, tmp);
            v = jskipstr(v);
        } else v++;
    }
}

/* Compte le nombre d'objets dans le tableau "Notes":[{...},...]. */
static uint32_t jnote_count(const char *json) {
    const char *arr = jfind(json, "Notes");
    if (!arr || *arr != '[') return 0;
    uint32_t count = 0; int depth = 0;
    const char *p = arr + 1;
    while (*p) {
        if (*p=='"') { p = jskipstr(p); continue; }
        if (*p=='[') depth++;
        if (*p==']') { if (depth==0) break; depth--; }
        if (*p=='{' && depth==0) count++;
        p++;
    }
    return count;
}

/* Parse le tableau "Notes" dans out->notes (deja alloue, maxN entrees). */
static uint32_t jnotes_parse(const char *json, SspmNote *notes, uint32_t maxN) {
    const char *arr = jfind(json, "Notes");
    if (!arr || *arr != '[') return 0;
    const char *p = arr + 1;
    uint32_t got = 0;
    while (*p && got < maxN) {
        while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n'||*p==',') p++;
        if (!*p || *p==']') break;
        if (*p != '{') { p++; continue; }
        /* trouver la fin de cet objet */
        const char *obj = p; p++;
        while (*p && *p != '}') {
            if (*p=='"') p = jskipstr(p); else p++;
        }
        if (*p == '}') p++;
        /* copier l'objet dans un tampon pour y chercher les champs */
        int len = (int)(p - obj);
        char buf[256];
        int n = len < (int)sizeof(buf)-1 ? len : (int)sizeof(buf)-1;
        memcpy(buf, obj, n); buf[n] = '\0';
        const char *tv = jfind(buf, "Time");
        const char *xv = jfind(buf, "X");
        const char *yv = jfind(buf, "Y");
        if (tv && xv && yv) {
            notes[got].ms = (float)jint(tv, 0);
            notes[got].x  = jfloat(xv, 0.0f);
            notes[got].y  = jfloat(yv, 0.0f);
            got++;
        }
    }
    return got;
}

/* ===== chargement format ZIP+JSON (nouveau format Rhythia) =================== */

/* Charge metadata depuis un bloc JSON deja extrait. */
static void zip_fill_info_from_json(const char *json, SspmInfo *out) {
    const char *tv = jfind(json, "SongName");
    if (!tv || *tv != '"') tv = jfind(json, "Title");
    jstr(tv, out->songName, sizeof out->songName);
    snprintf(out->mapName, sizeof out->mapName, "%s", out->songName);
    jmappers(json, out->mapper, sizeof out->mapper);
    out->lastMs    = (uint32_t)jint(jfind(json, "Duration"), 0);
    out->difficulty = (uint8_t)jint(jfind(json, "Difficulty"), 0);
    out->noteCount  = jnote_count(json);
}

static bool sspm_load_info_zip(const uint8_t *fileData, long fileSize, SspmInfo *out) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, fileData, (size_t)fileSize, 0)) return false;
    out->hasAudio = (mz_zip_reader_locate_file(&zip, "audio", NULL, 0) >= 0);
    out->hasCover = (mz_zip_reader_locate_file(&zip, "cover", NULL, 0) >= 0);
    int map_idx = mz_zip_reader_locate_file(&zip, "map", NULL, 0);
    if (map_idx < 0) { mz_zip_reader_end(&zip); return false; }
    size_t jsize = 0;
    char *json = (char *)mz_zip_reader_extract_to_heap(&zip, (mz_uint)map_idx, &jsize, 0);
    mz_zip_reader_end(&zip);
    if (!json) return false;
    zip_fill_info_from_json(json, out);
    free(json);
    out->valid = true;
    return true;
}

static bool sspm_load_zip(const char *path, uint8_t *fileData, long fileSize, SspmMap *out) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, fileData, (size_t)fileSize, 0)) {
        fprintf(stderr, "Invalid ZIP: '%s'\n", path);
        return false;
    }
    /* --- extraire et parser le JSON --- */
    int map_idx = mz_zip_reader_locate_file(&zip, "map", NULL, 0);
    if (map_idx < 0) { fprintf(stderr, "Missing 'map' entry in '%s'\n", path); mz_zip_reader_end(&zip); return false; }
    size_t jsize = 0;
    char *json = (char *)mz_zip_reader_extract_to_heap(&zip, (mz_uint)map_idx, &jsize, 0);
    if (!json) { mz_zip_reader_end(&zip); return false; }

    const char *tv = jfind(json, "SongName");
    if (!tv || *tv != '"') tv = jfind(json, "Title");
    jstr(tv, out->songName, sizeof out->songName);
    snprintf(out->mapName, sizeof out->mapName, "%s", out->songName);
    jmappers(json, out->mapper, sizeof out->mapper);
    out->lastMs    = (uint32_t)jint(jfind(json, "Duration"), 0);
    out->difficulty = (uint8_t)jint(jfind(json, "Difficulty"), 0);

    /* determiner le format audio depuis AudioFileName */
    char afname[64] = "";
    const char *afv = jfind(json, "AudioFileName");
    if (afv) jstr(afv, afname, sizeof afname);

    /* allouer et parser les notes */
    uint32_t ncap = jnote_count(json);
    out->notes = (SspmNote *)malloc(sizeof(SspmNote) * (ncap ? ncap : 1));
    if (!out->notes) { free(json); mz_zip_reader_end(&zip); return false; }
    out->noteCountLoaded = jnotes_parse(json, out->notes, ncap);
    out->noteCount = out->noteCountLoaded;
    free(json);

    if (out->noteCountLoaded > 1)
        qsort(out->notes, out->noteCountLoaded, sizeof(SspmNote), cmp_note_ms);

    /* --- extraire l'audio --- */
    int aidx = mz_zip_reader_locate_file(&zip, "audio", NULL, 0);
    if (aidx >= 0) {
        size_t asz = 0;
        uint8_t *ab = (uint8_t *)mz_zip_reader_extract_to_heap(&zip, (mz_uint)aidx, &asz, 0);
        if (ab && asz > 0) {
            out->audio      = ab;
            out->audioLen   = (uint64_t)asz;
            out->hasAudio   = true;
            out->audioAllocd = true;
            /* format : depuis nom ou depuis octets magiques */
            const char *ext = strrchr(afname, '.');
            if (ext && (strcmp(ext,".ogg")==0||strcmp(ext,".OGG")==0))
                strcpy(out->audioExt, ".ogg");
            else if (asz >= 4 && ab[0]=='O'&&ab[1]=='g'&&ab[2]=='g'&&ab[3]=='S')
                strcpy(out->audioExt, ".ogg");
            else
                strcpy(out->audioExt, ".mp3");
        }
    }
    mz_zip_reader_end(&zip);

    /* fileData du fichier ZIP n'est plus utile (audio dans alloc separee) */
    free(out->fileData);
    out->fileData = NULL;
    out->fileSize = 0;
    return true;
}

/* --- parseur V1 (sequentiel, strings newline-terminated, pas de pointer block) --- */

static bool sspm_parse_v1(const uint8_t *buf, long sz, SspmMap *out) {
    long c = 8; /* magic(4) + version(2) + reserved(2) */

    char mapId[256];
    read_nlstr(buf, sz, &c, mapId,         sizeof mapId);
    read_nlstr(buf, sz, &c, out->mapName,  sizeof out->mapName);
    read_nlstr(buf, sz, &c, out->mapper,   sizeof out->mapper);
    /* V1 n'a pas de champ song_name separe : on prend mapName */
    snprintf(out->songName, sizeof out->songName, "%s", out->mapName);

    if (c + 10 > sz) return false;
    out->lastMs     = rd_u32(buf + c); c += 4;
    out->noteCount  = rd_u32(buf + c); c += 4;
    out->difficulty = buf[c++];

    /* cover optionnelle */
    if (c + 1 > sz) return false;
    uint8_t coverType = buf[c++];
    if (coverType == 2) {
        if (c + 8 > sz) return false;
        uint64_t covLen = rd_u64(buf + c); c += 8;
        if (c + (long)covLen > sz) return false;
        out->hasCover = true;
        c += (long)covLen;
    }

    /* audio optionnel */
    if (c + 1 > sz) return false;
    uint8_t audioType = buf[c++];
    if (audioType == 1) {
        if (c + 8 > sz) return false;
        uint64_t audLen = rd_u64(buf + c); c += 8;
        if (c + (long)audLen > sz) return false;
        out->audio    = buf + c;
        out->audioLen = audLen;
        out->hasAudio = true;
        const uint8_t *a = buf + c;
        if (audLen >= 4 && a[0]=='O' && a[1]=='g' && a[2]=='g' && a[3]=='S')
            strcpy(out->audioExt, ".ogg");
        else
            strcpy(out->audioExt, ".mp3");
        c += (long)audLen;
    }

    /* notes : V1 n'a PAS de byte marker_type, juste ms + quantum + coords */
    uint32_t n = out->noteCount;
    out->notes = (SspmNote *)malloc(sizeof(SspmNote) * (n ? n : 1));
    if (!out->notes) return false;
    uint32_t got = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (c + 5 > sz) break;
        uint32_t ts = rd_u32(buf + c); c += 4;
        uint8_t  quantum = buf[c]; c += 1;
        float x, y;
        if (quantum == 1) {
            if (c + 8 > sz) break;
            x = rd_f32(buf + c); c += 4;
            y = rd_f32(buf + c); c += 4;
        } else {
            if (c + 2 > sz) break;
            x = (float)buf[c]; c += 1;
            y = (float)buf[c]; c += 1;
        }
        out->notes[got].ms = (float)ts;
        out->notes[got].x  = x;
        out->notes[got].y  = y;
        got++;
    }
    out->noteCountLoaded = got;
    if (got > 1) qsort(out->notes, got, sizeof(SspmNote), cmp_note_ms);
    return true;
}

static bool sspm_parse_v1_info(const uint8_t *buf, long sz, SspmInfo *out) {
    long c = 8;
    char mapId[256];
    read_nlstr(buf, sz, &c, mapId,        sizeof mapId);
    read_nlstr(buf, sz, &c, out->mapName, sizeof out->mapName);
    read_nlstr(buf, sz, &c, out->mapper,  sizeof out->mapper);
    snprintf(out->songName, sizeof out->songName, "%s", out->mapName);

    if (c + 10 > sz) return false;
    out->lastMs     = rd_u32(buf + c); c += 4;
    out->noteCount  = rd_u32(buf + c); c += 4;
    out->difficulty = buf[c++];

    /* on tente de lire les flags cover/audio si le buffer le permet */
    if (c + 1 > sz) { out->valid = true; return true; }
    uint8_t coverType = buf[c++];
    out->hasCover = (coverType == 2);
    if (coverType == 2) {
        if (c + 8 > sz) { out->valid = true; return true; }
        uint64_t covLen = rd_u64(buf + c); c += 8;
        c += (long)covLen;   /* peut depasser le prefixe lu : on accepte */
    }
    if (c + 1 > sz) { out->valid = true; return true; }
    uint8_t audioType = buf[c++];
    out->hasAudio = (audioType == 1);

    out->valid = true;
    return true;
}

bool sspm_load(const char *path, SspmMap *out) {
    memset(out, 0, sizeof *out);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open '%s'\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0x80) {
        fprintf(stderr, "File too small to be a .sspm (%ld bytes)\n", sz);
        fclose(f);
        return false;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fprintf(stderr, "Out of memory (%ld bytes)\n", sz);
        fclose(f);
        return false;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "Incomplete read of '%s'\n", path);
        free(buf);
        fclose(f);
        return false;
    }
    fclose(f);
    out->fileData = buf;
    out->fileSize = sz;

    /* --- detection format ZIP (nouveau format Rhythia : PK\x03\x04) --- */
    if (buf[0]=='P' && buf[1]=='K' && buf[2]==0x03 && buf[3]==0x04) {
        bool ok = sspm_load_zip(path, buf, sz, out);
        if (!ok) sspm_free(out);
        return ok;
    }

    /* --- header (10 o) : signature "SS+m" + version u16 --- */
    if (!(buf[0] == 0x53 && buf[1] == 0x53 && buf[2] == 0x2B && buf[3] == 0x6D)) {
        fprintf(stderr, "Invalid SSPM signature (not a .sspm)\n");
        sspm_free(out);
        return false;
    }
    uint16_t version = rd_u16(buf + 4);
    if (version == 1) {
        bool ok = sspm_parse_v1(buf, sz, out);
        if (!ok) { sspm_free(out); fprintf(stderr, "Error reading SSPM V1 '%s'\n", path); return false; }
        return true;
    }
    if (version != 2) {
        fprintf(stderr, "SSPM version %u not supported\n", version);
        sspm_free(out);
        return false;
    }

    /* --- metadata @ 0x1E (18 o) --- */
    const uint8_t *m = buf + 0x1E;
    out->lastMs     = rd_u32(m + 0);
    out->noteCount  = rd_u32(m + 4);
    /* m+8 : marker_count (ignore, == noteCount en pratique) */
    out->difficulty = m[12];
    /* m+13 : map_rating u16 (ignore) */
    out->hasAudio   = (m[15] == 1);
    out->hasCover   = (m[16] == 1);
    /* m[17] : requires_mod (ignore) */

    /* --- pointers @ 0x30 (80 o = 10 x u64) --- */
    const uint8_t *p = buf + 0x30;
    uint64_t audioOff  = rd_u64(p + 16);
    uint64_t audioLen  = rd_u64(p + 24);
    uint64_t markerOff = rd_u64(p + 64);

    /* --- strings @ 0x80 --- */
    long cur = 0x80;
    char mapId[256];
    if (!read_varstr(buf, sz, &cur, mapId, sizeof mapId) ||
        !read_varstr(buf, sz, &cur, out->mapName, sizeof out->mapName) ||
        !read_varstr(buf, sz, &cur, out->songName, sizeof out->songName)) {
        fprintf(stderr, "Error reading strings\n");
        sspm_free(out);
        return false;
    }
    if (cur + 2 > sz) {
        fprintf(stderr, "Error reading mapper count\n");
        sspm_free(out);
        return false;
    }
    uint16_t mapperCount = rd_u16(buf + cur);
    cur += 2;
    out->mapper[0] = '\0';
    for (uint16_t i = 0; i < mapperCount; i++) {
        char tmp[256];
        if (!read_varstr(buf, sz, &cur, tmp, sizeof tmp)) break;
        mapper_join(out->mapper, sizeof out->mapper, tmp);
    }

    /* --- audio embarque (zero-copy : pointe dans buf) --- */
    if (out->hasAudio && audioLen > 0 && audioOff + audioLen <= (uint64_t)sz) {
        out->audio    = buf + audioOff;
        out->audioLen = audioLen;
        const uint8_t *a = out->audio;
        if (audioLen >= 4 && a[0] == 'O' && a[1] == 'g' && a[2] == 'g' && a[3] == 'S')
            strcpy(out->audioExt, ".ogg");
        else
            strcpy(out->audioExt, ".mp3"); /* frame-sync 0xFFEx ou tag "ID3" */
    }

    /* --- notes @ markerOff --- */
    uint32_t n = out->noteCount;
    out->notes = (SspmNote *)malloc(sizeof(SspmNote) * (n ? n : 1));
    if (!out->notes) {
        fprintf(stderr, "Out of memory for %u notes\n", n);
        sspm_free(out);
        return false;
    }
    long c = (long)markerOff;
    uint32_t got = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (c + 6 > sz) break;
        uint32_t ts = rd_u32(buf + c); c += 4;
        /* buf[c] = marker_type (ignore) */ c += 1;
        uint8_t quantum = buf[c]; c += 1;
        float x, y;
        if (quantum == 1) {
            if (c + 8 > sz) break;
            x = rd_f32(buf + c); c += 4;
            y = rd_f32(buf + c); c += 4;
        } else {
            if (c + 2 > sz) break;
            x = (float)buf[c]; c += 1;
            y = (float)buf[c]; c += 1;
        }
        out->notes[got].ms = (float)ts;
        out->notes[got].x  = x;
        out->notes[got].y  = y;
        got++;
    }
    out->noteCountLoaded = got;

    /* tri par timestamp (le rendu suppose des notes ordonnees) */
    if (got > 1) qsort(out->notes, got, sizeof(SspmNote), cmp_note_ms);

    return true;
}

void sspm_free(SspmMap *m) {
    if (!m) return;
    free(m->notes);
    m->notes = NULL;
    /* format ZIP : audio est dans une allocation separee */
    if (m->audioAllocd) { free((void *)m->audio); m->audio = NULL; m->audioAllocd = false; }
    /* format binaire : audio pointe dans fileData, liberer fileData en dernier */
    free(m->fileData);
    m->fileData = NULL;
    m->audio = NULL;
}

uint64_t sspm_load_cover(const char *path, uint8_t **dataOut) {
    *dataOut = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    uint8_t hdr[0xb0];
    long nrd = (long)fread(hdr, 1, sizeof hdr, f);

    /* --- format ZIP --- */
    if (nrd >= 4 && hdr[0]=='P' && hdr[1]=='K' && hdr[2]==0x03 && hdr[3]==0x04) {
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *full = (uint8_t *)malloc((size_t)sz);
        if (!full) { fclose(f); return 0; }
        if ((long)fread(full, 1, (size_t)sz, f) != sz) { free(full); fclose(f); return 0; }
        fclose(f);
        mz_zip_archive zip; memset(&zip, 0, sizeof zip);
        if (!mz_zip_reader_init_mem(&zip, full, (size_t)sz, 0)) { free(full); return 0; }
        int cidx = mz_zip_reader_locate_file(&zip, "cover", NULL, 0);
        if (cidx < 0) { mz_zip_reader_end(&zip); free(full); return 0; }
        size_t csz = 0;
        uint8_t *cdata = (uint8_t *)mz_zip_reader_extract_to_heap(&zip, (mz_uint)cidx, &csz, 0);
        mz_zip_reader_end(&zip); free(full);
        if (!cdata || csz == 0) { free(cdata); return 0; }
        *dataOut = cdata;
        return (uint64_t)csz;
    }

    /* --- format SSPM binaire --- */
    if (nrd >= 6 && hdr[0]==0x53 && hdr[1]==0x53 && hdr[2]==0x2B && hdr[3]==0x6D) {
        uint16_t ver = rd_u16(hdr + 4);

        /* V2 : la cover est reference par le bloc de pointeurs a 0x30 */
        if (ver == 2 && nrd >= 0x40) {
            if (hdr[0x2E] != 1) { fclose(f); return 0; }  /* hasCover flag */
            uint64_t covOff = rd_u64(hdr + 0x30);
            uint64_t covLen = rd_u64(hdr + 0x38);
            if (covLen == 0 || covOff == 0) { fclose(f); return 0; }
            uint8_t *data = (uint8_t *)malloc((size_t)covLen);
            if (!data) { fclose(f); return 0; }
            if (fseek(f, (long)covOff, SEEK_SET) != 0 ||
                (uint64_t)fread(data, 1, (size_t)covLen, f) != covLen) {
                free(data); fclose(f); return 0;
            }
            fclose(f);
            *dataOut = data;
            return covLen;
        }

        /* V1 : lecture sequentielle jusqu'a la section cover */
        if (ver == 1) {
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            uint8_t *buf = (uint8_t *)malloc((size_t)sz);
            if (!buf) { fclose(f); return 0; }
            if ((long)fread(buf, 1, (size_t)sz, f) != sz) { free(buf); fclose(f); return 0; }
            fclose(f);
            long c = 8;
            char tmp[256];
            read_nlstr(buf, sz, &c, tmp, sizeof tmp);  /* mapId   */
            read_nlstr(buf, sz, &c, tmp, sizeof tmp);  /* mapName */
            read_nlstr(buf, sz, &c, tmp, sizeof tmp);  /* mapper  */
            if (c + 10 > sz) { free(buf); return 0; }
            c += 9;                              /* lastMs(4) + noteCount(4) + difficulty(1) */
            if (buf[c++] != 2) { free(buf); return 0; }  /* coverType : 2 = present */
            if (c + 8 > sz) { free(buf); return 0; }
            uint64_t covLen = rd_u64(buf + c); c += 8;
            if (covLen == 0 || c + (long)covLen > sz) { free(buf); return 0; }
            uint8_t *data = (uint8_t *)malloc((size_t)covLen);
            if (!data) { free(buf); return 0; }
            memcpy(data, buf + c, (size_t)covLen);
            free(buf);
            *dataOut = data;
            return covLen;
        }
    }

    fclose(f);
    return 0;
}

bool sspm_load_info(const char *path, SspmInfo *out) {
    memset(out, 0, sizeof *out);

    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0x80) { fclose(f); return false; }

    /* On ne lit qu'un prefixe : l'en-tete + les chaines tiennent largement dedans,
     * inutile de charger l'audio/les notes juste pour afficher un nom. */
    long cap = sz < 65536 ? sz : 65536;
    uint8_t *buf = (uint8_t *)malloc((size_t)cap);
    if (!buf) { fclose(f); return false; }
    long rd = (long)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    if (rd < 0x80) { free(buf); return false; }

    /* --- format ZIP (nouveau format Rhythia) : besoin du fichier entier --- */
    if (buf[0]=='P' && buf[1]=='K' && buf[2]==0x03 && buf[3]==0x04) {
        free(buf);
        /* recharger le fichier en entier pour miniz */
        FILE *f2 = fopen(path, "rb");
        if (!f2) return false;
        uint8_t *full = (uint8_t *)malloc((size_t)sz);
        if (!full) { fclose(f2); return false; }
        bool ok = false;
        if ((long)fread(full, 1, (size_t)sz, f2) == sz)
            ok = sspm_load_info_zip(full, sz, out);
        fclose(f2);
        free(full);
        return ok;
    }

    if (!(buf[0] == 0x53 && buf[1] == 0x53 && buf[2] == 0x2B && buf[3] == 0x6D)) { free(buf); return false; }
    uint16_t ver = rd_u16(buf + 4);
    if (ver == 1) {
        bool ok = sspm_parse_v1_info(buf, rd, out);
        free(buf);
        return ok;
    }
    if (ver != 2) { free(buf); return false; }

    const uint8_t *m = buf + 0x1E;
    out->lastMs     = rd_u32(m + 0);
    out->noteCount  = rd_u32(m + 4);
    out->difficulty = m[12];
    out->hasAudio   = (m[15] == 1);
    out->hasCover   = (m[16] == 1);

    long cur = 0x80;
    char mapId[256];
    if (!read_varstr(buf, rd, &cur, mapId, sizeof mapId) ||
        !read_varstr(buf, rd, &cur, out->mapName, sizeof out->mapName) ||
        !read_varstr(buf, rd, &cur, out->songName, sizeof out->songName)) { free(buf); return false; }

    if (cur + 2 <= rd) {
        uint16_t mc = rd_u16(buf + cur); cur += 2;
        for (uint16_t i = 0; i < mc; i++) {
            char tmp[256];
            if (!read_varstr(buf, rd, &cur, tmp, sizeof tmp)) break;
            mapper_join(out->mapper, sizeof out->mapper, tmp);
        }
    }

    out->valid = true;
    free(buf);
    return true;
}

const char *sspm_difficulty_name(uint8_t d) {
    switch (d) {
        case 1:  return "Easy";
        case 2:  return "Medium";
        case 3:  return "Hard";
        case 4:  return "Logic";
        case 5:  return "Tasukete";
        default: return "N/A";
    }
}
