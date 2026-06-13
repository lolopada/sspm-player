/*
 * bg.c — Fonds de scene proceduraux (BG_VOID : etoiles, BG_CIRCUIT : grille).
 *
 * Principe : zero allocation par frame.
 *   BG_VOID    : 300 cercles/pixels, seed fixe. Cout : 300 draw calls 2D.
 *   BG_CIRCUIT : tout pre-rendu dans un RenderTexture2D. Cout : 1 draw call.
 *
 * API publique (prototypes dans common.h) :
 *   bg_init(style, sw, sh)   — charge / regenere les ressources du style
 *   bg_draw(style, intensity) — appele dans play_draw_scene avant BeginMode3D
 *   bg_unload_all()          — libere la GPU memory (a la fermeture)
 */
#include "common.h"

/* =========================================================================
 * BG_VOID — champ d'etoiles
 * ========================================================================= */

#define VOID_STARS 300

typedef struct { Vector2 pos; float size; Color color; } Star;

static Star sVoidStars[VOID_STARS];
static bool sVoidReady = false;

void bg_void_init(int sw, int sh) {
    srand(42);
    for (int i = 0; i < VOID_STARS; i++) {
        float t  = (float)rand() / (float)RAND_MAX;
        float rc = (float)rand() / (float)RAND_MAX;
        sVoidStars[i].pos  = (Vector2){ (float)(rand() % sw), (float)(rand() % sh) };
        sVoidStars[i].size = t < .76f ? .5f : t < .94f ? 1.0f : 1.5f;
        unsigned char a    = (unsigned char)(50 + rand() % 165);
        if      (rc < .78f) sVoidStars[i].color = (Color){210, 215, 240, a};
        else if (rc < .92f) sVoidStars[i].color = (Color){ 90, 155, 255, a};
        else                sVoidStars[i].color = (Color){175, 135, 255, a};
    }
    sVoidReady = true;
}

static void bg_void_draw(float intensity) {
    ClearBackground((Color){12, 12, 20, 255});
    if (!sVoidReady || intensity <= 0.0f) return;
    unsigned char intA = (unsigned char)(intensity * 255.0f);
    for (int i = 0; i < VOID_STARS; i++) {
        Color c = sVoidStars[i].color;
        c.a = (unsigned char)((c.a * intA) / 255);
        if (sVoidStars[i].size < 1.0f)
            DrawPixelV(sVoidStars[i].pos, c);
        else
            DrawCircleV(sVoidStars[i].pos, sVoidStars[i].size, c);
    }
}

/* =========================================================================
 * BG_CIRCUIT — grille technique pre-rendue
 * ========================================================================= */

static RenderTexture2D sCircuitTex;
static bool            sCircuitReady = false;

void bg_circuit_init(int sw, int sh) {
    if (sCircuitReady) { UnloadRenderTexture(sCircuitTex); sCircuitReady = false; }
    sCircuitTex = LoadRenderTexture(sw, sh);

    BeginTextureMode(sCircuitTex);
    ClearBackground((Color){12, 12, 20, 255});

    const int G = 30;

    /* Grille de base — tres discrete (~8 % alpha) */
    Color gridLine = (Color){29, 158, 117, 20};
    for (int x = 0; x <= sw; x += G) DrawLine(x, 0,  x, sh, gridLine);
    for (int y = 0; y <= sh; y += G) DrawLine(0, y, sw,  y, gridLine);

    /* Traces lumineux (seed fixe) */
    srand(99);
    Color traceColors[3] = {
        {29, 158, 117, 80},   /* teal  */
        {83,  74, 183, 70},   /* purple */
        {24,  95, 165, 70},   /* blue  */
    };
    for (int i = 0; i < 10; i++) {
        Color tc  = traceColors[i % 3];
        int horiz = rand() % 2;
        if (horiz) {
            int gy = (rand() % (sh / G)) * G;
            int x1 = (rand() % (sw / G / 3)) * G;
            int x2 = (sw / G / 2 + rand() % (sw / G / 2)) * G;
            DrawLine(x1, gy, x2, gy, tc);
            DrawCircle(x1, gy, 3, tc);
            DrawCircle(x2, gy, 3, tc);
        } else {
            int gx = (rand() % (sw / G)) * G;
            int y1 = (rand() % (sh / G / 3)) * G;
            int y2 = (sh / G / 2 + rand() % (sh / G / 2)) * G;
            DrawLine(gx, y1, gx, y2, tc);
            DrawCircle(gx, y1, 3, tc);
            DrawCircle(gx, y2, 3, tc);
        }
    }

    /* Noeuds aux intersections (~10 % des croisements) */
    Color nodeColor = {29, 158, 117, 55};
    for (int x = G; x < sw; x += G)
        for (int y = G; y < sh; y += G)
            if (rand() % 10 == 0) DrawCircle(x, y, 2, nodeColor);

    EndTextureMode();
    sCircuitReady = true;
}

static void bg_circuit_draw(float intensity) {
    ClearBackground((Color){12, 12, 20, 255});
    if (!sCircuitReady || intensity <= 0.0f) return;
    int tw = sCircuitTex.texture.width;
    int th = sCircuitTex.texture.height;
    /* height negatif = flip Y OpenGL -> coordonnees ecran normales */
    Rectangle src = {0, 0, (float)tw, -(float)th};
    Color tint = {255, 255, 255, (unsigned char)(intensity * 255.0f)};
    DrawTextureRec(sCircuitTex.texture, src, (Vector2){0, 0}, tint);
}

/* =========================================================================
 * BG_PULSE — anneaux concentriques synchronises au beat
 * ========================================================================= */

#define PULSE_MAX 8

typedef struct {
    float age;
    float dur;
    Color color;
} PulseRing;

static PulseRing sPulsePool[PULSE_MAX];
static int       sPulseCount = 0;

void bg_pulse_on_beat(bool isDownbeat) {
    if (sPulseCount >= PULSE_MAX) return;
    Color c = isDownbeat
        ? (Color){29, 158, 117, 255}
        : (Color){83,  74, 183, 255};
    sPulsePool[sPulseCount++] = (PulseRing){ .age = 0.0f, .dur = 0.85f, .color = c };
}

void bg_pulse_reset(void) { sPulseCount = 0; }

static void bg_pulse_update(float dt) {
    for (int i = 0; i < sPulseCount; ) {
        sPulsePool[i].age += dt;
        if (sPulsePool[i].age >= sPulsePool[i].dur) {
            sPulsePool[i] = sPulsePool[--sPulseCount];
        } else {
            i++;
        }
    }
}

static void bg_pulse_draw(int sw, int sh, float intensity) {
    ClearBackground((Color){12, 12, 20, 255});
    if (sPulseCount == 0 || intensity <= 0.0f) return;

    Vector2 center = { sw * 0.5f, sh * 0.5f };
    float maxR = sqrtf((float)(sw * sw + sh * sh)) * 0.75f;

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < sPulseCount; i++) {
        float age  = sPulsePool[i].age / sPulsePool[i].dur;
        float ease = 1.0f - (1.0f - age) * (1.0f - age);
        float rad  = 5.0f + ease * maxR;

        float a = powf(1.0f - age, 1.5f) * 0.28f * intensity;
        if (a < 0.005f) continue;

        float thick = 2.5f + (0.3f - 2.5f) * age;  /* Lerp(2.5, 0.3, age) */

        Color c = sPulsePool[i].color;
        c.a = (unsigned char)(a * 255.0f);
        DrawRing(center, rad - thick, rad + thick, 0.0f, 360.0f, 64, c);
    }
    EndBlendMode();
}

/* =========================================================================
 * BG_AURORA — nebuleuse ambiante flottante
 * ========================================================================= */

#define AURORA_MAX 80

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float   size;
    float   alpha;
    Color   color;
} AuroraPart;

static AuroraPart sAuroraPool[AURORA_MAX];
static bool       sAuroraReady = false;

void bg_aurora_init(int sw, int sh) {
    srand(77);
    Color palette[3] = {
        {29, 158, 117, 255},
        {83,  74, 183, 255},
        {24,  95, 165, 255},
    };
    for (int i = 0; i < AURORA_MAX; i++) {
        sAuroraPool[i].pos   = (Vector2){ (float)(rand() % sw), (float)(rand() % sh) };
        sAuroraPool[i].vel   = (Vector2){
            ((float)rand() / RAND_MAX - 0.5f) * 14.0f,
            ((float)rand() / RAND_MAX - 0.5f) * 14.0f
        };
        sAuroraPool[i].size  = 20.0f + (float)(rand() % 50);
        sAuroraPool[i].alpha = 0.04f + (float)rand() / RAND_MAX * 0.10f;
        sAuroraPool[i].color = palette[rand() % 3];
    }
    sAuroraReady = true;
}

static void bg_aurora_update(float dt) {
    int sw = gRtW > 0 ? gRtW : GetScreenWidth();
    int sh = gRtH > 0 ? gRtH : GetScreenHeight();
    for (int i = 0; i < AURORA_MAX; i++) {
        sAuroraPool[i].pos.x += sAuroraPool[i].vel.x * dt;
        sAuroraPool[i].pos.y += sAuroraPool[i].vel.y * dt;
        float s = sAuroraPool[i].size;
        if (sAuroraPool[i].pos.x < -s)     sAuroraPool[i].pos.x += sw + s * 2.0f;
        if (sAuroraPool[i].pos.x > sw + s)  sAuroraPool[i].pos.x -= sw + s * 2.0f;
        if (sAuroraPool[i].pos.y < -s)     sAuroraPool[i].pos.y += sh + s * 2.0f;
        if (sAuroraPool[i].pos.y > sh + s)  sAuroraPool[i].pos.y -= sh + s * 2.0f;
    }
}

static void bg_aurora_draw(float intensity) {
    ClearBackground((Color){12, 12, 20, 255});
    if (!sAuroraReady || intensity <= 0.0f || !gHaveHalo) return;

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < AURORA_MAX; i++) {
        float s     = sAuroraPool[i].size;
        float scale = (s * 2.0f) / (float)gHaloTex.width;

        Color c = sAuroraPool[i].color;
        c.a = (unsigned char)(sAuroraPool[i].alpha * intensity * 255.0f);
        if (c.a < 2) continue;

        DrawTextureEx(
            gHaloTex,
            (Vector2){ sAuroraPool[i].pos.x - s, sAuroraPool[i].pos.y - s },
            0.0f,
            scale,
            c
        );
    }
    EndBlendMode();
}

/* =========================================================================
 * Fonds statiques pre-rendus — harness partage
 *
 * Generation one-time vers sStaticRT (RenderTexture) ou sStaticTex (Image).
 * Runtime = un seul DrawTextureRec / DrawTexture.
 * ========================================================================= */

#define SBG_BASE  ((Color){ 10, 10, 16, 255})
#define SBG_TEAL  ((Color){ 29,158,117, 255})
#define SBG_PURP  ((Color){ 83, 74,183, 255})
#define SBG_PURPL ((Color){127,119,221, 255})
#define SBG_BLUE  ((Color){ 24, 95,165, 255})

static Texture2D       sStaticTex    = {0};
static bool            sStaticReady  = false;
static bool            sStaticFromRT = false;
static RenderTexture2D sStaticRT     = {0};

static void static_bg_unload(void) {
    if (sStaticFromRT) { if (sStaticRT.id)  { UnloadRenderTexture(sStaticRT);  sStaticRT.id  = 0; } }
    else               { if (sStaticTex.id) { UnloadTexture(sStaticTex);       sStaticTex.id = 0; } }
    sStaticReady = false;
}

static void static_bg_draw(float intensity) {
    ClearBackground(SBG_BASE);
    if (!sStaticReady || intensity <= 0.0f) return;
    Color tint = {255, 255, 255, (unsigned char)(intensity * 255.0f)};
    if (sStaticFromRT) {
        Rectangle src = {0, 0, (float)sStaticRT.texture.width, -(float)sStaticRT.texture.height};
        DrawTextureRec(sStaticRT.texture, src, (Vector2){0, 0}, tint);
    } else {
        DrawTexture(sStaticTex, 0, 0, tint);
    }
}

/* --- BG_TOPO : courbes de niveau ----------------------------------------- */

static void bg_topo_init(int sw, int sh) {
#define TOPO_N 12
    Image noise = GenImagePerlinNoise(sw, sh, 0, 0, 4.0f);
    ImageFormat(&noise, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const Color *np = (const Color *)noise.data;

    Image out = GenImageColor(sw, sh, SBG_BASE);
    Color *op  = (Color *)out.data;

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int   idx  = y * sw + x;
            float v    = np[idx].r / 255.0f;
            int   band = (int)(v * TOPO_N); if (band >= TOPO_N) band = TOPO_N - 1;

            if (band % 2 == 0) {
                Color c = SBG_BASE; c.b = (unsigned char)(c.b + 8 < 255 ? c.b + 8 : 255);
                op[idx] = c;
            }

            bool onLine = false;
            if (x < sw - 1) {
                int br = (int)(np[idx+1].r / 255.0f * TOPO_N); if (br >= TOPO_N) br = TOPO_N-1;
                if (br != band) onLine = true;
            }
            if (!onLine && y < sh - 1) {
                int bb = (int)(np[idx+sw].r / 255.0f * TOPO_N); if (bb >= TOPO_N) bb = TOPO_N-1;
                if (bb != band) onLine = true;
            }
            if (onLine) {
                float t = (float)band / (float)(TOPO_N - 1);
                op[idx] = (Color){
                    (unsigned char)(24  + t * 5.0f),
                    (unsigned char)(95  + t * 63.0f),
                    (unsigned char)(165 - t * 48.0f),
                    (unsigned char)(40  + t * 30.0f)
                };
            }
        }
    }
    sStaticTex = LoadTextureFromImage(out);
    SetTextureFilter(sStaticTex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(noise); UnloadImage(out);
    sStaticFromRT = false; sStaticReady = true;
#undef TOPO_N
}

/* --- BG_VORONOI : cellules vitrail --------------------------------------- */

#define VOR_N    60
#define VOR_SEAM 2.5f

static void bg_voronoi_init(int sw, int sh) {
    int rw = sw / 2, rh = sh / 2;
    if (rw < 1) rw = 1;
    if (rh < 1) rh = 1;

    srand(17);
    Vector2 seeds[VOR_N];
    for (int i = 0; i < VOR_N; i++)
        seeds[i] = (Vector2){ (float)(rand() % rw), (float)(rand() % rh) };

    const Color pal[3] = { SBG_TEAL, SBG_PURP, SBG_BLUE };
    Image out = GenImageColor(rw, rh, SBG_BASE);
    Color *op  = (Color *)out.data;

    for (int y = 0; y < rh; y++) {
        for (int x = 0; x < rw; x++) {
            float px = (float)x, py = (float)y;
            float d1 = 1e9f, d2 = 1e9f;
            int   id = 0;
            for (int i = 0; i < VOR_N; i++) {
                float dx = px - seeds[i].x, dy = py - seeds[i].y;
                float d  = sqrtf(dx*dx + dy*dy);
                if      (d < d1) { d2 = d1; d1 = d; id = i; }
                else if (d < d2) { d2 = d; }
            }
            float seam = d2 - d1;
            Color c = SBG_BASE;
            if (seam < VOR_SEAM) {
                float t = seam / VOR_SEAM;
                t = 1.0f - t * t * (3.0f - 2.0f * t);   /* smoothstep, 1 au centre */
                const Color *sc = &pal[id % 3];
                c.r = (unsigned char)(SBG_BASE.r + (sc->r - SBG_BASE.r) * t * 0.5f);
                c.g = (unsigned char)(SBG_BASE.g + (sc->g - SBG_BASE.g) * t * 0.5f);
                c.b = (unsigned char)(SBG_BASE.b + (sc->b - SBG_BASE.b) * t * 0.5f);
            } else {
                const Color *tc = &pal[id % 3];  /* tint interieur quasi-invisible */
                c.r = (unsigned char)(SBG_BASE.r + tc->r / 51);
                c.g = (unsigned char)(SBG_BASE.g + tc->g / 51);
                c.b = (unsigned char)(SBG_BASE.b + tc->b / 51);
            }
            op[y * rw + x] = c;
        }
    }
    ImageResize(&out, sw, sh);
    sStaticTex = LoadTextureFromImage(out);
    SetTextureFilter(sStaticTex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(out);
    sStaticFromRT = false; sStaticReady = true;
}

/* --- BG_WAVEFORM : horizon d'onde (3 plans) ------------------------------ */

static void bg_waveform_init(int sw, int sh) {
    sStaticRT = LoadRenderTexture(sw, sh);
    BeginTextureMode(sStaticRT);
    ClearBackground(SBG_BASE);

    DrawRectangleGradientV(0, 0, sw, (int)(sh * 0.65f), (Color){14, 18, 36, 255}, SBG_BASE);

    for (int layer = 0; layer < 3; layer++) {
        Image nRow = GenImagePerlinNoise(sw, 1, layer * 300, 0, 2.5f);
        ImageFormat(&nRow, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        const Color *nr = (const Color *)nRow.data;

        float baseline  = sh * (0.72f - layer * 0.08f);
        float amplitude = sh * (0.07f + layer * 0.06f);

        Color fillCol;
        if (layer == 0)      fillCol = (Color){SBG_BLUE.r, SBG_BLUE.g, SBG_BLUE.b, 22};
        else if (layer == 1) fillCol = (Color){55, 84, 174, 35};
        else                 fillCol = (Color){SBG_PURP.r, SBG_PURP.g, SBG_PURP.b, 50};

        float prevY = baseline;
        for (int x = 0; x < sw; x++) {
            float yTop = baseline - (nr[x].r / 255.0f) * amplitude;
            DrawRectangle(x, (int)yTop, 1, sh - (int)yTop, fillCol);
            if (layer == 2 && x > 0) {
                Color crest = {SBG_TEAL.r, SBG_TEAL.g, SBG_TEAL.b, 85};
                DrawLineEx((Vector2){(float)(x-1), prevY}, (Vector2){(float)x, yTop}, 1.2f, crest);
            }
            prevY = yTop;
        }
        UnloadImage(nRow);
    }
    EndTextureMode();
    sStaticFromRT = true; sStaticReady = true;
}

/* --- BG_FLOW : champ de flux (streamlines) ------------------------------- */

#define FLOW_M      220
#define FLOW_K       70
#define FLOW_STEP     4.0f
#define FLOW_TURNS    2.5f
#define FLOW_OCELL   10

static void bg_flow_init(int sw, int sh) {
    Image noiseImg = GenImagePerlinNoise(sw, sh, 0, 0, 3.0f);
    ImageFormat(&noiseImg, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const Color *nfp = (const Color *)noiseImg.data;

    sStaticRT = LoadRenderTexture(sw, sh);
    BeginTextureMode(sStaticRT);
    ClearBackground(SBG_BASE);
    BeginBlendMode(BLEND_ADDITIVE);

    int oCols = (sw + FLOW_OCELL - 1) / FLOW_OCELL;
    int oRows = (sh + FLOW_OCELL - 1) / FLOW_OCELL;
    unsigned char *occ = (unsigned char *)calloc((size_t)(oCols * oRows), 1);

    srand(33);
    for (int m = 0; m < FLOW_M; m++) {
        float sx = (float)(rand() % sw), sy = (float)(rand() % sh);
        int ocx = (int)(sx / FLOW_OCELL); if (ocx >= oCols) ocx = oCols - 1;
        int ocy = (int)(sy / FLOW_OCELL); if (ocy >= oRows) ocy = oRows - 1;
        if (occ[ocy * oCols + ocx]) continue;

        Vector2 prev = {sx, sy};
        for (int k = 0; k < FLOW_K; k++) {
            int nx = (int)prev.x, ny = (int)prev.y;
            if (nx < 0 || nx >= sw || ny < 0 || ny >= sh) break;

            float angle = nfp[ny * sw + nx].r / 255.0f * 6.2831853f * FLOW_TURNS;
            Vector2 cur = { prev.x + cosf(angle) * FLOW_STEP,
                            prev.y + sinf(angle) * FLOW_STEP };
            if (cur.x < 0 || cur.x >= sw || cur.y < 0 || cur.y >= sh) break;

            int cx = (int)(cur.x / FLOW_OCELL); if (cx >= oCols) cx = oCols - 1;
            int cy = (int)(cur.y / FLOW_OCELL); if (cy >= oRows) cy = oRows - 1;
            if (occ[cy * oCols + cx] > 3) break;
            occ[cy * oCols + cx]++;

            float t = (float)k / (float)(FLOW_K - 1);
            DrawLineEx(prev, cur, 1.0f, (Color){
                (unsigned char)(29  + t * (127 - 29)),
                (unsigned char)(158 + t * (119 - 158)),
                (unsigned char)(117 + t * (221 - 117)),
                18
            });
            prev = cur;
        }
    }
    EndBlendMode();
    EndTextureMode();
    free(occ);
    UnloadImage(noiseImg);
    sStaticFromRT = true; sStaticReady = true;
}

/* --- BG_HALFTONE : trame de points --------------------------------------- */

#define HT_D    12
#define HT_RMAX (HT_D * 0.55f)

static void bg_halftone_init(int sw, int sh) {
    sStaticRT = LoadRenderTexture(sw, sh);
    BeginTextureMode(sStaticRT);
    ClearBackground(SBG_BASE);

    float bx   = sw * 0.30f, by = sh * 0.70f;
    float Rmax = sqrtf((float)(sw*sw + sh*sh)) * 0.50f;
    float angA  = 15.0f * 3.14159265f / 180.0f;
    float cosA = cosf(angA), sinA = sinf(angA);
    float angB  = angA + 8.0f * 3.14159265f / 180.0f;
    float cosB = cosf(angB), sinB = sinf(angB);
    int gW = sw / HT_D + 6, gH = sh / HT_D + 6;

    for (int gi = -3; gi < gW; gi++) {
        for (int gj = -3; gj < gH; gj++) {
            float gu = gi * HT_D, gv = gj * HT_D;
            float nx = gu * cosA - gv * sinA + sw * 0.5f;
            float ny = gu * sinA + gv * cosA + sh * 0.5f;
            if (nx < -HT_D || nx > sw + HT_D || ny < -HT_D || ny > sh + HT_D) continue;
            float dx = nx - bx, dy = ny - by;
            float f  = 1.0f - sqrtf(dx*dx + dy*dy) / Rmax;
            if (f < 0.0f) f = 0.0f;
            f = f * f;
            float r = f * HT_RMAX; if (r < 0.4f) continue;
            DrawCircleV((Vector2){nx, ny}, r, (Color){
                (unsigned char)(24  + f * 5.0f),
                (unsigned char)(95  + f * 63.0f),
                (unsigned char)(165 - f * 48.0f),
                (unsigned char)(40  + f * 40.0f)
            });
        }
    }
    for (int gi = -3; gi < gW; gi++) {
        for (int gj = -3; gj < gH; gj++) {
            float gu = gi * HT_D, gv = gj * HT_D;
            float nx = gu * cosB - gv * sinB + sw * 0.5f;
            float ny = gu * sinB + gv * cosB + sh * 0.5f;
            if (nx < -HT_D || nx > sw + HT_D || ny < -HT_D || ny > sh + HT_D) continue;
            float dx = nx - bx, dy = ny - by;
            float f  = 1.0f - sqrtf(dx*dx + dy*dy) / Rmax;
            if (f < 0.0f) f = 0.0f;
            f = f * f;
            float r = f * HT_RMAX * 0.65f; if (r < 0.4f) continue;
            DrawCircleV((Vector2){nx, ny}, r,
                (Color){SBG_PURP.r, SBG_PURP.g, SBG_PURP.b, (unsigned char)(18.0f * f)});
        }
    }
    EndTextureMode();
    sStaticFromRT = true; sStaticReady = true;
}

/* =========================================================================
 * Impulsion globale au beat (utilisee par VEIL, RADAR, GLITCH)
 * ========================================================================= */

static float sGlobalBeatPulse = 0.0f;

/* =========================================================================
 * BG_SPECTRUM — spectre FFT radial audio-reactif
 * ========================================================================= */

#define SPEC_N     512
#define SPEC_BANDS 48
#define SPEC_DECAY 5.0f

static float         sSpecRaw[SPEC_N];          /* ring buffer — audio thread ecrit */
static volatile int  sSpecWriteIdx = 0;
static float         sSpecBands[SPEC_BANDS];    /* magnitudes lissees, thread principal */
static float         sSpecRe[SPEC_N];
static float         sSpecIm[SPEC_N];
static bool          sSpecAttached = false;

static void bg_audio_tap(void *buf, unsigned int frames) {
    const float *f = (const float *)buf; /* float stereo interleave */
    for (unsigned int i = 0; i < frames; i++) {
        int idx = sSpecWriteIdx % SPEC_N;
        sSpecRaw[idx] = (f[i * 2] + f[i * 2 + 1]) * 0.5f;
        sSpecWriteIdx++;
    }
}

static void bg_spectrum_init(void) {
    if (!sSpecAttached) {
        AttachAudioMixedProcessor(bg_audio_tap);
        sSpecAttached = true;
    }
    for (int i = 0; i < SPEC_BANDS; i++) sSpecBands[i] = 0.0f;
}

static void bg_spectrum_update(float dt) {
    /* Copie + fenetre de Hann */
    for (int i = 0; i < SPEC_N; i++) {
        int src = (sSpecWriteIdx % SPEC_N + i) % SPEC_N;
        float h = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (SPEC_N - 1)));
        sSpecRe[i] = sSpecRaw[src] * h;
        sSpecIm[i] = 0.0f;
    }

    /* FFT radix-2 iteratif (DIT) */
    for (int i = 1, j = 0; i < SPEC_N; i++) {
        int bit = SPEC_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = sSpecRe[i]; sSpecRe[i] = sSpecRe[j]; sSpecRe[j] = t;
            t = sSpecIm[i]; sSpecIm[i] = sSpecIm[j]; sSpecIm[j] = t;
        }
    }
    for (int len = 2; len <= SPEC_N; len <<= 1) {
        float ang  = -2.0f * 3.14159265f / (float)len;
        float wRe  = cosf(ang), wIm = sinf(ang);
        for (int i = 0; i < SPEC_N; i += len) {
            float cRe = 1.0f, cIm = 0.0f;
            for (int j2 = 0; j2 < len / 2; j2++) {
                float uRe = sSpecRe[i + j2],       uIm = sSpecIm[i + j2];
                float tRe = cRe * sSpecRe[i + j2 + len/2] - cIm * sSpecIm[i + j2 + len/2];
                float tIm = cRe * sSpecIm[i + j2 + len/2] + cIm * sSpecRe[i + j2 + len/2];
                sSpecRe[i + j2]           = uRe + tRe;
                sSpecIm[i + j2]           = uIm + tIm;
                sSpecRe[i + j2 + len / 2] = uRe - tRe;
                sSpecIm[i + j2 + len / 2] = uIm - tIm;
                float nRe = cRe * wRe - cIm * wIm;
                cIm = cRe * wIm + cIm * wRe;
                cRe = nRe;
            }
        }
    }

    /* Mapping log-frequence -> bandes + lissage */
    for (int b = 0; b < SPEC_BANDS; b++) {
        int lo = (int)powf(256.0f, (float)b / SPEC_BANDS);
        int hi = (int)powf(256.0f, (float)(b + 1) / SPEC_BANDS);
        if (lo >= SPEC_N / 2) lo = SPEC_N / 2 - 1;
        if (hi >= SPEC_N / 2) hi = SPEC_N / 2 - 1;
        if (hi <= lo) hi = lo + 1;
        float mag = 0.0f;
        for (int k = lo; k < hi; k++) {
            float re = sSpecRe[k], im = sSpecIm[k];
            float m  = sqrtf(re * re + im * im) / (SPEC_N * 0.5f);
            if (m > mag) mag = m;
        }
        if (mag > 1.0f) mag = 1.0f;
        if (mag > sSpecBands[b]) sSpecBands[b] = mag;
        else                     sSpecBands[b] -= dt * SPEC_DECAY * sSpecBands[b];
    }
}

static void bg_spectrum_draw(int sw, int sh, float intensity) {
    ClearBackground(SBG_BASE);
    if (intensity <= 0.0f) return;
    Vector2 center = { sw * 0.5f, sh * 0.5f };
    float   minR   = 40.0f;
    float   maxR   = (float)(sw < sh ? sw : sh) * 0.38f;

    if (gHaveHalo) {
        float s = minR * 3.0f;
        BeginBlendMode(BLEND_ADDITIVE);
        Color hc = { SBG_TEAL.r, SBG_TEAL.g, SBG_TEAL.b, (unsigned char)(40 * intensity) };
        DrawTextureEx(gHaloTex, (Vector2){ center.x - s, center.y - s },
                      0.0f, (s * 2.0f) / (float)gHaloTex.width, hc);
        EndBlendMode();
    }

    BeginBlendMode(BLEND_ADDITIVE);
    for (int b = 0; b < SPEC_BANDS; b++) {
        float t      = (float)b / (SPEC_BANDS - 1);
        float angRad = 2.0f * 3.14159265f * (float)b / SPEC_BANDS;
        float len    = minR + sSpecBands[b] * maxR;
        Vector2 p0   = { center.x + cosf(angRad) * minR, center.y + sinf(angRad) * minR };
        Vector2 p1   = { center.x + cosf(angRad) * len,  center.y + sinf(angRad) * len  };
        Color c;
        if (t < 0.5f) {
            float tt = t * 2.0f;
            c = (Color){
                (unsigned char)(SBG_BLUE.r + tt * (SBG_TEAL.r - SBG_BLUE.r)),
                (unsigned char)(SBG_BLUE.g + tt * (SBG_TEAL.g - SBG_BLUE.g)),
                (unsigned char)(SBG_BLUE.b + tt * (SBG_TEAL.b - SBG_BLUE.b)),
                (unsigned char)(180.0f * intensity)
            };
        } else {
            float tt = (t - 0.5f) * 2.0f;
            c = (Color){
                (unsigned char)(SBG_TEAL.r + tt * (SBG_PURPL.r - SBG_TEAL.r)),
                (unsigned char)(SBG_TEAL.g + tt * (SBG_PURPL.g - SBG_TEAL.g)),
                (unsigned char)(SBG_TEAL.b + tt * (SBG_PURPL.b - SBG_TEAL.b)),
                (unsigned char)(180.0f * intensity)
            };
        }
        DrawLineEx(p0, p1, 2.0f + sSpecBands[b] * 4.0f, c);
    }
    EndBlendMode();
}

/* =========================================================================
 * BG_FLOWLIVE — champ de flux anime (160 particules advectees)
 * ========================================================================= */

#define FLIVEL_PARTS 160
#define FLIVEL_GW    40
#define FLIVEL_GH    24

typedef struct { Vector2 pos, prev; float life, maxLife; } FlowLivePart;

static FlowLivePart  sFLParts[FLIVEL_PARTS];
static float         sFLBaseAngles[FLIVEL_GW * FLIVEL_GH];
static float         sFLTime     = 0.0f;
static bool          sFLReady    = false;
static unsigned int  sFLLcg      = 0;

static unsigned int fl_rand(void) { sFLLcg = sFLLcg * 1664525u + 1013904223u; return sFLLcg; }
static float        fl_randf(void) { return (float)(fl_rand() & 0xFFFFu) / 65535.0f; }

static void bg_flowlive_init(int sw, int sh) {
    sFLLcg = 55;
    for (int i = 0; i < FLIVEL_GW * FLIVEL_GH; i++)
        sFLBaseAngles[i] = fl_randf() * 6.2831853f;
    for (int i = 0; i < FLIVEL_PARTS; i++) {
        sFLParts[i].pos.x  = fl_randf() * (float)sw;
        sFLParts[i].pos.y  = fl_randf() * (float)sh;
        sFLParts[i].prev   = sFLParts[i].pos;
        sFLParts[i].maxLife = 2.0f + fl_randf() * 3.0f;
        sFLParts[i].life   = fl_randf() * sFLParts[i].maxLife;
    }
    sFLTime  = 0.0f;
    sFLReady = true;
}

static void bg_flowlive_update(float dt) {
    if (!sFLReady) return;
    sFLTime += dt;
    int sw = gRtW > 0 ? gRtW : GetScreenWidth();
    int sh = gRtH > 0 ? gRtH : GetScreenHeight();
    for (int i = 0; i < FLIVEL_PARTS; i++) {
        sFLParts[i].life += dt;
        if (sFLParts[i].life >= sFLParts[i].maxLife) {
            sFLParts[i].pos.x  = fl_randf() * (float)sw;
            sFLParts[i].pos.y  = fl_randf() * (float)sh;
            sFLParts[i].prev   = sFLParts[i].pos;
            sFLParts[i].life   = 0.0f;
            sFLParts[i].maxLife = 2.0f + fl_randf() * 3.0f;
            continue;
        }
        int gx = (int)(sFLParts[i].pos.x / (float)sw * FLIVEL_GW);
        int gy = (int)(sFLParts[i].pos.y / (float)sh * FLIVEL_GH);
        if (gx < 0) gx = 0;
        if (gx >= FLIVEL_GW) gx = FLIVEL_GW - 1;
        if (gy < 0) gy = 0;
        if (gy >= FLIVEL_GH) gy = FLIVEL_GH - 1;
        float angle = sFLBaseAngles[gy * FLIVEL_GW + gx] + sFLTime * 0.15f;
        sFLParts[i].prev   = sFLParts[i].pos;
        sFLParts[i].pos.x += cosf(angle) * 80.0f * dt;
        sFLParts[i].pos.y += sinf(angle) * 80.0f * dt;
        /* wrap */
        if (sFLParts[i].pos.x < 0.0f)     sFLParts[i].pos.x += (float)sw;
        if (sFLParts[i].pos.x >= (float)sw) sFLParts[i].pos.x -= (float)sw;
        if (sFLParts[i].pos.y < 0.0f)     sFLParts[i].pos.y += (float)sh;
        if (sFLParts[i].pos.y >= (float)sh) sFLParts[i].pos.y -= (float)sh;
    }
}

static void bg_flowlive_draw(float intensity) {
    ClearBackground(SBG_BASE);
    if (!sFLReady || intensity <= 0.0f) return;
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < FLIVEL_PARTS; i++) {
        float k    = sFLParts[i].life / sFLParts[i].maxLife;
        float fade = sinf(k * 3.14159265f);
        unsigned char a = (unsigned char)(60.0f * fade * intensity);
        if (a < 2) continue;
        Color c = {
            (unsigned char)(29  + k * (127 - 29)),
            (unsigned char)(158 + k * (119 - 158)),
            (unsigned char)(117 + k * (221 - 117)),
            a
        };
        DrawLineEx(sFLParts[i].prev, sFLParts[i].pos, 1.5f, c);
    }
    EndBlendMode();
}

/* =========================================================================
 * BG_VEIL — rideaux d'aurore boreale
 * ========================================================================= */

#define VEILS 5

typedef struct { float baseX, ampl, speed, phase, hue; } VeilStrip;
static VeilStrip sVeils[VEILS];
static Texture2D sVeilTex   = {0};
static bool      sVeilReady = false;

static void bg_veil_init(int sw, int sh) { (void)sh;
    if (sVeilReady) { UnloadTexture(sVeilTex); sVeilReady = false; }

    /* Bande gaussienne 64x256 (profil horizontal) */
    Image img = GenImageColor(64, 256, (Color){0, 0, 0, 0});
    Color *px = (Color *)img.data;
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 64; x++) {
            float d = (float)x / 32.0f - 1.0f;
            unsigned char a = (unsigned char)(expf(-d * d * 5.0f) * 255.0f);
            px[y * 64 + x] = (Color){255, 255, 255, a};
        }
    }
    sVeilTex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(sVeilTex, TEXTURE_FILTER_BILINEAR);

    srand(66);
    for (int i = 0; i < VEILS; i++) {
        sVeils[i].baseX = sw * (0.12f + i * 0.19f);
        sVeils[i].ampl  = sw * (0.04f + (float)(rand() % 18) / 100.0f);
        sVeils[i].speed = 0.18f + (float)(rand() % 25) / 100.0f;
        sVeils[i].phase = (float)rand() / RAND_MAX * 6.2831853f;
        sVeils[i].hue   = 160.0f + i * 30.0f;
    }
    sVeilReady = true;
}

static void bg_veil_draw(int sw, int sh, float intensity) {
    ClearBackground(SBG_BASE);
    if (!sVeilReady || intensity <= 0.0f) return;
    float t         = (float)GetTime();
    float beatBoost = 1.0f + sGlobalBeatPulse * 0.6f;
    float w         = sw * 0.13f;

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < VEILS; i++) {
        float x    = sVeils[i].baseX + sinf(t * sVeils[i].speed + sVeils[i].phase) * sVeils[i].ampl;
        float hue  = fmodf(sVeils[i].hue + t * 4.0f, 360.0f);
        unsigned char a = (unsigned char)(28.0f * intensity * beatBoost);
        Color c    = hsv_to_rgb(hue, 0.70f, 1.0f, a);
        Rectangle src = { 0, 0, 64, 256 };
        Rectangle dst = { x - w * 0.5f, 0, w, (float)sh };
        DrawTexturePro(sVeilTex, src, dst, (Vector2){0, 0}, 0.0f, c);
    }
    EndBlendMode();
}

/* =========================================================================
 * BG_RADAR — balayage radar rotatif
 * ========================================================================= */

static float           sRadarAngle = 0.0f;
static RenderTexture2D sRadarRT    = {0};
static bool            sRadarReady = false;

static void bg_radar_init(int sw, int sh) {
    if (sRadarReady) { UnloadRenderTexture(sRadarRT); sRadarReady = false; }
    sRadarRT = LoadRenderTexture(sw, sh);
    BeginTextureMode(sRadarRT);
    ClearBackground(SBG_BASE);
    Vector2 center = { sw * 0.5f, sh * 0.5f };
    float maxR = (float)(sw < sh ? sw : sh) * 0.44f;
    Color gridC = { SBG_TEAL.r, SBG_TEAL.g, SBG_TEAL.b, 18 };
    for (int i = 1; i <= 4; i++)
        DrawCircleLines((int)center.x, (int)center.y, maxR * (float)i * 0.25f, gridC);
    DrawLineEx((Vector2){ center.x, center.y - maxR }, (Vector2){ center.x, center.y + maxR }, 1.0f, gridC);
    DrawLineEx((Vector2){ center.x - maxR, center.y }, (Vector2){ center.x + maxR, center.y }, 1.0f, gridC);
    EndTextureMode();
    sRadarAngle = 0.0f;
    sRadarReady = true;
}

static void bg_radar_update(float dt) {
    sRadarAngle += dt * 45.0f;
    if (sRadarAngle >= 360.0f) sRadarAngle -= 360.0f;
}

static void bg_radar_draw(int sw, int sh, float intensity) {
    ClearBackground(SBG_BASE);
    if (!sRadarReady || intensity <= 0.0f) return;
    Color tint = { 255, 255, 255, (unsigned char)(intensity * 180.0f) };
    Rectangle src = { 0, 0, (float)sRadarRT.texture.width, -(float)sRadarRT.texture.height };
    DrawTextureRec(sRadarRT.texture, src, (Vector2){0, 0}, tint);
    Vector2 center  = { sw * 0.5f, sh * 0.5f };
    float maxR = (float)(sw < sh ? sw : sh) * 0.44f;
    float beatBoost = 1.0f + sGlobalBeatPulse * 0.8f;
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 28; i++) {
        float al = powf(1.0f - (float)i / 28.0f, 2.0f) * 0.22f * intensity * beatBoost;
        if (al < 0.005f) break;
        Color sc = { SBG_TEAL.r, SBG_TEAL.g, SBG_TEAL.b, (unsigned char)(al * 255.0f) };
        float sEnd   = sRadarAngle - (float)i * (360.0f / 28.0f);
        float sStart = sEnd - (360.0f / 28.0f);
        DrawCircleSector(center, maxR, sStart, sEnd, 4, sc);
    }
    EndBlendMode();
}

/* =========================================================================
 * BG_GLITCH — bandes RGB-split sur les downbeats
 * ========================================================================= */

#define GLITCH_MAX 6

typedef struct { float y, h, offset, life; } GlitchBand;
static GlitchBand sGlitch[GLITCH_MAX];
static int        sGlitchCount = 0;

static void bg_glitch_on_beat(bool isDownbeat) {
    if (!isDownbeat) return;
    int add = 2 + GetRandomValue(0, 2);
    for (int i = 0; i < add && sGlitchCount < GLITCH_MAX; i++) {
        sGlitch[sGlitchCount++] = (GlitchBand){
            .y      = (float)GetRandomValue(0, 900),
            .h      = (float)GetRandomValue(2, 10),
            .offset = (float)GetRandomValue(6, 24),
            .life   = 0.15f
        };
    }
}

static void bg_glitch_update(float dt) {
    for (int i = 0; i < sGlitchCount; ) {
        sGlitch[i].life -= dt;
        if (sGlitch[i].life <= 0.0f) {
            sGlitch[i] = sGlitch[--sGlitchCount];
        } else {
            i++;
        }
    }
}

static void bg_glitch_draw(int sw, int sh, float intensity) {
    ClearBackground(SBG_BASE);
    if (sGlitchCount == 0 || intensity <= 0.0f) return;
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < sGlitchCount; i++) {
        float lifeFrac = sGlitch[i].life / 0.15f;
        int   y   = (int)(sGlitch[i].y / 900.0f * (float)sh);
        int   h   = (int)sGlitch[i].h;
        float off = sGlitch[i].offset * lifeFrac;
        unsigned char a = (unsigned char)(intensity * lifeFrac * 200.0f);
        DrawRectangle((int)-off, y, sw + (int)off, h,
            (Color){ SBG_TEAL.r, SBG_TEAL.g, SBG_TEAL.b, a });
        DrawRectangle((int)+off, y, sw,             h,
            (Color){ SBG_PURP.r, SBG_PURP.g, SBG_PURP.b, a });
        DrawRectangle(0,         y, sw,             h,
            (Color){ 255, 20, 20, (unsigned char)(a / 3) });
    }
    EndBlendMode();
}

/* =========================================================================
 * API commune
 * ========================================================================= */

void bg_on_beat(bool isDownbeat) {
    sGlobalBeatPulse = 1.0f;
    if (gSettings.bgStyle == BG_GLITCH) bg_glitch_on_beat(isDownbeat);
}

void bg_init(BgStyle style, int sw, int sh) {
    /* Detach audio tap si on quitte SPECTRUM */
    if (sSpecAttached && style != BG_SPECTRUM) {
        DetachAudioMixedProcessor(bg_audio_tap);
        sSpecAttached = false;
    }
    if (sCircuitReady) { UnloadRenderTexture(sCircuitTex);  sCircuitReady = false; }
    if (sRadarReady)   { UnloadRenderTexture(sRadarRT);     sRadarReady   = false; }
    if (sVeilReady)    { UnloadTexture(sVeilTex);           sVeilReady    = false; }
    static_bg_unload();
    switch (style) {
        case BG_VOID:     bg_void_init(sw, sh);     break;
        case BG_CIRCUIT:  bg_circuit_init(sw, sh);  break;
        case BG_AURORA:   bg_aurora_init(sw, sh);   break;
        case BG_PULSE:    bg_pulse_reset();          break;
        case BG_TOPO:     bg_topo_init(sw, sh);     break;
        case BG_VORONOI:  bg_voronoi_init(sw, sh);  break;
        case BG_WAVEFORM: bg_waveform_init(sw, sh); break;
        case BG_FLOW:     bg_flow_init(sw, sh);     break;
        case BG_HALFTONE: bg_halftone_init(sw, sh); break;
        case BG_SPECTRUM: bg_spectrum_init();        break;
        case BG_FLOWLIVE: bg_flowlive_init(sw, sh); break;
        case BG_VEIL:     bg_veil_init(sw, sh);     break;
        case BG_RADAR:    bg_radar_init(sw, sh);    break;
        case BG_GLITCH:   sGlitchCount = 0;         break;
        default: break;
    }
}

void bg_unload_all(void) {
    if (sSpecAttached) { DetachAudioMixedProcessor(bg_audio_tap); sSpecAttached = false; }
    if (sCircuitReady) { UnloadRenderTexture(sCircuitTex);  sCircuitReady = false; }
    if (sRadarReady)   { UnloadRenderTexture(sRadarRT);     sRadarReady   = false; }
    if (sVeilReady)    { UnloadTexture(sVeilTex);           sVeilReady    = false; }
    static_bg_unload();
    bg_pulse_reset();
    sGlitchCount = 0;
}

void bg_draw(BgStyle style, float intensity) {
    int sw = gRtW > 0 ? gRtW : GetScreenWidth();
    int sh = gRtH > 0 ? gRtH : GetScreenHeight();
    switch (style) {
        case BG_VOID:     bg_void_draw(intensity);              break;
        case BG_CIRCUIT:  bg_circuit_draw(intensity);           break;
        case BG_PULSE:    bg_pulse_draw(sw, sh, intensity);     break;
        case BG_AURORA:   bg_aurora_draw(intensity);            break;
        case BG_TOPO:
        case BG_VORONOI:
        case BG_WAVEFORM:
        case BG_FLOW:
        case BG_HALFTONE: static_bg_draw(intensity);            break;
        case BG_SPECTRUM: bg_spectrum_draw(sw, sh, intensity);  break;
        case BG_FLOWLIVE: bg_flowlive_draw(intensity);          break;
        case BG_VEIL:     bg_veil_draw(sw, sh, intensity);      break;
        case BG_RADAR:    bg_radar_draw(sw, sh, intensity);     break;
        case BG_GLITCH:   bg_glitch_draw(sw, sh, intensity);    break;
        default:          ClearBackground(SBG_BASE);            break;
    }
}

void bg_update(BgStyle style, float dt) {
    if (style == BG_PULSE)    bg_pulse_update(dt);
    if (style == BG_AURORA)   bg_aurora_update(dt);
    if (style == BG_SPECTRUM) bg_spectrum_update(dt);
    if (style == BG_FLOWLIVE) bg_flowlive_update(dt);
    if (style == BG_RADAR)    bg_radar_update(dt);
    if (style == BG_GLITCH)   bg_glitch_update(dt);
    /* Decroissance de l'impulsion globale (VEIL, RADAR, GLITCH) */
    sGlobalBeatPulse -= dt * 4.0f;
    if (sGlobalBeatPulse < 0.0f) sGlobalBeatPulse = 0.0f;
}
