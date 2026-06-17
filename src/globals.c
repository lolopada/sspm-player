/*
 * globals.c — Definitions de toutes les variables globales mutables.
 *             Les declarations extern correspondantes sont dans common.h.
 */
#include "common.h"

/* --- profil joueur --- */
Profile gProfile;
bool    gNewRecord  = false;
char    gCurrentMap[1024];

/* --- rendu / gameplay runtime --- */
float gApproachDist = APPROACH_DIST;
float gApproachMs   = APPROACH_MS;
bool  gTablet       = false;
Color gPalette[MAX_PALETTE] = { { 255, 90, 140, 255 }, { 80, 170, 255, 255 } };
int   gPaletteLen   = 2;

/* --- mesh de note personnalise --- */
NoteMesh gNoteMesh;

/* --- texture blanche 1x1 + render texture basse res --- */
Texture2D       gWhiteTex;
RenderTexture2D gRenderTex;
int gRtW = 0, gRtH = 0;

/* --- shaders / post-processing (off par defaut) --- */
bool  gShadersOn      = false;   /* interrupteur maitre : false -> aucun effet, cout nul */
bool  gBloomOn        = false;
float gBloomIntensity = 1.0f;
float gBloomThreshold = 0.7f;
float gBloomRadius    = 1.0f;
int   gBloomQuality   = 1;
int   gBloomTintHue   = -1;
bool  gBloomScreen    = false;
bool  gBloomInMenu    = false;
/* anamorphic streaks (trainees horizontales) */
bool  gStreakOn        = false;
float gStreakIntensity = 1.0f;
float gStreakLength    = 1.5f;
int   gStreakTintHue   = -1;
/* aberration chromatique (decalage RGB radial) */
bool  gCaOn            = false;
float gCaStrength      = 1.5f;
/* shockwave radial sur le hit (onde de distorsion UV) */
bool  gShockOn         = false;
float gShockStrength   = 24.0f;
float gShockSpeed      = 1.0f;
/* beat punch : leger zoom + aberration sur le downbeat (pilote par la FFT du grave) */
bool  gBeatPunchOn       = false;
float gBeatPunchStrength = 1.0f;
float gBeatPunchSens     = 1.0f;

/* --- couverture de la map selectionnee --- */
Texture2D gCoverTex;
bool      gHaveCover = false;

/* --- curseur : config generee (rond + halo) + trainee, image optionnelle --- */
CursorConfig gCursor;          /* initialise par settings_apply (cursor_config_defaults) */
int   gCursorMode   = 0;       /* 0 = genere, 1 = image */
Texture2D gHaloTex;            /* texture radiale partagee */
bool  gHaveHalo     = false;
CursorTex gCursorTex;
char  gCursorNames[MAX_CURSORS][128];
int   gCursorCount  = 0;
char  gCursorDir[1024] = "cursors";
char  gCursorMsg[160];
float gMusicVolume  = 1.0f;

/* --- hitsounds --- */
HitsoundSnd gHitsoundSnd;
char  gHitsoundNames[MAX_HITSOUNDS][128];
int   gHitsoundCount  = 0;
char  gHitsoundDir[1024] = "hitsounds";
char  gHitsoundMsg[160];
float gHitsoundVolume = 0.8f;

/* --- chaines de couleurs personnalisees --- */
ColorChain gColorChains[MAX_COLOR_CHAINS];
int        gColorChainCount = 0;
char       gColorChainDir[1024] = "colors";

/* --- parametres gameplay actifs --- */
bool  gGodMode       = false;
float gHitWindowMs   = HIT_EARLY;
int   gHueShift      = 0;
float gCullBehind    = CULL_BEHIND;
float gSens          = SENS;
float gAudioOffsetMs = 0.0f;
int   gJuiceMode     = 1;
int   gJuiceCount    = 14;
float gJuiceLife     = 0.50f;
float gJuiceSize     = 0.14f;
float gJuiceSpeed    = 1.00f;
float gJuicePulse    = 1.00f;
int   gJuiceCombo    = 50;

/* --- grille / notes --- */
int   gGridAlpha     = 90;
int   gGridStyle     = 0;
float gNoteScale     = 1.0f;

/* --- mods --- */
unsigned gMods      = 0;
float    gRate      = 1.0f;
float    gScoreMult = 1.0f;

/* --- mode de jeu --- */
GameMode gMode        = MODE_NORMAL;
float    gLadderRate  = 1.0f;
int      gLadderLevel = 0;
int      gMenuMode    = 0;

/* --- mode Entrainement (Practice) --- */
float gPracticeA    = 0.0f;
float gPracticeB    = 0.0f;
bool  gPracticeLoop = false;

/* --- meshes personnalises --- */
char gMeshNames[MAX_MESHES][128];
int  gMeshCount = 0;
char gMeshDir[1024] = "meshes";
char gMeshMsg[160];

/* --- touches configurables --- */
KeyBindings gKeys;
bool        gKeyCapture     = false;
int         gKeyCaptureSlot = 0;

/* --- reglages + UI options --- */
Settings  gSettings;
int       gOptSel     = 0;
int       gOptTab     = 0;
int       gShaderSection = 0;   /* onglet Shaders : 0=racine, 1=Bloom, 2=Anamorphic, 3=Chromatic, 4=Shockwave, 5=Beat punch */
bool      gSensEditing = false;
char      gSensEditBuf[32];
Rectangle gOptCtrl;
bool      gMapsDirRescan = false;

/* --- panel modes/aim --- */
int       gModesSel    = 0;
int       gModesChoice = -1;
Rectangle gAimCtrl;

/* --- chargement asynchrone --- */
AsyncLoader gLoader;
bool        gLoadFinalized = false;
MenuScanner gMenuScanner;

/* --- preview audio menu --- */
bool      gPreviewPlaying = false;
Rectangle gPreviewBtnRect;
