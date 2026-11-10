#include "roms.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool has_rom_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    dot++;
    return (dot[0] == 'g' || dot[0] == 'G') && (dot[1] == 'b' || dot[1] == 'B') &&
           ((dot[2] == '\0') ||
            ((dot[2] == 'c' || dot[2] == 'C') && dot[3] == '\0'));
}

static void sanitize_title(const unsigned char *rom, char *title,
                           size_t title_len)
{
    size_t n = 0;
    for (size_t i = 0x134; i <= 0x143 && n + 1 < title_len; i++) {
        unsigned char c = rom[i];
        if (c == 0) {
            break;
        }
        title[n++] = (char)((c >= 32 && c <= 126) ? c : ' ');
    }
    while (n > 0 && (title[n - 1] == ' ' || title[n - 1] == '\0')) {
        n--;
    }
    title[n] = '\0';
    if (n == 0) {
        snprintf(title, title_len, "Game Boy");
    }
}

static const char *mbc_name(unsigned char type, bool *battery)
{
    *battery = false;
    switch (type) {
    case 0x00:
        return "ROM";
    case 0x08:
        return "ROM + RAM";
    case 0x09:
        *battery = true;
        return "ROM + RAM";
    case 0x01:
    case 0x02:
        return "MBC1";
    case 0x03:
        *battery = true;
        return "MBC1";
    case 0x05:
        return "MBC2";
    case 0x06:
        *battery = true;
        return "MBC2";
    case 0x0F:
        *battery = true;
        return "MBC3 + RTC";
    case 0x10:
        *battery = true;
        return "MBC3 + RTC";
    case 0x11:
    case 0x12:
        return "MBC3";
    case 0x13:
        *battery = true;
        return "MBC3";
    case 0x19:
    case 0x1C:
        return "MBC5";
    case 0x1A:
    case 0x1D:
        return "MBC5 + RAM";
    case 0x1B:
    case 0x1E:
        *battery = true;
        return "MBC5 + RAM";
    default:
        return "Desconocido";
    }
}

static bool inspect_rom(const char *path, RomEntry *entry)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    unsigned char header[0x150];
    size_t got = fread(header, 1, sizeof(header), file);
    fclose(file);
    if (got < 0x150) {
        return false;
    }
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    sanitize_title(header, entry->title, sizeof(entry->title));
    bool battery = false;
    const char *mbc = mbc_name(header[0x147], &battery);
    snprintf(entry->mbc, sizeof(entry->mbc), "%s", mbc);
    entry->battery = battery;
    struct stat info;
    if (stat(path, &info) == 0 && info.st_size > 0) {
        entry->size = (unsigned)info.st_size;
    } else {
        entry->size = 0;
    }
    return true;
}

static int compare_entries(const void *a, const void *b)
{
    const RomEntry *ea = (const RomEntry *)a;
    const RomEntry *eb = (const RomEntry *)b;
    int cmp = _stricmp(ea->title, eb->title);
    if (cmp != 0) {
        return cmp;
    }
    return _stricmp(ea->path, eb->path);
}

static bool already_present(const Roms *roms, const char *path)
{
    for (int i = 0; i < roms->count; i++) {
        if (_stricmp(roms->items[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static void scan_directory(Roms *roms, const char *directory)
{
    DIR *dir = opendir(directory);
    if (!dir) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && roms->count < CB_MAX_ROMS) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!has_rom_extension(entry->d_name)) {
            continue;
        }
        char path[CB_PATH_MAX];
        if (strchr(directory, '\\') || strchr(directory, '/')) {
            snprintf(path, sizeof(path), "%s%s%s", directory,
                     (directory[strlen(directory) - 1] == '/' ||
                      directory[strlen(directory) - 1] == '\\')
                         ? ""
                         : "\\",
                     entry->d_name);
        } else {
            snprintf(path, sizeof(path), "%s", entry->d_name);
        }
        char full[CB_PATH_MAX];
        if (!_fullpath(full, path, sizeof(full))) {
            snprintf(full, sizeof(full), "%s", path);
        }
        if (already_present(roms, full)) {
            continue;
        }
        RomEntry item;
        memset(&item, 0, sizeof(item));
        if (inspect_rom(full, &item)) {
            roms->items[roms->count++] = item;
        }
    }
    closedir(dir);
}

void roms_clear(Roms *roms)
{
    if (roms) {
        memset(roms, 0, sizeof(*roms));
    }
}

void roms_scan(Roms *roms, const char *base_path)
{
    if (!roms) {
        return;
    }
    roms_clear(roms);
    if (!base_path) {
        return;
    }
    char directory[CB_PATH_MAX];
    snprintf(directory, sizeof(directory), "%sroms", base_path);
    scan_directory(roms, directory);
    snprintf(directory, sizeof(directory), "%s.", base_path);
    scan_directory(roms, directory);
    if (roms->count > 1) {
        qsort(roms->items, (size_t)roms->count, sizeof(RomEntry),
              compare_entries);
    }
}
