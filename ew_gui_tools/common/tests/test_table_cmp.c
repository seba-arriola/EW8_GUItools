/* Test headless del comparador de tablas (sin GTK ni display). */
#include <assert.h>
#include <stdio.h>

#include "ewgui/table_ui.h"

int main(void)
{
    /* Numeros: 10 va despues de 5.2 (alfabeticamente seria al reves). */
    assert(ewgui_table_cmp_values("10", "5.2") > 0);
    assert(ewgui_table_cmp_values("5.2", "10") < 0);
    assert(ewgui_table_cmp_values("5.2", "5.2") == 0);
    assert(ewgui_table_cmp_values(" 10 ", "9") > 0);
    assert(ewgui_table_cmp_values("-33.10", "-33.20") > 0);

    /* Texto: alfabetico sin distinguir mayusculas. */
    assert(ewgui_table_cmp_values("hhz", "HHN") > 0);
    assert(ewgui_table_cmp_values("M", "A") > 0);
    assert(ewgui_table_cmp_values("csnloc", "hyp2000") < 0);

    /* Fechas ISO: alfabetico == cronologico. */
    assert(ewgui_table_cmp_values("2026-10-11 12:00:00", "2026-10-11 09:00:00") > 0);
    assert(ewgui_table_cmp_values("2026-10-11 09:00:00", "2026-10-10 23:59:59") > 0);
    assert(ewgui_table_cmp_values("2026-10-11 00:00:00", "2026-10-11 00:00:00") == 0);

    /* Ausentes ("", "-") no son numeros: quedan antes en ascendente (o sea al
     * final en descendente). */
    assert(ewgui_table_cmp_values("", "5.2") < 0);
    assert(ewgui_table_cmp_values("-", "5.2") < 0);
    assert(ewgui_table_cmp_values(NULL, "0") < 0);

    /* Texto con cola no numerica ("5.2-10" = magnitud-nest) cae al alfabetico:
     * por eso las columnas de magnitud ordenan por su propiedad numerica. */
    assert(ewgui_table_cmp_values("5.2-10", "10.0-5") > 0);

    printf("OK test_table_cmp\n");
    return 0;
}
