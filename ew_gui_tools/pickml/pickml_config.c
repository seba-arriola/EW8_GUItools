/******************************************************************************
 * pickml_config.c — lectura de pickml.d (estilo kom, como pickS/csnloc).     *
 *                                                                            *
 * Toda clave es opcional salvo las que se validan al final. Los defaults     *
 * reproducen la geometría de EQTransformer: ventana 6000 muestras a 100 Hz.  *
 ******************************************************************************/

#include "pickml.h"
#include <kom.h>
#include <earthworm.h>

static void copy_str(char *dst, size_t dstsz, const char *src)
{
    if (!src) return;
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

static int parse_chan(const char *s)
{
    if (!s || !s[0]) return -1;
    if (s[0] == 'Z' || s[0] == 'z') return PM_COMP_Z;
    if (s[0] == 'N' || s[0] == 'n') return PM_COMP_N;
    if (s[0] == 'E' || s[0] == 'e') return PM_COMP_E;
    return -1;
}

void pm_config_defaults(PmConfig *c)
{
    memset(c, 0, sizeof(*c));

    strcpy(c->MyModName, "MOD_PICKML");
    strcpy(c->InRingName, "SLINK_RING");
    strcpy(c->OutRingName, "PICK_RING");
    c->HeartbeatInt = 30;
    c->LogFile      = 1;
    c->Debug        = 0;

    strcpy(c->StaFile, "pickml.sta");
    strcpy(c->ModelName, "eqt");
    c->ModelFile[0] = '\0';
    c->OrtLib[0]    = '\0';
    c->Engine       = PM_ENGINE_ONNX;   /* el stub se pide explícitamente */
    c->OrtThreads   = 0;
    c->OrtOptLevel  = 1;                /* basic: ver medición en README */

    c->WindowSamples = PM_WIN_SAMPLES;
    c->Rate          = 100.0;
    c->RateTolHz     = 0.01;

    c->StepSec     = 10.0;
    c->MinPosSec   = 20.0;
    c->MaxPosSec   = 0.0;
    c->BlindSamples = 500;              /* 5 s: mismo blinding que SeisBench */
    c->Stacking    = 0;

    c->PThreshold   = 0.30;
    c->SThreshold   = 0.30;
    c->DetThreshold = 0.0;              /* 0 = puerta de detección apagada */

    c->WProb[0] = 0.90;
    c->WProb[1] = 0.70;
    c->WProb[2] = 0.50;
    c->WProb[3] = 0.30;

    c->DedupSec  = 2.0;
    c->MinSPSec  = 2.0;              /* igual que pickS GuideMinDtSec */
    /* 1 s = 1.7 % de una ventana de 60 s. Con 0.1 s cualquier jitter de paquetes
     * entre componentes tiraba la ventana completa. */
    c->MaxGapSec = 1.0;

    strcpy(c->PReportChan, "Z");
    strcpy(c->SReportChan, "N");

    c->DumpWindowsDir[0] = '\0';
    c->PickOutFile[0]  = '\0';
    c->PicksOutFile[0] = '\0';
    c->RunMetaFile[0]  = '\0';
    c->LiveMaxAgeSec   = 120;       /* live: no reprocesar más de 2 min de atraso */

    /* Publicación al anillo: APAGADA por defecto. El .d vivo la enciende. */
    c->PublishRing      = 0;
    c->PublishP         = 1;
    c->PublishS         = 1;
    c->PublishMaxWeight = 3;
}

int pm_config_load(const char *file, PmConfig *c, char *err, size_t errn)
{
    char *com, *str;
    double winsec;

    if (!file || !c) {
        if (err) snprintf(err, errn, "config: argumento nulo");
        return PM_ERR;
    }
    if (err && errn) err[0] = '\0';

    if (!k_open(file)) {
        if (err) snprintf(err, errn, "no se pudo abrir %s", file);
        return PM_ERR;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if      (k_its("MyModuleId"))   { str = k_str(); copy_str(c->MyModName, sizeof(c->MyModName), str); }
        else if (k_its("InRing"))       { str = k_str(); copy_str(c->InRingName, sizeof(c->InRingName), str); }
        else if (k_its("OutRing"))      { str = k_str(); copy_str(c->OutRingName, sizeof(c->OutRingName), str); }
        else if (k_its("HeartbeatInt")) c->HeartbeatInt = k_int();
        else if (k_its("HeartBeatInt")) c->HeartbeatInt = k_int();
        else if (k_its("LogFile"))      c->LogFile = k_int();
        else if (k_its("Debug"))        c->Debug = k_int();

        else if (k_its("StaFile"))      { str = k_str(); copy_str(c->StaFile, sizeof(c->StaFile), str); }
        else if (k_its("ModelName"))    { str = k_str(); copy_str(c->ModelName, sizeof(c->ModelName), str); }
        else if (k_its("ModelFile"))    { str = k_str(); copy_str(c->ModelFile, sizeof(c->ModelFile), str); }
        else if (k_its("OrtLib"))       { str = k_str(); copy_str(c->OrtLib, sizeof(c->OrtLib), str); }
        else if (k_its("Engine")) {
            str = k_str();
            if (str && (str[0] == 's' || str[0] == 'S')) c->Engine = PM_ENGINE_STUB;
            else                                        c->Engine = PM_ENGINE_ONNX;
        }
        else if (k_its("OrtThreads"))   c->OrtThreads = k_int();
        else if (k_its("OrtOptLevel")) {
            str = k_str();
            if (!str || str[0] == 'n' || str[0] == 'N')      c->OrtOptLevel = 0;
            else if (str[0] == 'b' || str[0] == 'B')         c->OrtOptLevel = 1;
            else                                             c->OrtOptLevel = 2;
        }

        else if (k_its("WindowSamples")) c->WindowSamples = (long)k_int();
        else if (k_its("Rate"))          c->Rate = k_val();
        else if (k_its("RateTolHz"))     c->RateTolHz = k_val();

        else if (k_its("StepSec"))      c->StepSec = k_val();
        else if (k_its("MinPosSec"))    c->MinPosSec = k_val();
        else if (k_its("MaxPosSec"))    c->MaxPosSec = k_val();
        else if (k_its("BlindSamples")) c->BlindSamples = k_int();
        else if (k_its("Stacking")) {
            str = k_str();
            c->Stacking = (str && (str[0] == 'm' || str[0] == 'M')) ? 1 : 0;
        }

        else if (k_its("PThreshold"))   c->PThreshold = k_val();
        else if (k_its("SThreshold"))   c->SThreshold = k_val();
        else if (k_its("DetThreshold")) c->DetThreshold = k_val();
        else if (k_its("W0Prob"))       c->WProb[0] = k_val();
        else if (k_its("W1Prob"))       c->WProb[1] = k_val();
        else if (k_its("W2Prob"))       c->WProb[2] = k_val();
        else if (k_its("W3Prob"))       c->WProb[3] = k_val();

        else if (k_its("DedupSec"))     c->DedupSec = k_val();
        else if (k_its("MinSPSec"))     c->MinSPSec = k_val();
        else if (k_its("MaxGapSec"))    c->MaxGapSec = k_val();

        else if (k_its("PReportChan"))  { str = k_str(); copy_str(c->PReportChan, sizeof(c->PReportChan), str); }
        else if (k_its("SReportChan"))  { str = k_str(); copy_str(c->SReportChan, sizeof(c->SReportChan), str); }

        else if (k_its("DumpWindowsDir")) { str = k_str(); copy_str(c->DumpWindowsDir, sizeof(c->DumpWindowsDir), str); }
        else if (k_its("PickOutFile"))    { str = k_str(); copy_str(c->PickOutFile, sizeof(c->PickOutFile), str); }
        else if (k_its("PicksOutFile"))   { str = k_str(); copy_str(c->PicksOutFile, sizeof(c->PicksOutFile), str); }
        else if (k_its("RunMetaFile"))    { str = k_str(); copy_str(c->RunMetaFile, sizeof(c->RunMetaFile), str); }
        else if (k_its("LiveMaxAgeSec"))  c->LiveMaxAgeSec = k_int();

        else if (k_its("PublishRing"))    c->PublishRing = k_int();
        else if (k_its("PublishPhases"))  {
            str = k_str();
            if (pm_publish_phases_parse(str, &c->PublishP, &c->PublishS) != 0) {
                if (err) snprintf(err, errn,
                    "PublishPhases='%s' invalido (P|S|PS)", str ? str : "");
                k_close();
                return PM_ERR;
            }
        }
        else if (k_its("PublishMaxWeight")) c->PublishMaxWeight = k_int();

        else continue;   /* clave desconocida: se ignora, como en pickS */

        if (k_err()) {
            if (err) snprintf(err, errn, "error de config en '%s'", com);
            k_close();
            return PM_ERR;
        }
    }
    k_close();

    /* ---- validación geométrica / de rangos ---- */
    if (c->Rate <= 0.0) {
        if (err) snprintf(err, errn, "Rate debe ser > 0");
        return PM_ERR;
    }
    /* La ventana es la NATIVA del modelo (EQT 6000, PhaseNet 3001): se valida
     * contra la dimensión declarada del grafo al abrirlo, no aquí. */
    if (c->WindowSamples < 8) {
        if (err) snprintf(err, errn, "WindowSamples=%ld debe ser >= 8", c->WindowSamples);
        return PM_ERR;
    }
    winsec = (double)c->WindowSamples / c->Rate;
    if (c->StepSec <= 0.0 || c->StepSec > winsec) {
        if (err) snprintf(err, errn, "StepSec=%.3f fuera de (0, %.3f]", c->StepSec, winsec);
        return PM_ERR;
    }
    if (c->MinPosSec < 0.0 || c->MinPosSec >= winsec) {
        if (err) snprintf(err, errn, "MinPosSec=%.3f fuera de [0, %.3f)", c->MinPosSec, winsec);
        return PM_ERR;
    }
    if (c->MaxPosSec != 0.0 && c->MaxPosSec <= c->MinPosSec) {
        if (err) snprintf(err, errn, "MaxPosSec=%.3f debe ser 0 o > MinPosSec", c->MaxPosSec);
        return PM_ERR;
    }
    if (c->BlindSamples < 0 ||
        (c->MinPosSec > 0.0 && (double)c->BlindSamples / c->Rate >= c->MinPosSec)) {
        if (err) snprintf(err, errn,
            "BlindSamples=%d no cabe en MinPosSec=%.3f", c->BlindSamples, c->MinPosSec);
        return PM_ERR;
    }
    /* Sin pérdida de cobertura: cada muestra debe caer en alguna ventana
     * con posición >= MinPosSec. */
    if (c->StepSec > winsec - c->MinPosSec) {
        if (err) snprintf(err, errn,
            "StepSec=%.3f > %.3f (WindowSec - MinPosSec): habría muestras sin ventana",
            c->StepSec, winsec - c->MinPosSec);
        return PM_ERR;
    }
    if (c->MaxGapSec < 0.0) {
        if (err) snprintf(err, errn, "MaxGapSec debe ser >= 0");
        return PM_ERR;
    }
    if (c->DedupSec < 0.0) {
        if (err) snprintf(err, errn, "DedupSec debe ser >= 0");
        return PM_ERR;
    }
    if (parse_chan(c->PReportChan) < 0 || parse_chan(c->SReportChan) < 0) {
        if (err) snprintf(err, errn, "PReportChan/SReportChan deben ser Z, N o E");
        return PM_ERR;
    }
    if (c->WProb[0] < c->WProb[1] || c->WProb[1] < c->WProb[2] ||
        c->WProb[2] < c->WProb[3]) {
        if (err) snprintf(err, errn, "W0Prob..W3Prob deben ser decrecientes");
        return PM_ERR;
    }
    /* LogFile y publicación: rangos estrictos (un valor raro en un productor
     * vivo falla en silencio, así que se rechaza en el arranque). */
    if (c->LogFile < 0 || c->LogFile > 2) {
        if (err) snprintf(err, errn, "LogFile=%d fuera de 0..2", c->LogFile);
        return PM_ERR;
    }
    if (c->PublishRing != 0 && c->PublishRing != 1) {
        if (err) snprintf(err, errn, "PublishRing=%d debe ser 0 o 1", c->PublishRing);
        return PM_ERR;
    }
    if (c->PublishMaxWeight < 0 || c->PublishMaxWeight > 3) {
        if (err) snprintf(err, errn, "PublishMaxWeight=%d fuera de 0..3",
                          c->PublishMaxWeight);
        return PM_ERR;
    }
    return PM_OK;
}

void pm_config_print(const PmConfig *c, FILE *f)
{
    fprintf(f, "MyModuleId      %s\n", c->MyModName);
    fprintf(f, "InRing          %s\n", c->InRingName);
    fprintf(f, "OutRing         %s\n", c->OutRingName);
    fprintf(f, "HeartbeatInt    %d\n", c->HeartbeatInt);
    fprintf(f, "LogFile         %d\n", c->LogFile);
    fprintf(f, "Debug           %d\n", c->Debug);
    fprintf(f, "StaFile         %s\n", c->StaFile);
    fprintf(f, "ModelName       %s\n", c->ModelName);
    fprintf(f, "ModelFile       %s\n", c->ModelFile[0] ? c->ModelFile : "(sin definir)");
    fprintf(f, "OrtLib          %s\n", c->OrtLib[0] ? c->OrtLib : "(sin definir)");
    fprintf(f, "Engine          %s\n", c->Engine == PM_ENGINE_STUB ? "stub" : "onnx");
    fprintf(f, "OrtThreads      %d\n", c->OrtThreads);
    fprintf(f, "OrtOptLevel     %s\n",
            c->OrtOptLevel == 0 ? "none" : (c->OrtOptLevel == 1 ? "basic" : "all"));
    fprintf(f, "WindowSamples   %ld\n", c->WindowSamples);
    fprintf(f, "Rate            %.3f\n", c->Rate);
    fprintf(f, "RateTolHz       %.4f\n", c->RateTolHz);
    fprintf(f, "StepSec         %.3f\n", c->StepSec);
    fprintf(f, "MinPosSec       %.3f\n", c->MinPosSec);
    fprintf(f, "MaxPosSec       %.3f\n", c->MaxPosSec);
    fprintf(f, "BlindSamples    %d\n", c->BlindSamples);
    fprintf(f, "Stacking        %s\n", c->Stacking ? "max" : "avg");
    fprintf(f, "PThreshold      %.3f\n", c->PThreshold);
    fprintf(f, "SThreshold      %.3f\n", c->SThreshold);
    fprintf(f, "DetThreshold    %.3f\n", c->DetThreshold);
    fprintf(f, "W0Prob..W3Prob  %.3f %.3f %.3f %.3f\n",
            c->WProb[0], c->WProb[1], c->WProb[2], c->WProb[3]);
    fprintf(f, "DedupSec        %.3f\n", c->DedupSec);
    fprintf(f, "MinSPSec        %.3f\n", c->MinSPSec);
    fprintf(f, "MaxGapSec       %.3f\n", c->MaxGapSec);
    fprintf(f, "PReportChan     %s\n", c->PReportChan);
    fprintf(f, "SReportChan     %s\n", c->SReportChan);
    fprintf(f, "DumpWindowsDir  %s\n",
            c->DumpWindowsDir[0] ? c->DumpWindowsDir : "(apagado)");
    fprintf(f, "PickOutFile     %s\n", c->PickOutFile[0] ? c->PickOutFile : "(stdout)");
    fprintf(f, "PicksOutFile    %s\n", c->PicksOutFile[0] ? c->PicksOutFile : "(apagado)");
    fprintf(f, "RunMetaFile     %s\n", c->RunMetaFile[0] ? c->RunMetaFile : "(apagado)");
    fprintf(f, "LiveMaxAgeSec   %d\n", c->LiveMaxAgeSec);
    {
        char ph[8];
        pm_publish_phases_str(c->PublishP, c->PublishS, ph, sizeof(ph));
        fprintf(f, "PublishRing     %d\n", c->PublishRing);
        fprintf(f, "PublishPhases   %s\n", ph);
    }
    fprintf(f, "PublishMaxWeight %d\n", c->PublishMaxWeight);
}
