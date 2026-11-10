/*
 * ClassicBoy - ROM library scanning.
 */
#ifndef CB_ROMS_H
#define CB_ROMS_H

#include <stdbool.h>

#define CB_PATH_MAX 1024
#define CB_MAX_ROMS 128

typedef struct RomEntry {
    char path[CB_PATH_MAX];
    char title[64];
    char mbc[24];
    unsigned size;
    bool battery;
} RomEntry;

typedef struct Roms {
    RomEntry items[CB_MAX_ROMS];
    int count;
} Roms;

void roms_scan(Roms *roms, const char *base_path);
void roms_clear(Roms *roms);

#endif /* CB_ROMS_H */
