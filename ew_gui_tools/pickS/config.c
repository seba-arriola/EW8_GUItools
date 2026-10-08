/******************************************************************************
 * config.c — lectura de pickS.d (estilo kom / k_its, como csnloc).           *
 ******************************************************************************/

#include "pickS.h"
#include <kom.h>
#include <earthworm.h>

/* Valores por defecto (todos sobreescribibles desde pickS.d). */
void PickS_SetDefaults(PickS_Params *p)
{
    memset(p, 0, sizeof(*p));

    strcpy(p->MyModName, "MOD_PICKS");
    strcpy(p->InRingName, "SLINK_RING");
    strcpy(p->PickRingName, "PICK_RING");
    strcpy(p->OutRingName, "PICK_RING");
    p->InKey = 0;
    p->PickKey = 0;
    p->OutKey = 0;
    p->HeartbeatInt = 30;
    p->LogFile = 1;
    p->Debug = 0;
    strcpy(p->StaFile, "pickS.sta");

    p->FilterLowHz  = 1.0;
    p->FilterHighHz = 10.0;
    p->FilterOrder  = 4;

    p->StaLenSec    = 0.5;
    p->LtaLenSec    = 20.0;
    p->TriggerOn    = 3.5;
    p->TriggerOff   = 1.5;
    p->DeadTimeSec  = 5.0;
    p->AicWinSec    = 3.0;
    p->BufferSec    = 60.0;

    p->GuideMode      = PICKS_GUIDE_HYBRID;
    p->GuideMinDtSec  = 0.5;
    p->GuideMaxDtSec  = 60.0;

    p->PolWinSec = 1.5;

    p->MinSnr    = 3.0;
    p->MaxWeight = 4;
    p->ReportChan = PICKS_REPORT_DETECTED;

    p->Q_Snr_W0    = 10.0;
    p->Q_Snr_W4    = 2.0;
    p->Q_StaLta_W0 = 8.0;
    p->Q_StaLta_W4 = 2.0;
    p->Q_Rect_W0   = 0.85;
    p->Q_Rect_W4   = 0.30;
    p->Q_IncidMinDeg = 45.0;
    p->Q_IncidMaxDeg = 135.0;

    p->Q_Plan_W0 = 0.0;   /* 0 => planaridad no entra al weight (default) */
    p->Q_Plan_W4 = 0.0;
    p->Q_Hv_W0   = 0.0;   /* 0 => H/V no entra al weight (default) */
    p->Q_Hv_W4   = 0.0;

    p->GateIncidMinDeg = 0.0;   /* Max<=Min => compuerta de incidencia off */
    p->GateIncidMaxDeg = 0.0;

    p->MetricsLog = 0;    /* 0 => sin linea METRICS (solo para calibracion) */
}

static void copy_str(char *dst, size_t dstsz, const char *src)
{
    if (!src) return;
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

int PickS_ReadConfig(const char *file, PickS_Params *p)
{
    char *com, *str;

    if (!k_open(file)) {
        fprintf(stderr, "pickS: no se pudo abrir %s\n", file);
        return -1;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if      (k_its("MyModuleId"))   { str = k_str(); copy_str(p->MyModName, sizeof(p->MyModName), str); }
        else if (k_its("InRing"))       { str = k_str(); copy_str(p->InRingName, sizeof(p->InRingName), str); }
        else if (k_its("PickRing"))     { str = k_str(); copy_str(p->PickRingName, sizeof(p->PickRingName), str); }
        else if (k_its("OutRing"))      { str = k_str(); copy_str(p->OutRingName, sizeof(p->OutRingName), str); }
        else if (k_its("HeartbeatInt")) p->HeartbeatInt = k_int();
        else if (k_its("HeartBeatInt")) p->HeartbeatInt = k_int();
        else if (k_its("LogFile"))      p->LogFile = k_int();
        else if (k_its("Debug"))        p->Debug = k_int();
        else if (k_its("StaFile"))      { str = k_str(); copy_str(p->StaFile, sizeof(p->StaFile), str); }

        else if (k_its("FilterLowHz"))  p->FilterLowHz = k_val();
        else if (k_its("FilterHighHz")) p->FilterHighHz = k_val();
        else if (k_its("FilterOrder"))  p->FilterOrder = k_int();

        else if (k_its("StaLenSec"))    p->StaLenSec = k_val();
        else if (k_its("LtaLenSec"))    p->LtaLenSec = k_val();
        else if (k_its("TriggerOn"))    p->TriggerOn = k_val();
        else if (k_its("TriggerOff"))   p->TriggerOff = k_val();
        else if (k_its("DeadTimeSec"))  p->DeadTimeSec = k_val();
        else if (k_its("AicWinSec"))    p->AicWinSec = k_val();
        else if (k_its("BufferSec"))    p->BufferSec = k_val();

        else if (k_its("GuideMode")) {
            str = k_str();
            if (str && (str[0] == 'i' || str[0] == 'I')) p->GuideMode = PICKS_GUIDE_INDEPENDENT;
            else                                         p->GuideMode = PICKS_GUIDE_HYBRID;
        }
        else if (k_its("GuideMinDtSec")) p->GuideMinDtSec = k_val();
        else if (k_its("GuideMaxDtSec")) p->GuideMaxDtSec = k_val();

        else if (k_its("PolWinSec"))    p->PolWinSec = k_val();

        else if (k_its("MinSnr"))       p->MinSnr = k_val();
        else if (k_its("MaxWeight"))    p->MaxWeight = k_int();
        else if (k_its("ReportChan")) {
            str = k_str();
            if (!str) p->ReportChan = PICKS_REPORT_DETECTED;
            else if (str[0] == 'e' || str[0] == 'E') p->ReportChan = PICKS_REPORT_E;
            else if (str[0] == 'n' || str[0] == 'N') p->ReportChan = PICKS_REPORT_N;
            else                                     p->ReportChan = PICKS_REPORT_DETECTED;
        }

        else if (k_its("Q_Snr_W0"))     p->Q_Snr_W0 = k_val();
        else if (k_its("Q_Snr_W4"))     p->Q_Snr_W4 = k_val();
        else if (k_its("Q_StaLta_W0"))  p->Q_StaLta_W0 = k_val();
        else if (k_its("Q_StaLta_W4"))  p->Q_StaLta_W4 = k_val();
        else if (k_its("Q_Rect_W0"))    p->Q_Rect_W0 = k_val();
        else if (k_its("Q_Rect_W4"))    p->Q_Rect_W4 = k_val();
        else if (k_its("Q_IncidMinDeg")) p->Q_IncidMinDeg = k_val();
        else if (k_its("Q_IncidMaxDeg")) p->Q_IncidMaxDeg = k_val();
        else if (k_its("Q_Plan_W0"))     p->Q_Plan_W0 = k_val();
        else if (k_its("Q_Plan_W4"))     p->Q_Plan_W4 = k_val();
        else if (k_its("Q_Hv_W0"))       p->Q_Hv_W0 = k_val();
        else if (k_its("Q_Hv_W4"))       p->Q_Hv_W4 = k_val();
        else if (k_its("GateIncidMinDeg")) p->GateIncidMinDeg = k_val();
        else if (k_its("GateIncidMaxDeg")) p->GateIncidMaxDeg = k_val();
        else if (k_its("MetricsLog"))    p->MetricsLog = k_int();

        else continue;

        if (k_err()) {
            fprintf(stderr, "pickS: error de config en '%s'\n", com);
            k_close();
            return -1;
        }
    }
    k_close();
    return 0;
}

/* Resolver claves de anillo (requiere earthworm.d cargado via startstop/kom). */
int PickS_ResolveKeys(PickS_Params *p)
{
    p->InKey = GetKey(p->InRingName);
    if (p->InKey == -1) {
        fprintf(stderr, "pickS: anillo invalido <%s>\n", p->InRingName);
        return -1;
    }
    p->PickKey = GetKey(p->PickRingName);
    if (p->PickKey == -1) {
        fprintf(stderr, "pickS: anillo invalido <%s>\n", p->PickRingName);
        return -1;
    }
    p->OutKey = GetKey(p->OutRingName);
    if (p->OutKey == -1) {
        fprintf(stderr, "pickS: anillo invalido <%s>\n", p->OutRingName);
        return -1;
    }
    return 0;
}
