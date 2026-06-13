#include "common.h"

/* ===================================================================== */
/*  Menu de selection                                                    */
/* ===================================================================== */

Rectangle opt_btn_rect(int sw)     { return (Rectangle){ (float)(sw - 146), 4.0f, 130.0f, 28.0f }; }
Rectangle modes_btn_rect(int sw)   { return (Rectangle){ (float)(sw - 286), 4.0f, 130.0f, 28.0f }; }
/* 3 chips Normal/Zen/Speed Ladder au-dessus du bouton JOUER (panneau gauche). */
Rectangle menu_mode_chip_rect(int panelX, int sh, int idx) {
    int tx = 38, bw = panelX - tx - 30, cw = (bw - 8) / 3;
    return (Rectangle){ (float)(tx + idx * (cw + 4)), (float)(sh - 90), (float)cw, 22.0f };
}
/* Bouton JOUER dans le panneau gauche. */
Rectangle menu_play_btn_rect(int panelX, int sh) {
    return (Rectangle){ 38.0f, (float)(sh - 62), (float)(panelX - 68), 44.0f };
}
/* Bouton scroll haut : occupe la moitie superieure de la zone de liste, cote droit. */
Rectangle menu_scroll_up_rect(int sw, int sh) {
    int px, pw, ly, lh; menu_list_geo(sw, sh, &px, &pw, &ly, &lh);
    int half = lh / 2;
    return (Rectangle){ (float)(sw - SCROLL_BTN_W), (float)ly, (float)SCROLL_BTN_W, (float)(half - 1) };
}
/* Bouton scroll bas : occupe la moitie inferieure de la zone de liste, cote droit. */
Rectangle menu_scroll_down_rect(int sw, int sh) {
    int px, pw, ly, lh; menu_list_geo(sw, sh, &px, &pw, &ly, &lh);
    int half = lh / 2;
    return (Rectangle){ (float)(sw - SCROLL_BTN_W), (float)(ly + half + 1), (float)SCROLL_BTN_W, (float)(lh - half - 1) };
}

/* Comparaison de chaines insensible a la casse (sans dependance plateforme). */
static int ci_cmp(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
/* Recherche de sous-chaine insensible a la casse. */
static bool ci_contains(const char *hay, const char *needle) {
    if (!needle || !needle[0]) return true;
    int nl = (int)strlen(needle);
    for (int i = 0; hay[i]; i++) {
        int j;
        for (j = 0; j < nl; j++) {
            char a = hay[i + j], b = needle[j];
            if (!a) break;
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (j == nl) return true;
    }
    return false;
}

/* Extrait le star rate depuis le nom de fichier format "X.XX - Artist - Title.sspm".
 * Renvoie -1 si le fichier ne commence pas par un nombre suivi de " - ". */
static float parse_star_rate(const char *path) {
    const char *fname = GetFileName(path);
    const char *sep = strstr(fname, " - ");
    if (!sep) return -1.0f;
    int plen = (int)(sep - fname);
    if (plen <= 0 || plen > 16) return -1.0f;
    char buf[17];
    memcpy(buf, fname, (size_t)plen);
    buf[plen] = '\0';
    char *end;
    float v = strtof(buf, &end);
    if (end == buf || *end != '\0' || v < 0.0f) return -1.0f;
    return v;
}

static int cmp_entry_name(const void *a, const void *b) {
    return ci_cmp(((const MenuEntry *)a)->info.songName,
                  ((const MenuEntry *)b)->info.songName);
}
static int cmp_entry_diff(const void *a, const void *b) {
    int da = (int)((const MenuEntry *)a)->info.difficulty;
    int db = (int)((const MenuEntry *)b)->info.difficulty;
    if (da == 0) da = 255;   /* N/A trie apres toutes les difficultes connues */
    if (db == 0) db = 255;
    int d = da - db;
    return d ? d : cmp_entry_name(a, b);
}
static int cmp_entry_dur(const void *a, const void *b) {
    int d = (int)((const MenuEntry *)a)->info.lastMs -
            (int)((const MenuEntry *)b)->info.lastMs;
    return d ? d : cmp_entry_name(a, b);
}
static int cmp_entry_notes(const void *a, const void *b) {
    int d = (int)((const MenuEntry *)a)->info.noteCount -
            (int)((const MenuEntry *)b)->info.noteCount;
    return d ? d : cmp_entry_name(a, b);
}
static int cmp_entry_star(const void *a, const void *b) {
    float sa = ((const MenuEntry *)a)->starRate;
    float sb = ((const MenuEntry *)b)->starRate;
    if (sa < 0 && sb < 0) return cmp_entry_name(a, b);
    if (sa < 0) return 1;   /* pas de star rate -> a la fin */
    if (sb < 0) return -1;
    if (sa < sb) return -1;
    if (sa > sb) return  1;
    return cmp_entry_name(a, b);
}

/* Couleur associee a une difficulte (badges visuels). */
static Color diff_color(uint8_t d) {
    static const Color dc[6] = {
        { 140, 140, 160, 255 },  /* 0 = N/A   */
        {  80, 220, 100, 255 },  /* 1 = Easy  */
        { 220, 190,  50, 255 },  /* 2 = Medium*/
        { 240, 130,  40, 255 },  /* 3 = Hard  */
        { 230,  60,  60, 255 },  /* 4 = Logic */
        { 190,  60, 220, 255 },  /* 5 = Tasukete */
    };
    return (d < 6) ? dc[d] : dc[0];
}

/* Reconstruit la vue filtree selon m->filter. O(n), jamais par frame. */
void menu_filter_rebuild(Menu *m) {
    m->filteredCount = 0;
    const char *f = m->filter;
    bool inColl = (m->viewTab == 1 && m->collSel >= 0 && m->collSel < m->folderCount);
    for (int i = 0; i < m->count; i++) {
        bool textOk = (!f[0] ||
            ci_contains(m->items[i].info.songName, f) ||
            ci_contains(m->items[i].info.mapper,   f) ||
            ci_contains(m->items[i].info.mapName,  f));
        bool diffOk = (m->diffFilter < 0 || m->items[i].info.difficulty == (uint8_t)m->diffFilter);
        bool favOk  = (!m->favsOnly || profile_is_fav(GetFileName(m->items[i].path)));
        bool newOk  = (!m->newOnly  || profile_get_best(GetFileName(m->items[i].path)) == NULL);
        bool banOk  = (!m->hideBlacklisted || !profile_is_blacklisted(GetFileName(m->items[i].path)));
        bool collOk = true;
        if (inColl) {
            const char *sp = m->folderNodes[m->collSel].path;
            if (sp[0] == '\0') {
                collOk = (m->items[i].collection[0] == '\0');
            } else {
                int slen = (int)strlen(sp);
                const char *c = m->items[i].collection;
                collOk = (strcmp(c, sp) == 0 ||
                          (strncmp(c, sp, (size_t)slen) == 0 && c[slen] == '/'));
            }
        }
        if (textOk && diffOk && favOk && newOk && collOk && banOk) {
            if (m->filtered) m->filtered[m->filteredCount] = i;
            m->filteredCount++;
        }
    }
    if (m->sel >= m->filteredCount) m->sel = m->filteredCount > 0 ? m->filteredCount - 1 : 0;
    m->scroll = 0;
    /* En vue arbre de dossiers : filteredCount = folderCount pour que la navigation
     * (sel, scroll) parcoure les dossiers plutot que les maps. */
    if (m->viewTab == 1 && m->collSel < 0) {
        m->filteredCount = m->folderCount;
        if (m->sel >= m->filteredCount) m->sel = m->filteredCount > 0 ? m->filteredCount - 1 : 0;
    }
}

/* Trie m->items selon m->sortMode, puis reconstruit le filtre. */
void menu_sort(Menu *m) {
    if (m->count > 1) {
        typedef int (*CmpFn)(const void *, const void *);
        static const CmpFn fns[5] = {
            cmp_entry_name, cmp_entry_diff, cmp_entry_dur, cmp_entry_notes, cmp_entry_star
        };
        int mode = (m->sortMode >= 0 && m->sortMode < 5) ? m->sortMode : 0;
        qsort(m->items, (size_t)m->count, sizeof(MenuEntry), fns[mode]);
    }
    menu_filter_rebuild(m);
}

/* (Re)scanne le dossier du menu et lit l'en-tete de chaque .sspm.
 * Appele une seule fois (au demarrage / sur F5), jamais par frame. */
void menu_scan(Menu *m) {
    free(m->items);    m->items    = NULL;
    free(m->filtered); m->filtered = NULL;
    m->count = 0; m->filteredCount = 0;
    m->filter[0] = '\0';
    m->filterCursor = 0;
    m->filterFocused = false;
    m->folderCount = 0;
    m->collSel     = -1;
    m->scanCount   = 0;
    m->scanTotal   = 0;
    if (!DirectoryExists(m->dir)) return;

    FilePathList list = LoadDirectoryFilesEx(m->dir, ".sspm", true);
    int cap = (int)list.count;
    if (cap > MAX_MAPS) cap = MAX_MAPS;
    if (cap > 0) {
        m->items    = (MenuEntry *)calloc((size_t)cap, sizeof(MenuEntry));
        m->filtered = (int *)malloc(sizeof(int) * (size_t)cap);
        if (!m->items || !m->filtered) {
            free(m->items); free(m->filtered);
            m->items = NULL; m->filtered = NULL;
            UnloadDirectoryFiles(list);
            return;
        }
    }
    m->scanTotal = cap;  /* total connu avant la boucle -> barre de progression */

    int n = 0;
    for (unsigned int i = 0; i < list.count && n < cap; i++) {
        MenuEntry *e = &m->items[n];
        snprintf(e->path, sizeof e->path, "%s", list.paths[i]);
        if (!sspm_load_info(e->path, &e->info))
            e->info.valid = false;
        if (e->info.songName[0] == '\0')   /* secours : nom de fichier */
            snprintf(e->info.songName, sizeof e->info.songName, "%s", GetFileName(list.paths[i]));
        e->starRate = parse_star_rate(e->path);
        n++;
        m->scanCount = n;  /* progression visible par le thread principal */
    }
    /* Deduplication : si le meme nom de fichier apparait dans plusieurs sous-dossiers
     * (maps/ et maps/subfolder/), on ne garde que la premiere occurrence.
     * Complexite O(n^2) acceptable : appele une seule fois (demarrage / F5),
     * pas par frame. Pour MAX_MAPS=4096 c'est ~16M comparaisons au pire, ~1ms. */
    int unique = 0;
    for (int i = 0; i < n; i++) {
        const char *fname = GetFileName(m->items[i].path);
        bool dup = false;
        for (int j = 0; j < i && !dup; j++)
            if (strcmp(GetFileName(m->items[j].path), fname) == 0) dup = true;
        if (!dup) {
            if (unique != i) m->items[unique] = m->items[i];
            unique++;
        }
    }
    n = unique;

    /* Extraction du chemin de dossier relatif complet de chaque map. */
    {
        int dlen = (int)strlen(m->dir);
        for (int i = 0; i < n; i++) {
            const char *p = m->items[i].path;
            const char *rel = p;
            if (dlen > 0 && strncmp(p, m->dir, (size_t)dlen) == 0 &&
                (p[dlen] == '/' || p[dlen] == '\\'))
                rel = p + dlen + 1;
            /* rel = "a/b/c/file.sspm" ou "file.sspm" */
            int lastSep = -1;
            for (int k = 0; rel[k]; k++)
                if (rel[k] == '/' || rel[k] == '\\') lastSep = k;
            if (lastSep > 0) {
                int l = lastSep < 255 ? lastSep : 255;
                memcpy(m->items[i].collection, rel, (size_t)l);
                m->items[i].collection[l] = '\0';
                for (int k = 0; k < l; k++)
                    if (m->items[i].collection[k] == '\\') m->items[i].collection[k] = '/';
            } else {
                m->items[i].collection[0] = '\0';
            }
        }
    }
    /* Construction de l'arbre de dossiers (tous niveaux de profondeur). */
    {
        /* Collecte tous les chemins + prefixes intermediaires. */
        static char tmpPaths[512][256];
        static int  tmpDirect[512];
        int tmpCount = 0;

        for (int i = 0; i < n; i++) {
            const char *col = m->items[i].collection;
            if (col[0] == '\0') continue;
            int clen = (int)strlen(col);
            for (int k = 1; k <= clen; k++) {
                if (col[k] != '/' && col[k] != '\0') continue;
                /* prefixe = col[0..k-1] */
                bool found = false;
                for (int j = 0; j < tmpCount; j++) {
                    if ((int)strlen(tmpPaths[j]) == k &&
                        strncmp(tmpPaths[j], col, (size_t)k) == 0) {
                        found = true;
                        if (col[k] == '\0') tmpDirect[j]++;
                        break;
                    }
                }
                if (!found && tmpCount < 512) {
                    memcpy(tmpPaths[tmpCount], col, (size_t)k);
                    tmpPaths[tmpCount][k] = '\0';
                    tmpDirect[tmpCount] = (col[k] == '\0') ? 1 : 0;
                    tmpCount++;
                }
            }
        }

        /* Tri lexicographique */
        for (int i = 0; i < tmpCount - 1; i++)
            for (int j = i + 1; j < tmpCount; j++)
                if (ci_cmp(tmpPaths[i], tmpPaths[j]) > 0) {
                    char t[256]; int td;
                    memcpy(t, tmpPaths[i], 256);
                    memcpy(tmpPaths[i], tmpPaths[j], 256);
                    memcpy(tmpPaths[j], t, 256);
                    td = tmpDirect[i]; tmpDirect[i] = tmpDirect[j]; tmpDirect[j] = td;
                }

        /* Construction des FolderNode */
        m->folderCount = 0;
        for (int i = 0; i < tmpCount && m->folderCount < 511; i++) {
            FolderNode *fn = &m->folderNodes[m->folderCount];
            int plen = (int)strlen(tmpPaths[i]);
            memcpy(fn->path, tmpPaths[i], (size_t)(plen + 1));

            /* Nom = dernier composant */
            int lastSlash = -1;
            for (int k = 0; k < plen; k++)
                if (tmpPaths[i][k] == '/') lastSlash = k;
            snprintf(fn->name, sizeof fn->name, "%s", tmpPaths[i] + lastSlash + 1);

            /* Profondeur = nombre de '/' dans le chemin */
            fn->depth = 0;
            for (int k = 0; k < plen; k++)
                if (tmpPaths[i][k] == '/') fn->depth++;

            fn->directCount = tmpDirect[i];

            /* totalCount : maps dont collection == path ou commence par path + "/" */
            fn->totalCount = 0;
            for (int j = 0; j < n; j++) {
                const char *c = m->items[j].collection;
                if (strcmp(c, tmpPaths[i]) == 0 ||
                    (strncmp(c, tmpPaths[i], (size_t)plen) == 0 && c[plen] == '/'))
                    fn->totalCount++;
            }

            /* parentIdx : cherche le noeud dont le chemin est le prefixe direct */
            fn->parentIdx = -1;
            if (lastSlash > 0) {
                for (int j = 0; j < m->folderCount; j++) {
                    int jlen = (int)strlen(m->folderNodes[j].path);
                    if (jlen == lastSlash &&
                        strncmp(m->folderNodes[j].path, tmpPaths[i], (size_t)lastSlash) == 0) {
                        fn->parentIdx = j;
                        break;
                    }
                }
            }

            m->folderCount++;
        }

        /* Entree "Pas triees" si des maps sont a la racine */
        {
            int rootCount = 0;
            for (int i = 0; i < n; i++)
                if (m->items[i].collection[0] == '\0') rootCount++;
            if (rootCount > 0 && m->folderCount < 512) {
                FolderNode *fn = &m->folderNodes[m->folderCount];
                fn->path[0]     = '\0';
                snprintf(fn->name, sizeof fn->name, "Unsorted");
                fn->depth       = 0;
                fn->directCount = rootCount;
                fn->totalCount  = rootCount;
                fn->parentIdx   = -1;
                m->folderCount++;
            }
        }
    }

    m->count = n;
    UnloadDirectoryFiles(list);

    if (m->sel >= m->count) m->sel = m->count > 0 ? m->count - 1 : 0;
    menu_sort(m);   /* trie + reconstruit filtered */
}

/* Geometrie du panneau droit (cartes) et de la zone de liste. */
void menu_list_geo(int sw, int sh, int *pPX, int *pPW, int *pLY, int *pLH) {
    *pPX = (int)(sw * 0.50f);   /* panneau droit commence a 50% de la largeur */
    *pPW = sw - *pPX;
    *pLY = 72;                  /* sous la barre de recherche + onglets */
    *pLH = sh - *pLY - 24;
}

/* Scroll en pixels pour centrer la carte selectionnee dans la zone de liste.
 * Renvoie 0 si la liste tient entierement (pas de defilement). */
float menu_scrl_px(const Menu *m, int listH) {
    float sp = m->sel * (float)CARD_STEP - listH * 0.5f + CARD_H_SEL * 0.5f;
    float mx = m->filteredCount * (float)CARD_STEP - listH;
    if (sp < 0.0f) sp = 0.0f;
    if (mx > 0.0f && sp > mx) sp = mx;
    return sp;
}

/* Grade lettre selon la precision. */
static const char *grade_label(float acc) {
    if (acc >= 100.0f) return "SS";
    if (acc >= 95.0f)  return "S";
    if (acc >= 85.0f)  return "A";
    if (acc >= 70.0f)  return "B";
    if (acc >= 55.0f)  return "C";
    return "D";
}
static Color grade_color(float acc) {
    if (acc >= 95.0f)  return (Color){ 255, 215,  40, 255 };  /* or/jaune : S/SS */
    if (acc >= 85.0f)  return (Color){ 120, 220,  80, 255 };  /* vert     : A    */
    if (acc >= 70.0f)  return (Color){  80, 160, 255, 255 };  /* bleu     : B    */
    if (acc >= 55.0f)  return (Color){ 180, 180, 190, 255 };  /* gris     : C    */
    return                    (Color){ 210,  70,  70, 255 };  /* rouge    : D    */
}

/* Knob de volume : demi-cercle en anneau colle au bord gauche de l'ecran.
 * Centre dans l'espace libre entre la section personal-best (~y=250) et le
 * header MODS (sh-244, cf. mods_row_rect dans settings_ui.c).
 * Appelee depuis menu_draw ; geometrie dupliquee dans main.c pour la saisie.
 * Au repos : retracte vers la gauche (centre decale de -hideOff).
 * Au survol : sort en douceur (centre revient a 0). */
static void draw_volume_knob(int sh, Vector2 mp) {
    int mods_top = sh - 244;
    int space    = mods_top - 250;
    if (space < 60) return;

    float R_OUT = (float)(space / 2 - 8);
    if (R_OUT > 140.0f) R_OUT = 140.0f;
    if (R_OUT < 24.0f) return;
    float R_IN = R_OUT * 0.62f;
    float cy   = (float)(250 + mods_top) / 2.0f;

    float dt = GetFrameTime();
    if (dt > 0.1f) dt = 0.1f;

    /* Animation retract/reveal : 0 = cache a gauche, 1 = plein */
    static float volAnim = 0.0f;
    float hideOff = R_OUT * 0.58f;          /* combien le centre est cache */
    float cx = -hideOff * (1.0f - volAnim); /* centre x : de -hideOff a 0 */

    /* Zone de hover basee sur la partie visible actuelle */
    bool hov = (mp.x >= 0.0f && mp.x <= cx + R_OUT + 14.0f &&
                mp.y >= cy - R_OUT - 14.0f && mp.y <= cy + R_OUT + 14.0f);

    volAnim = clampf(volAnim + (hov ? 1.0f : -1.0f) * dt * (hov ? 9.0f : 5.5f),
                     0.0f, 1.0f);

    Vector2 vc = { cx, cy };
    float vol  = clampf(gMusicVolume, 0.0f, 1.0f);

    /* Halo de fond */
    DrawCircle((int)cx, (int)cy, (int)(R_OUT + 18.0f),
               (Color){ 18, 36, 80, (unsigned char)(hov ? 65 : 28) });

    /* Track (fond sombre, demi-cercle droit : 270 deg -> 450 deg) */
    DrawRing(vc, R_IN, R_OUT, 270.0f, 450.0f, 52,
             (Color){ 18, 22, 44, 255 });
    DrawRingLines(vc, R_IN, R_OUT, 270.0f, 450.0f, 52,
                  (Color){ 50, 58, 98, (unsigned char)(hov ? 230 : 170) });

    /* Fill proportionnel au volume */
    if (vol > 0.005f) {
        float fillEnd = 450.0f - 180.0f * vol;
        float t = vol;
        unsigned char fr = (unsigned char)(80  + (int)(t * 40.0f));
        unsigned char fg = (unsigned char)(140 + (int)(t * 90.0f));
        unsigned char fb = (unsigned char)(220 + (int)(t * 35.0f));
        DrawRing(vc, R_IN + 2.0f, R_OUT - 2.0f, fillEnd, 450.0f, 52,
                 (Color){ fr, fg, fb, (unsigned char)(hov ? 240 : 205) });
    }

    /* Ticks a 25%, 50%, 75% */
    for (int ti = 1; ti <= 3; ti++) {
        float ta  = (270.0f + 45.0f * (float)ti) * DEG2RAD;
        float tcx0 = cx + cosf(ta) * (R_IN + 2.0f);
        float tcy0 = cy + sinf(ta) * (R_IN + 2.0f);
        float tcx1 = cx + cosf(ta) * (R_OUT - 2.0f);
        float tcy1 = cy + sinf(ta) * (R_OUT - 2.0f);
        DrawLineEx((Vector2){ tcx0, tcy0 }, (Vector2){ tcx1, tcy1 },
                   2.0f, (Color){ 55, 65, 105, 200 });
    }

    /* Handle dot a la position courante */
    {
        float ha  = (450.0f - 180.0f * vol) * DEG2RAD;
        float mid = (R_IN + R_OUT) * 0.5f;
        float hx  = cx + cosf(ha) * mid;
        float hy  = cy + sinf(ha) * mid;
        float hr  = hov ? 14.0f : 11.0f;
        DrawCircleV((Vector2){ hx, hy }, hr + 5.0f,
                    (Color){ 20, 40, 90, 150 });
        DrawCircleV((Vector2){ hx, hy }, hr,
                    hov ? RAYWHITE : (Color){ 200, 225, 255, 255 });
    }

    /* Texte dans la zone interieure — apparait avec l'animation */
    {
        int pct = (int)(vol * 100.0f + 0.5f);
        const char *pctStr = TextFormat("%d%%", pct);
        int pw    = MeasureText(pctStr, 24);
        int inner = (int)(cx + R_IN);
        int txp   = (inner - pw) / 2;
        if (txp < 2) txp = 2;
        unsigned char ta = (unsigned char)(volAnim * volAnim * (hov ? 220.0f : 175.0f));
        DrawText("VOL", (int)(cx + 4.0f), (int)(cy - 30.0f), 20,
                 (Color){ 80, 100, 165, ta });
        DrawText(pctStr, txp, (int)(cy - 2.0f), 24,
                 hov ? (Color){ 255, 255, 255, ta }
                     : (Color){ 185, 215, 255, ta });
    }
}

void menu_draw(Menu *m, int sw, int sh) {
    static int wJouer = 0, wAucunRes = 0, wGradeMax = 0;
    if (!wJouer) {
        wJouer    = MeasureText("PLAY  ( Enter )", 20);
        wAucunRes = MeasureText("No results.", 18);
        wGradeMax = MeasureText("SS", 68);
    }
    int panelX, panelW, listY, listH;
    menu_list_geo(sw, sh, &panelX, &panelW, &listY, &listH);
    float scrollPx = m->scrollAnim;
    Vector2 mp = GetMousePosition();
    bool inFolderTree = (m->viewTab == 1 && m->collSel < 0);

    /* ================================================================= */
    /*  Fond general                                                     */
    /* ================================================================= */
    ClearBackground((Color){ 10, 10, 16, 255 });

    /* Teinte de la map ou du dossier selectionne sur le fond gauche */
    if (inFolderTree && m->folderCount > 0) {
        DrawRectangleGradientH(0, 0, panelX + 60, sh,
            (Color){ 12, 12, 20, 255 }, (Color){ 14, 18, 40, 255 });
    } else if (!inFolderTree && m->filteredCount > 0) {
        int idx = m->filtered ? m->filtered[m->sel] : m->sel;
        Color dc = diff_color(m->items[idx].info.difficulty);
        DrawRectangleGradientH(0, 0, panelX + 60, sh,
            (Color){ 12, 12, 20, 255 },
            (Color){ (unsigned char)(dc.r / 9), (unsigned char)(dc.g / 9), (unsigned char)(dc.b / 9), 255 });

        /* Cover image en fond du panneau gauche (scissoree, + overlay sombre pour la lisibilite) */
        if (gHaveCover && gCoverTex.id > 0) {
            float scaleX = (float)panelX / (float)gCoverTex.width;
            float scaleY = (float)sh     / (float)gCoverTex.height;
            float scale  = scaleX > scaleY ? scaleX : scaleY;
            int dw = (int)((float)gCoverTex.width  * scale);
            int dh = (int)((float)gCoverTex.height * scale);
            int dx = (panelX - dw) / 2;
            int dy = (sh     - dh) / 2;
            BeginScissorMode(0, 0, panelX, sh);
            DrawTexturePro(gCoverTex,
                (Rectangle){ 0, 0, (float)gCoverTex.width, (float)gCoverTex.height },
                (Rectangle){ (float)dx, (float)dy, (float)dw, (float)dh },
                (Vector2){ 0, 0 }, 0.0f, (Color){ 255, 255, 255, 110 });
            EndScissorMode();
            DrawRectangle(0, 0, panelX, sh, (Color){ 0, 0, 0, 158 });
        }
    }

    /* ================================================================= */
    /*  Panneau gauche : stats du dossier ou details de la map           */
    /* ================================================================= */
    if (inFolderTree && m->folderCount > 0 && m->sel >= 0 && m->sel < m->folderCount) {
        /* Vue arbre de dossiers : statistiques du dossier selectionne */
        FolderNode *fn = &m->folderNodes[m->sel];
        int tx = 38, ty = 38;

        /* Nom du dossier (taille adaptative) */
        int fs = 30;
        while (fs > 14 && MeasureText(fn->name, fs) > panelX - tx - 24) fs -= 2;
        DrawText(fn->name, tx, ty, fs, RAYWHITE);
        ty += fs + 8;

        /* Chemin relatif complet */
        if (fn->path[0]) {
            DrawText(fn->path, tx, ty, 13, (Color){ 110, 130, 175, 220 });
            ty += 20;
        }

        /* Separateur */
        DrawRectangleGradientH(tx, ty, panelX - tx - 30, 1,
                               (Color){ 70, 80, 110, 255 }, (Color){ 0, 0, 0, 0 });
        ty += 12;

        /* Totaux */
        DrawText(TextFormat("Total     %d map%s", fn->totalCount,
                            fn->totalCount != 1 ? "s" : ""),
                 tx, ty, 16, (Color){ 185, 200, 220, 255 }); ty += 24;
        if (fn->path[0] && fn->directCount != fn->totalCount) {
            DrawText(TextFormat("Direct    %d map%s", fn->directCount,
                                fn->directCount != 1 ? "s" : ""),
                     tx, ty, 16, (Color){ 150, 165, 195, 255 }); ty += 24;
        }

        /* Separateur */
        DrawRectangleGradientH(tx, ty, panelX - tx - 30, 1,
                               (Color){ 70, 80, 110, 255 }, (Color){ 0, 0, 0, 0 });
        ty += 12;
        DrawText("DIFFICULTIES", tx, ty, 12, (Color){ 115, 120, 148, 255 }); ty += 16;

        /* Calcul par difficulte + best scores sur les maps du dossier */
        int diffCounts[6] = {0};
        int bestsCount = 0;
        int fn_plen = fn->path[0] ? (int)strlen(fn->path) : 0;
        for (int i = 0; i < m->count; i++) {
            const char *c = m->items[i].collection;
            bool inF = fn->path[0] == '\0'
                ? (c[0] == '\0')
                : (strcmp(c, fn->path) == 0 ||
                   (fn_plen > 0 &&
                    strncmp(c, fn->path, (size_t)fn_plen) == 0 && c[fn_plen] == '/'));
            if (!inF) continue;
            int d = (int)m->items[i].info.difficulty;
            if (d >= 0 && d < 6) diffCounts[d]++; else diffCounts[0]++;
            if (profile_get_best(GetFileName(m->items[i].path))) bestsCount++;
        }

        static const char *dnames[6] = { "N/A", "Easy", "Medium", "Hard", "Logic", "Tasukete" };
        int maxD = 1;
        for (int d = 0; d < 6; d++) if (diffCounts[d] > maxD) maxD = diffCounts[d];
        int barAvailW = panelX - tx - 210;
        if (barAvailW < 20) barAvailW = 20;

        for (int d = 0; d < 6; d++) {
            if (diffCounts[d] == 0) continue;
            Color dc2 = diff_color((uint8_t)d);
            for (int s = 0; s < 5; s++) {
                Color sc = (s < d) ? dc2 : (Color){ 36, 36, 56, 255 };
                DrawCircle(tx + s * 12 + 6, ty + 8, 4, sc);
            }
            DrawText(dnames[d], tx + 68, ty, 15, dc2);
            DrawText(TextFormat("%d", diffCounts[d]), tx + 155, ty, 15,
                     (Color){ 185, 200, 220, 255 });
            int bw = (barAvailW * diffCounts[d]) / maxD;
            if (bw < 2) bw = 2;
            DrawRectangle(tx + 200, ty + 3, bw, 11,
                          (Color){ dc2.r/4, dc2.g/4, dc2.b/4, 200 });
            DrawRectangle(tx + 200, ty + 3, bw < 5 ? bw : 5, 11, dc2);
            ty += 22;
        }

        /* Separateur */
        ty += 4;
        DrawRectangleGradientH(tx, ty, panelX - tx - 30, 1,
                               (Color){ 70, 80, 110, 255 }, (Color){ 0, 0, 0, 0 });
        ty += 14;

        /* Personal bests */
        DrawText("PERSONAL BESTS", tx, ty, 12, (Color){ 115, 120, 148, 255 }); ty += 18;
        Color compCol = bestsCount > 0
            ? (Color){ 100, 220, 130, 255 }
            : (Color){ 90, 90, 115, 255 };
        DrawText(TextFormat("%d / %d  maps completed", bestsCount, fn->totalCount),
                 tx, ty, 16, compCol);

    } else if (!inFolderTree && m->filteredCount > 0) {
        int idx = m->filtered ? m->filtered[m->sel] : m->sel;
        MenuEntry *e = &m->items[idx];
        Color dc = diff_color(e->info.difficulty);
        const char *fname = GetFileName(e->path);

        int tx = 38, ty = 38;

        /* Titre (taille adaptative) */
        int fs = 32;
        while (fs > 16 && MeasureText(e->info.songName, fs) > panelX - tx - 20) fs -= 2;
        DrawText(e->info.songName, tx, ty, fs, RAYWHITE);
        ty += fs + 8;

        /* Mapped by */
        DrawText(TextFormat("Mapped by  %s", e->info.mapper[0] ? e->info.mapper : "?"),
                 tx, ty, 16, (Color){ 160, 170, 195, 255 });
        ty += 26;

        /* Separateur */
        DrawRectangleGradientH(tx, ty, panelX - tx - 30, 1,
                               (Color){ 70, 80, 110, 255 }, (Color){ 0, 0, 0, 0 });
        ty += 12;

        /* Stats */
        int mins = (int)(e->info.lastMs / 60000), secs = (int)(e->info.lastMs / 1000) % 60;
        DrawText(TextFormat("Length     %d:%02d", mins, secs),        tx, ty, 16, (Color){ 185, 200, 220, 255 }); ty += 24;
        DrawText(TextFormat("Notes      %u",      e->info.noteCount), tx, ty, 16, (Color){ 185, 200, 220, 255 }); ty += 24;

        /* Difficulte + points */
        const char *dname = sspm_difficulty_name(e->info.difficulty);
        DrawText("Difficulty", tx, ty, 16, (Color){ 185, 200, 220, 255 });
        DrawText(dname, tx + 100, ty, 16, dc);
        int nst = (int)e->info.difficulty;
        for (int s = 0; s < 5; s++) {
            Color sc = (s < nst) ? dc : (Color){ 38, 38, 58, 255 };
            DrawCircle(tx + 100 + MeasureText(dname, 16) + 14 + s * 14, ty + 8, 5, sc);
        }
        ty += 30;

        /* Star Rate (si renseigne dans le nom de fichier) */
        if (e->starRate >= 0.0f) {
            DrawText("Star Rate", tx, ty, 16, (Color){ 185, 200, 220, 255 });
            DrawText(TextFormat("%.2f *", e->starRate), tx + 100, ty, 16, (Color){ 255, 200, 80, 255 });
            ty += 24;
        }

        /* Separateur */
        DrawRectangleGradientH(tx, ty, panelX - tx - 30, 1,
                               (Color){ 70, 80, 110, 255 }, (Color){ 0, 0, 0, 0 });
        ty += 14;

        /* Personal Best */
        DrawText("PERSONAL BEST", tx, ty, 12, (Color){ 115, 120, 148, 255 });
        ty += 18;

        BestScore *bs = profile_get_best(fname);
        if (bs && bs->total > 0) {
            float bAcc = 100.0f * bs->hits / (float)bs->total;
            const char *gr = grade_label(bAcc);
            Color gc = grade_color(bAcc);
            /* Grade lettre (grand) */
            DrawText(gr, tx, ty, 68, gc);
            int gw = wGradeMax + 14;
            /* Score */
            DrawText(TextFormat("%d", bs->score), tx + gw, ty + 2, 28, RAYWHITE);
            /* Details */
            DrawText(TextFormat("%.2f%%",          bAcc),        tx + gw, ty + 36, 17, (Color){ 150, 205, 255, 255 });
            DrawText(TextFormat("x%d  combo",      bs->maxCombo),tx + gw, ty + 58, 16, (Color){ 255, 210,  90, 255 });
            DrawText(TextFormat("%d / %d notes",   bs->hits, bs->total), tx + gw, ty + 78, 15, (Color){ 170, 185, 210, 255 });
        } else {
            DrawText("No personal record set", tx, ty + 8, 17, (Color){ 90, 90, 115, 255 });
        }

        /* Chips de mode : Normal / Zen / Speed Ladder */
        {
            static const char *mnames[3] = { "Normal", "Zen", "Speed Ladder" };
            static const Color mcols[3]  = {
                { 100, 180, 255, 255 }, { 130, 220, 200, 255 }, { 255, 170, 90, 255 }
            };
            for (int mi = 0; mi < 3; mi++) {
                Rectangle cr = menu_mode_chip_rect(panelX, sh, mi);
                bool on = (gMenuMode == mi), ov = CheckCollisionPointRec(mp, cr);
                Color col = mcols[mi];
                DrawRectangleRounded(cr, 0.3f, 4,
                    on ? (Color){ col.r/5, col.g/5, col.b/5, 255 } : (Color){ 20, 22, 32, 255 });
                DrawRectangleLinesEx(cr, on ? 1.5f : 1.0f,
                    on  ? col
                    : ov ? (Color){ 80, 85, 110, 255 } : (Color){ 45, 48, 68, 255 });
                int tw = MeasureText(mnames[mi], 13);
                DrawText(mnames[mi],
                         (int)(cr.x + cr.width * 0.5f - tw * 0.5f), (int)(cr.y + 4),
                         13, on ? RAYWHITE : (Color){ 145, 150, 174, 255 });
            }
        }

        /* Bouton JOUER */
        Rectangle btnR = menu_play_btn_rect(panelX, sh);
        bool btnOv = CheckCollisionPointRec(mp, btnR);
        DrawRectangleRounded(btnR, 0.22f, 6,
            btnOv ? dc : (Color){ (unsigned char)(dc.r/2+8), (unsigned char)(dc.g/2+8), (unsigned char)(dc.b/2+8), 210 });
        DrawRectangleLinesEx(btnR, 1.5f, (Color){ dc.r, dc.g, dc.b, 140 });
        DrawText("PLAY  ( Enter )", (int)(btnR.x + btnR.width * 0.5f - wJouer * 0.5f),
                 (int)(btnR.y + btnR.height * 0.5f - 10), 20, RAYWHITE);

        /* Bloc Mods (cases a cocher + Vitesse), au-dessus du bouton JOUER */
        mods_draw_inline(panelX, sh, mp);

        /* Favori : etoile dans le coin superieur droit du panneau gauche */
        bool isFav = profile_is_fav(fname);
        Color favCol = isFav ? (Color){ 255, 200, 40, 255 } : (Color){ 60, 65, 90, 255 };
        DrawCircle(panelX - 28, 36, 10, favCol);
        DrawText("F", panelX - 32, 29, 14, isFav ? (Color){ 30, 20, 10, 255 } : (Color){ 130, 130, 160, 255 });
        /* Bannie : croix rouge */
        bool isBanned = profile_is_blacklisted(fname);
        Color banCol = isBanned ? (Color){ 220, 50, 50, 255 } : (Color){ 60, 65, 90, 255 };
        DrawCircle(panelX - 58, 36, 10, banCol);
        DrawText("X", panelX - 63, 29, 14, isBanned ? (Color){ 255, 220, 220, 255 } : (Color){ 130, 130, 160, 255 });

    } else if (!inFolderTree) {
        /* Aucune map */
        const char *msg = m->count == 0 ? "No maps found." : "No results.";
        DrawText(msg, panelX / 2 - MeasureText(msg, 22) / 2, sh / 2 - 20, 22, (Color){ 180, 180, 200, 255 });
        if (m->count == 0) {
            const char *msg2 = TextFormat("Put your .sspm files in:  %s", m->dir);
            DrawText(msg2, panelX / 2 - MeasureText(msg2, 15) / 2, sh / 2 + 14, 15, (Color){ 120, 140, 190, 255 });
        }
    }

    /* Knob de volume (bord gauche, toujours visible) */
    draw_volume_knob(sh, mp);

    /* ================================================================= */
    /*  Panneau droit : cartes de maps (style osu!)                      */
    /* ================================================================= */

    /* Fond du panneau droit (plus sombre) */
    DrawRectangle(panelX, 0, panelW, sh, (Color){ 8, 8, 14, 200 });

    /* Barre de recherche */
    {
        int sx = panelX + 4, sw2 = panelW - 438, sry = 4, srh = 28;
        bool fa = m->filter[0] != '\0';
        bool focused = m->filterFocused;
        DrawRectangle(sx, sry, sw2, srh, (Color){ 18, 18, 30, 255 });
        Color borderCol = focused   ? (Color){ 100, 220, 120, 255 } :
                          fa        ? (Color){ 80,  130, 220, 255 } :
                                      (Color){ 44,   44,  70, 255 };
        DrawRectangleLinesEx((Rectangle){ (float)sx, (float)sry, (float)sw2, (float)srh }, focused ? 1.5f : 1.0f, borderCol);
        if (fa || focused) {
            int cur = m->filterCursor;
            int len = (int)strlen(m->filter);
            if (cur > len) cur = len;
            char before[128];
            strncpy(before, m->filter, (size_t)cur); before[cur] = '\0';
            int cx = sx + 8 + MeasureText(before, 15);
            DrawText(before, sx + 8, sry + 7, 15, RAYWHITE);
            DrawText(m->filter + cur, cx, sry + 7, 15, RAYWHITE);
            /* Curseur clignotant quand focus, statique sinon */
            if (focused && (int)(GetTime() * 2) % 2 == 0)
                DrawLine(cx, sry + 5, cx, sry + srh - 5, RAYWHITE);
        } else {
            DrawText("Search...", sx + 8, sry + 7, 15, (Color){ 55, 55, 82, 255 });
        }
    }

    /* Bouton Options */
    {
        Rectangle ob = opt_btn_rect(sw);
        bool ov = CheckCollisionPointRec(mp, ob);
        DrawRectangleRec(ob, ov ? (Color){ 50, 90, 160, 255 } : (Color){ 22, 22, 38, 255 });
        DrawRectangleLinesEx(ob, 1.0f, (Color){ 60, 65, 105, 255 });
        DrawText("Options (S)", (int)(ob.x + 10), (int)(ob.y + 7), 14, RAYWHITE);
    }
    /* Bouton Modes de jeu */
    {
        Rectangle gb = modes_btn_rect(sw);
        bool ov = CheckCollisionPointRec(mp, gb);
        DrawRectangleRec(gb, ov ? (Color){ 90, 70, 150, 255 } : (Color){ 28, 22, 42, 255 });
        DrawRectangleLinesEx(gb, 1.0f, (Color){ 90, 70, 130, 255 });
        DrawText("Aim Trainer (G)", (int)(gb.x + 10), (int)(gb.y + 7), 14, RAYWHITE);
    }
    /* Bouton Profile */
    {
        Rectangle pb = profile_btn_rect(sw);
        bool ov = CheckCollisionPointRec(mp, pb);
        DrawRectangleRec(pb, ov ? (Color){ 40, 80, 110, 255 } : (Color){ 18, 26, 40, 255 });
        DrawRectangleLinesEx(pb, 1.0f, (Color){ 50, 80, 120, 255 });
        DrawText("Profile (P)", (int)(pb.x + 10), (int)(pb.y + 7), 14, RAYWHITE);
    }

    /* Onglets Toutes / Dossiers */
    {
        static const char *tabNames[2] = { "All", "Folders" };
        int tabW = 120, tabH = 22, tabY0 = 34, tabGap = 3;
        for (int t = 0; t < 2; t++) {
            Rectangle tr = { (float)(panelX + 4 + t * (tabW + tabGap)), (float)tabY0,
                             (float)tabW, (float)tabH };
            bool on = (m->viewTab == t);
            bool ov = CheckCollisionPointRec(mp, tr);
            DrawRectangleRec(tr, on ? (Color){ 40, 50, 80, 255 } : (Color){ 18, 18, 28, 255 });
            DrawRectangleLinesEx(tr, on ? 1.5f : 1.0f,
                on ? (Color){ 80, 130, 220, 255 } :
                ov ? (Color){ 55,  60,  95, 255 } : (Color){ 35, 38, 62, 255 });
            int tw = MeasureText(tabNames[t], 13);
            DrawText(tabNames[t],
                     (int)(tr.x + tr.width * 0.5f - tw * 0.5f), (int)(tr.y + 5), 13,
                     on ? RAYWHITE : (Color){ 130, 135, 170, 255 });
        }
    }
    /* Fil d'Ariane (dossier ouvert) ou indicateurs de filtres actifs */
    if (m->viewTab == 1 && m->collSel >= 0) {
        FolderNode *sfn = &m->folderNodes[m->collSel];
        const char *crumb = sfn->path[0] ? sfn->path : "Unsorted";
        DrawText(TextFormat("Folders > %s", crumb),
                 panelX + 8, 59, 11, (Color){ 100, 120, 180, 200 });
    } else if (m->diffFilter >= 0 || m->favsOnly || m->newOnly || !m->hideBlacklisted) {
        char fhint[100] = ""; int fho = 0;
        if (m->favsOnly)           fho += snprintf(fhint+fho, (int)sizeof fhint-fho, "[FAV] ");
        if (m->newOnly)            fho += snprintf(fhint+fho, (int)sizeof fhint-fho, "[NEW] ");
        if (!m->hideBlacklisted)   fho += snprintf(fhint+fho, (int)sizeof fhint-fho, "[SHOW BANNED] ");
        if (m->diffFilter >= 0)    snprintf(fhint+fho, (int)sizeof fhint-fho, "[%s]",
                                            sspm_difficulty_name((uint8_t)m->diffFilter));
        DrawText(fhint, panelX + 8, 59, 12, (Color){ 255, 200, 55, 255 });
    }

    /* Liste (clippee dans la zone de liste, hors colonne de scroll) */
    BeginScissorMode(panelX, listY, panelW - SCROLL_BTN_W, listH);

    if (m->viewTab == 1 && m->collSel < 0) {
        /* === Vue arbre de dossiers === */
        if (m->folderCount == 0) {
            const char *msg = "No folders.";
            DrawText(msg, panelX + panelW/2 - MeasureText(msg, 18)/2, listY + listH/2 - 9, 18,
                     (Color){ 160, 160, 180, 255 });
            const char *msg2 = "Put .sspm files in subfolders of maps/";
            DrawText(msg2, panelX + panelW/2 - MeasureText(msg2, 13)/2, listY + listH/2 + 16, 13,
                     (Color){ 100, 115, 150, 255 });
        } else {
            /* Couleurs d'accent par niveau de profondeur */
            static const Color depthCols[6] = {
                {  80, 130, 220, 255 }, {  70, 160, 140, 255 },
                { 140, 100, 210, 255 }, { 200, 120,  60, 255 },
                {  80, 180, 100, 255 }, { 160,  80, 130, 255 }
            };
            int firstVis = (int)((scrollPx - CARD_H_SEL) / CARD_STEP) - 1;
            int lastVis  = (int)((scrollPx + listH + CARD_H_SEL) / CARD_STEP) + 1;
            if (firstVis < 0) firstVis = 0;
            if (lastVis >= m->folderCount) lastVis = m->folderCount - 1;
            for (int ci = firstVis; ci <= lastVis; ci++) {
                FolderNode *fn = &m->folderNodes[ci];
                bool sel      = (ci == m->sel);
                int cardH     = sel ? CARD_H_SEL : CARD_H_NORM;
                int cardRight = sw - SCROLL_BTN_W - 4;
                int indent    = fn->depth * 18;
                int cardLeft  = sel ? (panelX - 8) : (panelX + CARD_INDENT + indent);
                int cardW     = cardRight - cardLeft;
                int cardY     = listY + (int)((float)(ci * CARD_STEP) - scrollPx);
                int di        = fn->depth < 6 ? fn->depth : 5;
                Color acFull  = depthCols[di];
                Color acDim   = (Color){ acFull.r/2, acFull.g/2, acFull.b/2, 255 };
                Color accent  = sel ? acFull : acDim;

                DrawRectangleGradientH(cardLeft, cardY, cardW, cardH,
                    sel ? (Color){ 28, 38, 68, 255 } : (Color){ 16, 16, 26, 248 },
                    sel ? (Color){ 45, 55, 95, 255 } : (Color){ 20, 22, 36, 230 });
                DrawRectangle(cardLeft, cardY, sel ? 6 : 4, cardH, accent);

                /* Connecteur arborescent : L-shape vers le niveau parent */
                if (fn->depth > 0) {
                    int lx   = panelX + CARD_INDENT + (fn->depth - 1) * 18 + 7;
                    int midY = cardY + cardH / 2;
                    DrawLine(lx, cardY, lx, midY, (Color){ 55, 65, 100, 160 });
                    DrawLine(lx, midY, cardLeft, midY, (Color){ 55, 65, 100, 160 });
                }

                int txtX = cardLeft + (sel ? 16 : 12);
                DrawText(fn->name, txtX, cardY + (sel ? 7 : 6), sel ? 17 : 16,
                         sel ? RAYWHITE : (Color){ 210, 210, 228, 255 });
                int nc = fn->totalCount;
                DrawText(TextFormat("%d map%s", nc, nc > 1 ? "s" : ""),
                         txtX, cardY + (sel ? 34 : 27), 12,
                         sel ? (Color){ 150, 175, 255, 190 } : (Color){ 80, 85, 118, 200 });
                /* Fleche droite */
                int ax = cardRight - 24, ay = cardY + cardH / 2;
                DrawTriangle((Vector2){ (float)(ax + 8), (float)ay },
                             (Vector2){ (float)ax, (float)(ay - 7) },
                             (Vector2){ (float)ax, (float)(ay + 7) },
                             sel ? RAYWHITE : (Color){ 100, 110, 150, 200 });
                DrawRectangle(cardLeft, cardY + cardH, cardW, CARD_GAP, (Color){ 6, 6, 10, 255 });
            }
        }
    } else {
    /* === Vue liste de maps (toutes ou filtrées par collection) === */
    if (m->filteredCount == 0 && m->count > 0) {
        DrawText("No results.", panelX + panelW/2 - wAucunRes/2,
                 listY + listH/2 - 9, 18, (Color){ 160, 160, 180, 255 });
    } else {
        /* Plage de cartes visibles */
        int firstVis = (int)((scrollPx - CARD_H_SEL) / CARD_STEP) - 1;
        int lastVis  = (int)((scrollPx + listH + CARD_H_SEL) / CARD_STEP) + 1;
        if (firstVis < 0) firstVis = 0;
        if (lastVis >= m->filteredCount) lastVis = m->filteredCount - 1;

        for (int fi = firstVis; fi <= lastVis; fi++) {
            int idx = m->filtered ? m->filtered[fi] : fi;
            bool sel = (fi == m->sel);
            MenuEntry *e = &m->items[idx];
            Color dc = diff_color(e->info.difficulty);

            int cardH = sel ? CARD_H_SEL : CARD_H_NORM;
            /* Toutes les cartes ont le meme bord DROIT ; la selectionnee depasse plus a gauche */
            int cardRight = sw - SCROLL_BTN_W - 4;
            int cardLeft  = sel ? (panelX -  8) : (panelX + CARD_INDENT);
            int cardW     = cardRight - cardLeft;
            int cardY     = listY + (int)((float)(fi * CARD_STEP) - scrollPx);

            /* Fond gradient (gauche=diff teintÃ©, droite=plus saturÃ©) */
            Color bgL = sel
                ? (Color){ (unsigned char)(dc.r/4+8), (unsigned char)(dc.g/4+8), (unsigned char)(dc.b/4+8), 255 }
                : (Color){ 16, 16, 26, 248 };
            Color bgR = sel
                ? (Color){ (unsigned char)(dc.r/2), (unsigned char)(dc.g/2), (unsigned char)(dc.b/2), 255 }
                : (Color){ (unsigned char)(dc.r/7), (unsigned char)(dc.g/7), (unsigned char)(dc.b/7), 230 };
            DrawRectangleGradientH(cardLeft, cardY, cardW, cardH, bgL, bgR);

            /* Cover en fond de la carte selectionnee : alignee a droite, hauteur = cardH */
            if (sel && gHaveCover && gCoverTex.id > 0 && gCoverTex.height > 0) {
                int pw = (int)((float)cardH / (float)gCoverTex.height * (float)gCoverTex.width);
                int cx = cardRight - pw;
                if (cx < cardLeft) { pw = cardW; cx = cardLeft; }
                DrawTexturePro(gCoverTex,
                    (Rectangle){ 0, 0, (float)gCoverTex.width, (float)gCoverTex.height },
                    (Rectangle){ (float)cx, (float)cardY, (float)pw, (float)cardH },
                    (Vector2){ 0, 0 }, 0.0f, (Color){ 255, 255, 255, 45 });
            }

            /* Bande de couleur de difficulte (bord gauche) */
            DrawRectangle(cardLeft, cardY, sel ? 6 : 4, cardH, dc);

            /* Titre */
            int txtX = cardLeft + (sel ? 16 : 12);
            DrawText(e->info.songName, txtX, cardY + (sel ? 7 : 6), sel ? 18 : 17,
                     sel ? RAYWHITE : (Color){ 210, 210, 228, 255 });

            /* Sous-ligne : mapper | difficulte | star rate (optionnel) | notes | duree */
            const char *favFile = GetFileName(e->path);
            const char *sub = (e->starRate >= 0.0f)
                ? TextFormat("by %s  |  %s  |  %.2f*  |  %u notes  |  %.0fs",
                              e->info.mapper[0] ? e->info.mapper : "?",
                              sspm_difficulty_name(e->info.difficulty),
                              e->starRate, e->info.noteCount, e->info.lastMs / 1000.0f)
                : TextFormat("by %s  |  %s  |  %u notes  |  %.0fs",
                              e->info.mapper[0] ? e->info.mapper : "?",
                              sspm_difficulty_name(e->info.difficulty),
                              e->info.noteCount, e->info.lastMs / 1000.0f);
            DrawText(sub, txtX, cardY + (sel ? 34 : 27), 12,
                     sel ? (Color){ 180, 200, 255, 195 } : (Color){ 88, 88, 115, 200 });

            /* Meilleur score (seulement sur la carte selectionnee) */
            if (sel) {
                BestScore *bs = profile_get_best(favFile);
                if (bs && bs->total > 0) {
                    float bAcc = 100.0f * bs->hits / (float)bs->total;
                    DrawText(TextFormat("Best  %d pts   %.1f%%   x%d",
                                        bs->score, bAcc, bs->maxCombo),
                             txtX, cardY + 54, 12, (Color){ 255, 200, 50, 225 });
                }
            }

            /* Points de difficulte (bord droit) */
            int nst = (int)e->info.difficulty;
            for (int s = 0; s < 5; s++) {
                Color sc = (s < nst) ? dc : (Color){ 36, 36, 56, 255 };
                DrawCircle(cardRight - 14 - (4 - s) * 13, cardY + cardH / 2, sel ? 5 : 4, sc);
            }

            /* Disque favori (coin inferieur droit de la carte) */
            if (profile_is_fav(favFile))
                DrawCircle(cardRight - 10, cardY + cardH - 9, 5, (Color){ 255, 200, 38, 215 });
            /* Disque bannie (a gauche du disque favori) */
            if (profile_is_blacklisted(favFile))
                DrawCircle(cardRight - 24, cardY + cardH - 9, 5, (Color){ 210, 50, 50, 200 });

            /* Separateur sous la carte */
            DrawRectangle(cardLeft, cardY + cardH, cardW, CARD_GAP, (Color){ 6, 6, 10, 255 });
        }
    }
    } /* fin else vue maps */
    EndScissorMode();

    /* Boutons de scroll haut / bas (colonne droite de la liste) */
    {
        Rectangle ru = menu_scroll_up_rect(sw, sh);
        Rectangle rd = menu_scroll_down_rect(sw, sh);
        bool hovU = CheckCollisionPointRec(mp, ru);
        bool hovD = CheckCollisionPointRec(mp, rd);
        DrawRectangleRec(ru, hovU ? (Color){ 45, 55, 80, 255 } : (Color){ 14, 16, 24, 255 });
        DrawRectangleLinesEx(ru, 1.0f, (Color){ 44, 48, 72, 255 });
        DrawRectangleRec(rd, hovD ? (Color){ 45, 55, 80, 255 } : (Color){ 14, 16, 24, 255 });
        DrawRectangleLinesEx(rd, 1.0f, (Color){ 44, 48, 72, 255 });
        /* fleche haut */
        float cxU = ru.x + ru.width * 0.5f, cyU = ru.y + ru.height * 0.5f;
        Color aU = hovU ? RAYWHITE : (Color){ 150, 165, 200, 255 };
        DrawTriangle((Vector2){ cxU, cyU - 11 },
                     (Vector2){ cxU - 11, cyU + 7 },
                     (Vector2){ cxU + 11, cyU + 7 }, aU);
        /* fleche bas */
        float cxD = rd.x + rd.width * 0.5f, cyD = rd.y + rd.height * 0.5f;
        Color aD = hovD ? RAYWHITE : (Color){ 150, 165, 200, 255 };
        DrawTriangle((Vector2){ cxD, cyD + 11 },
                     (Vector2){ cxD + 11, cyD - 7 },
                     (Vector2){ cxD - 11, cyD - 7 }, aD);
    }

    /* Compteur + tri (en bas du panneau droit) */
    {
        static const char *sortNames[5] = { "Name", "Difficulty", "Length", "Notes", "Star Rate" };
        const char *cnt;
        if (m->viewTab == 1 && m->collSel < 0)
            cnt = TextFormat("%d folder%s  |  %d map%s",
                             m->folderCount, m->folderCount > 1 ? "s" : "",
                             m->count, m->count > 1 ? "s" : "");
        else
            cnt = TextFormat("[%s]  %d / %d", sortNames[m->sortMode],
                             m->filteredCount, m->count);
        DrawText(cnt, sw - 12 - MeasureText(cnt, 12), sh - 18, 12, (Color){ 80, 90, 125, 255 });
    }

    /* Hints clavier (bas gauche) */
    if (m->viewTab == 1 && m->collSel >= 0)
        DrawText("F: fav  |  X: ban  |  V: favorites  |  H: show banned  |  D: difficulty  |  R: star rate  |  Tab: sort  |  F5: refresh  |  S: options  |  Esc: folders",
                 38, sh - 18, 11, (Color){ 68, 70, 100, 255 });
    else if (m->viewTab == 1)
        DrawText("Up/Down: navigate  |  Enter: open  |  F5: refresh  |  S: options  |  Esc: quit",
                 38, sh - 18, 11, (Color){ 68, 70, 100, 255 });
    else
        DrawText("F: fav  |  X: ban  |  V: favorites  |  H: show banned  |  N: never played  |  D: difficulty  |  R: star rate  |  Tab: sort  |  F5: refresh  |  S: options  |  Esc: quit",
                 38, sh - 18, 11, (Color){ 68, 70, 100, 255 });
}

/* ===================================================================== */
/*  Ecran Modes : cartes Zen / Speed Ladder + Aim Trainer 100% custom    */
/* ===================================================================== */
#define AIM_ROWS 9

static int modes_total(void) { return AIM_ROWS + 1; }   /* AIM_ROWS lignes + bouton Lancer */

/* Libelle d'une ligne de config de l'Aim Trainer. */
static const char *aim_row_label(int row) {
    switch (row) {
        case 0: return "Density (flow speed)";
        case 1: return "Reaction time";
        case 2: return "Pattern style";
        case 3: return "Pattern length";
        case 4: return "Field spread";
        case 5: return "Note size";
        case 6: return "Timing window";
        case 7: return "Session length";
        case 8: return "Acceleration";
        default: return "";
    }
}
/* Valeur affichee d'une ligne de config. */
static void aim_row_value(int row, char *buf, int n) {
    const AimConfig *a = &gSettings.aim;
    int st = (a->style >= 0 && a->style < N_AIM_STYLES) ? a->style : AS_MIX;
    switch (row) {
        case 0: snprintf(buf, (size_t)n, "%.1f n/s", a->density); break;
        case 1: snprintf(buf, (size_t)n, "%.0f ms", a->approachMs); break;
        case 2: snprintf(buf, (size_t)n, "%s", AIM_STYLE_NAMES[st]); break;
        case 3: snprintf(buf, (size_t)n, "%d notes", a->segLen); break;
        case 4: snprintf(buf, (size_t)n, "%.0f %%", a->radius * 100.0f); break;
        case 5: snprintf(buf, (size_t)n, "%.0f %%", a->size * 100.0f); break;
        case 6: snprintf(buf, (size_t)n, "%.0f ms", a->hitWindowMs); break;
        case 7: snprintf(buf, (size_t)n, "%d s", a->durationSec); break;
        case 8: { if (a->accelPct == 0) snprintf(buf, (size_t)n, "constant");
                  else                  snprintf(buf, (size_t)n, "+%d %%", a->accelPct); } break;
        default: buf[0] = '\0'; break;
    }
}
/* Applique un pas (-1 / +1) a une ligne de config. */
static void aim_row_step(int row, int step) {
    AimConfig *a = &gSettings.aim;
    switch (row) {
        case 0: a->density     = clampf(a->density     + step * 0.5f,  0.5f,   14.0f);  break;
        case 1: a->approachMs  = clampf(a->approachMs  + step * 20.0f, 150.0f, 1000.0f); break;
        case 2: { int t = N_AIM_STYLES; a->style = ((a->style + step) % t + t) % t; } break;
        case 3: { int v = a->segLen + step;        a->segLen = v < 2 ? 2 : (v > 24 ? 24 : v); } break;
        case 4: a->radius      = clampf(a->radius      + step * 0.05f, 0.30f,  1.0f);   break;
        case 5: a->size        = clampf(a->size        + step * 0.05f, 0.50f,  1.50f);  break;
        case 6: a->hitWindowMs = clampf(a->hitWindowMs + step * 5.0f,  40.0f,  200.0f); break;
        case 7: { int v = a->durationSec + step * 15;  a->durationSec = v < 15 ? 15 : (v > 600 ? 600 : v); } break;
        case 8: { int v = a->accelPct + step * 10;     a->accelPct = v < 0 ? 0 : (v > 100 ? 100 : v); } break;
        default: break;
    }
}

/* Rectangle d'un item : 0..AIM_ROWS-1 = lignes de config ; AIM_ROWS = bouton Lancer. */
static Rectangle modes_item_rect(int sw, int idx) {
    int x = 40, w = sw - 80;
    if (idx >= AIM_ROWS)                            /* bouton Lancer */
        return (Rectangle){ (float)x, (float)(76 + AIM_ROWS * 29 + 6), (float)w, 38.0f };
    return (Rectangle){ (float)x, (float)(76 + idx * 29), (float)w, 26.0f };
}

void modes_update(int sw, int sh) {
    (void)sh;
    gModesChoice = -1;
    int total = modes_total();
    int firstCfg = 0, lastCfg = AIM_ROWS - 1;

    if (IsKeyPressed(KEY_DOWN)) gModesSel = (gModesSel + 1) % total;
    if (IsKeyPressed(KEY_UP))   gModesSel = (gModesSel + total - 1) % total;

    int step = 0;
    if (IsKeyPressed(KEY_RIGHT)) step = 1;
    if (IsKeyPressed(KEY_LEFT))  step = -1;

    Vector2 mp = GetMousePosition();
    for (int r = 0; r < total; r++) {
        if (CheckCollisionPointRec(mp, modes_item_rect(sw, r))) {
            gModesSel = r;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (r >= firstCfg && r <= lastCfg) {
                    /* clic dans "< valeur >" : moitie gauche = baisser, droite = monter */
                    if (gAimCtrl.width > 0 && CheckCollisionPointRec(mp, gAimCtrl))
                        step = (mp.x < gAimCtrl.x + gAimCtrl.width * 0.5f) ? -1 : 1;
                    else step = 1;
                } else {
                    gModesChoice = 0;   /* bouton Lancer */
                }
            }
        }
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        if (gModesSel >= firstCfg && gModesSel <= lastCfg) step = (wheel > 0) ? 1 : -1;
        else gModesSel = (gModesSel + (wheel > 0 ? total - 1 : 1)) % total;
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        if (gModesSel >= firstCfg && gModesSel <= lastCfg) { if (step == 0) step = 1; }
        else gModesChoice = 0;
    }

    if (step != 0 && gModesSel >= firstCfg && gModesSel <= lastCfg) {
        aim_row_step(gModesSel, step);
        settings_save(&gSettings);                 /* la config perso est persistee a chaque reglage */
    }
}

void modes_draw(int sw, int sh, const Menu *m) {
    (void)m;
    ClearBackground((Color){ 12, 12, 18, 255 });
    DrawText("AIM TRAINER", 40, 18, 24, (Color){ 190, 175, 255, 255 });
    DrawText("Custom configuration", 40, 50, 14, (Color){ 130, 125, 175, 255 });

    /* --- lignes de config --- */
    Vector2 mp = GetMousePosition();
    gAimCtrl = (Rectangle){ 0, 0, 0, 0 };
    for (int i = 0; i < AIM_ROWS; i++) {
        Rectangle r = modes_item_rect(sw, i);
        bool sel = (gModesSel == i);
        if (sel) DrawRectangleRounded(r, 0.32f, 4, (Color){ 30, 34, 54, 255 });
        DrawText(aim_row_label(i), (int)r.x + 12, (int)(r.y + 5), 16,
                 sel ? RAYWHITE : (Color){ 200, 204, 222, 255 });
        char val[48]; aim_row_value(i, val, sizeof val);
        int fs = 16, vw = MeasureText(val, fs);
        int rx = (int)(r.x + r.width) - 14;            /* bord droit (apres ">") */
        int decX = rx - 12 - 10 - vw - 16;             /* x du "<" */
        Color ac = (Color){ 120, 150, 220, 255 };
        Color vc = sel ? (Color){ 200, 230, 255, 255 } : (Color){ 165, 185, 215, 255 };
        Rectangle ctrl = { (float)(decX - 4), r.y, (float)((rx + fs) - decX + 8), r.height };
        float cmid = ctrl.x + ctrl.width * 0.5f;
        bool hov = sel && CheckCollisionPointRec(mp, ctrl);
        int cyT = (int)(r.y + 4);
        DrawText("<", decX, cyT, fs, (hov && mp.x <  cmid) ? RAYWHITE : ac);
        DrawText(val, rx - 12 - 10 - vw, cyT, fs, vc);
        DrawText(">", rx - 12, cyT, fs, (hov && mp.x >= cmid) ? RAYWHITE : ac);
        if (sel) gAimCtrl = ctrl;
    }

    /* --- bouton Lancer l'Aim Trainer --- */
    {
        Rectangle r = modes_item_rect(sw, AIM_ROWS);
        bool sel = (gModesSel == AIM_ROWS);
        Color base = (Color){ 120, 90, 220, 255 };
        DrawRectangleRounded(r, 0.30f, 6, sel ? (Color){ 70, 52, 130, 255 } : (Color){ 34, 30, 54, 255 });
        DrawRectangleLinesEx(r, sel ? 2.0f : 1.0f, sel ? (Color){ 190, 170, 255, 255 } : base);
        const char *lbl = "Start Aim Trainer";
        DrawText(lbl, (int)(r.x + r.width / 2 - MeasureText(lbl, 20) / 2), (int)(r.y + r.height / 2 - 10), 20,
                 sel ? RAYWHITE : (Color){ 210, 200, 240, 255 });
    }

    /* --- ligne de detail (bas) --- */
    {
        const AimConfig *a = &gSettings.aim;
        int st = (a->style >= 0 && a->style < N_AIM_STYLES) ? a->style : AS_MIX;
        int est = (int)((float)a->durationSec * a->density);
        char hint[200];
        snprintf(hint, sizeof hint, "~%d notes in %d s  -  style %s%s",
                 est, a->durationSec, AIM_STYLE_NAMES[st],
                 a->accelPct > 0 ? "  -  acceleration on" : "");
        DrawText(hint, 40, sh - 50, 14, (Color){ 175, 180, 205, 255 });
    }
    DrawText("Up/Down: navigate  -  Left/Right: adjust  -  Enter/click: start  -  Esc: back",
             40, sh - 28, 13, (Color){ 110, 110, 130, 255 });
}

/* ===================================================================== */
/*  Scan asynchrone du dossier de maps                                   */
/* ===================================================================== */

static void menu_scan_body(MenuScanner *sc) {
    menu_scan(sc->menu);
    sc->state = ALOAD_DONE;
}

#ifdef _WIN32
static unsigned __stdcall menu_scan_win(void *arg) { menu_scan_body((MenuScanner *)arg); return 0; }
void menu_scan_start(Menu *m, MenuScanner *sc) {
    sc->menu = m;
    sc->state = ALOAD_RUNNING;
    sc->threadStarted = true;
    sc->thread = (ATHREAD_HANDLE)_beginthreadex(NULL, 0, menu_scan_win, sc, 0, NULL);
}
void menu_scan_join(MenuScanner *sc) {
    if (sc->threadStarted) {
        WaitForSingleObject(sc->thread, INFINITE);
        CloseHandle(sc->thread);
        sc->threadStarted = false;
    }
}
#else
static void *menu_scan_posix(void *arg) { menu_scan_body((MenuScanner *)arg); return NULL; }
void menu_scan_start(Menu *m, MenuScanner *sc) {
    sc->menu = m;
    sc->state = ALOAD_RUNNING;
    sc->threadStarted = true;
    pthread_create(&sc->thread, NULL, menu_scan_posix, sc);
}
void menu_scan_join(MenuScanner *sc) {
    if (sc->threadStarted) {
        pthread_join(sc->thread, NULL);
        sc->threadStarted = false;
    }
}
#endif
