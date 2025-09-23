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

#include <obs-module.h>
#include <util/platform.h>
#include <obs-frontend-api.h>

#include <QGridLayout>
#include <QLabel>
#include <QTimer>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QDesktopServices>

#include "../plugin-main.hpp"
#include "output-status-dock.hpp"

#define TIMER_INTERVAL 2000
#define SETTINGS_JSON_NAME "outputStatusDock.json"

// FIXME: Duplicated definition error with util/base.h
extern "C" {
extern void obs_log(int log_level, const char *format, ...);
}

//--- BranchOutputStatusDock class ---//

BranchOutputStatusDock::BranchOutputStatusDock(QWidget *parent) : QFrame(parent), timer(this)
{
    setMinimumWidth(320);

    // Setup statistics table
    outputTable = new QTableWidget(this);
    outputTable->verticalHeader()->hide();
    outputTable->horizontalHeader()->setSectionsClickable(false);
    outputTable->horizontalHeader()->setMinimumSectionSize(100);
    outputTable->setGridStyle(Qt::NoPen);
    outputTable->setHorizontalScrollMode(QTableView::ScrollMode::ScrollPerPixel);
    outputTable->setVerticalScrollMode(QTableView::ScrollMode::ScrollPerPixel);
    outputTable->setSelectionMode(QTableWidget::SelectionMode::NoSelection);
    outputTable->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    outputTable->setColumnCount(8);

    int col = 0;
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("FilterName")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("SourceName")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("Output")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("Status")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("DropFrames")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("SentDataSize")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("BitRate")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QString::fromUtf8("")));

    QObject::connect(&timer, &QTimer::timeout, this, &BranchOutputStatusDock::update);

    timer.setInterval(TIMER_INTERVAL);
    if (isVisible()) {
        timer.start();
    }

    // Tool buttons
    applyToAllLabel = new QLabel(QTStr("ApplyToAll"), this);

    enableAllButton = new QToolButton(this);
    enableAllButton->setToolTip(QTStr("EnableAll"));
    enableAllButton->setIcon(QIcon(":/branch-output/images/visible.svg"));
    enableAllButton->setEnabled(false);
    connect(enableAllButton, &QToolButton::clicked, this, [this]() { setEabnleAll(true); });

    disableAllButton = new QToolButton(this);
    disableAllButton->setToolTip(QTStr("DisableAll"));
    disableAllButton->setIcon(QIcon(":/branch-output/images/invisible.svg"));
    disableAllButton->setEnabled(false);
    connect(disableAllButton, &QToolButton::clicked, this, [this]() { setEabnleAll(false); });

    splitRecordingAllButton = new QToolButton(this);
    splitRecordingAllButton->setToolTip(QTStr("SplitAllRecordings"));
    splitRecordingAllButton->setIcon(QIcon(":/branch-output/images/scissors.svg"));
    splitRecordingAllButton->setEnabled(false);
    connect(splitRecordingAllButton, &QToolButton::clicked, this, [this]() { splitRecordingAll(); });

    pauseRecordingAllButton = new QToolButton(this);
    pauseRecordingAllButton->setToolTip(QTStr("PauseAllRecordings"));
    pauseRecordingAllButton->setIcon(QIcon(":/branch-output/images/pause.svg"));
    pauseRecordingAllButton->setEnabled(false);
    connect(pauseRecordingAllButton, &QToolButton::clicked, this, [this]() { pauseRecordingAll(); });

    unpauseRecordingAllButton = new QToolButton(this);
    unpauseRecordingAllButton->setToolTip(QTStr("UnpauseAllRecordings"));
    unpauseRecordingAllButton->setIcon(QIcon(":/branch-output/images/unpause.svg"));
    unpauseRecordingAllButton->setEnabled(false);
    connect(unpauseRecordingAllButton, &QToolButton::clicked, this, [this]() { unpauseRecordingAll(); });

    addChapterToRecordingAllButton = new QToolButton(this);
    addChapterToRecordingAllButton->setToolTip(QTStr("AddChapterToAllRecordings"));
    addChapterToRecordingAllButton->setIcon(QIcon(":/branch-output/images/chapter.svg"));
    addChapterToRecordingAllButton->setEnabled(false);
    connect(addChapterToRecordingAllButton, &QToolButton::clicked, this, [this]() { addChapterToRecordingAll(); });

    interlockLabel = new QLabel(QTStr("Interlock"), this);
    interlockComboBox = new QComboBox(this);
    interlockComboBox->addItem(QTStr("AlwaysOn"), BranchOutputFilter::INTERLOCK_TYPE_ALWAYS_ON);
    interlockComboBox->addItem(QTStr("Streaming"), BranchOutputFilter::INTERLOCK_TYPE_STREAMING);
    interlockComboBox->addItem(QTStr("Recording"), BranchOutputFilter::INTERLOCK_TYPE_RECORDING);
    interlockComboBox->addItem(QTStr("StreamingOrRecording"), BranchOutputFilter::INTERLOCK_TYPE_STREAMING_RECORDING);
    interlockComboBox->addItem(QTStr("VirtualCam"), BranchOutputFilter::INTERLOCK_TYPE_VIRTUAL_CAM);

    auto buttonsContainerLayout = new QHBoxLayout();
    buttonsContainerLayout->addWidget(applyToAllLabel);
    buttonsContainerLayout->addSpacing(5);
    buttonsContainerLayout->addWidget(enableAllButton);
    buttonsContainerLayout->addWidget(disableAllButton);
    buttonsContainerLayout->addWidget(splitRecordingAllButton);
    buttonsContainerLayout->addWidget(pauseRecordingAllButton);
    buttonsContainerLayout->addWidget(unpauseRecordingAllButton);
    buttonsContainerLayout->addWidget(addChapterToRecordingAllButton);
    buttonsContainerLayout->addStretch();
    buttonsContainerLayout->addWidget(interlockLabel);
    buttonsContainerLayout->addWidget(interlockComboBox);

    auto *outputContainerLayout = new QVBoxLayout();
    outputContainerLayout->addWidget(outputTable);
    outputContainerLayout->addLayout(buttonsContainerLayout);
    this->setLayout(outputContainerLayout);

    // Register hotkeys
    enableAllHotkey = obs_hotkey_register_frontend(
        "EnableAllBranchOutputsHotkey", obs_module_text("EnableAllHotkey"), onEanbleAllHotkeyPressed, this
    );
    disableAllHotkey = obs_hotkey_register_frontend(
        "DisableAllBranchOutputsHotkey", obs_module_text("DisableAllHotkey"), onDisableAllHotkeyPressed, this
    );
    splitRecordingAllHotkey = obs_hotkey_register_frontend(
        "SplitRecordingAllBranchOutputsHotkey", obs_module_text("SplitRecordingAllHotkey"),
        onSplitRecordingAllHotkeyPressed, this
    );
    pauseRecordingAllHotkey = obs_hotkey_register_frontend(
        "PauseRecordingAllBranchOutputsHotkey", obs_module_text("PauseRecordingAllHotkey"),
        onPauseRecordingAllHotkeyPressed, this
    );
    unpauseRecordingAllHotkey = obs_hotkey_register_frontend(
        "UnpauseRecordingAllBranchOutputsHotkey", obs_module_text("UnpauseRecordingAllHotkey"),
        onUnpauseRecordingAllHotkeyPressed, this
    );
    addChapterToRecordingAllHotkey = obs_hotkey_register_frontend(
        "AddChapterToRecordingAllBranchOutputsHotkey", obs_module_text("AddChapterToRecordingAllHotkey"),
        onAddChapterToRecordingAllHotkeyPressed, this
    );

    loadSettings();
    loadHotkey(enableAllHotkey, "EnableAllBranchOutputsHotkey");
    loadHotkey(disableAllHotkey, "DisableAllBranchOutputsHotkey");
    loadHotkey(splitRecordingAllHotkey, "SplitRecordingAllBranchOutputsHotkey");
    loadHotkey(pauseRecordingAllHotkey, "PauseRecordingAllBranchOutputsHotkey");
    loadHotkey(unpauseRecordingAllHotkey, "UnpauseRecordingAllBranchOutputsHotkey");
    loadHotkey(addChapterToRecordingAllHotkey, "AddChapterToRecordingAllBranchOutputsHotkey");

    obs_log(LOG_DEBUG, "BranchOutputStatusDock created");
}

BranchOutputStatusDock::~BranchOutputStatusDock()
{
    saveSettings();

    // Unregister hotkeys
    obs_hotkey_unregister(enableAllHotkey);
    obs_hotkey_unregister(disableAllHotkey);
    obs_hotkey_unregister(splitRecordingAllHotkey);
    obs_hotkey_unregister(pauseRecordingAllHotkey);
    obs_hotkey_unregister(unpauseRecordingAllHotkey);
    obs_hotkey_unregister(addChapterToRecordingAllHotkey);

    obs_log(LOG_DEBUG, "BranchOutputStatusDock destroyed");
}

void BranchOutputStatusDock::loadSettings()
{
    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    OBSDataAutoRelease settings = obs_data_create_from_json_file(path);
    if (!settings) {
        return;
    }

    auto loadColumn = [&](int i, const char *key) {
        auto width = obs_data_get_int(settings, qUtf8Printable(QString("column.%1.width").arg(key)));
        if (width > 0) {
            outputTable->setColumnWidth(i, (int)width);
        }
    };

    int col = 0;
    loadColumn(col++, "filterName");
    loadColumn(col++, "sourceName");
    loadColumn(col++, "output");
    loadColumn(col++, "status");
    loadColumn(col++, "dropFrames");
    loadColumn(col++, "sentDataSize");
    loadColumn(col++, "bitRate");

    interlockComboBox->setCurrentIndex(interlockComboBox->findData(obs_data_get_int(settings, "interlock")));
}

void BranchOutputStatusDock::saveSettings()
{
    OBSDataAutoRelease settings = obs_data_create();

    auto saveColumn = [&](int i, const char *key) {
        obs_data_set_int(settings, qUtf8Printable(QString("column.%1.width").arg(key)), outputTable->columnWidth(i));
    };

    int col = 0;
    saveColumn(col++, "filterName");
    saveColumn(col++, "sourceName");
    saveColumn(col++, "output");
    saveColumn(col++, "status");
    saveColumn(col++, "dropFrames");
    saveColumn(col++, "sentDataSize");
    saveColumn(col++, "bitRate");

    obs_data_set_int(settings, "interlock", interlockComboBox->currentData().toInt());

    OBSString config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);

    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settings, path, "tmp", "bak");
}

void BranchOutputStatusDock::addRow(
    BranchOutputFilter *filter, size_t streamingIndex, RowOutputType outputType, size_t groupIndex
)
{
    auto row = (int)outputTableRows.size();
    outputTableRows.push_back(new OutputTableRow(row, filter, streamingIndex, outputType, groupIndex, this));

    applyEnableAllButtonEnabled();
    applyDisableAllButtonEnabled();
}

void BranchOutputStatusDock::addFilter(BranchOutputFilter *filter)
{
    // Ensure filter removed
    removeFilter(filter);

    OBSDataAutoRelease settings = obs_source_get_settings(filter->filterSource);

    auto groupIndex = 0;

    // Recording row
    if (filter->isRecordingEnabled(settings)) {
        addRow(filter, 0, ROW_OUTPUT_RECORDING, groupIndex++);
    }

    // Streaming rows
    auto serviceCount = (size_t)obs_data_get_int(settings, "service_count");
    for (size_t i = 0; i < MAX_SERVICES && i < serviceCount; i++) {
        if (filter->isStreamingEnabled(settings, i)) {
            addRow(filter, i, ROW_OUTPUT_STREAMING, groupIndex++);
        }
    }

    if (groupIndex == 0) {
        // Add disabled row
        addRow(filter, 0, ROW_OUTPUT_NONE, groupIndex++);
    }
}

void BranchOutputStatusDock::removeFilter(BranchOutputFilter *filter)
{
    // DO NOT access filter resources at this time (It may be already deleted)
    foreach (auto row, outputTableRows) {
        if (row->filter == filter) {
            outputTable->removeRow((int)outputTableRows.indexOf(row));
            outputTableRows.removeOne(row);
            row->deleteLater();
        }
    }
}

void BranchOutputStatusDock::update()
{
    foreach (auto row, outputTableRows) {
        if (!sourceInFrontend(obs_filter_get_parent(row->filter->filterSource))) {
            // Remove filter that no longer exists in the frontend
            removeFilter(row->filter);
            continue;
        }
        row->update();
    }

    applyEnableAllButtonEnabled();
    applyDisableAllButtonEnabled();
    applySplitRecordingAllButtonEnabled();
    applyPauseRecordingAllButtonEnabled();
    applyUnpauseRecordingAllButtonEnabled();
    applyAddChapterToRecordingAllButtonEnabled();
}

void BranchOutputStatusDock::applyEnableAllButtonEnabled()
{
    foreach (auto row, outputTableRows) {
        if (!row->filterCell->isVisibilityChecked()) {
            enableAllButton->setEnabled(true);
            return;
        }
    }
    enableAllButton->setEnabled(false);
}

void BranchOutputStatusDock::applyDisableAllButtonEnabled()
{
    foreach (auto row, outputTableRows) {
        if (row->filterCell->isVisibilityChecked()) {
            disableAllButton->setEnabled(true);
            return;
        }
    }
    disableAllButton->setEnabled(false);
}

void BranchOutputStatusDock::applySplitRecordingAllButtonEnabled()
{
    foreach (auto row, outputTableRows) {
        if (row->status->isSplitRecordingButtonShow()) {
            splitRecordingAllButton->setEnabled(true);
            return;
        }
    }
    splitRecordingAllButton->setEnabled(false);
}

void BranchOutputStatusDock::applyPauseRecordingAllButtonEnabled()
{
    foreach (auto row, outputTableRows) {
        if (row->status->isPauseRecordingButtonShow()) {
            pauseRecordingAllButton->setEnabled(true);
            return;
        }
    }
    pauseRecordingAllButton->setEnabled(false);
}

void BranchOutputStatusDock::applyUnpauseRecordingAllButtonEnabled()
{
    foreach (auto row, outputTableRows) {
        if (row->status->isUnpauseRecordingButtonShow()) {
            unpauseRecordingAllButton->setEnabled(true);
            return;
        }
    }
    unpauseRecordingAllButton->setEnabled(false);
}

void BranchOutputStatusDock::applyAddChapterToRecordingAllButtonEnabled()
{
    foreach (auto row, outputTableRows) {
        if (row->status->isAddChapterToRecordingButtonShow()) {
            addChapterToRecordingAllButton->setEnabled(true);
            return;
        }
    }
    addChapterToRecordingAllButton->setEnabled(false);
}

void BranchOutputStatusDock::showEvent(QShowEvent *)
{
    timer.start(TIMER_INTERVAL);
}

void BranchOutputStatusDock::hideEvent(QHideEvent *)
{
    timer.stop();
}

void BranchOutputStatusDock::setEabnleAll(bool enabled)
{
    foreach (auto row, outputTableRows) {
        if (row->groupIndex == 0) {
            // Do only once for each filters
            obs_source_set_enabled(row->filter->filterSource, enabled);
        }
    }

    applyEnableAllButtonEnabled();
    applyDisableAllButtonEnabled();
}

void BranchOutputStatusDock::splitRecordingAll()
{
    foreach (auto row, outputTableRows) {
        if (row->outputType == ROW_OUTPUT_RECORDING && row->status->isSplitRecordingButtonShow()) {
            row->filter->splitRecording();
        }
    }
}

void BranchOutputStatusDock::pauseRecordingAll()
{
    foreach (auto row, outputTableRows) {
        if (row->outputType == ROW_OUTPUT_RECORDING && row->status->isPauseRecordingButtonShow()) {
            row->filter->pauseRecording();
        }
    }
}

void BranchOutputStatusDock::unpauseRecordingAll()
{
    foreach (auto row, outputTableRows) {
        if (row->outputType == ROW_OUTPUT_RECORDING && row->status->isUnpauseRecordingButtonShow()) {
            row->filter->unpauseRecording();
        }
    }
}

void BranchOutputStatusDock::addChapterToRecordingAll()
{
    foreach (auto row, outputTableRows) {
        if (row->outputType == ROW_OUTPUT_RECORDING && row->status->isAddChapterToRecordingButtonShow()) {
            row->filter->addChapterToRecording();
        }
    }
}

void BranchOutputStatusDock::onEanbleAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    auto dock = static_cast<BranchOutputStatusDock *>(data);
    if (pressed) {
        dock->setEabnleAll(true);
    }
}

void BranchOutputStatusDock::onDisableAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    auto dock = static_cast<BranchOutputStatusDock *>(data);
    if (pressed) {
        dock->setEabnleAll(false);
    }
}

void BranchOutputStatusDock::onSplitRecordingAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    auto dock = static_cast<BranchOutputStatusDock *>(data);
    if (pressed) {
        dock->splitRecordingAll();
    }
}

void BranchOutputStatusDock::onPauseRecordingAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    auto dock = static_cast<BranchOutputStatusDock *>(data);
    if (pressed) {
        dock->pauseRecordingAll();
    }
}

void BranchOutputStatusDock::onUnpauseRecordingAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    auto dock = static_cast<BranchOutputStatusDock *>(data);
    if (pressed) {
        dock->unpauseRecordingAll();
    }
}

void BranchOutputStatusDock::onAddChapterToRecordingAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *, bool pressed)
{
    auto dock = static_cast<BranchOutputStatusDock *>(data);
    if (pressed) {
        dock->addChapterToRecordingAll();
    }
}

//--- OutputTableRow class ---//

OutputTableRow::OutputTableRow(
    int row, BranchOutputFilter *_filter, size_t _streamingIndex, RowOutputType _outputType, size_t _groupIndex,
    BranchOutputStatusDock *parent
)
    : QObject(parent),
      filter(_filter),
      streamingIndex(_streamingIndex),
      outputType(_outputType),
      groupIndex(_groupIndex)
{
    auto source = obs_filter_get_parent(filter->filterSource);

    filterCell = new FilterCell(filter->name, filter->filterSource, parent);
    parentCell = new ParentCell(obs_source_get_name(source), source, parent);
    status = new StatusCell(QTStr("Status.Inactive"), parent);

    OBSDataAutoRelease settings = obs_source_get_settings(filter->filterSource);

    switch (outputType) {
    case ROW_OUTPUT_STREAMING:
        outputName = new QLabel(QTStr("Streaming%1").arg(streamingIndex + 1), parent);
        break;
    case ROW_OUTPUT_RECORDING:
        outputName = new RecordingOutputCell(QTStr("Recording"), filter->filterSource, parent);
        isSplitRecordingEnabled = filter->isRecordingSplitEnabled(settings);
        break;
    default:
        outputName = new QLabel(QTStr("None"), parent);
    }

    droppedFrames = new QLabel("", parent);
    megabytesSent = new QLabel("", parent);
    bitrate = new QLabel("", parent);

    auto col = 0;
    parent->outputTable->setRowCount(row + 1);
    parent->outputTable->setCellWidget(row, col++, filterCell);
    parent->outputTable->setCellWidget(row, col++, parentCell);
    parent->outputTable->setCellWidget(row, col++, outputName);
    parent->outputTable->setCellWidget(row, col++, status);
    parent->outputTable->setCellWidget(row, col++, droppedFrames);
    parent->outputTable->setCellWidget(row, col++, megabytesSent);
    parent->outputTable->setCellWidget(row, col++, bitrate);

    parent->outputTable->setRowHeight(row, 32);

    // Setup reset button
    auto buttonsContainer = new QWidget(parent);
    auto buttonsContainerLayout = new QHBoxLayout();
    buttonsContainerLayout->setContentsMargins(0, 0, 0, 0);
    buttonsContainer->setLayout(buttonsContainerLayout);

    auto resetButton = new QPushButton(QTStr("Reset"), parent);
    connect(resetButton, &QPushButton::clicked, this, [parent, row]() { parent->outputTableRows[row]->reset(); });
    resetButton->setProperty("toolButton", true);  // Until OBS 30
    resetButton->setProperty("class", "btn-tool"); // Since OBS 31
    resetButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    buttonsContainerLayout->addWidget(resetButton);
    parent->outputTable->setCellWidget(row, col, buttonsContainer);

    // Setup status buttons
    connect(status, &StatusCell::splitRecordingButtonClicked, this, [this]() { splitRecording(); });
    connect(status, &StatusCell::pauseRecordingButtonClicked, this, [this]() { pauseRecording(); });
    connect(status, &StatusCell::unpauseRecordingButtonClicked, this, [this]() { unpauseRecording(); });
    connect(status, &StatusCell::addChapterToRecordingButtonClicked, this, [this]() { addChapterToRecording(); });
}

OutputTableRow::~OutputTableRow()
{
    disconnect(this);
}

// Imitate UI/window-basic-stats.cpp
void OutputTableRow::update()
{
    obs_output_t *output;

    switch (outputType) {
    case ROW_OUTPUT_STREAMING:
        output = filter->streamings[streamingIndex].output.Get();
        break;
    case ROW_OUTPUT_RECORDING:
        output = filter->recordingOutput.Get();
        break;
    default:
        output = nullptr;
        break;
    }

    // Status display
    if (output) {
        bool reconnecting;
        // Only recording can be paused (When recording is paused, streaming is also paused)
        bool paused = filter->recordingOutput.Get() && obs_output_paused(filter->recordingOutput.Get());

        switch (outputType) {
        case ROW_OUTPUT_STREAMING:
            reconnecting = output ? !obs_output_active(output) || obs_output_reconnecting(output) : false;
            break;
        default:
            reconnecting = false;
            break;
        }

        if (reconnecting) {
            status->setText(QTStr("Status.Reconnecting"));
            status->setTheme("error", "text-danger");
            status->setIconShow(StatusCell::StatusIcon::STATUS_ICON_NONE);
            status->setSplitRecordingButtonShow(false);
            status->setPauseRecordingButtonShow(false);
            status->setUnpauseRecordingButtonShow(false);
            status->setAddChapterToRecordingButtonShow(false);
            status->setAddChapterToRecordingButtonShow(false);
        } else {
            switch (outputType) {
            case ROW_OUTPUT_STREAMING:
                if (paused) {
                    status->setText(QTStr("Status.Paused"));
                    status->setTheme("", "");
                } else {
                    status->setText(QTStr("Status.Streaming"));
                    status->setTheme("good", "text-success");
                }
                status->setIconShow(StatusCell::StatusIcon::STATUS_ICON_STREAMING);
                status->setSplitRecordingButtonShow(false);
                status->setPauseRecordingButtonShow(false);
                status->setUnpauseRecordingButtonShow(false);
                status->setAddChapterToRecordingButtonShow(false);
                break;
            case ROW_OUTPUT_RECORDING:
                if (paused) {
                    status->setText(QTStr("Status.Paused"));
                    status->setTheme("", "");
                    status->setIconShow(StatusCell::StatusIcon::STATUS_ICON_RECORDING_PAUSED);
                    status->setSplitRecordingButtonShow(false);
                } else {
                    status->setText(QTStr("Status.Recording"));
                    status->setTheme("good", "text-success");
                    status->setIconShow(StatusCell::StatusIcon::STATUS_ICON_RECORDING);
                    status->setSplitRecordingButtonShow(isSplitRecordingEnabled);
                }
                status->setPauseRecordingButtonShow(!paused && filter->canPauseRecording());
                status->setUnpauseRecordingButtonShow(paused && !filter->recordingPending);
                status->setAddChapterToRecordingButtonShow(!paused && filter->canAddChapterToRecording());
                break;
            default:
                status->setText(QTStr("Status.Inactive"));
                status->setTheme("good", "text-success");
                status->setIconShow(StatusCell::StatusIcon::STATUS_ICON_NONE);
                status->setSplitRecordingButtonShow(false);
                status->setPauseRecordingButtonShow(false);
                status->setUnpauseRecordingButtonShow(false);
                status->setAddChapterToRecordingButtonShow(false);
            }
        }
    } else {
        if (outputType == ROW_OUTPUT_RECORDING && filter->recordingPending) {
            status->setText(QTStr("Status.Pending"));
        } else {
            status->setText(QTStr("Status.Inactive"));
        }
        status->setTheme("", "");
        status->setIconShow(StatusCell::StatusIcon::STATUS_ICON_NONE);
        status->setSplitRecordingButtonShow(false);
        status->setPauseRecordingButtonShow(false);
        status->setUnpauseRecordingButtonShow(false);
        status->setAddChapterToRecordingButtonShow(false);
        droppedFrames->setText("");
        megabytesSent->setText("");
        bitrate->setText("");
        return;
    }

    uint64_t totalBytes = obs_output_get_total_bytes(output);
    uint64_t curTime = os_gettime_ns();
    uint64_t bytesSent = totalBytes;

    if (bytesSent < lastBytesSent) {
        bytesSent = 0;
    }
    if (bytesSent == 0) {
        lastBytesSent = 0;
    }

    uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;
    long double timePassed = (long double)(curTime - lastBytesSentTime) / 1000000000.0l;
    kbps = (long double)bitsBetween / timePassed / 1000.0l;

    if (timePassed < 0.01l) {
        kbps = 0.0l;
    }

    long double num = (long double)totalBytes / (1024.0l * 1024.0l);
    const char *unit = "MiB";
    if (num > 1024) {
        num /= 1024;
        unit = "GiB";
    }
    megabytesSent->setText(outputType != ROW_OUTPUT_NONE ? QString("%1 %2").arg((double)num, 0, 'f', 1).arg(unit) : "");

    num = kbps;
    unit = "kb/s";
    if (num >= 10'000) {
        num /= 1000;
        unit = "Mb/s";
    }
    bitrate->setText(outputType != ROW_OUTPUT_NONE ? QString("%1 %2").arg((double)num, 0, 'f', 0).arg(unit) : "");

    // Calculate statistics
    int total = output ? obs_output_get_total_frames(output) : 0;
    int dropped = output ? obs_output_get_frames_dropped(output) : 0;

    if (total < first_total || dropped < first_dropped) {
        first_total = 0;
        first_dropped = 0;
    }

    total -= first_total;
    dropped -= first_dropped;

    num = total ? (long double)dropped / (long double)total * 100.0l : 0.0l;

    QString dropFramesStr =
        QString("%1 / %2 (%3%)")
            .arg(QString::number(dropped), QString::number(total), QString::number((double)num, 'f', 1));
    droppedFrames->setText(outputType != ROW_OUTPUT_NONE ? dropFramesStr : "");

    if (num > 5.0l) {
        setThemeID(droppedFrames, "error", "text-danger");
    } else if (num > 1.0l) {
        setThemeID(droppedFrames, "warning", "text-warning");
    } else {
        setThemeID(droppedFrames, "", "");
    }

    lastBytesSent = bytesSent;
    lastBytesSentTime = curTime;
}

void OutputTableRow::reset()
{
    obs_output_t *output;

    switch (outputType) {
    case ROW_OUTPUT_STREAMING:
        output = streamingIndex < MAX_SERVICES ? filter->streamings[streamingIndex].output.Get() : nullptr;
        break;
    case ROW_OUTPUT_RECORDING:
        output = filter->recordingOutput.Get();
        break;
    default:
        output = nullptr;
    }

    if (!output) {
        droppedFrames->setText("");
        megabytesSent->setText("");
        bitrate->setText("");
        return;
    }

    first_total = obs_output_get_total_frames(output);
    first_dropped = obs_output_get_frames_dropped(output);
    droppedFrames->setText(QString("0 / 0 (0)"));
    megabytesSent->setText(QString("0 MiB"));
    bitrate->setText(QString("0 kb/s"));
}

void OutputTableRow::splitRecording()
{
    if (outputType != ROW_OUTPUT_RECORDING) {
        return;
    }

    filter->splitRecording();
}

void OutputTableRow::pauseRecording()
{
    if (outputType != ROW_OUTPUT_RECORDING) {
        return;
    }

    filter->pauseRecording();
}

void OutputTableRow::unpauseRecording()
{
    if (outputType != ROW_OUTPUT_RECORDING) {
        return;
    }

    filter->unpauseRecording();
}

void OutputTableRow::addChapterToRecording()
{
    if (outputType != ROW_OUTPUT_RECORDING) {
        return;
    }

    filter->addChapterToRecording();
}

//--- FilterCell class ---//

FilterCell::FilterCell(const QString &text, obs_source_t *source, QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(27);

    visibilityCheckbox = new QCheckBox(this);
    visibilityCheckbox->setProperty("visibilityCheckBox", true);      // Until OBS 30
    visibilityCheckbox->setProperty("class", "indicator-visibility"); // Since OBS 31
    visibilityCheckbox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    visibilityCheckbox->setChecked(obs_source_enabled(source));
    visibilityCheckbox->setCursor(Qt::PointingHandCursor);

    connect(visibilityCheckbox, &QCheckBox::clicked, this, [source](bool visible) {
        obs_source_set_enabled(source, visible);
    });

    name = new QLabel(text, this);

    auto checkboxLayout = new QHBoxLayout();
    checkboxLayout->setContentsMargins(0, 0, 0, 0);
    checkboxLayout->addWidget(visibilityCheckbox);
    checkboxLayout->addWidget(name);
    setLayout(checkboxLayout);

    // Listen signal for filter update
    filterRenamedSignal.Connect(obs_source_get_signal_handler(source), "rename", FilterCell::onFilterRenamed, this);

    // Listen signal for filter enabled/disabled
    enableSignal.Connect(obs_source_get_signal_handler(source), "enable", FilterCell::onVisibilityChanged, this);
}

FilterCell::~FilterCell()
{
    filterRenamedSignal.Disconnect();
    enableSignal.Disconnect();
}

void FilterCell::setText(const QString &text)
{
    name->setText(text);
}

void FilterCell::onFilterRenamed(void *data, calldata_t *cd)
{
    auto item = static_cast<FilterCell *>(data);
    item->setText(calldata_string(cd, "new_name"));
}

void FilterCell::onVisibilityChanged(void *data, calldata_t *cd)
{
    auto item = static_cast<FilterCell *>(data);
    auto enabled = calldata_bool(cd, "enabled");
    item->visibilityCheckbox->setChecked(enabled);
}

//--- ParentCell class ---//

ParentCell::ParentCell(const QString &text, obs_source_t *_source, QWidget *parent) : QLabel(parent), source(_source)
{
    parentRenamedSignal.Connect(obs_source_get_signal_handler(source), "rename", ParentCell::onParentRenamed, this);

    // Markup as link
    setTextFormat(Qt::RichText);
    setCursor(Qt::PointingHandCursor);

    setSourceName(text);
}

ParentCell::~ParentCell()
{
    parentRenamedSignal.Disconnect();
}

void ParentCell::onParentRenamed(void *data, calldata_t *cd)
{
    auto item = static_cast<ParentCell *>(data);
    auto newName = calldata_string(cd, "new_name");
    item->setSourceName(newName);
}

void ParentCell::setSourceName(const QString &text)
{
    // Markup as link
    setText(QString("<u>%1</u>").arg(text));
}

void ParentCell::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Open filter properties dialog
        obs_log(LOG_DEBUG, "uuid=%s", obs_source_get_uuid(source));
        obs_frontend_open_source_filters(source);
    }
}

//--- RecordingOutputCell class ---//

RecordingOutputCell::RecordingOutputCell(const QString &text, obs_source_t *_source, QWidget *parent)
    : QLabel(parent),
      source(_source)
{
    // Markup as link
    setTextFormat(Qt::RichText);
    setCursor(Qt::PointingHandCursor);

    setOutputName(text);
}

RecordingOutputCell::~RecordingOutputCell() {}

void RecordingOutputCell::setOutputName(const QString &text)
{

    // Markup as link
    setText(QString("<u>%1</u>").arg(text));
}

void RecordingOutputCell::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Open OS file browser
        OBSDataAutoRelease settings = obs_source_get_settings(source);
        auto path = obs_data_get_bool(settings, "use_profile_recording_path")
                        ? getProfileRecordingPath(obs_frontend_get_profile_config())
                        : obs_data_get_string(settings, "path");
        obs_log(LOG_DEBUG, "path=%s", path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

//--- StatusCell class ---//

StatusCell::StatusCell(const QString &text, QWidget *parent) : QWidget(parent)
{
    streamingIcon = new QLabel(this);
    recordingIcon = new QLabel(this);
    recordingPausedIcon = new QLabel(this);
    statusText = new QLabel(text, this);
    splitRecordingButton = new QToolButton(this);
    pauseRecordingButton = new QToolButton(this);
    unpauseRecordingButton = new QToolButton(this);
    addChapterToRecordingButton = new QToolButton(this);

    streamingIcon->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    streamingIcon->setPixmap(QPixmap(":/branch-output/images/streaming.svg").scaled(16, 16));
    streamingIcon->setVisible(false);

    recordingIcon->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    recordingIcon->setPixmap(QPixmap(":/branch-output/images/recording.svg").scaled(16, 16));
    recordingIcon->setVisible(false);

    recordingPausedIcon->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    recordingPausedIcon->setPixmap(QPixmap(":/branch-output/images/recording-paused.svg").scaled(16, 16));
    recordingPausedIcon->setVisible(false);

    splitRecordingButton->setIcon(QIcon(":/branch-output/images/scissors.svg"));
    splitRecordingButton->setVisible(false);

    pauseRecordingButton->setIcon(QIcon(":/branch-output/images/pause.svg"));
    pauseRecordingButton->setVisible(false);

    unpauseRecordingButton->setIcon(QIcon(":/branch-output/images/unpause.svg"));
    unpauseRecordingButton->setVisible(false);

    addChapterToRecordingButton->setIcon(QIcon(":/branch-output/images/chapter.svg"));
    addChapterToRecordingButton->setVisible(false);

    connect(splitRecordingButton, &QToolButton::clicked, this, [this]() { emit splitRecordingButtonClicked(); });
    connect(pauseRecordingButton, &QToolButton::clicked, this, [this]() { emit pauseRecordingButtonClicked(); });
    connect(unpauseRecordingButton, &QToolButton::clicked, this, [this]() { emit unpauseRecordingButtonClicked(); });
    connect(addChapterToRecordingButton, &QToolButton::clicked, this, [this]() { emit addChapterToRecordingButtonClicked(); });

    auto layout = new QHBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(streamingIcon);
    layout->addWidget(recordingIcon);
    layout->addWidget(recordingPausedIcon);
    layout->addWidget(statusText);
    layout->addWidget(pauseRecordingButton);
    layout->addWidget(unpauseRecordingButton);
    layout->addWidget(splitRecordingButton);
    layout->addWidget(addChapterToRecordingButton);
    layout->addSpacing(5);
    setLayout(layout);
}

StatusCell::~StatusCell()
{
    disconnect(this);
}

void StatusCell::setIconShow(StatusIcon show)
{
    switch (show) {
    case STATUS_ICON_NONE:
        streamingIcon->setVisible(false);
        recordingIcon->setVisible(false);
        recordingPausedIcon->setVisible(false);
        break;
    case STATUS_ICON_STREAMING:
        streamingIcon->setVisible(true);
        recordingIcon->setVisible(false);
        recordingPausedIcon->setVisible(false);
        break;
    case STATUS_ICON_RECORDING:
        streamingIcon->setVisible(false);
        recordingIcon->setVisible(true);
        recordingPausedIcon->setVisible(false);
        break;
    case STATUS_ICON_RECORDING_PAUSED:
        streamingIcon->setVisible(false);
        recordingIcon->setVisible(false);
        recordingPausedIcon->setVisible(true);
        break;
    }
}
