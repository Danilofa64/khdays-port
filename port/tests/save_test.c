/* khdays-save-test [FOLDER]: the save written so a crash cannot corrupt it (khdays_host_write_file_atomic):
 * a child (--child PATH) writes two 64 KB images in turn and is stopped at a random moment, 100 times;
 * the file must be one of the two, whole. */
#include "../host/app/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SIZE 65536
#define STOPS 100

static unsigned char images[2][SIZE];

static void make_images(void)
{
    for (int i = 0; i < SIZE; ++i) {
        images[0][i] = (unsigned char)(0xa5 ^ i);
        images[1][i] = (unsigned char)(i * 7 + 3);
    }
}

static int child(const char *path)
{
    for (unsigned n = 0;; ++n) {
        if (!khdays_host_write_file_atomic(path, images[n & 1], SIZE)) {
            return 2;
        }
    }
}

int main(int argc, char **argv)
{
    char folder[MAX_PATH], path[MAX_PATH + 32], command[2 * MAX_PATH + 64], self[MAX_PATH];
    static unsigned char read[SIZE + 1];
    int seen[2] = {0, 0};
    make_images();
    if (argc == 3 && strcmp(argv[1], "--child") == 0) {
        return child(argv[2]);
    }
    if (argc >= 2) {
        snprintf(folder, sizeof(folder), "%s", argv[1]);
    } else {
        GetTempPathA(sizeof(folder), folder);
    }
    snprintf(path, sizeof(path), "%s\\khdays-save-test-%lu.sav", folder, (unsigned long)GetCurrentProcessId());
    GetModuleFileNameA(NULL, self, sizeof(self));
    if (!khdays_host_write_file_atomic(path, images[0], SIZE)) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    srand((unsigned)GetTickCount());
    for (int stop = 0; stop < STOPS; ++stop) {
        STARTUPINFOA startup = {sizeof(startup)};
        PROCESS_INFORMATION process;
        long n;
        int which = -1;
        snprintf(command, sizeof(command), "\"%s\" --child \"%s\"", self, path);
        if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
            fprintf(stderr, "cannot start the writer (error %lu)\n", GetLastError());
            return 1;
        }
        Sleep((DWORD)(20 + rand() % 100)); /* the writer started, then stopped mid-way */
        TerminateProcess(process.hProcess, 9);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        n = khdays_host_read_file(path, read, sizeof(read));
        for (int k = 0; k < 2; ++k) {
            if (n == SIZE && memcmp(read, images[k], SIZE) == 0) {
                which = k;
            }
        }
        if (which < 0) {
            fprintf(stderr, "stop %d: the file is neither image whole (%ld bytes)\n", stop + 1, n);
            return 1;
        }
        ++seen[which];
    }
    {
        /* the writers' temporary files left by the stops: removed */
        char pattern[MAX_PATH + 48];
        WIN32_FIND_DATAA found;
        HANDLE find;
        snprintf(pattern, sizeof(pattern), "%s.*.tmp", path);
        find = FindFirstFileA(pattern, &found);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                char leftover[2 * MAX_PATH];
                snprintf(leftover, sizeof(leftover), "%s\\%s", folder, found.cFileName);
                DeleteFileA(leftover);
            } while (FindNextFileA(find, &found));
            FindClose(find);
        }
    }
    DeleteFileA(path);
    printf("%d interrupted saves: the file whole each time (the first image %d times, the second %d)\n", STOPS,
           seen[0], seen[1]);
    return 0;
}
