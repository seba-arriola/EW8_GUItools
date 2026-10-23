/*
 * bpq_wave_worker.h - Fetch de ondas fuera del hilo de la GUI.
 *
 * El fetch puede tardar (SDS por NFS, store frio), asi que corre en su propio
 * QThread. El hilo principal le manda una lista de SCNL ya resuelta (no toca el
 * catalogo desde el worker) y recibe una traza por estacion; el `jobId` permite
 * descartar resultados de un evento que el usuario ya dejo atras.
 *
 * Las trazas se crean en el worker y se liberan en el hilo principal con
 * bpq_trace_free: EwGuiTrace es malloc puro, sin afinidad de hilo.
 */
#ifndef BPQ_WAVE_WORKER_H
#define BPQ_WAVE_WORKER_H

#include <QObject>
#include <QString>
#include <QVector>

#include "bpq_capi.h"

/* BpqTrace es OPACO por diseno (el capi no expone EwGuiTrace). Sin esta macro Qt
 * pide sizeof(T) para el metatipo del signal y no compila. */
Q_DECLARE_OPAQUE_POINTER(BpqTrace *)

struct BpqFetchReq {
    QString sta, net, chan, loc;
};

class BpqWaveWorker : public QObject
{
    Q_OBJECT
public:
    BpqWaveWorker(BpqWave *wave, const BpqConfig *cfg, QObject *parent = nullptr);

    /* Debe correr EN el hilo del worker (se invoca con QMetaObject::invokeMethod). */
    void runJob(quint64 jobId, const QVector<BpqFetchReq> &req,
                double t0, double window);

signals:
    void traceReady(quint64 jobId, int row, BpqTrace *trace);
    void jobFinished(quint64 jobId, int requested, int withData);

private:
    BpqWave         *m_wave;
    const BpqConfig *m_cfg;
};

#endif /* BPQ_WAVE_WORKER_H */
