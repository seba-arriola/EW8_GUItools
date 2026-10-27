/*
 * bpq_ring_worker.cxx - Ver bpq_ring_worker.h.
 */
#include "bpq_ring_worker.h"

#include <QTimer>

#include <cstdio>
#include <ctime>

/* Thunks con enlace C: es lo que esperan los typedefs de bpq_capi.h. */
extern "C" void bpq_worker_arc_cb(const CsnhypodbpSolution *sol, const char *raw,
                                  size_t len, void *ud);
extern "C" void bpq_worker_mag_cb(const BpqMagMsg *m, void *ud);
extern "C" void bpq_worker_pick_cb(const CsnhypodbpTracePick *p, void *ud);

extern "C" void bpq_worker_arc_cb(const CsnhypodbpSolution *sol, const char *raw,
                                  size_t len, void *ud)
{
    (void)raw;
    (void)len;
    if (ud && sol) static_cast<BpqRingWorker *>(ud)->deliverArc(sol);
}

extern "C" void bpq_worker_mag_cb(const BpqMagMsg *m, void *ud)
{
    if (ud && m) static_cast<BpqRingWorker *>(ud)->deliverMag(m);
}

extern "C" void bpq_worker_pick_cb(const CsnhypodbpTracePick *p, void *ud)
{
    if (ud && p) static_cast<BpqRingWorker *>(ud)->deliverPick(p);
}

/* ------------------------------------------------------------------------- */

BpqRingWorker::BpqRingWorker(BpqRings *rings, BpqEvents *events, QObject *parent)
    : QObject(parent), m_rings(rings), m_events(events)
{
}

void BpqRingWorker::start()
{
    if (!m_rings) return;
    if (!m_drainTimer) {
        m_drainTimer = new QTimer(this);
        m_drainTimer->setInterval(500);          /* main.c:2486 */
        connect(m_drainTimer, &QTimer::timeout, this, &BpqRingWorker::onDrain);
    }
    if (!m_tickTimer) {
        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(1000);          /* main.c:2485 */
        connect(m_tickTimer, &QTimer::timeout, this, &BpqRingWorker::onTick);
    }
    m_drainTimer->start();
    m_tickTimer->start();
    drainNow();                                  /* no esperar 500 ms al arrancar */
}

void BpqRingWorker::stop()
{
    if (m_drainTimer) m_drainTimer->stop();
    if (m_tickTimer) m_tickTimer->stop();
}

int BpqRingWorker::drainNow()
{
    char err[256] = "";
    int  n, before;

    if (!m_rings || !m_events) return 0;
    before = m_narc;

    n = bpq_rings_drain(m_rings, bpq_worker_arc_cb, bpq_worker_mag_cb,
                        bpq_worker_pick_cb, this, err, sizeof(err));
    if (n < 0) {
        if (!m_warned_error) {
            m_warned_error = true;
            emit errorOccurred(QString::fromUtf8(err[0] ? err : "error drenando anillos"));
        }
        return 0;
    }
    m_nmsg += n;
    if (m_narc != before) emit eventsChanged();
    return n;
}

void BpqRingWorker::onDrain()
{
    drainNow();
}

void BpqRingWorker::onTick()
{
    if (!m_rings) return;
    /* Latido: la version GTK publica TYPE_HEARTBEAT; el prototipo solo cuenta,
     * porque escribir heartbeats exige el Status() de EarthWorm (fase 6). */
    (void)bpq_rings_heartbeat(m_rings, (double)std::time(nullptr));
    if (bpq_rings_should_quit(m_rings)) emit quitRequested();
}

void BpqRingWorker::deliverArc(const CsnhypodbpSolution *sol)
{
    int rc;

    m_narc++;
    rc = bpq_events_feed_arc(m_events, sol);
    if (rc == -2 && !m_warned_capacity) {
        m_warned_capacity = true;
        emit errorOccurred(QStringLiteral(
            "store de eventos lleno (%1 grupos): se descartan los eventos nuevos")
            .arg(bpq_events_count(m_events)));
    }
}

void BpqRingWorker::deliverMag(const BpqMagMsg *m)
{
    m_nmag++;
    bpq_events_feed_mag(m_events, m);
    if (m) emit magReceived(*m);
}

void BpqRingWorker::deliverPick(const CsnhypodbpTracePick *p)
{
    /* Fase 4: las picadas del anillo alimentan la ventana de picking. Aqui solo
     * se cuentan (el store no las necesita: cada solucion trae las suyas). */
    (void)p;
    m_npick++;
}
