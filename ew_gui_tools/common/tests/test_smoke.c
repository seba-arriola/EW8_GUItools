#include <assert.h>
#include <string.h>

#include "ewgui/version.h"

int main(void)
{
    const char *v = ewgui_version();
    assert(v != NULL);
    assert(strlen(v) > 0);
    return 0;
}
