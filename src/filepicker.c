#include "filepicker.h"

#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <shobjidl.h>

/* GetWindowHandle() de raylib : plus fiable que GetActiveWindow() qui peut renvoyer
   NULL si la fenetre n'est pas "active" au sens Win32 au moment de l'appel. */
extern void *GetWindowHandle(void);

/* SW_HIDE/SW_SHOW plutot que SW_MINIMIZE/SW_RESTORE : en mode borderless windowed
   la surface OpenGL reste parfois visible meme apres une minimisation (rendu
   asynchrone), alors que HIDE la retire immediatement de l'ecran. */
static HWND hide_game_window(void) {
    HWND h = (HWND)GetWindowHandle();
    if (h) ShowWindow(h, SW_HIDE);
    return h;
}
static void restore_game_window(HWND h) {
    if (h) ShowWindow(h, SW_SHOW);
}

bool pick_folder(char *out, int outsz, const char *title) {
    bool result = false;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninit = (hr == S_OK);
    /* S_FALSE = déjà initialisé par un autre code ; on ne doit pas uninit dans ce cas */

    IFileOpenDialog *pfd = NULL;
    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                          &IID_IFileOpenDialog, (void **)&pfd);
    if (FAILED(hr)) goto done;

    DWORD opts = 0;
    IFileOpenDialog_GetOptions(pfd, &opts);
    IFileOpenDialog_SetOptions(pfd, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    if (title) {
        wchar_t wtitle[256] = { 0 };
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
        IFileOpenDialog_SetTitle(pfd, wtitle);
    }

    HWND gameWnd = hide_game_window();
    hr = IFileOpenDialog_Show(pfd, NULL);
    restore_game_window(gameWnd);
    if (FAILED(hr)) goto release;  /* annulé = HRESULT_FROM_WIN32(ERROR_CANCELLED) */

    IShellItem *psi = NULL;
    hr = IFileOpenDialog_GetResult(pfd, &psi);
    if (FAILED(hr)) goto release;

    PWSTR pszPath = NULL;
    hr = IShellItem_GetDisplayName(psi, SIGDN_FILESYSPATH, &pszPath);
    if (SUCCEEDED(hr)) {
        WideCharToMultiByte(CP_ACP, 0, pszPath, -1, out, outsz, NULL, NULL);
        CoTaskMemFree(pszPath);
        result = true;
    }
    IShellItem_Release(psi);

release:
    IFileOpenDialog_Release(pfd);
done:
    if (needUninit) CoUninitialize();
    return result;
}

bool pick_file(char *out, int outsz, const char *title,
               const char *filterDesc, const char *filterExts) {
    (void)filterExts;
    bool result = false;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninit = (hr == S_OK);

    IFileOpenDialog *pfd = NULL;
    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                          &IID_IFileOpenDialog, (void **)&pfd);
    if (FAILED(hr)) goto fdone;

    /* filtre de type si fourni : format "Description\0*.ext;*.ext2\0\0" */
    if (filterDesc) {
        /* On parse la chaine double-nul pour extraire description + spec. */
        const char *desc = filterDesc;
        const char *spec = desc + strlen(desc) + 1;
        if (*spec) {
            wchar_t wdesc[128] = { 0 }, wspec[256] = { 0 };
            MultiByteToWideChar(CP_UTF8, 0, desc, -1, wdesc, 128);
            MultiByteToWideChar(CP_UTF8, 0, spec, -1, wspec, 256);
            COMDLG_FILTERSPEC ft = { wdesc, wspec };
            IFileOpenDialog_SetFileTypes(pfd, 1, &ft);
        }
    }

    if (title) {
        wchar_t wtitle[256] = { 0 };
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
        IFileOpenDialog_SetTitle(pfd, wtitle);
    }

    HWND fGameWnd = hide_game_window();
    hr = IFileOpenDialog_Show(pfd, NULL);
    restore_game_window(fGameWnd);
    if (FAILED(hr)) goto frelease;

    IShellItem *psi = NULL;
    hr = IFileOpenDialog_GetResult(pfd, &psi);
    if (FAILED(hr)) goto frelease;

    PWSTR pszPath = NULL;
    hr = IShellItem_GetDisplayName(psi, SIGDN_FILESYSPATH, &pszPath);
    if (SUCCEEDED(hr)) {
        WideCharToMultiByte(CP_ACP, 0, pszPath, -1, out, outsz, NULL, NULL);
        CoTaskMemFree(pszPath);
        result = true;
    }
    IShellItem_Release(psi);

frelease:
    IFileOpenDialog_Release(pfd);
fdone:
    if (needUninit) CoUninitialize();
    return result;
}

#else
/* Linux : essaie zenity (GNOME), puis kdialog (KDE).
   Ces outils impriment le chemin choisi sur stdout et quittent avec 0,
   ou n'impriment rien et quittent avec 1 si l'utilisateur annule. */
#include <stdio.h>
#include <string.h>

bool pick_folder(char *out, int outsz, const char *title) {
    const char *t = title ? title : "Select folder";
    char cmd[640];
    FILE *f;

    /* zenity */
    snprintf(cmd, sizeof cmd,
             "zenity --file-selection --directory --title='%s' 2>/dev/null", t);
    f = popen(cmd, "r");
    if (f) {
        bool got = (fgets(out, outsz, f) != NULL);
        pclose(f);
        if (got && out[0]) {
            int len = (int)strlen(out);
            if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
            if (out[0]) return true;
        }
    }

    /* kdialog */
    snprintf(cmd, sizeof cmd,
             "kdialog --getexistingdirectory . --title '%s' 2>/dev/null", t);
    f = popen(cmd, "r");
    if (f) {
        bool got = (fgets(out, outsz, f) != NULL);
        pclose(f);
        if (got && out[0]) {
            int len = (int)strlen(out);
            if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
            if (out[0]) return true;
        }
    }

    out[0] = '\0';
    return false;
}

bool pick_file(char *out, int outsz, const char *title,
               const char *filterDesc, const char *filterExts) {
    (void)filterDesc;
    const char *t = title ? title : "Select file";
    const char *fe = (filterExts && filterExts[0]) ? filterExts : "";
    char cmd[768];
    FILE *f;

    /* zenity */
    if (fe[0])
        snprintf(cmd, sizeof cmd,
                 "zenity --file-selection --title='%s' --file-filter='%s' 2>/dev/null", t, fe);
    else
        snprintf(cmd, sizeof cmd,
                 "zenity --file-selection --title='%s' 2>/dev/null", t);
    f = popen(cmd, "r");
    if (f) {
        bool got = (fgets(out, outsz, f) != NULL);
        pclose(f);
        if (got && out[0]) {
            int len = (int)strlen(out);
            if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
            if (out[0]) return true;
        }
    }

    /* kdialog */
    if (fe[0])
        snprintf(cmd, sizeof cmd,
                 "kdialog --getopenfilename . '%s' --title '%s' 2>/dev/null", fe, t);
    else
        snprintf(cmd, sizeof cmd,
                 "kdialog --getopenfilename . --title '%s' 2>/dev/null", t);
    f = popen(cmd, "r");
    if (f) {
        bool got = (fgets(out, outsz, f) != NULL);
        pclose(f);
        if (got && out[0]) {
            int len = (int)strlen(out);
            if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
            if (out[0]) return true;
        }
    }

    out[0] = '\0';
    return false;
}
#endif
