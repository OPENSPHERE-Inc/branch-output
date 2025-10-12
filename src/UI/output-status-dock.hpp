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
#include <QToolButton>
#include <QCheckBox>

#include "../utils.hpp"

class QTableWidget;
class QString;
class QPushButton;
class BranchOutputFilter;
class OutputTableRow;

class OutputTableCellItem : public QTableWidgetItem {
public:
    enum ItemRole {
        ValueRole = Qt::UserRole,
        RowIdRole,
    };

    explicit OutputTableCellItem(const QString &rowId, const QVariant &value);
    ~OutputTableCellItem() {}

    bool operator<(const QTableWidgetItem &other) const override;
    inline void setRowId(const QString &id) { setData(RowIdRole, id); }
};

class LabelCell : public QLabel {
    Q_OBJECT

    OutputTableCellItem *_item;

public:
    explicit LabelCell(const QString &rowId, QWidget *parent = (QWidget *)nullptr);
    explicit LabelCell(const QString &rowId, const QString &textValue, QWidget *parent = (QWidget *)nullptr);
    ~LabelCell();

    inline void setTextValue(const QString &textValue);
    inline void setValue(const QVariant &value) { _item->setData(Qt::UserRole, value); }
    inline OutputTableCellItem *item() const { return _item; }
};

class FilterCell : public QWidget {
    Q_OBJECT

    OutputTableCellItem *_item;
    QCheckBox *visibilityCheckbox;
    QLabel *name;

    OBSSignal enableSignal;
    OBSSignal filterRenamedSignal;

    static void onVisibilityChanged(void *data, calldata_t *cd);
    static void onFilterRenamed(void *data, calldata_t *cd);

signals:
    void renamed(const QString &newName);

public:
    explicit FilterCell(
        const QString &rowId, const QString &textValue, obs_source_t *_source, QWidget *parent = (QWidget *)nullptr
    );
    ~FilterCell();

    void setTextValue(const QString &value);
    inline bool isVisibilityChecked() const { return visibilityCheckbox->isChecked(); }
    inline OutputTableCellItem *item() const { return _item; }
};

class ParentCell : public LabelCell {
    Q_OBJECT

    OBSSignal parentRenamedSignal;
    obs_source_t *source;

    static void onParentRenamed(void *data, calldata_t *cd);

protected:
    void mousePressEvent(QMouseEvent *event) override;

signals:
    void renamed(const QString &newName);

public:
    explicit ParentCell(
        const QString &rowId, const QString &textValue, obs_source_t *source, QWidget *parent = (QWidget *)nullptr
    );
    ~ParentCell();

    void setTextValue(const QString &value);
};

class RecordingOutputCell : public LabelCell {
    Q_OBJECT

    obs_source_t *source;

protected:
    void mousePressEvent(QMouseEvent *event) override;

public:
    explicit RecordingOutputCell(
        const QString &rowId, const QString &textValue, obs_source_t *source, QWidget *parent = (QWidget *)nullptr
    );
    ~RecordingOutputCell();

    void setTextValue(const QString &value);
};

class StatusCell : public QWidget {
    Q_OBJECT

    OutputTableCellItem *_item;
    QLabel *streamingIcon;
    QLabel *recordingIcon;
    QLabel *recordingPausedIcon;
    QLabel *statusText;
    QToolButton *splitRecordingButton;
    QToolButton *pauseRecordingButton;
    QToolButton *unpauseRecordingButton;
    QToolButton *addChapterToRecordingButton;

signals:
    void splitRecordingButtonClicked();
    void pauseRecordingButtonClicked();
    void unpauseRecordingButtonClicked();
    void addChapterToRecordingButtonClicked();

public:
    enum StatusIcon { STATUS_ICON_NONE, STATUS_ICON_STREAMING, STATUS_ICON_RECORDING, STATUS_ICON_RECORDING_PAUSED };

    explicit StatusCell(const QString &rowId, const QString &textValue, QWidget *parent = (QWidget *)nullptr);
    ~StatusCell();

    void setIconShow(StatusIcon showIcon);

    void setTextValue(const QString &textValue);
    inline void setTheme(const QString &id, const QString &classes) { setThemeID(statusText, id, classes); };
    inline void setSplitRecordingButtonShow(bool show) { splitRecordingButton->setVisible(show); };
    inline bool isSplitRecordingButtonShow() const { return splitRecordingButton->isVisible(); };
    inline void setPauseRecordingButtonShow(bool show) { pauseRecordingButton->setVisible(show); };
    inline bool isPauseRecordingButtonShow() const { return pauseRecordingButton->isVisible(); };
    inline void setUnpauseRecordingButtonShow(bool show) { unpauseRecordingButton->setVisible(show); };
    inline bool isUnpauseRecordingButtonShow() const { return unpauseRecordingButton->isVisible(); };
    inline void setAddChapterToRecordingButtonShow(bool show) { addChapterToRecordingButton->setVisible(show); };
    inline bool isAddChapterToRecordingButtonShow() const { return addChapterToRecordingButton->isVisible(); };
    inline OutputTableCellItem *item() const { return _item; }
};

enum RowOutputType {
    ROW_OUTPUT_NONE = 0,
    ROW_OUTPUT_STREAMING = 1,
    ROW_OUTPUT_RECORDING = 2,
};

class BranchOutputStatusDock : public QFrame {
    Q_OBJECT

    friend class OutputTableRow;

    QIcon ascendingIcon;
    QIcon descendingIcon;

    QTimer timer;
    QTableWidget *outputTable = nullptr;
    QList<OutputTableRow *> outputTableRows;
    QLabel *applyToAllLabel = nullptr;
    QToolButton *enableAllButton = nullptr;
    QToolButton *disableAllButton = nullptr;
    QToolButton *splitRecordingAllButton = nullptr;
    QToolButton *pauseRecordingAllButton = nullptr;
    QToolButton *unpauseRecordingAllButton = nullptr;
    QToolButton *addChapterToRecordingAllButton = nullptr;
    QLabel *interlockLabel = nullptr;
    QComboBox *interlockComboBox = nullptr;
    OBSSignal sourceAddedSignal;
    obs_hotkey_id enableAllHotkey;
    obs_hotkey_id disableAllHotkey;
    obs_hotkey_id splitRecordingAllHotkey;
    obs_hotkey_id pauseRecordingAllHotkey;
    obs_hotkey_id unpauseRecordingAllHotkey;
    obs_hotkey_id addChapterToRecordingAllHotkey;
    int resetColumnIndex;
    int sortingColumnIndex;
    Qt::SortOrder sortingOrder;

    void update();
    void applyEnableAllButtonEnabled();
    void applyDisableAllButtonEnabled();
    void applySplitRecordingAllButtonEnabled();
    void applyPauseRecordingAllButtonEnabled();
    void applyUnpauseRecordingAllButtonEnabled();
    void applyAddChapterToRecordingAllButtonEnabled();
    void saveSettings();
    void loadSettings();

    static void onEanbleAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static void onDisableAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static void onSplitRecordingAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static void onPauseRecordingAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static void onUnpauseRecordingAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);
    static void onAddChapterToRecordingAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey *hotkey, bool pressed);

private slots:
    void onHeaderPressed(int index);

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
    void splitRecordingAll();
    void pauseRecordingAll();
    void unpauseRecordingAll();
    void addChapterToRecordingAll();
    void resetStatsAll();
    void sort();

    inline int getInterlockType() const { return interlockComboBox->currentData().toInt(); };
};

class OutputTableRow : public QObject {
    Q_OBJECT

    friend class BranchOutputStatusDock;

    BranchOutputFilter *filter;
    FilterCell *filterCell;
    ParentCell *parentCell;
    StatusCell *status;
    LabelCell *droppedFrames;
    LabelCell *megabytesSent;
    LabelCell *bitrate;
    LabelCell *outputName;

    RowOutputType outputType;
    size_t streamingIndex;
    size_t groupIndex;

    uint64_t lastBytesSent = 0;
    uint64_t lastBytesSentTime = 0;

    int first_total = 0;
    int first_dropped = 0;

    void update();
    void reset();
    void splitRecording();
    void pauseRecording();
    void unpauseRecording();
    void addChapterToRecording();
    void updateRowId();

    long double kbps = 0.0l;

public:
    explicit OutputTableRow(
        int row, BranchOutputFilter *filter, size_t streamingIndex, RowOutputType outputType, size_t groupIndex,
        BranchOutputStatusDock *parent = nullptr
    );
    ~OutputTableRow();
};
