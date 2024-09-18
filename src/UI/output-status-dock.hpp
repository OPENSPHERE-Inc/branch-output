/*
Branch Output Plugin
Copyright (C) 2024 OPENSPHERE Inc. info@opensphere.co.jp

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <obs-module.h>
#include <obs.hpp>

#include <QFrame>
#include <QPointer>
#include <QList>
#include <QTimer>
#include <QTableWidgetItem>
#include <QLabel>

class QTableWidget;
class QString;
class QCheckBox;
struct BranchOutputFilter;

class FilterCell : public QWidget {
    Q_OBJECT

    QCheckBox *visibilityCheckbox;
    QLabel *name;

    OBSSignal enableSignal;
    OBSSignal filterRenamedSignal;

    static void onVisibilityChanged(void *data, calldata_t *cd);
    static void onFilterRenamed(void *data, calldata_t *cd);

public:
    explicit FilterCell(const QString &text, obs_source_t *_source, QWidget *parent = (QWidget *)nullptr);
    ~FilterCell();

    void setText(const QString &text);
};

class ParentCell : public QLabel {
    Q_OBJECT

    OBSSignal parentRenamedSignal;

    static void onParentRenamed(void *data, calldata_t *cd);

public:
    explicit ParentCell(const QString &text, obs_source_t *source, QWidget *parent = (QWidget *)nullptr);
    ~ParentCell();
};

class BranchOutputStatusDock : public QFrame {
    Q_OBJECT

    struct OutputTableRow {
        BranchOutputFilter *filter;
        FilterCell *filterCell;
        ParentCell *parentCell;
        QLabel *status;
        QLabel *droppedFrames;
        QLabel *megabytesSent;
        QLabel *bitrate;

        uint64_t lastBytesSent = 0;
        uint64_t lastBytesSentTime = 0;

        int first_total = 0;
        int first_dropped = 0;

        void update(bool rec);
        void reset();

        long double kbps = 0.0l;
    };

    QTimer timer;
    QTableWidget *outputTable = nullptr;
    QList<OutputTableRow> outputTableRows;

    void update();

protected:
    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

public:
    explicit BranchOutputStatusDock(QWidget *parent = (QWidget *)nullptr);
    ~BranchOutputStatusDock();

    void addFilter(BranchOutputFilter *filter);
    void removeFilter(BranchOutputFilter *filter);
    void setEabnleAll(bool enabled);
};

inline QString QTStr(const char *lookupVal)
{
    return QString::fromUtf8(obs_module_text(lookupVal));
}
