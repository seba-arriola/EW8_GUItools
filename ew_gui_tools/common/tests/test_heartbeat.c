#include <assert.h>

#include "ewgui/ring.h"

int main(void)
{
    EwGuiHeartbeat hb = {0};

    /* last=0; intervalo 1 s */
    assert(ewgui_heartbeat_due(&hb, 1.0, 1) == 1);   /* 1.0 - 0.0 >= 1 */
    assert(ewgui_heartbeat_due(&hb, 1.5, 1) == 0);
    assert(ewgui_heartbeat_due(&hb, 2.0, 1) == 1);   /* 2.0 - 1.0 >= 1 */
    assert(ewgui_heartbeat_due(&hb, 3.5, 1) == 1);   /* 3.5 - 2.0 >= 1 */
    assert(ewgui_heartbeat_due(&hb, 3.9, 1) == 0);

    /* intervalo 5 s (last = 3.5 tras la llamada anterior) */
    assert(ewgui_heartbeat_due(&hb, 8.4, 5) == 0);   /* 8.4 - 3.5 = 4.9 < 5 */
    assert(ewgui_heartbeat_due(&hb, 8.5, 5) == 1);   /* 8.5 - 3.5 = 5.0 >= 5 */

    return 0;
}
