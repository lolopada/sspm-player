#include "common.h"
#include "filepicker.h"

/* --- onglets du menu Options --- */
static const char *const OPT_TAB_NAMES[N_TABS] = { "Gameplay", "Visual", "Controls", "System", "Trail", "Cursor", "Keys" };

/* OPT_TABS[onglet][ligne_locale] = indice global du reglage affiche.
 * -1 = fin de la liste de l'onglet (les lignes juice, trainee et curseur sont
 * ajoutees dynamiquement par build_opt_rows selon juiceMode / cursorMode /
 * trailEnabled).
 *
 * Table des indices globaux (= indice dans le tableau labels[] de settings_draw) :
 *  0  distance d'apparition         20  resolution interne
 *  1  vitesse (approachMs)           21  offset audio
 *  2  tablette graphique             22  effets visuels (juice mode)
 *  3  palette de couleurs            23  particules : quantite
 *  4  forme des notes (mesh)         24  particules : duree
 *  5  curseur image (PNG)            25  particules : taille
 *  6  mode dieu                      26  particules : vitesse
 *  7  tolerance de timing            27  pulse : intensite
 *  8  teinte (hue shift)             28  combo : palier flash
 *  9  sensibilite souris             33  volume musique
 * 10  vsync                          34  style de grille
 * 16  opacite grille
 * 17  taille notes              --- CURSEUR (genere) ---     --- TRAINEE ---
 * 18  hitsound (fichier)        35  mode (genere/image)      42  activer
 * 19  volume hitsound           36  couleur (teinte)         43  duree
 *                               37  taille du coeur          44  segments max
 *                               38  intensite du coeur       45  espacement (px)
 *                               39  taille du halo           46  echelle depart
 *                               40  intensite du halo        47  echelle fin
 *                               41  additif (lumiere)        48  opacite depart
 *                                                            49  opacite fin
 *                                                            50  suivre couleur curseur
 *                                                            51  couleur trainee
 * (indices 11-15 et 29-32 : libres, ancien systeme curseur/trainee supprime.)
 *
 * IMPORTANT : si on ajoute un reglage, lui attribuer le prochain indice libre,
 * l'ajouter dans labels[] (settings_draw), le cas dans le switch de settings_update,
 * et le mentionner ici + dans l'onglet concerne. */
static const int OPT_TABS[N_TABS][8] = {
    { 0, 1, 7,21, 6,-1,-1,-1 },   /* Gameplay    : dist, vitesse, timing, offset, god */
    { 3, 8, 4,16,34,17,22,80 },   /* Visuel      : palette, teinte, forme, grille, style, notes, effets, bg */
    { 2, 9,-1,-1,-1,-1,-1,-1 },   /* Controles   : tablette, sensibilite */
    {10,33,18,19,20,77,-1,-1 },   /* Systeme     : vsync, volume musique, hitsound, res, dossier maps */
    {42,-1,-1,-1,-1,-1,-1,-1 },   /* Trainee     : activer + sous-options dynamiques */
    {82,35,-1,-1,-1,-1,-1,-1 },   /* Curseur     : visible menu + mode + sous-options */
    {-1,-1,-1,-1,-1,-1,-1,-1 },   /* Touches     : tout ajoute dynamiquement (60-76) */
};

static int build_opt_rows(int tab, int *rows) {
    int n = 0;
    for (int i = 0; i < 8; i++) { int g = OPT_TABS[tab][i]; if (g >= 0) rows[n++] = g; }
    if (tab == 1) {
        if (gJuiceMode == 1 || gJuiceMode == 3) { rows[n++] = 23; rows[n++] = 24; rows[n++] = 25; rows[n++] = 26; }
        if (gJuiceMode == 2 || gJuiceMode == 3) { rows[n++] = 27; rows[n++] = 28; }
        if (gSettings.bgStyle != BG_NONE) rows[n++] = 81;   /* intensite : visible seulement si un fond est actif */
        /* HUD configurable */
        for (int g = 83; g <= 87; g++) rows[n++] = g;
    }
    if (tab == 4) {   /* Trainee : sous-options visibles seulement si activee */
        if (gSettings.cursor.trailEnabled) {
            rows[n++] = 45;                       /* espacement : le reglage cle anti-paté */
            rows[n++] = 43; rows[n++] = 44;       /* duree, segments max */
            for (int g = 46; g <= 50; g++) rows[n++] = g;
            if (!gSettings.cursor.trailUseCursorColor) rows[n++] = 51;
        }
    }
    if (tab == 5) {   /* Curseur : image -> fichier ; genere -> apparence */
        if (gSettings.cursorMode == 1) rows[n++] = 5;
        else for (int g = 36; g <= 41; g++) rows[n++] = g;
    }
    if (tab == 6) {   /* Touches : 4 en jeu + 13 menu */
        for (int g = 60; g <= 76; g++) rows[n++] = g;
    }
    return n;
}
int opt_selected_global(void) {
    int rows[MAX_OPT_ROWS];
    int n = build_opt_rows(gOptTab, rows);
    return (gOptSel >= 0 && gOptSel < n) ? rows[gOptSel] : -1;
}

/* Fait tourner la teinte d'une couleur en gardant un rendu pastel (saturation
 * moderee, valeur pleine) qui colle a l'esthetique douce du curseur. */
static Color step_pastel_hue(Color c, int step) {
    float h, s, v; rgb_to_hsv(c, &h, &s, &v);
    h = fmodf(h + (float)step * 5.0f, 360.0f); if (h < 0.0f) h += 360.0f;
    return hsv_to_rgb(h, 0.55f, 1.0f, 255);
}
/* Teinte (degres) d'une couleur, pour l'affichage de la valeur. */
static int color_hue_deg(Color c) {
    float h, s, v; rgb_to_hsv(c, &h, &s, &v);
    return (int)(h + 0.5f);
}

/* Renvoie le nom affichable d'une touche raylib. */
static const char *key_name(int k) {
    switch (k) {
        case KEY_SPACE:     return "Space";
        case KEY_ESCAPE:    return "Escape";
        case KEY_ENTER:     return "Enter";
        case KEY_TAB:       return "Tab";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_INSERT:    return "Insert";
        case KEY_DELETE:    return "Delete";
        case KEY_RIGHT:     return "Right";
        case KEY_LEFT:      return "Left";
        case KEY_DOWN:      return "Down";
        case KEY_UP:        return "Up";
        case KEY_PAGE_UP:   return "Page Up";
        case KEY_PAGE_DOWN: return "Page Down";
        case KEY_HOME:      return "Home";
        case KEY_END:       return "End";
        case KEY_CAPS_LOCK: return "Caps Lock";
        case KEY_F1:  return "F1";  case KEY_F2:  return "F2";  case KEY_F3:  return "F3";
        case KEY_F4:  return "F4";  case KEY_F5:  return "F5";  case KEY_F6:  return "F6";
        case KEY_F7:  return "F7";  case KEY_F8:  return "F8";  case KEY_F9:  return "F9";
        case KEY_F10: return "F10"; case KEY_F11: return "F11"; case KEY_F12: return "F12";
        case KEY_KP_0: return "Num 0"; case KEY_KP_1: return "Num 1"; case KEY_KP_2: return "Num 2";
        case KEY_KP_3: return "Num 3"; case KEY_KP_4: return "Num 4"; case KEY_KP_5: return "Num 5";
        case KEY_KP_6: return "Num 6"; case KEY_KP_7: return "Num 7"; case KEY_KP_8: return "Num 8";
        case KEY_KP_9: return "Num 9";
        case KEY_KP_DECIMAL:  return "Num .";  case KEY_KP_DIVIDE:   return "Num /";
        case KEY_KP_MULTIPLY: return "Num *";  case KEY_KP_SUBTRACT: return "Num -";
        case KEY_KP_ADD:      return "Num +";  case KEY_KP_ENTER:    return "Num Enter";
        case KEY_LEFT_SHIFT:  return "L Shift"; case KEY_LEFT_CONTROL: return "L Ctrl";
        case KEY_LEFT_ALT:    return "L Alt";
        case KEY_RIGHT_SHIFT: return "R Shift"; case KEY_RIGHT_CONTROL: return "R Ctrl";
        case KEY_RIGHT_ALT:   return "R Alt";
        case KEY_APOSTROPHE:  return "'";  case KEY_COMMA:  return ",";
        case KEY_MINUS:       return "-";  case KEY_PERIOD: return ".";
        case KEY_SLASH:       return "/";  case KEY_SEMICOLON: return ";";
        case KEY_EQUAL:       return "=";  case KEY_LEFT_BRACKET:  return "[";
        case KEY_BACKSLASH:   return "\\"; case KEY_RIGHT_BRACKET: return "]";
        case KEY_GRAVE:       return "`";
        /* A-Z */
        case 65: return "A"; case 66: return "B"; case 67: return "C"; case 68: return "D";
        case 69: return "E"; case 70: return "F"; case 71: return "G"; case 72: return "H";
        case 73: return "I"; case 74: return "J"; case 75: return "K"; case 76: return "L";
        case 77: return "M"; case 78: return "N"; case 79: return "O"; case 80: return "P";
        case 81: return "Q"; case 82: return "R"; case 83: return "S"; case 84: return "T";
        case 85: return "U"; case 86: return "V"; case 87: return "W"; case 88: return "X";
        case 89: return "Y"; case 90: return "Z";
        /* 0-9 */
        case 48: return "0"; case 49: return "1"; case 50: return "2"; case 51: return "3";
        case 52: return "4"; case 53: return "5"; case 54: return "6"; case 55: return "7";
        case 56: return "8"; case 57: return "9";
        default: { static char buf[16]; snprintf(buf, sizeof buf, "Key %d", k); return buf; }
    }
}

void settings_update(int sw, int sh) {
    /* === Mode saisie directe de la sensibilite === */
    if (gSensEditing) {
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            int len = (int)strlen(gSensEditBuf);
            if ((ch >= '0' && ch <= '9') ||
                (ch == '.' && !strchr(gSensEditBuf, '.')) ||
                (ch == ',' && !strchr(gSensEditBuf, '.'))) {
                char ins = (ch == ',') ? '.' : (char)ch;
                if (len < 30) { gSensEditBuf[len] = ins; gSensEditBuf[len + 1] = '\0'; }
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) && gSensEditBuf[0]) {
            int len = (int)strlen(gSensEditBuf);
            gSensEditBuf[len - 1] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            float v = (float)atof(gSensEditBuf);
            if (v > 0.001f) {
                gSettings.sensMultiplier = clampf(v, 0.01f, 20.0f);
                settings_apply(&gSettings);
            }
            gSensEditing = false;
        }
        if (IsKeyPressed(KEY_ESCAPE)) gSensEditing = false;
        return;
    }

    /* === Mode capture de touche === */
    if (gKeyCapture) {
        int k = GetKeyPressed();
        if (k == KEY_ESCAPE) {
            gKeyCapture = false;
        } else if (k != 0) {
            int *target = NULL;
            if      (gKeyCaptureSlot == 60) target = &gSettings.keys.quit;
            else if (gKeyCaptureSlot == 61) target = &gSettings.keys.pause;
            else if (gKeyCaptureSlot == 62) target = &gSettings.keys.restart;
            else if (gKeyCaptureSlot == 63) target = &gSettings.keys.skipIntro;
            else if (gKeyCaptureSlot == 64) target = &gSettings.keys.menuSettings;
            else if (gKeyCaptureSlot == 65) target = &gSettings.keys.menuModes;
            else if (gKeyCaptureSlot == 66) target = &gSettings.keys.menuProfile;
            else if (gKeyCaptureSlot == 67) target = &gSettings.keys.menuPlay;
            else if (gKeyCaptureSlot == 68) target = &gSettings.keys.menuFavorite;
            else if (gKeyCaptureSlot == 69) target = &gSettings.keys.menuBan;
            else if (gKeyCaptureSlot == 70) target = &gSettings.keys.menuHideBanned;
            else if (gKeyCaptureSlot == 71) target = &gSettings.keys.menuFavsOnly;
            else if (gKeyCaptureSlot == 72) target = &gSettings.keys.menuNewOnly;
            else if (gKeyCaptureSlot == 73) target = &gSettings.keys.menuDiffFilter;
            else if (gKeyCaptureSlot == 74) target = &gSettings.keys.menuSortStar;
            else if (gKeyCaptureSlot == 75) target = &gSettings.keys.menuCycleSort;
            else if (gKeyCaptureSlot == 76) target = &gSettings.keys.menuRescan;
            if (target) { *target = k; settings_apply(&gSettings); settings_save(&gSettings); }
            gKeyCapture = false;
        }
        return;
    }

    int rows[MAX_OPT_ROWS];
    int nOpts = build_opt_rows(gOptTab, rows);

    /* Geometrie sidebar + panneau (doit coller a settings_draw). */
    int sbX = 40, sbY = 96, sbW = 200, catH = 46;
    int pX = sbX + sbW + 30, pY = sbY, rowH = 44;
    int pBottom = sh - 76;
    int maxVis = (pBottom - pY) / rowH; if (maxVis < 1) maxVis = 1;
    int pW = sw - pX - 40;
    int scroll = 0;
    if (nOpts > maxVis) {
        scroll = gOptSel - maxVis / 2;
        if (scroll < 0) scroll = 0;
        if (scroll > nOpts - maxVis) scroll = nOpts - maxVis;
    }

    /* Changement de categorie (Tab / Maj+Tab) */
    if (IsKeyPressed(KEY_TAB)) {
        int dir = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? -1 : 1;
        gOptTab = (gOptTab + dir + N_TABS) % N_TABS;
        gOptSel = 0;
        nOpts = build_opt_rows(gOptTab, rows);
    }

    if (IsKeyPressed(KEY_DOWN)) gOptSel = (gOptSel + 1) % nOpts;
    if (IsKeyPressed(KEY_UP))   gOptSel = (gOptSel + nOpts - 1) % nOpts;

    int step = 0;
    if (IsKeyPressed(KEY_RIGHT)) step = 1;
    if (IsKeyPressed(KEY_LEFT))  step = -1;
    float wheel = GetMouseWheelMove();
    if (wheel != 0) step = (wheel > 0) ? 1 : -1;
    if (IsKeyPressed(KEY_ENTER) && step == 0) step = 1;

    Vector2 mp = GetMousePosition();

    /* Clic sur une categorie de la sidebar */
    bool catClicked = false;
    for (int ti = 0; ti < N_TABS && !catClicked; ti++) {
        Rectangle cr = { (float)sbX, (float)(sbY + ti * catH), (float)sbW, (float)(catH - 6) };
        if (CheckCollisionPointRec(mp, cr) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            gOptTab = ti; gOptSel = 0;
            nOpts = build_opt_rows(gOptTab, rows);
            catClicked = true;
        }
    }

    /* Survol / clic sur une ligne visible du panneau */
    if (!catClicked) {
        int last = scroll + maxVis; if (last > nOpts) last = nOpts;
        for (int r = scroll; r < last; r++) {
            int ry = pY + (r - scroll) * rowH;
            Rectangle rr = { (float)pX, (float)ry, (float)pW, (float)(rowH - 6) };
            if (CheckCollisionPointRec(mp, rr)) {
                gOptSel = r;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    /* clic dans la zone "< valeur >" : moitie gauche = baisser, droite = monter ;
                     * ailleurs sur la ligne = +1 (pratique pour les bascules Oui/Non). */
                    if (gOptCtrl.width > 0 && CheckCollisionPointRec(mp, gOptCtrl))
                        step = (mp.x < gOptCtrl.x + gOptCtrl.width * 0.5f) ? -1 : 1;
                    else
                        step = 1;
                }
            }
        }
    }

    if (step != 0) {
        int globalR = (gOptSel >= 0 && gOptSel < nOpts) ? rows[gOptSel] : -1;
        switch (globalR) {
            case 0: gSettings.approachDist = clampf(gSettings.approachDist + step * 5.0f, 5.0f, 120.0f); break;
            case 1: gSettings.approachMs   = clampf(gSettings.approachMs   + step * 50.0f, 200.0f, 1500.0f); break;
            case 2: gSettings.tablet = !gSettings.tablet; break;
            case 3: {
                /* cycle : built-ins (0..NPALETTE-1) + custom (NPALETTE) + chaines fichier (NPALETTE+1..) */
                int total = NPALETTE + 1 + gColorChainCount;
                int curPos = gSettings.paletteIdx;
                if (curPos > NPALETTE && gColorChainCount > 0) {
                    int ci = colorchain_index_of(gSettings.colorChainFile);
                    curPos = (ci >= 0) ? NPALETTE + 1 + ci : NPALETTE + 1;
                }
                int newPos = ((curPos + step) % total + total) % total;
                if (newPos <= NPALETTE) {
                    gSettings.paletteIdx = newPos;
                } else {
                    gSettings.paletteIdx = NPALETTE + 1;
                    int ci = newPos - (NPALETTE + 1);
                    if (ci >= 0 && ci < gColorChainCount)
                        snprintf(gSettings.colorChainFile, sizeof gSettings.colorChainFile, "%s", gColorChains[ci].filename);
                }
            } break;
            case 4: {
                int cnt = gMeshCount + 1;
                int cur = mesh_index_of(gSettings.meshName);
                int ni  = ((cur + step) % cnt + cnt) % cnt;
                const char *nm = mesh_name_at(ni);
                snprintf(gSettings.meshName, sizeof gSettings.meshName, "%s", nm);
                if (nm[0] && !notemesh_set(nm)) { gSettings.meshName[0] = '\0'; notemesh_set(""); }
            } break;
            case 5: {
                int cnt = gCursorCount + 1;
                int cur = cursor_index_of(gSettings.cursorName);
                int ni  = ((cur + step) % cnt + cnt) % cnt;
                const char *nm = cursor_name_at(ni);
                snprintf(gSettings.cursorName, sizeof gSettings.cursorName, "%s", nm);
                if (nm[0] && !cursortex_set(nm)) { gSettings.cursorName[0] = '\0'; cursortex_set(""); }
            } break;
            case 6: gSettings.godMode = !gSettings.godMode; break;
            case 7: gSettings.hitWindowMs = clampf(gSettings.hitWindowMs + step * 5.0f, 20.0f, 100.0f); break;
            case 8: { int t2 = 360; gSettings.hueShift = ((gSettings.hueShift + step * 5) % t2 + t2) % t2; } break;
            case 9: {
                /* sensibilite : Entree ouvre la saisie directe ; gauche/droite ignorees */
                if (IsKeyPressed(KEY_ENTER)) {
                    snprintf(gSensEditBuf, sizeof gSensEditBuf, "%.2f", gSettings.sensMultiplier);
                    gSensEditing = true;
                }
            } break;
            case 10: gSettings.vsync = !gSettings.vsync; break;
            case 16: { int tv = gSettings.gridAlpha + step * 10; gSettings.gridAlpha = tv < 0 ? 0 : (tv > 255 ? 255 : tv); } break;
            case 34: { int t2 = 4; gSettings.gridStyle = ((gSettings.gridStyle + step) % t2 + t2) % t2; } break;
            case 17: gSettings.noteScale = clampf(gSettings.noteScale + step * 0.05f, 0.5f, 1.5f); break;
            case 18: {
                int cnt = gHitsoundCount + 1;
                int cur = hitsound_index_of(gSettings.hitsoundName);
                int ni  = ((cur + step) % cnt + cnt) % cnt;
                const char *nm = hitsound_name_at(ni);
                snprintf(gSettings.hitsoundName, sizeof gSettings.hitsoundName, "%s", nm);
                if (nm[0] && !hitsound_set(nm)) { gSettings.hitsoundName[0] = '\0'; hitsound_set(""); }
                else if (!nm[0]) hitsound_set("");
            } break;
            case 19: gSettings.hitsoundVolume = clampf(gSettings.hitsoundVolume + step * 0.05f, 0.0f, 1.0f); break;
            case 20: { int t2 = 3; gSettings.internalRes = ((gSettings.internalRes + step) % t2 + t2) % t2; } break;
            case 21: gSettings.audioOffsetMs = clampf(gSettings.audioOffsetMs + step * 5.0f, -300.0f, 300.0f); break;
            case 22: { int t2 = 4; gSettings.juiceMode = ((gSettings.juiceMode + step) % t2 + t2) % t2; } break;
            case 23: { int v = gSettings.juiceCount + step * 2; gSettings.juiceCount = v < 2 ? 2 : (v > 60 ? 60 : v); } break;
            case 24: gSettings.juiceLife  = clampf(gSettings.juiceLife  + step * 0.05f, 0.20f, 1.50f); break;
            case 25: gSettings.juiceSize  = clampf(gSettings.juiceSize  + step * 0.01f, 0.05f, 0.40f); break;
            case 26: gSettings.juiceSpeed = clampf(gSettings.juiceSpeed + step * 0.10f, 0.30f, 2.50f); break;
            case 27: gSettings.juicePulse = clampf(gSettings.juicePulse + step * 0.10f, 0.20f, 2.00f); break;
            case 28: { static const int cp[4] = { 0, 25, 50, 100 }; int idx = 0;
                       for (int q = 0; q < 4; q++) if (cp[q] == gSettings.juiceCombo) idx = q;
                       idx = ((idx + step) % 4 + 4) % 4; gSettings.juiceCombo = cp[idx]; } break;
            case 33: gSettings.musicVolume = clampf(gSettings.musicVolume + step * 0.05f, 0.0f, 1.0f); break;
            /* --- Curseur (mode + apparence du curseur genere) --- */
            case 35: {
                int t2 = 2;
                gSettings.cursorMode = ((gSettings.cursorMode + step) % t2 + t2) % t2;
                gCursorMode = gSettings.cursorMode;
                if (gSettings.cursorMode == 1) { if (gSettings.cursorName[0]) cursortex_set(gSettings.cursorName); }
                else cursortex_clear();
                gOptSel = 0;  /* nombre de lignes change selon le mode */
            } break;
            case 36: gSettings.cursor.color = step_pastel_hue(gSettings.cursor.color, step); break;
            case 37: gSettings.cursor.size = clampf(gSettings.cursor.size + step * 1.0f, 4.0f, 40.0f); break;
            case 38: gSettings.cursor.coreIntensity = clampf(gSettings.cursor.coreIntensity + step * 0.05f, 0.0f, 1.0f); break;
            case 39: gSettings.cursor.glowSize = clampf(gSettings.cursor.glowSize + step * 0.2f, 1.0f, 6.0f); break;
            case 40: gSettings.cursor.glowIntensity = clampf(gSettings.cursor.glowIntensity + step * 0.05f, 0.0f, 1.0f); break;
            case 41: gSettings.cursor.additive = !gSettings.cursor.additive; break;
            /* --- Trainee --- */
            case 42: gSettings.cursor.trailEnabled = !gSettings.cursor.trailEnabled; gOptSel = 0; break;
            case 43: gSettings.cursor.trailDuration = clampf(gSettings.cursor.trailDuration + step * 0.05f, 0.05f, 2.0f); break;
            case 44: { int tv = gSettings.cursor.trailMaxSegments + step * 4; gSettings.cursor.trailMaxSegments = tv < 4 ? 4 : (tv > TRAIL_MAX ? TRAIL_MAX : tv); } break;
            case 45: gSettings.cursor.trailSpacing = clampf(gSettings.cursor.trailSpacing + step * 1.0f, 1.0f, 40.0f); break;
            case 46: gSettings.cursor.trailStartScale = clampf(gSettings.cursor.trailStartScale + step * 0.1f, 0.2f, 2.0f); break;
            case 47: gSettings.cursor.trailEndScale = clampf(gSettings.cursor.trailEndScale + step * 0.05f, 0.0f, 1.0f); break;
            case 48: gSettings.cursor.trailStartAlpha = clampf(gSettings.cursor.trailStartAlpha + step * 0.05f, 0.0f, 1.0f); break;
            case 49: gSettings.cursor.trailEndAlpha = clampf(gSettings.cursor.trailEndAlpha + step * 0.05f, 0.0f, 1.0f); break;
            case 50: gSettings.cursor.trailUseCursorColor = !gSettings.cursor.trailUseCursorColor; break;
            case 51: gSettings.cursor.trailColor = step_pastel_hue(gSettings.cursor.trailColor, step); break;
            /* --- Touches configurables --- */
            case 60: case 61: case 62: case 63:
            case 64: case 65: case 66: case 67: case 68: case 69: case 70:
            case 71: case 72: case 73: case 74: case 75: case 76:
                /* n'entre en capture que sur Entree ou clic direct, pas sur molette/fleches */
                if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    gKeyCapture = true;
                    gKeyCaptureSlot = globalR;
                }
                break;
            case 77: {
                if (step > 0) {
                    char newDir[512] = { 0 };
                    if (pick_folder(newDir, sizeof newDir, "Choisir le dossier de maps")) {
                        snprintf(gSettings.mapsDir, sizeof gSettings.mapsDir, "%s", newDir);
                        gMapsDirRescan = true;
                    }
                } else if (step < 0) {
                    gSettings.mapsDir[0] = '\0';
                    gMapsDirRescan = true;
                }
            } break;
            /* --- Background --- */
            case 80: {
                int t2 = BG_COUNT;
                gSettings.bgStyle = (BgStyle)(((int)gSettings.bgStyle + step % t2 + t2) % t2);
                bg_init(gSettings.bgStyle, sw, sh);
                gOptSel = 0;
            } break;
            case 81: gSettings.bgIntensity = clampf(gSettings.bgIntensity + step * 0.05f, 0.0f, 1.0f); break;
            case 82:
                gSettings.cursorInMenu = !gSettings.cursorInMenu;
                if (gSettings.cursorInMenu) HideCursor(); else ShowCursor();
                break;
            case 83: gSettings.hudShowScore    = !gSettings.hudShowScore;    break;
            case 84: gSettings.hudShowCombo    = !gSettings.hudShowCombo;    break;
            case 85: gSettings.hudShowAccuracy = !gSettings.hudShowAccuracy; break;
            case 86: gSettings.hudShowHp       = !gSettings.hudShowHp;       break;
            case 87: gSettings.hudShowSongInfo = !gSettings.hudShowSongInfo; break;
            default: break;
        }
        if (globalR != 9 && !(globalR >= 60 && globalR <= 76) && globalR != 77
            && globalR != 80)
            settings_apply(&gSettings);
    }
}

/* Apercu inline d'un reglage : ancre a droite sur rightX, centre verticalement
 * dans la ligne [ry, ry+rowInner].  Centralise tous les mini-apercus.          */
static void settings_preview(int globalR, bool sel, int rightX, int ry, int rowInner) {
    int h = 18, cy = ry + (rowInner - h) / 2;
    if (globalR == 3 || globalR == 8) {                     /* palette / teinte */
        Color pc[MAX_PALETTE]; int pn; settings_palette(&gSettings, pc, &pn);
        int swc = 16, gap = 3, totalW = pn * (swc + gap) - gap, x0 = rightX - totalW;
        for (int i = 0; i < pn && i < MAX_PALETTE; i++) {
            Color c = pc[i];
            if (globalR == 8 && gHueShift != 0) {
                float hh, ss, vv; rgb_to_hsv(c, &hh, &ss, &vv);
                hh = fmodf(hh + (float)gHueShift, 360.0f); if (hh < 0.0f) hh += 360.0f;
                c = hsv_to_rgb(hh, ss, vv, c.a);
            }
            DrawRectangle(x0 + i * (swc + gap), cy, swc, h, c);
        }
        return;
    }
    if (globalR == 33) {                                     /* volume musique : barre palette toujours visible */
        Color pc[MAX_PALETTE]; int pn; settings_palette(&gSettings, pc, &pn);
        int bw = 160, bh = 10, x0 = rightX - bw, by = ry + (rowInner - bh) / 2;
        DrawRectangle(x0, by, bw, bh, (Color){ 22, 22, 38, 255 });
        float frac = clampf(gSettings.musicVolume, 0.0f, 1.0f);
        int fw = (int)(bw * frac);
        if (fw > 0) {
            Color c1 = (pn > 0) ? pc[0] : (Color){ 90, 200, 255, 220 };
            Color c2 = (pn > 1) ? pc[(pn - 1) % (size_t)pn] : c1;
            /* degrade : gauche = teinte foncee, droite = teinte vive */
            Color c0 = (Color){ (unsigned char)(c1.r / 3), (unsigned char)(c1.g / 3),
                                (unsigned char)(c1.b / 3), 220 };
            DrawRectangleGradientH(x0, by, fw, bh, c0, c2);
        }
        DrawRectangleLinesEx((Rectangle){ (float)x0, (float)by, (float)bw, (float)bh },
                              1.0f, (Color){ 55, 58, 90, 200 });
        return;
    }
    if (!sel) return;                                       /* le reste : ligne active */
    if (globalR == 4) {                                      /* forme des notes : mini-preview 3D */
        static RenderTexture2D rt = { 0 };
        int psz = rowInner;
        if ((int)rt.texture.width != psz) {
            if (rt.id != 0) UnloadRenderTexture(rt);
            rt = LoadRenderTexture(psz, psz);
        }
        Color pc[MAX_PALETTE]; int pn;
        settings_palette(&gSettings, pc, &pn);
        Color nc = (pn > 0) ? pc[0] : (Color){ 255, 90, 140, 255 };
        float sc = gSettings.noteScale;
        float angle = fmodf((float)GetTime() * 40.0f, 360.0f);
        BeginTextureMode(rt);
        ClearBackground((Color){ 14, 14, 24, 255 });
        Camera3D cam = {
            .position   = { 2.2f, 1.4f, 2.2f },
            .target     = { 0.0f, 0.0f, 0.0f },
            .up         = { 0.0f, 1.0f, 0.0f },
            .fovy       = 45.0f,
            .projection = CAMERA_PERSPECTIVE
        };
        BeginMode3D(cam);
        if (gNoteMesh.active) {
            float ms = gNoteMesh.scale * sc;
            DrawModelEx(gNoteMesh.model,
                        (Vector3){ 0, 0, 0 },
                        (Vector3){ 0, 1, 0 }, angle,
                        (Vector3){ ms, ms, ms }, nc);
        } else {
            float side = CUBE_SIZE * sc;
            DrawCubeV((Vector3){ 0, 0, 0 }, (Vector3){ side, side, side }, nc);
            DrawCubeWiresV((Vector3){ 0, 0, 0 },
                           (Vector3){ side + 0.05f, side + 0.05f, side + 0.05f },
                           (Color){ 255, 255, 255, 60 });
        }
        EndMode3D();
        EndTextureMode();
        int px = rightX - psz - 4, py = ry;
        DrawRectangle(px - 2, py - 2, psz + 4, psz + 4, (Color){ 20, 22, 40, 255 });
        DrawTextureRec(rt.texture,
                       (Rectangle){ 0, 0, (float)psz, -(float)psz },
                       (Vector2){ (float)px, (float)py }, WHITE);
        DrawRectangleLinesEx((Rectangle){ (float)(px - 2), (float)(py - 2),
                                          (float)(psz + 4), (float)(psz + 4) },
                              1.0f, (Color){ 55, 65, 100, 200 });
        return;
    }
    if (globalR == 35) {                                     /* mode curseur : mini icone */
        int cx2 = rightX - 16, cy2 = ry + rowInner / 2;
        if (gSettings.cursorMode == 1) {
            DrawRectangleLinesEx((Rectangle){ (float)(cx2 - 9), (float)(cy2 - 8), 18.0f, 16.0f }, 1.0f, (Color){ 100, 160, 255, 200 });
            DrawText("IMG", cx2 - 7, cy2 - 6, 12, (Color){ 100, 160, 255, 220 });
        } else if (gHaveHalo) {
            const CursorConfig *c = &gSettings.cursor;
            BeginBlendMode(BLEND_ADDITIVE);
            Color glow = c->color; glow.a = (unsigned char)(c->glowIntensity * 255.0f);
            DrawTexturePro(gHaloTex, (Rectangle){ 0,0,(float)gHaloTex.width,(float)gHaloTex.height },
                (Rectangle){ cx2 - 13.0f, cy2 - 13.0f, 26.0f, 26.0f }, (Vector2){ 0,0 }, 0.0f, glow);
            DrawTexturePro(gHaloTex, (Rectangle){ 0,0,(float)gHaloTex.width,(float)gHaloTex.height },
                (Rectangle){ cx2 - 6.0f, cy2 - 6.0f, 12.0f, 12.0f }, (Vector2){ 0,0 }, 0.0f, (Color){ 230,240,255,(unsigned char)(c->coreIntensity*255.0f) });
            EndBlendMode();
        }
        return;
    }
    if (globalR == 36) {                                     /* couleur curseur : pastille */
        Color col = gSettings.cursor.color; col.a = 255;
        DrawCircle(rightX - 14, ry + rowInner / 2, 9, col);
        DrawCircleLines(rightX - 14, ry + rowInner / 2, 9, (Color){ 255, 255, 255, 80 });
        return;
    }
    if (globalR >= 37 && globalR <= 41 && gHaveHalo) {       /* apercu du curseur genere */
        const CursorConfig *c = &gSettings.cursor;
        int cx2 = rightX - 26, cy2 = ry + rowInner / 2;
        float coreR = 8.0f;
        if (c->additive) BeginBlendMode(BLEND_ADDITIVE);
        Color glow = c->color; glow.a = (unsigned char)(clampf(c->glowIntensity, 0.0f, 1.0f) * 255.0f);
        float glowD = coreR * 2.0f * c->glowSize;
        DrawTexturePro(gHaloTex, (Rectangle){ 0,0,(float)gHaloTex.width,(float)gHaloTex.height },
            (Rectangle){ cx2 - glowD * 0.5f, cy2 - glowD * 0.5f, glowD, glowD }, (Vector2){ 0,0 }, 0.0f, glow);
        Color core = { (unsigned char)(c->color.r + (255 - c->color.r) * 0.65f),
                       (unsigned char)(c->color.g + (255 - c->color.g) * 0.65f),
                       (unsigned char)(c->color.b + (255 - c->color.b) * 0.65f),
                       (unsigned char)(clampf(c->coreIntensity, 0.0f, 1.0f) * 255.0f) };
        DrawTexturePro(gHaloTex, (Rectangle){ 0,0,(float)gHaloTex.width,(float)gHaloTex.height },
            (Rectangle){ cx2 - coreR, cy2 - coreR, coreR * 2.0f, coreR * 2.0f }, (Vector2){ 0,0 }, 0.0f, core);
        if (c->additive) EndBlendMode();
        return;
    }
    if (globalR >= 43 && globalR <= 51 && gHaveHalo) {       /* apercu trainee : points fondus */
        const CursorConfig *c = &gSettings.cursor;
        Color base = c->trailUseCursorColor ? c->color : c->trailColor;
        int n = 7; float st = 22.0f; int cy2 = ry + rowInner / 2;
        int x0 = rightX - (int)(n * st);
        if (c->additive) BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < n; i++) {
            float tt = 1.0f - (float)i / (float)(n - 1);     /* gauche = vieux, droite = recent */
            float sc = c->trailStartScale + (c->trailEndScale - c->trailStartScale) * tt;
            float al = c->trailStartAlpha + (c->trailEndAlpha - c->trailStartAlpha) * tt;
            if (al <= 0.003f) continue;
            float d = 20.0f * sc;
            float px = (float)x0 + (float)i * st;
            Color tc = base; tc.a = (unsigned char)(clampf(al, 0.0f, 1.0f) * 255.0f);
            DrawTexturePro(gHaloTex, (Rectangle){ 0,0,(float)gHaloTex.width,(float)gHaloTex.height },
                (Rectangle){ px - d * 0.5f, (float)cy2 - d * 0.5f, d, d }, (Vector2){ 0,0 }, 0.0f, tc);
        }
        if (c->additive) EndBlendMode();
        return;
    }
    if (globalR == 16 || globalR == 19 || globalR == 27 || globalR == 81) {  /* barres */
        int bw = 140, bh = 10, x0 = rightX - bw, by = ry + (rowInner - bh) / 2;
        DrawRectangle(x0, by, bw, bh, (Color){ 30, 30, 50, 255 });
        float frac; Color fc;
        if      (globalR == 16) { frac = gSettings.gridAlpha / 255.0f;  fc = (Color){ 200, 200, 220, 220 }; }
        else if (globalR == 19) { frac = gSettings.hitsoundVolume;      fc = (Color){ 90, 210, 255, 220 }; }
        else if (globalR == 27) { frac = gSettings.juicePulse / 2.0f;   fc = (Color){ 150, 170, 255, 230 }; }
        else                    { frac = gSettings.bgIntensity;         fc = (Color){ 29, 158, 117, 220 }; }
        frac = clampf(frac, 0.0f, 1.0f);
        DrawRectangle(x0, by, (int)(bw * frac), bh, fc);
        return;
    }
    if (globalR == 17) {                                    /* taille note */
        int sz = (int)(26.0f * gSettings.noteScale), x0 = rightX - 28;
        int yy = ry + (rowInner - sz) / 2;
        DrawRectangle(x0, yy, sz, sz, (Color){ 255, 90, 140, 180 });
        DrawRectangleLines(x0, yy, sz, sz, (Color){ 255, 255, 255, 120 });
        return;
    }
    if (globalR >= 23 && globalR <= 26) {                   /* eclats particules */
        int cx2 = rightX - 22, cy2 = ry + rowInner / 2;
        int shown = gSettings.juiceCount < 14 ? gSettings.juiceCount : 14;
        float rad = 4.0f + gSettings.juiceSpeed * 9.0f; if (rad > 18.0f) rad = 18.0f;
        int sz = (int)(gSettings.juiceSize * 60.0f); sz = sz < 2 ? 2 : (sz > 10 ? 10 : sz);
        for (int i = 0; i < shown; i++) {
            float ang = (float)i / (float)(shown > 0 ? shown : 1) * 6.2831853f;
            int px = cx2 + (int)(cosf(ang) * rad), py = cy2 + (int)(sinf(ang) * rad);
            Color c = note_color((size_t)i); c.a = 220;
            DrawRectangle(px - sz / 2, py - sz / 2, sz, sz, c);
        }
        return;
    }
}

/* Texte d'aide de la ligne selectionnee (affiche en bas du panneau). */
static void settings_hint(int g, const char **out, Color *col) {
    *out = NULL; *col = (Color){ 150, 170, 210, 255 };
    switch (g) {
        case 3: *out = TextFormat("(built-in  |  Custom (colors= in settings.cfg)  |  .txt chains in %s/  -  %d chain(s) loaded)",
                                   gColorChainDir, gColorChainCount); break;
        case 1: *out = "(smaller = faster)"; break;
        case 2: *out = "(absolute pen pointing; No = classic mouse)"; break;
        case 4:
            if (gMeshMsg[0]) { *out = gMeshMsg; *col = (Color){ 255, 140, 120, 255 }; }
            else *out = TextFormat("(.obj / .glb / .gltf in %s/  -  %d found)",
                                   gMeshDir, gMeshCount);
            break;
        case 5:
            if (gCursorMsg[0]) { *out = gCursorMsg; *col = (Color){ 255, 140, 120, 255 }; }
            else *out = TextFormat("(PNG in %s/  -  %d found  -  None = generated cursor)",
                                   gCursorDir, gCursorCount);
            break;
        case 6:  *out = "(Yes = invincible, the health bar never drops)"; break;
        case 7:  *out = "(ms before/after the plane: 20=strict (default)  100=loose)"; break;
        case 8:  *out = "(hue rotation - 0=original, 180=complementary)"; break;
        case 9:  *out = "(type the exact value - 1.00=default  0.50=slow  2.00=fast  -  relative mouse)"; break;
        case 10: *out = "(No = instant rendering, recommended for tablet - may cause tearing)";
                 *col = (Color){ 255, 200, 120, 255 }; break;
        case 16: *out = "(opacity of grid frame and lines - 0=invisible, 255=full)"; break;
        case 17: *out = "(visual scale of notes - 0.50=small, 1.00=default, 1.50=large)"; break;
        case 18:
            if (gHitsoundMsg[0]) { *out = gHitsoundMsg; *col = (Color){ 255, 140, 120, 255 }; }
            else *out = TextFormat("(.wav/.ogg/.mp3/.flac in %s/  -  %d found)",
                                   gHitsoundDir, gHitsoundCount);
            break;
        case 19: *out = "(volume of the sound played on each note hit - 0=mute, 100=full)"; break;
        case 21: *out = "(+ = notes later, if sound is late / BT headset.  Key C: calibrate)";
                 *col = (Color){ 150, 200, 255, 255 }; break;
        case 22: *out = "(Particles = bursts on hit  |  Pulse = grid/camera beating  |  Neon = all + glow)";
                 *col = (Color){ 180, 160, 255, 255 }; break;
        case 23: *out = "(number of bursts thrown on each note hit - 2 to 60)"; break;
        case 24: *out = "(time before a burst fades - 0.20 to 1.50 s)"; break;
        case 25: *out = "(size of a burst in world space - 0.05 to 0.40)"; break;
        case 26: *out = "(burst projection force - 0.30=soft, 1.00=default, 2.50=explosive)"; break;
        case 27: *out = "(grid+camera beat amplitude on the beat - 0.20 to 2.00)"; break;
        case 28: *out = "(flash 'COMBO N' every N hits in a row - None to disable)"; break;
        case 33: *out = "(in-game music volume - 0=mute, 50=half, 100=full)"; break;
        /* --- Curseur genere --- */
        case 35: *out = "(Generated = glowing dot + soft glow  |  Image = your PNG in cursors/)";
                 *col = (Color){ 150, 200, 255, 255 }; break;
        case 36: *out = "(cursor and glow color - pastel hue)"; break;
        case 37: *out = "(glowing core radius in pixels - 4=tiny, 16=default, 40=large)"; break;
        case 38: *out = "(brightness of the near-white center - 0=off, 100=full)"; break;
        case 39: *out = "(glow size = core x this factor - 1=none, 3.2=default, 6=huge)"; break;
        case 40: *out = "(intensity/opacity of the soft glow - 0=invisible, 100=intense)"; break;
        case 41: *out = "(additive blend: whiter center, light effect; No on light backgrounds)"; break;
        /* --- Trainee --- */
        case 42: *out = "(trail of cursor copies along the in-game path)"; break;
        case 43: *out = "(lifetime of a segment before fading - 0.05 to 2.0 s)"; break;
        case 44: *out = TextFormat("(max number of simultaneous copies - 4 to %d; more = smoother)", TRAIL_MAX); break;
        case 45: *out = "(min distance between points - ~0.3 to 0.5 x cursor size; THE anti-clump setting)"; break;
        case 46: *out = "(scale of the segment closest to the cursor - 1.0 = cursor size)"; break;
        case 47: *out = "(scale of the oldest segment - 0.1 = tiny)"; break;
        case 48: *out = "(opacity of the closest segment - 0=invisible, 100=full)"; break;
        case 49: *out = "(opacity of the oldest segment - 0 for a soft fade-out)"; break;
        case 50: *out = "(Yes = trail uses cursor color  |  No = dedicated color)"; break;
        case 51: *out = "(trail color when 'Follow cursor color' = No)"; break;
        case 60: *out = "(in-game: returns to menu)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 61: *out = "(in-game: pause / resume the map)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 62: *out = "(in-game: restart from the beginning)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 63: *out = "(in-game: jump to 1250 ms before the first note)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 64: *out = "(menu: open the Options screen)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 65: *out = "(menu: open the Game Modes screen)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 66: *out = "(menu: open the Profile screen)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 67: *out = "(menu: play the selected map)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 68: *out = "(menu: add / remove the map from favorites)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 69: *out = "(menu: add / remove the map from the ban list)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 70: *out = "(menu: show or hide banned maps in the list)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 71: *out = "(menu: show only favorited maps)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 72: *out = "(menu: show only maps that have never been played)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 73: *out = "(menu: cycle through difficulty ranges)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 74: *out = "(menu: sort the list by star rating)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 75: *out = "(menu: cycle through sort modes: name / rating / plays / ...)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 76: *out = "(menu: rescan the maps folder)"; *col = (Color){ 180, 200, 255, 255 }; break;
        case 77: *out = "(Enter / Right: browse for folder  |  Left: reset to default maps/  |  rescan is automatic)";
                 *col = (Color){ 150, 200, 255, 255 }; break;
        case 80: *out = "(None | Void=stars | Circuit=grid | Pulse=rings | Aurora=nebula | Topo=terrain | Voronoi=cells | Waveform=horizon | Flow=wind | Halftone=dots | Spectrum=fft | FlowLive=flux | Veil=aurore | Radar=sweep | Glitch=rgb)";
                 *col = (Color){ 150, 200, 255, 255 }; break;
        case 81: *out = "(brightness of the background - 0=invisible, 100=full)"; break;
        case 82: *out = "(Yes = use your cursor skin in menus - hides the system cursor)";
                 *col = (Color){ 150, 200, 255, 255 }; break;
        default: break;
    }
}

void settings_draw(int sw, int sh) {
    ClearBackground((Color){ 12, 12, 18, 255 });
    DrawText("OPTIONS", 40, 28, 26, RAYWHITE);

    int rows[MAX_OPT_ROWS];
    int nOpts = build_opt_rows(gOptTab, rows);

    int sbX = 40, sbY = 96, sbW = 200, catH = 46;
    int pX = sbX + sbW + 30, pY = sbY, rowH = 44;
    int pBottom = sh - 76;
    int maxVis = (pBottom - pY) / rowH; if (maxVis < 1) maxVis = 1;
    int pW = sw - pX - 40;
    int scroll = 0;
    if (nOpts > maxVis) {
        scroll = gOptSel - maxVis / 2;
        if (scroll < 0) scroll = 0;
        if (scroll > nOpts - maxVis) scroll = nOpts - maxVis;
    }

    /* === Sidebar des categories === */
    for (int ti = 0; ti < N_TABS; ti++) {
        int cy = sbY + ti * catH;
        bool active = (ti == gOptTab);
        if (active) {
            DrawRectangle(sbX, cy, sbW, catH - 6, (Color){ 30, 42, 74, 255 });
            DrawRectangle(sbX, cy, 4, catH - 6, (Color){ 100, 160, 255, 255 });
        }
        Color fg = active ? RAYWHITE : (Color){ 140, 145, 170, 255 };
        DrawText(OPT_TAB_NAMES[ti], sbX + 18, cy + (catH - 6 - 18) / 2, 18, fg);
    }
    DrawRectangle(pX - 16, sbY, 1, pBottom - sbY, (Color){ 36, 36, 52, 255 });

    /* === Panneau de reglages de la categorie active === */
    static const char *const labels[88] = {
        "Spawn distance", "Speed (approach time)",
        "Graphics tablet", "Color palette", "Note shape", "Image file",
        "God Mode", "Timing tolerance", "Note hue", "Mouse sensitivity",
        "VSync",
        "", "", "", "", "",                       /* 11-15 : libres (ancienne trainee) */
        "Grid opacity", "Note size",
        "Hitsound", "Hitsound volume",
        "Internal resolution",
        "Audio offset (sync)",
        "Visual effects",
        "Particles: amount", "Particles: duration",
        "Particles: size", "Particles: speed",
        "Pulse: intensity", "Combo: flash step",
        "", "", "", "",                           /* 29-32 : libres (ancien curseur) */
        "Music volume",
        "Grid style",
        /* 35-41 : curseur genere */
        "Cursor type", "Color", "Core size", "Core intensity",
        "Glow size", "Glow intensity", "Additive (light)",
        /* 42-51 : trainee */
        "Cursor trail", "Trail duration", "Max segments",
        "Spacing (px)", "Start scale", "End scale",
        "Start opacity", "End opacity", "Follow cursor color",
        "Trail color",
        /* 52-59 : libres */
        "", "", "", "", "", "", "", "",
        /* 60-63 : en jeu */
        "Quit game", "Pause / Resume", "Restart map", "Skip intro",
        /* 64-76 : menu */
        "Open Settings", "Open Modes", "Open Profile", "Play / Select",
        "Toggle Favorite", "Toggle Ban", "Hide Banned",
        "Favorites Only", "New Only", "Difficulty Filter",
        "Sort: Star Rate", "Cycle Sort", "Rescan Maps",
        /* 77 : dossier maps */
        "Maps folder",
        /* 78-79 : libres */
        "", "",
        /* 80-81 : background procedural */
        "Background", "Intensity",
        /* 82 : curseur dans les menus */
        "Cursor in menus",
        /* 83-87 : HUD configurable */
        "HUD: Score", "HUD: Combo", "HUD: Accuracy", "HUD: HP bar", "HUD: Song info"
    };

    int last = scroll + maxVis; if (last > nOpts) last = nOpts;
    for (int r = scroll; r < last; r++) {
        int globalR = rows[r];
        int ry = pY + (r - scroll) * rowH;
        bool sel = (r == gOptSel);
        bool sub = (globalR >= 23 && globalR <= 28) || globalR == 5
                || (globalR >= 36 && globalR <= 41) || (globalR >= 43 && globalR <= 51)
                || globalR == 81;
        if (sel) DrawRectangle(pX, ry, pW, rowH - 6, (Color){ 34, 60, 110, 230 });

        int fs = 20, cyT = ry + (rowH - 6 - fs) / 2;
        int rx = pX + pW - 16;
        Color ac = (Color){ 150, 160, 185, 255 };
        if (sub) DrawText(">", pX + 22, cyT, 16, (Color){ 90, 110, 150, 255 });
        DrawText(labels[globalR], pX + (sub ? 42 : 18), cyT, fs,
                 sel ? RAYWHITE : (Color){ 205, 208, 222, 255 });

        /* Cas special : capture de touche en cours */
        if (globalR >= 60 && globalR <= 76 && sel && gKeyCapture && gKeyCaptureSlot == globalR) {
            bool blink = ((int)(GetTime() * 3) & 1);
            const char *waiting = blink ? "[ press any key... ]" : "[                   ]";
            int ww = MeasureText(waiting, fs);
            DrawText(waiting, rx - ww, cyT, fs, (Color){ 255, 210, 80, 255 });
            continue;
        }

        /* Cas special : saisie directe de la sensibilite */
        if (globalR == 9 && sel && gSensEditing) {
            const char *cursor_blink = ((int)(GetTime() * 2) & 1) ? "|" : " ";
            char display[48];
            snprintf(display, sizeof display, "%s%s", gSensEditBuf, cursor_blink);
            int dw = MeasureText(display, fs);
            int bx = rx - dw - 8, by = ry + (rowH - 6 - (fs + 8)) / 2;
            DrawRectangle(bx - 6, by, dw + 14, fs + 8, (Color){ 20, 30, 60, 255 });
            DrawRectangleLinesEx((Rectangle){ (float)(bx - 6), (float)by, (float)(dw + 14), (float)(fs + 8) },
                                 1.0f, (Color){ 80, 140, 255, 255 });
            DrawText(display, bx, by + 4, fs, (Color){ 180, 220, 255, 255 });
            continue;
        }

        char val[128];
        switch (globalR) {
            case 0:  snprintf(val, sizeof val, "%.0f", gSettings.approachDist); break;
            case 1:  snprintf(val, sizeof val, "%.0f ms", gSettings.approachMs); break;
            case 2:  snprintf(val, sizeof val, "%s", gSettings.tablet ? "Yes" : "No"); break;
            case 3:  snprintf(val, sizeof val, "%s", settings_palette_name(&gSettings)); break;
            case 4:  snprintf(val, sizeof val, "%s", mesh_index_of(gSettings.meshName) == 0 ? "Cube (default)" : gSettings.meshName); break;
            case 5:  snprintf(val, sizeof val, "%s", cursor_index_of(gSettings.cursorName) == 0 ? "None (default)" : gSettings.cursorName); break;
            case 6:  snprintf(val, sizeof val, "%s", gSettings.godMode ? "Yes" : "No"); break;
            case 7:  snprintf(val, sizeof val, "%.0f ms", gSettings.hitWindowMs); break;
            case 8:  snprintf(val, sizeof val, "%d deg", gSettings.hueShift); break;
            case 9:  snprintf(val, sizeof val, "%.2f", gSettings.sensMultiplier); break;
            case 10: snprintf(val, sizeof val, "%s", gSettings.vsync ? "Yes" : "No"); break;
            case 16: snprintf(val, sizeof val, "%d", gSettings.gridAlpha); break;
            case 17: snprintf(val, sizeof val, "%.2f", gSettings.noteScale); break;
            case 18: snprintf(val, sizeof val, "%s", hitsound_index_of(gSettings.hitsoundName) == 0 ? "None (default)" : gSettings.hitsoundName); break;
            case 19: snprintf(val, sizeof val, "%d %%", (int)(gSettings.hitsoundVolume * 100.0f + 0.5f)); break;
            case 20: { static const char *rn[3] = {"Native","1280x720","854x480"};
                       snprintf(val, sizeof val, "%s", rn[gSettings.internalRes]); } break;
            case 21: snprintf(val, sizeof val, "%+.0f ms", gSettings.audioOffsetMs); break;
            case 22: { static const char *jn[4] = {"None","Particles","Pulse","Neon"};
                       snprintf(val, sizeof val, "%s", jn[gSettings.juiceMode]); } break;
            case 23: snprintf(val, sizeof val, "%d", gSettings.juiceCount); break;
            case 24: snprintf(val, sizeof val, "%.2f s", gSettings.juiceLife); break;
            case 25: snprintf(val, sizeof val, "%.2f", gSettings.juiceSize); break;
            case 26: snprintf(val, sizeof val, "%.2f x", gSettings.juiceSpeed); break;
            case 27: snprintf(val, sizeof val, "%.2f x", gSettings.juicePulse); break;
            case 28: snprintf(val, sizeof val, "%s",
                       gSettings.juiceCombo == 0 ? "None" : TextFormat("every %d", gSettings.juiceCombo)); break;
            case 33: snprintf(val, sizeof val, "%d %%", (int)(gSettings.musicVolume * 100.0f + 0.5f)); break;
            case 34: { static const char *gs[4] = {"Classic","Minimal","Corners","Diagonals"};
                       snprintf(val, sizeof val, "%s", gs[gSettings.gridStyle]); } break;
            /* --- Curseur genere --- */
            case 35: { static const char *cm[2] = {"Generated","Image"};
                       snprintf(val, sizeof val, "%s", cm[gSettings.cursorMode]); } break;
            case 36: snprintf(val, sizeof val, "%d deg", color_hue_deg(gSettings.cursor.color)); break;
            case 37: snprintf(val, sizeof val, "%.0f px", gSettings.cursor.size); break;
            case 38: snprintf(val, sizeof val, "%d %%", (int)(gSettings.cursor.coreIntensity * 100.0f + 0.5f)); break;
            case 39: snprintf(val, sizeof val, "%.1f x", gSettings.cursor.glowSize); break;
            case 40: snprintf(val, sizeof val, "%d %%", (int)(gSettings.cursor.glowIntensity * 100.0f + 0.5f)); break;
            case 41: snprintf(val, sizeof val, "%s", gSettings.cursor.additive ? "Yes" : "No"); break;
            /* --- Trainee --- */
            case 42: snprintf(val, sizeof val, "%s", gSettings.cursor.trailEnabled ? "Yes" : "No"); break;
            case 43: snprintf(val, sizeof val, "%.2f s", gSettings.cursor.trailDuration); break;
            case 44: snprintf(val, sizeof val, "%d", gSettings.cursor.trailMaxSegments); break;
            case 45: snprintf(val, sizeof val, "%.0f px", gSettings.cursor.trailSpacing); break;
            case 46: snprintf(val, sizeof val, "%.2f", gSettings.cursor.trailStartScale); break;
            case 47: snprintf(val, sizeof val, "%.2f", gSettings.cursor.trailEndScale); break;
            case 48: snprintf(val, sizeof val, "%d %%", (int)(gSettings.cursor.trailStartAlpha * 100.0f + 0.5f)); break;
            case 49: snprintf(val, sizeof val, "%d %%", (int)(gSettings.cursor.trailEndAlpha * 100.0f + 0.5f)); break;
            case 50: snprintf(val, sizeof val, "%s", gSettings.cursor.trailUseCursorColor ? "Yes" : "No"); break;
            case 51: snprintf(val, sizeof val, "%d deg", color_hue_deg(gSettings.cursor.trailColor)); break;
            /* --- Touches configurables : en jeu --- */
            case 60: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.quit));           break;
            case 61: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.pause));          break;
            case 62: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.restart));        break;
            case 63: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.skipIntro));      break;
            /* --- Touches configurables : menu --- */
            case 64: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuSettings));   break;
            case 65: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuModes));      break;
            case 66: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuProfile));    break;
            case 67: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuPlay));       break;
            case 68: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuFavorite));   break;
            case 69: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuBan));        break;
            case 70: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuHideBanned)); break;
            case 71: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuFavsOnly));   break;
            case 72: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuNewOnly));    break;
            case 73: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuDiffFilter)); break;
            case 74: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuSortStar));   break;
            case 75: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuCycleSort));  break;
            case 76: snprintf(val, sizeof val, "%s", key_name(gSettings.keys.menuRescan));     break;
            case 77: {
                if (!gSettings.mapsDir[0]) {
                    snprintf(val, sizeof val, "Default (maps/)");
                } else {
                    int len = (int)strlen(gSettings.mapsDir);
                    if (len > 44) snprintf(val, sizeof val, "...%s", gSettings.mapsDir + len - 41);
                    else          snprintf(val, sizeof val, "%s", gSettings.mapsDir);
                }
            } break;
            /* --- Background --- */
            case 80: {
                static const char *bm[BG_COUNT] = {
                    "None", "Void", "Circuit", "Pulse", "Aurora",
                    "Topo", "Voronoi", "Waveform", "Flow", "Halftone",
                    "Spectrum", "FlowLive", "Veil", "Radar", "Glitch"
                };
                snprintf(val, sizeof val, "%s", bm[gSettings.bgStyle]);
            } break;
            case 81: snprintf(val, sizeof val, "%d %%", (int)(gSettings.bgIntensity * 100.0f + 0.5f)); break;
            case 82: snprintf(val, sizeof val, "%s", gSettings.cursorInMenu ? "Yes" : "No"); break;
            case 83: snprintf(val, sizeof val, "%s", gSettings.hudShowScore    ? "Show" : "Hide"); break;
            case 84: snprintf(val, sizeof val, "%s", gSettings.hudShowCombo    ? "Show" : "Hide"); break;
            case 85: snprintf(val, sizeof val, "%s", gSettings.hudShowAccuracy ? "Show" : "Hide"); break;
            case 86: snprintf(val, sizeof val, "%s", gSettings.hudShowHp       ? "Show" : "Hide"); break;
            case 87: snprintf(val, sizeof val, "%s", gSettings.hudShowSongInfo ? "Show" : "Hide"); break;
            default: snprintf(val, sizeof val, "?"); break;
        }
        int vw = MeasureText(val, fs);
        Color vc = sel ? (Color){ 200, 230, 255, 255 } : (Color){ 165, 185, 215, 255 };
        bool isBindRow = (globalR >= 60 && globalR <= 76);
        bool isPickRow = (globalR == 77);
        int valLeft;
        if (globalR != 9 && !isBindRow && !isPickRow) {
            int decX = rx - 12 - 10 - vw - 16;   /* x du "<" */
            /* zone cliquable "< valeur >" ; clic moitie gauche = baisser, droite = monter */
            Rectangle ctrl = { (float)(decX - 4), (float)ry, (float)((rx + fs) - decX + 8), (float)(rowH - 6) };
            float cmid = ctrl.x + ctrl.width * 0.5f;
            Vector2 mpo = GetMousePosition();
            bool hov = sel && CheckCollisionPointRec(mpo, ctrl);
            DrawText("<", decX,        cyT, fs, (hov && mpo.x <  cmid) ? RAYWHITE : ac);
            DrawText(val, rx - 12 - 10 - vw, cyT, fs, vc);
            DrawText(">", rx - 12,      cyT, fs, (hov && mpo.x >= cmid) ? RAYWHITE : ac);
            valLeft = decX;
            if (sel) gOptCtrl = ctrl;
        } else {
            if (sel) gOptCtrl = (Rectangle){ 0, 0, 0, 0 };
            /* lignes de touches : couleur differente pour signaler qu'elles sont cliquables */
            Color kvc = isBindRow
                ? (sel ? (Color){ 255, 220, 120, 255 } : (Color){ 200, 180, 120, 255 })
                : vc;
            DrawText(val, rx - vw, cyT, fs, kvc);
            valLeft = rx - vw;
            if (sel) {
                int bw = MeasureText("[Enter]", 14);
                DrawText("[Enter]", rx - vw - bw - 12, cyT + 2, 14, (Color){ 100, 160, 255, 255 });
                valLeft = rx - vw - bw - 12;
            }
        }
        settings_preview(globalR, sel, valLeft - 14, ry, rowH - 6);
    }

    /* Indicateurs de defilement */
    if (nOpts > maxVis) {
        if (scroll > 0)
            DrawText("^", pX + pW / 2, pY - 4, 16, (Color){ 120, 130, 160, 255 });
        if (scroll + maxVis < nOpts)
            DrawText("v", pX + pW / 2, pBottom - 8, 16, (Color){ 120, 130, 160, 255 });
    }

    /* === Aide de la ligne selectionnee (bas) === */
    int gselR = (gOptSel >= 0 && gOptSel < nOpts) ? rows[gOptSel] : -1;
    const char *hint = NULL; Color hc = (Color){ 150, 170, 210, 255 };
    settings_hint(gselR, &hint, &hc);
    if (hint && hint[0]) DrawText(hint, sbX, sh - 56, 15, hc);

    const char *footer = gSensEditing
        ? "Type the value  -  Enter: confirm  -  Esc: cancel"
        : (gKeyCapture
            ? "Press a key to bind it  -  Esc: cancel"
            : "Tab: category  -  Up/Down: navigate  -  Left/Right: change  -  Esc: back");
    DrawText(footer, sbX, sh - 32, 14, (Color){ 110, 110, 130, 255 });
}

/* ===================================================================== */
/*  Mods (modificateurs) : cases a cocher inline dans le panneau gauche  */
/* ===================================================================== */
static const ModDef MOD_DEFS[] = {
    { MOD_HARDROCK, "Hard Rock",    "Smaller hit window and increased damage.  Score x1.10" },
    { MOD_HIDDEN,   "Hidden",       "Notes fade out as they approach the plane.  Score x1.06" },
    { MOD_NOFAIL,   "No Fail",      "No game over: the map plays to the end.  Score x0.50" },
    { MOD_SUDDEN,   "Sudden Death", "A single missed note ends the run." },
    { MOD_MIRROR_X,   "Mirror H",    "Mirrors the note layout horizontally." },
    { MOD_MIRROR_Y,   "Mirror V",    "Mirrors the note layout vertically." },
    { MOD_FLASHLIGHT, "Flashlight",  "Only the area around the cursor is visible.  Score x1.12" },
    { MOD_VANISH,     "Vanish",      "Notes fade out just before reaching the hit plane." },
};
#define N_MODS ((int)(sizeof(MOD_DEFS) / sizeof(MOD_DEFS[0])))
static const float RATE_STEPS[] = {
    0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f,
    0.75f, 0.80f, 0.85f, 0.90f, 0.95f,
    1.00f, 1.05f, 1.10f, 1.15f, 1.20f,
    1.25f, 1.30f, 1.35f, 1.40f, 1.45f,
    1.50f, 1.55f, 1.60f, 1.65f, 1.70f,
    1.75f, 1.80f, 1.85f, 1.90f, 1.95f,
    2.00f
};
#define N_RATE ((int)(sizeof(RATE_STEPS) / sizeof(RATE_STEPS[0])))

/* Applique un pas a la vitesse (mod Vitesse). */
static void mods_rate_step(int dir) {
    int ri = 15;  /* defaut = 1.0x (index 15 dans RATE_STEPS) */
    for (int i = 0; i < N_RATE; i++) if (fabsf(RATE_STEPS[i] - gSettings.rate) < 0.01f) ri = i;
    ri += dir; if (ri < 0) ri = 0; if (ri >= N_RATE) ri = N_RATE - 1;
    gSettings.rate = RATE_STEPS[ri];
    settings_apply(&gSettings);
    settings_save(&gSettings);
}

/* Bascule un mod en gerant l'exclusivite No Fail / Sudden Death. */
static void mods_toggle(unsigned bit) {
    gSettings.mods ^= (int)bit;
    if ((gSettings.mods & MOD_NOFAIL) && (gSettings.mods & MOD_SUDDEN)) {
        if (bit == MOD_NOFAIL) gSettings.mods &= ~(int)MOD_SUDDEN;
        else                   gSettings.mods &= ~(int)MOD_NOFAIL;
    }
    settings_apply(&gSettings);
    settings_save(&gSettings);
}

/* Geometrie du bloc de mods dans le panneau gauche (partagee draw/input).
 * r in [0..N_MODS-1] = case a cocher (2 colonnes), r == N_MODS = ligne Vitesse.
 * Ancre en bas, juste au-dessus du bouton JOUER (a sh-82). */
static Rectangle mods_row_rect(int panelX, int sh, int r) {
    int tx = 42;
    int blockW   = panelX - tx - 30;
    int colW     = blockW / 2;
    int rowH = 32, rateH = 30;
    int nColRows = (N_MODS + 1) / 2;            /* ceil(N_MODS/2) lignes par colonne */
    int rateY    = sh - 92 - rateH;
    int rowsTop  = rateY - nColRows * rowH;
    if (r >= N_MODS)
        return (Rectangle){ (float)tx, (float)rateY, (float)blockW, (float)(rateH - 4) };
    int col = r / nColRows, row = r % nColRows;
    return (Rectangle){ (float)(tx + col * colW), (float)(rowsTop + row * rowH),
                        (float)(colW - 12), (float)(rowH - 6) };
}

/* Zone de controle "< 1.0x >" de la ligne Vitesse (a droite de la ligne).
 * Clic dans la moitie gauche = baisser, moitie droite = augmenter. */
static Rectangle mods_rate_ctrl(int panelX, int sh) {
    Rectangle r = mods_row_rect(panelX, sh, N_MODS);
    float aR = r.x + r.width, w = 160.0f;
    return (Rectangle){ aR - w, r.y, w, r.height };
}

/* Bulle d'aide dessinee au-dessus du curseur, bornee au panneau gauche. */
static void mods_tooltip(const char *text, Vector2 mp, int panelX, int sh) {
    if (!text || !text[0]) return;
    int fs = 14, padX = 10, padY = 8;
    float boxW = (float)(MeasureText(text, fs) + 2 * padX);
    float boxH = (float)(fs + 2 * padY);
    float x = mp.x + 16.0f, y = mp.y - boxH - 10.0f;
    if (x + boxW > (float)(panelX - 10)) x = (float)(panelX - 10) - boxW;   /* reste dans le panneau gauche */
    if (x < 10.0f) x = 10.0f;
    if (y < 10.0f) y = mp.y + 22.0f;                                        /* sinon, sous le curseur */
    if (y + boxH > (float)(sh - 6)) y = (float)(sh - 6) - boxH;
    Rectangle box = { x, y, boxW, boxH };
    DrawRectangleRounded(box, 0.22f, 5, (Color){ 18, 20, 28, 244 });
    DrawRectangleRoundedLinesEx(box, 0.22f, 5, 1.5f, (Color){ 96, 106, 138, 255 });
    DrawText(text, (int)(x + padX), (int)(y + padY), fs, (Color){ 225, 230, 245, 255 });
}

/* Dessine le bloc de mods (cases a cocher + ligne Vitesse) dans le panneau gauche. */
void mods_draw_inline(int panelX, int sh, Vector2 mp) {
    Rectangle r0 = mods_row_rect(panelX, sh, 0);
    int headerY = (int)r0.y - 26;
    DrawText("MODS", 42, headerY, 18, (Color){ 150, 160, 190, 255 });
    { const char *mm = TextFormat("score x%.2f", mods_score_mult((unsigned)gSettings.mods, gSettings.rate));
      DrawText(mm, panelX - 30 - MeasureText(mm, 15), headerY + 2, 15, (Color){ 255, 180, 90, 255 }); }

    const char *tip = NULL;   /* description du mod survole, dessinee en dernier (par-dessus) */
    for (int r = 0; r < N_MODS; r++) {
        Rectangle rr = mods_row_rect(panelX, sh, r);
        bool on = (gSettings.mods & (int)MOD_DEFS[r].bit) != 0;
        bool ov = CheckCollisionPointRec(mp, rr);
        if (ov) { DrawRectangleRounded(rr, 0.25f, 4, (Color){ 255, 255, 255, 14 }); tip = MOD_DEFS[r].desc; }
        /* case a cocher */
        int bs = 18;
        Rectangle cb = { rr.x, rr.y + (rr.height - bs) / 2.0f, (float)bs, (float)bs };
        DrawRectangleRounded(cb, 0.25f, 4, on ? (Color){ 255, 180, 90, 255 } : (Color){ 28, 30, 40, 255 });
        DrawRectangleLinesEx(cb, 1.5f, on ? (Color){ 255, 205, 140, 255 }
                                         : (ov ? (Color){ 120, 130, 160, 255 } : (Color){ 66, 70, 94, 255 }));
        if (on) {
            Vector2 p1 = { cb.x + 4, cb.y + 9 }, p2 = { cb.x + 7, cb.y + 13 }, p3 = { cb.x + 14, cb.y + 5 };
            DrawLineEx(p1, p2, 2.2f, (Color){ 30, 25, 15, 255 });
            DrawLineEx(p2, p3, 2.2f, (Color){ 30, 25, 15, 255 });
        }
        Color nc = on ? (Color){ 255, 210, 160, 255 } : (ov ? RAYWHITE : (Color){ 200, 205, 225, 255 });
        DrawText(MOD_DEFS[r].name, (int)(cb.x + bs + 10), (int)(rr.y + (rr.height - 18) / 2.0f), 18, nc);
    }

    /* ligne Vitesse : "Vitesse   < 1.0x >" */
    Rectangle rr = mods_row_rect(panelX, sh, N_MODS);
    bool rov = CheckCollisionPointRec(mp, rr);
    if (rov) { DrawRectangleRounded(rr, 0.25f, 4, (Color){ 255, 255, 255, 14 });
               tip = "Map playback speed.  Faster = harder, score adjusted."; }
    bool ron = (gSettings.rate != 1.0f);
    DrawText("Speed", (int)rr.x, (int)(rr.y + (rr.height - 18) / 2.0f), 18,
             ron ? (Color){ 255, 210, 160, 255 } : (Color){ 200, 205, 225, 255 });
    Rectangle ctrl = mods_rate_ctrl(panelX, sh);
    int gy = (int)(rr.y + (rr.height - 18) / 2.0f);
    bool lov = rov && (mp.x <  ctrl.x + ctrl.width * 0.5f) && (mp.x >= ctrl.x);
    bool riv = rov && (mp.x >= ctrl.x + ctrl.width * 0.5f);
    DrawText("<", (int)ctrl.x + 8, gy, 20, lov ? RAYWHITE : (Color){ 160, 170, 195, 255 });
    DrawText(">", (int)(ctrl.x + ctrl.width) - 22, gy, 20, riv ? RAYWHITE : (Color){ 160, 170, 195, 255 });
    const char *rv = TextFormat("%gx", gSettings.rate);
    int rvCx = (int)(ctrl.x + ctrl.width * 0.5f);
    DrawText(rv, rvCx - MeasureText(rv, 18) / 2, gy, 18, ron ? (Color){ 255, 225, 170, 255 } : (Color){ 200, 230, 255, 255 });

    mods_tooltip(tip, mp, panelX, sh);   /* par-dessus tout le bloc */
}

/* Gere les clics sur les cases a cocher + zone Vitesse (moitie gauche/droite). */
void mods_handle_click(int panelX, int sh, Vector2 mp) {
    for (int r = 0; r < N_MODS; r++)
        if (CheckCollisionPointRec(mp, mods_row_rect(panelX, sh, r))) { mods_toggle(MOD_DEFS[r].bit); return; }
    Rectangle ctrl = mods_rate_ctrl(panelX, sh);
    if (CheckCollisionPointRec(mp, ctrl))
        mods_rate_step(mp.x < ctrl.x + ctrl.width * 0.5f ? -1 : 1);
}