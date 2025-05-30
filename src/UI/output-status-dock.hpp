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
#include <QComboBox>

#include "../utils.hpp"

class QTableWidget;
class QString;
class QCheckBox;
class QPushButton;
class BranchOutputFilter;
class OutputTableRow;

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
    obs_source_t *source;

    static void onParentRenamed(void *data, calldata_t *cd);

protected:
    void mousePressEvent(QMouseEvent *event) override;

public:
    explicit ParentCell(const QString &text, obs_source_t *source, QWidget *parent = (QWidget *)nullptr);
    ~ParentCell();

    void setSourceName(const QString &text);
};

class RecordingOutputCell : public QLabel {
    Q_OBJECT

    obs_source_t *source;

protected:
    void mousePressEvent(QMouseEvent *event) override;

public:
    explicit RecordingOutputCell(const QString &text, obs_source_t *source, QWidget *parent = (QWidget *)nullptr);
    ~RecordingOutputCell();

    void setOutputName(const QString &text);
};

class StatusCell : public QWidget {
    Q_OBJECT

    QLabel *icon;
    QLabel *statusText;

public:
    explicit StatusCell(const QString &text, QWidget *parent = (QWidget *)nullptr);
    ~StatusCell();

    inline void setText(const QString &text) { statusText->setText(text); };
    inline void setIcon(const QPixmap &pixmap) { icon->setPixmap(pixmap); };
    inline void setIconShow(bool show) { icon->setVisible(show); };
    inline void setTheme(const QString &id, const QString &classes) { setThemeID(statusText, id, classes); };
};

enum RowOutputType {
    ROW_OUTPUT_NONE = 0,
    ROW_OUTPUT_STREAMING = 1,
    ROW_OUTPUT_RECORDING = 2,
};

class BranchOutputStatusDock : public QFrame {
    Q_OBJECT

    QTimer timer;
    QTableWidget *outputTable = nullptr;
    QList<OutputTableRow *> outputTableRows;
    QPushButton *enableAllButton = nullptr;
    QPushButton *disableAllButton = nullptr;
    QLabel *interlockLabel = nullptr;
    QComboBox *interlockComboBox = nullptr;
    OBSSignal sourceAddedSignal;
    obs_hotkey_id enableAllHotkey;
    obs_hotkey_id disableAllHotkey;

    void update();
    void saveSettings();
    void loadSettings();

    static void onEanbleAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static void onDisableAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);

protected:
    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

public:
    explicit BranchOutputStatusDock(QWidget *parent = (QWidget *)nullptr);
    ~BranchOutputStatusDock();

public slots:
    void addRow(BranchOutputFilter *filter, size_t streamingIndex, RowOutputType outputType, size_t groupIndex = 0);
    void addFilter(BranchOutputFilter *filter);
    void removeFilter(BranchOutputFilter *filter);
    void setEabnleAll(bool enabled);

    inline int getInterlockType() const { return interlockComboBox->currentData().toInt(); };
};

class OutputTableRow : public QObject {
    Q_OBJECT

    friend class BranchOutputStatusDock;

    BranchOutputFilter *filter;
    FilterCell *filterCell;
    ParentCell *parentCell;
    StatusCell *status;
    RowOutputType outputType;
    size_t streamingIndex;
    size_t groupIndex;
    QLabel *droppedFrames;
    QLabel *megabytesSent;
    QLabel *bitrate;
    QLabel *outputName;

    uint64_t lastBytesSent = 0;
    uint64_t lastBytesSentTime = 0;

    int first_total = 0;
    int first_dropped = 0;

    void update();
    void reset();

    long double kbps = 0.0l;

public:
    explicit OutputTableRow(QObject *parent = (QObject *)nullptr);
    ~OutputTableRow();
};
