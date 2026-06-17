/*
 * postfx.c — Shaders de post-processing, optionnels et bornes.
 *            Interrupteur maitre gShadersOn ; effets : bloom + anamorphic streaks.
 *
 * Pipeline (uniquement quand gShadersOn et au moins un effet actif) :
 *   1) Bright-pass : shader de seuil (threshold + soft knee) ; scene -> rtBright,
 *      en resolution reduite (1/4 par defaut). PARTAGE par le bloom ET les streaks.
 *   2) Bloom (si gBloomOn) : flou gaussien separable (un seul shader, uniform uDir
 *      en texels) : H : rtBright -> rtBlurA puis V : rtBlurA -> rtBlurB, repete
 *      BLOOM_PASSES fois en ping-pong. L'intensite est appliquee a la passe finale.
 *   2b) Streaks (si gStreakOn) : UNE seule passe de flou horizontal tres large
 *      (rtBright -> rtStreak) -> trainees lumineuses facon synthwave. Cout quasi
 *      nul puisqu'on reutilise le bright-pass deja calcule.
 *   3) Composite a l'ecran : scene (base) + bloom + streaks (additif/screen, teintes).
 *
 * Regle de flip Y (RenderTexture raylib stockees a l'envers) : on lit CHAQUE
 * `.texture` avec une hauteur source NEGATIVE, dans les passes offscreen comme a
 * l'ecran. L'algebre de parite montre que la hauteur negative a chaque copie
 * preserve l'orientation : la scene et les halos ressortent tous a l'endroit.
 * Les flous etant symetriques, seule l'orientation finale importe.
 *
 * Cout nul quand les shaders sont OFF : postfx_present fait alors le meme blit
 * que le code d'origine et ne touche a aucune render texture.
 */
#include "common.h"
#include "postfx.h"
#include "rlgl.h"   /* rlSetBlendFactors + RL_* : composite "screen" optionnel */

/* Nombre d'iterations H+V du flou (BORNE — pas de boucle non bornee). */
#define BLOOM_PASSES 3

/* Facteur de reduction de resolution selon gBloomQuality (0=1/2, 1=1/4, 2=1/8).
 * Plus le diviseur est grand, plus le bloom est leger (et flou). */
static int bloom_div(void) {
    switch (gBloomQuality) { case 0: return 2; case 2: return 8; default: return 4; }
}

/* Teinte du halo : blanc (neutre) si gBloomTintHue < 0, sinon une couleur pastel
 * a la teinte demandee. Multipliee sur le blit additif/screen du bloom. */
static Color bloom_tint(void) {
    if (gBloomTintHue < 0) return WHITE;
    return hsv_to_rgb((float)(gBloomTintHue % 360), 0.45f, 1.0f, 255);
}

/* Teinte des streaks : blanc (neutre) si gStreakTintHue < 0, sinon une couleur
 * un peu plus saturee que le bloom (les traits synthwave aiment le cyan/magenta). */
static Color streak_tint(void) {
    if (gStreakTintHue < 0) return WHITE;
    return hsv_to_rgb((float)(gStreakTintHue % 360), 0.55f, 1.0f, 255);
}

/* --- Shaders embarques (GLSL #version 330, defaut raylib desktop) ---
 * Pour un support GLES2 / vieux GPU, fournir une variante "#version 100" :
 * remplacer `in`/`out`/`texture()` par `varying`/`gl_FragColor`/`texture2D()`
 * et passer le numero de version a LoadShaderFromMemory. Ici on cible le desktop. */

/* Bright-pass : ne garde que ce qui depasse uThreshold, avec un genou doux
 * (soft knee) pour une transition progressive plutot qu'un seuil net. */
static const char *BRIGHT_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float uThreshold;\n"
    "uniform float uKnee;\n"
    "void main() {\n"
    "    vec3 c = texture(texture0, fragTexCoord).rgb;\n"
    "    float br = max(c.r, max(c.g, c.b));\n"
    "    float knee = uThreshold * uKnee + 1e-5;\n"
    "    float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);\n"
    "    soft = soft * soft / (4.0 * knee + 1e-5);\n"
    "    float contrib = max(soft, br - uThreshold);\n"
    "    contrib = max(contrib, 0.0) / max(br, 1e-5);\n"
    "    finalColor = vec4(c * contrib, 1.0);\n"
    "}\n";

/* Flou gaussien separable (9 taps). uDir = (1/largeur, 0) en H, (0, 1/hauteur)
 * en V (en texels). uIntensity multiplie la sortie (1.0 sauf passe finale). */
static const char *BLUR_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec2 uDir;\n"
    "uniform float uIntensity;\n"
    "void main() {\n"
    "    float w0 = 0.227027;\n"
    "    float w1 = 0.1945946;\n"
    "    float w2 = 0.1216216;\n"
    "    float w3 = 0.054054;\n"
    "    float w4 = 0.016216;\n"
    "    vec3 acc = texture(texture0, fragTexCoord).rgb * w0;\n"
    "    acc += texture(texture0, fragTexCoord + uDir * 1.0).rgb * w1;\n"
    "    acc += texture(texture0, fragTexCoord - uDir * 1.0).rgb * w1;\n"
    "    acc += texture(texture0, fragTexCoord + uDir * 2.0).rgb * w2;\n"
    "    acc += texture(texture0, fragTexCoord - uDir * 2.0).rgb * w2;\n"
    "    acc += texture(texture0, fragTexCoord + uDir * 3.0).rgb * w3;\n"
    "    acc += texture(texture0, fragTexCoord - uDir * 3.0).rgb * w3;\n"
    "    acc += texture(texture0, fragTexCoord + uDir * 4.0).rgb * w4;\n"
    "    acc += texture(texture0, fragTexCoord - uDir * 4.0).rgb * w4;\n"
    "    finalColor = vec4(acc * uIntensity, 1.0);\n"
    "}\n";

/* Anamorphic streaks : flou HORIZONTAL large en une seule passe (49 taps), depuis
 * le bright-pass. Donne les traits lumineux facon synthwave qui partent des zones
 * vives (notes, curseur). uStep = ecart horizontal entre taps (en texels), mis a
 * l'echelle par la longueur ; falloff gaussien ; uIntensity multiplie la sortie. */
static const char *STREAK_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float uStep;\n"
    "uniform float uIntensity;\n"
    "void main() {\n"
    "    vec3 acc = vec3(0.0);\n"
    "    float wsum = 0.0;\n"
    "    const int R = 24;\n"
    "    for (int i = -R; i <= R; i++) {\n"
    "        float x = float(i) / float(R);\n"
    "        float w = exp(-x * x * 3.0);\n"
    "        acc += texture(texture0, fragTexCoord + vec2(uStep * float(i), 0.0)).rgb * w;\n"
    "        wsum += w;\n"
    "    }\n"
    "    finalColor = vec4(acc / max(wsum, 1e-5) * uIntensity, 1.0);\n"
    "}\n";

/* Aberration chromatique : decale R et B en sens opposes le long du rayon centre
 * -> ecran (frange RGB qui s'ouvre vers les bords). Passe pleine resolution sur la
 * scene elle-meme (pas le bright-pass). uOffset = decalage en UV au bord de l'ecran
 * (mis a l'echelle par la distance au centre) ; subtil par defaut pour ne pas
 * brouiller les notes. La symetrie radiale rend la passe insensible au flip Y. */
static const char *CA_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec2 uOffset;\n"
    "uniform float uZoom;\n"            /* beat punch : leger zoom vers le centre (0 = aucun) */
    "void main() {\n"
    "    vec2 uv  = (fragTexCoord - vec2(0.5)) * (1.0 - uZoom) + vec2(0.5);\n"
    "    vec2 dir = uv - vec2(0.5);\n"     /* du centre vers le pixel : 0 au centre, max aux bords */
    "    vec2 off = dir * uOffset;\n"
    "    float r = texture(texture0, uv + off).r;\n"
    "    float g = texture(texture0, uv).g;\n"
    "    float b = texture(texture0, uv - off).b;\n"
    "    float a = texture(texture0, uv).a;\n"
    "    finalColor = vec4(r, g, b, a) * colDiffuse;\n"
    "}\n";

/* Shockwave radial sur le hit : a chaque note frappee, une onde de distorsion UV
 * part du curseur et s'etale vers les bords. Passe pleine resolution sur la scene
 * (comme l'aberration chromatique) -> MONO-PASSE, aucune render texture. Jusqu'a
 * SHOCK_MAX ondes simultanees (uShockCenter/uShockAge en tableaux) : pour chacune,
 * un anneau de rayon croissant (ring = age/vie) pousse les UV radialement, avec un
 * profil derivee-de-gaussienne (pousse puis aspire) et une amplitude qui s'eteint.
 * uAspect corrige le ratio pour des anneaux circulaires. uCaOffset replie
 * l'aberration chromatique dans la MEME passe (0 = desactivee) : les deux effets se
 * composent sans passe supplementaire. Les centres sont fournis en espace texcoord
 * (y deja retourne par l'appelant) pour coller au flip Y du blit final. */
#define SHOCK_MAX 6
static const char *SHOCK_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec2 uShockCenter[6];\n"
    "uniform float uShockAge[6];\n"
    "uniform float uShockLife;\n"
    "uniform float uShockAmp;\n"
    "uniform float uAspect;\n"
    "uniform vec2 uCaOffset;\n"
    "uniform float uZoom;\n"           /* beat punch : leger zoom vers le centre (0 = aucun) */
    "void main() {\n"
    "    vec2 disp = vec2(0.0);\n"
    "    for (int i = 0; i < 6; i++) {\n"
    "        float age = uShockAge[i];\n"
    "        if (age >= 0.0 && age < uShockLife) {\n"
    "            float t = age / uShockLife;\n"          /* 0..1 sur la duree de vie */
    "            float decay = 1.0 - t; decay *= decay;\n"  /* amplitude qui s'eteint */
    "            float ring = t * 1.3;\n"                /* rayon de l'anneau (sort de l'ecran a la fin) */
    "            vec2 d = fragTexCoord - uShockCenter[i];\n"
    "            vec2 dd = vec2(d.x * uAspect, d.y);\n"  /* corrige le ratio -> anneau circulaire */
    "            float dist = length(dd);\n"
    "            float diff = dist - ring;\n"
    "            float band = 0.07;\n"                   /* epaisseur de l'anneau */
    "            float prof = (diff / band) * exp(-(diff * diff) / (band * band));\n"
    "            vec2 dir = dist > 1e-4 ? d / dist : vec2(0.0);\n"
    "            disp += dir * prof * uShockAmp * decay;\n"
    "        }\n"
    "    }\n"
    "    vec2 uv  = fragTexCoord + disp;\n"
    "    uv = (uv - vec2(0.5)) * (1.0 - uZoom) + vec2(0.5);\n"   /* beat punch : zoom replie ici */
    "    vec2 off = (uv - vec2(0.5)) * uCaOffset;\n"     /* aberration chromatique repliee ici */
    "    float r = texture(texture0, uv + off).r;\n"
    "    float g = texture(texture0, uv).g;\n"
    "    float b = texture(texture0, uv - off).b;\n"
    "    float a = texture(texture0, uv).a;\n"
    "    finalColor = vec4(r, g, b, a) * colDiffuse;\n"
    "}\n";

/* --- Etat du module --- */
static bool   sReady = false;          /* shaders compiles avec succes */
static Shader sBright, sBlur, sStreak, sCA, sShock;
static int    sLocThreshold, sLocKnee; /* uniforms du bright-pass */
static int    sLocDir, sLocIntensity;  /* uniforms du flou */
static int    sLocStep, sLocStreakInt; /* uniforms des streaks */
static int    sLocCAOff, sLocCAZoom;   /* uniforms de l'aberration chromatique (+ zoom beat punch) */
/* uniforms du shockwave */
static int    sLocShCenter, sLocShAge, sLocShLife, sLocShAmp, sLocShAspect, sLocShCa, sLocShZoom;

/* Ondes de choc actives (tampon circulaire). Centre en espace texcoord ; instant de
 * declenchement (GetTime) ; start < 0 -> emplacement libre. */
static Vector2 sShockCenter[SHOCK_MAX];
static double  sShockStart[SHOCK_MAX];
static int     sShockNext = 0;

static RenderTexture2D rtBright, rtBlurA, rtBlurB, rtStreak;
static int sBW = 0, sBH = 0;           /* dimensions courantes des RT (reduites) */

void postfx_init(void) {
    sBright = LoadShaderFromMemory(0, BRIGHT_FS);
    sBlur   = LoadShaderFromMemory(0, BLUR_FS);
    sStreak = LoadShaderFromMemory(0, STREAK_FS);
    sCA     = LoadShaderFromMemory(0, CA_FS);
    sShock  = LoadShaderFromMemory(0, SHOCK_FS);
    /* shader.id == 0 -> echec de compilation : on reste sur le blit simple */
    sReady = (sBright.id != 0 && sBlur.id != 0 && sStreak.id != 0 &&
              sCA.id != 0 && sShock.id != 0);
    for (int i = 0; i < SHOCK_MAX; i++) sShockStart[i] = -1.0;   /* aucune onde au depart */
    if (!sReady) return;
    sLocThreshold = GetShaderLocation(sBright, "uThreshold");
    sLocKnee      = GetShaderLocation(sBright, "uKnee");
    sLocDir       = GetShaderLocation(sBlur,   "uDir");
    sLocIntensity = GetShaderLocation(sBlur,   "uIntensity");
    sLocStep      = GetShaderLocation(sStreak, "uStep");
    sLocStreakInt = GetShaderLocation(sStreak, "uIntensity");
    sLocCAOff     = GetShaderLocation(sCA,     "uOffset");
    sLocCAZoom    = GetShaderLocation(sCA,     "uZoom");
    sLocShCenter  = GetShaderLocation(sShock,  "uShockCenter");
    sLocShAge     = GetShaderLocation(sShock,  "uShockAge");
    sLocShLife    = GetShaderLocation(sShock,  "uShockLife");
    sLocShAmp     = GetShaderLocation(sShock,  "uShockAmp");
    sLocShAspect  = GetShaderLocation(sShock,  "uAspect");
    sLocShCa      = GetShaderLocation(sShock,  "uCaOffset");
    sLocShZoom    = GetShaderLocation(sShock,  "uZoom");
}

void postfx_unload(void) {
    if (sBW > 0) {
        UnloadRenderTexture(rtBright);
        UnloadRenderTexture(rtBlurA);
        UnloadRenderTexture(rtBlurB);
        UnloadRenderTexture(rtStreak);
        sBW = sBH = 0;
    }
    if (sBright.id != 0) UnloadShader(sBright);
    if (sBlur.id   != 0) UnloadShader(sBlur);
    if (sStreak.id != 0) UnloadShader(sStreak);
    if (sCA.id     != 0) UnloadShader(sCA);
    if (sShock.id  != 0) UnloadShader(sShock);
    sReady = false;
}

/* Voir postfx.h. Enregistre une onde de choc partant du point ecran (sx, sy) en
 * coordonnees normalisees y-bas (0,0 = haut-gauche). On stocke le centre en espace
 * texcoord : le blit final retourne Y (hauteur source negative), donc cy_tex = 1-sy
 * (X non retourne). No-op si les shaders ne sont pas prets. */
void postfx_shockwave(float sx, float sy) {
    if (!sReady) return;
    int i = sShockNext;
    sShockCenter[i].x = clampf(sx, 0.0f, 1.0f);
    sShockCenter[i].y = 1.0f - clampf(sy, 0.0f, 1.0f);
    sShockStart[i]    = GetTime();
    sShockNext = (i + 1) % SHOCK_MAX;
}

/* Voir postfx.h. */
bool postfx_active(void) {
    return sReady && gShadersOn && (gBloomOn || gStreakOn || gCaOn || gShockOn || gBeatPunchOn);
}

/* (Re)alloue les 3 render textures du bloom en 1/4 de resolution. Ne realloue
 * que si la taille change (changement de fenetre/plein ecran). */
static void ensure_targets(int screenW, int screenH) {
    int div = bloom_div();
    int bw = screenW / div; if (bw < 1) bw = 1;
    int bh = screenH / div; if (bh < 1) bh = 1;
    if (bw == sBW && bh == sBH) return;
    if (sBW > 0) {
        UnloadRenderTexture(rtBright);
        UnloadRenderTexture(rtBlurA);
        UnloadRenderTexture(rtBlurB);
        UnloadRenderTexture(rtStreak);
    }
    rtBright = LoadRenderTexture(bw, bh);
    rtBlurA  = LoadRenderTexture(bw, bh);
    rtBlurB  = LoadRenderTexture(bw, bh);
    rtStreak = LoadRenderTexture(bw, bh);
    /* filtrage bilineaire : halo lisse a l'upscale plein ecran */
    SetTextureFilter(rtBright.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(rtBlurA.texture,  TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(rtBlurB.texture,  TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(rtStreak.texture, TEXTURE_FILTER_BILINEAR);
    sBW = bw; sBH = bh;
}

/* Blit plein-cible d'une texture, hauteur source NEGATIVE (flip Y des RT).
 * tint = WHITE pour un blit neutre ; sert a teinter le halo au composite final. */
static void blit_flip(Texture2D t, int srcW, int srcH, int dstW, int dstH, Color tint) {
    DrawTexturePro(t,
        (Rectangle){ 0.0f, 0.0f, (float)srcW, -(float)srcH },
        (Rectangle){ 0.0f, 0.0f, (float)dstW, (float)dstH },
        (Vector2){ 0.0f, 0.0f }, 0.0f, tint);
}

void postfx_present(Texture2D scene, int srcW, int srcH, int screenW, int screenH) {
    bool bloomActive  = gShadersOn && gBloomOn;
    bool streakActive = gShadersOn && gStreakOn;
    bool caActive     = gShadersOn && gCaOn;
    bool shockOn      = gShadersOn && gShockOn;

    /* Ondes de choc vivantes : age = maintenant - declenchement, vie pilotee par la
     * vitesse (plus rapide -> plus court & plus nerveux). On expire les emplacements
     * echus. Le shader multi-ondes ne tourne QUE s'il reste une onde vivante. */
    float shockAges[SHOCK_MAX];
    bool  shockLive = false;
    float shockLife = 0.0f;
    if (shockOn && sReady) {
        double now = GetTime();
        shockLife  = 0.55f / clampf(gShockSpeed, 0.5f, 2.5f);
        for (int i = 0; i < SHOCK_MAX; i++) {
            if (sShockStart[i] >= 0.0) {
                float age = (float)(now - sShockStart[i]);
                if (age >= 0.0f && age < shockLife) { shockAges[i] = age; shockLive = true; }
                else { shockAges[i] = -1.0f; sShockStart[i] = -1.0; }
            } else shockAges[i] = -1.0f;
        }
    }
    bool shockActive = shockOn && shockLive;

    /* Beat punch : enveloppe 0..1 du grave (FFT partagee avec le fond SPECTRUM, cf.
     * bg.c). Pilote un leger zoom vers le centre + une aberration chromatique qui
     * pulsent sur le downbeat. Replie dans la passe de base (zoom + CA) : mono-passe. */
    bool  punchOn   = gShadersOn && gBeatPunchOn;
    float punchZoom = 0.0f;
    Vector2 punchCa = { 0.0f, 0.0f };
    if (punchOn && sReady) {
        float env = bg_punch_level();
        float st  = clampf(gBeatPunchStrength, 0.0f, 2.0f);
        punchZoom = env * 0.10f * st;                 /* jusqu'a ~10% de zoom * intensite */
        float caPx = env * 7.0f * st;                 /* jusqu'a ~7 px d'aberration * intensite */
        punchCa = (Vector2){ 2.0f * caPx / (float)screenW, 2.0f * caPx / (float)screenH };
    }
    bool punchActive = punchOn && (punchZoom > 0.0001f || punchCa.x > 0.0f);

    /* --- Shaders OFF (ou indisponibles) : chemin identique a l'origine --- */
    if ((!bloomActive && !streakActive && !caActive && !shockOn && !punchActive) || !sReady) {
        BeginDrawing();
        ClearBackground(BLACK);
        blit_flip(scene, srcW, srcH, screenW, screenH, WHITE);
        EndDrawing();
        return;
    }

    /* Bloom et streaks utilisent des passes offscreen en resolution reduite (et le
     * bright-pass partage). L'aberration chromatique, elle, s'applique en pleine
     * resolution directement sur le blit de base : aucune render texture requise. */
    int bw = 0, bh = 0;
    bool needTargets = bloomActive || streakActive;
    if (needTargets) {
        ensure_targets(screenW, screenH);
        bw = sBW; bh = sBH;

        /* 1) Bright-pass : scene -> rtBright (resolution reduite). Partage bloom+streaks. */
        float thr  = clampf(gBloomThreshold, 0.0f, 1.0f);
        float knee = 0.5f;
        SetShaderValue(sBright, sLocThreshold, &thr,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(sBright, sLocKnee,      &knee, SHADER_UNIFORM_FLOAT);
        BeginTextureMode(rtBright);
            ClearBackground(BLACK);
            BeginShaderMode(sBright);
                blit_flip(scene, srcW, srcH, bw, bh, WHITE);
            EndShaderMode();
        EndTextureMode();
    }

    /* 2) Bloom : flou separable en ping-pong, borne a BLOOM_PASSES iterations.
     *    Iteration i : H (source -> rtBlurA) puis V (rtBlurA -> rtBlurB).
     *    source = rtBright a la 1ere iteration, rtBlurB ensuite.
     *    uDir mis a l'echelle par le rayon : ecarte les taps pour elargir le halo. */
    if (bloomActive) {
        float rad = clampf(gBloomRadius, 0.5f, 3.0f);
        Vector2 dirH = { rad / (float)bw, 0.0f };
        Vector2 dirV = { 0.0f, rad / (float)bh };
        float one = 1.0f;
        for (int i = 0; i < BLOOM_PASSES; i++) {
            Texture2D src = (i == 0) ? rtBright.texture : rtBlurB.texture;
            /* passe horizontale */
            SetShaderValue(sBlur, sLocDir,       &dirH, SHADER_UNIFORM_VEC2);
            SetShaderValue(sBlur, sLocIntensity, &one,  SHADER_UNIFORM_FLOAT);
            BeginTextureMode(rtBlurA);
                ClearBackground(BLACK);
                BeginShaderMode(sBlur);
                    blit_flip(src, bw, bh, bw, bh, WHITE);
                EndShaderMode();
            EndTextureMode();
            /* passe verticale ; intensite appliquee a la passe finale uniquement */
            float vint = (i == BLOOM_PASSES - 1) ? clampf(gBloomIntensity, 0.0f, 3.0f) : 1.0f;
            SetShaderValue(sBlur, sLocDir,       &dirV, SHADER_UNIFORM_VEC2);
            SetShaderValue(sBlur, sLocIntensity, &vint, SHADER_UNIFORM_FLOAT);
            BeginTextureMode(rtBlurB);
                ClearBackground(BLACK);
                BeginShaderMode(sBlur);
                    blit_flip(rtBlurA.texture, bw, bh, bw, bh, WHITE);
                EndShaderMode();
            EndTextureMode();
        }
    }

    /* 2b) Anamorphic streaks : une seule passe horizontale tres large depuis le
     *     bright-pass. uStep = ecart entre taps en texels, proportionnel a la
     *     longueur demandee (chaque cote couvre 24 * uStep texels). */
    if (streakActive) {
        float len  = clampf(gStreakLength, 0.5f, 4.0f);
        float step = len * 4.0f / (float)bw;
        float sint = clampf(gStreakIntensity, 0.0f, 3.0f);
        SetShaderValue(sStreak, sLocStep,      &step, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sStreak, sLocStreakInt, &sint, SHADER_UNIFORM_FLOAT);
        BeginTextureMode(rtStreak);
            ClearBackground(BLACK);
            BeginShaderMode(sStreak);
                blit_flip(rtBright.texture, bw, bh, bw, bh, WHITE);
            EndShaderMode();
        EndTextureMode();
    }

    /* 3) Composite a l'ecran : scene (base) + bloom + streaks par-dessus.
     *    Base, par ordre de priorite (toujours UNE seule passe pleine resolution) :
     *      - shockwave actif : shader d'ondes (distorsion UV radiale) qui replie aussi
     *        l'aberration chromatique (uCaOffset) -> les deux effets se composent ;
     *      - sinon aberration chromatique seule (sCA) ;
     *      - sinon scene directe.
     *    Bloom additif (defaut, lumineux) ou screen : src + dst*(1-src), plus doux
     *    (via rlSetBlendFactors). Streaks toujours additifs (traits de lumiere). */
    BeginDrawing();
        ClearBackground(BLACK);
        /* decalage CA au bord = px pixels : dir va jusqu'a 0.5 -> facteur 2 px / dim.
         * Le beat punch ajoute sa propre aberration (punchCa) et un zoom (punchZoom). */
        Vector2 caOff = { 0.0f, 0.0f };
        if (caActive) {
            float px = clampf(gCaStrength, 0.0f, 4.0f);
            caOff = (Vector2){ 2.0f * px / (float)screenW, 2.0f * px / (float)screenH };
        }
        caOff.x += punchCa.x;
        caOff.y += punchCa.y;
        float zoom = punchZoom;
        if (shockActive) {
            float amp    = clampf(gShockStrength, 0.0f, 80.0f) / (0.45f * (float)screenW);
            float aspect = (float)screenW / (float)screenH;
            SetShaderValueV(sShock, sLocShCenter, sShockCenter, SHADER_UNIFORM_VEC2,  SHOCK_MAX);
            SetShaderValueV(sShock, sLocShAge,    shockAges,    SHADER_UNIFORM_FLOAT, SHOCK_MAX);
            SetShaderValue (sShock, sLocShLife,   &shockLife,   SHADER_UNIFORM_FLOAT);
            SetShaderValue (sShock, sLocShAmp,    &amp,         SHADER_UNIFORM_FLOAT);
            SetShaderValue (sShock, sLocShAspect, &aspect,      SHADER_UNIFORM_FLOAT);
            SetShaderValue (sShock, sLocShCa,     &caOff,       SHADER_UNIFORM_VEC2);
            SetShaderValue (sShock, sLocShZoom,   &zoom,        SHADER_UNIFORM_FLOAT);
            BeginShaderMode(sShock);
                blit_flip(scene, srcW, srcH, screenW, screenH, WHITE);
            EndShaderMode();
        } else if (caActive || punchActive) {
            SetShaderValue(sCA, sLocCAOff,  &caOff, SHADER_UNIFORM_VEC2);
            SetShaderValue(sCA, sLocCAZoom, &zoom,  SHADER_UNIFORM_FLOAT);
            BeginShaderMode(sCA);
                blit_flip(scene, srcW, srcH, screenW, screenH, WHITE);
            EndShaderMode();
        } else {
            blit_flip(scene, srcW, srcH, screenW, screenH, WHITE);
        }
        if (bloomActive) {
            if (gBloomScreen) {
                rlSetBlendFactors(RL_ONE, RL_ONE_MINUS_SRC_COLOR, RL_FUNC_ADD);
                BeginBlendMode(BLEND_CUSTOM);
            } else {
                BeginBlendMode(BLEND_ADDITIVE);
            }
                blit_flip(rtBlurB.texture, bw, bh, screenW, screenH, bloom_tint());
            EndBlendMode();
        }
        if (streakActive) {
            BeginBlendMode(BLEND_ADDITIVE);
                blit_flip(rtStreak.texture, bw, bh, screenW, screenH, streak_tint());
            EndBlendMode();
        }
    EndDrawing();
}
