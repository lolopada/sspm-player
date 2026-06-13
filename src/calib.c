#include "common.h"

/* ===================================================================== */
/*  Calibration de l'offset audio (metronome + taps)                     */
/* ===================================================================== */

/* Synthetise un clic court (40 ms, ~1 kHz avec enveloppe decroissante). */
static void calib_make_click(Calib *c) {
    int sr = 44100, frames = sr / 25;                 /* 40 ms */
    short *pcm = (short *)malloc(sizeof(short) * frames);
    if (!pcm) { c->haveClick = false; return; }
    for (int i = 0; i < frames; i++) {
        float t   = (float)i / (float)sr;
        float env = expf(-t * 55.0f);
        float s   = sinf(2.0f * PI * 1000.0f * t) * env;
        pcm[i] = (short)(s * 14000.0f);
    }
    Wave w = { 0 };
    w.frameCount = (unsigned int)frames;
    w.sampleRate = (unsigned int)sr;
    w.sampleSize = 16; w.channels = 1; w.data = pcm;
    c->click     = LoadSoundFromWave(w);              /* copie les donnees */
    c->haveClick = (c->click.frameCount > 0);
    free(pcm);
}

void calib_reset_samples(Calib *c) {
    c->nSamples = 0; c->suggested = 0.0f; c->lastDelta = 0.0f;
}

void calib_begin(Calib *c) {
    if (!c->haveClick) calib_make_click(c);
    c->startT = GetTime();
    c->beatsPlayed = 0;
    c->lastTapT = -1.0;
    calib_reset_samples(c);
}

void calib_update(Calib *c) {
    double now     = GetTime();
    double elapsed = now - (c->startT + CALIB_LEADIN);
    /* jouer les clics dus (un par beat) */
    int due = (elapsed >= 0.0) ? (int)(elapsed / CALIB_PERIOD) + 1 : 0;
    while (c->beatsPlayed < due) {
        if (c->haveClick) PlaySound(c->click);
        c->beatsPlayed++;
    }
    /* tap : ecart au beat le plus proche */
    if (IsKeyPressed(KEY_SPACE) && elapsed > -CALIB_PERIOD) {
        double nearest = round(elapsed / CALIB_PERIOD) * CALIB_PERIOD;
        double dms     = (elapsed - nearest) * 1000.0;
        if (nearest >= 0.0 && dms > -CALIB_PERIOD * 500.0 && dms < CALIB_PERIOD * 500.0) {
            if (c->nSamples < CALIB_MAX) c->samples[c->nSamples++] = (float)dms;
            else { for (int i = 1; i < CALIB_MAX; i++) c->samples[i-1] = c->samples[i];
                   c->samples[CALIB_MAX-1] = (float)dms; }
            float sum = 0.0f;
            for (int i = 0; i < c->nSamples; i++) sum += c->samples[i];
            c->suggested = sum / (float)c->nSamples;
            c->lastDelta = (float)dms;
            c->lastTapT  = now;
        }
    }
}

void calib_draw(Calib *c, int sw, int sh) {
    ClearBackground((Color){ 10, 10, 18, 255 });
    const char *title = "AUDIO OFFSET CALIBRATION";
    DrawText(title, sw/2 - MeasureText(title, 30)/2, 56, 30, RAYWHITE);
    const char *l1 = "Tap SPACE in time with the metronome.";
    const char *l2 = "Stay steady for about ten beeps, then confirm.";
    DrawText(l1, sw/2 - MeasureText(l1, 20)/2, 112, 20, (Color){ 200, 210, 235, 255 });
    DrawText(l2, sw/2 - MeasureText(l2, 18)/2, 140, 18, (Color){ 150, 160, 190, 255 });

    double now     = GetTime();
    double elapsed = now - (c->startT + CALIB_LEADIN);
    float  phase   = 0.0f;
    if (elapsed >= 0.0) { double f = elapsed / CALIB_PERIOD; phase = (float)(f - floor(f)); }
    float pulse = 1.0f - phase;                 /* 1 juste apres le bip -> 0 */
    int cx = sw/2, cy = sh/2 + 4;
    float baseR = 48.0f, r = baseR * (0.7f + 0.5f * pulse);
    Color ring = (elapsed < 0.0)
        ? (Color){ 90, 90, 110, 255 }
        : (Color){ 90, 170, 255, (unsigned char)(110.0f + 140.0f * pulse) };
    DrawCircleLines(cx, cy, r, ring);
    DrawCircle(cx, cy, baseR * 0.22f, (Color){ 90, 170, 255, 200 });
    if (elapsed < 0.0) {
        const char *rdy = "Get ready...";
        DrawText(rdy, cx - MeasureText(rdy, 18)/2, cy + (int)baseR + 14, 18, (Color){ 150, 160, 190, 255 });
    }
    if (c->lastTapT > 0.0 && now - c->lastTapT < 0.12) {
        Color fc = (fabsf(c->lastDelta) < 25.0f) ? (Color){ 120, 255, 160, 220 }
                                                  : (Color){ 255, 200, 120, 220 };
        DrawCircleLines(cx, cy, baseR * 1.7f, fc);
    }

    const char *cnt = TextFormat("Beeps tapped : %d", c->nSamples);
    DrawText(cnt, sw/2 - MeasureText(cnt, 20)/2, sh/2 + 96, 20, RAYWHITE);
    if (c->nSamples > 0) {
        const char *sg = TextFormat("Suggested offset : %+.0f ms", c->suggested);
        DrawText(sg, sw/2 - MeasureText(sg, 26)/2, sh/2 + 126, 26, (Color){ 150, 255, 180, 255 });
        const char *ld = TextFormat("(last delta : %+.0f ms)", c->lastDelta);
        DrawText(ld, sw/2 - MeasureText(ld, 16)/2, sh/2 + 160, 16, (Color){ 150, 160, 190, 255 });
    } else {
        const char *sg = "Suggested offset : --";
        DrawText(sg, sw/2 - MeasureText(sg, 26)/2, sh/2 + 126, 26, (Color){ 120, 130, 160, 255 });
    }

    const char *foot = "ENTER : apply    R : restart    ESC : cancel";
    DrawText(foot, sw/2 - MeasureText(foot, 16)/2, sh - 44, 16, (Color){ 140, 150, 175, 255 });
}