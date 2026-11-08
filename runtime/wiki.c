// In-game guide (runtime/wiki.h). One worker thread does all network and disk work; the render thread only reads a
// snapshot under the lock. Network use happens only when the player opens a page or types in the guide's search box.
#include "wiki.h"

#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "rt.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

#define HOST "zeldawiki.wiki"
#define MAX_SECTIONS 96
#define CACHE_DAYS 30

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
// requests
static char g_want[160], g_want_hint[24], g_want_query[96];
static int g_want_page, g_want_suggest;
// results
static char *g_title, *g_status, *g_body;  // body: the sections' strings live in this one buffer
static WikiSection g_sec[MAX_SECTIONS];
static int g_nsec, g_state;
static char g_sugg[8][96];
static int g_nsugg;

bool wiki_available(void) {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

// ---- HTTP (WinHTTP; elsewhere the guide only offers "open in browser")
static char *http_get(const char *path, size_t *len) {
#ifdef _WIN32
    char *out = NULL;
    size_t n = 0;
    wchar_t wpath[2048];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 2048)) return NULL;
    HINTERNET s = WinHttpOpen(L"SavageWindLocalGuide/1.0 (in-game BotW guide)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, L"" HOST, INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", wpath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE) : NULL;
    if (r) WinHttpSetTimeouts(r, 5000, 8000, 8000, 15000);
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD code = 0, sz = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
        DWORD avail;
        while (code == 200 && WinHttpQueryDataAvailable(r, &avail) && avail && n < (8u << 20)) {
            char *grown = realloc(out, n + avail + 1);
            if (!grown) break;
            out = grown;
            DWORD got = 0;
            if (!WinHttpReadData(r, out + n, avail, &got) || !got) break;
            n += got;
        }
        if (code != 200) { free(out); out = NULL; }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    if (out) { out[n] = 0; *len = n; }
    return out;
#else
    (void)path; (void)len;
    return NULL;
#endif
}

static void url_encode(const char *in, char *out, size_t n) {
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && k + 4 < n; p++) {
        if (isalnum(*p) || strchr("-_.~", *p)) out[k++] = (char)*p;
        else if (*p == ' ') out[k++] = '+';
        else k += (size_t)snprintf(out + k, n - k, "%%%02X", *p);
    }
    out[k] = 0;
}

// ---- tiny JSON string reading (the API's answers are simple)
static void put_utf8(char **o, unsigned cp) {
    char *p = *o;
    if (cp < 0x80) *p++ = (char)cp;
    else if (cp < 0x800) { *p++ = (char)(0xC0 | cp >> 6); *p++ = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { *p++ = (char)(0xE0 | cp >> 12); *p++ = (char)(0x80 | (cp >> 6 & 63)); *p++ = (char)(0x80 | (cp & 63)); }
    else { *p++ = (char)(0xF0 | cp >> 18); *p++ = (char)(0x80 | (cp >> 12 & 63)); *p++ = (char)(0x80 | (cp >> 6 & 63)); *p++ = (char)(0x80 | (cp & 63)); }
    *o = p;
}
// s points just after an opening quote; returns a malloc'd UTF-8 string and sets *end past the closing quote
static char *json_str(const char *s, const char **end) {
    char *out = malloc(strlen(s) + 1), *o = out;
    if (!out) return NULL;
    while (*s && *s != '"') {
        if (*s != '\\') { *o++ = *s++; continue; }
        s++;
        switch (*s) {
        case 'n': *o++ = '\n'; s++; break;
        case 't': *o++ = ' '; s++; break;
        case 'r': s++; break;
        case 'b': case 'f': s++; break;
        case 'u': {
            unsigned cp = (unsigned)strtoul((char[5]){s[1], s[2], s[3], s[4], 0}, NULL, 16);
            s += 5;
            if (cp >= 0xD800 && cp < 0xDC00 && s[0] == '\\' && s[1] == 'u') {
                unsigned lo = (unsigned)strtoul((char[5]){s[2], s[3], s[4], s[5], 0}, NULL, 16);
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                s += 6;
            }
            put_utf8(&o, cp);
            break;
        }
        default: if (*s) *o++ = *s++;
        }
    }
    *o = 0;
    if (end) *end = *s ? s + 1 : s;
    return out;
}
static char *json_field(const char *json, const char *key) {  // first "key":"..." value
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = json ? strstr(json, pat) : NULL;
    return p ? json_str(p + strlen(pat), NULL) : NULL;
}

// ---- making a page smart for Breath of the Wild
static const char *k_games[] = {
    "The Legend of Zelda", "Zelda II", "A Link to the Past", "Link's Awakening", "Ocarina of Time", "Majora's Mask",
    "Oracle of Seasons", "Oracle of Ages", "Four Swords", "The Wind Waker", "The Minish Cap", "Twilight Princess",
    "Phantom Hourglass", "Spirit Tracks", "Skyward Sword", "A Link Between Worlds", "Tri Force Heroes",
    "Tears of the Kingdom", "Echoes of Wisdom", "Hyrule Warriors", "Age of Calamity", "Cadence of Hyrule", "Super Smash Bros",
};
static const char *k_drop[] = {"Nomenclature", "Gallery", "See Also", "References", "Notes", "Names in Other Regions",
                               "Translations", "Etymology", "Other Appearances", "Navigation", "External Links"};
static const char *k_spoil[] = {"Solution", "Walkthrough", "Themes and Navigation", "Treasure", "Puzzle", "Strategy",
                                "Completion", "How to Complete"};
static bool has(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) if (!strncasecmp(hay, needle, n)) return true;
    return false;
}
static bool any_of(const char *h, const char **list, size_t n, bool exact) {
    for (size_t i = 0; i < n; i++) if (exact ? !strcasecmp(h, list[i]) : has(h, list[i])) return true;
    return false;
}
#define COUNT(a) (sizeof a / sizeof *a)

// Split "== Heading ==" sections into g_sec (strings point into body, which is modified in place)
static void build_sections(char *body) {
    WikiSection all[MAX_SECTIONS];
    int n = 0;
    all[n++] = (WikiSection){"", body, 1, false};
    for (char *p = body; *p && n < MAX_SECTIONS; ) {
        char *nl = strchr(p, '\n');
        char *line = p;
        if (line[0] == '=' && line[1] == '=') {
            int lv = 0;
            while (line[lv] == '=') lv++;
            char *h = line + lv;
            while (*h == ' ') h++;
            char *e = nl ? nl : line + strlen(line);
            while (e > h && (e[-1] == '=' || e[-1] == ' ' || e[-1] == '\r')) e--;
            *line = 0;  // ends the previous section's text
            *e = 0;
            all[n++] = (WikiSection){h, nl ? nl + 1 : e, lv, false};
        }
        if (!nl) break;
        p = nl + 1;
    }
    // Which level-2 section is about Breath of the Wild (multi-game pages)?
    int botw = -1;
    bool multi = false;
    for (int i = 1; i < n; i++)
        if (all[i].level == 2 && any_of(all[i].heading, k_games, COUNT(k_games), false)) multi = true;
    for (int i = 1; i < n; i++)
        if (all[i].level == 2 && has(all[i].heading, "Breath of the Wild")) { botw = i; break; }
    g_nsec = 0;
    g_sec[g_nsec++] = all[0];
    // the BotW section and its subsections first
    if (botw > 0) {
        for (int i = botw; i < n && (i == botw || all[i].level > 2); i++) g_sec[g_nsec++] = all[i];
    }
    for (int i = 1; i < n; i++) {  // then the rest, without other games' sections and reference matter
        int top = i;
        while (top > 0 && all[top].level > 2) top--;
        if (botw > 0 && top == botw) continue;  // (placed above)
        if (multi && top > 0 && any_of(all[top].heading, k_games, COUNT(k_games), false)) continue;
        if (top > 0 && any_of(all[top].heading, k_drop, COUNT(k_drop), true)) continue;
        if (g_nsec < MAX_SECTIONS) g_sec[g_nsec++] = all[i];
    }
    for (int i = 0; i < g_nsec; i++) {
        g_sec[i].spoiler = any_of(g_sec[i].heading, k_spoil, COUNT(k_spoil), false);
        while (*g_sec[i].text == '\n') g_sec[i].text++;
    }
}

// ---- choosing the title
static void pick_title(const char *name, const char *hint, char *out, size_t n) {
    snprintf(out, n, "%s", name);
    char *paren = strstr(out, " (");  // armor levels "(+2)", actor names "(Weapon_...)", Korok placements
    if (paren && strcmp(hint, "shrine")) *paren = 0;
    if (!strcmp(hint, "korok")) snprintf(out, n, "Korok Seed");
    if (!strncmp(out, "Korok Seed near", 15)) snprintf(out, n, "Korok Seed");
}

static void cache_path(const char *title, char *out, size_t n) {
    char safe[160];
    size_t k = 0;
    for (const char *p = title; *p && k + 1 < sizeof safe; p++) safe[k++] = isalnum((unsigned char)*p) ? *p : '_';
    safe[k] = 0;
    snprintf(out, n, "cheats/wiki/%s.txt", safe);
}
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = sz > 0 && sz < (8 << 20) ? malloc((size_t)sz + 1) : NULL;
    if (b && fread(b, 1, (size_t)sz, f) == (size_t)sz) b[sz] = 0;
    else { free(b); b = NULL; }
    fclose(f);
    return b;
}

// fetch the plain-text extract of a title (following redirects); *real gets the page's own title
static char *fetch_extract(const char *title, char **real) {
    char enc[400], path[700];
    url_encode(title, enc, sizeof enc);
    snprintf(path, sizeof path, "/w/api.php?action=query&prop=extracts&explaintext=1&redirects=1&format=json&titles=%s", enc);
    size_t len;
    char *json = http_get(path, &len);
    if (!json) return NULL;
    char *text = strstr(json, "\"missing\"") ? NULL : json_field(json, "extract");
    if (text && real) {
        const char *pages = strstr(json, "\"pages\"");
        *real = pages ? json_field(pages, "title") : NULL;
    }
    free(json);
    return text;
}
static char *search_title(const char *query) {
    char enc[400], path[700];
    url_encode(query, enc, sizeof enc);
    snprintf(path, sizeof path, "/w/api.php?action=query&list=search&srlimit=3&srnamespace=0&format=json&srsearch=%s", enc);
    size_t len;
    char *json = http_get(path, &len);
    const char *s = json ? strstr(json, "\"search\"") : NULL;
    char *t = s ? json_field(s, "title") : NULL;
    free(json);
    return t;
}

static void make_dir(const char *p) {
#ifdef _WIN32
    CreateDirectoryA(p, NULL);
#else
    mkdir(p, 0755);
#endif
}

static void set_page(char *title, char *body, const char *status, int state) {
    pthread_mutex_lock(&g_lock);
    free(g_title); free(g_body); free(g_status);
    g_title = title; g_body = body; g_status = strdup(status); g_state = state;
    g_nsec = 0;
    if (body) build_sections(body);
    pthread_mutex_unlock(&g_lock);
}

static void load_page(const char *name, const char *hint) {
    char title[160], path[256];
    pick_title(name, hint, title, sizeof title);
    char *t = strdup(title);
    set_page(t, NULL, "Loading...", WIKI_LOADING);
    cache_path(title, path, sizeof path);
    struct stat st;
    char *cached = !stat(path, &st) && time(NULL) - st.st_mtime < CACHE_DAYS * 86400 ? read_file(path) : NULL;
    if (cached) {  // first line: the page's own title
        char *nl = strchr(cached, '\n');
        if (nl) {
            *nl = 0;
            char *real = strdup(cached), *body = strdup(nl + 1);
            free(cached);
            set_page(real, body, "From the guide cache (Zelda Wiki).", WIKI_READY);
            return;
        }
        free(cached);
    }
    if (!wiki_available()) { set_page(strdup(title), NULL, "Pages can only be fetched on Windows; use Open in browser.", WIKI_FAILED); return; }
    char *real = NULL;
    char *text = fetch_extract(title, &real);
    if (!text || !has(text, "Breath of the Wild")) {  // not a BotW page: search for one that is
        char q[220];
        snprintf(q, sizeof q, "%s Breath of the Wild", title);
        char *found = search_title(q);
        if (found) {
            char *real2 = NULL, *text2 = fetch_extract(found, &real2);
            if (text2) { free(text); free(real); text = text2; real = real2 ? real2 : strdup(found); }
            free(found);
        }
    }
    if (!text) { free(real); set_page(strdup(title), NULL, "Could not load the page (offline, or no such page).", WIKI_FAILED); return; }
    if (!real) real = strdup(title);
    make_dir("cheats");
    make_dir("cheats/wiki");
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "%s\n%s", real, text); fclose(f); }
    set_page(real, text, "Zelda Wiki (zeldawiki.wiki), text under CC BY-SA.", WIKI_READY);
}

static void load_suggestions(const char *q) {
    char enc[300], path[600];
    url_encode(q, enc, sizeof enc);
    snprintf(path, sizeof path, "/w/api.php?action=opensearch&limit=8&namespace=0&format=json&search=%s", enc);
    size_t len;
    char *json = wiki_available() ? http_get(path, &len) : NULL;
    char got[8][96];
    int n = 0;
    const char *p = json ? strchr(json + 1, '[') : NULL;  // ["query",["t1","t2"],...]
    while (p && *p && *p != ']' && n < 8) {
        const char *qt = strchr(p, '"');
        const char *close = strchr(p, ']');
        if (!qt || (close && close < qt)) break;
        const char *end;
        char *s = json_str(qt + 1, &end);
        if (s) { snprintf(got[n++], 96, "%s", s); free(s); }
        p = end;
    }
    free(json);
    pthread_mutex_lock(&g_lock);
    memcpy(g_sugg, got, sizeof got);
    g_nsugg = n;
    pthread_mutex_unlock(&g_lock);
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_lock);
        while (!g_want_page && !g_want_suggest) pthread_cond_wait(&g_wake, &g_lock);
        char name[160], hint[24], query[96];
        int page = g_want_page, sugg = g_want_suggest;
        snprintf(name, sizeof name, "%s", g_want);
        snprintf(hint, sizeof hint, "%s", g_want_hint);
        snprintf(query, sizeof query, "%s", g_want_query);
        g_want_page = g_want_suggest = 0;
        pthread_mutex_unlock(&g_lock);
        if (page) load_page(name, hint);
        if (sugg && strlen(query) >= 2) load_suggestions(query);
    }
    return NULL;
}
static void start(void) {
    pthread_t t;
    if (!pthread_create(&t, NULL, worker, NULL)) pthread_detach(t);
}

void wiki_open(const char *name, const char *hint) {
    pthread_once(&g_once, start);
    pthread_mutex_lock(&g_lock);
    snprintf(g_want, sizeof g_want, "%s", name ? name : "");
    snprintf(g_want_hint, sizeof g_want_hint, "%s", hint ? hint : "");
    g_want_page = 1;
    g_state = WIKI_LOADING;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}
void wiki_suggest(const char *query) {
    pthread_once(&g_once, start);
    pthread_mutex_lock(&g_lock);
    snprintf(g_want_query, sizeof g_want_query, "%s", query ? query : "");
    g_want_suggest = 1;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}
int wiki_lock_page(const char **title, const char **status, const WikiSection **sections, int *count) {
    pthread_mutex_lock(&g_lock);
    *title = g_title ? g_title : "";
    *status = g_status ? g_status : "";
    *sections = g_sec;
    *count = g_nsec;
    return g_state;
}
void wiki_unlock_page(void) { pthread_mutex_unlock(&g_lock); }
int wiki_suggestions(char out[][96], int max) {
    pthread_mutex_lock(&g_lock);
    int n = g_nsugg < max ? g_nsugg : max;
    memcpy(out, g_sugg, (size_t)n * sizeof g_sugg[0]);
    pthread_mutex_unlock(&g_lock);
    return n;
}
void wiki_url(int site, char *out, int n) {
    char t[160] = "";
    pthread_mutex_lock(&g_lock);
    snprintf(t, sizeof t, "%s", g_title ? g_title : "");
    pthread_mutex_unlock(&g_lock);
    for (char *p = t; *p; p++) if (*p == ' ') *p = '_';
    if (site == 1) snprintf(out, (size_t)n, "https://www.zeldadungeon.net/wiki/%s", t);
    else snprintf(out, (size_t)n, "https://zeldawiki.wiki/wiki/%s", t);
}
