#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hypodb_ids.h"

int main(void)
{
    char b[64];

    /* event_id: con hora origen (UTC) y sin ella */
    assert(hypodb_event_id(b, sizeof b, "1234567890", 0.0) == 0);
    assert(strcmp(b, "E1234567890") == 0);
    {
        struct tm tmv;
        char exp[64];

        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = 126; tmv.tm_mon = 9; tmv.tm_mday = 10; tmv.tm_hour = 12;
        assert(hypodb_event_id(b, sizeof b, "1234567890",
                               (double)timegm(&tmv)) == 0);
        strftime(exp, sizeof exp, "E1234567890-%Y%m%d%H%M%S", &tmv);
        assert(strcmp(b, exp) == 0);
    }

    /* origin_id */
    assert(hypodb_origin_id(b, sizeof b, "E1234567890", 162) == 0);
    assert(strcmp(b, "E1234567890:162") == 0);
    assert(hypodb_origin_id(b, sizeof b, "E1234567890", 164) == 0);
    assert(strcmp(b, "E1234567890:164") == 0);

    /* magnitude_id */
    assert(hypodb_magnitude_id(b, sizeof b, "E1234567890:163", "Mwp") == 0);
    assert(strcmp(b, "E1234567890:163/mag/Mwp") == 0);

    /* station_magnitude_id */
    assert(hypodb_station_magnitude_id(b, sizeof b, "E1234567890:163/mag/ML",
                                       "C1.GO01..HHZ") == 0);
    assert(strcmp(b, "E1234567890:163/mag/ML/C1.GO01..HHZ") == 0);

    /* pick_id */
    assert(hypodb_pick_id(b, sizeof b, "E1234567890:162", "GO01", "P") == 0);
    assert(strcmp(b, "E1234567890:162/pick/GO01.P") == 0);

    /* NSLCO */
    assert(hypodb_channel_nslco(b, sizeof b, "C1", "GO01", "--", "HHZ") == 0);
    assert(strcmp(b, "C1.GO01..HHZ") == 0);
    assert(hypodb_channel_nslco(b, sizeof b, "C1", "GO01", "", "HHZ") == 0);
    assert(strcmp(b, "C1.GO01..HHZ") == 0);
    assert(hypodb_channel_nslco(b, sizeof b, "C1", "GO01", "00", "HHZ") == 0);
    assert(strcmp(b, "C1.GO01.00.HHZ") == 0);

    /* argumentos inválidos */
    assert(hypodb_event_id(b, sizeof b, "", 0.0) == -1);
    assert(hypodb_origin_id(b, sizeof b, "", 162) == -1);
    assert(hypodb_magnitude_id(b, sizeof b, "E1:162", "") == -1);

    /* no cabe -> -1 */
    assert(hypodb_event_id(b, 4, "1234567890", 0.0) == -1);
    assert(hypodb_station_magnitude_id(b, 8, "E1:163/mag/ML", "C1.GO01..HHZ")
           == -1);

    printf("ALL HYPODB IDS TESTS PASSED\n");
    return 0;
}
