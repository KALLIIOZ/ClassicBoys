#include "test.h"

#include "classicboy/common.h"

void test_cpu_all(void);
void test_memory_all(void);
void test_ppu_all(void);
void test_apu_all(void);
void test_cart_all(void);
void test_cgb_all(void);
void test_integration_all(void);
void test_stability_all(void);

int main(void)
{
    printf("ClassicBoy - suite de pruebas del nucleo CGB\n");
    printf("============================================\n\n");

    TEST_ASSERT(gb_alloc_check() == 0);

    test_cpu_all();
    test_memory_all();
    test_ppu_all();
    test_apu_all();
    test_cart_all();
    test_cgb_all();
    test_integration_all();
    test_stability_all();

    test_summary();
    gb_alloc_report(stdout);

    size_t blocks = 0;
    size_t bytes = 0;
    size_t peak = 0;
    gb_alloc_stats(&blocks, &bytes, &peak);
    if (blocks != 0 || bytes != 0) {
        printf("FALLO: quedan %zu bloque(s) y %zu byte(s) sin liberar\n",
               blocks, bytes);
        g_check_failures++;
    }
    if (gb_alloc_check() != 0) {
        printf("FALLO: el comprobador de canarios detecto corrupcion\n");
        g_check_failures++;
    }

    bool ok = g_test_failures == 0 && g_check_failures == 0;
    printf("\n%s\n", ok ? "RESULTADO: TODO OK" : "RESULTADO: HAY FALLOS");
    return ok ? 0 : 1;
}
