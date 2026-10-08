#include <assert.h>
#include <stdio.h>

#include "ewgui/actions.h"

static int g_calls = 0;
static int g_last_target = -1;

static void on_go(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)ud;
    g_calls++;
    if (p)
        g_variant_get(p, "i", &g_last_target);
}

int main(void)
{
    GSimpleActionGroup *grp = g_simple_action_group_new();
    ewgui_action_add(G_ACTION_MAP(grp), "go", "i", on_go, NULL);
    ewgui_action_add(G_ACTION_MAP(grp), "plain", NULL, on_go, NULL);

    /* Acción con target */
    g_action_group_activate_action(G_ACTION_GROUP(grp), "go", g_variant_new_int32(3));
    assert(g_calls == 1 && g_last_target == 3);

    /* Acción sin parámetro */
    g_action_group_activate_action(G_ACTION_GROUP(grp), "plain", NULL);
    assert(g_calls == 2);

    /* Menú: un submenú con un ítem + separador + ítem */
    EwMenuItem items[] = {
        { "Go",    "win.go",    "i", 7 },
        { NULL,    NULL,        NULL, 0 },   /* separador */
        { "Plain", "win.plain", NULL, 0 },
    };
    EwMenuGroup groups[] = { { "Control", items, 3 } };
    GMenuModel *m = ewgui_menu_build(groups, 1);
    assert(g_menu_model_get_n_items(m) == 1);   /* un submenú de primer nivel */

    GMenuModel *sub = g_menu_model_get_item_link(m, 0, G_MENU_LINK_SUBMENU);
    assert(sub != NULL);
    assert(g_menu_model_get_n_items(sub) >= 1);

    g_object_unref(sub);
    g_object_unref(m);
    g_object_unref(grp);

    printf("ALL ACTIONS TESTS PASSED\n");
    return 0;
}
