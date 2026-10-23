/*
 * bpq_filters_dialog.h - Ventana modal `Filters…` del picking (fase 4.3).
 *
 * Lista los presets del `.d` (con su atajo) mas `Raw`, y permite alternar
 * `FilterPhase zero` y el STA/LTA de la vista. Reemplaza al popup del menu de
 * filtros: en WSLg los popups quedan detras de la ventana, asi que es una
 * ventana modal propia (igual que el resto de los selectores del prototipo).
 *
 * El filtro es SOLO de vista: el audio crudo que se manda al localizador no se
 * toca.
 */
#ifndef BPQ_FILTERS_DIALOG_H
#define BPQ_FILTERS_DIALOG_H

#include <QDialog>
#include <QVector>

#include "bpq_capi.h"

class QButtonGroup;
class QCheckBox;

class BpqFiltersDialog : public QDialog
{
    Q_OBJECT
public:
    BpqFiltersDialog(const BpqConfig *cfg, int active, bool phaseZero,
                     bool stalta, QWidget *parent = nullptr);

    /* Preset elegido (0..nf-1) o -1 = Raw. */
    int  selected() const;
    bool phaseZero() const;
    bool stalta() const;

private:
    QVector<int>   m_idx;     /* preset de cada radio; -1 = Raw */
    QButtonGroup  *m_grp = nullptr;
    QCheckBox     *m_zero = nullptr;
    QCheckBox     *m_stalta = nullptr;
};

#endif /* BPQ_FILTERS_DIALOG_H */
