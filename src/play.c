#include "common.h"

/* --- mods / modificateurs (cases a cocher du panneau gauche du menu) --- */

/* Construit une etiquette courte des mods actifs ("HR HD 1.5x"). false si aucun. */
static bool mods_label(char *buf, size_t n) {
    size_t o = 0; buf[0] = '\0';
    #define MODS_ADD(s) do { o += (size_t)snprintf(buf + o, o < n ? n - o : 0, "%s%s", o ? " " : "", s); } while (0)
    if (gMods & MOD_HARDROCK) MODS_ADD("HR");
    if (gMods & MOD_HIDDEN)   MODS_ADD("HD");
    if (gMods & MOD_NOFAIL)   MODS_ADD("NF");
    if (gMods & MOD_SUDDEN)   MODS_ADD("SD");
    if (gMods & MOD_MIRROR_X)   MODS_ADD("MH");
    if (gMods & MOD_MIRROR_Y)   MODS_ADD("MV");
    if (gMods & MOD_FLASHLIGHT) MODS_ADD("FL");
    if (gMods & MOD_VANISH)    MODS_ADD("VN");
    if (gRate != 1.0f) { char r[16]; snprintf(r, sizeof r, "%gx", gRate); MODS_ADD(r); }
    #undef MODS_ADD
    return buf[0] != '\0';
}

/* --- modes de jeu (au-dela de la partie normale) --- */


const char *AIM_STYLE_NAMES[N_AIM_STYLES] = { "Flow", "Mixed", "Jumps", "Tech", "Chaos" };

/* patterns d'aim (segments coherents enchaines pour former un vrai parcours) */
/* Choisit un pattern dans le pool du style courant. */
static int aim_pick_pattern(int style) {
    static const int flow[]  = { P_CIRCLE, P_ZIG, P_PINGH, P_PINGV, P_DIAG, P_SPIRAL };
    static const int mix[]   = { P_CIRCLE, P_ZIG, P_PINGH, P_PINGV, P_DIAG, P_STAR, P_SQUARE, P_RANDFAR };
    static const int jump[]  = { P_STAR, P_BURST, P_PINGH, P_PINGV, P_SQUARE, P_RANDFAR };
    static const int tech[]  = { P_ZIG, P_SPIRAL, P_SQUARE, P_STAR, P_DIAG, P_RANDFAR };
    static const int chaos[] = { P_BURST, P_RANDFAR, P_STAR, P_PINGH, P_PINGV, P_SQUARE, P_ZIG, P_SPIRAL, P_DIAG };
    const int *pool; int n;
    switch (style) {
        case AS_MIX:   pool = mix;   n = (int)(sizeof mix   / sizeof mix[0]);   break;
        case AS_JUMP:  pool = jump;  n = (int)(sizeof jump  / sizeof jump[0]);  break;
        case AS_TECH:  pool = tech;  n = (int)(sizeof tech  / sizeof tech[0]);  break;
        case AS_CHAOS: pool = chaos; n = (int)(sizeof chaos / sizeof chaos[0]); break;
        default:       pool = flow;  n = (int)(sizeof flow  / sizeof flow[0]);  break;
    }
    return pool[GetRandomValue(0, n - 1)];
}

/* Couleur d'une note selon la palette active (cycle) + rotation de teinte globale. */
Color note_color(size_t i) {
    int n = gPaletteLen > 0 ? gPaletteLen : 1;
    Color c = gPalette[i % (size_t)n];
    if (gHueShift == 0) return c;
    float h, s, v;
    rgb_to_hsv(c, &h, &s, &v);
    h = fmodf(h + (float)gHueShift, 360.0f);
    if (h < 0.0f) h += 360.0f;
    return hsv_to_rgb(h, s, v, c.a);
}

static float note_wx(const SspmNote *nt) {
    float x = (gMods & MOD_MIRROR_X) ? (2.0f - nt->x) : nt->x;
    return (x - 1.0f) * GRID_SPACING;
}
static float note_wy(const SspmNote *nt) {
    float y = (gMods & MOD_MIRROR_Y) ? (2.0f - nt->y) : nt->y;
    return (1.0f - y) * GRID_SPACING; /* y=0 en haut */
}

/* Position monde d'une note a l'instant nowMs.
 * Camera sur +z regardant -z : une note a venir est devant (en -z) ; au plan z=0
 * c'est le moment du hit ; ensuite elle file en +z (derriere la camera). */
static Vector3 note_world(const SspmNote *nt, float nowMs) {
    float z = -gApproachDist * (nt->ms - nowMs) / gApproachMs;
    return (Vector3){ note_wx(nt), note_wy(nt), z };
}

/* Carre sur le plan z=0 (4 lignes). */
static void square_z0(float cx, float cy, float half, Color c) {
    Vector3 a = { cx - half, cy - half, 0 };
    Vector3 b = { cx + half, cy - half, 0 };
    Vector3 d = { cx + half, cy + half, 0 };
    Vector3 e = { cx - half, cy + half, 0 };
    DrawLine3D(a, b, c); DrawLine3D(b, d, c);
    DrawLine3D(d, e, c); DrawLine3D(e, a, c);
}

/* glow >0 (mode Pulse/Neon) eclaircit la grille sur le beat. */
static void draw_grid_frame(float glow) {
    const float e = 1.5f, ip = 0.5f;
    int ba0 = gGridAlpha + (int)(clampf(glow, 0.0f, 1.5f) * 140.0f);
    if (ba0 > 255) ba0 = 255;
    unsigned char ba = (unsigned char)ba0;
    unsigned char ia = (unsigned char)(ba0 * 56 / 100);
    Color border = (Color){ 200, 200, 220, ba };
    Color inner  = (Color){ 120, 120, 150, ia };

    switch (gGridStyle) {
        default:
        case 0: /* Classique : bordure + croix interieure */
            square_z0(0, 0, e, border);
            DrawLine3D((Vector3){ -ip, -e, 0 }, (Vector3){ -ip,  e, 0 }, inner);
            DrawLine3D((Vector3){  ip, -e, 0 }, (Vector3){  ip,  e, 0 }, inner);
            DrawLine3D((Vector3){ -e, -ip, 0 }, (Vector3){  e, -ip, 0 }, inner);
            DrawLine3D((Vector3){ -e,  ip, 0 }, (Vector3){  e,  ip, 0 }, inner);
            break;

        case 1: /* Minimal : bordure seule */
            square_z0(0, 0, e, border);
            break;

        case 2: /* Coins : L-marks aux 4 angles + croix fine */
            {
                const float d = 0.48f;  /* longueur de chaque branche du L */
                DrawLine3D((Vector3){ -e, -e, 0 }, (Vector3){ -e+d, -e, 0 }, border);
                DrawLine3D((Vector3){ -e, -e, 0 }, (Vector3){ -e,  -e+d, 0 }, border);
                DrawLine3D((Vector3){  e, -e, 0 }, (Vector3){  e-d, -e, 0 }, border);
                DrawLine3D((Vector3){  e, -e, 0 }, (Vector3){  e,  -e+d, 0 }, border);
                DrawLine3D((Vector3){ -e,  e, 0 }, (Vector3){ -e+d,  e, 0 }, border);
                DrawLine3D((Vector3){ -e,  e, 0 }, (Vector3){ -e,   e-d, 0 }, border);
                DrawLine3D((Vector3){  e,  e, 0 }, (Vector3){  e-d,  e, 0 }, border);
                DrawLine3D((Vector3){  e,  e, 0 }, (Vector3){  e,   e-d, 0 }, border);
                DrawLine3D((Vector3){ -ip, -e, 0 }, (Vector3){ -ip,  e, 0 }, inner);
                DrawLine3D((Vector3){  ip, -e, 0 }, (Vector3){  ip,  e, 0 }, inner);
                DrawLine3D((Vector3){ -e, -ip, 0 }, (Vector3){  e, -ip, 0 }, inner);
                DrawLine3D((Vector3){ -e,  ip, 0 }, (Vector3){  e,  ip, 0 }, inner);
            }
            break;

        case 3: /* Diagonales : bordure + X du centre aux coins */
            square_z0(0, 0, e, border);
            DrawLine3D((Vector3){ -e, -e, 0 }, (Vector3){  e,  e, 0 }, inner);
            DrawLine3D((Vector3){  e, -e, 0 }, (Vector3){ -e,  e, 0 }, inner);
            break;
    }
}

/* Bascule plein ecran en mode borderless windowed (alt-tab fonctionnel).
 * Apres SetWindowState, raylib peut poser HWND_TOPMOST ; on le retire via
 * SetWindowPos (declaration minimale, sans inclure windows.h). */
#ifdef _WIN32
extern __declspec(dllimport) int __stdcall SetWindowPos(
    void *hWnd, void *hWndInsertAfter, int X, int Y, int cx, int cy, unsigned int uFlags);
#define W32_HWND_NOTOPMOST ((void *)(intptr_t)-2)
#define W32_SWP_NOMOVE     0x0002u
#define W32_SWP_NOSIZE     0x0001u
#endif

void toggle_fullscreen(int winW, int winH) {
    if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
        ClearWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
        SetWindowSize(winW, winH);
        int mon = GetCurrentMonitor();
        Vector2 mpos = GetMonitorPosition(mon);
        SetWindowPosition(
            (int)mpos.x + (GetMonitorWidth(mon) - winW) / 2,
            (int)mpos.y + (GetMonitorHeight(mon) - winH) / 2
        );
    } else {
        SetWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
#ifdef _WIN32
        SetWindowPos((void *)GetWindowHandle(), W32_HWND_NOTOPMOST,
                     0, 0, 0, 0, W32_SWP_NOMOVE | W32_SWP_NOSIZE);
#endif
    }
}

/* ===================================================================== */
/*  Curseur : texture radiale partagee + rendu (coeur + halo + trainee)  */
/* ===================================================================== */

/* Valeurs par defaut reproduisant l'image de reference : coeur bleu clair
 * quasi blanc au centre, halo bleu tres fondu sans aucun contour. */
void cursor_config_defaults(CursorConfig *c) {
    c->color               = (Color){ 90, 170, 255, 255 };
    c->size                = 16.0f;
    c->coreIntensity       = 1.0f;
    c->glowSize            = 3.2f;
    c->glowIntensity       = 0.55f;
    c->additive            = true;
    c->trailEnabled        = true;
    c->trailDuration       = 0.28f;
    c->trailMaxSegments    = 32;
    c->trailSpacing        = 6.0f;     /* ~0.4 x size : assez serre pour etre continu, assez large
                                        * pour ne pas saturer en additif (supprime le paté) */
    c->trailStartScale     = 1.00f;
    c->trailEndScale       = 0.12f;
    c->trailStartAlpha     = 0.40f;    /* modere : les copies se recouvrent, eviter de rebriller */
    c->trailEndAlpha       = 0.00f;
    c->trailUseCursorColor = true;
    c->trailColor          = (Color){ 120, 200, 255, 255 };
}

/* Genere UNE fois la texture radiale (256x256) : blanche, alpha en falloff doux
 * (gaussien) du centre (255) au bord (0). Reutilisee teintee pour le coeur, le
 * halo ET chaque segment de trainee (une seule texture, draws batches).
 * Filtrage bilineaire : les copies reduites/agrandies restent lisses. */
void cursor_halo_init(void) {
    if (gHaveHalo) return;
    const int S = 256;
    Image img = GenImageColor(S, S, (Color){ 255, 255, 255, 0 });
    unsigned char *d = (unsigned char *)img.data;   /* format R8G8B8A8 */
    const float cc = (S - 1) * 0.5f, R = S * 0.5f;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float dx = (x - cc) / R, dy = (y - cc) / R;
            float dist = sqrtf(dx * dx + dy * dy);          /* 0 au centre, ~1 au bord */
            float a = expf(-dist * dist * 5.5f);            /* falloff gaussien doux */
            a *= 1.0f - clampf((dist - 0.82f) / 0.18f, 0.0f, 1.0f);  /* coupe nette au bord */
            int o = (y * S + x) * 4;
            d[o + 0] = 255; d[o + 1] = 255; d[o + 2] = 255;
            d[o + 3] = (unsigned char)(clampf(a, 0.0f, 1.0f) * 255.0f);
        }
    }
    gHaloTex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(gHaloTex, TEXTURE_FILTER_BILINEAR);
    gHaveHalo = true;
}

void cursor_halo_unload(void) {
    if (gHaveHalo) { UnloadTexture(gHaloTex); gHaveHalo = false; }
}

/* Dessine la texture radiale centree sur 'center', diametre 'diam', teinte 'tint'. */
static void halo_sprite(Vector2 center, float diam, Color tint) {
    if (diam < 1.0f || tint.a == 0) return;
    float h = diam * 0.5f;
    DrawTexturePro(gHaloTex,
        (Rectangle){ 0, 0, (float)gHaloTex.width, (float)gHaloTex.height },
        (Rectangle){ center.x - h, center.y - h, diam, diam },
        (Vector2){ 0, 0 }, 0.0f, tint);
}

/* Dessine l'image (curseur PNG) centree sur 'center', diametre 'diam'. */
static void cursor_image_sprite(Vector2 center, float diam, unsigned char alpha) {
    float h = diam * 0.5f;
    DrawTexturePro(gCursorTex.tex,
        (Rectangle){ 0, 0, (float)gCursorTex.tex.width, (float)gCursorTex.tex.height },
        (Rectangle){ center.x - h, center.y - h, diam, diam },
        (Vector2){ 0, 0 }, 0.0f, (Color){ 255, 255, 255, alpha });
}

/* Curseur genere = halo (large, teinte couleur, alpha faible) + coeur (petit,
 * eclairci vers le blanc). En additif, la superposition vire au blanc au centre.
 * Le blend mode est gere par l'appelant (un seul Begin/End pour trainee+curseur). */
static void cursor_generated(Vector2 pos, const CursorConfig *c) {
    Color glow = c->color;
    glow.a = (unsigned char)(clampf(c->glowIntensity, 0.0f, 1.0f) * 255.0f);
    halo_sprite(pos, c->size * 2.0f * c->glowSize, glow);
    Color core = {
        (unsigned char)(c->color.r + (255 - c->color.r) * 0.65f),
        (unsigned char)(c->color.g + (255 - c->color.g) * 0.65f),
        (unsigned char)(c->color.b + (255 - c->color.b) * 0.65f),
        (unsigned char)(clampf(c->coreIntensity, 0.0f, 1.0f) * 255.0f)
    };
    halo_sprite(pos, c->size * 2.0f, core);
}

/* --- Trainee du curseur dans les menus (coordonnees ecran directes) --- */
static TrailPt sMenuTrail[TRAIL_MAX];
static int     sMenuTrailHead  = 0;
static int     sMenuTrailCount = 0;
static float   sMenuTrailLastX = -9999.0f;
static float   sMenuTrailLastY = -9999.0f;

void cursor_menu_trail_reset(void) {
    sMenuTrailCount = 0;
    sMenuTrailHead  = 0;
    sMenuTrailLastX = -9999.0f;
    sMenuTrailLastY = -9999.0f;
}

/* Enregistre la position courante dans le buffer de trainee menu.
 * Emission par distance (comme en jeu) : jamais de paté, meme lenteur. */
static void cursor_menu_update(Vector2 pos) {
    if (!gCursor.trailEnabled) return;
    float dx   = pos.x - sMenuTrailLastX;
    float dy   = pos.y - sMenuTrailLastY;
    float dist = sqrtf(dx * dx + dy * dy);
    float sp   = gCursor.trailSpacing;
    /* premier appel ou teleportation (alt-tab, resize…) : ancre sans emettre */
    if (sMenuTrailLastX < -9990.0f || dist > sp * (float)TRAIL_MAX) {
        sMenuTrailLastX = pos.x; sMenuTrailLastY = pos.y; return;
    }
    if (dist < sp) return;
    float nowS = (float)GetTime();
    int cap   = gCursor.trailMaxSegments < TRAIL_MAX ? gCursor.trailMaxSegments : TRAIL_MAX;
    int steps = (int)(dist / sp); if (steps > cap) steps = cap;
    for (int i = 0; i < steps; i++) {
        float t  = (float)(i + 1) / (float)steps;
        TrailPt *pt = &sMenuTrail[sMenuTrailHead];
        pt->x = sMenuTrailLastX + dx * t;
        pt->y = sMenuTrailLastY + dy * t;
        pt->t = nowS;
        sMenuTrailHead = (sMenuTrailHead + 1) % TRAIL_MAX;
        if (sMenuTrailCount < cap) sMenuTrailCount++; else sMenuTrailCount = cap;
    }
    sMenuTrailLastX = pos.x; sMenuTrailLastY = pos.y;
}

/* Dessine trainee + tete du curseur a une position ecran arbitraire.
 * Utilise par le rendu dans les menus quand cursorInMenu est active. */
void cursor_draw_at(Vector2 pos, int projH) {
    cursor_menu_update(pos);
    bool image  = (gCursorMode == 1 && gCursorTex.active);
    float tNow  = (float)GetTime();
    if (image) {
        float diam = (float)projH * 0.13f;
        if (gCursor.trailEnabled && sMenuTrailCount > 0) {
            int nVis = sMenuTrailCount < gCursor.trailMaxSegments
                       ? sMenuTrailCount : gCursor.trailMaxSegments;
            for (int ti = nVis - 1; ti >= 0; ti--) {
                int idx = ((sMenuTrailHead - 1 - ti) % TRAIL_MAX + TRAIL_MAX) % TRAIL_MAX;
                float tt = clampf((tNow - sMenuTrail[idx].t) / gCursor.trailDuration, 0.0f, 1.0f);
                float sc = gCursor.trailStartScale + (gCursor.trailEndScale - gCursor.trailStartScale) * tt;
                float al = gCursor.trailStartAlpha + (gCursor.trailEndAlpha - gCursor.trailStartAlpha) * tt;
                if (al <= 0.003f) continue;
                Vector2 sp2 = { sMenuTrail[idx].x, sMenuTrail[idx].y };
                cursor_image_sprite(sp2, diam * sc, (unsigned char)(clampf(al, 0.0f, 1.0f) * 255.0f));
            }
        }
        cursor_image_sprite(pos, diam, 255);
    } else {
        const CursorConfig *c = &gCursor;
        float glowDiam = c->size * 2.0f * c->glowSize;
        if (c->additive) BeginBlendMode(BLEND_ADDITIVE);
        if (c->trailEnabled && sMenuTrailCount > 0) {
            int nVis = sMenuTrailCount < c->trailMaxSegments
                       ? sMenuTrailCount : c->trailMaxSegments;
            Color base = c->trailUseCursorColor ? c->color : c->trailColor;
            for (int ti = nVis - 1; ti >= 0; ti--) {
                int idx = ((sMenuTrailHead - 1 - ti) % TRAIL_MAX + TRAIL_MAX) % TRAIL_MAX;
                float tt = clampf((tNow - sMenuTrail[idx].t) / c->trailDuration, 0.0f, 1.0f);
                float sc = c->trailStartScale + (c->trailEndScale - c->trailStartScale) * tt;
                float al = c->trailStartAlpha + (c->trailEndAlpha - c->trailStartAlpha) * tt;
                if (al <= 0.003f) continue;
                Vector2 sp2 = { sMenuTrail[idx].x, sMenuTrail[idx].y };
                Color tc = base; tc.a = (unsigned char)(clampf(al, 0.0f, 1.0f) * 255.0f);
                halo_sprite(sp2, glowDiam * sc, tc);
            }
        }
        cursor_generated(pos, c);
        if (c->additive) EndBlendMode();
    }
}

/* ===================================================================== */
/*  Etat d'une partie                                                    */
/* ===================================================================== */
/* Particule de juice (eclat au hit). */


void play_unload(Play *p) {
    if (!p->loaded) return;
    if (p->haveMusic) UnloadMusicStream(p->music);  /* avant sspm_free : l'audio pointe dans fileData */
    free(p->state); p->state = NULL;
    sspm_free(&p->map);
    p->loaded = false; p->haveMusic = false; p->N = 0;
}

/* Remet la partie a zero (sans recharger). La musique est stoppee : elle
 * (re)demarrera a la fin du decompte. */
void play_reset(Play *p) {
    p->nowMs = 0.0f; p->clockMs = 0.0;
    p->paused = false; p->unpauseCountdown = 0.0f; p->finished = false; p->gameOver = false; p->endDelay = 0.0f;
    p->head = 0; p->cx = p->cy = 0.0f;
    p->score = p->hits = p->misses = p->combo = p->maxCombo = 0;
    p->missFlash = 0.0f;
    p->missTrackCount = 0;
    p->hp = 1.0f;
    if (p->state && p->N) memset(p->state, NOTE_PENDING, p->N);
    if (p->haveMusic) StopMusicStream(p->music);
    p->scoreSaved  = false;
    p->trailHead   = 0;
    p->trailCount  = 0;
    p->trailLastX  = p->cx;   /* = 0 : pas de rafale au demarrage */
    p->trailLastY  = p->cy;
    p->partHead    = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) p->parts[i].life = 0.0f;
    p->pulse = 0.0f; p->hitFlash = 0.0f; p->comboFlashT = 0.0f; p->comboFlashN = 0;
    p->apFromX = 0.0f; p->apFromY = 0.0f; p->apFromMs = 0.0f;
    p->apTargetNote = UINT32_MAX; p->apVelX = 0.0f; p->apVelY = 0.0f;
}

/* Mode Entrainement : repositionne la lecture a `ms` (seek musique + horloge +
 * etats des notes). Les notes avant `ms` sont marquees consommees (NOTE_HIT :
 * ignorees par le hit-loop ET le rendu) ; celles a partir de `ms` repassent
 * PENDING. Les stats de la section sont remises a zero pour refleter la tentative
 * courante. Reutilise le motif de seek du skip-intro (cf. main.c). */
void play_seek(Play *p, float ms) {
    if (ms < 0.0f) ms = 0.0f;
    /* premiere note dont le timestamp est >= ms (notes triees par ms) */
    size_t h = 0;
    while (h < p->N && p->map.notes[h].ms < ms) h++;
    p->head = h;
    if (p->state && p->N) {
        if (h > 0)     memset(p->state,     NOTE_HIT,     h);            /* consommees : ignorees */
        if (h < p->N)  memset(p->state + h, NOTE_PENDING, p->N - h);     /* a (re)jouer */
    }
    /* stats de la section (repartent a zero a chaque saut / boucle) */
    p->score = p->hits = p->misses = p->combo = p->maxCombo = 0;
    p->missTrackCount = 0;
    p->missFlash = 0.0f;
    p->hp = 1.0f;
    p->finished = false; p->gameOver = false; p->endDelay = 0.0f; p->scoreSaved = false;
    p->clockMs = (double)ms;
    p->nowMs   = ms;
    if (p->haveMusic) SeekMusicStream(p->music, ms > 0.0f ? ms / 1000.0f : 0.0f);
    /* trainee + particules deviennent incoherentes apres un saut : on les efface */
    p->trailHead = p->trailCount = 0;
    p->trailLastX = p->cx; p->trailLastY = p->cy;
    for (int i = 0; i < MAX_PARTICLES; i++) p->parts[i].life = 0.0f;
    p->pulse = 0.0f; p->hitFlash = 0.0f; p->comboFlashT = 0.0f; p->comboFlashN = 0;
    p->apFromX = p->cx; p->apFromY = p->cy; p->apFromMs = ms;
    p->apTargetNote = UINT32_MAX; p->apVelX = 0.0f; p->apVelY = 0.0f;
}

/* Emet n particules depuis 'at' (plan z=0), explosant vers l'exterieur. */
static void juice_spawn(Play *p, Vector3 at, Color base, int n) {
    for (int k = 0; k < n; k++) {
        Particle *pa = &p->parts[p->partHead];
        p->partHead = (p->partHead + 1) % MAX_PARTICLES;
        float ang = (float)GetRandomValue(0, 6283) / 1000.0f;
        float spd = (1.2f + (float)GetRandomValue(0, 200) / 100.0f) * gJuiceSpeed;
        pa->vel   = (Vector3){ cosf(ang) * spd, sinf(ang) * spd,
                               (float)GetRandomValue(-60, 60) / 100.0f * gJuiceSpeed };
        pa->pos   = at;
        pa->life0 = pa->life = gJuiceLife * (0.75f + (float)GetRandomValue(0, 25) / 100.0f);
        pa->col   = base;
    }
}

/* Avance les particules + decroissance des energies (pulse/flash/combo). */
static void juice_update(Play *p, float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *pa = &p->parts[i];
        if (pa->life <= 0.0f) continue;
        pa->life  -= dt;
        pa->pos.x += pa->vel.x * dt;
        pa->pos.y += pa->vel.y * dt;
        pa->pos.z += pa->vel.z * dt;
        pa->vel.y -= 2.2f * dt;                         /* legere gravite */
        float drag = 1.0f - 2.6f * dt; if (drag < 0.0f) drag = 0.0f;
        pa->vel.x *= drag; pa->vel.y *= drag; pa->vel.z *= drag;
    }
    p->pulse       -= dt * 3.5f; if (p->pulse       < 0.0f) p->pulse       = 0.0f;
    p->hitFlash    -= dt * 4.0f; if (p->hitFlash    < 0.0f) p->hitFlash    = 0.0f;
    p->comboFlashT -= dt;        if (p->comboFlashT < 0.0f) p->comboFlashT = 0.0f;
}

void play_cursor(Play *p, bool autoplay, int sw, int sh) {
    if (autoplay) {
        float now = p->nowMs - gAudioOffsetMs;
        /* Trouver la prochaine note pending */
        uint32_t to_i = p->N;
        for (size_t i = p->head; i < p->N; i++) {
            if (p->state[i] == NOTE_PENDING) { to_i = (uint32_t)i; break; }
        }
        if (to_i == p->N) return;
        float toX  = note_wx(&p->map.notes[to_i]);
        float toY  = note_wy(&p->map.notes[to_i]);
        float toMs = p->map.notes[to_i].ms;
        /* Si la cible a change : memoriser la direction du segment sortant (tangente d'entree)
         * et sauvegarder la position reelle comme nouveau point de depart. */
        if (to_i != p->apTargetNote) {
            float odx = p->cx - p->apFromX;
            float ody = p->cy - p->apFromY;
            float olen = sqrtf(odx * odx + ody * ody);
            if (olen > 0.001f) {
                p->apVelX = odx / olen;
                p->apVelY = ody / olen;
            }
            /* sinon on conserve la velocite precedente (ou zero pour la 1re note) */
            p->apFromX      = p->cx;
            p->apFromY      = p->cy;
            p->apFromMs     = now;
            p->apTargetNote = to_i;
        }
        float span = toMs - p->apFromMs;
        float t = (span > 0.001f) ? clampf((now - p->apFromMs) / span, 0.0f, 1.0f) : 1.0f;
        /* ease-out cubique */
        t = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        float dx = toX - p->apFromX, dy = toY - p->apFromY;
        float segLen = sqrtf(dx * dx + dy * dy);
        /* Bezier quadratique : P0=depart, P1=tangente d'entree projetee, P2=cible.
         * Le point de controle en direction du segment precedent cree un virage souple
         * au lieu d'un changement de cap abrupt. */
        if (segLen > 0.01f && (p->apVelX * p->apVelX + p->apVelY * p->apVelY) > 0.001f) {
            float k     = segLen * 0.4f;
            float ctrlX = p->apFromX + p->apVelX * k;
            float ctrlY = p->apFromY + p->apVelY * k;
            float u     = 1.0f - t;
            p->cx = u * u * p->apFromX + 2.0f * u * t * ctrlX + t * t * toX;
            p->cy = u * u * p->apFromY + 2.0f * u * t * ctrlY + t * t * toY;
        } else {
            p->cx = p->apFromX + dx * t;
            p->cy = p->apFromY + dy * t;
        }
        return;
    }
    if (gTablet) {
        /* tablette : position absolue du stylet projetee sur le plan de jeu.
         * halfH/halfW precalcules : le tanf n'est recalcule qu'au changement de taille d'ecran.
         * L'aspect utilise est celui du FRAMEBUFFER rendu (la render texture si la
         * resolution interne est active), pour que le curseur tombe pile sous le stylet. */
        int pw = gRtW > 0 ? gRtW : sw;
        int ph = gRtH > 0 ? gRtH : sh;
        static float sHalfH = 0.0f, sHalfW = 0.0f;
        static int   sPW = 0, sPH = 0;
        if (pw != sPW || ph != sPH || sHalfH == 0.0f) {
            sHalfH = tanf(35.0f * (PI / 180.0f)) * CAM_BACK;
            sHalfW = sHalfH * ((ph > 0) ? (float)pw / (float)ph : 1.0f);
            sPW = pw; sPH = ph;
        }
        Vector2 mp = GetMousePosition();
        float ndcx = (sw > 0) ? (mp.x / (float)sw) * 2.0f - 1.0f : 0.0f;
        float ndcy = (sh > 0) ? 1.0f - (mp.y / (float)sh) * 2.0f : 0.0f;
        p->cx = clampf(ndcx * sHalfW, -CURSOR_RANGE, CURSOR_RANGE);
        p->cy = clampf(ndcy * sHalfH, -CURSOR_RANGE, CURSOR_RANGE);
    } else {
        Vector2 md = GetMouseDelta();
        p->cx = clampf(p->cx + md.x * gSens, -CURSOR_RANGE, CURSOR_RANGE);
        p->cy = clampf(p->cy - md.y * gSens, -CURSOR_RANGE, CURSOR_RANGE);
    }
}

/* Tick de jeu : horloge, curseur, hit/miss, culling, timers d'effets. */
void play_update(Play *p, bool autoplay, float dt, int sw, int sh) {
    /* Practice force l'horloge manuelle meme a 1.0x : on doit pouvoir seek/boucler
     * librement (GetMusicTimePlayed suivrait l'audio sans nous laisser repositionner). */
    bool masterClock = (p->haveMusic && gRate == 1.0f && gMode != MODE_PRACTICE);
    if (masterClock) {
        /* horloge maitresse = position audio (synchro parfaite a vitesse normale) */
        if (!p->paused && !p->finished) UpdateMusicStream(p->music);
        p->nowMs = GetMusicTimePlayed(p->music) * 1000.0f;
    } else {
        /* pas d'audio OU vitesse != 1.0 OU Practice : horloge manuelle a la vitesse choisie.
         * L'audio (pitch regle sur gRate) avance a la meme cadence -> reste cale. */
        if (p->haveMusic && !p->paused && !p->finished) UpdateMusicStream(p->music);
        if (!p->paused && !p->finished) p->clockMs += (double)dt * 1000.0 * (double)gRate;
        p->nowMs = (float)p->clockMs;
    }
    if (p->haveMusic) SetMusicVolume(p->music, gMusicVolume);

    /* Practice : boucle automatique. Quand on depasse la fin de section, on
     * revient a l'ancre A (jamais d'ecran de resultats en entrainement). */
    if (gMode == MODE_PRACTICE && !p->paused && !p->finished) {
        float endMs = (gPracticeLoop && gPracticeB > gPracticeA + 1.0f)
                      ? gPracticeB
                      : (float)(p->map.lastMs > 0 ? p->map.lastMs : 0) + 400.0f;
        if (p->nowMs >= endMs) {
            play_seek(p, gPracticeA);
        }
    }

    if (!p->paused && !p->finished) {
        play_cursor(p, autoplay, sw, sh);

        /* enregistrement trainee : emission PAR DISTANCE (jamais par temps).
         * On ne seme un point que si le curseur a parcouru >= trailSpacing depuis
         * le dernier, puis on interpole pour des points reguliers le long du
         * trajet. Resultat : curseur immobile -> rien (la trainee s'eteint par
         * age) ; lent -> points rares, jamais empiles (plus de paté) ; rapide ->
         * trous combles. trailSpacing est en pixels : on le convertit en unites
         * monde via l'echelle fixe de la camera (z=0). */
        if (gCursor.trailEnabled && gCursor.trailSpacing > 0.0f) {
            int   projH      = gRtH > 0 ? gRtH : sh;
            float pxPerWorld = (projH > 0) ? (float)projH / (2.0f * tanf(35.0f * (PI / 180.0f)) * CAM_BACK) : 1.0f;
            float spacing    = (pxPerWorld > 0.0001f) ? gCursor.trailSpacing / pxPerWorld : 0.0f;
            float dx = p->cx - p->trailLastX, dy = p->cy - p->trailLastY;
            float dist = sqrtf(dx * dx + dy * dy);
            if (spacing > 0.00001f && dist >= spacing) {
                float nowS = p->nowMs / 1000.0f;
                int   cap  = gCursor.trailMaxSegments < TRAIL_MAX ? gCursor.trailMaxSegments : TRAIL_MAX;
                int   steps = (int)(dist / spacing);
                bool  teleport = (steps > cap);     /* mouvement enorme / retour de fenetre */
                if (teleport) steps = cap;
                for (int i = 1; i <= steps; i++) {
                    /* normal : pas fixe de 'spacing' ; teleport : repartir 'cap' points sur tout le trajet */
                    float t  = teleport ? (float)i / (float)steps : (float)i * spacing / dist;
                    float px = p->trailLastX + dx * t;
                    float py = p->trailLastY + dy * t;
                    p->trail[p->trailHead] = (TrailPt){ px, py, nowS };
                    p->trailHead = (p->trailHead + 1) % TRAIL_MAX;
                    if (p->trailCount < TRAIL_MAX) p->trailCount++;
                }
                /* avancer lastEmit d'un multiple EXACT de l'espacement (sinon snap a pos) */
                if (teleport) { p->trailLastX = p->cx; p->trailLastY = p->cy; }
                else {
                    float covered = (float)steps * spacing / dist;
                    p->trailLastX += dx * covered;
                    p->trailLastY += dy * covered;
                }
            }
        }

        /* valeurs effectives selon les mods */
        float hitWin = gHitWindowMs * ((gMods & MOD_HARDROCK) ? 0.65f : 1.0f);
        float hpMiss = HP_MISS      * ((gMods & MOD_HARDROCK) ? 1.60f : 1.0f);
        bool  noFail = (gMods & MOD_NOFAIL) != 0 || gMode == MODE_ZEN || gMode == MODE_PRACTICE;
        bool  sudden = (gMods & MOD_SUDDEN) != 0 && gMode != MODE_ZEN && gMode != MODE_PRACTICE;

        float now = p->nowMs - gAudioOffsetMs;   /* offset audio : decale la synchro notes<->musique */
        for (size_t i = p->head; i < p->N; i++) {
            float dtms = p->map.notes[i].ms - now;
            if (dtms > hitWin) break;                /* notes futures (triees) : on s'arrete */
            if (p->state[i] != NOTE_PENDING) continue;
            if (dtms < -hitWin) {                    /* trop tard -> rate */
                p->state[i] = NOTE_MISS; p->misses++; p->combo = 0; p->missFlash = 0.28f;
                if (p->missTrackCount < MAX_MISS_TRACK) {
                    p->missTrack[p->missTrackCount].ms = now;
                    p->missTrack[p->missTrackCount].x  = note_wx(&p->map.notes[i]);
                    p->missTrack[p->missTrackCount].y  = note_wy(&p->map.notes[i]);
                    p->missTrackCount++;
                }
                bool die = (sudden && !autoplay);
                if (!gGodMode && !autoplay && !noFail) {
                    p->hp = clampf(p->hp - hpMiss, 0.0f, 1.0f);
                    if (p->hp <= 0.0f) die = true;
                }
                if (die) {
                    p->finished = true; p->gameOver = true;
                    if (p->haveMusic) StopMusicStream(p->music);
                }
            } else {
                float hx = clampf(p->cx, -GRID_SPACING, GRID_SPACING);
                float hy = clampf(p->cy, -GRID_SPACING, GRID_SPACING);
                bool on = autoplay ||
                          (fabsf(hx - note_wx(&p->map.notes[i])) <= HIT_TOL &&
                           fabsf(hy - note_wy(&p->map.notes[i])) <= HIT_TOL);
                if (on) {
                    p->state[i] = NOTE_HIT; p->hits++; p->combo++;
                    if (p->combo > p->maxCombo) p->maxCombo = p->combo;
                    p->score += (int)lroundf((100 + (p->combo - 1) * 5) * gScoreMult);
                    p->hp = clampf(p->hp + HP_HIT, 0.0f, 1.0f);
                    if (gHitsoundSnd.active) {
                        SetSoundVolume(gHitsoundSnd.snd, gHitsoundVolume);
                        PlaySound(gHitsoundSnd.snd);
                    }
                    /* juice visuel selon le mode */
                    { bool bgDown = (p->combo % 4 == 1);
                      if (gSettings.bgStyle == BG_PULSE) bg_pulse_on_beat(bgDown);
                      bg_on_beat(bgDown); }
                    if (gJuiceMode != 0) {
                        Vector3 at = { note_wx(&p->map.notes[i]), note_wy(&p->map.notes[i]), 0.0f };
                        if (gJuiceMode == 1 || gJuiceMode == 3) juice_spawn(p, at, note_color(i), gJuiceCount);
                        if (gJuiceMode == 2 || gJuiceMode == 3) p->pulse = 1.0f;
                        if (gJuiceMode == 3) p->hitFlash = 1.0f;
                        if ((gJuiceMode == 2 || gJuiceMode == 3) && gJuiceCombo > 0 &&
                            p->combo > 0 && p->combo % gJuiceCombo == 0) {
                            p->comboFlashT = 0.7f; p->comboFlashN = p->combo;
                        }
                    }
                }
            }
        }
        while (p->head < p->N) {
            float z = -gApproachDist * (p->map.notes[p->head].ms - now) / gApproachMs;
            if (z > gCullBehind) p->head++; else break;
        }
        /* fin par comptage : desactivee en Practice (la boucle gere le rebouclage) */
        if (p->N > 0 && (p->hits + p->misses) >= (int)p->N && !p->finished
            && p->endDelay <= 0.0f && gMode != MODE_PRACTICE)
            p->endDelay = 1.0f;
        juice_update(p, dt);
        /* Beat punch : (de)reclame l'analyse FFT du grave selon le reglage (idempotent). */
        bg_punch_set_active(gShadersOn && gBeatPunchOn);
        bg_update(gSettings.bgStyle, dt);
    }

    /* Décompte avant écran de fin (win uniquement — game over est immédiat) */
    if (p->endDelay > 0.0f && !p->finished) {
        p->endDelay -= dt;
        if (p->endDelay <= 0.0f) {
            p->endDelay = 0.0f;
            p->finished = true;
        }
    }

    if (p->missFlash > 0.0f) p->missFlash -= dt;
}

/* Scene 3D : grille, notes (cube ou mesh), effets, curseur (utilise p->nowMs). */
void play_draw_scene(Play *p, Camera3D cam, bool autoplay) {
    (void)autoplay;
    /* Fond procedural — dessin 2D avant le mode 3D (appelle ClearBackground). */
    bg_draw(gSettings.bgStyle, gSettings.bgIntensity);

    /* Pulse de beat (modes Pulse/Neon) : leger zoom de la camera sur le hit.
       On modifie la copie locale de 'cam' -> overlay 2D (curseur/trainee) reste aligne. */
    float glow = 0.0f;
    if (gJuiceMode == 2 || gJuiceMode == 3) {
        float k = p->pulse > 1.0f ? 1.0f : p->pulse;
        cam.position.z = CAM_BACK - k * 0.10f * gJuicePulse;
        glow = p->pulse * gJuicePulse;
    }
    BeginMode3D(cam);
    draw_grid_frame(glow);

    float now = p->nowMs - gAudioOffsetMs;   /* meme offset que play_update : hit et rendu restent alignes */
    for (size_t i = p->head; i < p->N; i++) {
        if (p->state[i] == NOTE_HIT || p->state[i] == NOTE_MISS) continue;
        Vector3 pos = note_world(&p->map.notes[i], now);
        if (pos.z < -gApproachDist) break;        /* notes triees : suivantes plus loin */

        /* La note reste visible tant qu'elle est encore touchable (fenetre de hit
           "en retard"), bornee pour ne pas foncer dans la camera. Sinon on a
           l'impression d'un perfect hit alors que la note a deja disparu. */
        float zLate = gApproachDist * gHitWindowMs / gApproachMs;
        if (zLate < 0.30f) zLate = 0.30f;
        if (zLate > 2.40f) zLate = 2.40f;
        if (pos.z >= zLate) continue;             /* hors de la zone encore touchable */

        float az = pos.z < 0 ? -pos.z : 0.0f;
        float a = 1.0f, fadeStart = gApproachDist * (1.0f - FADE_FRAC);
        if (az > fadeStart) a = (gApproachDist - az) / (gApproachDist * FADE_FRAC);
        if (pos.z > 0.0f) a = 1.0f - pos.z / zLate; /* fondu apres le plan, sur toute la fenetre de retard */
        a = clampf(a, 0.0f, 1.0f);
        /* Hidden : la note s'efface dans la derniere moitie avant le plan */
        if (gMods & MOD_HIDDEN) a *= clampf(az / (gApproachDist * 0.5f), 0.0f, 1.0f);
        /* Vanish : la note disparait rapidement dans les 20% finaux avant le plan */
        if (gMods & MOD_VANISH) a *= clampf(az / (gApproachDist * 0.20f), 0.0f, 1.0f);

        Color col = note_color(i);
        col.a = (unsigned char)(255.0f * a);

        if (gNoteMesh.active) {
            float ms = gNoteMesh.scale * gNoteScale;
            Vector3 dp = { pos.x - ms * gNoteMesh.center.x,
                           pos.y - ms * gNoteMesh.center.y,
                           pos.z - ms * gNoteMesh.center.z };
            DrawModelEx(gNoteMesh.model, dp, (Vector3){ 0, 1, 0 }, 0.0f,
                        (Vector3){ ms, ms, ms }, col);
        } else {
            float cs = CUBE_SIZE * gNoteScale;
            DrawBillboard(cam, gWhiteTex, pos, cs, col);
        }
    }

    /* particules de juice (Neon = melange additif pour l'effet glow) */
    if (gJuiceMode == 1 || gJuiceMode == 3) {
        bool neon = (gJuiceMode == 3);
        if (neon) BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < MAX_PARTICLES; i++) {
            Particle *pa = &p->parts[i];
            if (pa->life <= 0.0f) continue;
            float f = pa->life / pa->life0;
            Color c = pa->col;
            c.a = (unsigned char)(255.0f * (f > 1.0f ? 1.0f : f));
            float sz = gJuiceSize * (0.4f + 0.6f * f);
            DrawBillboard(cam, gWhiteTex, pa->pos, sz, c);
        }
        if (neon) EndBlendMode();
    }
    EndMode3D();

    /* trainee + curseur : overlay 2D projete depuis le plan de jeu.
       NB : on dessine dans le framebuffer courant (fenetre OU render texture).
       GetWorldToScreen() utilise la taille de la FENETRE ; quand on rend dans
       une RT basse resolution il faut projeter avec la taille de la RT, sinon
       le curseur/la trainee sont decentres et rognes. */
    {
        int projW = gRtW > 0 ? gRtW : GetScreenWidth();
        int projH = gRtH > 0 ? gRtH : GetScreenHeight();
        const CursorConfig *c = &gCursor;
        bool image = (gCursorMode == 1 && gCursorTex.active);
        float imgDiam  = (float)projH * 0.13f;            /* diametre du curseur image */
        float glowDiam = c->size * 2.0f * c->glowSize;    /* diametre de base d'un blob de trainee */

        Vector2 cpos = GetWorldToScreenEx((Vector3){ p->cx, p->cy, 0.0f }, cam, projW, projH);

        /* Combien de segments dessiner : les plus recents, dans la fenetre de
         * temps, cappes a trailMaxSegments. Calcule une fois, partage image/genere. */
        int nVis = 0;
        if (c->trailEnabled && p->trailCount > 0) {
            float tNow = p->nowMs / 1000.0f;
            int maxSeg = c->trailMaxSegments < TRAIL_MAX ? c->trailMaxSegments : TRAIL_MAX;
            for (int ti = 0; ti < p->trailCount && nVis < maxSeg; ti++) {
                int idx = ((p->trailHead - 1 - ti) % TRAIL_MAX + TRAIL_MAX) % TRAIL_MAX;
                if (tNow - p->trail[idx].t > c->trailDuration) break;
                nVis++;
            }
        }

        if (image) {
            /* --- Mode image : trainee = copies du PNG, puis le curseur --- */
            float tNow = p->nowMs / 1000.0f;
            for (int ti = nVis - 1; ti >= 0; ti--) {       /* du plus vieux au plus recent */
                int idx = ((p->trailHead - 1 - ti) % TRAIL_MAX + TRAIL_MAX) % TRAIL_MAX;
                float tt = clampf((tNow - p->trail[idx].t) / c->trailDuration, 0.0f, 1.0f);
                float sc = c->trailStartScale + (c->trailEndScale - c->trailStartScale) * tt;
                float al = c->trailStartAlpha + (c->trailEndAlpha - c->trailStartAlpha) * tt;
                if (al <= 0.003f) continue;
                Vector2 sp = GetWorldToScreenEx((Vector3){ p->trail[idx].x, p->trail[idx].y, 0.0f }, cam, projW, projH);
                cursor_image_sprite(sp, imgDiam * sc, (unsigned char)(clampf(al, 0.0f, 1.0f) * 255.0f));
            }
            cursor_image_sprite(cpos, imgDiam, 255);
        } else {
            /* --- Mode genere : trainee + coeur/halo dans UN SEUL bloc de blend
             *     (additif optionnel) pour limiter les changements d'etat GPU. --- */
            if (c->additive) BeginBlendMode(BLEND_ADDITIVE);
            if (nVis > 0) {
                float tNow = p->nowMs / 1000.0f;
                Color base = c->trailUseCursorColor ? c->color : c->trailColor;
                for (int ti = nVis - 1; ti >= 0; ti--) {
                    int idx = ((p->trailHead - 1 - ti) % TRAIL_MAX + TRAIL_MAX) % TRAIL_MAX;
                    float tt = clampf((tNow - p->trail[idx].t) / c->trailDuration, 0.0f, 1.0f);
                    float sc = c->trailStartScale + (c->trailEndScale - c->trailStartScale) * tt;
                    float al = c->trailStartAlpha + (c->trailEndAlpha - c->trailStartAlpha) * tt;
                    if (al <= 0.003f) continue;
                    Vector2 sp = GetWorldToScreenEx((Vector3){ p->trail[idx].x, p->trail[idx].y, 0.0f }, cam, projW, projH);
                    Color tc = base; tc.a = (unsigned char)(clampf(al, 0.0f, 1.0f) * 255.0f);
                    halo_sprite(sp, glowDiam * sc, tc);
                }
            }
            cursor_generated(cpos, c);   /* coeur + halo dessines APRES la trainee */
            if (c->additive) EndBlendMode();
        }
    }

    /* Flashlight : zone sombre autour du curseur de jeu.
       DrawRing(inner=r, outer=diagonale ecran) couvre tout ce qui est hors du cercle.
       Le centre suit p->cx/cy (curseur de jeu, identique a la trainee). */
    if (gMods & MOD_FLASHLIGHT) {
        int fW = gRtW > 0 ? gRtW : GetScreenWidth();
        int fH = gRtH > 0 ? gRtH : GetScreenHeight();
        Vector2 fc = GetWorldToScreenEx((Vector3){ p->cx, p->cy, 0.0f }, cam, fW, fH);
        float r  = (float)fH * 0.26f;
        float ro = sqrtf((float)(fW * fW + fH * fH)) + r;
        /* bord interieur progressif */
        float feather = r * 0.28f;
        int steps = 5;
        for (int i = 0; i < steps; i++) {
            float r0 = r - feather + feather * (float)i / steps;
            float r1 = r - feather + feather * (float)(i + 1) / steps;
            unsigned char a = (unsigned char)(210 * (i + 1) / steps);
            DrawRing(fc, r0 < 0 ? 0 : r0, r1, 0.0f, 360.0f, 64, (Color){ 0, 0, 0, a });
        }
        /* zone sombre principale */
        DrawRing(fc, r, ro, 0.0f, 360.0f, 128, (Color){ 0, 0, 0, 215 });
    }
}

/* Rampe thermique pour la heatmap de miss : t in [0,1].
 * transparent -> rouge sombre -> rouge -> orange -> jaune clair. */
static Color heat_color(float t) {
    t = clampf(t, 0.0f, 1.0f);
    float r, g, b;
    if (t < 0.5f) {                 /* rouge sombre -> rouge-orange */
        float u = t / 0.5f;
        r = 120.0f + u * 135.0f;    /* 120 -> 255 */
        g = u * 75.0f;              /*   0 -> 75  */
        b = 0.0f;
    } else {                        /* orange -> jaune presque blanc */
        float u = (t - 0.5f) / 0.5f;
        r = 255.0f;
        g = 75.0f + u * 180.0f;     /*  75 -> 255 */
        b = u * 175.0f;             /*   0 -> 175 */
    }
    unsigned char a = (unsigned char)(36.0f + 200.0f * t);
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, a };
}

/* Grade selon la precision (0..100). Renvoie la lettre et remplit *col. */
static char grade_letter(float acc, Color *col) {
    if (acc >= 98.0f) { *col = (Color){ 255, 215,  40, 255 }; return 'S'; }
    if (acc >= 93.0f) { *col = (Color){ 120, 220, 255, 255 }; return 'A'; }
    if (acc >= 82.0f) { *col = (Color){ 120, 255, 160, 255 }; return 'B'; }
    if (acc >= 70.0f) { *col = (Color){ 255, 220, 100, 255 }; return 'C'; }
    if (acc >= 55.0f) { *col = (Color){ 255, 150,  80, 255 }; return 'D'; }
    *col = (Color){ 255, 60, 60, 255 }; return 'F';
}

void play_draw_hud(Play *p, int sw, int sh, bool autoplay) {
    static int wHint = 0;
    if (!wHint)
        wHint = MeasureText("R replay    -    ESC menu", 20);

    int total = p->hits + p->misses;
    float acc = total > 0 ? (100.0f * p->hits / total) : 100.0f;

    /* --- info chanson (haut gauche) --- */
    if (gSettings.hudShowSongInfo) {
        DrawText(p->map.songName, 14, 12, 22, RAYWHITE);
        DrawText(TextFormat("%s  -  %s", p->map.mapper, sspm_difficulty_name(p->map.difficulty)),
                 14, 38, 16, (Color){ 180, 180, 195, 255 });
    }

    /* --- compteurs haut droit (hors mode Practice) --- */
    if (gMode != MODE_PRACTICE) {
        int ry = 12;
        if (gSettings.hudShowScore) {
            const char *scoreTxt = TextFormat("%d", p->score);
            DrawText(scoreTxt, sw - 14 - MeasureText(scoreTxt, 30), ry, 30, RAYWHITE);
            ry += 36;
        }
        if (gSettings.hudShowAccuracy) {
            Color gc; grade_letter(acc, &gc);
            const char *accTxt = TextFormat("%.2f%%", acc);
            DrawText(accTxt, sw - 14 - MeasureText(accTxt, 18), ry, 18, gc);
            ry += 24;
        }
        if (gSettings.hudShowCombo && p->combo > 1) {
            const char *c = TextFormat("x%d", p->combo);
            DrawText(c, sw - 14 - MeasureText(c, 26), ry, 26, (Color){ 255, 220, 120, 255 });
            ry += 32;
        }
        { char modstr[64];
          if (mods_label(modstr, sizeof modstr))
              DrawText(modstr, sw - 14 - MeasureText(modstr, 16), ry, 16, (Color){ 255, 180, 90, 255 }); }
    }

    /* --- hits/total (petit, gauche) + grade --- */
    if (gSettings.hudShowAccuracy)
        DrawText(TextFormat("%d / %u", p->hits, p->N), 14, 64, 14, (Color){ 100, 140, 190, 180 });
    if (!p->finished && total > 0) {
        Color gc; char gl = grade_letter(acc, &gc);
        DrawText(TextFormat("%c", gl), 14, 112, 28, gc);
    }

    /* banniere de mode (Autoplay / Zen / Ladder / Aim / Practice) */
    if (autoplay || gMode != MODE_NORMAL) {
        const char *mt = autoplay          ? "AUTOPLAY"
                       : gMode == MODE_ZEN      ? "ZEN"
                       : gMode == MODE_LADDER    ? TextFormat("SPEED LADDER  -  Lv. %d  -  %gx", gLadderLevel + 1, gRate)
                       : gMode == MODE_PRACTICE  ? TextFormat("PRACTICE  -  %.2fx", gRate)
                       :                            "AIM TRAINER";
        Color mc = autoplay          ? (Color){ 190, 130, 255, 255 }
                 : gMode == MODE_ZEN      ? (Color){ 130, 220, 200, 255 }
                 : gMode == MODE_LADDER    ? (Color){ 255, 170, 90, 255 }
                 : gMode == MODE_PRACTICE  ? (Color){ 130, 200, 255, 255 }
                 :                            (Color){ 180, 160, 255, 255 };
        DrawText(mt, sw / 2 - MeasureText(mt, 18) / 2, 12, 18, mc);
    }

    /* barre de vie (cachee en Zen/Practice ou si HUD HP desactive) */
    if (gSettings.hudShowHp && !autoplay && gMode != MODE_ZEN && gMode != MODE_PRACTICE) {
        int hpX = 14, hpY = 84, hpW = 180, hpH = 8;
        DrawRectangle(hpX, hpY, hpW, hpH, (Color){ 30, 20, 20, 200 });
        if (gGodMode) {
            DrawRectangle(hpX, hpY, hpW, hpH, (Color){ 255, 200, 40, 140 });
            DrawText("GOD", hpX + hpW + 8, hpY - 1, 14, (Color){ 255, 200, 40, 220 });
        } else {
            int fill = (int)((float)hpW * clampf(p->hp, 0.0f, 1.0f));
            Color hc = p->hp > 0.5f ? (Color){ 80, 220, 100, 255 }
                     : p->hp > 0.25f ? (Color){ 220, 190, 50, 255 }
                     :                  (Color){ 220, 60, 60, 255 };
            if (fill > 0) DrawRectangle(hpX, hpY, fill, hpH, hc);
        }
        DrawRectangleLinesEx((Rectangle){ (float)hpX, (float)hpY, (float)hpW, (float)hpH },
                              1.0f, (Color){ 120, 120, 140, 180 });
        if (p->misses > 0)
            DrawText(TextFormat("%d miss", p->misses), hpX, hpY + hpH + 5, 13, (Color){ 220, 90, 90, 200 });
    }

    float prog = (p->map.lastMs > 0) ? clampf(p->nowMs / (float)p->map.lastMs, 0.0f, 1.0f) : 0.0f;
    int barY = sh - 26, barX = 14, barW = sw - 28;
    DrawRectangle(barX, barY, barW, 8, (Color){ 40, 40, 52, 255 });
    DrawRectangle(barX, barY, (int)(barW * prog), 8, (Color){ 90, 200, 255, 255 });

    /* Practice : ancre A + fin de section (B ou fin), region de boucle surlignee */
    if (gMode == MODE_PRACTICE && p->map.lastMs > 0) {
        float dur = (float)p->map.lastMs;
        float ax  = (float)barX + (float)barW * clampf(gPracticeA / dur, 0.0f, 1.0f);
        bool  loop = (gPracticeLoop && gPracticeB > gPracticeA + 1.0f);
        float endMs = loop ? gPracticeB : dur;
        float bx  = (float)barX + (float)barW * clampf(endMs / dur, 0.0f, 1.0f);
        if (loop) {
            DrawRectangle((int)ax, barY - 2, (int)(bx - ax), 12, (Color){ 120, 200, 255, 45 });
            DrawRectangle((int)bx - 1, barY - 4, 3, 16, (Color){ 255, 170, 90, 235 });   /* B (orange) */
        }
        DrawRectangle((int)ax - 1, barY - 4, 3, 16, (Color){ 110, 235, 140, 235 });       /* A (vert) */
    }

    if (gMode == MODE_PRACTICE) {
        char loopBuf[96];
        if (gPracticeLoop && gPracticeB > gPracticeA + 1.0f)
            snprintf(loopBuf, sizeof loopBuf, "Loop ON  %.1f-%.1fs", gPracticeA / 1000.0f, gPracticeB / 1000.0f);
        else
            snprintf(loopBuf, sizeof loopBuf, "Loop OFF  (start %.1fs)", gPracticeA / 1000.0f);
        DrawText(TextFormat("PRACTICE  %.2fx  -  %s", gRate, loopBuf),
                 14, sh - 62, 14, (Color){ 130, 200, 255, 230 });
        DrawText("<- -> seek   [ set A   ] set B   L loop   - / + speed   R restart   ESC menu",
                 14, sh - 46, 14, (Color){ 130, 130, 145, 255 });
    } else {
        DrawText(autoplay ? "AUTOPLAY  -  SPACE pause  -  R restart  -  F11 fullscreen  -  ESC menu"
                          : "Mouse/pen: aim  -  SPACE pause  -  R restart  -  F11 fullscreen  -  ESC menu",
                 14, sh - 46, 14, (Color){ 130, 130, 145, 255 });
    }
    /* hint skip intro : s'affiche tant que la premiere note est loin (>1 s) */
    if (!autoplay && !p->finished && p->N > 0) {
        float firstNoteTime = p->map.notes[0].ms - gAudioOffsetMs;
        if (firstNoteTime - p->nowMs > 1000.0f) {
            static const char *skipTxt = "Tab : skip intro";
            int stw = MeasureText(skipTxt, 15);
            DrawText(skipTxt, sw / 2 - stw / 2, sh - 68, 15, (Color){ 100, 160, 255, 180 });
        }
    }
    if (gMode != MODE_PRACTICE)
        DrawText(TextFormat("%d FPS", GetFPS()), sw - 70, sh - 46, 14, (Color){ 110, 180, 110, 255 });

    if (p->missFlash > 0.0f) {
        unsigned char a = (unsigned char)(120.0f * clampf(p->missFlash / 0.28f, 0.0f, 1.0f));
        DrawRectangle(0, 0, sw, 6, (Color){ 230, 40, 60, a });
        DrawRectangle(0, sh - 6, sw, 6, (Color){ 230, 40, 60, a });
        DrawRectangle(0, 0, 6, sh, (Color){ 230, 40, 60, a });
        DrawRectangle(sw - 6, 0, 6, sh, (Color){ 230, 40, 60, a });
    }

    /* juice : flash de hit (Neon) + flash de palier de combo (Pulse/Neon) */
    if (gJuiceMode == 3 && p->hitFlash > 0.0f) {
        unsigned char a = (unsigned char)(38.0f * (p->hitFlash > 1.0f ? 1.0f : p->hitFlash));
        DrawRectangle(0, 0, sw, sh, (Color){ 120, 200, 255, a });
    }
    if ((gJuiceMode == 2 || gJuiceMode == 3) && p->comboFlashT > 0.0f && p->comboFlashN > 0) {
        float f = p->comboFlashT / 0.7f;
        unsigned char a = (unsigned char)(255.0f * (f > 1.0f ? 1.0f : f));
        const char *t = TextFormat("COMBO %d", p->comboFlashN);
        int fs = 44, tw = MeasureText(t, fs);
        DrawText(t, sw / 2 - tw / 2, sh / 2 - 150, fs, (Color){ 255, 215, 110, a });
    }

    if (p->paused && !p->finished) {
        if (p->unpauseCountdown > 0.0f) {
            int n = (int)ceilf(p->unpauseCountdown);
            if (n < 1) n = 1;
            if (n > 3) n = 3;
            const char *nt = TextFormat("%d", n);
            int cfs = sh / 4; if (cfs < 80) cfs = 80; if (cfs > 220) cfs = 220;
            DrawText(nt, sw / 2 - MeasureText(nt, cfs) / 2, sh / 2 - cfs / 2, cfs,
                     (Color){ 255, 230, 120, 255 });
        } else {
            DrawText("PAUSE", sw / 2 - 40, sh / 2 - 20, 40, (Color){ 255, 230, 120, 255 });
        }
    }

    if (p->finished) {
        DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 192 });

        Color gc; char gl = grade_letter(acc, &gc);
        char gls[2] = { gl, '\0' };

        /* --- Titre --- */
        const char *title   = p->gameOver ? "GAME OVER" : "COMPLETE";
        Color       titleCol = p->gameOver ? (Color){ 255, 75, 75, 255 }
                                           : (Color){ 120, 225, 255, 255 };
        int titleFS = (int)(sh * 0.048f); if (titleFS < 20) titleFS = 20;
        int titleY  = (int)(sh * 0.058f);
        DrawText(title, sw/2 - MeasureText(title, titleFS)/2, titleY, titleFS, titleCol);

        /* Sous-titre : nom de la map + mods + new record sur une ligne */
        {
            char subBuf[220]; int soff = 0;
            soff += snprintf(subBuf + soff, sizeof subBuf - soff, "%s", p->map.songName);
            char modstr[64];
            if (gMode == MODE_AIM)
                soff += snprintf(subBuf + soff, sizeof subBuf - soff, "  (Aim Trainer)");
            else if (mods_label(modstr, sizeof modstr))
                soff += snprintf(subBuf + soff, sizeof subBuf - soff, "  [%s  x%.2f]", modstr, gScoreMult);
            if (gNewRecord && !p->gameOver)
                snprintf(subBuf + soff, sizeof subBuf - soff, "  ** NEW RECORD **");
            int subFS = (int)(sh * 0.019f); if (subFS < 11) subFS = 11;
            Color subCol = (gNewRecord && !p->gameOver) ? (Color){ 255, 215, 60, 215 }
                                                        : (Color){ 140, 162, 215, 200 };
            DrawText(subBuf, sw/2 - MeasureText(subBuf, subFS)/2,
                     titleY + titleFS + 6, subFS, subCol);
        }

        /* Ladder */
        if (gMode == MODE_LADDER) {
            const char *ls = TextFormat("Level reached : %d   -   %gx", gLadderLevel + 1, gRate);
            int lsFS = (int)(sh * 0.021f); if (lsFS < 12) lsFS = 12;
            DrawText(ls, sw/2 - MeasureText(ls, lsFS)/2,
                     titleY + titleFS + 30, lsFS, (Color){ 255, 180, 90, 255 });
        }

        /* --- Badge grade (grand, centré) --- */
        float gradeR = (float)sh * 0.162f;
        int   gcx    = sw / 2;
        int   gcy    = (int)(sh * 0.400f);

        /* Halo coloré derrière */
        DrawCircle(gcx, gcy, (int)(gradeR * 1.50f),
                   (Color){ (unsigned char)(gc.r/5), (unsigned char)(gc.g/5), (unsigned char)(gc.b/5), 62 });
        DrawCircle(gcx, gcy, (int)(gradeR * 1.15f),
                   (Color){ (unsigned char)(gc.r/4), (unsigned char)(gc.g/4), (unsigned char)(gc.b/4), 55 });
        /* Fond sombre du badge */
        DrawCircle(gcx, gcy, (int)(gradeR * 0.92f), (Color){ 8, 10, 22, 225 });
        /* Contour triple pour épaisseur */
        DrawCircleLines(gcx, gcy, gradeR * 0.92f, (Color){ gc.r, gc.g, gc.b, 210 });
        DrawCircleLines(gcx, gcy, gradeR * 0.905f, (Color){ gc.r, gc.g, gc.b, 130 });
        DrawCircleLines(gcx, gcy, gradeR * 0.890f, (Color){ gc.r, gc.g, gc.b, 60  });

        /* Lettre */
        int gfs = (int)(gradeR * 1.55f); if (gfs > 220) gfs = 220; if (gfs < 60) gfs = 60;
        int gw  = MeasureText(gls, gfs);
        DrawText(gls, gcx - gw/2 + 4, gcy - gfs/2 + 5, gfs, (Color){ 0, 0, 0, 115 }); /* ombre */
        DrawText(gls, gcx - gw/2,     gcy - gfs/2,     gfs, gc);

        /* --- Stats (5 boxes sous le badge) --- */
        int statsY  = gcy + (int)(gradeR * 0.92f) + (int)(sh * 0.032f);
        int statH   = (int)(sh * 0.082f); if (statH < 46) statH = 46;
        int statW   = (int)(sw * 0.132f); if (statW < 88) statW = 88;
        int statGap = (int)(sw * 0.014f); if (statGap < 5) statGap = 5;

        int statsRowW = 5 * statW + 4 * statGap;
        int statsX    = sw/2 - statsRowW/2;

        const char *slabels[5] = { "SCORE", "ACCURACY", "NOTES", "MISSES", "MAX COMBO" };
        char svals[5][32];
        snprintf(svals[0], sizeof svals[0], "%d",       p->score);
        snprintf(svals[1], sizeof svals[1], "%.2f%%",   acc);
        snprintf(svals[2], sizeof svals[2], "%d / %u",  p->hits, (unsigned)p->N);
        snprintf(svals[3], sizeof svals[3], "%d",       p->misses);
        snprintf(svals[4], sizeof svals[4], "%d",       p->maxCombo);
        Color svalCols[5] = {
            { 195, 225, 255, 255 },
            { 135, 255, 185, 255 },
            { 195, 225, 255, 255 },
            { 255, 115, 115, 255 },
            { 255, 215,  95, 255 }
        };

        int lFS = (int)(sh * 0.0135f); if (lFS < 9)  lFS = 9;
        int vFS = (int)(sh * 0.026f);  if (vFS < 14) vFS = 14;

        for (int si = 0; si < 5; si++) {
            int bx = statsX + si * (statW + statGap);
            DrawRectangle(bx, statsY, statW, statH, (Color){ 18, 22, 44, 215 });
            DrawRectangleLinesEx(
                (Rectangle){ (float)bx, (float)statsY, (float)statW, (float)statH },
                1.0f, (Color){ 45, 56, 98, 190 });
            int lw = MeasureText(slabels[si], lFS);
            int vw = MeasureText(svals[si],   vFS);
            DrawText(slabels[si], bx + statW/2 - lw/2, statsY + (int)(statH * 0.12f),
                     lFS, (Color){ 108, 128, 185, 205 });
            DrawText(svals[si],   bx + statW/2 - vw/2, statsY + (int)(statH * 0.46f),
                     vFS, svalCols[si]);
        }

        /* ============================================================
         *  Analyse des miss : carte spatiale (OU) + timeline (QUAND)
         * ============================================================ */
        int chartMgn = (int)(sw * 0.080f);
        int chartX   = chartMgn;
        int chartW2  = sw - 2 * chartMgn;
        int chartY2  = statsY + statH + (int)(sh * 0.046f);
        int chartH2  = (int)(sh * 0.170f); if (chartH2 < 96) chartH2 = 96;

        float durMs  = (float)(p->map.lastMs > 0 ? p->map.lastMs : 1);
        float durSec = durMs / 1000.0f;

        /* Decoupe horizontale : panneau carre (heatmap) a gauche, timeline a droite */
        int mapSize = chartH2;                       /* carre */
        int gapPan  = (int)(sw * 0.022f); if (gapPan < 12) gapPan = 12;
        int mapX = chartX, mapY = chartY2;
        int tlX  = mapX + mapSize + gapPan;
        int tlY  = chartY2;
        int tlW  = chartX + chartW2 - tlX;
        int tlH  = chartH2;

        /* ---- Panneau 1 : MISS MAP (ou les miss tombent sur la grille) ---- */
        DrawText("MISS MAP", mapX, mapY - 19, 13, (Color){ 88, 110, 175, 195 });
        DrawRectangle(mapX, mapY, mapSize, mapSize, (Color){ 8, 9, 20, 235 });
        {
            const float HALF = 1.6f;                 /* fenetre monde affichee (grille = +-1.5) */
            #define W2SX(wx) (mapX + (int)(((wx) + HALF) / (2.0f*HALF) * (float)mapSize))
            #define W2SY(wy) (mapY + (int)((HALF - (wy)) / (2.0f*HALF) * (float)mapSize))
            #define HN 40
            static float hb[HN*HN], tmp[HN*HN], sm2[HN*HN];
            static const float K[7] = { 0.06f, 0.12f, 0.20f, 0.24f, 0.20f, 0.12f, 0.06f };
            int mi, x, y, k;

            /* binning des miss en coords monde -> grille HNxHN */
            memset(hb, 0, sizeof hb);
            for (mi = 0; mi < p->missTrackCount; mi++) {
                int gx = (int)((p->missTrack[mi].x + HALF) / (2.0f*HALF) * HN);
                int gy = (int)((HALF - p->missTrack[mi].y) / (2.0f*HALF) * HN);
                if (gx < 0) gx = 0;
                if (gx >= HN) gx = HN-1;
                if (gy < 0) gy = 0;
                if (gy >= HN) gy = HN-1;
                hb[gy*HN + gx] += 1.0f;
            }
            /* lissage gaussien separable (rayon 3) */
            for (y = 0; y < HN; y++)
                for (x = 0; x < HN; x++) {
                    float s = 0.0f;
                    for (k = -3; k <= 3; k++) {
                        int xx = x + k; if (xx < 0) xx = 0; if (xx >= HN) xx = HN-1;
                        s += hb[y*HN + xx] * K[k+3];
                    }
                    tmp[y*HN + x] = s;
                }
            for (y = 0; y < HN; y++)
                for (x = 0; x < HN; x++) {
                    float s = 0.0f;
                    for (k = -3; k <= 3; k++) {
                        int yy = y + k; if (yy < 0) yy = 0; if (yy >= HN) yy = HN-1;
                        s += tmp[yy*HN + x] * K[k+3];
                    }
                    sm2[y*HN + x] = s;
                }
            float mxv = 0.0001f;
            for (k = 0; k < HN*HN; k++) if (sm2[k] > mxv) mxv = sm2[k];

            /* cellules thermiques */
            float cw = (float)mapSize / (float)HN;
            for (y = 0; y < HN; y++)
                for (x = 0; x < HN; x++) {
                    float t = sm2[y*HN + x] / mxv;
                    if (t < 0.02f) continue;
                    DrawRectangle(mapX + (int)((float)x*cw), mapY + (int)((float)y*cw),
                                  (int)cw + 1, (int)cw + 1, heat_color(t));
                }

            /* grille 3x3 (lignes a +-0.5 et +-1.5) */
            { float ln[4] = { -1.5f, -0.5f, 0.5f, 1.5f };
              Color glc = (Color){ 70, 86, 130, 110 };
              for (k = 0; k < 4; k++) {
                  int sx = W2SX(ln[k]), sy = W2SY(ln[k]);
                  DrawLine(sx, mapY, sx, mapY + mapSize, glc);
                  DrawLine(mapX, sy, mapX + mapSize, sy, glc);
              } }
            /* bordure de l'aire jouable (+-1.5) accentuee */
            { int bx0 = W2SX(-1.5f), by0 = W2SY(1.5f), bx1 = W2SX(1.5f), by1 = W2SY(-1.5f);
              DrawRectangleLinesEx((Rectangle){ (float)bx0, (float)by0,
                  (float)(bx1-bx0), (float)(by1-by0) }, 1.0f, (Color){ 110, 130, 185, 150 }); }
            /* points exacts par-dessus (precision) */
            for (mi = 0; mi < p->missTrackCount; mi++)
                DrawCircle(W2SX(p->missTrack[mi].x), W2SY(p->missTrack[mi].y),
                           1.6f, (Color){ 255, 235, 210, 150 });

            DrawRectangleLinesEx((Rectangle){ (float)mapX, (float)mapY,
                (float)mapSize, (float)mapSize }, 1.0f, (Color){ 38, 50, 88, 205 });
            if (p->missTrackCount == 0) {
                const char *nm = "Perfect";
                DrawText(nm, mapX + mapSize/2 - MeasureText(nm, 13)/2,
                         mapY + mapSize/2 - 7, 13, (Color){ 95, 235, 130, 200 });
            }
            #undef W2SX
            #undef W2SY
            #undef HN
        }

        /* ---- Panneau 2 : MISS TIMELINE (quand les miss arrivent) ---- */
        DrawText("MISS TIMELINE", tlX, tlY - 19, 13, (Color){ 88, 110, 175, 195 });
        DrawRectangle(tlX, tlY, tlW, tlH, (Color){ 10, 12, 24, 225 });

        /* graduations temporelles */
        { float tickIv = durSec <= 60.0f ? 10.0f : durSec <= 180.0f ? 30.0f : 60.0f;
          for (float ts = tickIv; ts < durSec - tickIv * 0.1f; ts += tickIv) {
              int tx = tlX + (int)(ts / durSec * (float)tlW);
              DrawLine(tx, tlY, tx, tlY + tlH, (Color){ 38, 50, 82, 120 });
              char tbuf[16]; snprintf(tbuf, sizeof tbuf, "%ds", (int)ts);
              DrawText(tbuf, tx - MeasureText(tbuf, 10)/2, tlY + tlH + 3, 10,
                       (Color){ 68, 88, 140, 170 });
          } }

        /* densite de miss : aire remplie + courbe lissee */
        {
            #define TN 200
            static float bins[TN], sm[TN];
            int mi, b;
            memset(bins, 0, sizeof bins);
            for (mi = 0; mi < p->missTrackCount; mi++) {
                int bb = (int)(p->missTrack[mi].ms / durMs * TN);
                if (bb < 0) bb = 0;
                if (bb >= TN) bb = TN-1;
                bins[bb] += 1.0f;
            }
            for (b = 0; b < TN; b++) {
                float s = 0.0f; int cnt = 0, d;
                for (d = -6; d <= 6; d++) {
                    int nb = b + d;
                    if (nb >= 0 && nb < TN) { s += bins[nb]; cnt++; }
                }
                sm[b] = cnt > 0 ? s / (float)cnt : 0.0f;
            }
            float mxv = 0.0001f; int peak = 0;
            for (b = 0; b < TN; b++) if (sm[b] > mxv) { mxv = sm[b]; peak = b; }

            float bwf = (float)tlW / (float)TN;
            int base = tlY + tlH - 1;
            for (b = 0; b < TN; b++) {
                float t = sm[b] / mxv;
                int bh = (int)(t * (float)(tlH - 3));
                if (bh <= 0) continue;
                DrawRectangle(tlX + (int)((float)b * bwf), base - bh, (int)bwf + 1, bh,
                              (Color){ 200, 46, 64, (unsigned char)(40 + (int)(t * 120.0f)) });
            }
            for (b = 0; b < TN-1; b++) {
                float t0 = sm[b]/mxv, t1 = sm[b+1]/mxv;
                float x0 = (float)tlX + ((float)b   + 0.5f) * bwf;
                float x1 = (float)tlX + ((float)b+1 + 0.5f) * bwf;
                float y0 = (float)base - t0 * (float)(tlH - 3);
                float y1 = (float)base - t1 * (float)(tlH - 3);
                DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 2.0f,
                           (Color){ 255, 92, 112, 235 });
            }
            /* marqueur du pic (pire moment) */
            if (p->missTrackCount > 0) {
                int px = tlX + (int)(((float)peak + 0.5f) * bwf);
                int py = base - (tlH - 3);
                DrawCircleLines(px, py, 4.0f, (Color){ 255, 210, 120, 235 });
                DrawCircle(px, py, 2.0f, (Color){ 255, 210, 120, 235 });
            }
            if (p->missTrackCount == 0) {
                const char *nm = "No misses !";
                DrawText(nm, tlX + tlW/2 - MeasureText(nm, 14)/2,
                         tlY + tlH/2 - 7, 14, (Color){ 95, 235, 130, 215 });
            }
            #undef TN
        }

        /* marqueur game over (position de la mort dans la timeline) */
        if (p->gameOver && p->map.lastMs > 0) {
            float pct = clampf(p->nowMs / durMs, 0.0f, 1.0f);
            int mx = tlX + (int)(pct * (float)tlW);
            DrawLine(mx, tlY, mx, tlY + tlH, (Color){ 255, 80, 80, 220 });
            DrawCircle(mx, tlY, 5, (Color){ 255, 80, 80, 230 });
        }
        DrawRectangleLinesEx((Rectangle){ (float)tlX, (float)tlY,
            (float)tlW, (float)tlH }, 1.0f, (Color){ 38, 50, 88, 205 });

        /* Hint */
        int hintY = chartY2 + chartH2 + (int)(sh * 0.026f);
        DrawText("R replay    -    ESC menu", sw/2 - wHint/2, hintY, 20,
                 (Color){ 168, 168, 188, 255 });
    }
}

/* Genere une session d'Aim Trainer depuis la config personnalisee du joueur
 * (notes procedurales, aucun .sspm). */
void aim_build(Play *p, const AimConfig *cfg) {
    play_unload(p);
    memset(&p->map, 0, sizeof p->map);

    int   style     = (cfg->style >= 0 && cfg->style < N_AIM_STYLES) ? cfg->style : AS_MIX;
    float density   = clampf(cfg->density, 0.5f, 14.0f);
    float baseIv    = 1000.0f / density;                 /* ms entre deux notes (vitesse de base) */
    int   accelP    = cfg->accelPct < 0 ? 0 : (cfg->accelPct > 100 ? 100 : cfg->accelPct);
    float accelFrac = (float)accelP / 100.0f * 0.7f;     /* a fond -> interval final = 30% du depart */
    int   durSec    = cfg->durationSec < 15 ? 15 : (cfg->durationSec > 600 ? 600 : cfg->durationSec);
    /* nb de notes estime depuis l'interval MOYEN sur la duree (l'accel le reduit) */
    float avgIv     = baseIv * (1.0f - accelFrac * 0.5f);
    int   n         = (int)((float)durSec * 1000.0f / avgIv);
    n = (n < 16) ? 16 : (n > 20000 ? 20000 : n);
    SspmNote *notes = (SspmNote *)malloc(sizeof(SspmNote) * (size_t)n);
    if (!notes) return;

    const float leadIn = 1200.0f;
    const float R = clampf(cfg->radius, 0.30f, 1.0f) * 0.92f;  /* rayon utile (0.92 -> coins) */
    static const float CX[4] = { -1, 1, 1, -1 }, CY[4] = { -1, -1, 1, 1 };  /* coins */
    int   baseSeg = cfg->segLen < 2 ? 2 : (cfg->segLen > 24 ? 24 : cfg->segLen);
    float px = 1.0f, py = 1.0f;            /* position precedente (grille, centre 1) */
    float ang = (float)GetRandomValue(0, 628) / 100.0f;   /* angle courant (cercle/etoile/spirale) */
    int   lastCorner = 0;
    int   i = 0;
    float tms = leadIn;                    /* horloge cumulative (gere l'acceleration) */

    /* Generation par segments coherents : chaque iteration choisit un pattern
     * (P_CIRCLE, P_STAR, etc.) et joue baseSeg notes consecutives selon sa
     * geometrie, puis rebascule sur un nouveau pattern. On obtient ainsi un
     * parcours lisible (pas de points totalement aleatoires) tout en variant
     * les formes.  L'acceleration est appliquee a chaque note : l'intervalle
     * temporel entre notes diminue lineairement de baseIv vers baseIv*(1-accelFrac)
     * a mesure que 'prog' (0..1) avance sur la session. */
    while (i < n) {
        int pat = aim_pick_pattern(style);
        int segLen = baseSeg;
        float dir  = GetRandomValue(0, 1) ? 1.0f : -1.0f;

        /* mise en place propre au segment */
        float yc = 1.0f + (float)GetRandomValue(-60, 60) / 100.0f * R;   /* PINGH : ligne y */
        float xc = 1.0f + (float)GetRandomValue(-60, 60) / 100.0f * R;   /* PINGV : colonne x */
        float drift = (float)GetRandomValue(-25, 25) / 100.0f * R / (float)segLen;
        int   sc = GetRandomValue(0, 3);                                  /* DIAG : coin de depart */
        float dx0 = 1.0f + CX[sc] * R, dy0 = 1.0f + CY[sc] * R;
        float dx1 = 1.0f - CX[sc] * R, dy1 = 1.0f - CY[sc] * R;

        for (int k = 0; k < segLen && i < n; k++, i++) {
            float t  = (segLen > 1) ? (float)k / (float)(segLen - 1) : 0.0f;
            float nx, ny;
            switch (pat) {
                case P_PINGH:  nx = 1.0f + ((k & 1) ? R : -R); ny = yc + drift * (float)k; break;
                case P_PINGV:  ny = 1.0f + ((k & 1) ? R : -R); nx = xc + drift * (float)k; break;
                case P_CIRCLE: ang += dir * 0.55f; nx = 1.0f + R * cosf(ang); ny = 1.0f + R * sinf(ang); break;
                case P_STAR:   ang += dir * 2.39996f; nx = 1.0f + R * cosf(ang); ny = 1.0f + R * sinf(ang); break;
                case P_SPIRAL: { ang += dir * 0.70f; float rr = R * (0.35f + 0.6f * (0.5f + 0.5f * sinf((float)k * 0.6f)));
                                 nx = 1.0f + rr * cosf(ang); ny = 1.0f + rr * sinf(ang); } break;
                case P_ZIG:    ny = (dir > 0) ? (1.0f - R) + 2.0f * R * t : (1.0f + R) - 2.0f * R * t;
                               nx = 1.0f + ((k & 1) ? R : -R); break;
                case P_SQUARE: { int c = k % 4; nx = 1.0f + CX[c] * R; ny = 1.0f + CY[c] * R; } break;
                case P_DIAG:   nx = dx0 + (dx1 - dx0) * t; ny = dy0 + (dy1 - dy0) * t; break;
                case P_BURST:  { int c = GetRandomValue(0, 3); if (c == lastCorner) c = (c + 1 + GetRandomValue(0, 2)) % 4;
                                 nx = 1.0f + CX[c] * R; ny = 1.0f + CY[c] * R; lastCorner = c; } break;
                default:       { /* RANDFAR : point aleatoire le plus loin possible du precedent */
                                 float bnx = 1.0f, bny = 1.0f, bd = -1.0f;
                                 for (int q = 0; q < 6; q++) {
                                     float a2 = (float)GetRandomValue(0, 628) / 100.0f;
                                     float r2 = R * (0.55f + 0.45f * (float)GetRandomValue(0, 100) / 100.0f);
                                     float X = 1.0f + r2 * cosf(a2), Y = 1.0f + r2 * sinf(a2);
                                     float d = (X - px) * (X - px) + (Y - py) * (Y - py);
                                     if (d > bd) { bd = d; bnx = X; bny = Y; }
                                 }
                                 nx = bnx; ny = bny; } break;
            }
            nx = clampf(nx, 0.08f, 1.92f);
            ny = clampf(ny, 0.08f, 1.92f);
            px = nx; py = ny;
            float prog = (n > 1) ? (float)i / (float)(n - 1) : 0.0f;   /* 0 -> 1 sur la session */
            notes[i].ms = tms;
            notes[i].x  = nx;
            notes[i].y  = ny;
            tms += baseIv * (1.0f - accelFrac * prog);                 /* l'interval retrecit avec l'accel */
        }
    }

    p->map.notes           = notes;
    p->map.noteCount       = (uint32_t)n;
    p->map.noteCountLoaded = (uint32_t)n;
    p->map.lastMs          = (uint32_t)tms;
    p->map.difficulty      = 0;
    p->map.hasAudio        = false;
    snprintf(p->map.songName, sizeof p->map.songName, "Custom Aim Trainer  -  %s  %.1f n/s",
             AIM_STYLE_NAMES[style], density);
    snprintf(p->map.mapName,  sizeof p->map.mapName,  "Aim Trainer");
    snprintf(p->map.mapper,   sizeof p->map.mapper,   "%d notes", n);

    p->state     = (uint8_t *)malloc((size_t)(n ? n : 1));
    p->N         = (uint32_t)n;
    p->loaded    = true;
    p->haveMusic = false;

    /* parametres de jeu propres a l'aim (tous pilotes par la config du joueur) */
    gApproachMs  = clampf(cfg->approachMs, 150.0f, 1000.0f);
    gNoteScale   = clampf(cfg->size, 0.50f, 1.50f);
    gHitWindowMs = clampf(cfg->hitWindowMs, 40.0f, 60.0f);
    gCullBehind  = gApproachDist * gHitWindowMs / gApproachMs + 2.0f;
}