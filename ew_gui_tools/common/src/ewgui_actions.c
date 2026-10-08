#include "ewgui/actions.h"

void ewgui_action_add(GActionMap *map, const char *name, const char *param_type,
                      EwActionCb cb, gpointer user_data)
{
    GVariantType *pt = param_type ? g_variant_type_new(param_type) : NULL;
    GSimpleAction *a = g_simple_action_new(name, pt);

    if (pt)
        g_variant_type_free(pt);
    if (cb)
        g_signal_connect(a, "activate", G_CALLBACK(cb), user_data);
    g_action_map_add_action(map, G_ACTION(a));
    g_object_unref(a);
}

GMenuModel *ewgui_menu_build(const EwMenuGroup *groups, size_t n)
{
    GMenu *bar = g_menu_new();

    for (size_t g = 0; g < n; g++) {
        GMenu *sub = g_menu_new();

        for (size_t i = 0; i < groups[g].n_items; i++) {
            const EwMenuItem *it = &groups[g].items[i];

            if (!it->label) {                 /* separador de sección */
                GMenu *sec = g_menu_new();
                g_menu_append_section(sub, NULL, G_MENU_MODEL(sec));
                g_object_unref(sec);
                continue;
            }

            GMenuItem *mi;
            if (it->target_fmt) {
                mi = g_menu_item_new(it->label, NULL);
                g_menu_item_set_action_and_target(mi, it->action,
                                                  it->target_fmt, it->target_int);
            } else {
                mi = g_menu_item_new(it->label, it->action);
            }
            g_menu_append_item(sub, mi);
            g_object_unref(mi);
        }

        g_menu_append_submenu(bar, groups[g].label, G_MENU_MODEL(sub));
        g_object_unref(sub);
    }

    return G_MENU_MODEL(bar);
}
