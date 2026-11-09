// Companion overlay state (runtime/companion.c): short messages and a direction beacon that a companion app (Hyrule
// Fieldnotes) or the console sets with the `notify` and `beacon` commands. Display only: nothing here reads or writes
// game memory. Drawn by runtime/ui/companion_overlay.mm.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMPANION_TEXT_CAP 281   // 280 bytes of UTF-8 and the terminator
#define COMPANION_NAME_CAP 81
#define COMPANION_TOASTS 3

typedef struct {
    char text[COMPANION_TEXT_CAP];
    char from[COMPANION_NAME_CAP];
    uint64_t until_ns;           // plat_now_ns() deadline
} CompanionToast;

typedef struct {
    bool active;
    char name[COMPANION_NAME_CAP];
    float x, z;
} CompanionBeacon;

// Copies of the current state (any thread). Returns how many toasts are still showing (newest first).
int companion_toasts(CompanionToast out[COMPANION_TOASTS], uint64_t now_ns);
bool companion_beacon(CompanionBeacon *out);
bool companion_overlay_wanted(void);  // a toast or a beacon is showing: the overlay must draw this frame

// For tests and front ends. Text is cleaned to printable characters; seconds clamp to 2..30.
bool companion_notify(const char *text, const char *from, double seconds, uint64_t now_ns);
void companion_notify_clear(void);
void companion_beacon_set(const char *name, float x, float z);
void companion_beacon_clear(void);

void companion_init(void);  // registers `notify` and `beacon`

#ifdef __cplusplus
}
#endif
