/*
 * bpq_locate_worker.h - Cadena de localizacion fuera del hilo de la GUI.
 *
 * `locate` escribe el ARC, corre csnloc y refina con hyp2000_ring/nlloc_ring:
 * segundos de reloj (y hasta LocatorTimeoutSec si el refinador se cuelga). Por
 * eso corre en su propio QThread, igual que el fetch de ondas.
 *
 * El hilo GUI manda COPIAS (solucion elegida, filas de la tabla, casillas por
 * fila y checks A/M/Teo): el usuario puede cambiar de evento mientras corre y el
 * resultado se descarta por `jobId`, pero nada de lo que toca el worker puede
 * quedar liberado por el hilo principal.
 */
#ifndef BPQ_LOCATE_WORKER_H
#define BPQ_LOCATE_WORKER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QVector>

#include "bpq_capi.h"
#include "bpq_wave_worker.h"     /* BpqFetchReq */

/* BpqLocateOut cruza por una conexion en cola: es un struct C sin metatipo, asi
 * que viaja como puntero opaco (mismo criterio que BpqTrace). */
Q_DECLARE_OPAQUE_POINTER(BpqLocateOut *)

class BpqLocateWorker : public QObject
{
    Q_OBJECT
public:
    BpqLocateWorker(BpqWave *wave, const BpqConfig *cfg, const BpqStations *st,
                    QObject *parent = nullptr);

    /* Corre EN el hilo del worker (QMetaObject::invokeMethod del llamador).
     * `pickOn` es un array plano de 2*nrows: [i*2] = P, [i*2+1] = S.
     * `req` son las estaciones con P de las que se baja onda para el TANK de
     * csnmags (ventana magT0..+magWindow): sin tank no hay magnitudes. */
    void runJob(quint64 jobId, const CsnhypodbpSolution &base,
                const QVector<CsnhypodbpStaRow> &rows, const QVector<int> &pickOn,
                int autoOn, int manualOn, int theoOn,
                double fixDepthKm, const QByteArray &locator,
                const QVector<BpqFetchReq> &req, double magT0, double magWindow);

    /* SOLO magnitudes (boton `calculate` de la sub-pestana): baja el mismo tank
     * y corre csnmags con las ventanas ajustadas (`ovr`), sin tocar la solucion.
     * `kindMask` elige que magnitudes llevan ventana ajustada (0 = las cuatro). */
    void runMagJob(quint64 jobId, const CsnhypodbpSolution &base,
                   const QVector<BpqFetchReq> &req, double magT0, double magWindow,
                   const QVector<CsnhypodbpMagOvrRow> &ovr, int kindMask);

    /* Se llama DIRECTO desde el hilo GUI: mientras runJob bloquea el bucle de
     * eventos del worker, una conexion en cola no se atenderia nunca. */
    void requestCancel() { m_cancel = 1; }

signals:
    /* `out` es del receptor (delete). `ok` = 0 si no hubo solucion y `err` trae
     * el motivo real (timeout / ref.err / ARC invalido / sin picadas). */
    void finished(quint64 jobId, bool ok, BpqLocateOut *out,
                  const QString &err, double elapsedS);

    /* Resultado de `calculate`: solo las cuatro magnitudes. */
    void magFinished(quint64 jobId, bool ok, CsnhypodbpMagOut *out,
                     const QString &err, double elapsedS);

private:
    BpqWave           *m_wave;
    const BpqConfig   *m_cfg;
    const BpqStations *m_st;
    volatile int       m_cancel;
};

#endif /* BPQ_LOCATE_WORKER_H */
