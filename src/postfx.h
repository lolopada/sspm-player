/*
 * postfx.h — Post-traitement optionnel : effet de bloom / glow multi-passes.
 *
 * DESACTIVE PAR DEFAUT (gBloomOn == false). Dans ce cas postfx_present se borne a
 * blitter la scene plein ecran, ce qui reproduit EXACTEMENT le chemin de rendu
 * original (aucune render texture ni passe supplementaire -> cout nul).
 *
 * Quand le bloom est actif, le halo est calcule en resolution reduite (1/4) avec
 * un nombre de passes BORNE (voir BLOOM_PASSES dans postfx.c). Les shaders GLSL
 * sont EMBARQUES dans le binaire (LoadShaderFromMemory) : aucun fichier externe,
 * le .zip Windows reste self-contained.
 */
#pragma once

#include "raylib.h"

/* A appeler une seule fois, apres InitWindow (contexte GL pret). Compile les
 * shaders embarques. Sans echec, le bloom est disponible ; en cas d'echec de
 * compilation (vieux GPU/GLES), postfx_present retombe sur le blit simple. */
void postfx_init(void);

/* Decharge shaders + render textures du bloom. A appeler avant CloseWindow. */
void postfx_unload(void);

/* True si au moins un effet shader serait reellement rendu : interrupteur maitre
 * (gShadersOn) ON, au moins un effet actif (bloom, streaks, aberration chromatique,
 * shockwave ou beat punch) et shaders compiles. main.c s'en sert pour allouer un RT
 * plein ecran ; si false, le chemin de presentation reste le blit direct d'origine
 * (cout nul). */
bool postfx_active(void);

/* Declenche une onde de choc radiale (distorsion UV) partant du point ecran
 * (sx, sy) en coordonnees normalisees y-bas (0,0 = haut-gauche, 1,1 = bas-droite).
 * A appeler quand une note est frappee, avec la position ecran du curseur. L'onde
 * s'etale puis s'eteint ; le rendu se fait dans postfx_present (passe de base). */
void postfx_shockwave(float sx, float sy);

/* Presente la scene a l'ecran (avec bloom si gBloomOn).
 *   scene            texture de la RenderTexture ou la scene a ete dessinee
 *   srcW, srcH       dimensions de cette texture source
 *   screenW, screenH dimensions de la fenetre
 * La texture source est TOUJOURS lue avec une hauteur negative (flip Y des
 * RenderTexture raylib) ; idem pour toutes les passes offscreen. */
void postfx_present(Texture2D scene, int srcW, int srcH, int screenW, int screenH);
