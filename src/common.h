/*
 * common.h — Types, constantes et declarations partagés entre tous les modules
 *            de sspm-player. Inclure en premiere ligne de chaque .c.
 *
 * Ce fichier ne contient :
 *   - que des #include, #define, typedef, enum et struct
 *   - AUCUNE variable (les definitions vivent dans globals.c)
 *   - AUCUNE fonction (les definitions vivent dans leur .c respectif)
 */
#pragma once

#include "raylib.h"
#include "sspm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

/* Threading pour le chargement asynchrone */
#ifdef _WIN32
  #include <process.h>   /* _beginthreadex — pas de conflit avec raylib */
  /* Declarations Win32 minimales sans inclure <windows.h> (conflits Rectangle/CloseWindow/DrawText) */
  typedef void *ATHREAD_HANDLE;
  #ifndef INFINITE
  #define INFINITE 0xFFFFFFFFUL
  #endif
  extern __declspec(dllimport) unsigned long __stdcall WaitForSingleObject(ATHREAD_HANDLE, unsigned long);
  extern __declspec(dllimport) int           __stdcall CloseHandle(ATHREAD_HANDLE);
#else
  #include <pthread.h>
#endif

/* =========================================================================
 * Constantes
 * ========================================================================= */

/* --- approche / rendu --- */
#define APPROACH_MS    700.0f
#define APPROACH_DIST  50.0f
#define CAM_BACK       3.2f
#define CULL_BEHIND    7.0f
#define CUBE_SIZE      0.82f
#define GRID_SPACING   1.0f
#define FADE_FRAC      0.15f

/* --- gameplay --- */
#define HIT_EARLY      80.0f
#define HIT_LATE       80.0f
#define NOTE_HALF      (CUBE_SIZE * 0.5f)
#define CURSOR_HALF    0.05f
#define HIT_TOL        (NOTE_HALF + CURSOR_HALF)
#define SENS           0.0042f
#define CURSOR_RANGE   1.5f
#define HP_MISS        0.15f
#define HP_HIT         0.03f

/* --- menu / options --- */
#define MAX_MAPS       8192
#define COUNTDOWN_SEC  3.0
#define MAX_PALETTE    64
#define MAX_MESHES     64
#define SETTINGS_FILE  "settings.cfg"
#define PROFILE_FILE   "profile.cfg"
#define MAX_FAV_MAPS       512
#define MAX_BLACKLIST_MAPS 512
#define MAX_BEST_MAPS      512

/* --- menu osu!-style --- */
#define CARD_H_NORM   62
#define CARD_H_SEL    68
#define CARD_GAP       3
#define CARD_STEP     (CARD_H_NORM + CARD_GAP)
#define CARD_INDENT   14
#define SCROLL_BTN_W  40   /* largeur de la colonne de boutons scroll (haut/bas) */

/* --- curseurs / hitsounds / trainee / chaines de couleurs --- */
#define MAX_CURSORS      64
#define MAX_HITSOUNDS    64
#define MAX_COLOR_CHAINS 64
#define TRAIL_MAX        512

/* --- gameplay : etats de note --- */
enum { NOTE_PENDING = 0, NOTE_HIT = 1, NOTE_MISS = 2 };

/* --- mods --- */
enum {
    MOD_HARDROCK = 1 << 0,
    MOD_HIDDEN   = 1 << 1,
    MOD_NOFAIL   = 1 << 2,
    MOD_SUDDEN   = 1 << 3,
    MOD_MIRROR_X  = 1 << 4,
    MOD_MIRROR_Y  = 1 << 5,
    MOD_FLASHLIGHT= 1 << 6,
};

/* --- styles / patterns aim trainer --- */
enum { AS_FLOW = 0, AS_MIX = 1, AS_JUMP = 2, AS_TECH = 3, AS_CHAOS = 4 };
#define N_AIM_STYLES 5

enum { P_PINGH = 0, P_PINGV, P_CIRCLE, P_STAR, P_ZIG, P_SQUARE, P_DIAG, P_BURST, P_SPIRAL, P_RANDFAR };

/* --- particules --- */
#define MAX_PARTICLES 256

/* --- historique de miss pour le graphique fin de partie --- */
#define MAX_MISS_TRACK 2048

/* --- calibration --- */
#define CALIB_MAX     64
#define CALIB_LEADIN  0.9
#define CALIB_PERIOD  0.5

/* --- options UI --- */
#define N_TABS        7
#define MAX_OPT_ROWS  28

/* =========================================================================
 * Utilitaires inline (utilises dans plusieurs modules)
 * ========================================================================= */

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline void rgb_to_hsv(Color c, float *h, float *s, float *v) {
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d  = mx - mn;
    *v = mx;
    *s = mx < 0.0001f ? 0.0f : d / mx;
    if (d < 0.0001f) { *h = 0.0f; return; }
    if      (mx == r) { *h = 60.0f * fmodf((g - b) / d, 6.0f); if (*h < 0.0f) *h += 360.0f; }
    else if (mx == g)   *h = 60.0f * ((b - r) / d + 2.0f);
    else                *h = 60.0f * ((r - g) / d + 4.0f);
}
static inline Color hsv_to_rgb(float h, float s, float v, unsigned char a) {
    float c2 = v * s, x = c2 * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c2;
    float r = 0, g = 0, b = 0;
    int hi = (int)(h / 60.0f) % 6;
    switch (hi) {
        case 0: r=c2;g=x; b=0;  break; case 1: r=x; g=c2;b=0;  break;
        case 2: r=0; g=c2;b=x;  break; case 3: r=0; g=x; b=c2; break;
        case 4: r=x; g=0; b=c2; break; default: r=c2;g=0; b=x;  break;
    }
    return (Color){ (unsigned char)((r+m)*255.0f), (unsigned char)((g+m)*255.0f),
                    (unsigned char)((b+m)*255.0f), a };
}

/* =========================================================================
 * Enumerations
 * ========================================================================= */

typedef enum {
    SCR_MENU = 0,
    SCR_SETTINGS,
    SCR_MODES,
    SCR_PROFILE,
    SCR_CALIBRATE,
    SCR_COUNTDOWN,
    SCR_PLAYING,
    SCR_LOADING,
    SCR_PRACTICE_SETUP
} AppScreen;

typedef enum {
    MODE_NORMAL = 0,
    MODE_ZEN,
    MODE_LADDER,
    MODE_AIM,
    MODE_PRACTICE
} GameMode;

typedef enum {
    BG_NONE = 0,
    BG_VOID,
    BG_CIRCUIT,
    BG_PULSE,
    BG_AURORA,
    BG_TOPO,
    BG_VORONOI,
    BG_WAVEFORM,
    BG_FLOW,
    BG_HALFTONE,
    BG_SPECTRUM,    /* FFT audio reactif — segments radiaux                */
    BG_FLOWLIVE,    /* champ de flux anime — 160 particules advectees      */
    BG_VEIL,        /* rideaux d'aurore boreale oscillants                 */
    BG_RADAR,       /* balayage radar rotatif + grille concentrique        */
    BG_GLITCH,      /* bandes RGB-split sur les downbeats                  */
    BG_COUNT        /* toujours en dernier : nombre de styles valides */
} BgStyle;

typedef enum {
    ALOAD_IDLE = 0,
    ALOAD_RUNNING,
    ALOAD_DONE,
    ALOAD_FAIL
} ALoadState;

/* =========================================================================
 * Structures
 * ========================================================================= */

typedef struct {
    char filename[256];
    int  score, hits, total, maxCombo;
} BestScore;

typedef struct {
    char      favFiles[MAX_FAV_MAPS][256];
    int       favCount;
    char      blacklistFiles[MAX_BLACKLIST_MAPS][256];
    int       blacklistCount;
    BestScore best[MAX_BEST_MAPS];
    int       bestCount;
    /* statistiques globales */
    int       totalRuns;           /* parties lancees (decompte -> jeu) */
    float     totalPlayTimeSec;    /* temps cumule en jeu (secondes) */
    int       allTimeHits;         /* notes frappees cumulees */
    int       allTimeTotalNotes;   /* notes presentees cumulees */
} Profile;

typedef struct { bool active; Model model; float scale; Vector3 center; } NoteMesh;
typedef struct { bool active; Texture2D tex; }  CursorTex;
typedef struct { bool active; Sound snd; }      HitsoundSnd;
typedef struct {
    char  name[128];
    char  filename[128];
    Color cols[MAX_PALETTE];
    int   len;
} ColorChain;
typedef struct { float x, y, t; }               TrailPt;

typedef struct { const char *name; int len; Color cols[MAX_PALETTE]; } Palette;

/* --- Configuration complete du curseur + trainee (pilotee par les options).
 * Le rendu lit cette struct chaque frame. Le curseur "genere" = un coeur
 * lumineux + un halo radial tres fondu (une seule texture reutilisee). La
 * trainee = des copies du curseur le long du trajet, decroissantes. --- */
typedef struct {
    /* --- Curseur --- */
    Color  color;               /* couleur principale (RGB) */
    float  size;                /* rayon du coeur (px framebuffer) */
    float  coreIntensity;       /* 0..1 luminosite du coeur */
    float  glowSize;            /* taille halo = size * glowSize (ex 3.0) */
    float  glowIntensity;       /* 0..1 intensite/alpha du halo */
    bool   additive;            /* blend additif on/off */

    /* --- Trainee --- */
    bool   trailEnabled;
    float  trailDuration;       /* duree de vie d'un segment (s), ex 0.25 */
    int    trailMaxSegments;    /* nb max de copies (cap dur), ex 32 */
    float  trailSpacing;        /* espacement min entre points (px) : emission PAR DISTANCE,
                                 * jamais par temps -> pas d'empilement (paté) */
    float  trailStartScale;     /* echelle proche du curseur (1.0) */
    float  trailEndScale;       /* echelle la plus ancienne (ex 0.1) */
    float  trailStartAlpha;     /* 0..1 */
    float  trailEndAlpha;       /* 0..1 (souvent 0) */
    bool   trailUseCursorColor; /* sinon trailColor */
    Color  trailColor;
} CursorConfig;

typedef struct {
    float density;
    float approachMs;
    int   style;
    int   segLen;
    float radius;
    float size;
    float hitWindowMs;
    int   durationSec;
    int   accelPct;
} AimConfig;

typedef struct {
    /* --- En jeu --- */
    int quit;           /* quitter la partie   (KEY_ESCAPE) */
    int pause;          /* pause / reprise     (KEY_SPACE)  */
    int restart;        /* recommencer la map  (KEY_R)      */
    int skipIntro;      /* sauter l'intro      (KEY_TAB)    */
    /* --- Menu principal --- */
    int menuSettings;   /* ouvrir Options      (KEY_S)      */
    int menuModes;      /* ouvrir Modes        (KEY_G)      */
    int menuProfile;    /* ouvrir Profil       (KEY_P)      */
    int menuPlay;       /* jouer la map        (KEY_ENTER)  */
    int menuFavorite;   /* toggle favori       (KEY_F)      */
    int menuBan;        /* toggle banni        (KEY_X)      */
    int menuHideBanned; /* masquer bannis       (KEY_H)     */
    int menuFavsOnly;   /* favoris uniquement  (KEY_V)      */
    int menuNewOnly;    /* jamais joue         (KEY_N)      */
    int menuDiffFilter; /* filtre difficulte   (KEY_D)      */
    int menuSortStar;   /* tri par star rate   (KEY_R)      */
    int menuCycleSort;  /* cycle de tri        (KEY_TAB)    */
    int menuRescan;     /* rescanner les maps  (KEY_F5)     */
} KeyBindings;

typedef struct {
    float approachDist;
    float approachMs;
    bool  tablet;
    int   paletteIdx;
    char  meshName[128];
    char  cursorName[128];   /* image PNG (mode Image) */
    int   cursorMode;        /* 0 = genere (rond + halo), 1 = image */
    CursorConfig cursor;     /* apparence du curseur genere + trainee */
    float musicVolume;
    bool  godMode;
    float hitWindowMs;
    int   hueShift;
    float sensMultiplier;
    bool  vsync;
    int   gridAlpha;
    int   gridStyle;   /* 0=Classique 1=Minimal 2=Coins 3=Diagonales */
    float noteScale;
    char  hitsoundName[128];
    float hitsoundVolume;
    int   internalRes;
    float audioOffsetMs;
    int   juiceMode;
    int   juiceCount;
    float juiceLife;
    float juiceSize;
    float juiceSpeed;
    float juicePulse;
    int   juiceCombo;
    int   mods;
    float rate;
    Color customPal[MAX_PALETTE];
    int   customLen;
    char  colorChainFile[128];  /* fichier .txt dans colors/ ; vide = aucun */
    char  mapsDir[512];         /* dossier de maps ; vide = "maps/" par défaut */
    BgStyle bgStyle;            /* BG_NONE / BG_VOID / BG_CIRCUIT / BG_PULSE / BG_AURORA */
    float   bgIntensity;        /* 0.0 (invisible) .. 1.0 (plein), defaut 0.5 */
    bool    cursorInMenu;       /* affiche le curseur jeu dans les menus (masque le curseur systeme) */
    /* --- HUD configurable --- */
    bool    hudShowScore;
    bool    hudShowCombo;
    bool    hudShowAccuracy;
    bool    hudShowHp;
    bool    hudShowSongInfo;
    AimConfig   aim;
    KeyBindings keys;
} Settings;

typedef struct { unsigned bit; const char *name; const char *desc; } ModDef;

typedef struct { Vector3 pos, vel; float life, life0; Color col; } Particle;

typedef struct {
    SspmMap map;
    bool    loaded;
    uint8_t *state;
    uint32_t N;
    Music   music;
    bool    haveMusic;
    float   nowMs;
    double  clockMs;
    bool    paused;
    float   unpauseCountdown;
    bool    finished;
    bool    gameOver;
    float   endDelay;       /* >0 = attente avant affichage écran de fin (win) */
    size_t  head;
    float   cx, cy;
    int     score, hits, misses, combo, maxCombo;
    float   missFlash;
    float   missTrack[MAX_MISS_TRACK];
    int     missTrackCount;
    float   hp;
    bool    scoreSaved;
    TrailPt trail[TRAIL_MAX];
    int     trailHead;
    int     trailCount;
    float   trailLastX, trailLastY;   /* derniere position emise (monde) : emission par distance */
    Particle parts[MAX_PARTICLES];
    int      partHead;
    float    pulse;
    float    hitFlash;
    float    comboFlashT;
    int      comboFlashN;
} Play;

typedef struct {
    char     path[1024];
    SspmInfo info;
    char     collection[256]; /* chemin relatif complet depuis maps/, ex: "a/b/c", "" = racine */
    float    starRate;        /* extrait du nom de fichier (X.XX - ...) ; -1 = absent */
} MenuEntry;

typedef struct {
    char path[256];   /* chemin relatif complet, ex: "a/b/c" ; "" = racine */
    char name[128];   /* dernier composant, ex: "c" ; "Pas triees" pour racine */
    int  depth;       /* 0 = niveau racine */
    int  directCount; /* maps directement dans ce dossier */
    int  totalCount;  /* maps dans ce dossier + tous les sous-dossiers */
    int  parentIdx;   /* indice dans folderNodes, -1 si depth==0 */
} FolderNode;

typedef struct {
    char       dir[1024];
    MenuEntry *items;
    int        count;
    int       *filtered;
    int        filteredCount;
    int        sel;
    int        scroll;
    char       filter[128];
    int        filterCursor;
    bool       filterFocused;
    int        sortMode;
    int        diffFilter;
    bool       favsOnly;
    bool       newOnly;
    bool       hideBlacklisted;
    /* animation defilement */
    float      scrollAnim;      /* position courante en pixels (lerpe vers la cible) */
    int        scrollHoldDir;   /* -1 = bouton haut tenu, +1 = bas, 0 = aucun */
    float      scrollHoldTimer; /* temps restant avant prochain pas de repetition */
    /* dossiers */
    int        viewTab;              /* 0 = Toutes, 1 = Dossiers */
    FolderNode folderNodes[512];
    int        folderCount;
    int        collSel;              /* -1 = arbre de dossiers ; >=0 = dossier selectionne */
    /* progression du scan asynchrone (ecrits par le thread, lus par le main) */
    volatile int scanCount;
    volatile int scanTotal;
} Menu;

typedef struct {
    char         path[1024];
    Play        *play;
    volatile int state;
    bool         threadStarted;
#ifdef _WIN32
    ATHREAD_HANDLE thread;
#else
    pthread_t      thread;
#endif
} AsyncLoader;

typedef struct {
    Menu        *menu;
    volatile int state;
    bool         threadStarted;
#ifdef _WIN32
    ATHREAD_HANDLE thread;
#else
    pthread_t      thread;
#endif
} MenuScanner;

typedef struct {
    Sound  click;
    bool   haveClick;
    double startT;
    int    beatsPlayed;
    float  samples[CALIB_MAX];
    int    nSamples;
    float  suggested;
    float  lastDelta;
    double lastTapT;
} Calib;

/* =========================================================================
 * Variables globales (definies dans globals.c)
 * ========================================================================= */

/* profil joueur */
extern Profile gProfile;
extern bool    gNewRecord;
extern char    gCurrentMap[1024];

/* rendu / gameplay runtime */
extern float gApproachDist;
extern float gApproachMs;
extern bool  gTablet;
extern Color gPalette[MAX_PALETTE];
extern int   gPaletteLen;

/* mesh de note personnalise */
extern NoteMesh gNoteMesh;

/* texture blanche 1x1 + render texture basse res */
extern Texture2D       gWhiteTex;
extern RenderTexture2D gRenderTex;
extern int gRtW, gRtH;

/* couverture de la map selectionnee (chargee a la demande dans main.c) */
extern Texture2D gCoverTex;
extern bool      gHaveCover;

/* curseur : config generee (rond + halo) + trainee, et image optionnelle */
extern CursorConfig gCursor;        /* apparence + trainee, lues chaque frame */
extern int   gCursorMode;           /* 0 = genere, 1 = image */
extern Texture2D gHaloTex;          /* texture radiale partagee (coeur/halo/trainee) */
extern bool  gHaveHalo;
extern CursorTex gCursorTex;        /* image PNG (mode Image) */
extern char  gCursorNames[MAX_CURSORS][128];
extern int   gCursorCount;
extern char  gCursorDir[1024];
extern char  gCursorMsg[160];
extern float gMusicVolume;

/* hitsounds */
extern HitsoundSnd gHitsoundSnd;
extern char  gHitsoundNames[MAX_HITSOUNDS][128];
extern int   gHitsoundCount;
extern char  gHitsoundDir[1024];
extern char  gHitsoundMsg[160];
extern float gHitsoundVolume;

/* chaines de couleurs personnalisees (colors/) */
extern ColorChain gColorChains[MAX_COLOR_CHAINS];
extern int        gColorChainCount;
extern char       gColorChainDir[1024];

/* parametres gameplay actifs */
extern bool  gGodMode;
extern float gHitWindowMs;
extern int   gHueShift;
extern float gCullBehind;
extern float gSens;
extern float gAudioOffsetMs;
extern int   gJuiceMode;
extern int   gJuiceCount;
extern float gJuiceLife;
extern float gJuiceSize;
extern float gJuiceSpeed;
extern float gJuicePulse;
extern int   gJuiceCombo;

/* grille / notes */
extern int   gGridAlpha;
extern int   gGridStyle;
extern float gNoteScale;

/* mods */
extern unsigned gMods;
extern float    gRate;
extern float    gScoreMult;

/* mode de jeu */
extern GameMode gMode;
extern float    gLadderRate;
extern int      gLadderLevel;
extern int      gMenuMode;

/* mode Entrainement (Practice) : point de depart + boucle A/B (ms) + vitesse libre.
 * gPracticeA  = ancre de depart (R / lancement) et debut de boucle.
 * gPracticeB  = fin de boucle ; ignore si <= gPracticeA (joue jusqu'a la fin).
 * gPracticeLoop = true -> boucle A..B ; false -> joue A..fin puis revient a A. */
extern float gPracticeA;
extern float gPracticeB;
extern bool  gPracticeLoop;

/* meshes personnalises */
extern char gMeshNames[MAX_MESHES][128];
extern int  gMeshCount;
extern char gMeshDir[1024];
extern char gMeshMsg[160];

/* reglages + UI options */
extern Settings  gSettings;
extern int       gOptSel;
extern int       gOptTab;
extern bool      gSensEditing;
extern char      gSensEditBuf[32];
extern Rectangle gOptCtrl;
extern bool      gMapsDirRescan;  /* true = main.c doit updater menu.dir et rescanner */

/* panel modes/aim */
extern int       gModesSel;
extern int       gModesChoice;
extern Rectangle gAimCtrl;

/* touches configurables */
extern KeyBindings gKeys;
extern bool        gKeyCapture;
extern int         gKeyCaptureSlot;

/* chargement asynchrone */
extern AsyncLoader gLoader;
extern bool        gLoadFinalized;
extern MenuScanner gMenuScanner;

/* preview audio menu */
extern bool      gPreviewPlaying;
extern Rectangle gPreviewBtnRect;

/* =========================================================================
 * Prototypes : settings.c
 * ========================================================================= */

extern const int NPALETTE;

float       mods_score_mult(unsigned mods, float rate);

void        meshlist_scan(const char *dir);
int         mesh_index_of(const char *name);
const char *mesh_name_at(int idx);
void        notemesh_clear(void);
bool        notemesh_set(const char *name);

void        cursorlist_scan(const char *dir);
int         cursor_index_of(const char *name);
const char *cursor_name_at(int idx);
void        cursortex_clear(void);
bool        cursortex_set(const char *name);

void        hitsoundlist_scan(const char *dir);
int         hitsound_index_of(const char *name);
const char *hitsound_name_at(int idx);
void        hitsound_clear(void);
bool        hitsound_set(const char *name);

void        colorchain_scan(const char *dir);
int         colorchain_index_of(const char *filename);

void        settings_palette(const Settings *s, Color *out, int *len);
const char *settings_palette_name(const Settings *s);
void        settings_apply(const Settings *s);
void        settings_defaults(Settings *s);
void        settings_load(Settings *s);
void        settings_save(const Settings *s);

/* bg.c */
void bg_void_init(int sw, int sh);
void bg_circuit_init(int sw, int sh);
void bg_aurora_init(int sw, int sh);
void bg_pulse_on_beat(bool isDownbeat);
void bg_pulse_reset(void);
void bg_init(BgStyle style, int sw, int sh);
void bg_unload_all(void);
void bg_draw(BgStyle style, float intensity);
void bg_update(BgStyle style, float dt);
void bg_on_beat(bool isDownbeat);  /* a appeler sur chaque note frappee */
void cursor_draw_at(Vector2 pos, int projH);   /* dessine curseur + trainee a une position ecran */
void cursor_menu_trail_reset(void);            /* reinitialise le buffer de trainee menu */

bool        profile_is_fav(const char *fname);
void        profile_toggle_fav(const char *fname);
bool        profile_is_blacklisted(const char *fname);
void        profile_toggle_blacklist(const char *fname);
BestScore  *profile_get_best(const char *fname);
bool        profile_update_best(const char *fname, int score, int hits, int total, int maxCombo);
void        profile_load(void);
void        profile_save(void);

/* =========================================================================
 * Prototypes : profile_ui.c
 * ========================================================================= */
Rectangle   profile_btn_rect(int sw);
void        profile_draw(int sw, int sh, int mapCount);

/* =========================================================================
 * Prototypes : play.c
 * ========================================================================= */

extern const char *AIM_STYLE_NAMES[N_AIM_STYLES];

Color note_color(size_t i);
void  toggle_fullscreen(int winW, int winH);

void  cursor_halo_init(void);     /* genere la texture radiale (1x au demarrage) */
void  cursor_halo_unload(void);
void  cursor_config_defaults(CursorConfig *c);   /* valeurs par defaut (ref: bleu fondu) */

void  play_unload(Play *p);
void  play_reset(Play *p);
void  play_seek(Play *p, float ms);
void  play_cursor(Play *p, bool autoplay, int sw, int sh);
void  play_update(Play *p, bool autoplay, float dt, int sw, int sh);
void  play_draw_scene(Play *p, Camera3D cam, bool autoplay);
void  play_draw_hud(Play *p, int sw, int sh, bool autoplay);

void  aim_build(Play *p, const AimConfig *cfg);

/* =========================================================================
 * Prototypes : settings_ui.c
 * ========================================================================= */

void settings_update(int sw, int sh);
void settings_draw(int sw, int sh);
void mods_draw_inline(int panelX, int sh, Vector2 mp);
void mods_handle_click(int panelX, int sh, Vector2 mp);
int  opt_selected_global(void);

/* =========================================================================
 * Prototypes : menu.c
 * ========================================================================= */

Rectangle opt_btn_rect(int sw);
Rectangle modes_btn_rect(int sw);
Rectangle menu_mode_chip_rect(int panelX, int sh, int idx);
Rectangle menu_play_btn_rect(int panelX, int sh);
Rectangle menu_scroll_up_rect(int sw, int sh);
Rectangle menu_scroll_down_rect(int sw, int sh);

void menu_scan(Menu *m);
void menu_scan_start(Menu *m, MenuScanner *sc);
void menu_scan_join(MenuScanner *sc);
void menu_filter_rebuild(Menu *m);
void menu_sort(Menu *m);
void menu_draw(Menu *m, int sw, int sh);
void menu_list_geo(int sw, int sh, int *pPX, int *pPW, int *pLY, int *pLH);
float menu_scrl_px(const Menu *m, int listH);

void modes_update(int sw, int sh);
void modes_draw(int sw, int sh, const Menu *m);

/* =========================================================================
 * Prototypes : calib.c
 * ========================================================================= */

void calib_reset_samples(Calib *c);
void calib_begin(Calib *c);
void calib_update(Calib *c);
void calib_draw(Calib *c, int sw, int sh);
