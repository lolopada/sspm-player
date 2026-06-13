/*
 * sspm.h — Lecteur minimal du format SSPM v2 (Sound Space Plus Map / Rhythia).
 *
 * Le fichier entier est chargé en mémoire (les .sspm font quelques Mo, l'audio
 * embarqué étant le plus gros). L'audio n'est PAS copié : `audio` pointe
 * directement dans `fileData`. Conséquence : il faut décharger le flux audio
 * (UnloadMusicStream) AVANT d'appeler sspm_free().
 */
#ifndef SSPM_H
#define SSPM_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float ms;     /* timestamp en millisecondes */
    float x, y;   /* position sur la grille (0..2 en standard, float si quantum) */
} SspmNote;

typedef struct {
    /* --- métadonnées --- */
    uint32_t lastMs;       /* timestamp de la derniere note */
    uint32_t noteCount;    /* nb de notes annonce dans l'en-tete */
    uint8_t  difficulty;   /* 0..5 (voir sspm_difficulty_name) */
    bool     hasAudio;
    bool     hasCover;

    /* --- chaines --- */
    char mapName[256];
    char songName[256];
    char mapper[256];      /* mappeurs joints par ", " */

    /* --- notes (triees par ms apres chargement) --- */
    SspmNote *notes;
    uint32_t  noteCountLoaded;

    /* --- audio embarque (pointe dans fileData, ne pas free separement) --- */
    const uint8_t *audio;
    uint64_t       audioLen;
    char           audioExt[8];   /* ".mp3" ou ".ogg" */

    /* --- buffer du fichier complet (garde vivant tant que l'audio joue) --- */
    uint8_t *fileData;
    long      fileSize;
    /* vrai si audio est une allocation separee (format ZIP) et doit etre free dans sspm_free */
    bool      audioAllocd;
} SspmMap;

/* Infos legeres d'une carte, pour un menu : pas de notes ni d'audio charges.
 * Seul un petit prefixe du fichier est lu -> tres rapide pour lister un dossier. */
typedef struct {
    char     songName[256];
    char     mapName[256];
    char     mapper[256];
    uint8_t  difficulty;
    uint32_t noteCount;
    uint32_t lastMs;
    bool     hasAudio;
    bool     hasCover;
    bool     valid;
} SspmInfo;

/* Lit seulement l'en-tete (nom, mappeur, difficulte, duree, nb de notes) sans
 * charger les notes ni l'audio. Renvoie false si le fichier n'est pas un .sspm V2. */
bool sspm_load_info(const char *path, SspmInfo *out);

/* Charge `path` dans `out`. Renvoie false (et message sur stderr) en cas d'echec. */
bool sspm_load(const char *path, SspmMap *out);

/* Libere les buffers. A appeler APRES UnloadMusicStream. */
void sspm_free(SspmMap *m);

/* Extrait les octets bruts de la cover (PNG ou JPG) dans *dataOut (a free() par l'appelant).
 * Renvoie la taille en octets, ou 0 si pas de cover ou erreur. */
uint64_t sspm_load_cover(const char *path, uint8_t **dataOut);

/* Nom lisible d'une difficulte. */
const char *sspm_difficulty_name(uint8_t d);

#endif /* SSPM_H */
