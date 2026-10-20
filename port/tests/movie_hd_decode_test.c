/* The HD video decoder (movie_hd_win.c) on one of the player's files, played to its end at its own pace
 * with its sound, every picture taken as a 60 Hz display would: it must end within a second of the
 * file's length, or of the end asked for (a third argument, khdays_hd_end_at), no picture past it.
 * Without the file the test is skipped (the videos are the player's, never the repo's). */
#include "../host/movies/movie_hd.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "data/cutscenes/cinematics/hd808.mp4";
    const double end = argc > 3 ? atof(argv[3]) : 0.0;
    const double length = end > 0.0 ? end : argc > 2 ? atof(argv[2]) : 7.2;
    KhdaysHdInfo info;
    char why[320];
    FILE *f = fopen(path, "rb");
    Uint64 start, last_change;
    long pictures = 0;
    double latest = 0.0; /* the last picture's time */
    int status = 0;
    if (f == NULL) {
        printf("movie_hd_decode_test: no %s; skipped\n", path);
        return 0;
    }
    fclose(f);
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        printf("movie_hd_decode_test: no audio (%s)\n", SDL_GetError());
        return 1;
    }
    if (!khdays_hd_open(path, &info, why, sizeof(why))) {
        printf("movie_hd_decode_test: %s cannot be played: %s\n", path, why);
        return 1;
    }
    printf("movie_hd_decode_test: %s: %dx%d, %.3f frames a second, %d channels at %d Hz\n", path, info.width,
           info.height, info.fps, info.channels, info.rate);
    khdays_hd_set_gain(0.0f); /* silent: a test */
    khdays_hd_end_at(end);
    khdays_hd_start();
    start = last_change = SDL_GetTicks();
    while (SDL_GetTicks() - start < (Uint64)((length + 5.0) * 1000.0)) {
        KhdaysHdPicture p;
        if (khdays_hd_picture_begin(&p)) {
            ++pictures;
            latest = p.time;
            khdays_hd_picture_done();
        }
        status = khdays_hd_status(why, sizeof(why));
        if (status != 0) {
            break;
        }
        SDL_Delay(16);
    }
    {
        const double took = (double)(SDL_GetTicks() - start) / 1000.0;
        printf("movie_hd_decode_test: status %d after %.2f s, %ld pictures taken, the last at %.3f s%s%s\n", status,
               took, pictures, latest, status == -1 ? ": " : "", status == -1 ? why : "");
        khdays_hd_close();
        SDL_Quit();
        return status == 1 && took < length + 1.0 && took > length - 1.5 && (end == 0.0 || latest < end) ? 0 : 1;
    }
}
