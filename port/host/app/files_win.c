/* files.h on Windows. */
#include "files.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

/* `path` converted to UTF-16 (a new string, to be freed), or NULL. */
static wchar_t *wide(const char *path)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)malloc(sizeof(wchar_t) * (size_t)n) : NULL;
    if (w != NULL && MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}

/* Creates the folders above `w` that are missing. */
static void make_folders(const wchar_t *w)
{
    wchar_t *copy = _wcsdup(w);
    if (copy == NULL) {
        return;
    }
    for (wchar_t *c = copy; *c != L'\0'; ++c) {
        if ((*c == L'\\' || *c == L'/') && c != copy && c[-1] != L':') {
            const wchar_t keep = *c;
            *c = L'\0';
            CreateDirectoryW(copy, NULL);
            *c = keep;
        }
    }
    free(copy);
}

int khdays_host_write_file_atomic(const char *path, const void *data, size_t size)
{
    wchar_t *target = wide(path);
    wchar_t *temp;
    size_t length;
    HANDLE file;
    const unsigned char *at = (const unsigned char *)data;
    size_t left = size;
    int ok = 0;
    if (target == NULL) {
        fprintf(stderr, "host: the file name %s cannot be used\n", path);
        return 0;
    }
    /* the temporary file is beside it, named after this process, so two copies never share one */
    length = wcslen(target) + 32;
    temp = (wchar_t *)malloc(sizeof(wchar_t) * length);
    if (temp == NULL) {
        free(target);
        return 0;
    }
    swprintf(temp, length, L"%ls.%lu.tmp", target, (unsigned long)GetCurrentProcessId());
    make_folders(target);
    file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "host: cannot write beside %s (error %lu)\n", path, GetLastError());
        free(temp);
        free(target);
        return 0;
    }
    while (left > 0) {
        const DWORD chunk = left > 0x40000000u ? 0x40000000u : (DWORD)left;
        DWORD done = 0;
        if (!WriteFile(file, at, chunk, &done, NULL) || done == 0) {
            break;
        }
        at += done;
        left -= done;
    }
    ok = left == 0 && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok && !MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = 0;
    }
    if (!ok) {
        fprintf(stderr, "host: cannot write %s (error %lu); it stays as it was\n", path, GetLastError());
        DeleteFileW(temp);
    }
    free(temp);
    free(target);
    return ok;
}

long khdays_host_read_file(const char *path, void *data, size_t room)
{
    wchar_t *w = wide(path);
    HANDLE file;
    unsigned char *at = (unsigned char *)data;
    long total = 0;
    if (w == NULL) {
        return -1;
    }
    file = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(w);
    if (file == INVALID_HANDLE_VALUE) {
        return -1;
    }
    while ((size_t)total < room) {
        const size_t left = room - (size_t)total;
        DWORD done = 0;
        if (!ReadFile(file, at + total, left > 0x40000000u ? 0x40000000u : (DWORD)left, &done, NULL) || done == 0) {
            break;
        }
        total += (long)done;
    }
    CloseHandle(file);
    return total;
}

void khdays_host_local_time(int *hour, int *minute, int *second)
{
    SDL_Time now = 0;
    SDL_DateTime t;
    SDL_zero(t);
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &t, true)) {
        *hour = t.hour;
        *minute = t.minute;
        *second = t.second;
    } else {
        *hour = *minute = *second = 0;
    }
}
