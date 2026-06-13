#include "common.h"

/* =========================================================================
 * Palette
 * ========================================================================= */
#define COL_BG        (Color){ 12,  12,  20, 255}
#define COL_CARD      (Color){ 17,  17,  32, 255}
#define COL_DIVIDER   (Color){ 28,  28,  44, 255}
#define COL_LABEL     (Color){ 53,  53,  74, 255}
#define COL_VALUE     (Color){221, 221, 232, 255}
#define COL_DIM       (Color){ 37,  37,  53, 255}

#define ACC_TEAL       (Color){ 15, 110,  86, 255}
#define ACC_TEAL_LIT   (Color){ 29, 158, 117, 255}
#define ACC_BLUE       (Color){ 24,  95, 165, 255}
#define ACC_AMBER      (Color){133,  79,  11, 255}
#define ACC_PURPLE     (Color){ 83,  74, 183, 255}
#define ACC_PURPLE_DK  (Color){ 60,  52, 137, 255}
#define ACC_PURPLE_LIT (Color){127, 119, 221, 255}
#define ACC_GOLD       (Color){186, 117,  23, 255}
#define ACC_RED        (Color){163,  45,  45, 255}
#define ACC_RED_DK     (Color){113,  43,  19, 255}

/* =========================================================================
 * Helpers
 * ========================================================================= */

static void DrawTextRight_(const char *t, int x, int y, int sz, Color c) {
    DrawText(t, x - MeasureText(t, sz), y, sz, c);
}

static void DrawTextCx_(const char *t, int cx, int y, int sz, Color c) {
    DrawText(t, cx - MeasureText(t, sz) / 2, y, sz, c);
}

static void DrawStatCard(Rectangle r, Color accent) {
    DrawRectangleRec(r, COL_CARD);
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, 2, accent);
}

static void FormatPlayTime(int secs, char *buf, int bufLen) {
    if (secs < 3600)
        snprintf(buf, bufLen, "%dm %02ds", secs / 60, secs % 60);
    else
        snprintf(buf, bufLen, "%dh %02dm", secs / 3600, (secs % 3600) / 60);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

Rectangle profile_btn_rect(int sw) {
    return (Rectangle){ (float)(sw - 426), 4.0f, 130.0f, 28.0f };
}

void profile_draw(int sw, int sh, int mapCount) {
    ClearBackground(COL_BG);

    /* --- header / footer --- */
    DrawText("PROFILE", 40, 30, 24, COL_VALUE);
    DrawTextRight_("ESC: BACK", sw - 40, sh - 25, 14, COL_DIM);

    /* --- stats precomputation --- */
    int  bestCombo = 0;
    char bestComboFile[256] = {0};
    for (int i = 0; i < gProfile.bestCount; i++) {
        if (gProfile.best[i].maxCombo > bestCombo) {
            bestCombo = gProfile.best[i].maxCombo;
            strncpy(bestComboFile, gProfile.best[i].filename,
                    sizeof(bestComboFile) - 1);
        }
    }
    /* nom affichable : base sans extension, tronqué à 38 chars */
    char bestComboMap[40] = {0};
    if (bestComboFile[0]) {
        const char *base = bestComboFile;
        for (const char *p = bestComboFile; *p; p++)
            if (*p == '/' || *p == '\\') base = p + 1;
        int i = 0;
        while (base[i] && base[i] != '.' && i < 38)
            { bestComboMap[i] = base[i]; i++; }
        if (i == 38) { bestComboMap[36] = '.'; bestComboMap[37] = '.'; }
        bestComboMap[i] = '\0';
    }

    int notesHit    = gProfile.allTimeHits;
    int notesTotal  = gProfile.allTimeTotalNotes;
    int notesMissed = notesTotal - notesHit;
    if (notesMissed < 0) notesMissed = 0;

    float accuracy = (notesTotal > 0)
        ? (float)notesHit / (float)notesTotal : 0.0f;

    int hitTotal = notesHit + notesMissed;
    float hitRatio = (hitTotal > 0)
        ? (float)notesHit / (float)hitTotal : 0.0f;

    int totalSec    = (int)gProfile.totalPlayTimeSec;
    int mapsPlayed  = gProfile.bestCount;
    int mapsAvail   = (mapCount > 0) ? mapCount : 1;
    int neverPlayed = mapsAvail - mapsPlayed;
    if (neverPlayed < 0) neverPlayed = 0;

    /* --- layout --- */
    int mgX    = 40;
    int gap    = 8;
    int usable = sw - 80;

    /* Row 1  y=70  h=150 */
    int row1Y = 70,  row1H = 150;
    int accW  = 200;
    int w2    = (usable - accW - gap * 2) / 2;
    int accX  = mgX;
    int ptX   = accX + accW + gap;
    int cbX   = ptX  + w2   + gap;

    /* Row 2  y=228  h=90 */
    int row2Y = row1Y + row1H + gap;
    int row2H = 90;

    /* Row 3  y=326  h=120 */
    int row3Y = row2Y + row2H + gap;
    int row3H = 120;
    int favW  = 148, banW = 148;
    int collW = usable - gap * 2 - favW - banW;
    int collX = mgX;
    int favX  = collX + collW + gap;
    int banX  = favX  + favW  + gap;

    /* =====================================================================
     * CARD 1 — ACCURACY  (anneau de progression)
     * ===================================================================== */
    {
        DrawStatCard((Rectangle){ (float)accX, (float)row1Y,
                                  (float)accW, (float)row1H }, ACC_TEAL);

        DrawText("ACCURACY", accX + 8, row1Y + 10, 10, ACC_TEAL);

        Vector2 center = { (float)(accX + accW / 2), (float)(row1Y + 82) };
        float innerR = 26.0f, outerR = 33.0f;

        /* piste fond */
        DrawRing(center, innerR, outerR, 0.0f, 360.0f, 64,
                 (Color){ 8, 26, 16, 255 });

        /* arc rempli */
        if (accuracy > 0.001f)
            DrawRing(center, innerR, outerR,
                     -90.0f, -90.0f + 360.0f * accuracy,
                     64, ACC_TEAL_LIT);

        /* valeur centrale */
        char accBuf[16];
        snprintf(accBuf, sizeof(accBuf), "%.1f%%", accuracy * 100.0f);
        DrawTextCx_(accBuf, (int)center.x, (int)center.y - 7, 14, COL_VALUE);

        DrawTextCx_("ALL TIME", (int)center.x, (int)center.y + 37, 10, COL_DIM);
    }

    /* =====================================================================
     * CARD 2 — PLAY TIME
     * ===================================================================== */
    {
        int cardR = ptX + w2;
        DrawStatCard((Rectangle){ (float)ptX, (float)row1Y,
                                  (float)w2,  (float)row1H }, ACC_BLUE);

        DrawText("PLAY TIME", ptX + 12, row1Y + 14, 10, ACC_BLUE);

        char ptBuf[32];
        FormatPlayTime(totalSec, ptBuf, sizeof(ptBuf));
        DrawText(ptBuf, ptX + 12, row1Y + 32, 28, COL_VALUE);

        DrawRectangle(ptX + 12, row1Y + 92, w2 - 24, 1, COL_DIVIDER);

        DrawText("RUNS", ptX + 12, row1Y + 102, 10, COL_LABEL);

        char runsBuf[24];
        snprintf(runsBuf, sizeof(runsBuf), "%d", gProfile.totalRuns);
        DrawTextRight_(runsBuf, cardR - 12, row1Y + 102, 20, COL_VALUE);
    }

    /* =====================================================================
     * CARD 3 — BEST COMBO
     * ===================================================================== */
    {
        int cardR = cbX + w2;
        DrawStatCard((Rectangle){ (float)cbX, (float)row1Y,
                                  (float)w2,  (float)row1H }, ACC_AMBER);

        DrawText("BEST COMBO", cbX + 12, row1Y + 14, 10, ACC_AMBER);

        char comboBuf[24];
        snprintf(comboBuf, sizeof(comboBuf), "%d", bestCombo);
        DrawText(comboBuf, cbX + 12, row1Y + 32, 28, COL_VALUE);

        if (bestComboMap[0])
            DrawText(bestComboMap, cbX + 12, row1Y + 66, 10, COL_DIM);

        DrawRectangle(cbX + 12, row1Y + 92, w2 - 24, 1, COL_DIVIDER);

        DrawText("HIT RATIO", cbX + 12, row1Y + 102, 10, COL_LABEL);

        char hrBuf[16];
        if (hitTotal > 0)
            snprintf(hrBuf, sizeof(hrBuf), "%.1f%%", hitRatio * 100.0f);
        else
            snprintf(hrBuf, sizeof(hrBuf), "?");
        DrawTextRight_(hrBuf, cardR - 12, row1Y + 102, 20, COL_VALUE);
    }

    /* =====================================================================
     * CARD 4 — PERFORMANCE  (pleine largeur)
     * ===================================================================== */
    {
        int cardR = mgX + usable;
        DrawStatCard((Rectangle){ (float)mgX,  (float)row2Y,
                                  (float)usable,(float)row2H }, ACC_PURPLE);

        char hitsStr[24], missedStr[24];
        snprintf(hitsStr,   sizeof(hitsStr),   "%d", notesHit);
        snprintf(missedStr, sizeof(missedStr),  "%d", notesMissed);

        DrawText("NOTES HIT",     mgX + 12,   row2Y + 14, 10, ACC_PURPLE);
        DrawText(hitsStr,         mgX + 12,   row2Y + 28, 24, ACC_TEAL_LIT);
        DrawTextRight_("NOTES MISSED", cardR - 12, row2Y + 14, 10, ACC_PURPLE);
        DrawTextRight_(missedStr,      cardR - 12, row2Y + 28, 24, ACC_RED_DK);

        if (hitTotal > 0) {
            int barX = mgX + 12, barY = row2Y + 72;
            int barW = usable - 24;
            DrawRectangle(barX, barY, barW, 5, COL_DIVIDER);
            DrawRectangle(barX, barY, (int)(barW * hitRatio), 5, ACC_TEAL_LIT);

            char lblHit[32], lblMiss[32];
            snprintf(lblHit,  sizeof(lblHit),  "hits %.1f%%",    hitRatio * 100.0f);
            snprintf(lblMiss, sizeof(lblMiss), "misses %.1f%%", (1.0f - hitRatio) * 100.0f);
            DrawText(lblHit,  barX,       barY + 8, 10, ACC_TEAL);
            DrawTextRight_(lblMiss, cardR - 12, barY + 8, 10, ACC_RED_DK);
        } else {
            DrawTextCx_("—", mgX + usable / 2, row2Y + (row2H / 2) - 10, 20, COL_LABEL);
        }
    }

    /* =====================================================================
     * CARD 5 — COLLECTION
     * ===================================================================== */
    {
        int cardR = collX + collW;
        int halfW = collW / 2;
        DrawStatCard((Rectangle){ (float)collX, (float)row3Y,
                                  (float)collW, (float)row3H }, ACC_PURPLE_DK);

        DrawText("COLLECTION", collX + 12, row3Y + 14, 10, ACC_PURPLE_DK);

        char playedLabel[32], totalLabel[32];
        snprintf(playedLabel, sizeof(playedLabel), "%d played", mapsPlayed);
        snprintf(totalLabel,  sizeof(totalLabel),  "/ %d",      mapsAvail);
        DrawText(playedLabel,    collX + 12,  row3Y + 30, 11, ACC_PURPLE_LIT);
        DrawTextRight_(totalLabel, cardR - 12, row3Y + 30, 11, COL_LABEL);

        /* barre de progression 3px */
        float progress = (float)mapsPlayed / (float)mapsAvail;
        int barX = collX + 12, barY = row3Y + 48, barW = collW - 24;
        DrawRectangle(barX, barY, barW, 3, COL_DIVIDER);
        if (progress > 0.0f)
            DrawRectangle(barX, barY, (int)(barW * progress), 3, ACC_PURPLE_LIT);

        /* deux sous-stats */
        char mpsStr[24], nvrStr[24];
        snprintf(mpsStr, sizeof(mpsStr), "%d", mapsPlayed);
        snprintf(nvrStr, sizeof(nvrStr), "%d", neverPlayed);

        DrawText("MAPS PLAYED",  collX + 12,          row3Y + 62, 10, COL_LABEL);
        DrawText(mpsStr,         collX + 12,          row3Y + 76, 18, COL_VALUE);
        DrawText("NEVER PLAYED", collX + 12 + halfW,  row3Y + 62, 10, COL_LABEL);
        DrawText(nvrStr,         collX + 12 + halfW,  row3Y + 76, 18, COL_VALUE);
    }

    /* =====================================================================
     * CARD 6 — FAVORITES
     * ===================================================================== */
    {
        int fav = gProfile.favCount;
        DrawStatCard((Rectangle){ (float)favX, (float)row3Y,
                                  (float)favW, (float)row3H }, ACC_GOLD);

        DrawText("FAVORITES", favX + 12, row3Y + 14, 10, ACC_GOLD);

        char favBuf[16];
        snprintf(favBuf, sizeof(favBuf), "%d", fav);
        Color valCol = (fav > 0) ? ACC_GOLD : COL_VALUE;
        DrawTextCx_(favBuf, favX + favW / 2, row3Y + 48, 34, valCol);
    }

    /* =====================================================================
     * CARD 7 — BANNED
     * ===================================================================== */
    {
        int ban = gProfile.blacklistCount;
        DrawStatCard((Rectangle){ (float)banX, (float)row3Y,
                                  (float)banW, (float)row3H }, ACC_RED);

        DrawText("BANNED", banX + 12, row3Y + 14, 10, ACC_RED);

        char banBuf[16];
        snprintf(banBuf, sizeof(banBuf), "%d", ban);
        Color valCol = (ban > 0) ? ACC_RED : COL_VALUE;
        DrawTextCx_(banBuf, banX + banW / 2, row3Y + 48, 34, valCol);
    }
}
