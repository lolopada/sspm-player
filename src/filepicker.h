#pragma once
#include <stdbool.h>

/* Ouvre un dialog natif de sélection de dossier (Windows : IFileOpenDialog).
   Retourne true si l'utilisateur a confirmé, false si annulé ou erreur.
   Le chemin est écrit dans out (UTF-8, au plus outsz bytes, nul-terminé). */
bool pick_folder(char *out, int outsz, const char *title);

/* Ouvre un dialog natif de sélection de fichier.
   filterDesc : description affichée ("Images\0*.png;*.jpg\0\0") ou NULL = tous.
   Linux : filterExts : extensions pour zenity ("*.png *.jpg"), NULL = tous. */
bool pick_file(char *out, int outsz, const char *title,
               const char *filterDesc, const char *filterExts);
