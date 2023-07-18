#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "ansipixel.h"
#include "cbmp.h"
#include "imageutil.h"

struct Info {
    size_t nframes;
    float fps;
    size_t w, h;
    bool horizontal;
} INFO;

void sleepInUs(uint64_t us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = us % 1000000 * 1000;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR);
}

uint64_t nowInUs() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}

void readInfo(char* dir, long* ratio, size_t* height, size_t* width) {
    char name[1024] = {0};
    sprintf(name, "%s/index.txt", dir);
    FILE* findex = fopen(name, "r");
    if (!findex) {
        perror("");
        exit(1);
    }
    if (fscanf(findex, "%zu,%f,%zu,%zu",
            &INFO.nframes, &INFO.fps, &INFO.w, &INFO.h) != 4) {
        fputs("Incorrect index.txt format. Expects [nframes],[fps],[width],[height]",
            stderr);
        exit(1);
    }
    fclose(findex);


    printf("Frame dimensions: %zux%zu; NFrames: %zu; FPS: %f\n", INFO.w, INFO.h, INFO.nframes, INFO.fps);
    INFO.horizontal = INFO.w > INFO.h;

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    float hsample = (float)INFO.h / w.ws_row / 2;
    float wsample = (float)INFO.w / w.ws_col;
    *ratio = INFO.horizontal ?
        (hsample < wsample ? hsample : wsample) :
        (hsample > wsample ? hsample : wsample);
    *width = INFO.w / *ratio;
    *height = INFO.h / *ratio;

    printf("Downscale to %zux%zu\n", *width, *height);
}

void playFrames(
    struct AP_BufferRgb* buf,
    AP_ColorRgb** frames,
    size_t height,
    size_t width)
{
    size_t f;
    for (f = 0; f < INFO.nframes; f++) {
        uint64_t start = nowInUs();

        for (int i = 2; i < height; i++) {
            for (int j = 0; j < width; j++) {
                AP_BufferRgb_setPixel(buf, i, j, frames[f][i * width + j]);
            }
        }
        AP_BufferRgb_draw(buf);

        uint64_t end = nowInUs();
        uint64_t elapsed = end - start;
        const uint64_t waitTime = 1000000/INFO.fps;

        AP_move(0, 0);
        AP_resettextcolor();

        if (elapsed < waitTime) {
            sleepInUs(waitTime - elapsed);
        }

        {
            uint64_t end = nowInUs();
            uint64_t elapsed = end - start;
            const uint64_t waitTime = 1000000/INFO.fps;
            printf("%f\n", 1000000.0 / elapsed);
        }
    }
}

#define min(x, y) ((x) < (y) ? (x): (y))

int main(int argc, char** argv) {
    if (argc < 2) {
        return 0;
    }

    char* dir = argv[1];
    printf("Reading frames from %s directory\n", dir);
    size_t width, height;
    long ratio;
    readInfo(dir, &ratio, &height, &width);

    AP_ColorRgb** frames = calloc(INFO.nframes, sizeof(*frames));
    pthread_mutex_t counter_mutex;
    pthread_mutex_init(&counter_mutex, NULL);
    size_t counter = 0;

    uint64_t startPreprocess = nowInUs();

    #pragma omp parallel for
    for (int f = 0; f < INFO.nframes; f++) {
        pthread_mutex_lock(&counter_mutex);
        counter++;
        size_t localCounter = counter;
        pthread_mutex_unlock(&counter_mutex);

        uint64_t now = nowInUs();
        uint64_t timeElapsed = now - startPreprocess;
        printf("\e[2K\e[GProcessing frame %zu/%zu, FPS: %.3f", counter, INFO.nframes, counter / (timeElapsed / 1000000.f));
        fflush(stdout);

        char name[100] = {0};
        sprintf(name, "%s/%d.bmp", dir, f+1);
        BMP* bmp = bopen(name);
        frames[f] = malloc(height*width*sizeof(*frames[f]));

        AP_ColorRgb* source = malloc(
            INFO.h*INFO.w*sizeof(*source));
        for (int i = 0; i < INFO.h; i++) {
            for (int j = 0; j < INFO.w; j++) {
                unsigned char r, g, b;
                get_pixel_rgb(bmp, j, INFO.h - i - 1, &r, &g, &b);
                source[i*INFO.w + j] = AP_ColorRgb(r, g, b);
            }
        }
        frames[f] = resize_bicubic(&source, INFO.h, INFO.w, height, width);

        free(source);
        bclose(bmp);
    }

    AP_clearScreen();
    AP_showcursor(false);
    struct AP_BufferRgb* buf = AP_BufferRgb_new(
        height, width);

    playFrames(buf, frames, height, width);

    AP_BufferRgb_del(buf);
    AP_resettextcolor();
    AP_clearScreen();
    AP_showcursor(true);

    puts("");
    return 0;
}
