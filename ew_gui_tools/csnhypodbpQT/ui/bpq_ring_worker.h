/*
 * bpq_ring_worker.h - Bucle de anillos del prototipo (hilo GUI).
 *
 * Sustituye a los g_timeout_add de csnhypodbp_main.c:2485-2487:
 *   - cada 500 ms drena InRing / InRingRaw / InRingPick y alimenta el store;
 *   - cada 1 s comprueba el latido y la peticion de terminar.
 *
 * Corre en el hilo de la GUI a proposito: tport_getmsg sobre memoria compartida
 * es barato y no bloquea, y asi el store no necesita candados (la version GTK
 * hace exactamente lo mismo). Lo que si bloquea (ondas, locate, Mww) va a
 * QThread en las fases 3-6.
 */
#ifndef BPQ_RING_WORKER_H
#define BPQ_RING_WORKER_H

#include <QObject>
#include <QString>

#include "bpq_capi.h"

class QTimer;

class BpqRingWorker : public QObject
{
    Q_OBJECT
public:
    BpqRingWorker(BpqRings *rings, BpqEvents *events, QObject *parent = nullptr);

    void start();
    void stop();

    /* Un ciclo de drenaje inmediato (lo usan el arranque y los tests).
     * Devuelve el nº de mensajes procesados en este ciclo. */
    int drainNow();

    int messagesSeen() const { return m_nmsg; }
    int arcsSeen() const { return m_narc; }

    /* Entregas de los callbacks C (publicas para poder usarlas de thunk). */
    void deliverArc(const CsnhypodbpSolution *sol);
    void deliverMag(const BpqMagMsg *m);
    void deliverPick(const CsnhypodbpTracePick *p);

signals:
    void eventsChanged();
    void errorOccurred(const QString &msg);
    void quitRequested();

private slots:
    void onDrain();
    void onTick();

private:
    BpqRings  *m_rings = nullptr;
    BpqEvents *m_events = nullptr;
    QTimer    *m_drainTimer = nullptr;
    QTimer    *m_tickTimer = nullptr;
    int        m_nmsg = 0;
    int        m_narc = 0;
    int        m_nmag = 0;
    int        m_npick = 0;
    bool       m_warned_capacity = false;
    bool       m_warned_error = false;
};

#endif /* BPQ_RING_WORKER_H */
