/*
 * bpq_wave_worker.cxx - Ver bpq_wave_worker.h.
 */
#include "bpq_wave_worker.h"

#include <QByteArray>
#include <QMetaType>

/* Fixture de ondas (BPQ_WAVE_FAKE=1): 60 s a 100 Hz, sin tocar store ni SDS.
 * Solo lo usan los tests de la cache/precarga, que necesitan ejecutar el camino
 * real del worker sin depender de datos ni del I/O. */
static BpqTrace *fakeTrace(double t0)
{
    enum { N = 6000 };
    int32_t buf[N];
    int i;

    for (i = 0; i < N; i++) buf[i] = (i % 200) - 100;
    return bpq_trace_new(t0, 100.0, buf, N);
}

BpqWaveWorker::BpqWaveWorker(BpqWave *wave, const BpqConfig *cfg,
                             BpqWaveCache *cache, QObject *parent)
    : QObject(parent), m_wave(wave), m_cfg(cfg), m_cache(cache),
      m_fake(!qgetenv("BPQ_WAVE_FAKE").isEmpty())
{
    /* El puntero cruza por una conexion en cola: hay que declararlo. */
    qRegisterMetaType<BpqTrace *>("BpqTrace *");
    qRegisterMetaType<quint64>("quint64");
}

BpqTrace *BpqWaveWorker::serve(const BpqFetchReq &r, double t0, double window,
                               int *cached)
{
    char key[128] = "";
    BpqTrace *t = nullptr;
    const QByteArray sta = r.sta.toUtf8();
    const QByteArray net = r.net.toUtf8();
    const QByteArray chan = r.chan.toUtf8();
    const QByteArray loc = r.loc.toUtf8();

    if (cached) *cached = 0;
    if (m_cache)
        (void)bpq_wavecache_key(key, sizeof(key), sta.constData(),
                                net.constData(), chan.constData(),
                                loc.constData(), t0, window);
    if (key[0] && bpq_wavecache_get(m_cache, key, &t)) {
        if (cached) *cached = 1;
        return t;
    }

    if (m_fake) {
        t = fakeTrace(t0);
    } else {
        char err[256] = "";

        t = bpq_wave_fetch(m_wave, m_cfg, sta.constData(), net.constData(),
                           chan.constData(), loc.constData(), t0, window,
                           err, sizeof(err));
    }
    if (t && key[0]) bpq_wavecache_put(m_cache, key, t);
    return t;
}

void BpqWaveWorker::runJob(quint64 jobId, const QVector<BpqFetchReq> &req,
                           double t0, double window)
{
    int with = 0;

    for (int i = 0; i < req.size(); i++) {
        BpqTrace *t = serve(req.at(i), t0, window, nullptr);

        if (t) with++;
        emit traceReady(jobId, i, t);
    }
    emit jobFinished(jobId, req.size(), with);
}

void BpqWaveWorker::runPrefetch(quint64 jobId, const QVector<BpqFetchReq> &req,
                                double t0, double window,
                                const std::atomic<quint64> *gen)
{
    int with = 0, without = 0;

    for (int i = 0; i < req.size(); i++) {
        BpqTrace *t;

        /* Se comprueba por CANAL, no solo al entrar: un lote de 3 canales es
         * corto, pero el prefetch se encola por estacion justamente para que un
         * clic nuevo se intercale cuanto antes. */
        if (gen && gen->load() != jobId) return;
        t = serve(req.at(i), t0, window, nullptr);
        if (t) {
            with++;
            bpq_trace_free(t);     /* la cache guarda su propia copia */
        } else {
            without++;
        }
        emit prefetched(jobId, req.at(i).sta, t ? 1 : 0);
    }
    emit prefetchFinished(jobId, with, without);
}
