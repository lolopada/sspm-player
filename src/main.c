/*
 * main.c — Lecteur jouable de cartes SSPM (Rhythia) en C + raylib.
 *
 * Ecrans (machine a etats), une seule fenetre / un seul device audio :
 *   MENU       liste les .sspm d'un dossier ; choisir une carte la lance.
 *   OPTIONS    distance/vitesse des notes, tablette graphique, palette, mesh.
 *   DECOMPTE   3 - 2 - 1 avant le depart (la musique demarre a "GO").
 *   JEU        gameplay facon Sound Space : les notes foncent vers le plan, tu
 *              vises a la souris (ou au stylet), hit si le curseur est dessus au
 *              passage du plan. Score + combo + precision, ecran de resultats.
 *
 * Modes : --autoplay (visualiseur), --info (infos sans fenetre), --fullscreen.
 * Usage : ./sspm-player [dossier|carte.sspm] [LARGEURxHAUTEUR] [--autoplay] [--fullscreen] [--info]
 *
 * Touches : MENU    -> Haut/Bas + Entree/clic jouer, S options, F5 actualiser, Echap quitter.
 *           OPTIONS -> Haut/Bas naviguer, Gauche/Droite modifier, Echap retour.
 *           JEU     -> Souris/stylet viser, ESPACE pause, R recommencer, F11 plein ecran, Echap menu.
 *
 * Concu pour une machine faible : dossier scanne une seule fois (pas par frame),
 * la liste ne lit qu'un petit en-tete par fichier ; rendu O(notes visibles)/frame,
 * pas de lumiere/ombres/postfx, vsync.
 */
#include "common.h"

/* ===================================================================== */
/*  Chargement asynchrone                                                */
/* ===================================================================== */

/* Corps du chargement (thread-safe) : sspm_load + alloc state. */
static void async_load_body(AsyncLoader *ld) {
    Play *p = ld->play;
    memset(p, 0, sizeof *p);
    if (!sspm_load(ld->path, &p->map)) { sspm_free(&p->map); ld->state = ALOAD_FAIL; return; }
    p->loaded = true;
    p->N = p->map.noteCountLoaded;
    p->state = (uint8_t *)calloc(p->N ? p->N : 1, 1);
    if (!p->state) { sspm_free(&p->map); p->loaded = false; ld->state = ALOAD_FAIL; return; }
    ld->state = ALOAD_DONE;
}
#ifdef _WIN32
static unsigned __stdcall async_load_win(void *arg) { async_load_body((AsyncLoader *)arg); return 0; }
static void athread_start(AsyncLoader *ld) { ld->thread = (ATHREAD_HANDLE)_beginthreadex(NULL,0,async_load_win,ld,0,NULL); }
static void athread_join(AsyncLoader *ld)  { WaitForSingleObject(ld->thread,INFINITE); CloseHandle(ld->thread); ld->threadStarted=false; }
#else
static void *async_load_posix(void *arg) { async_load_body((AsyncLoader *)arg); return NULL; }
static void athread_start(AsyncLoader *ld) { pthread_create(&ld->thread,NULL,async_load_posix,ld); }
static void athread_join(AsyncLoader *ld)  { pthread_join(ld->thread,NULL); ld->threadStarted=false; }
#endif

/* ===================================================================== */
/*  Transitions                                                          */
/* ===================================================================== */
static double sPlayStartTime = 0.0;  /* heure GetTime() au debut de la partie en cours */
static bool   sStatsSaved    = false; /* notes cumulees deja enregistrees pour cette partie */

static void go_menu(Play *p, AppScreen *screen) {
    /* Sauvegarde le temps de jeu de la partie qui se termine */
    if (sPlayStartTime > 0.0) {
        gProfile.totalPlayTimeSec += (float)(GetTime() - sPlayStartTime);
        sPlayStartTime = 0.0;
        profile_save();
    }
    sStatsSaved = false;
    if (gLoader.threadStarted) { athread_join(&gLoader); }  /* attend la fin du chargement */
    if (p->loaded) play_unload(p);
    gLoader.state    = ALOAD_IDLE;
    gLoadFinalized   = false;
    gMode            = MODE_NORMAL;
    settings_apply(&gSettings);   /* restaure approach/taille/mods que l'Aim Trainer a pu modifier */
    bg_pulse_reset();
    EnableCursor();
    if (gSettings.cursorInMenu) HideCursor(); else ShowCursor();
    cursor_menu_trail_reset();   /* efface la trainee menu (positions jeu incoherentes) */
    SetWindowTitle("SSPM Player");
    /* Si le scan initial n'est pas encore termine, revenir en SCR_LOADING */
    if (gMenuScanner.state == ALOAD_RUNNING) {
        *screen = SCR_LOADING;
    } else {
        menu_scan_join(&gMenuScanner);
        *screen = SCR_MENU;
    }
}

static bool go_play(Play *p, const char *path, double *cd, AppScreen *screen, bool autoplay) {
    if (gLoader.threadStarted) { athread_join(&gLoader); }  /* securite si re-entre */
    play_unload(p);
    snprintf(gCurrentMap, sizeof gCurrentMap, "%s", GetFileName(path));
    gNewRecord      = false;
    gLoadFinalized  = false;
    /* Lance le chargement en arriere-plan */
    snprintf(gLoader.path, sizeof gLoader.path, "%s", path);
    gLoader.play    = p;
    gLoader.state   = ALOAD_RUNNING;
    gLoader.threadStarted = true;
    athread_start(&gLoader);
    *cd    = COUNTDOWN_SEC;
    *screen = SCR_COUNTDOWN;
    if (!autoplay) {
        if (gTablet) { EnableCursor(); HideCursor(); }
        else DisableCursor();
    }
    return true;
}

/* Lance une map dans un mode particulier (Zen / Speed Ladder). */
static void go_mode_map(Play *p, const char *path, GameMode mode,
                        double *cd, AppScreen *screen, bool autoplay) {
    gMode = mode;
    if (mode == MODE_LADDER) { gLadderRate = 1.0f; gLadderLevel = 0; }
    if (mode == MODE_PRACTICE) {
        /* Entrainement : depart au debut, pas de boucle, vitesse normale (reglable en jeu). */
        gPracticeA = 0.0f; gPracticeB = 0.0f; gPracticeLoop = true;
        gRate = 1.0f; gScoreMult = mods_score_mult(gMods, gRate);
    }
    go_play(p, path, cd, screen, autoplay);
}

/* Lance l'Aim Trainer avec la config personnalisee courante. */
static void go_aim(Play *p, double *cd, AppScreen *screen, bool autoplay) {
    if (gLoader.threadStarted) athread_join(&gLoader);
    settings_apply(&gSettings);            /* palette, curseur, vsync... */
    aim_build(p, &gSettings.aim);          /* regle approach/size/hit window */
    gMode      = MODE_AIM;
    gMods      = 0;                        /* l'aim ignore les mods/vitesse */
    gRate      = 1.0f;
    gScoreMult = 1.0f;
    snprintf(gCurrentMap, sizeof gCurrentMap, "aim_custom");   /* clef de high-score */
    gNewRecord = false;
    play_reset(p);
    gLoadFinalized = true;
    gLoader.state  = ALOAD_IDLE;
    *cd = COUNTDOWN_SEC;
    *screen = SCR_COUNTDOWN;
    if (!autoplay) {
        if (gTablet) { EnableCursor(); HideCursor(); }
        else DisableCursor();
    }
}

/* ===================================================================== */
/*  Ecran de chargement                                                  */
/* ===================================================================== */

static void loading_draw(int sw, int sh, float t, int count, int total) {
    ClearBackground((Color){ 10, 10, 16, 255 });

    /* halo de fond centre */
    int cx = sw / 2, cy = sh / 2 - 24;
    DrawCircleGradient((Vector2){ (float)cx, (float)cy }, 200.0f, (Color){ 16, 22, 52, 80 }, (Color){ 10, 10, 16, 0 });

    /* titre */
    const char *title = "SSPM PLAYER";
    int tw = MeasureText(title, 40);
    DrawText(title, cx - tw / 2, 52, 40, (Color){ 200, 215, 255, 255 });

    /* separateur sous le titre */
    DrawRectangleGradientH(cx - 120, 100, 120, 1, (Color){ 0, 0, 0, 0 }, (Color){ 70, 95, 170, 180 });
    DrawRectangleGradientH(cx,       100, 120, 1, (Color){ 70, 95, 170, 180 }, (Color){ 0, 0, 0, 0 });

    /* arc exterieur : tourne dans le sens horaire */
    float R1 = 62.0f, r1 = 50.0f;
    float a1  = fmodf(t * 220.0f, 360.0f);
    Vector2 vc = { (float)cx, (float)cy };
    DrawRing(vc, r1 - 1.0f, R1 + 1.0f, 0.0f, 360.0f, 80, (Color){ 20, 26, 52, 255 });
    DrawRing(vc, r1, R1, a1, a1 + 130.0f, 28, (Color){ 90, 150, 255, 220 });
    DrawRing(vc, r1 + 2.0f, R1 - 2.0f, a1 + 118.0f, a1 + 133.0f, 6, (Color){ 170, 210, 255, 110 });

    /* arc interieur : tourne en sens inverse */
    float R2 = 43.0f, r2 = 34.0f;
    float a2  = fmodf(360.0f - fmodf(t * 160.0f, 360.0f), 360.0f);
    DrawRing(vc, r2, R2, a2, a2 + 85.0f, 20, (Color){ 160, 100, 255, 190 });
    DrawRing(vc, r2 + 1.0f, R2 - 1.0f, a2 + 74.0f, a2 + 88.0f, 5, (Color){ 210, 170, 255, 100 });

    /* point central pulsant */
    float pulse = 0.5f + 0.5f * sinf(t * 3.5f);
    DrawCircle(cx, cy, 5.0f + pulse * 2.5f,
               (Color){ 180, 210, 255, (unsigned char)(170 + (int)(pulse * 80.0f)) });

    /* texte */
    const char *lbl = "Loading maps...";
    int lw = MeasureText(lbl, 18);
    DrawText(lbl, cx - lw / 2, cy + 82, 18, (Color){ 180, 198, 240, 255 });

    /* progression */
    if (total > 0) {
        const char *prog = TextFormat("%d / %d", count, total);
        int pw = MeasureText(prog, 14);
        DrawText(prog, cx - pw / 2, cy + 108, 14, (Color){ 120, 150, 215, 200 });
        int bw = 220, bh = 3;
        int bx = cx - bw / 2, by = cy + 130;
        DrawRectangle(bx, by, bw, bh, (Color){ 24, 30, 58, 255 });
        int fill = (int)((float)count / (float)total * (float)bw);
        if (fill > 0)
            DrawRectangleGradientH(bx, by, fill, bh,
                (Color){ 80, 140, 240, 255 }, (Color){ 150, 95, 255, 255 });
    } else if (count > 0) {
        const char *prog = TextFormat("%d maps found", count);
        int pw = MeasureText(prog, 14);
        DrawText(prog, cx - pw / 2, cy + 108, 14, (Color){ 120, 150, 215, 200 });
    }

    DrawText("ESC : quit", 14, sh - 26, 13, (Color){ 68, 72, 104, 255 });
}

/* Formatte des millisecondes en "m:ss.t" (dixièmes de seconde). */
static void practice_fmt_ms(char *buf, int n, float ms) {
    if (ms < 0.0f) ms = 0.0f;
    int s = (int)(ms / 1000.0f);
    snprintf(buf, n, "%d:%02d.%d", s / 60, s % 60, ((int)ms % 1000) / 100);
}

int main(int argc, char **argv) {
    const char *argPath = NULL;
    int  width = 960, height = 540;
    /* startFull : fullscreen au lancement par defaut (--fullscreen est conserve pour compatibilite) */
    bool infoOnly = false, autoplay = false, startFull = true;
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--info") == 0)       { infoOnly = true;  continue; }
        else if (strcmp(argv[i], "--autoplay") == 0)   { autoplay = true;  continue; }
        else if (strcmp(argv[i], "--fullscreen") == 0) { startFull = true; continue; }
        int w, h;
        if (sscanf(argv[i], "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240) {
            width = w; height = h; continue;
        }
        if (!argPath) argPath = argv[i];
    }

    /* --info : chemin direct, infos, et on quitte sans fenetre. */
    if (infoOnly) {
        const char *file = argPath ? argPath : "map1.sspm";
        SspmMap map;
        if (!sspm_load(file, &map)) return 1;
        printf("Map      : %s\n", map.mapName);
        printf("Song     : %s\n", map.songName);
        printf("Mapper   : %s\n", map.mapper);
        printf("Notes    : %u (declared %u)\n", map.noteCountLoaded, map.noteCount);
        printf("Difficulty: %s\n", sspm_difficulty_name(map.difficulty));
        printf("Length   : %.1f s\n", map.lastMs / 1000.0f);
        printf("Audio    : %s%s\n", map.hasAudio ? "yes" : "no", map.hasAudio ? map.audioExt : "");
        fflush(stdout);
        sspm_free(&map);
        return 0;
    }

    /* Resolution du dossier de maps et d'un eventuel lancement direct. */
    Menu menu;
    memset(&menu, 0, sizeof menu);
    menu.diffFilter      = -1;   /* -1 = tous ; 0..5 = filtre par difficulte */
    menu.sortMode        =  1;   /* tri par defaut : difficulte */
    menu.collSel         = -1;   /* -1 = liste de collections (pas encore selectionnee) */
    menu.hideBlacklisted = true; /* maps bannies masquees par defaut */
    const char *directFile = NULL;
    if (argPath && DirectoryExists(argPath)) {
        snprintf(menu.dir, sizeof menu.dir, "%s", argPath);
    } else if (argPath && FileExists(argPath)) {
        directFile = argPath;
        const char *d = GetDirectoryPath(argPath);
        snprintf(menu.dir, sizeof menu.dir, "%s", (d && d[0]) ? d : ".");
    } else {
        snprintf(menu.dir, sizeof menu.dir, "%s", DirectoryExists("maps") ? "maps" : ".");
    }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(0);   /* vsync applique apres chargement des settings via settings_apply */
    InitWindow(width, height, "SSPM Player");
    InitAudioDevice();
    SetExitKey(KEY_NULL);   /* on gere Echap nous-memes (menu -> quitter, jeu -> menu) */
    EnableCursor();
    if (startFull) toggle_fullscreen(width, height);

    /* Texture blanche 1x1 pour les billboards de notes */
    { Image img = GenImageColor(1, 1, WHITE); gWhiteTex = LoadTextureFromImage(img); UnloadImage(img); }
    cursor_halo_init();   /* texture radiale partagee (coeur + halo + trainee) */

    /* reglages + meshes + curseurs (le contexte GL est pret -> chargement possible) */
    meshlist_scan("meshes");
    cursorlist_scan("cursors");
    hitsoundlist_scan("hitsounds");
    colorchain_scan("colors");
    settings_load(&gSettings);
    profile_load();
    if (gSettings.meshName[0]      && !notemesh_set(gSettings.meshName))      gSettings.meshName[0]      = '\0';
    if (gSettings.cursorMode == 1 && gSettings.cursorName[0] && !cursortex_set(gSettings.cursorName)) gSettings.cursorName[0] = '\0';
    if (gSettings.hitsoundName[0]  && !hitsound_set(gSettings.hitsoundName))  gSettings.hitsoundName[0]  = '\0';
    bg_init(gSettings.bgStyle, GetScreenWidth(), GetScreenHeight());
    settings_apply(&gSettings);
    if (gSettings.cursorInMenu) HideCursor();  /* applique l'etat initial du curseur menu */
    /* Si aucun chemin passé en arg et qu'un dossier est configuré, l'appliquer */
    if (!argPath && gSettings.mapsDir[0] && DirectoryExists(gSettings.mapsDir))
        snprintf(menu.dir, sizeof menu.dir, "%s", gSettings.mapsDir);

    /* Scan asynchrone : l'ecran de chargement tourne pendant la lecture des en-tetes */
    menu_scan_start(&menu, &gMenuScanner);

    Camera3D cam = { 0 };
    cam.position   = (Vector3){ 0.0f, 0.0f,  CAM_BACK };
    cam.target     = (Vector3){ 0.0f, 0.0f, -1.0f };
    cam.up         = (Vector3){ 0.0f, 1.0f,  0.0f };
    cam.fovy       = 70.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    Play play; memset(&play, 0, sizeof play);
    Calib calib; memset(&calib, 0, sizeof calib);
    AppScreen screen = SCR_LOADING;
    bool directFilePlayed = false;
    double cdRemain = 0.0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        if (IsKeyPressed(KEY_F11)) toggle_fullscreen(width, height);

        if (screen == SCR_LOADING) {
            if (IsKeyPressed(KEY_ESCAPE)) break;

            if (gMenuScanner.state == ALOAD_DONE) {
                menu_scan_join(&gMenuScanner);
                if (directFile && !directFilePlayed) {
                    directFilePlayed = true;
                    if (!go_play(&play, directFile, &cdRemain, &screen, autoplay))
                        screen = SCR_MENU;
                } else {
                    screen = SCR_MENU;
                }
                BeginDrawing(); ClearBackground((Color){ 10, 10, 16, 255 }); EndDrawing();
                continue;
            }

            BeginDrawing();
            loading_draw(sw, sh, (float)GetTime(), menu.scanCount, menu.scanTotal);
            EndDrawing();
        }
        else if (screen == SCR_MENU) {
            /* --- Options : verifie avant la saisie texte pour eviter les conflits --- */
            {
                bool openSettings = false;
                if (CheckCollisionPointRec(GetMousePosition(), opt_btn_rect(sw)) &&
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) openSettings = true;
                /* S ouvre les Options tant que la barre de recherche n'a pas le focus (contour) */
                if (!openSettings && !menu.filterFocused && IsKeyPressed(gKeys.menuSettings)) openSettings = true;
                if (openSettings) {
                    while (GetCharPressed() > 0) {}  /* vide la file de chars */
                    gOptSel = 0; screen = SCR_SETTINGS;
                    BeginDrawing(); ClearBackground((Color){ 12, 12, 20, 255 }); EndDrawing();
                    continue;
                }
                /* Modes de jeu : touche G ou clic sur le bouton Modes */
                {
                    bool openModes = (!menu.filterFocused && IsKeyPressed(gKeys.menuModes));
                    if (!openModes && CheckCollisionPointRec(GetMousePosition(), modes_btn_rect(sw)) &&
                        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) openModes = true;
                    if (openModes) {
                        while (GetCharPressed() > 0) {}
                        gModesSel = 0; screen = SCR_MODES;
                        BeginDrawing(); ClearBackground((Color){ 12, 12, 18, 255 }); EndDrawing();
                        continue;
                    }
                }
                /* Profile : touche P ou clic sur le bouton Profile */
                {
                    bool openProfile = (!menu.filterFocused && IsKeyPressed(gKeys.menuProfile));
                    if (!openProfile && CheckCollisionPointRec(GetMousePosition(), profile_btn_rect(sw)) &&
                        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) openProfile = true;
                    if (openProfile) {
                        while (GetCharPressed() > 0) {}
                        screen = SCR_PROFILE;
                        BeginDrawing(); ClearBackground((Color){ 12, 12, 18, 255 }); EndDrawing();
                        continue;
                    }
                }

                /* Mods : clic sur une case a cocher / fleche Vitesse du panneau gauche */
                if (menu.filteredCount > 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    int mpx, mpw, mly, mlh; menu_list_geo(sw, sh, &mpx, &mpw, &mly, &mlh);
                    mods_handle_click(mpx, sh, GetMousePosition());
                }
            }

            /* --- focus barre de recherche (clic) --- */
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int panelXs, panelWs, listYs, listHs;
                menu_list_geo(sw, sh, &panelXs, &panelWs, &listYs, &listHs);
                int sx = panelXs + 4, sw2 = panelWs - 292, sry = 4, srh = 28;
                bool onBar = CheckCollisionPointRec(GetMousePosition(),
                    (Rectangle){ (float)sx, (float)sry, (float)sw2, (float)srh });
                if (onBar) {
                    menu.filterFocused = true;
                    menu.filterCursor  = (int)strlen(menu.filter);
                } else {
                    menu.filterFocused = false;
                }
            }

            /* --- clic sur les onglets Toutes / Collections --- */
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int ptx, ptw, pty, pth; menu_list_geo(sw, sh, &ptx, &ptw, &pty, &pth);
                int tabW = 120, tabH = 22, tabY0 = 34, tabGap = 3;
                for (int t = 0; t < 2; t++) {
                    Rectangle tr = { (float)(ptx + 4 + t * (tabW + tabGap)), (float)tabY0,
                                     (float)tabW, (float)tabH };
                    if (CheckCollisionPointRec(GetMousePosition(), tr)) {
                        if (t != menu.viewTab) {
                            menu.viewTab = t;
                            menu.collSel = -1;
                            menu.sel = 0;
                            menu.scrollAnim = 0.0f;
                            menu_filter_rebuild(&menu);
                        } else if (t == 1 && menu.collSel >= 0) {
                            /* re-clic sur l'onglet Collections -> retour liste */
                            menu.collSel = -1;
                            menu.sel = 0;
                            menu.scrollAnim = 0.0f;
                            menu_filter_rebuild(&menu);
                        }
                    }
                }
            }

            /* --- raccourcis (avant GetCharPressed pour ne pas polluer le filtre) --- */
            /* F: toggle favori  /  V: favoris uniquement  /  N: jamais joue  /  D: cycle difficulte */
            /* Actifs des que la barre de recherche n'a pas le focus (contour), meme si du   */
            /* texte de recherche subsiste : le contour est le seul gate de capture clavier. */
            if (!menu.filterFocused) {
                bool shortcut = false;
                if (IsKeyPressed(gKeys.menuFavorite)) {
                    if (menu.filteredCount > 0) {
                        int idx = menu.filtered ? menu.filtered[menu.sel] : menu.sel;
                        profile_toggle_fav(GetFileName(menu.items[idx].path));
                        profile_save();
                        menu_filter_rebuild(&menu);
                    }
                    shortcut = true;
                }
                if (IsKeyPressed(gKeys.menuBan)) {
                    if (menu.filteredCount > 0) {
                        int idx = menu.filtered ? menu.filtered[menu.sel] : menu.sel;
                        profile_toggle_blacklist(GetFileName(menu.items[idx].path));
                        profile_save();
                        menu_filter_rebuild(&menu);
                    }
                    shortcut = true;
                }
                if (IsKeyPressed(gKeys.menuHideBanned)) {
                    menu.hideBlacklisted = !menu.hideBlacklisted;
                    menu.sel = 0; menu_filter_rebuild(&menu);
                    shortcut = true;
                }
                if (IsKeyPressed(gKeys.menuFavsOnly)) {
                    menu.favsOnly = !menu.favsOnly;
                    menu.sel = 0; menu_filter_rebuild(&menu);
                    shortcut = true;
                }
                if (IsKeyPressed(gKeys.menuNewOnly)) {
                    menu.newOnly = !menu.newOnly;
                    menu.sel = 0; menu_filter_rebuild(&menu);
                    shortcut = true;
                }
                if (IsKeyPressed(gKeys.menuDiffFilter)) {
                    menu.diffFilter = (menu.diffFilter >= 5) ? -1 : menu.diffFilter + 1;
                    menu.sel = 0; menu_filter_rebuild(&menu);
                    shortcut = true;
                }
                if (IsKeyPressed(gKeys.menuSortStar)) {
                    menu.sortMode = 4;
                    menu.sel = 0; menu_sort(&menu);
                    shortcut = true;
                }
                if (shortcut) { while (GetCharPressed() > 0) {} }  /* vide la file de chars */
            }

            /* --- saisie du filtre de recherche --- */
            {
                bool filterChanged = false;
                int len = (int)strlen(menu.filter);

                /* Deplacement curseur (uniquement si focused) */
                if (menu.filterFocused) {
                    if (IsKeyPressed(KEY_LEFT)  && menu.filterCursor > 0)   menu.filterCursor--;
                    if (IsKeyPressed(KEY_RIGHT) && menu.filterCursor < len) menu.filterCursor++;
                    if (IsKeyPressed(KEY_HOME))  menu.filterCursor = 0;
                    if (IsKeyPressed(KEY_END))   menu.filterCursor = len;
                }

                /* Saisie de caracteres */
                int ch;
                while ((ch = GetCharPressed()) > 0) {
                    if (ch >= 32 && ch < 127 && len < 127) {
                        int cur = menu.filterCursor;
                        if (cur > len) cur = menu.filterCursor = len;
                        memmove(menu.filter + cur + 1, menu.filter + cur, (size_t)(len - cur + 1));
                        menu.filter[cur] = (char)ch;
                        menu.filterCursor = cur + 1;
                        len++;
                        filterChanged = true;
                        if (!menu.filterFocused) menu.filterFocused = true;
                    }
                }

                /* Backspace : supprime le caractere avant le curseur */
                if (IsKeyPressed(KEY_BACKSPACE) && len > 0) {
                    int cur = menu.filterCursor;
                    if (cur > len) cur = menu.filterCursor = len;
                    if (cur > 0) {
                        memmove(menu.filter + cur - 1, menu.filter + cur, (size_t)(len - cur + 1));
                        menu.filterCursor = cur - 1;
                        filterChanged = true;
                    }
                }

                /* Delete : supprime le caractere sous le curseur */
                if (IsKeyPressed(KEY_DELETE) && menu.filterFocused && menu.filterCursor < len) {
                    int cur = menu.filterCursor;
                    memmove(menu.filter + cur, menu.filter + cur + 1, (size_t)(len - cur));
                    filterChanged = true;
                }

                if (filterChanged) { menu.sel = 0; menu_filter_rebuild(&menu); }
            }

            /* --- touches globales --- */
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (menu.viewTab == 1 && menu.collSel >= 0 &&
                    menu.filter[0] == '\0' && !menu.filterFocused) {
                    /* dans une collection : Echap = retour liste de collections */
                    menu.collSel = -1;
                    menu.sel = 0;
                    menu.scrollAnim = 0.0f;
                    menu_filter_rebuild(&menu);
                } else if (menu.filterFocused || menu.filter[0]) {
                    menu.filter[0] = '\0'; menu.filterCursor = 0;
                    menu.filterFocused = false;
                    menu_filter_rebuild(&menu);
                } else break;  /* quitter */
            }
            if (IsKeyPressed(gKeys.menuRescan)) {
                menu_scan_join(&gMenuScanner);   /* securite si un scan precedent tournait encore */
                menu.filter[0] = '\0'; menu.filterCursor = 0;
                menu.filterFocused = false;
                menu.scrollAnim = 0.0f;
                if (gHaveCover) { UnloadTexture(gCoverTex); gHaveCover = false; gCoverTex = (Texture2D){0}; }
                menu_scan_start(&menu, &gMenuScanner);
                screen = SCR_LOADING;
            }

            /* --- changement de tri --- */
            if (IsKeyPressed(gKeys.menuCycleSort)) {
                menu.sortMode = (menu.sortMode + 1) % 5;
                menu.sel = 0;
                menu_sort(&menu);
            }

            /* --- fichier glisse sur la fenetre -> jouer directement --- */
            if (IsFileDropped()) {
                FilePathList d = LoadDroppedFiles();
                char dropped[1024]; dropped[0] = '\0';
                if (d.count > 0 && IsFileExtension(d.paths[0], ".sspm"))
                    snprintf(dropped, sizeof dropped, "%s", d.paths[0]);
                UnloadDroppedFiles(d);
                if (dropped[0]) go_play(&play, dropped, &cdRemain, &screen, autoplay);
            }

            /* --- navigation dans la liste filtree (osu!-style) --- */
            if (screen == SCR_MENU) {
                int panelX2, panelW2, listY2, listH2;
                menu_list_geo(sw, sh, &panelX2, &panelW2, &listY2, &listH2);

                if (IsKeyPressed(KEY_DOWN))      menu.sel++;
                if (IsKeyPressed(KEY_UP))        menu.sel--;
                if (IsKeyPressed(KEY_PAGE_DOWN)) menu.sel += 8;
                if (IsKeyPressed(KEY_PAGE_UP))   menu.sel -= 8;
                /* HOME/END : navigation liste seulement si la barre de recherche n'est pas active */
                if (!menu.filterFocused) {
                    if (IsKeyPressed(KEY_HOME)) menu.sel = 0;
                    if (IsKeyPressed(KEY_END))  menu.sel = menu.filteredCount - 1;
                }

                float wheel = GetMouseWheelMove();
                /* Volume knob : si le curseur est dessus, consommer la molette */
                if (wheel != 0.0f) {
                    int vkMT = sh - 244, vkSp = vkMT - 250;
                    if (vkSp >= 60) {
                        float vkR = (float)(vkSp / 2 - 8);
                        if (vkR > 140.0f) vkR = 140.0f;
                        float vkCy = (float)(250 + vkMT) / 2.0f;
                        Vector2 mpv = GetMousePosition();
                        if (vkR >= 24.0f &&
                            mpv.x >= -4.0f && mpv.x <= vkR + 14.0f &&
                            mpv.y >= vkCy - vkR - 14.0f && mpv.y <= vkCy + vkR + 14.0f) {
                            gMusicVolume = clampf(gMusicVolume + wheel * 0.05f, 0.0f, 1.0f);
                            gSettings.musicVolume = gMusicVolume;
                            settings_save(&gSettings);
                            wheel = 0.0f;
                        }
                    }
                }
                if (wheel != 0) menu.sel -= (int)wheel;  /* molette scrolle par la selection */

                /* clampage */
                if (menu.sel < 0) menu.sel = 0;
                if (menu.sel >= menu.filteredCount && menu.filteredCount > 0)
                    menu.sel = menu.filteredCount - 1;

                Vector2 mpNav = GetMousePosition();
                int chosen = -1;

                /* Boutons de scroll : defilement continu si maintenu, comme une molette.
                 * Delai initial de 0.28 s avant la repetition, puis un pas toutes les 70 ms. */
                {
                    Rectangle ru = menu_scroll_up_rect(sw, sh);
                    Rectangle rd = menu_scroll_down_rect(sw, sh);
                    bool held = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
                    int dir = 0;
                    if (held && menu.filteredCount > 0) {
                        if      (CheckCollisionPointRec(mpNav, ru)) dir = -1;
                        else if (CheckCollisionPointRec(mpNav, rd)) dir =  1;
                    }
                    if (dir != 0) {
                        if (menu.scrollHoldDir != dir) {
                            /* premiere pression ou changement de bouton */
                            menu.scrollHoldDir   = dir;
                            menu.scrollHoldTimer = 0.28f;
                            menu.sel += dir;
                        } else {
                            menu.scrollHoldTimer -= dt;
                            if (menu.scrollHoldTimer <= 0.0f) {
                                menu.scrollHoldTimer += 0.015f;
                                menu.sel += dir;
                            }
                        }
                        if (menu.sel < 0) menu.sel = 0;
                        if (menu.sel >= menu.filteredCount) menu.sel = menu.filteredCount - 1;
                    } else {
                        menu.scrollHoldDir   = 0;
                        menu.scrollHoldTimer = 0.0f;
                    }
                }

                /* clic sur une carte (panneau droit, hors colonne de scroll) */
                if (mpNav.x >= panelX2 && mpNav.x < sw - SCROLL_BTN_W
                    && mpNav.y >= listY2 && mpNav.y <= listY2 + listH2
                    && menu.filteredCount > 0) {
                    int fi = (int)((mpNav.y - listY2 + menu.scrollAnim) / (float)CARD_STEP);
                    if (fi >= 0 && fi < menu.filteredCount) {
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            if (fi == menu.sel) chosen = fi;  /* 2eme clic -> jouer */
                            else menu.sel = fi;               /* 1er clic  -> selectionner */
                        }
                    }
                }

                /* clic sur les chips Normal/Zen/Ladder/Practice */
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    for (int mi = 0; mi < 4; mi++)
                        if (CheckCollisionPointRec(mpNav, menu_mode_chip_rect(panelX2, sh, mi)))
                            gMenuMode = mi;
                }

                /* clic sur le bouton JOUER (panneau gauche) */
                {
                    Rectangle btnR2 = menu_play_btn_rect(panelX2, sh);
                    if (CheckCollisionPointRec(mpNav, btnR2) &&
                        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && menu.filteredCount > 0)
                        chosen = menu.sel;
                }

                if (IsKeyPressed(gKeys.menuPlay) && menu.filteredCount > 0) chosen = menu.sel;

                /* En vue arbre de dossiers : chosen = ouvrir le dossier, pas jouer */
                if (chosen >= 0 && menu.viewTab == 1 && menu.collSel < 0) {
                    if (chosen < menu.folderCount) {
                        menu.collSel = chosen;
                        menu.sel = 0;
                        menu.scrollAnim = 0.0f;
                        menu_filter_rebuild(&menu);
                        if (gHaveCover) { UnloadTexture(gCoverTex); gHaveCover = false; gCoverTex = (Texture2D){0}; }
                    }
                    chosen = -1;
                }

                if (chosen >= 0 && chosen < menu.filteredCount) {
                    int idx = menu.filtered ? menu.filtered[chosen] : chosen;
                    if      (gMenuMode == 1) go_mode_map(&play, menu.items[idx].path, MODE_ZEN,      &cdRemain, &screen, autoplay);
                    else if (gMenuMode == 2) go_mode_map(&play, menu.items[idx].path, MODE_LADDER,   &cdRemain, &screen, autoplay);
                    else if (gMenuMode == 3) go_mode_map(&play, menu.items[idx].path, MODE_PRACTICE, &cdRemain, &screen, autoplay);
                    else                     go_play    (&play, menu.items[idx].path,                 &cdRemain, &screen, autoplay);
                }
            }

            /* Chargement de la cover : debounce 180 ms pour ne pas charger pendant le scroll */
            {
                static int   sCoverSel  = -2;   /* -2 = jamais charge */
                static int   sCoverPend = -2;
                static float sCoverDelay = 0.0f;

                if (menu.filteredCount > 0 && menu.sel != sCoverPend &&
                    !(menu.viewTab == 1 && menu.collSel < 0)) {
                    sCoverPend  = menu.sel;
                    sCoverDelay = 0.18f;
                }
                if (sCoverDelay > 0.0f) {
                    sCoverDelay -= dt;
                    if (sCoverDelay <= 0.0f && sCoverPend != sCoverSel) {
                        sCoverSel = sCoverPend;
                        if (gHaveCover) { UnloadTexture(gCoverTex); gHaveCover = false; gCoverTex = (Texture2D){0}; }
                        if (sCoverSel >= 0 && sCoverSel < menu.filteredCount) {
                            int ci = menu.filtered ? menu.filtered[sCoverSel] : sCoverSel;
                            if (menu.items[ci].info.hasCover) {
                                uint8_t *cdata = NULL;
                                uint64_t clen  = sspm_load_cover(menu.items[ci].path, &cdata);
                                if (clen > 0 && cdata) {
                                    /* detection format : PNG (89 50 4E 47) ou JPG (FF D8) */
                                    const char *ext = (clen >= 3 && cdata[0]==0xFF && cdata[1]==0xD8)
                                                      ? ".jpg" : ".png";
                                    Image img = LoadImageFromMemory(ext, cdata, (int)clen);
                                    free(cdata);
                                    if (img.data) {
                                        gCoverTex  = LoadTextureFromImage(img);
                                        UnloadImage(img);
                                        gHaveCover = (gCoverTex.id > 0);
                                    }
                                } else { free(cdata); }
                            }
                        }
                    }
                }
            }

            /* Mise a jour de l'animation de defilement (lerp vers la cible chaque frame) */
            {
                int apx, apw, aly, alh; menu_list_geo(sw, sh, &apx, &apw, &aly, &alh);
                float target = (menu.filteredCount > 0) ? menu_scrl_px(&menu, alh) : 0.0f;
                float k = fminf(1.0f, dt * 20.0f);
                menu.scrollAnim += (target - menu.scrollAnim) * k;
            }

            BeginDrawing();
            menu_draw(&menu, sw, sh);
            if (gSettings.cursorInMenu) cursor_draw_at(GetMousePosition(), sh);
            EndDrawing();
        }
        else if (screen == SCR_SETTINGS) {
            if (IsKeyPressed(KEY_ESCAPE) && !gSensEditing) {
                settings_save(&gSettings);
                screen = SCR_MENU;
                /* PollInputEvents est appele dans BeginDrawing : sans ce flush,
                 * la touche Echap resterait "pressee" a l'iteration suivante et
                 * declencherait le break du menu (fermeture du jeu). */
                BeginDrawing();
                ClearBackground((Color){ 12, 12, 20, 255 });
                EndDrawing();
            } else if (!gSensEditing && IsKeyPressed(KEY_C) &&
                       opt_selected_global() == 21) {
                /* Ligne "Offset audio" selectionnee : ouvrir la calibration */
                calib_begin(&calib);
                screen = SCR_CALIBRATE;
                while (GetCharPressed() > 0) {}
                BeginDrawing(); ClearBackground((Color){ 12, 12, 20, 255 }); EndDrawing();
            } else {
                settings_update(sw, sh);
                if (gMapsDirRescan) {
                    gMapsDirRescan = false;
                    settings_save(&gSettings);
                    snprintf(menu.dir, sizeof menu.dir, "%s",
                             (gSettings.mapsDir[0] && DirectoryExists(gSettings.mapsDir))
                                 ? gSettings.mapsDir
                                 : (DirectoryExists("maps") ? "maps" : "."));
                    menu_scan_join(&gMenuScanner);
                    menu.filter[0] = '\0'; menu.filterCursor = 0;
                    menu.filterFocused = false; menu.scrollAnim = 0.0f;
                    if (gHaveCover) { UnloadTexture(gCoverTex); gHaveCover = false; gCoverTex = (Texture2D){0}; }
                    menu_scan_start(&menu, &gMenuScanner);
                    BeginDrawing(); ClearBackground((Color){ 12, 12, 20, 255 }); EndDrawing();
                    screen = SCR_LOADING;
                    continue;
                }
                BeginDrawing();
                settings_draw(sw, sh);
                if (gSettings.cursorInMenu) cursor_draw_at(GetMousePosition(), sh);
                EndDrawing();
            }
        }
        else if (screen == SCR_MODES) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                screen = SCR_MENU;
                BeginDrawing(); ClearBackground((Color){ 12, 12, 18, 255 }); EndDrawing();
                continue;
            }
            modes_update(sw, sh);
            if (gModesChoice >= 0) {
                go_aim(&play, &cdRemain, &screen, autoplay);
                continue;
            }
            BeginDrawing();
            modes_draw(sw, sh, &menu);
            if (gSettings.cursorInMenu) cursor_draw_at(GetMousePosition(), sh);
            EndDrawing();
        }
        else if (screen == SCR_PROFILE) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                screen = SCR_MENU;
                BeginDrawing(); ClearBackground((Color){ 12, 12, 18, 255 }); EndDrawing();
                continue;
            }
            BeginDrawing();
            profile_draw(sw, sh, menu.count);
            if (gSettings.cursorInMenu) cursor_draw_at(GetMousePosition(), sh);
            EndDrawing();
        }
        else if (screen == SCR_CALIBRATE) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                screen = SCR_SETTINGS;
                BeginDrawing(); ClearBackground((Color){ 12, 12, 20, 255 }); EndDrawing();
                continue;
            }
            if (IsKeyPressed(KEY_R)) calib_reset_samples(&calib);
            if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && calib.nSamples > 0) {
                gSettings.audioOffsetMs = clampf(roundf(calib.suggested), -300.0f, 300.0f);
                settings_apply(&gSettings);
                settings_save(&gSettings);
                screen = SCR_SETTINGS;
                BeginDrawing(); ClearBackground((Color){ 12, 12, 20, 255 }); EndDrawing();
                continue;
            }
            calib_update(&calib);
            BeginDrawing();
            calib_draw(&calib, sw, sh);
            if (gSettings.cursorInMenu) cursor_draw_at(GetMousePosition(), sh);
            EndDrawing();
        }
        else if (screen == SCR_PRACTICE_SETUP) {
            /* === Modal de configuration Practice =================================
             * Affiche une timeline draggable pour choisir A (début) et B (fin de
             * boucle), régler la vitesse, et lancer la map.  Apparaît une seule
             * fois par chargement ; les redémarrages (KEY_R en jeu) passent
             * directement au décompte car gLoadFinalized reste vrai.
             * ===================================================================== */
            static bool sPSdragA = false, sPSdragB = false;

            float ps_total = play.map.lastMs > 0 ? (float)play.map.lastMs : 60000.0f;

            /* Géométrie du panneau */
            int ps_pw = 680, ps_ph = 430;
            int ps_px = (sw - ps_pw) / 2, ps_py = (sh - ps_ph) / 2;
            if (ps_py < 8) ps_py = 8;

            /* Barre de timeline */
            int ps_bx = ps_px + 44, ps_bw = ps_pw - 88;
            int ps_by = ps_py + 148, ps_bh = 14;

            /* Positions initiales des marqueurs */
            float ps_an = clampf(gPracticeA / ps_total, 0.0f, 1.0f);
            float ps_bn = clampf(gPracticeB / ps_total, 0.0f, 1.0f);
            int ps_ax  = ps_bx + (int)(ps_an * ps_bw);
            int ps_bxp = ps_bx + (int)(ps_bn * ps_bw);

            /* Poignées (rectangles au-dessus de la barre, hHW=8, hH=22) */
            int ps_hHW = 8, ps_hH = 22;
            Rectangle ps_aHan = { (float)(ps_ax  - ps_hHW), (float)(ps_by - ps_hH - 2), (float)(ps_hHW*2), (float)ps_hH };
            Rectangle ps_bHan = { (float)(ps_bxp - ps_hHW), (float)(ps_by - ps_hH - 2), (float)(ps_hHW*2), (float)ps_hH };
            /* Zone de clic élargie pour faciliter le drag */
            Rectangle ps_aHit = { (float)(ps_ax  - 14), (float)(ps_by - ps_hH - 4), 28.0f, (float)(ps_hH + ps_bh + 4) };
            Rectangle ps_bHit = { (float)(ps_bxp - 14), (float)(ps_by - ps_hH - 4), 28.0f, (float)(ps_hH + ps_bh + 4) };

            Vector2 ps_mp = GetMousePosition();
            bool ps_mdn  = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            bool ps_mpr  = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            bool ps_mrl  = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

            if (ps_mrl) { sPSdragA = false; sPSdragB = false; }

            /* Début du drag / clic sur la barre */
            if (ps_mpr) {
                bool onA = CheckCollisionPointRec(ps_mp, ps_aHit);
                bool onB = CheckCollisionPointRec(ps_mp, ps_bHit);
                Rectangle ps_barZone = { (float)ps_bx, (float)(ps_by - ps_hH - 4),
                                         (float)ps_bw, (float)(ps_hH + ps_bh + 8) };
                if      (onA && !onB) { sPSdragA = true; }
                else if (onB && !onA) { sPSdragB = true; }
                else if (onA &&  onB) {
                    sPSdragA = (ps_mp.x <= (ps_ax + ps_bxp) * 0.5f);
                    sPSdragB = !sPSdragA;
                } else if (CheckCollisionPointRec(ps_mp, ps_barZone)) {
                    float cMs = clampf((ps_mp.x - ps_bx) / (float)ps_bw, 0.0f, 1.0f) * ps_total;
                    if (fabsf(cMs - gPracticeA) <= fabsf(cMs - gPracticeB)) { sPSdragA = true; gPracticeA = cMs; }
                    else                                                       { sPSdragB = true; gPracticeB = cMs; }
                }
            }
            /* Application du drag avec contrainte A < B - 200 ms */
            if (ps_mdn) {
                float ps_norm = clampf((ps_mp.x - ps_bx) / (float)ps_bw, 0.0f, 1.0f);
                float ps_ms   = ps_norm * ps_total;
                if (sPSdragA) gPracticeA = clampf(ps_ms, 0.0f, gPracticeB - 200.0f > 0.0f ? gPracticeB - 200.0f : 0.0f);
                if (sPSdragB) gPracticeB = clampf(ps_ms, gPracticeA + 200.0f, ps_total);
            }

            /* Boutons vitesse (clic) */
            int ps_spCX = ps_px + ps_pw / 2;
            Rectangle ps_btnM = { (float)(ps_spCX - 84), (float)(ps_py + 256), 36.0f, 30.0f };
            Rectangle ps_btnP = { (float)(ps_spCX + 48), (float)(ps_py + 256), 36.0f, 30.0f };
            if (ps_mpr && CheckCollisionPointRec(ps_mp, ps_btnM)) gRate = clampf(gRate - 0.05f, 0.25f, 2.0f);
            if (ps_mpr && CheckCollisionPointRec(ps_mp, ps_btnP)) gRate = clampf(gRate + 0.05f, 0.25f, 2.0f);

            /* Bouton Loop */
            Rectangle ps_btnLp = { (float)(ps_px + ps_pw - 124), (float)(ps_py + 184), 80.0f, 24.0f };
            if (ps_mpr && CheckCollisionPointRec(ps_mp, ps_btnLp)) gPracticeLoop = !gPracticeLoop;

            /* Bouton LAUNCH */
            Rectangle ps_btnLn = { (float)(ps_spCX - 90), (float)(ps_py + ps_ph - 86), 180.0f, 44.0f };
            bool ps_doLaunch = (ps_mpr && CheckCollisionPointRec(ps_mp, ps_btnLn))
                            || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)
                            || IsKeyPressed(KEY_SPACE);
            if (ps_doLaunch) {
                gScoreMult = mods_score_mult(gMods, gRate);
                if (play.haveMusic) SetMusicPitch(play.music, gRate);
                cdRemain = COUNTDOWN_SEC;
                screen   = SCR_COUNTDOWN;
                /* Recache la souris pour le gameplay (comme go_play le ferait) */
                if (!autoplay) {
                    if (gTablet) { EnableCursor(); HideCursor(); }
                    else DisableCursor();
                }
                BeginDrawing(); ClearBackground((Color){ 10, 10, 16, 255 }); EndDrawing();
                continue;
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                sPSdragA = sPSdragB = false;
                go_menu(&play, &screen);
                BeginDrawing(); ClearBackground((Color){ 10, 10, 16, 255 }); EndDrawing();
                continue;
            }
            /* Raccourcis clavier vitesse + boucle */
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) gRate = clampf(gRate - 0.05f, 0.25f, 2.0f);
            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))      gRate = clampf(gRate + 0.05f, 0.25f, 2.0f);
            if (IsKeyPressed(KEY_L)) gPracticeLoop = !gPracticeLoop;

            /* Recalcul des positions après drag (pour le rendu) */
            ps_an  = clampf(gPracticeA / ps_total, 0.0f, 1.0f);
            ps_bn  = clampf(gPracticeB / ps_total, 0.0f, 1.0f);
            ps_ax  = ps_bx + (int)(ps_an  * ps_bw);
            ps_bxp = ps_bx + (int)(ps_bn  * ps_bw);
            ps_aHan = (Rectangle){ (float)(ps_ax  - ps_hHW), (float)(ps_by - ps_hH - 2), (float)(ps_hHW*2), (float)ps_hH };
            ps_bHan = (Rectangle){ (float)(ps_bxp - ps_hHW), (float)(ps_by - ps_hH - 2), (float)(ps_hHW*2), (float)ps_hH };
            ps_aHit = (Rectangle){ (float)(ps_ax  - 14), (float)(ps_by - ps_hH - 4), 28.0f, (float)(ps_hH + ps_bh + 4) };
            ps_bHit = (Rectangle){ (float)(ps_bxp - 14), (float)(ps_by - ps_hH - 4), 28.0f, (float)(ps_hH + ps_bh + 4) };
            bool ps_aHov = CheckCollisionPointRec(ps_mp, ps_aHit);
            bool ps_bHov = CheckCollisionPointRec(ps_mp, ps_bHit);

            /* ---- RENDU ---- */
            BeginDrawing();
            ClearBackground((Color){ 10, 10, 16, 255 });

            /* Halo de fond */
            DrawCircleGradient((Vector2){ (float)(sw / 2), (float)(sh / 2) }, (float)(sh * 0.65f),
                               (Color){ 18, 26, 64, 55 }, (Color){ 10, 10, 16, 0 });

            /* Panneau principal */
            DrawRectangle(ps_px, ps_py, ps_pw, ps_ph, (Color){ 14, 18, 36, 248 });
            DrawRectangleLines(ps_px, ps_py, ps_pw, ps_ph, (Color){ 55, 85, 165, 210 });
            /* Trait accent en haut */
            DrawRectangle(ps_px + 2, ps_py, ps_pw - 4, 3, (Color){ 100, 170, 255, 200 });

            /* Titre */
            { const char *t = "PRACTICE MODE";
              DrawText(t, ps_px + ps_pw/2 - MeasureText(t, 24)/2, ps_py + 18, 24, (Color){ 130, 200, 255, 255 }); }

            /* Nom de la map */
            { const char *mn = play.map.songName[0] ? play.map.songName : gCurrentMap;
              DrawText(mn, ps_px + ps_pw/2 - MeasureText(mn, 15)/2, ps_py + 50, 15,
                       (Color){ 165, 182, 225, 210 }); }

            /* Durée */
            { char ps_tot[24]; practice_fmt_ms(ps_tot, sizeof ps_tot, ps_total);
              const char *ds = TextFormat("Duration  %s", ps_tot);
              DrawText(ds, ps_px + ps_pw/2 - MeasureText(ds, 12)/2, ps_py + 72, 12,
                       (Color){ 95, 120, 182, 175 });

              /* Séparateurs */
              DrawRectangleGradientH(ps_px+44, ps_py+96, ps_pw/2-44, 1, (Color){0,0,0,0}, (Color){50,75,148,180});
              DrawRectangleGradientH(ps_px+ps_pw/2, ps_py+96, ps_pw/2-44, 1, (Color){50,75,148,180}, (Color){0,0,0,0});
              DrawText("TIMELINE", ps_px + 44, ps_py + 106, 11, (Color){ 65, 98, 178, 200 });

              /* Fond de barre */
              DrawRectangle(ps_bx, ps_by, ps_bw, ps_bh, (Color){ 20, 26, 54, 255 });

              /* Région A→B */
              int ps_rgW = ps_bxp - ps_ax;
              if (ps_rgW > 0) DrawRectangle(ps_ax, ps_by, ps_rgW, ps_bh, (Color){ 80, 148, 255, 38 });

              /* Contour de barre */
              DrawRectangleLines(ps_bx, ps_by, ps_bw, ps_bh, (Color){ 42, 62, 124, 200 });

              /* Lignes verticales des marqueurs */
              DrawRectangle(ps_ax  - 1, ps_by - ps_hH - 2, 2, ps_hH + ps_bh + 2, (Color){ 95, 215, 95, 185 });
              DrawRectangle(ps_bxp - 1, ps_by - ps_hH - 2, 2, ps_hH + ps_bh + 2, (Color){ 255, 162, 55, 185 });

              /* Poignée A (verte) */
              DrawRectangle((int)ps_aHan.x, (int)ps_aHan.y, (int)ps_aHan.width, (int)ps_aHan.height,
                            ps_aHov ? (Color){ 95, 210, 95, 248 } : (Color){ 65, 175, 65, 218 });
              DrawRectangleLines((int)ps_aHan.x, (int)ps_aHan.y, (int)ps_aHan.width, (int)ps_aHan.height,
                                 (Color){ 135, 255, 135, 205 });
              DrawText("A", ps_ax - MeasureText("A", 11)/2, (int)ps_aHan.y + 6, 11, WHITE);

              /* Poignée B (orange) */
              DrawRectangle((int)ps_bHan.x, (int)ps_bHan.y, (int)ps_bHan.width, (int)ps_bHan.height,
                            ps_bHov ? (Color){ 255, 175, 80, 248 } : (Color){ 218, 140, 50, 218 });
              DrawRectangleLines((int)ps_bHan.x, (int)ps_bHan.y, (int)ps_bHan.width, (int)ps_bHan.height,
                                 (Color){ 255, 200, 100, 205 });
              DrawText("B", ps_bxp - MeasureText("B", 11)/2, (int)ps_bHan.y + 6, 11, WHITE);

              /* Labels de temps sous la barre */
              DrawText("0:00", ps_bx, ps_by + ps_bh + 4, 11, (Color){ 75, 105, 172, 172 });
              DrawText(ps_tot, ps_bx + ps_bw - MeasureText(ps_tot, 11), ps_by + ps_bh + 4, 11,
                       (Color){ 75, 105, 172, 172 }); }

            /* Infos A / B en clair */
            { char ps_ab[24], ps_bb[24];
              practice_fmt_ms(ps_ab, sizeof ps_ab, gPracticeA);
              practice_fmt_ms(ps_bb, sizeof ps_bb, gPracticeB);
              DrawText(TextFormat("Start (A):  %s", ps_ab), ps_px + 44, ps_py + 188, 13,
                       (Color){ 115, 215, 115, 225 });
              DrawText(TextFormat("End (B):  %s",   ps_bb), ps_px + 240, ps_py + 188, 13,
                       (Color){ 255, 172, 75, 225 }); }

            /* Bouton Loop */
            { bool lp_h = CheckCollisionPointRec(ps_mp, ps_btnLp);
              DrawRectangle((int)ps_btnLp.x, (int)ps_btnLp.y, (int)ps_btnLp.width, (int)ps_btnLp.height,
                            lp_h ? (Color){ 48, 68, 132, 238 } : (Color){ 26, 34, 70, 208 });
              DrawRectangleLines((int)ps_btnLp.x, (int)ps_btnLp.y, (int)ps_btnLp.width, (int)ps_btnLp.height,
                                 gPracticeLoop ? (Color){ 95, 215, 95, 228 } : (Color){ 55, 82, 152, 208 });
              const char *lp_t = TextFormat("Loop  %s", gPracticeLoop ? "ON" : "OFF");
              Color lp_c = gPracticeLoop ? (Color){ 115, 238, 115, 255 } : (Color){ 155, 170, 218, 208 };
              DrawText(lp_t, (int)ps_btnLp.x + (int)ps_btnLp.width/2 - MeasureText(lp_t, 12)/2,
                       (int)ps_btnLp.y + 6, 12, lp_c); }

            /* Séparateur + label SPEED */
            DrawRectangleGradientH(ps_px+44, ps_py+228, ps_pw/2-44, 1, (Color){0,0,0,0}, (Color){40,60,120,150});
            DrawRectangleGradientH(ps_px+ps_pw/2, ps_py+228, ps_pw/2-44, 1, (Color){40,60,120,150}, (Color){0,0,0,0});
            { const char *sl = "SPEED";
              DrawText(sl, ps_spCX - MeasureText(sl, 12)/2, ps_py + 236, 12, (Color){ 105, 138, 198, 200 }); }

            /* Bouton [ - ] */
            { bool mh = CheckCollisionPointRec(ps_mp, ps_btnM);
              DrawRectangle((int)ps_btnM.x, (int)ps_btnM.y, (int)ps_btnM.width, (int)ps_btnM.height,
                            mh ? (Color){ 58, 78, 158, 238 } : (Color){ 26, 34, 70, 208 });
              DrawRectangleLines((int)ps_btnM.x, (int)ps_btnM.y, (int)ps_btnM.width, (int)ps_btnM.height,
                                 (Color){ 65, 98, 185, 208 });
              DrawText("-", (int)ps_btnM.x + 12, (int)ps_btnM.y + 6, 20, RAYWHITE); }

            /* Taux de vitesse centré */
            { const char *rs = TextFormat("%.2f x", gRate);
              DrawText(rs, ps_spCX - MeasureText(rs, 20)/2, ps_py + 260, 20, (Color){ 130, 200, 255, 255 }); }

            /* Bouton [ + ] */
            { bool ph2 = CheckCollisionPointRec(ps_mp, ps_btnP);
              DrawRectangle((int)ps_btnP.x, (int)ps_btnP.y, (int)ps_btnP.width, (int)ps_btnP.height,
                            ph2 ? (Color){ 58, 78, 158, 238 } : (Color){ 26, 34, 70, 208 });
              DrawRectangleLines((int)ps_btnP.x, (int)ps_btnP.y, (int)ps_btnP.width, (int)ps_btnP.height,
                                 (Color){ 65, 98, 185, 208 });
              DrawText("+", (int)ps_btnP.x + 11, (int)ps_btnP.y + 6, 20, RAYWHITE); }

            /* Séparateur bas */
            DrawRectangleGradientH(ps_px+44, ps_py+ps_ph-104, ps_pw/2-44, 1, (Color){0,0,0,0}, (Color){50,75,148,180});
            DrawRectangleGradientH(ps_px+ps_pw/2, ps_py+ps_ph-104, ps_pw/2-44, 1, (Color){50,75,148,180}, (Color){0,0,0,0});

            /* Bouton LAUNCH */
            { bool lnh = CheckCollisionPointRec(ps_mp, ps_btnLn);
              DrawRectangle((int)ps_btnLn.x, (int)ps_btnLn.y, (int)ps_btnLn.width, (int)ps_btnLn.height,
                            lnh ? (Color){ 68, 145, 255, 248 } : (Color){ 42, 108, 212, 220 });
              DrawRectangleLines((int)ps_btnLn.x, (int)ps_btnLn.y, (int)ps_btnLn.width, (int)ps_btnLn.height,
                                 (Color){ 118, 190, 255, 255 });
              const char *lt = "LAUNCH";
              DrawText(lt, (int)ps_btnLn.x + (int)ps_btnLn.width/2 - MeasureText(lt, 22)/2,
                       (int)ps_btnLn.y + 10, 22, WHITE); }

            /* Hints clavier */
            DrawText("ESC : back to menu", ps_px + 12, ps_py + ps_ph - 20, 12, (Color){ 68, 82, 128, 182 });
            { const char *eh = "ENTER / SPACE : launch";
              DrawText(eh, ps_px + ps_pw - 12 - MeasureText(eh, 12), ps_py + ps_ph - 20, 12,
                       (Color){ 68, 82, 128, 182 }); }

            if (gSettings.cursorInMenu) cursor_draw_at(ps_mp, sh);
            EndDrawing();
        }
        else if (screen == SCR_COUNTDOWN) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                go_menu(&play, &screen);   /* join + unload */
                BeginDrawing(); ClearBackground((Color){ 12, 12, 20, 255 }); EndDrawing();
                continue;
            }

            /* --- Phase 1 : attendre la fin du chargement asynchrone --- */
            if (!gLoadFinalized) {
                /* spinT est statique : il accumule entre deux decomptes, mais est remis a
                 * zero apres finalisation (ligne plus bas), donc jamais deja-haut au debut. */
                static float spinT = 0.0f; spinT += (float)dt;
                /* Finalize quand le thread a termine */
                if (gLoader.state == ALOAD_DONE || gLoader.state == ALOAD_FAIL) {
                    athread_join(&gLoader);
                    if (gLoader.state == ALOAD_FAIL) {
                        gLoader.state = ALOAD_IDLE;
                        go_menu(&play, &screen);
                        BeginDrawing(); ClearBackground((Color){ 12, 12, 20, 255 }); EndDrawing();
                        continue;
                    }
                    /* Chargement OK : finir sur le thread principal (audio + reset) */
                    if (play.map.hasAudio && play.map.audio) {
                        play.music = LoadMusicStreamFromMemory(play.map.audioExt,
                                         play.map.audio, (int)play.map.audioLen);
                        if (play.music.stream.buffer) { play.music.looping = false; play.haveMusic = true; }
                        else fprintf(stderr, "Unreadable audio (%s): playing muted.\n", play.map.audioExt);
                    }
                    SetWindowTitle(TextFormat("SSPM Player - %s", play.map.songName));
                    float prevRate = gRate;   /* preserve la vitesse Practice (reglee en jeu) au restart */
                    settings_apply(&gSettings);
                    if (gMode == MODE_LADDER)        { gRate = gLadderRate; gScoreMult = mods_score_mult(gMods, gRate); }
                    else if (gMode == MODE_PRACTICE) { gRate = prevRate;    gScoreMult = mods_score_mult(gMods, gRate); }
                    if (play.haveMusic) SetMusicPitch(play.music, gRate);  /* Vitesse / Ladder / Practice : pitch = vitesse de lecture */
                    play_reset(&play);
                    gLoader.state   = ALOAD_IDLE;
                    gLoadFinalized  = true;
                    cdRemain        = COUNTDOWN_SEC;
                    spinT           = 0.0f;
                    if (gMode == MODE_PRACTICE) {
                        /* Ouvre le modal de configuration avant le decompte */
                        if (gPracticeB <= gPracticeA + 1.0f)
                            gPracticeB = play.map.lastMs > 0 ? (float)play.map.lastMs : 0.0f;
                        screen = SCR_PRACTICE_SETUP;
                        EnableCursor(); ShowCursor();   /* souris necessaire pour le modal */
                        BeginDrawing(); ClearBackground((Color){ 10, 10, 16, 255 }); EndDrawing();
                        continue;
                    }
                }
                if (!gLoadFinalized) {
                    /* Ecran de chargement de map */
                    int cx2 = sw / 2, cy2 = sh / 2 - 16;
                    float R  = 46.0f, r = 36.0f;
                    float aa = fmodf(spinT * 260.0f, 360.0f);
                    Vector2 vc2 = { (float)cx2, (float)cy2 };
                    BeginDrawing();
                    ClearBackground((Color){ 10, 10, 16, 255 });
                    DrawCircleGradient((Vector2){ (float)cx2, (float)cy2 }, 160.0f, (Color){ 14, 20, 48, 70 }, (Color){ 10, 10, 16, 0 });
                    /* nom de la map */
                    int fnw = MeasureText(gCurrentMap, 20);
                    DrawText(gCurrentMap, cx2 - fnw / 2, cy2 - 80, 20, (Color){ 170, 190, 240, 200 });
                    /* arc tournant */
                    DrawRing(vc2, r - 1.0f, R + 1.0f, 0.0f, 360.0f, 64, (Color){ 18, 24, 48, 255 });
                    DrawRing(vc2, r, R, aa, aa + 140.0f, 28, (Color){ 90, 150, 255, 220 });
                    DrawRing(vc2, r + 2.0f, R - 2.0f, aa + 128.0f, aa + 143.0f, 6, (Color){ 170, 210, 255, 110 });
                    /* arc interieur inverse */
                    float ab = fmodf(360.0f - fmodf(spinT * 180.0f, 360.0f), 360.0f);
                    DrawRing(vc2, r * 0.6f, R * 0.6f, ab, ab + 90.0f, 18, (Color){ 160, 100, 255, 180 });
                    /* point central */
                    float p2 = 0.5f + 0.5f * sinf(spinT * 4.0f);
                    DrawCircle(cx2, cy2, 4.0f + p2 * 2.0f,
                               (Color){ 180, 210, 255, (unsigned char)(160 + (int)(p2 * 80.0f)) });
                    /* texte */
                    int dots = ((int)(spinT * 3.0f)) % 4;
                    char s[40]; snprintf(s, sizeof s, "Loading%.*s", dots, "...");
                    int sw3 = MeasureText(s, 22);
                    DrawText(s, cx2 - sw3 / 2, cy2 + 60, 22, RAYWHITE);
                    DrawText("ESC : back to menu", 14, sh - 28, 14, (Color){ 100, 105, 135, 255 });
                    EndDrawing();
                    continue;
                }
            }

            /* --- Phase 2 : decompte normal --- */
            double cddt = dt > 0.05 ? 0.05 : dt;
            cdRemain -= cddt;
            play.nowMs = -(float)((cdRemain > 0 ? cdRemain : 0) * 1000.0);
            play_cursor(&play, autoplay, sw, sh);

            /* Rendu dans la render texture basse resolution si internalRes > 0,
             * sinon directement dans la fenetre.  La source RT a la hauteur NEGATIVE
             * dans DrawTexturePro pour corriger le flip Y des RenderTexture raylib. */
            { int rtw = gRtW > 0 ? gRtW : sw, rth = gRtH > 0 ? gRtH : sh;
              if (gRtW > 0) BeginTextureMode(gRenderTex); else BeginDrawing();
              play_draw_scene(&play, cam, autoplay);

              int n = (int)ceilf((float)cdRemain);
              n = n < 1 ? 1 : (n > 3 ? 3 : n);
              const char *nt = TextFormat("%d", n);
              int fs = 160;
              DrawText(nt, rtw / 2 - MeasureText(nt, fs) / 2, rth / 2 - fs / 2, fs, RAYWHITE);
              const char *ready = (gMode == MODE_LADDER)
                  ? TextFormat("Speed Ladder  -  Level %d  -  %gx", gLadderLevel + 1, gRate)
                  : (gMode == MODE_AIM      ? "Aim Trainer"
                  : (gMode == MODE_PRACTICE ? TextFormat("Practice  -  %.2fx", gRate)
                  :                            "Get ready..."));
              DrawText(ready, rtw / 2 - MeasureText(ready, 24) / 2, rth / 2 - fs / 2 - 44, 24,
                       (Color){ 180, 200, 255, 255 });
              DrawText(play.map.songName, 14, 12, 22, RAYWHITE);
              DrawText("ESC : back to menu", 14, rth - 30, 14, (Color){ 130, 130, 145, 255 });
              if (gRtW > 0) {
                  EndTextureMode();
                  BeginDrawing(); ClearBackground(BLACK);
                  DrawTexturePro(gRenderTex.texture,
                      (Rectangle){0,0,(float)gRtW,-(float)gRtH},
                      (Rectangle){0,0,(float)sw,(float)sh},
                      (Vector2){0,0}, 0.0f, WHITE);
                  EndDrawing();
              } else { EndDrawing(); } }

            if (cdRemain <= 0.0) {
                if (play.haveMusic) { PlayMusicStream(play.music); SetMusicVolume(play.music, gMusicVolume); }
                play.clockMs = 0.0;
                if (gMode == MODE_PRACTICE) play_seek(&play, gPracticeA);  /* demarrer a l'ancre */
                /* Comptabilise le temps de la partie precedente (cas restart KEY_R) */
                if (sPlayStartTime > 0.0)
                    gProfile.totalPlayTimeSec += (float)(GetTime() - sPlayStartTime);
                gProfile.totalRuns++;
                sPlayStartTime = GetTime();
                sStatsSaved    = false;
                if (gSettings.bgStyle == BG_AURORA) bg_aurora_init(sw, sh);
                if (gSettings.bgStyle == BG_PULSE)  bg_pulse_reset();
                screen = SCR_PLAYING;
            }
        }
        else { /* SCR_PLAYING */
            if (IsKeyPressed(gKeys.quit)) {
                go_menu(&play, &screen);
                BeginDrawing(); ClearBackground((Color){ 10, 10, 16, 255 }); EndDrawing();
                continue;
            }
            if (IsKeyPressed(gKeys.pause) && !play.finished) {
                if (!play.paused) {
                    play.paused = true;
                    play.unpauseCountdown = 0.0f;
                    if (play.haveMusic) PauseMusicStream(play.music);
                } else if (play.unpauseCountdown <= 0.0f) {
                    play.unpauseCountdown = 3.0f;  /* lance le décompte */
                } else {
                    play.unpauseCountdown = 0.0f;  /* annule le décompte */
                }
            }
            /* Décompte de dépause */
            if (play.paused && play.unpauseCountdown > 0.0f && !play.finished) {
                play.unpauseCountdown -= dt;
                if (play.unpauseCountdown <= 0.0f) {
                    play.unpauseCountdown = 0.0f;
                    play.paused = false;
                    if (play.haveMusic) ResumeMusicStream(play.music);
                }
            }
            if (IsKeyPressed(gKeys.restart)) {
                if (gMode == MODE_LADDER) {
                    gLadderLevel = 0; gLadderRate = 1.0f;
                    gRate = 1.0f; gScoreMult = mods_score_mult(gMods, gRate);
                    if (play.haveMusic) SetMusicPitch(play.music, gRate);
                }
                play_reset(&play);
                cdRemain = COUNTDOWN_SEC;
                screen = SCR_COUNTDOWN;
                continue;
            }

            /* Skip intro : saute 1250 ms avant la premiere note */
            if (!autoplay && IsKeyPressed(gKeys.skipIntro) && !play.paused && !play.finished && play.N > 0) {
                /* targetNowMs : position musique qui correspond a "1250ms avant la 1ere note" */
                float targetNowMs = play.map.notes[0].ms - 1250.0f + gAudioOffsetMs;
                if (targetNowMs > play.nowMs + 500.0f) {  /* n'agit que si le gain est reel */
                    if (play.haveMusic)
                        SeekMusicStream(play.music, targetNowMs > 0.0f ? targetNowMs / 1000.0f : 0.0f);
                    play.clockMs = (double)targetNowMs;
                    play.nowMs   = targetNowMs;
                    /* reinit trainee + particules (devenues incoherentes apres le saut) */
                    play.trailHead = play.trailCount = 0;
                    play.trailLastX = play.cx; play.trailLastY = play.cy;
                    for (int _i = 0; _i < MAX_PARTICLES; _i++) play.parts[_i].life = 0.0f;
                    play.pulse = 0.0f; play.hitFlash = 0.0f;
                }
            }

            /* Mode Entrainement : seek (<-/->), marqueurs de boucle ([ / ]), boucle (L), vitesse (-/+) */
            if (gMode == MODE_PRACTICE && !play.paused && !play.finished) {
                if (IsKeyPressed(KEY_LEFT))  play_seek(&play, play.nowMs - 2000.0f);
                if (IsKeyPressed(KEY_RIGHT)) {
                    float t  = play.nowMs + 2000.0f;
                    float mx = (float)(play.map.lastMs > 0 ? play.map.lastMs : 0);
                    if (mx > 0.0f && t > mx) t = mx;
                    play_seek(&play, t);
                }
                if (IsKeyPressed(KEY_LEFT_BRACKET)) {        /* [ : poser A = position courante */
                    gPracticeA = play.nowMs;
                    if (gPracticeB < gPracticeA + 1.0f) gPracticeB = 0.0f;  /* B devenu invalide */
                }
                if (IsKeyPressed(KEY_RIGHT_BRACKET)) {       /* ] : poser B = position courante */
                    if (play.nowMs > gPracticeA + 1.0f) gPracticeB = play.nowMs;
                }
                if (IsKeyPressed(KEY_L)) gPracticeLoop = !gPracticeLoop;   /* L : activer/couper la boucle */
                bool spdDown = IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT);
                bool spdUp   = IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD);
                if (spdDown || spdUp) {
                    gRate = clampf(gRate + (spdUp ? 0.05f : -0.05f), 0.25f, 2.0f);
                    gScoreMult = mods_score_mult(gMods, gRate);
                    if (play.haveMusic) SetMusicPitch(play.music, gRate);
                }
            }

            play_update(&play, autoplay, dt, sw, sh);

            /* Speed Ladder : map reussie -> on accelere et on rejoue (jusqu'a la mort) */
            if (gMode == MODE_LADDER && play.finished && !play.gameOver) {
                gLadderLevel++;
                gLadderRate += 0.10f; if (gLadderRate > 3.0f) gLadderRate = 3.0f;
                gRate = gLadderRate; gScoreMult = mods_score_mult(gMods, gRate);
                play_reset(&play);
                if (play.haveMusic) SetMusicPitch(play.music, gRate);
                cdRemain = COUNTDOWN_SEC;
                screen = SCR_COUNTDOWN;
                continue;
            }

            /* enregistrement du meilleur score au 1er frame de fin (parties normales + aim) */
            if (play.finished && !play.gameOver && !play.scoreSaved && gCurrentMap[0] &&
                (gMode == MODE_NORMAL || gMode == MODE_AIM)) {
                play.scoreSaved = true;
                gNewRecord = profile_update_best(gCurrentMap, play.score,
                                                  play.hits, play.hits + play.misses,
                                                  play.maxCombo);
                if (gNewRecord) profile_save();
            }
            /* statistiques cumulees (toutes fins de partie, y compris game over) */
            if (play.finished && !sStatsSaved) {
                sStatsSaved = true;
                gProfile.allTimeHits       += play.hits;
                gProfile.allTimeTotalNotes += play.hits + play.misses;
                profile_save();
            }

            { int rtw = gRtW > 0 ? gRtW : sw, rth = gRtH > 0 ? gRtH : sh;
              if (gRtW > 0) BeginTextureMode(gRenderTex); else BeginDrawing();
              play_draw_scene(&play, cam, autoplay);
              play_draw_hud(&play, rtw, rth, autoplay);
              if (gRtW > 0) {
                  EndTextureMode();
                  BeginDrawing(); ClearBackground(BLACK);
                  DrawTexturePro(gRenderTex.texture,
                      (Rectangle){0,0,(float)gRtW,-(float)gRtH},
                      (Rectangle){0,0,(float)sw,(float)sh},
                      (Vector2){0,0}, 0.0f, WHITE);
                  EndDrawing();
              } else { EndDrawing(); } }
        }
    }

    settings_save(&gSettings);
    profile_save();
    if (gLoader.threadStarted) athread_join(&gLoader);
    menu_scan_join(&gMenuScanner);
    play_unload(&play);
    notemesh_clear();
    cursortex_clear();
    hitsound_clear();
    if (calib.haveClick) UnloadSound(calib.click);
    if (gHaveCover) UnloadTexture(gCoverTex);
    bg_unload_all();
    UnloadTexture(gWhiteTex);
    cursor_halo_unload();
    if (gRtW > 0) UnloadRenderTexture(gRenderTex);
    free(menu.items);
    free(menu.filtered);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}