// nfp:user, the amiibo service, with one virtual reader (local addition; it used to report no reader). The cheat panel
// "places" a figure on it (nfp_virtual_place); the game's next detection finds it, as if the figure were tapped.
// Command numbers, structure layouts and result codes follow switchbrew and the open-source emulators (yuzu's
// nfp_types.h; Ryujinx's VirtualAmiibo for the ModelInfo byte order). A figure's ID is the 8-byte amiibo ID
// (character, variant, type, model number, series). Each scan gets a fresh random UID, so the game's
// once-a-day-per-figure limit does not apply. The game's own data on the figure (its application area: Wolf Link's
// hearts, for example) is kept in <data root>/cheats/amiibo/<id>.bin.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../hle.h"
#include "../ipc.h"
#include "../kernel.h"
#include "../rt.h"
#include "../platform.h"
#include "nfp.h"

#define NFP_RESULT(desc) (115u | ((uint32_t)(desc) << 9))
#define R_WRONG_STATE NFP_RESULT(73)
#define R_TAG_REMOVED NFP_RESULT(97)
#define R_APP_AREA_MISSING NFP_RESULT(128)
#define R_APP_AREA_EXISTS NFP_RESULT(168)
#define R_WRONG_APP_AREA_ID NFP_RESULT(152)
#define APP_AREA_SIZE 0xD8

enum { S_INITIALIZED, S_SEARCHING, S_FOUND, S_REMOVED, S_MOUNTED, S_UNAVAILABLE, S_FINALIZED };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_state = S_INITIALIZED;
static uint64_t g_placed;      // figure waiting on the reader (0: none)
static uint64_t g_tag;         // figure being read
static uint8_t g_uid[7];
static KEvent *g_activate, *g_deactivate;
static uint32_t g_activate_r, g_deactivate_r;
static uint8_t g_area[APP_AREA_SIZE];
static uint32_t g_area_id;     // the game's access id for the area (0: none created)
static bool g_area_open;
static char g_last[96] = "No figure placed.";
static bool g_mounted_once;  // the game read the figure in this detection (else it stays on the reader)
static int g_gen;            // detection generation, for the delayed tap

static void signal_ev(KEvent *ev) {
    if (!ev) return;
    klock();
    kernel_signal(ev);
    pthread_mutex_unlock(&g_klock);
}

static void area_path(uint64_t id, char *out, size_t n) { snprintf(out, n, "cheats/amiibo/%016llx.bin", (unsigned long long)id); }
static void load_area(uint64_t id) {
    char p[96];
    area_path(id, p, sizeof p);
    g_area_id = 0;
    memset(g_area, 0, sizeof g_area);
    FILE *f = fopen(p, "rb");
    if (!f) return;
    uint32_t aid;
    if (fread(&aid, 4, 1, f) == 1 && fread(g_area, 1, APP_AREA_SIZE, f) == APP_AREA_SIZE) g_area_id = aid;
    fclose(f);
}
static void save_area(void) {
    char p[96];
    area_path(g_tag, p, sizeof p);
#ifdef _WIN32
    extern int _mkdir(const char *);
    _mkdir("cheats");
    _mkdir("cheats/amiibo");
#endif
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fwrite(&g_area_id, 4, 1, f);
    fwrite(g_area, 1, APP_AREA_SIZE, f);
    fclose(f);
}

// The reader finds the placed figure (caller holds g_lock).
static void detect(void) {
    if (!g_placed || g_state != S_SEARCHING) return;
    g_tag = g_placed;
    g_mounted_once = false;
    for (int i = 0; i < 7; i++) g_uid[i] = (uint8_t)(rand() & 0xff);
    g_uid[0] = 0x04;  // (NXP's manufacturer byte, as on real NTAG215 figures)
    load_area(g_tag);
    g_state = S_FOUND;
    snprintf(g_last, sizeof g_last, "The game found the figure...");
    LOG("[nfp] t=%llu tap\n", (unsigned long long)(plat_now_ns() / 1000000ull));
    LOG("[nfp] virtual figure %016llx detected\n", (unsigned long long)g_tag);
    signal_ev(g_activate);
}

// The tap comes a moment after detection starts: the game clears the activate event right after StartDetection and
// only then waits on it, so a signal sent inside StartDetection itself was lost.
static void *delayed_tap(void *arg) {
    int gen = (int)(intptr_t)arg;
    struct timespec ts = {0, 150 * 1000 * 1000};
    nanosleep(&ts, NULL);
    pthread_mutex_lock(&g_lock);
    if (gen == g_gen) detect();
    pthread_mutex_unlock(&g_lock);
    return NULL;
}
static void tap_soon(void) {  // (caller holds g_lock)
    if (!g_placed || g_state != S_SEARCHING) return;
    pthread_t t;
    if (!pthread_create(&t, NULL, delayed_tap, (void *)(intptr_t)++g_gen)) pthread_detach(t);
}

void nfp_virtual_place(uint64_t amiibo_id) {
    pthread_mutex_lock(&g_lock);
    g_placed = amiibo_id;
    snprintf(g_last, sizeof g_last, "Figure on the reader: use the amiibo rune in the game.");
    tap_soon();  // (the game may already be waiting)
    pthread_mutex_unlock(&g_lock);
}
void nfp_virtual_status(char *out, int n) {
    pthread_mutex_lock(&g_lock);
    snprintf(out, (size_t)n, "%s", g_last);
    pthread_mutex_unlock(&g_lock);
}

static void put_buf(IpcReq *req, int i, const void *src, size_t n) {
    IpcBuf *b = out_buf(req, i);
    if (!b || !b->addr) return;
    size_t k = n < b->size ? n : b->size;
    memset((void *)(uintptr_t)b->addr, 0, b->size);
    memcpy((void *)(uintptr_t)b->addr, src, k);
}

static void nfp_user(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    pthread_mutex_lock(&g_lock);
    bool have = g_state == S_FOUND || g_state == S_MOUNTED;
    switch (req->cmd) {
    case 0: g_state = S_INITIALIZED; break;                                   // Initialize
    case 1: g_state = S_FINALIZED; break;                                     // Finalize
    case 2: {                                                                 // ListDevices: one reader (player 1)
        uint64_t handle = 0;
        put_buf(req, 0, &handle, sizeof handle);
        OUT(int32_t, 0) = 1;
        resp_size(resp, 4);
        break;
    }
    case 3: g_state = S_SEARCHING; tap_soon(); break;                         // StartDetection
    case 4:                                                                   // StopDetection
        if (g_state == S_FOUND || g_state == S_MOUNTED) signal_ev(g_deactivate);
        if (g_mounted_once && g_placed == g_tag) {  // read: the figure leaves the reader
            g_placed = 0;
            snprintf(g_last, sizeof g_last, "Figure read by the game. Place it again for another scan.");
        }
        g_gen++;  // (a pending tap is for this detection only)
        g_state = S_INITIALIZED;
        g_area_open = false;
        break;
    case 5: if (have) { g_state = S_MOUNTED; g_mounted_once = true; } else resp->result = R_WRONG_STATE; break;  // Mount
    case 6: if (have) g_state = S_FOUND; g_area_open = false; break;                    // Unmount
    case 7: {                                                                 // OpenApplicationArea(access id)
        uint32_t aid = IN(uint32_t, 8);
        if (g_state != S_MOUNTED) resp->result = R_WRONG_STATE;
        else if (!g_area_id) resp->result = R_APP_AREA_MISSING;
        else if (g_area_id != aid) resp->result = R_WRONG_APP_AREA_ID;
        else g_area_open = true;
        break;
    }
    case 8:                                                                   // GetApplicationArea
        if (g_state != S_MOUNTED || !g_area_open) { resp->result = R_WRONG_STATE; break; }
        put_buf(req, 0, g_area, APP_AREA_SIZE);
        OUT(uint32_t, 0) = APP_AREA_SIZE;
        resp_size(resp, 4);
        break;
    case 9: {                                                                 // SetApplicationArea
        IpcBuf *b = in_buf(req, 0);
        if (g_state != S_MOUNTED || !g_area_open || !b) { resp->result = R_WRONG_STATE; break; }
        memset(g_area, 0, sizeof g_area);
        memcpy(g_area, (void *)(uintptr_t)b->addr, b->size < APP_AREA_SIZE ? b->size : APP_AREA_SIZE);
        break;
    }
    case 10: if (g_state == S_MOUNTED) save_area(); else resp->result = R_WRONG_STATE; break;  // Flush
    case 11: break;                                                           // Restore
    case 12: case 24: {                                                       // Create / RecreateApplicationArea
        IpcBuf *b = in_buf(req, 0);
        if (g_state != S_MOUNTED) { resp->result = R_WRONG_STATE; break; }
        if (req->cmd == 12 && g_area_id) { resp->result = R_APP_AREA_EXISTS; break; }
        g_area_id = IN(uint32_t, 8);
        memset(g_area, 0, sizeof g_area);
        if (b) memcpy(g_area, (void *)(uintptr_t)b->addr, b->size < APP_AREA_SIZE ? b->size : APP_AREA_SIZE);
        save_area();
        break;
    }
    case 13: {                                                                // GetTagInfo (0x58)
        if (!have) { resp->result = g_state == S_REMOVED ? R_TAG_REMOVED : R_WRONG_STATE; break; }
        uint8_t t[0x58] = {0};
        memcpy(t, g_uid, 7);
        t[10] = 7;                          // uid length
        t[0x20] = 1;                        // protocol: type A
        t[0x24] = 2;                        // tag type: type 2 (NTAG215)
        put_buf(req, 0, t, sizeof t);
        break;
    }
    case 14: {                                                                // GetRegisterInfo (0x100)
        if (g_state != S_MOUNTED) { resp->result = R_WRONG_STATE; break; }
        uint8_t r[0x100] = {0};
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        uint16_t y = (uint16_t)(tm->tm_year + 1900);
        memcpy(r + 0x58, &y, 2); r[0x5a] = (uint8_t)(tm->tm_mon + 1); r[0x5b] = (uint8_t)tm->tm_mday;
        const char *name = "amiibo";  // (UTF-16)
        for (int i = 0; name[i]; i++) r[0x5c + 2 * i] = (uint8_t)name[i];
        put_buf(req, 0, r, sizeof r);
        break;
    }
    case 15: {                                                                // GetCommonInfo (0x40)
        if (g_state != S_MOUNTED) { resp->result = R_WRONG_STATE; break; }
        uint8_t c[0x40] = {0};
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        uint16_t y = (uint16_t)(tm->tm_year + 1900);
        uint32_t size = APP_AREA_SIZE;
        memcpy(c, &y, 2); c[2] = (uint8_t)(tm->tm_mon + 1); c[3] = (uint8_t)tm->tm_mday;
        memcpy(c + 8, &size, 4);
        put_buf(req, 0, c, sizeof c);
        break;
    }
    case 16: {                                                                // GetModelInfo (0x40)
        if (g_state != S_MOUNTED) { resp->result = R_WRONG_STATE; break; }
        uint8_t m[0x40] = {0};
        uint8_t id[8];
        for (int i = 0; i < 8; i++) id[i] = (uint8_t)(g_tag >> (56 - 8 * i));  // the ID's bytes as on the figure
        m[0] = id[0]; m[1] = id[1];          // character id: the figure's byte order
        m[2] = id[2];                        // variant
        m[3] = id[3];                        // type (figure, card, yarn...)
        m[4] = id[5]; m[5] = id[4];          // model number: native order
        m[6] = id[6];                        // series
        put_buf(req, 0, m, sizeof m);
        break;
    }
    // A fresh event each time: the game attaches, finalizes (closing its handles), initializes and attaches again;
    // handing back the first handle left it waiting on a closed one, so a found figure was never read.
    case 17:                                                                  // AttachActivateEvent
        g_activate = kernel_create_event_pair(&(uint32_t){0}, &g_activate_r);
        resp->copy_handles[resp->ncopy++] = g_activate_r;
        break;
    case 18:                                                                  // AttachDeactivateEvent
        g_deactivate = kernel_create_event_pair(&(uint32_t){0}, &g_deactivate_r);
        resp->copy_handles[resp->ncopy++] = g_deactivate_r;
        break;
    case 19: OUT(uint32_t, 0) = g_state == S_FINALIZED ? 0 : 1; resp_size(resp, 4); break;   // GetState
    case 20: OUT(uint32_t, 0) = (uint32_t)g_state; resp_size(resp, 4); break;               // GetDeviceState
    case 21: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;                                // GetNpadId
    case 22: OUT(uint32_t, 0) = APP_AREA_SIZE; resp_size(resp, 4); break;                    // GetApplicationAreaSize
    case 23: {                                                                // AttachAvailabilityChangeEvent
        uint32_t w, r;
        kernel_create_event_pair(&w, &r);
        resp->copy_handles[resp->ncopy++] = r;
        break;
    }
    default: LOG("[nfp] IUser cmd %u (ok)\n", req->cmd);
    }
    {   // (all commands, timed: the game's detection loop decides how the reader must behave)
        static uint32_t last_cmd = 999;
        static int last_state = -1, repeats;
        bool poll = req->cmd == 19 || req->cmd == 20;  // (polled about 9 times a second: logged on a change only)
        if ((poll ? g_state != last_state : req->cmd != last_cmd || g_state != last_state) || ++repeats % 2000 == 0) {
            LOG("[nfp] t=%llu cmd %u -> state %d result %x\n", (unsigned long long)(plat_now_ns() / 1000000ull), req->cmd,
                g_state, resp->result);
            last_cmd = req->cmd;
            last_state = g_state;
            repeats = 0;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

SERVICE("nfp:user", nfp_user_manager) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("IUser(nfp)", nfp_user, NULL));  // CreateUserInterface
    else LOG("[nfp] IUserManager cmd %u (ok)\n", req->cmd);
}
