/*
 * bpq_wave_worker.cxx - Ver bpq_wave_worker.h.
 */
#include "bpq_wave_worker.h"

#include <QMetaType>

BpqWaveWorker::BpqWaveWorker(BpqWave *wave, const BpqConfig *cfg, QObject *parent)
    : QObject(parent), m_wave(wave), m_cfg(cfg)
{
    /* El puntero cruza por una conexion en cola: hay que declararlo. */
    qRegisterMetaType<BpqTrace *>("BpqTrace *");
    qRegisterMetaType<quint64>("quint64");
}

void BpqWaveWorker::runJob(quint64 jobId, const QVector<BpqFetchReq> &req,
                           double t0, double window)
{
    int with = 0;

    for (int i = 0; i < req.size(); i++) {
        char err[256] = "";
        const QByteArray sta = req.at(i).sta.toUtf8();
        const QByteArray net = req.at(i).net.toUtf8();
        const QByteArray chan = req.at(i).chan.toUtf8();
        const QByteArray loc = req.at(i).loc.toUtf8();
        BpqTrace *t = bpq_wave_fetch(m_wave, m_cfg, sta.constData(),
                                     net.constData(), chan.constData(),
                                     loc.constData(), t0, window,
                                     err, sizeof(err));
        if (t) with++;
        emit traceReady(jobId, i, t);
    }
    emit jobFinished(jobId, req.size(), with);
}
