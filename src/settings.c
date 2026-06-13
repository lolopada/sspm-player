/*
 * settings.c — Persistance (Settings + Profile), assets optionnels
 *              (meshes, curseurs, hitsounds) et application des reglages.
 */
#include "common.h"

/* =========================================================================
 * Mods : multiplicateur de score (besoin de settings_apply)
 * ========================================================================= */

float mods_score_mult(unsigned mods, float rate) {
    float m = 1.0f;
    if (mods & MOD_HARDROCK) m *= 1.10f;
    if (mods & MOD_HIDDEN)   m *= 1.06f;
    if (mods & MOD_NOFAIL)     m *= 0.50f;
    if (mods & MOD_FLASHLIGHT) m *= 1.12f;
    if (rate > 1.0f)      m *= 1.0f + (rate - 1.0f) * 0.5f;
    else if (rate < 1.0f) m *= rate;
    return m;
}

/* =========================================================================
 * Palettes predefinies
 * ========================================================================= */

static const Palette PALETTES[] = {
    { "Pink / Blue", 2, { { 255, 90, 140, 255 }, { 80, 170, 255, 255 } } },
    { "Rainbow",     6, { { 255, 80, 80, 255 }, { 255, 170, 60, 255 }, { 255, 230, 90, 255 },
                          { 90, 220, 120, 255 }, { 80, 170, 255, 255 }, { 180, 110, 255, 255 } } },
    { "Fire",        3, { { 255, 90, 60, 255 }, { 255, 160, 40, 255 }, { 255, 225, 90, 255 } } },
    { "Ice",         3, { { 120, 230, 255, 255 }, { 90, 170, 255, 255 }, { 170, 140, 255, 255 } } },
    { "Neon",        4, { { 255, 40, 160, 255 }, { 40, 255, 200, 255 }, { 255, 240, 60, 255 }, { 120, 90, 255, 255 } } },
    { "Mono white",  1, { { 240, 240, 255, 255 } } },
};
const int NPALETTE = (int)(sizeof(PALETTES) / sizeof(PALETTES[0]));

/* =========================================================================
 * Meshes personnalises
 * ========================================================================= */

void meshlist_scan(const char *dir) {
    gMeshCount = 0;
    snprintf(gMeshDir, sizeof gMeshDir, "%s", (dir && dir[0]) ? dir : "meshes");
    if (!DirectoryExists(gMeshDir)) return;
    FilePathList l = LoadDirectoryFilesEx(gMeshDir, ".obj;.glb;.gltf;.iqm;.vox;.m3d", false);
    for (unsigned int i = 0; i < l.count && gMeshCount < MAX_MESHES; i++) {
        snprintf(gMeshNames[gMeshCount], 128, "%s", GetFileName(l.paths[i]));
        gMeshCount++;
    }
    UnloadDirectoryFiles(l);
}

int mesh_index_of(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < gMeshCount; i++)
        if (strcmp(gMeshNames[i], name) == 0) return i + 1;
    return 0;
}
const char *mesh_name_at(int idx) {
    if (idx <= 0 || idx > gMeshCount) return "";
    return gMeshNames[idx - 1];
}

void notemesh_clear(void) {
    if (gNoteMesh.active) { UnloadModel(gNoteMesh.model); gNoteMesh.active = false; }
}

/* Chargeur OBJ maison : lit les positions (v) et triangule les faces (fan), en
 * ignorant normales/UV (pas d'eclairage : notes en aplat teinte). On evite ainsi
 * le chargeur tinyobj de raylib, qui peut planter sur certains .obj (n-gons,
 * objets multiples, etc.). Construit un mesh non-indexe (3 sommets/triangle), ce
 * qui gere aussi les faces a plus de 65535 sommets sans souci d'index 16 bits. */
static bool load_obj_model(const char *path, Model *out) {
    char *txt = LoadFileText(path);
    if (!txt) return false;

    int    vcap = 256, vcount = 0;
    float *vpos = (float *)malloc(sizeof(float) * (size_t)vcap * 3);
    int    tcap = 256, tcount = 0;
    int   *tri  = (int *)malloc(sizeof(int) * (size_t)tcap * 3);
    if (!vpos || !tri) { free(vpos); free(tri); UnloadFileText(txt); return false; }

    char *p = txt;
    while (*p) {
        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            float a, b, c;
            if (sscanf(p + 1, "%f %f %f", &a, &b, &c) == 3) {
                if (vcount >= vcap) {
                    vcap *= 2;
                    float *nv = (float *)realloc(vpos, sizeof(float) * (size_t)vcap * 3);
                    if (!nv) { free(vpos); free(tri); UnloadFileText(txt); return false; }
                    vpos = nv;
                }
                vpos[vcount*3+0] = a; vpos[vcount*3+1] = b; vpos[vcount*3+2] = c; vcount++;
            }
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            int i0 = -1, iprev = -1, fv = 0;
            char *q = p + 1;
            while (*q && *q != '\n' && *q != '\r') {
                while (*q == ' ' || *q == '\t') q++;
                if (!*q || *q == '\n' || *q == '\r') break;
                int sign = 1; if (*q == '-') { sign = -1; q++; }
                int val = 0; bool any = false;
                while (*q >= '0' && *q <= '9') { val = val*10 + (*q - '0'); q++; any = true; }
                while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
                if (!any) continue;
                int pi = (sign < 0) ? (vcount - val) : (val - 1);
                if (fv == 0) i0 = pi;
                else if (fv == 1) iprev = pi;
                else {
                    if (tcount >= tcap) {
                        tcap *= 2;
                        int *nt = (int *)realloc(tri, sizeof(int) * (size_t)tcap * 3);
                        if (!nt) { free(vpos); free(tri); UnloadFileText(txt); return false; }
                        tri = nt;
                    }
                    tri[tcount*3+0] = i0; tri[tcount*3+1] = iprev; tri[tcount*3+2] = pi; tcount++;
                    iprev = pi;
                }
                fv++;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    UnloadFileText(txt);

    if (vcount == 0 || tcount == 0) { free(vpos); free(tri); return false; }

    Mesh mesh = { 0 };
    mesh.triangleCount = tcount;
    mesh.vertexCount   = tcount * 3;
    mesh.vertices = (float *)malloc(sizeof(float) * (size_t)mesh.vertexCount * 3);
    if (!mesh.vertices) { free(vpos); free(tri); return false; }
    for (int t = 0; t < tcount; t++) {
        for (int j = 0; j < 3; j++) {
            int pi = tri[t*3+j];
            float x = 0, y = 0, z = 0;
            if (pi >= 0 && pi < vcount) { x = vpos[pi*3+0]; y = vpos[pi*3+1]; z = vpos[pi*3+2]; }
            int o = (t*3 + j) * 3;
            mesh.vertices[o+0] = x; mesh.vertices[o+1] = y; mesh.vertices[o+2] = z;
        }
    }
    free(vpos); free(tri);

    UploadMesh(&mesh, false);
    *out = LoadModelFromMesh(mesh);
    return true;
}

bool notemesh_set(const char *name) {
    notemesh_clear();
    gMeshMsg[0] = '\0';
    if (!name || !name[0]) return true;
    char full[1280];
    snprintf(full, sizeof full, "%s/%s", gMeshDir, name);
    if (!FileExists(full)) { snprintf(gMeshMsg, sizeof gMeshMsg, "File not found."); return false; }

    Model m; bool ok;
    if (IsFileExtension(full, ".obj")) {
        ok = load_obj_model(full, &m);
    } else {
        m = LoadModel(full);
        ok = (m.meshCount > 0 && m.meshes != NULL);
        if (!ok) UnloadModel(m);
    }
    if (!ok) {
        snprintf(gMeshMsg, sizeof gMeshMsg, "Cannot load (empty or invalid model).");
        return false;
    }
    BoundingBox bb = GetModelBoundingBox(m);
    float dx = bb.max.x - bb.min.x, dy = bb.max.y - bb.min.y, dz = bb.max.z - bb.min.z;
    float md = dx > dy ? dx : dy; if (dz > md) md = dz;
    gNoteMesh.model  = m;
    gNoteMesh.scale  = (md > 0.0001f) ? (CUBE_SIZE / md) : 1.0f;
    gNoteMesh.center = (Vector3){ (bb.min.x + bb.max.x) * 0.5f,
                                  (bb.min.y + bb.max.y) * 0.5f,
                                  (bb.min.z + bb.max.z) * 0.5f };
    gNoteMesh.active = true;
    return true;
}

/* =========================================================================
 * Curseurs personnalises
 * ========================================================================= */

void cursorlist_scan(const char *dir) {
    gCursorCount = 0;
    snprintf(gCursorDir, sizeof gCursorDir, "%s", (dir && dir[0]) ? dir : "cursors");
    if (!DirectoryExists(gCursorDir)) return;
    FilePathList l = LoadDirectoryFilesEx(gCursorDir, ".png", false);
    for (unsigned int i = 0; i < l.count && gCursorCount < MAX_CURSORS; i++) {
        snprintf(gCursorNames[gCursorCount], 128, "%s", GetFileName(l.paths[i]));
        gCursorCount++;
    }
    UnloadDirectoryFiles(l);
}

int cursor_index_of(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < gCursorCount; i++)
        if (strcmp(gCursorNames[i], name) == 0) return i + 1;
    return 0;
}
const char *cursor_name_at(int idx) {
    if (idx <= 0 || idx > gCursorCount) return "";
    return gCursorNames[idx - 1];
}

void cursortex_clear(void) {
    if (gCursorTex.active) { UnloadTexture(gCursorTex.tex); gCursorTex.active = false; }
}

bool cursortex_set(const char *name) {
    cursortex_clear();
    gCursorMsg[0] = '\0';
    if (!name || !name[0]) return true;
    char full[1280];
    snprintf(full, sizeof full, "%s/%s", gCursorDir, name);
    if (!FileExists(full)) { snprintf(gCursorMsg, sizeof gCursorMsg, "File not found."); return false; }
    Texture2D t = LoadTexture(full);
    if (t.id == 0) { snprintf(gCursorMsg, sizeof gCursorMsg, "Cannot load (invalid PNG)."); return false; }
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    gCursorTex.tex = t;
    gCursorTex.active = true;
    return true;
}

/* =========================================================================
 * Hitsounds
 * ========================================================================= */

void hitsoundlist_scan(const char *dir) {
    gHitsoundCount = 0;
    snprintf(gHitsoundDir, sizeof gHitsoundDir, "%s", (dir && dir[0]) ? dir : "hitsounds");
    if (!DirectoryExists(gHitsoundDir)) return;
    static const char *const EXTS[] = { ".wav", ".ogg", ".mp3", ".flac", NULL };
    for (int ei = 0; EXTS[ei] && gHitsoundCount < MAX_HITSOUNDS; ei++) {
        FilePathList l = LoadDirectoryFilesEx(gHitsoundDir, EXTS[ei], false);
        for (unsigned int i = 0; i < l.count && gHitsoundCount < MAX_HITSOUNDS; i++) {
            snprintf(gHitsoundNames[gHitsoundCount], 128, "%s", GetFileName(l.paths[i]));
            gHitsoundCount++;
        }
        UnloadDirectoryFiles(l);
    }
}
int hitsound_index_of(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < gHitsoundCount; i++)
        if (strcmp(gHitsoundNames[i], name) == 0) return i + 1;
    return 0;
}
const char *hitsound_name_at(int idx) {
    if (idx <= 0 || idx > gHitsoundCount) return "";
    return gHitsoundNames[idx - 1];
}
void hitsound_clear(void) {
    if (gHitsoundSnd.active) { UnloadSound(gHitsoundSnd.snd); gHitsoundSnd.active = false; }
}
bool hitsound_set(const char *name) {
    hitsound_clear();
    gHitsoundMsg[0] = '\0';
    if (!name || !name[0]) return true;
    char full[1280];
    snprintf(full, sizeof full, "%s/%s", gHitsoundDir, name);
    if (!FileExists(full)) { snprintf(gHitsoundMsg, sizeof gHitsoundMsg, "File not found."); return false; }
    Sound s = LoadSound(full);
    if (s.stream.buffer == NULL) { snprintf(gHitsoundMsg, sizeof gHitsoundMsg, "Cannot load."); return false; }
    gHitsoundSnd.snd    = s;
    gHitsoundSnd.active = true;
    return true;
}

/* =========================================================================
 * Settings : palette, apply, defaults, load, save
 * ========================================================================= */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool parse_one_color(const char *s, Color *out) {
    int v[6];
    for (int i = 0; i < 6; i++) { v[i] = hexval(s[i]); if (v[i] < 0) return false; }
    out->r = (unsigned char)(v[0] * 16 + v[1]);
    out->g = (unsigned char)(v[2] * 16 + v[3]);
    out->b = (unsigned char)(v[4] * 16 + v[5]);
    out->a = 255;
    return true;
}
static int parse_hex_colors(const char *val, Color *out, int maxn) {
    int n = 0; const char *p = val;
    while (*p && n < maxn) {
        while (*p == ' ' || *p == ',' || *p == '#') p++;
        const char *q = p; int cnt = 0;
        while (*q && hexval(*q) >= 0) { cnt++; q++; }
        if (cnt >= 6) { Color c; if (parse_one_color(p, &c)) out[n++] = c; }
        p = q;
        if (!*p) break;
    }
    return n;
}

/* Lit une seule couleur "rrggbb" (avec ou sans #) ; renvoie fallback si invalide. */
static Color parse_color_hex(const char *val, Color fallback) {
    Color out[1];
    if (parse_hex_colors(val, out, 1) == 1) { out[0].a = 255; return out[0]; }
    return fallback;
}

/* =========================================================================
 * Chaines de couleurs personnalisees (dossier colors/)
 * ========================================================================= */

void colorchain_scan(const char *dir) {
    gColorChainCount = 0;
    snprintf(gColorChainDir, sizeof gColorChainDir, "%s", (dir && dir[0]) ? dir : "colors");
    if (!DirectoryExists(gColorChainDir)) return;
    FilePathList l = LoadDirectoryFilesEx(gColorChainDir, ".txt", false);
    for (unsigned int i = 0; i < l.count && gColorChainCount < MAX_COLOR_CHAINS; i++) {
        const char *fname = GetFileName(l.paths[i]);
        if (strcmp(fname, "LISEZMOI.txt") == 0 || strcmp(fname, "README.txt") == 0) continue;
        ColorChain *cc = &gColorChains[gColorChainCount];
        snprintf(cc->filename, sizeof cc->filename, "%s", fname);
        snprintf(cc->name, sizeof cc->name, "%s", GetFileNameWithoutExt(l.paths[i]));
        cc->len = 0;
        char *txt = LoadFileText(l.paths[i]);
        if (txt) {
            char tmp[16384]; snprintf(tmp, sizeof tmp, "%s", txt);
            UnloadFileText(txt);
            for (char *line = strtok(tmp, "\r\n"); line; line = strtok(NULL, "\r\n")) {
                if (line[0] == '#' || line[0] == '\0') continue;
                char *eq = strchr(line, '='); if (!eq) continue;
                *eq = '\0'; const char *key = line, *val = eq + 1;
                if      (strcmp(key, "name")   == 0) snprintf(cc->name, sizeof cc->name, "%s", val);
                else if (strcmp(key, "colors") == 0) { int n = parse_hex_colors(val, cc->cols, MAX_PALETTE); if (n > 0) cc->len = n; }
            }
        }
        if (cc->len > 0) gColorChainCount++;
    }
    UnloadDirectoryFiles(l);
}

int colorchain_index_of(const char *filename) {
    if (!filename || !filename[0]) return -1;
    for (int i = 0; i < gColorChainCount; i++)
        if (strcmp(gColorChains[i].filename, filename) == 0) return i;
    return -1;
}


void settings_palette(const Settings *s, Color *out, int *len) {
    if (s->paletteIdx >= 0 && s->paletteIdx < NPALETTE) {
        int n = PALETTES[s->paletteIdx].len;
        for (int i = 0; i < n; i++) out[i] = PALETTES[s->paletteIdx].cols[i];
        *len = n;
    } else if (s->paletteIdx == NPALETTE) {
        if (s->customLen > 0) {
            for (int i = 0; i < s->customLen; i++) out[i] = s->customPal[i];
            *len = s->customLen;
        } else {
            out[0] = PALETTES[0].cols[0]; out[1] = PALETTES[0].cols[1]; *len = 2;
        }
    } else {
        /* chaine de couleur externe (fichier .txt dans colors/) */
        int ci = colorchain_index_of(s->colorChainFile);
        if (ci >= 0 && gColorChains[ci].len > 0) {
            for (int i = 0; i < gColorChains[ci].len; i++) out[i] = gColorChains[ci].cols[i];
            *len = gColorChains[ci].len;
        } else {
            out[0] = PALETTES[0].cols[0]; out[1] = PALETTES[0].cols[1]; *len = 2;
        }
    }
}
const char *settings_palette_name(const Settings *s) {
    if (s->paletteIdx >= 0 && s->paletteIdx < NPALETTE) return PALETTES[s->paletteIdx].name;
    if (s->paletteIdx == NPALETTE) return "Custom (colors=)";
    int ci = colorchain_index_of(s->colorChainFile);
    if (ci >= 0) return gColorChains[ci].name;
    return s->colorChainFile[0] ? s->colorChainFile : "Unknown chain";
}
void settings_apply(const Settings *s) {
    gApproachDist = s->approachDist;
    gApproachMs   = s->approachMs;
    gAudioOffsetMs = s->audioOffsetMs;
    gJuiceMode    = s->juiceMode;
    gJuiceCount   = s->juiceCount;
    gJuiceLife    = s->juiceLife;
    gJuiceSize    = s->juiceSize;
    gJuiceSpeed   = s->juiceSpeed;
    gJuicePulse   = s->juicePulse;
    gJuiceCombo   = s->juiceCombo;
    gTablet       = s->tablet;
    settings_palette(s, gPalette, &gPaletteLen);
    if (gPaletteLen > MAX_PALETTE) gPaletteLen = MAX_PALETTE;
    if (gPaletteLen < 1) gPaletteLen = 1;
    gGodMode     = s->godMode;
    gHitWindowMs = s->hitWindowMs;
    gHueShift    = s->hueShift;
    gSens          = SENS * s->sensMultiplier;
    gGridAlpha       = s->gridAlpha;
    gGridStyle       = s->gridStyle;
    gNoteScale       = s->noteScale;
    gHitsoundVolume  = s->hitsoundVolume;
    gCursor       = s->cursor;        /* apparence curseur + trainee (lu chaque frame) */
    gCursorMode   = s->cursorMode;
    gMusicVolume  = s->musicVolume;
    gKeys         = s->keys;
    gMods            = (unsigned)s->mods;
    gRate            = clampf(s->rate, 0.5f, 2.0f);
    gScoreMult       = mods_score_mult(gMods, gRate);
    if (s->vsync) { SetWindowState(FLAG_VSYNC_HINT); }
    else          { ClearWindowState(FLAG_VSYNC_HINT); }
    SetTargetFPS(0);
    gCullBehind  = gApproachDist * gHitWindowMs / gApproachMs + 2.0f;
    { static const int RW[3] = {0,1280,854}, RH[3] = {0,720,480};
      int ri = (s->internalRes >= 0 && s->internalRes < 3) ? s->internalRes : 0;
      int nw = RW[ri], nh = RH[ri];
      if (nw != gRtW || nh != gRtH) {
          if (gRenderTex.texture.id > 0) UnloadRenderTexture(gRenderTex);
          gRtW = nw; gRtH = nh;
          if (gRtW > 0) gRenderTex = LoadRenderTexture(gRtW, gRtH);
      } }
}

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof *s);
    s->approachDist = APPROACH_DIST;
    s->approachMs   = APPROACH_MS;
    s->tablet       = false;
    s->paletteIdx   = 0;
    s->meshName[0]     = '\0';
    s->cursorName[0]   = '\0';
    s->cursorMode   = 0;                 /* genere (rond + halo) par defaut */
    cursor_config_defaults(&s->cursor);  /* couleurs/halo/trainee de reference */
    s->musicVolume  = 1.0f;
    s->godMode         = false;
    s->hitWindowMs     = HIT_EARLY;
    s->hueShift        = 0;
    s->sensMultiplier  = 1.0f;
    s->vsync           = true;
    s->gridAlpha       = 90;
    s->gridStyle       = 0;
    s->noteScale       = 1.0f;
    s->hitsoundName[0] = '\0';
    s->hitsoundVolume  = 0.8f;
    s->internalRes     = 0;
    s->audioOffsetMs   = 0.0f;
    s->juiceMode       = 1;
    s->juiceCount      = 14;
    s->juiceLife       = 0.50f;
    s->juiceSize       = 0.14f;
    s->juiceSpeed      = 1.00f;
    s->juicePulse      = 1.00f;
    s->juiceCombo      = 50;
    s->mods            = 0;
    s->rate            = 1.0f;
    s->aim.density     = 3.0f;
    s->aim.approachMs  = 550.0f;
    s->aim.style       = AS_MIX;
    s->aim.segLen      = 10;
    s->aim.radius      = 0.90f;
    s->aim.size        = 1.0f;
    s->aim.hitWindowMs = 120.0f;
    s->aim.durationSec = 90;
    s->aim.accelPct    = 0;
    s->keys.quit           = KEY_ESCAPE;
    s->keys.pause          = KEY_SPACE;
    s->keys.restart        = KEY_R;
    s->keys.skipIntro      = KEY_TAB;
    s->keys.menuSettings   = KEY_S;
    s->keys.menuModes      = KEY_G;
    s->keys.menuProfile    = KEY_P;
    s->keys.menuPlay       = KEY_ENTER;
    s->keys.menuFavorite   = KEY_F;
    s->keys.menuBan        = KEY_X;
    s->keys.menuHideBanned = KEY_H;
    s->keys.menuFavsOnly   = KEY_V;
    s->keys.menuNewOnly    = KEY_N;
    s->keys.menuDiffFilter = KEY_D;
    s->keys.menuSortStar   = KEY_R;
    s->keys.menuCycleSort  = KEY_TAB;
    s->keys.menuRescan     = KEY_F5;
    const Palette *r = &PALETTES[1];
    for (int i = 0; i < r->len; i++) s->customPal[i] = r->cols[i];
    s->customLen = r->len;
    s->colorChainFile[0] = '\0';
    s->mapsDir[0] = '\0';
    s->bgStyle      = BG_NONE;
    s->bgIntensity  = 0.5f;
    s->cursorInMenu = false;
    s->hudShowScore    = true;
    s->hudShowCombo    = true;
    s->hudShowAccuracy = true;
    s->hudShowHp       = true;
    s->hudShowSongInfo = true;
}

void settings_load(Settings *s) {
    settings_defaults(s);
    if (!FileExists(SETTINGS_FILE)) return;
    char *txt = LoadFileText(SETTINGS_FILE);
    if (!txt) return;
    for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (line[0] == '#') continue;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0'; const char *key = line, *val = eq + 1;
        if      (strcmp(key, "approach_dist") == 0) s->approachDist = clampf((float)atof(val), 5.0f, 120.0f);
        else if (strcmp(key, "approach_ms")   == 0) s->approachMs   = clampf((float)atof(val), 200.0f, 1500.0f);
        else if (strcmp(key, "tablet")        == 0) s->tablet = (atoi(val) != 0);
        else if (strcmp(key, "palette")       == 0) { int p = atoi(val); if (p < 0) p = 0; if (p > NPALETTE + 1) p = NPALETTE + 1; s->paletteIdx = p; }
        else if (strcmp(key, "color_chain")   == 0) snprintf(s->colorChainFile, sizeof s->colorChainFile, "%s", val);
        else if (strcmp(key, "maps_dir")      == 0) snprintf(s->mapsDir, sizeof s->mapsDir, "%s", val);
        else if (strcmp(key, "bg_style")       == 0) { int v = atoi(val); s->bgStyle = (v >= 0 && v < BG_COUNT) ? (BgStyle)v : BG_NONE; }
        else if (strcmp(key, "bg_intensity")   == 0) s->bgIntensity = clampf((float)atof(val), 0.0f, 1.0f);
        else if (strcmp(key, "cursor_in_menu") == 0) s->cursorInMenu = (atoi(val) != 0);
        else if (strcmp(key, "hud_score")      == 0) s->hudShowScore    = (atoi(val) != 0);
        else if (strcmp(key, "hud_combo")      == 0) s->hudShowCombo    = (atoi(val) != 0);
        else if (strcmp(key, "hud_accuracy")   == 0) s->hudShowAccuracy = (atoi(val) != 0);
        else if (strcmp(key, "hud_hp")         == 0) s->hudShowHp       = (atoi(val) != 0);
        else if (strcmp(key, "hud_song")       == 0) s->hudShowSongInfo = (atoi(val) != 0);
        else if (strcmp(key, "mesh")          == 0) snprintf(s->meshName, sizeof s->meshName, "%s", val);
        else if (strcmp(key, "cursor")        == 0) snprintf(s->cursorName, sizeof s->cursorName, "%s", val);
        else if (strcmp(key, "cursor_mode")   == 0) { int v = atoi(val); s->cursorMode = (v == 1) ? 1 : 0; }
        else if (strcmp(key, "cursor_type")   == 0) { int v = atoi(val); s->cursorMode = (v == 1) ? 1 : 0; } /* legacy : 1=image, 0/2=genere */
        else if (strcmp(key, "cur_color")     == 0) s->cursor.color = parse_color_hex(val, s->cursor.color);
        else if (strcmp(key, "cur_size")      == 0) s->cursor.size = clampf((float)atof(val), 4.0f, 40.0f);
        else if (strcmp(key, "cur_core")      == 0) s->cursor.coreIntensity = clampf((float)atof(val), 0.0f, 1.0f);
        else if (strcmp(key, "cur_glow_size") == 0) s->cursor.glowSize = clampf((float)atof(val), 1.0f, 6.0f);
        else if (strcmp(key, "cur_glow_int")  == 0) s->cursor.glowIntensity = clampf((float)atof(val), 0.0f, 1.0f);
        else if (strcmp(key, "cur_additive")  == 0) s->cursor.additive = (atoi(val) != 0);
        else if (strcmp(key, "music_volume")  == 0) s->musicVolume = clampf((float)atof(val), 0.0f, 1.0f);
        else if (strcmp(key, "godmode")       == 0) s->godMode = (atoi(val) != 0);
        else if (strcmp(key, "hit_window")    == 0) s->hitWindowMs = clampf((float)atof(val), 20.0f, 200.0f);
        else if (strcmp(key, "hue_shift")     == 0) { int hv = atoi(val); s->hueShift = ((hv % 360) + 360) % 360; }
        else if (strcmp(key, "sens")           == 0) s->sensMultiplier = clampf((float)atof(val), 0.01f, 20.0f);
        else if (strcmp(key, "vsync")          == 0) s->vsync = (atoi(val) != 0);
        else if (strcmp(key, "trail_on")       == 0) s->cursor.trailEnabled = (atoi(val) != 0);
        else if (strcmp(key, "trail_dur")      == 0) s->cursor.trailDuration = clampf((float)atof(val), 0.05f, 2.0f);
        else if (strcmp(key, "trail_max")      == 0) { int tv = atoi(val); s->cursor.trailMaxSegments = tv < 4 ? 4 : (tv > TRAIL_MAX ? TRAIL_MAX : tv); }
        else if (strcmp(key, "trail_spacing")  == 0) s->cursor.trailSpacing = clampf((float)atof(val), 1.0f, 40.0f);
        else if (strcmp(key, "trail_sscale")   == 0) s->cursor.trailStartScale = clampf((float)atof(val), 0.2f, 2.0f);
        else if (strcmp(key, "trail_escale")   == 0) s->cursor.trailEndScale = clampf((float)atof(val), 0.0f, 1.0f);
        else if (strcmp(key, "trail_salpha")   == 0) s->cursor.trailStartAlpha = clampf((float)atof(val), 0.0f, 1.0f);
        else if (strcmp(key, "trail_ealpha")   == 0) s->cursor.trailEndAlpha = clampf((float)atof(val), 0.0f, 1.0f);
        else if (strcmp(key, "trail_usecol")   == 0) s->cursor.trailUseCursorColor = (atoi(val) != 0);
        else if (strcmp(key, "trail_color")    == 0) s->cursor.trailColor = parse_color_hex(val, s->cursor.trailColor);
        else if (strcmp(key, "grid_alpha")     == 0) { int tv = atoi(val); s->gridAlpha = tv < 0 ? 0 : (tv > 255 ? 255 : tv); }
        else if (strcmp(key, "grid_style")     == 0) { int v = atoi(val); s->gridStyle = (v >= 0 && v <= 3) ? v : 0; }
        else if (strcmp(key, "note_scale")     == 0) s->noteScale = clampf((float)atof(val), 0.5f, 1.5f);
        else if (strcmp(key, "hitsound")       == 0) snprintf(s->hitsoundName, sizeof s->hitsoundName, "%s", val);
        else if (strcmp(key, "hitsound_volume") == 0) s->hitsoundVolume = clampf((float)atof(val), 0.0f, 1.0f);
        else if (strcmp(key, "internal_res")   == 0) { int v = atoi(val); s->internalRes = (v >= 0 && v <= 2) ? v : 0; }
        else if (strcmp(key, "audio_offset")   == 0) s->audioOffsetMs = clampf((float)atof(val), -300.0f, 300.0f);
        else if (strcmp(key, "juice")          == 0) { int v = atoi(val); s->juiceMode = (v >= 0 && v <= 3) ? v : 1; }
        else if (strcmp(key, "juice_count")    == 0) { int v = atoi(val); s->juiceCount = v < 2 ? 2 : (v > 60 ? 60 : v); }
        else if (strcmp(key, "juice_life")     == 0) s->juiceLife  = clampf((float)atof(val), 0.20f, 1.50f);
        else if (strcmp(key, "juice_size")     == 0) s->juiceSize  = clampf((float)atof(val), 0.05f, 0.40f);
        else if (strcmp(key, "juice_speed")    == 0) s->juiceSpeed = clampf((float)atof(val), 0.30f, 2.50f);
        else if (strcmp(key, "juice_pulse")    == 0) s->juicePulse = clampf((float)atof(val), 0.20f, 2.00f);
        else if (strcmp(key, "juice_combo")    == 0) { int v = atoi(val); s->juiceCombo = v <= 0 ? 0 : (v <= 25 ? 25 : (v <= 75 ? 50 : 100)); }
        else if (strcmp(key, "mods")           == 0) s->mods = atoi(val);
        else if (strcmp(key, "rate")           == 0) s->rate = clampf((float)atof(val), 0.5f, 2.0f);
        else if (strcmp(key, "aim_density")    == 0) s->aim.density     = clampf((float)atof(val), 0.5f, 14.0f);
        else if (strcmp(key, "aim_approach")   == 0) s->aim.approachMs  = clampf((float)atof(val), 150.0f, 1000.0f);
        else if (strcmp(key, "aim_style")      == 0) { int v = atoi(val); s->aim.style = (v >= 0 && v < N_AIM_STYLES) ? v : AS_MIX; }
        else if (strcmp(key, "aim_seg")        == 0) { int v = atoi(val); s->aim.segLen = v < 2 ? 2 : (v > 24 ? 24 : v); }
        else if (strcmp(key, "aim_radius")     == 0) s->aim.radius      = clampf((float)atof(val), 0.30f, 1.0f);
        else if (strcmp(key, "aim_size")       == 0) s->aim.size        = clampf((float)atof(val), 0.50f, 1.50f);
        else if (strcmp(key, "aim_hit")        == 0) s->aim.hitWindowMs = clampf((float)atof(val), 40.0f, 200.0f);
        else if (strcmp(key, "aim_duration")   == 0) { int v = atoi(val); s->aim.durationSec = v < 15 ? 15 : (v > 600 ? 600 : v); }
        else if (strcmp(key, "aim_accel")      == 0) { int v = atoi(val); s->aim.accelPct = v < 0 ? 0 : (v > 100 ? 100 : v); }
        else if (strcmp(key, "colors")        == 0) { int n = parse_hex_colors(val, s->customPal, MAX_PALETTE); if (n > 0) s->customLen = n; }
        else if (strcmp(key, "key_quit")           == 0) { int v = atoi(val); if (v > 0) s->keys.quit           = v; }
        else if (strcmp(key, "key_pause")          == 0) { int v = atoi(val); if (v > 0) s->keys.pause          = v; }
        else if (strcmp(key, "key_restart")        == 0) { int v = atoi(val); if (v > 0) s->keys.restart        = v; }
        else if (strcmp(key, "key_skipintro")      == 0) { int v = atoi(val); if (v > 0) s->keys.skipIntro      = v; }
        else if (strcmp(key, "key_m_settings")     == 0) { int v = atoi(val); if (v > 0) s->keys.menuSettings   = v; }
        else if (strcmp(key, "key_m_modes")        == 0) { int v = atoi(val); if (v > 0) s->keys.menuModes      = v; }
        else if (strcmp(key, "key_m_profile")      == 0) { int v = atoi(val); if (v > 0) s->keys.menuProfile    = v; }
        else if (strcmp(key, "key_m_play")         == 0) { int v = atoi(val); if (v > 0) s->keys.menuPlay       = v; }
        else if (strcmp(key, "key_m_fav")          == 0) { int v = atoi(val); if (v > 0) s->keys.menuFavorite   = v; }
        else if (strcmp(key, "key_m_ban")          == 0) { int v = atoi(val); if (v > 0) s->keys.menuBan        = v; }
        else if (strcmp(key, "key_m_hidebanned")   == 0) { int v = atoi(val); if (v > 0) s->keys.menuHideBanned = v; }
        else if (strcmp(key, "key_m_favsonly")     == 0) { int v = atoi(val); if (v > 0) s->keys.menuFavsOnly   = v; }
        else if (strcmp(key, "key_m_newonly")      == 0) { int v = atoi(val); if (v > 0) s->keys.menuNewOnly    = v; }
        else if (strcmp(key, "key_m_difffilter")   == 0) { int v = atoi(val); if (v > 0) s->keys.menuDiffFilter = v; }
        else if (strcmp(key, "key_m_sortstar")     == 0) { int v = atoi(val); if (v > 0) s->keys.menuSortStar   = v; }
        else if (strcmp(key, "key_m_cyclesort")    == 0) { int v = atoi(val); if (v > 0) s->keys.menuCycleSort  = v; }
        else if (strcmp(key, "key_m_rescan")       == 0) { int v = atoi(val); if (v > 0) s->keys.menuRescan     = v; }
    }
    UnloadFileText(txt);
}

void settings_save(const Settings *s) {
    /* buf : ~75 cles * ~40 chars + chemin bg (~512) + couleurs = ~6000 chars max. */
    char buf[8192]; int o = 0;
    o += snprintf(buf + o, sizeof buf - o, "# sspm-player - settings\n");
    o += snprintf(buf + o, sizeof buf - o, "approach_dist=%.0f\n", s->approachDist);
    o += snprintf(buf + o, sizeof buf - o, "approach_ms=%.0f\n", s->approachMs);
    o += snprintf(buf + o, sizeof buf - o, "tablet=%d\n", s->tablet ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "palette=%d\n", s->paletteIdx);
    o += snprintf(buf + o, sizeof buf - o, "mesh=%s\n", s->meshName);
    o += snprintf(buf + o, sizeof buf - o, "cursor=%s\n", s->cursorName);
    o += snprintf(buf + o, sizeof buf - o, "cursor_mode=%d\n", s->cursorMode);
    o += snprintf(buf + o, sizeof buf - o, "cur_color=%02x%02x%02x\n", s->cursor.color.r, s->cursor.color.g, s->cursor.color.b);
    o += snprintf(buf + o, sizeof buf - o, "cur_size=%.0f\n", s->cursor.size);
    o += snprintf(buf + o, sizeof buf - o, "cur_core=%.2f\n", s->cursor.coreIntensity);
    o += snprintf(buf + o, sizeof buf - o, "cur_glow_size=%.1f\n", s->cursor.glowSize);
    o += snprintf(buf + o, sizeof buf - o, "cur_glow_int=%.2f\n", s->cursor.glowIntensity);
    o += snprintf(buf + o, sizeof buf - o, "cur_additive=%d\n", s->cursor.additive ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "music_volume=%.2f\n", s->musicVolume);
    o += snprintf(buf + o, sizeof buf - o, "godmode=%d\n", s->godMode ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "hit_window=%.0f\n", s->hitWindowMs);
    o += snprintf(buf + o, sizeof buf - o, "hue_shift=%d\n", s->hueShift);
    o += snprintf(buf + o, sizeof buf - o, "sens=%.2f\n", s->sensMultiplier);
    o += snprintf(buf + o, sizeof buf - o, "vsync=%d\n", s->vsync ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "trail_on=%d\n", s->cursor.trailEnabled ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "trail_dur=%.2f\n", s->cursor.trailDuration);
    o += snprintf(buf + o, sizeof buf - o, "trail_max=%d\n", s->cursor.trailMaxSegments);
    o += snprintf(buf + o, sizeof buf - o, "trail_spacing=%.0f\n", s->cursor.trailSpacing);
    o += snprintf(buf + o, sizeof buf - o, "trail_sscale=%.2f\n", s->cursor.trailStartScale);
    o += snprintf(buf + o, sizeof buf - o, "trail_escale=%.2f\n", s->cursor.trailEndScale);
    o += snprintf(buf + o, sizeof buf - o, "trail_salpha=%.2f\n", s->cursor.trailStartAlpha);
    o += snprintf(buf + o, sizeof buf - o, "trail_ealpha=%.2f\n", s->cursor.trailEndAlpha);
    o += snprintf(buf + o, sizeof buf - o, "trail_usecol=%d\n", s->cursor.trailUseCursorColor ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "trail_color=%02x%02x%02x\n", s->cursor.trailColor.r, s->cursor.trailColor.g, s->cursor.trailColor.b);
    o += snprintf(buf + o, sizeof buf - o, "grid_alpha=%d\n", s->gridAlpha);
    o += snprintf(buf + o, sizeof buf - o, "grid_style=%d\n", s->gridStyle);
    o += snprintf(buf + o, sizeof buf - o, "note_scale=%.2f\n", s->noteScale);
    o += snprintf(buf + o, sizeof buf - o, "hitsound=%s\n", s->hitsoundName);
    o += snprintf(buf + o, sizeof buf - o, "hitsound_volume=%.2f\n", s->hitsoundVolume);
    o += snprintf(buf + o, sizeof buf - o, "internal_res=%d\n", s->internalRes);
    o += snprintf(buf + o, sizeof buf - o, "audio_offset=%.0f\n", s->audioOffsetMs);
    o += snprintf(buf + o, sizeof buf - o, "juice=%d\n", s->juiceMode);
    o += snprintf(buf + o, sizeof buf - o, "juice_count=%d\n", s->juiceCount);
    o += snprintf(buf + o, sizeof buf - o, "juice_life=%.2f\n", s->juiceLife);
    o += snprintf(buf + o, sizeof buf - o, "juice_size=%.2f\n", s->juiceSize);
    o += snprintf(buf + o, sizeof buf - o, "juice_speed=%.2f\n", s->juiceSpeed);
    o += snprintf(buf + o, sizeof buf - o, "juice_pulse=%.2f\n", s->juicePulse);
    o += snprintf(buf + o, sizeof buf - o, "juice_combo=%d\n", s->juiceCombo);
    o += snprintf(buf + o, sizeof buf - o, "mods=%d\n", s->mods);
    o += snprintf(buf + o, sizeof buf - o, "rate=%.2f\n", s->rate);
    o += snprintf(buf + o, sizeof buf - o, "aim_density=%.1f\n", s->aim.density);
    o += snprintf(buf + o, sizeof buf - o, "aim_approach=%.0f\n", s->aim.approachMs);
    o += snprintf(buf + o, sizeof buf - o, "aim_style=%d\n", s->aim.style);
    o += snprintf(buf + o, sizeof buf - o, "aim_seg=%d\n", s->aim.segLen);
    o += snprintf(buf + o, sizeof buf - o, "aim_radius=%.2f\n", s->aim.radius);
    o += snprintf(buf + o, sizeof buf - o, "aim_size=%.2f\n", s->aim.size);
    o += snprintf(buf + o, sizeof buf - o, "aim_hit=%.0f\n", s->aim.hitWindowMs);
    o += snprintf(buf + o, sizeof buf - o, "aim_duration=%d\n", s->aim.durationSec);
    o += snprintf(buf + o, sizeof buf - o, "aim_accel=%d\n", s->aim.accelPct);
    o += snprintf(buf + o, sizeof buf - o, "colors=");
    for (int i = 0; i < s->customLen && o < (int)sizeof buf - 8; i++)
        o += snprintf(buf + o, sizeof buf - o, "%s%02x%02x%02x", i ? "," : "",
                      s->customPal[i].r, s->customPal[i].g, s->customPal[i].b);
    o += snprintf(buf + o, sizeof buf - o, "\n");
    o += snprintf(buf + o, sizeof buf - o, "color_chain=%s\n", s->colorChainFile);
    o += snprintf(buf + o, sizeof buf - o, "maps_dir=%s\n", s->mapsDir);
    o += snprintf(buf + o, sizeof buf - o, "bg_style=%d\n",       (int)s->bgStyle);
    o += snprintf(buf + o, sizeof buf - o, "bg_intensity=%.2f\n", s->bgIntensity);
    o += snprintf(buf + o, sizeof buf - o, "cursor_in_menu=%d\n", s->cursorInMenu ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "hud_score=%d\n",    s->hudShowScore    ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "hud_combo=%d\n",    s->hudShowCombo    ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "hud_accuracy=%d\n", s->hudShowAccuracy ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "hud_hp=%d\n",       s->hudShowHp       ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "hud_song=%d\n",     s->hudShowSongInfo ? 1 : 0);
    o += snprintf(buf + o, sizeof buf - o, "key_quit=%d\n",           s->keys.quit);
    o += snprintf(buf + o, sizeof buf - o, "key_pause=%d\n",          s->keys.pause);
    o += snprintf(buf + o, sizeof buf - o, "key_restart=%d\n",        s->keys.restart);
    o += snprintf(buf + o, sizeof buf - o, "key_skipintro=%d\n",      s->keys.skipIntro);
    o += snprintf(buf + o, sizeof buf - o, "key_m_settings=%d\n",     s->keys.menuSettings);
    o += snprintf(buf + o, sizeof buf - o, "key_m_modes=%d\n",        s->keys.menuModes);
    o += snprintf(buf + o, sizeof buf - o, "key_m_profile=%d\n",      s->keys.menuProfile);
    o += snprintf(buf + o, sizeof buf - o, "key_m_play=%d\n",         s->keys.menuPlay);
    o += snprintf(buf + o, sizeof buf - o, "key_m_fav=%d\n",          s->keys.menuFavorite);
    o += snprintf(buf + o, sizeof buf - o, "key_m_ban=%d\n",          s->keys.menuBan);
    o += snprintf(buf + o, sizeof buf - o, "key_m_hidebanned=%d\n",   s->keys.menuHideBanned);
    o += snprintf(buf + o, sizeof buf - o, "key_m_favsonly=%d\n",     s->keys.menuFavsOnly);
    o += snprintf(buf + o, sizeof buf - o, "key_m_newonly=%d\n",      s->keys.menuNewOnly);
    o += snprintf(buf + o, sizeof buf - o, "key_m_difffilter=%d\n",   s->keys.menuDiffFilter);
    o += snprintf(buf + o, sizeof buf - o, "key_m_sortstar=%d\n",     s->keys.menuSortStar);
    o += snprintf(buf + o, sizeof buf - o, "key_m_cyclesort=%d\n",    s->keys.menuCycleSort);
    o += snprintf(buf + o, sizeof buf - o, "key_m_rescan=%d\n",       s->keys.menuRescan);
    SaveFileText(SETTINGS_FILE, buf);
}

/* =========================================================================
 * Profil joueur (favoris + meilleurs scores)
 * ========================================================================= */

bool profile_is_fav(const char *fname) {
    for (int i = 0; i < gProfile.favCount; i++)
        if (strcmp(gProfile.favFiles[i], fname) == 0) return true;
    return false;
}
void profile_toggle_fav(const char *fname) {
    for (int i = 0; i < gProfile.favCount; i++) {
        if (strcmp(gProfile.favFiles[i], fname) == 0) {
            for (int j = i; j < gProfile.favCount - 1; j++)
                memcpy(gProfile.favFiles[j], gProfile.favFiles[j+1], sizeof gProfile.favFiles[0]);
            gProfile.favCount--;
            return;
        }
    }
    if (gProfile.favCount < MAX_FAV_MAPS)
        snprintf(gProfile.favFiles[gProfile.favCount++], 256, "%s", fname);
}
bool profile_is_blacklisted(const char *fname) {
    for (int i = 0; i < gProfile.blacklistCount; i++)
        if (strcmp(gProfile.blacklistFiles[i], fname) == 0) return true;
    return false;
}
void profile_toggle_blacklist(const char *fname) {
    for (int i = 0; i < gProfile.blacklistCount; i++) {
        if (strcmp(gProfile.blacklistFiles[i], fname) == 0) {
            for (int j = i; j < gProfile.blacklistCount - 1; j++)
                memcpy(gProfile.blacklistFiles[j], gProfile.blacklistFiles[j+1], sizeof gProfile.blacklistFiles[0]);
            gProfile.blacklistCount--;
            return;
        }
    }
    if (gProfile.blacklistCount < MAX_BLACKLIST_MAPS)
        snprintf(gProfile.blacklistFiles[gProfile.blacklistCount++], 256, "%s", fname);
}
BestScore *profile_get_best(const char *fname) {
    for (int i = 0; i < gProfile.bestCount; i++)
        if (strcmp(gProfile.best[i].filename, fname) == 0) return &gProfile.best[i];
    return NULL;
}
bool profile_update_best(const char *fname, int score, int hits, int total, int maxCombo) {
    BestScore *b = profile_get_best(fname);
    if (b) {
        if (score <= b->score) return false;
        b->score = score; b->hits = hits; b->total = total; b->maxCombo = maxCombo;
        return true;
    }
    if (gProfile.bestCount >= MAX_BEST_MAPS) return false;
    BestScore *nb = &gProfile.best[gProfile.bestCount++];
    snprintf(nb->filename, sizeof nb->filename, "%.255s", fname);
    nb->score = score; nb->hits = hits; nb->total = total; nb->maxCombo = maxCombo;
    return true;
}
void profile_load(void) {
    memset(&gProfile, 0, sizeof gProfile);
    if (!FileExists(PROFILE_FILE)) return;
    char *txt = LoadFileText(PROFILE_FILE);
    if (!txt) return;
    for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (line[0] == '#') continue;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0'; const char *key = line, *val = eq + 1;
        if (strncmp(key, "fav:", 4) == 0 && atoi(val) && gProfile.favCount < MAX_FAV_MAPS)
            snprintf(gProfile.favFiles[gProfile.favCount++], 256, "%s", key + 4);
        else if (strncmp(key, "ban:", 4) == 0 && atoi(val) && gProfile.blacklistCount < MAX_BLACKLIST_MAPS)
            snprintf(gProfile.blacklistFiles[gProfile.blacklistCount++], 256, "%s", key + 4);
        else if (strcmp(key, "stats.runs")  == 0) gProfile.totalRuns         = atoi(val);
        else if (strcmp(key, "stats.time")  == 0) gProfile.totalPlayTimeSec  = (float)atof(val);
        else if (strcmp(key, "stats.hits")  == 0) gProfile.allTimeHits       = atoi(val);
        else if (strcmp(key, "stats.total") == 0) gProfile.allTimeTotalNotes = atoi(val);
        else if (strncmp(key, "best:", 5) == 0 && gProfile.bestCount < MAX_BEST_MAPS) {
            BestScore *b = &gProfile.best[gProfile.bestCount++];
            snprintf(b->filename, 256, "%s", key + 5);
            sscanf(val, "%d,%d,%d,%d", &b->score, &b->hits, &b->total, &b->maxCombo);
        }
    }
    UnloadFileText(txt);
}
void profile_save(void) {
    int bufsz = 512 + gProfile.favCount * 280 + gProfile.blacklistCount * 280 + gProfile.bestCount * 320;
    if (bufsz < 1024) bufsz = 1024;
    char *buf = (char *)malloc((size_t)bufsz);
    if (!buf) return;
    int o = 0;
    o += snprintf(buf + o, (size_t)(bufsz - o), "# sspm-player - profil\n");
    o += snprintf(buf + o, (size_t)(bufsz - o),
                  "stats.runs=%d\nstats.time=%.1f\nstats.hits=%d\nstats.total=%d\n",
                  gProfile.totalRuns, (double)gProfile.totalPlayTimeSec,
                  gProfile.allTimeHits, gProfile.allTimeTotalNotes);
    for (int i = 0; i < gProfile.favCount && o < bufsz - 300; i++)
        o += snprintf(buf + o, (size_t)(bufsz - o), "fav:%s=1\n", gProfile.favFiles[i]);
    for (int i = 0; i < gProfile.blacklistCount && o < bufsz - 300; i++)
        o += snprintf(buf + o, (size_t)(bufsz - o), "ban:%s=1\n", gProfile.blacklistFiles[i]);
    for (int i = 0; i < gProfile.bestCount && o < bufsz - 300; i++) {
        BestScore *b = &gProfile.best[i];
        o += snprintf(buf + o, (size_t)(bufsz - o), "best:%s=%d,%d,%d,%d\n",
                      b->filename, b->score, b->hits, b->total, b->maxCombo);
    }
    SaveFileText(PROFILE_FILE, buf);
    free(buf);
}
