/*
 * bpq_locate_worker.cxx - `locate` y `calculate` en su propio hilo.
 *
 * Antes de correr la cadena baja el TRACE de ondas de las estaciones con P: sin
 * el, csnmags no tiene nada que medir y la candidata sale sin ML/Mwp/Mb/Ms. Las
 * trazas viven hasta que termina la corrida porque el trace apunta a su buffer
 * crudo.
 */
#include "bpq_locate_worker.h"

#include <QElapsedTimer>

#include <cstdio>
#include <cstring>

namespace {

/* Baja las trazas de `req` y arma el trace alineado. Devuelve cuantas entradas
 * quedaron (las trazas se guardan en `traces` para liberarlas despues). */
int fetch_traces(BpqWave *wave, const BpqConfig *cfg,
               const QVector<BpqFetchReq> &req,
               BpqTrace **traces, CsnhypodbpTrace *trace, int max)
{
    int n = 0;

    for (int i = 0; i < req.size() && n < max; i++) {
        char ferr[256] = "";
        const QByteArray sta = req.at(i).sta.toUtf8();
        const QByteArray net = req.at(i).net.toUtf8();
        const QByteArray chan = req.at(i).chan.toUtf8();
        const QByteArray loc = req.at(i).loc.toUtf8();
        BpqTrace *tr;

        if (!(req.at(i).seconds > 0.0)) continue;
        tr = wave ? bpq_wave_fetch(wave, cfg, sta.constData(),
                                   net.constData(), chan.constData(),
                                   loc.constData(), req.at(i).t0,
                                   req.at(i).seconds, ferr, sizeof(ferr))
                  : nullptr;

        traces[n] = tr;
        if (!tr) continue;
        if (bpq_trace_copy(tr, sta.constData(), net.constData(), chan.constData(),
                           loc.constData(), &trace[n])) {
            n++;
        } else {
            bpq_trace_free(tr);
            traces[n] = nullptr;
        }
    }
    return n;
}

void release_traces(BpqTrace **traces, int n, int max)
{
    for (int i = 0; i < n && i < max; i++) bpq_trace_free(traces[i]);
}

}   /* namespace */

BpqLocateWorker::BpqLocateWorker(BpqWave *wave, const BpqConfig *cfg,
                                 const BpqStations *st, QObject *parent)
    : QObject(parent), m_wave(wave), m_cfg(cfg), m_st(st), m_cancel(0)
{
    qRegisterMetaType<BpqLocateOut *>("BpqLocateOut *");
    qRegisterMetaType<CsnhypodbpMagOut *>("CsnhypodbpMagOut *");
    qRegisterMetaType<quint64>("quint64");
}

void BpqLocateWorker::runJob(quint64 jobId, const CsnhypodbpSolution &base,
                             const QVector<CsnhypodbpStaRow> &rows,
                             const QVector<int> &pickOn,
                             int autoOn, int manualOn, int theoOn,
                             double fixDepthKm, const QByteArray &locator,
                             const QVector<BpqFetchReq> &req)
{
    QElapsedTimer t;
    BpqLocateOut *out = new BpqLocateOut;
    BpqTrace *traces[BPQ_TRACE_MAX];
    CsnhypodbpTrace trace[BPQ_TRACE_MAX];
    int ntr;
    char err[512] = "";
    const int *on = pickOn.isEmpty() ? nullptr : pickOn.constData();
    bool ok;

    std::memset(out, 0, sizeof(*out));
    m_cancel = 0;
    t.start();

    ntr = fetch_traces(m_wave, m_cfg, req, traces, trace, BPQ_TRACE_MAX);

    ok = bpq_locate_run(m_cfg, m_st, &base, locator.constData(),
                        rows.constData(), rows.size(),
                        (const int (*)[2])on, autoOn, manualOn, theoOn,
                        fixDepthKm, trace, ntr, out, &m_cancel,
                        err, sizeof(err)) == 1;

    release_traces(traces, ntr, BPQ_TRACE_MAX);
    emit finished(jobId, ok, out, QString::fromUtf8(err),
                  t.elapsed() / 1000.0);
}

void BpqLocateWorker::runMagJob(quint64 jobId, const CsnhypodbpSolution &base,
                                const QVector<BpqFetchReq> &req,
                                const QVector<CsnhypodbpMagOvrRow> &ovr,
                                int kindMask)
{
    QElapsedTimer t;
    CsnhypodbpMagOut *out = new CsnhypodbpMagOut;
    BpqTrace *traces[BPQ_TRACE_MAX];
    CsnhypodbpTrace trace[BPQ_TRACE_MAX];
    char err[512] = "";
    int ntr, nh = 0, i;
    bool ok;

    std::memset(out, 0, sizeof(*out));
    t.start();

    ntr = fetch_traces(m_wave, m_cfg, req, traces, trace, BPQ_TRACE_MAX);
    for (i = 0; i < ntr; i++) {
        const size_t l = std::strlen(trace[i].chan);

        if (l && (trace[i].chan[l - 1] == 'N' || trace[i].chan[l - 1] == 'E'))
            nh++;
    }
    ok = bpq_mag_run(m_cfg, &base, trace, ntr,
                     ovr.isEmpty() ? nullptr : ovr.constData(), ovr.size(),
                     kindMask < 0 ? 0u : (unsigned)kindMask,
                     out, err, sizeof(err)) == 1;
    if (!ok) {
        const size_t l = std::strlen(err);

        if (l + 48 < sizeof(err))
            std::snprintf(err + l, sizeof(err) - l,
                          " [picks=%d trazas=%d H=%d]",
                          base.npicks, ntr, nh);
    }
    release_traces(traces, ntr, BPQ_TRACE_MAX);

    emit magFinished(jobId, ok, out, QString::fromUtf8(err),
                     t.elapsed() / 1000.0);
}
