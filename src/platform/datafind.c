/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: the game data for a build started without a DATA_DIR and with
 * no "orig" folder in the default places (a packaged build started from
 * its icon). The player picks the CD (or a folder with its files), or a CD
 * image, whose game files are copied into the pref folder's "data" (as
 * web/pre.ts keeps them in the browser). The choice is kept in the pref
 * folder's data-path.txt (~/.local/share/Afterlife/Afterlife/ on Linux,
 * %APPDATA%\Afterlife\Afterlife\ on Windows, ~/Library/Application
 * Support/Afterlife/Afterlife/ on macOS) and used again while it has the
 * game data.
 */
#include "platform/datafind.h"

#include <SDL3/SDL.h>
#include <string.h>

#define TITLE "Afterlife"

/* --- the game files --------------------------------------------------------- */

static bool has_suffix(const char *name, const char *suffix)
{
    size_t n = SDL_strlen(name), m = SDL_strlen(suffix);
    return n > m && SDL_strcasecmp(name + n - m, suffix) == 0;
}

bool datafind_has_game_data(const char *dir)
{
    int count = 0;
    char **names = SDL_GlobDirectory(dir, NULL, 0, &count);
    bool ini = false, exe = false, bundle = false;
    for (int i = 0; names && i < count; i++) {
        ini |= SDL_strcasecmp(names[i], "ALIFE.INI") == 0;
        exe |= SDL_strcasecmp(names[i], "ALIFE.EXE") == 0;
        bundle |= has_suffix(names[i], ".000");
    }
    SDL_free(names);
    return ini && exe && bundle;
}

/* The files the port reads (web/pre.ts's wanted); the rest of the CD is
 * skipped. */
static bool wanted(const char *name)
{
    return SDL_strcasecmp(name, "ALIFE.INI") == 0 || SDL_strcasecmp(name, "ALIFE.EXE") == 0 ||
           has_suffix(name, ".000") || (SDL_strncasecmp(name, "ASCEN", 5) == 0 && has_suffix(name, ".ASC"));
}

/* --- CD images (web/iso9660.ts): an .iso (2048-byte sectors), a raw .bin
 * (2352: mode 1, or mode 2 form 1), or a .cue naming its .bin. Only the
 * ISO 9660 file system of the data track is read. --------------------------- */

#define SECTOR 2048

typedef struct {
    SDL_IOStream *io;
    int size; /* bytes per sector in the image */
    int ud;   /* offset of the user data in a sector */
} Image;

static Uint32 le32(const Uint8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (Uint32)p[3] << 24; }

/* len bytes of user data from byte off of the extent at sector lba */
static bool image_read(Image *im, Uint32 lba, Uint32 off, void *buf, size_t len)
{
    Uint8 *p = buf;
    while (len) {
        Uint32 in = off % SECTOR;
        size_t n = im->size == SECTOR ? len : SDL_min(len, (size_t)(SECTOR - in));
        Sint64 at = (Sint64)(lba + off / SECTOR) * im->size + im->ud + in;
        if (SDL_SeekIO(im->io, at, SDL_IO_SEEK_SET) != at || SDL_ReadIO(im->io, p, n) != n)
            return false;
        p += n;
        off += (Uint32)n;
        len -= n;
    }
    return true;
}

/* The image (a .cue's .bin) and its sector layout: the first volume
 * descriptor ("CD001", type 1) is at sector 16. */
static bool image_open(Image *im, const char *path)
{
    char bin[4096];
    SDL_strlcpy(bin, path, sizeof bin);
    if (has_suffix(path, ".cue")) {
        char *cue = SDL_LoadFile(path, NULL);
        const char *f = cue ? SDL_strstr(cue, "FILE \"") : NULL;
        const char *end = f ? SDL_strchr(f + 6, '"') : NULL;
        if (end) {
            const char *slash = SDL_strrchr(path, '/'), *bslash = SDL_strrchr(path, '\\');
            if (bslash > slash)
                slash = bslash;
            size_t dir = slash ? (size_t)(slash - path + 1) : 0;
            SDL_snprintf(bin, sizeof bin, "%.*s%.*s", (int)dir, path, (int)(end - f - 6), f + 6);
        }
        SDL_free(cue);
        if (!end)
            return false;
    }
    im->io = SDL_IOFromFile(bin, "rb");
    if (!im->io)
        return false;
    static const int layouts[][2] = {{SECTOR, 0}, {2352, 16}, {2352, 24}};
    for (size_t i = 0; i < SDL_arraysize(layouts); i++) {
        Uint8 vd[6];
        im->size = layouts[i][0];
        im->ud = layouts[i][1];
        if (image_read(im, 16, 0, vd, sizeof vd) && vd[0] == 1 && memcmp(vd + 1, "CD001", 5) == 0)
            return true;
    }
    SDL_CloseIO(im->io);
    return false;
}

static bool copy_out(Image *im, Uint32 lba, Uint32 size, const char *path)
{
    SDL_IOStream *out = SDL_IOFromFile(path, "wb");
    if (!out)
        return false;
    enum { CHUNK = 64 * SECTOR };
    Uint8 *buf = SDL_malloc(CHUNK);
    bool ok = buf != NULL;
    for (Uint32 off = 0; ok && off < size; off += CHUNK) {
        size_t n = SDL_min((size_t)CHUNK, (size_t)(size - off));
        ok = image_read(im, lba, off, buf, n) && SDL_WriteIO(out, buf, n) == n;
    }
    SDL_free(buf);
    if (!SDL_CloseIO(out))
        ok = false;
    return ok;
}

/* Copies the wanted files of the directory at (lba, size) and below into
 * dest; the number copied, or -1. */
static int walk(Image *im, Uint32 lba, Uint32 size, const char *dest, int depth)
{
    Uint8 *dir = size <= (1u << 20) ? SDL_malloc(size) : NULL;
    if (!dir || !image_read(im, lba, 0, dir, size)) {
        SDL_free(dir);
        return -1;
    }
    int copied = 0;
    for (Uint32 pos = 0; pos < size && copied >= 0;) {
        const Uint8 *r = dir + pos;
        if (r[0] == 0) {
            /* records don't cross sectors: the rest of this one is padding */
            pos = (pos / SECTOR + 1) * SECTOR;
            continue;
        }
        if (r[0] < 34 || pos + r[0] > size)
            break;
        pos += r[0];
        int nlen = r[32];
        if (33 + nlen > r[0] || (nlen == 1 && r[33] < 2))
            continue; /* "." and ".." */
        char name[256], path[4096];
        SDL_memcpy(name, r + 33, nlen);
        name[nlen] = '\0';
        name[strcspn(name, ";")] = '\0';
        size_t n = SDL_strlen(name);
        if (n && name[n - 1] == '.')
            name[n - 1] = '\0';
        if (r[25] & 2) {
            if (depth < 8 && le32(r + 10)) {
                int c = walk(im, le32(r + 2), le32(r + 10), dest, depth + 1);
                copied = c < 0 ? -1 : copied + c;
            }
        } else if (wanted(name)) {
            SDL_snprintf(path, sizeof path, "%s/%s", dest, name);
            copied = copy_out(im, le32(r + 2), le32(r + 10), path) ? copied + 1 : -1;
        }
    }
    SDL_free(dir);
    return copied;
}

/* The game files of the image at path into dest; an error message, or NULL. */
static const char *extract(const char *path, const char *dest)
{
    Image im;
    if (!image_open(&im, path))
        return "This is not a CD image (no ISO 9660 file system). If it is a .zip, unzip it first.";
    Uint8 root[34];
    const char *err = NULL;
    /* the root directory record is at offset 156 of the volume descriptor */
    if (!SDL_CreateDirectory(dest))
        err = "Could not create the folder for the game files.";
    else if (!image_read(&im, 16, 156, root, sizeof root) || walk(&im, le32(root + 2), le32(root + 10), dest, 0) < 0)
        err = "Reading the CD image failed.";
    SDL_CloseIO(im.io);
    return err;
}

/* --- asking ----------------------------------------------------------------- */

static SDL_AtomicInt g_dialog; /* 0 open, 1 chosen, 2 cancelled, 3 failed */
static char g_picked[4096];

static void SDLCALL dialog_done(void *user, const char *const *files, int filter)
{
    (void)user, (void)filter;
    /* (zenity's cancel is an empty path) */
    bool chosen = files && files[0] && files[0][0];
    if (chosen)
        SDL_strlcpy(g_picked, files[0], sizeof g_picked);
    SDL_SetAtomicInt(&g_dialog, !files ? 3 : chosen ? 1 : 2);
}

/* The system's file or folder dialog, waited for; 1 chosen (g_picked),
 * 2 cancelled, 3 no dialog. */
static int pick(SDL_FileDialogType type, const char *title)
{
    static const SDL_DialogFileFilter filters[] = {
        {"CD images (*.iso, *.bin, *.cue, *.img)", "iso;bin;cue;img"},
        {"All files", "*"},
    };
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
    if (type == SDL_FILEDIALOG_OPENFILE) {
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, (void *)filters);
        SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, SDL_arraysize(filters));
    }
    SDL_SetAtomicInt(&g_dialog, 0);
    SDL_ShowFileDialogWithProperties(type, dialog_done, NULL, props);
    while (SDL_GetAtomicInt(&g_dialog) == 0) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(props);
    return SDL_GetAtomicInt(&g_dialog);
}

static void error_box(const char *msg)
{
    SDL_Log("%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TITLE, msg, NULL);
}

/* 1 a folder, 2 a CD image, 0 quit */
static int ask_what(void)
{
    static const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose folder..."},
        {0, 2, "Choose CD image..."},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"},
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
        NULL,
        TITLE,
        "Afterlife needs the files of its CD, which this port doesn't include.\n\n"
        "Choose the CD (or a folder with its files, such as an installed Afterlife), "
        "or an image of the CD (.iso, .bin or .cue). The game files of an image are "
        "copied, so the image isn't needed afterwards.\n\n"
        "An image of the US CD is at https://archive.org/details/AfterlifeUSA (unzip it first).",
        SDL_arraysize(buttons),
        buttons,
        NULL,
    };
    int id = 0;
    if (!SDL_ShowMessageBox(&box, &id)) {
        SDL_Log("no message box (%s): start Afterlife with the folder of the game files as its argument",
                SDL_GetError());
        return 0;
    }
    return id;
}

const char *datafind_data_dir(const char *def, bool ask)
{
    static char dir[4096];
    if (datafind_has_game_data(def) || !ask)
        return def;
    char *pref = SDL_GetPrefPath("Afterlife", "Afterlife");
    if (!pref)
        return def;
    char kept[4096], copies[4096];
    SDL_snprintf(kept, sizeof kept, "%sdata-path.txt", pref);
    SDL_snprintf(copies, sizeof copies, "%sdata", pref);
    SDL_free(pref);

    char *last = SDL_LoadFile(kept, NULL);
    if (last) {
        SDL_strlcpy(dir, last, sizeof dir);
        SDL_free(last);
        dir[strcspn(dir, "\r\n")] = '\0';
        if (dir[0] && datafind_has_game_data(dir))
            return dir;
    }

    for (;;) {
        int what = ask_what();
        if (what == 0)
            return NULL;
        int got =
            pick(what == 1 ? SDL_FILEDIALOG_OPENFOLDER : SDL_FILEDIALOG_OPENFILE,
                 what == 1 ? "Choose the Afterlife CD or the folder of its files" : "Choose an Afterlife CD image");
        if (got == 3) {
            char msg[1024];
            SDL_snprintf(msg, sizeof msg,
                         "No file dialog (%s). Start Afterlife with the folder of the game files as its argument.",
                         SDL_GetError());
            error_box(msg);
            return NULL;
        }
        if (got != 1)
            continue;
        if (what == 1) {
            if (!datafind_has_game_data(g_picked)) {
                error_box("That folder doesn't have the Afterlife game files (ALIFE.INI, ALIFE.EXE and a .000 "
                          "bundle). Choose the CD, or the folder Afterlife is installed in.");
                continue;
            }
            SDL_strlcpy(dir, g_picked, sizeof dir);
        } else {
            const char *err = extract(g_picked, copies);
            if (!err && !datafind_has_game_data(copies))
                err = "That CD image doesn't have the Afterlife game files (ALIFE.INI, ALIFE.EXE and a .000 bundle).";
            if (err) {
                error_box(err);
                continue;
            }
            SDL_strlcpy(dir, copies, sizeof dir);
        }
        char line[4200];
        SDL_snprintf(line, sizeof line, "%s\n", dir);
        SDL_SaveFile(kept, line, SDL_strlen(line));
        return dir;
    }
}
