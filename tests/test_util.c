#include "test_util.h"

#include "test.h"

int g_test_count;
int g_test_failures;
long g_check_count;
long g_check_failures;

void test_fail(const char *file, int line, const char *expression)
{
    g_check_failures++;
    fprintf(stderr, "  FAIL %s:%d: %s\n", file, line,
            expression ? expression : "(unknown)");
}

void run_test(const char *name, void (*fn)(void))
{
    long before = g_check_failures;
    g_test_count++;
    printf("[ RUN  ] %s\n", name);
    fn();
    if (g_check_failures == before) {
        printf("[  OK  ] %s\n", name);
    } else {
        g_test_failures++;
        printf("[ FAIL ] %s\n", name);
    }
}

void test_summary(void)
{
    printf("\n");
    printf("Tests: %d, fallos de test: %d\n", g_test_count, g_test_failures);
    printf("Comprobaciones: %ld, fallos: %ld\n", g_check_count,
           g_check_failures);
}

void test_gb_init(Gb *gb)
{
    gb_init(gb);
    gb->ppu.lcdc = 0;
    gb->ppu.mode = 0;
    gb->mmu.ie = 0;
    gb->mmu.io[0x0F] = 0;
    gb->timer.tac = 0;
    gb->cpu.ime = false;
    gb->cpu.halted = false;
    gb->cpu.ei_delay = 0;
}

uint8_t *test_rom_make(size_t size, uint8_t type, uint8_t rom_code,
                       uint8_t ram_code)
{
    if (size < 0x8000) {
        size = 0x8000;
    }
    uint8_t *rom = (uint8_t *)gb_calloc(1, size);
    if (!rom) {
        return NULL;
    }
    rom[0x100] = 0x18;
    rom[0x101] = 0xFE;
    const char *title = "CLASSICBOY TEST";
    memcpy(rom + 0x134, title, strlen(title));
    rom[0x147] = type;
    rom[0x148] = rom_code;
    rom[0x149] = ram_code;
    rom[0x143] = 0x00;
    uint8_t checksum = 0;
    for (size_t i = 0x134; i <= 0x14C; i++) {
        checksum = (uint8_t)(checksum - rom[i] - 1u);
    }
    rom[0x14D] = checksum;
    return rom;
}

void test_rom_set_code(uint8_t *rom, const uint8_t *code, size_t length)
{
    if (!rom || !code) {
        return;
    }
    memcpy(rom + 0x100, code, length);
}

void test_rom_set_cgb(uint8_t *rom, bool cgb_only)
{
    if (!rom) {
        return;
    }
    rom[0x143] = cgb_only ? 0xC0 : 0x80;
    uint8_t checksum = 0;
    for (size_t i = 0x134; i <= 0x14C; i++) {
        checksum = (uint8_t)(checksum - rom[i] - 1u);
    }
    rom[0x14D] = checksum;
}

void test_install_code(Gb *gb, const uint8_t *code, size_t length)
{
    if (!gb || !code) {
        return;
    }
    for (size_t i = 0; i < length; i++) {
        gb->mmu.wram[i] = code[i];
    }
    gb->cpu.pc = 0xC000;
    gb->cpu.sp = 0xD000;
}

unsigned test_step(Gb *gb)
{
    return cpu_step(gb, &gb->cpu);
}

void test_run_steps(Gb *gb, int count)
{
    for (int i = 0; i < count; i++) {
        (void)test_step(gb);
    }
}

bool test_alloc_clean(void)
{
    return gb_alloc_check() == 0;
}
