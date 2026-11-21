/* Denboku.exe: starts bin\denboku.exe, which needs its DLLs next to it. An update the game unpacked into
 * update\ (update\ready names its folder) is put in place first, since files in use cannot be replaced
 * while the game runs: each file it replaces goes to old\, and comes back if the new game fails within
 * 15 seconds; the game removes old\ once it has run a while (src/core/updater.c). */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static wchar_t base[MAX_PATH];

/* base + a + "\" + b, in out (MAX_PATH). */
static BOOL path(wchar_t *out, const wchar_t *a, const wchar_t *b)
{
    if (lstrlenW(base) + lstrlenW(a) + (b ? lstrlenW(b) + 1 : 0) >= MAX_PATH)
        return FALSE;
    lstrcpyW(out, base);
    lstrcatW(out, a);
    if (b) {
        lstrcatW(out, L"\\");
        lstrcatW(out, b);
    }
    return TRUE;
}

static void remove_tree(const wchar_t *dir)
{
    wchar_t pattern[MAX_PATH], child[MAX_PATH];
    if (lstrlenW(dir) + 3 >= MAX_PATH)
        return;
    lstrcpyW(pattern, dir);
    lstrcatW(pattern, L"\\*");
    WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileW(pattern, &f);
    while (h != INVALID_HANDLE_VALUE) {
        if (lstrcmpW(f.cFileName, L".") && lstrcmpW(f.cFileName, L"..") && lstrlenW(dir) + lstrlenW(f.cFileName) + 2 < MAX_PATH) {
            lstrcpyW(child, dir);
            lstrcatW(child, L"\\");
            lstrcatW(child, f.cFileName);
            if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                remove_tree(child);
            else
                DeleteFileW(child);
        }
        if (!FindNextFileW(h, &f))
            break;
    }
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    RemoveDirectoryW(dir);
}

/* A rename, tried for a few seconds: the game that asked for the restart may still be closing. */
static BOOL move(const wchar_t *from, const wchar_t *to)
{
    for (int i = 0; i < 50; i++) {
        if (MoveFileExW(from, to, 0))
            return TRUE;
        Sleep(200);
    }
    return FALSE;
}

static BOOL exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

/* Every entry of old\ back in its place; old\ goes only once all of it is back (else the next start
 * tries again). The game failed: the mark becomes .update-failed, and the game skips that version. */
static void restore(BOOL game_failed)
{
    wchar_t old[MAX_PATH], pattern[MAX_PATH], from[MAX_PATH], to[MAX_PATH];
    if (!path(old, L"old", NULL) || !path(pattern, L"old", L"*"))
        return;
    BOOL back = TRUE;
    WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileW(pattern, &f);
    while (h != INVALID_HANDLE_VALUE) {
        if (lstrcmpW(f.cFileName, L".") && lstrcmpW(f.cFileName, L"..") && path(from, L"old", f.cFileName) && path(to, f.cFileName, NULL)) {
            for (int i = 0; i < 50 && exists(to); i++) { /* the game's engine may still be closing */
                if (GetFileAttributesW(to) & FILE_ATTRIBUTE_DIRECTORY)
                    remove_tree(to);
                else
                    DeleteFileW(to);
                if (exists(to))
                    Sleep(200);
            }
            if (exists(to) || !move(from, to))
                back = FALSE;
        }
        if (!FindNextFileW(h, &f))
            break;
    }
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    if (!back)
        return;
    remove_tree(old);
    wchar_t failed[MAX_PATH];
    if (path(to, L".update-pending", NULL) && path(failed, L".update-failed", NULL)) {
        if (game_failed)
            MoveFileExW(to, failed, MOVEFILE_REPLACE_EXISTING);
        else
            DeleteFileW(to);
    }
}

/* An update put in place that has not run a while yet (the game removes the mark then). */
static BOOL pending(void)
{
    wchar_t mark[MAX_PATH], old[MAX_PATH];
    return path(mark, L".update-pending", NULL) && path(old, L"old", NULL) && exists(mark) && exists(old);
}

/* The update in update\<folder>\ moved in; FALSE (and nothing changed) if there is none or it fails. */
static BOOL apply_update(void)
{
    wchar_t ready[MAX_PATH], update[MAX_PATH], fresh[MAX_PATH], old[MAX_PATH], pattern[MAX_PATH], from[MAX_PATH], to[MAX_PATH], keep[MAX_PATH];
    if (!path(ready, L"update", L"ready") || !path(update, L"update", NULL) || !path(old, L"old", NULL))
        return FALSE;
    HANDLE r = CreateFileW(ready, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (r == INVALID_HANDLE_VALUE)
        return FALSE;
    if (pending()) { /* the version before is still untried: it keeps old\; this one comes again */
        CloseHandle(r);
        remove_tree(update);
        return FALSE;
    }
    char name[64] = {0};
    DWORD got = 0;
    ReadFile(r, name, sizeof name - 1, &got, NULL);
    CloseHandle(r);
    wchar_t wname[64];
    if (!got || strchr(name, '\\') || strchr(name, '/') || strchr(name, '.') ||
        !MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 64) || !path(fresh, L"update", wname) ||
        lstrlenW(fresh) + 3 >= MAX_PATH) {
        remove_tree(update);
        return FALSE;
    }
    lstrcpyW(pattern, fresh);
    lstrcatW(pattern, L"\\*");
    WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileW(pattern, &f);
    if (h == INVALID_HANDLE_VALUE) { /* no such folder: nothing to put in place */
        remove_tree(update);
        return FALSE;
    }
    remove_tree(old);
    CreateDirectoryW(old, NULL);
    BOOL ok = TRUE;
    while (ok && h != INVALID_HANDLE_VALUE) {
        if (lstrcmpW(f.cFileName, L".") && lstrcmpW(f.cFileName, L"..")) {
            ok = lstrlenW(fresh) + lstrlenW(f.cFileName) + 2 < MAX_PATH && path(to, f.cFileName, NULL) && path(keep, L"old", f.cFileName);
            if (ok) {
                lstrcpyW(from, fresh);
                lstrcatW(from, L"\\");
                lstrcatW(from, f.cFileName);
                if (GetFileAttributesW(to) != INVALID_FILE_ATTRIBUTES)
                    ok = move(to, keep);
                ok = ok && move(from, to);
            }
        }
        if (!FindNextFileW(h, &f))
            break;
    }
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    if (ok && path(to, L".update-pending", NULL)) {
        HANDLE m = CreateFileW(to, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (m != INVALID_HANDLE_VALUE)
            CloseHandle(m);
    } else
        restore(FALSE);
    remove_tree(update);
    return ok;
}

static PWSTR args; /* the launcher's own, passed on (--help, --dev…) */

static BOOL start(PROCESS_INFORMATION *pi)
{
    wchar_t exe[MAX_PATH], line[MAX_PATH + 32768];
    STARTUPINFOW si = {.cb = sizeof si};
    if (!path(exe, L"bin\\denboku.exe", NULL))
        return FALSE;
    _snwprintf(line, sizeof line / sizeof *line, L"\"%ls\" %ls", exe, args ? args : L"");
    line[sizeof line / sizeof *line - 1] = 0;
    return CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, pi);
}

/* In the system's language: the launcher has no tr(). Opened from inside the zip, Windows unpacks only
 * the launcher to a temporary folder, without bin\. */
static const wchar_t *failure(void)
{
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_JAPANESE:
        return L"bin\\denboku.exe を起動できませんでした。zip の中から直接開いた場合は、先に zip をすべて展開し、"
               L"展開したフォルダーから開いてください。ファイルがない場合は、ウイルス対策ソフトが削除した可能性があります。"
               L"元に戻すか、もう一度展開してください。";
    case LANG_PORTUGUESE:
        return L"Não foi possível abrir bin\\denboku.exe. Se você o abriu de dentro do zip, extraia o zip inteiro "
               L"primeiro e abra a pasta extraída. Se o arquivo sumiu, um antivírus pode tê-lo removido: restaure-o "
               L"ou extraia o zip de novo.";
    default:
        return L"Could not start bin\\denboku.exe. If you opened it from inside the zip, extract the whole zip first "
               L"and open the extracted folder. If the file is missing, an antivirus may have removed it: restore it, "
               L"or extract the zip again.";
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void) inst, (void) prev, (void) show;
    args = cmd;
    DWORD n = GetModuleFileNameW(NULL, base, MAX_PATH);
    while (n && base[n - 1] != L'\\')
        n--;
    base[n] = 0;
    SetCurrentDirectoryW(base); /* not inside bin\, which the update renames */
    BOOL updated = apply_update() || pending();
    /* Options (--help): the game's text goes to the console the launcher was started from, which it
     * keeps until the game is done. */
    BOOL console = args && *args && AttachConsole(ATTACH_PARENT_PROCESS);
    PROCESS_INFORMATION pi;
    BOOL ok = start(&pi);
    if (ok && updated) {
        DWORD code = 0;
        if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code) && code != 0) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            restore(TRUE); /* the new version did not start: the one before it */
            ok = start(&pi);
        }
    } else if (!ok && updated) {
        restore(TRUE);
        ok = start(&pi);
    }
    if (!ok) {
        MessageBoxW(NULL, failure(), L"Denboku Sh\x14Dgi", MB_ICONERROR);
        return 1;
    }
    DWORD code = 0;
    if (console && WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int) code;
}
