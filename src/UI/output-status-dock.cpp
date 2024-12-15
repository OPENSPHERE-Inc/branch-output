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
#include <QCheckBox>
#include <QMouseEvent>

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
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("Status")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("Recording")));
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
    enableAllButton = new QPushButton(QTStr("EnableAll"), this);
    connect(enableAllButton, &QPushButton::clicked, [this]() { setEabnleAll(true); });

    disableAllButton = new QPushButton(QTStr("DisableAll"), this);
    connect(disableAllButton, &QPushButton::clicked, [this]() { setEabnleAll(false); });

    interlockLabel = new QLabel(QTStr("Interlock"), this);
    interlockComboBox = new QComboBox(this);
    interlockComboBox->addItem(QTStr("AlwaysOn"), INTERLOCK_TYPE_ALWAYS_ON);
    interlockComboBox->addItem(QTStr("Streaming"), INTERLOCK_TYPE_STREAMING);
    interlockComboBox->addItem(QTStr("Recording"), INTERLOCK_TYPE_RECORDING);
    interlockComboBox->addItem(QTStr("StreamingOrRecording"), INTERLOCK_TYPE_STREAMING_RECORDING);
    interlockComboBox->addItem(QTStr("VirtualCam"), INTERLOCK_TYPE_VIRTUAL_CAM);

    auto buttonsContainerLayout = new QHBoxLayout();
    buttonsContainerLayout->addWidget(enableAllButton);
    buttonsContainerLayout->addWidget(disableAllButton);
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

    loadSettings();
    loadHotkey(enableAllHotkey, "EnableAllBranchOutputsHotkey");
    loadHotkey(disableAllHotkey, "DisableAllBranchOutputsHotkey");

    obs_log(LOG_DEBUG, "BranchOutputStatusDock created");
}

BranchOutputStatusDock::~BranchOutputStatusDock()
{
    saveSettings();

    // Unregister hotkeys
    obs_hotkey_unregister(enableAllHotkey);
    obs_hotkey_unregister(disableAllHotkey);

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
    loadColumn(col++, "status");
    loadColumn(col++, "recording");
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
    saveColumn(col++, "status");
    saveColumn(col++, "recording");
    saveColumn(col++, "dropFrames");
    saveColumn(col++, "sentDataSize");
    saveColumn(col++, "bitRate");

    obs_data_set_int(settings, "interlock", interlockComboBox->currentData().toInt());

    OBSString config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);

    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settings, path, "tmp", "bak");
}

void BranchOutputStatusDock::addFilter(BranchOutputFilter *filter)
{
    auto parent = obs_filter_get_parent(filter->filterSource);
    auto row = (int)outputTableRows.size();

    auto otr = new OutputTableRow(this);

    otr->filter = filter;
    otr->filterCell =
        new FilterCell(QString::fromUtf8(obs_source_get_name(filter->filterSource)), filter->filterSource, this);
    otr->parentCell = new ParentCell(obs_source_get_name(parent), parent, this);
    otr->status = new StatusCell(QTStr("Status.Inactive"), this);
    otr->status->setIcon(QPixmap(":/branch-output/images/streaming.svg").scaled(16, 16));
    otr->recording = new StatusCell(QTStr("Status.Inactive"), this);
    otr->recording->setIcon(QPixmap(":/branch-output/images/recording.svg").scaled(16, 16));
    otr->droppedFrames = new QLabel("", this);
    otr->megabytesSent = new QLabel("", this);
    otr->bitrate = new QLabel("", this);

    auto col = 0;
    outputTable->setRowCount(row + 1);
    outputTable->setCellWidget(row, col++, otr->filterCell);
    outputTable->setCellWidget(row, col++, otr->parentCell);
    outputTable->setCellWidget(row, col++, otr->status);
    outputTable->setCellWidget(row, col++, otr->recording);
    outputTable->setCellWidget(row, col++, otr->droppedFrames);
    outputTable->setCellWidget(row, col++, otr->megabytesSent);
    outputTable->setCellWidget(row, col++, otr->bitrate);

    outputTable->setRowHeight(row, 32);

    // Setup reset button
    auto resetButtonContainer = new QWidget(this);
    auto resetButtonContainerLayout = new QHBoxLayout();
    resetButtonContainerLayout->setContentsMargins(0, 0, 0, 0);
    resetButtonContainer->setLayout(resetButtonContainerLayout);

    auto resetButton = new QPushButton(QTStr("Reset"), this);
    connect(resetButton, &QPushButton::clicked, [this, row]() { outputTableRows[row]->reset(); });
    resetButton->setProperty("toolButton", true);  // Until OBS 30
    resetButton->setProperty("class", "btn-tool"); // Since OBS 31
    resetButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    resetButtonContainerLayout->addWidget(resetButton);
    outputTable->setCellWidget(row, col, resetButtonContainer);

    outputTableRows.push_back(otr);
}

void BranchOutputStatusDock::removeFilter(BranchOutputFilter *filter)
{
    // DO NOT access filter resources at this time (It may be already deleted)
    for (int i = 0; i < outputTableRows.size(); i++) {
        auto otr = outputTableRows[i];
        if (otr->filter == filter) {
            outputTable->removeRow(i);
            outputTableRows.removeAt(i);
            otr->deleteLater();
            break;
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
        obs_source_set_enabled(row->filter->filterSource, enabled);
    }
}

BranchOutputFilter *BranchOutputStatusDock::findFilter(const QString &parentName, const QString &filterName)
{
    for (auto row : outputTableRows) {
        if (filterName == obs_source_get_name(row->filter->filterSource) &&
            parentName == obs_source_get_name(obs_filter_get_parent(row->filter->filterSource))) {
            return row->filter;
        }
    }
    return nullptr;
}

void BranchOutputStatusDock::onEanbleAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *hotkey, bool pressed)
{
    auto dock = (BranchOutputStatusDock *)data;
    if (pressed) {
        dock->setEabnleAll(true);
    }
}

void BranchOutputStatusDock::onDisableAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey *hotkey, bool pressed)
{
    auto dock = (BranchOutputStatusDock *)data;
    if (pressed) {
        dock->setEabnleAll(false);
    }
}

//--- OutputTableRow class ---//

OutputTableRow::OutputTableRow(QObject *parent) : QObject(parent) {}

OutputTableRow::~OutputTableRow() {}

// Imitate UI/window-basic-stats.cpp
void OutputTableRow::update()
{
    auto output = filter->streamOutput ? filter->streamOutput : filter->recordingOutput;
    uint64_t totalBytes = output ? obs_output_get_total_bytes(output) : 0;
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

    // Status display
    if (filter->streamOutput) {
        bool reconnecting = filter->streamOutput ? !obs_output_active(filter->streamOutput) ||
                                                       obs_output_reconnecting(filter->streamOutput)
                                                 : false;

        if (reconnecting) {
            status->setText(QTStr("Status.Reconnecting"));
            status->setTheme("error", "text-danger");
            status->setIconShow(false);
        } else {
            status->setText(QTStr("Status.Live"));
            status->setTheme("good", "text-success");
            status->setIconShow(true);
        }
    } else {
        status->setText(QTStr("Status.Inactive"));
        status->setTheme("", "");
        status->setIconShow(false);
    }

    // Recording display
    if (filter->recordingOutput && obs_output_active(filter->recordingOutput)) {
        recording->setText(QTStr("Status.Recording"));
        recording->setTheme("good", "text-success");
        recording->setIconShow(true);
    } else {
        recording->setText(QTStr("Status.Inactive"));
        recording->setTheme("", "");
        recording->setIconShow(false);
    }

    long double num = (long double)totalBytes / (1024.0l * 1024.0l);
    const char *unit = "MiB";
    if (num > 1024) {
        num /= 1024;
        unit = "GiB";
    }
    megabytesSent->setText(QString("%1 %2").arg((double)num, 0, 'f', 1).arg(unit));

    num = kbps;
    unit = "kb/s";
    if (num >= 10'000) {
        num /= 1000;
        unit = "Mb/s";
    }
    bitrate->setText(QString("%1 %2").arg((double)num, 0, 'f', 0).arg(unit));

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
    droppedFrames->setText(dropFramesStr);

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
    auto output = filter->streamOutput ? filter->streamOutput : filter->recordingOutput;
    if (!output) {
        return;
    }

    first_total = obs_output_get_total_frames(output);
    first_dropped = obs_output_get_frames_dropped(output);
    droppedFrames->setText(QString("0 / 0 (0)"));
    megabytesSent->setText(QString("0 MiB"));
    bitrate->setText(QString("0 kb/s"));
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

    connect(visibilityCheckbox, &QCheckBox::clicked, [source](bool visible) {
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
    auto item = (FilterCell *)data;
    item->setText(calldata_string(cd, "new_name"));
}

void FilterCell::onVisibilityChanged(void *data, calldata_t *cd)
{
    auto item = (FilterCell *)data;
    auto enabled = calldata_bool(cd, "enabled");
    item->visibilityCheckbox->setChecked(enabled);
}

//--- ParentCell class ---//

ParentCell::ParentCell(const QString &text, obs_source_t *_source, QWidget *parent) : QLabel(parent), source(_source)
{
    parentRenamedSignal.Connect(obs_source_get_signal_handler(source), "rename", ParentCell::onParentRenamed, this);
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
    auto item = (ParentCell *)data;
    auto newName = calldata_string(cd, "new_name");
    item->setSourceName(newName);
}

void ParentCell::setSourceName(const QString &text)
{
    setText(QString("<u>%1</u>").arg(text));
}

void ParentCell::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        obs_log(LOG_DEBUG, "uuid=%s", obs_source_get_uuid(source));
        obs_frontend_open_source_filters(source);
    }
}

//--- StatusCell class ---//

StatusCell::StatusCell(const QString &text, QWidget *parent) : QWidget(parent)
{
    icon = new QLabel(this);
    statusText = new QLabel(text, this);

    icon->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    icon->setVisible(false);

    auto layout = new QHBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(icon);
    layout->addWidget(statusText);
    setLayout(layout);
}

StatusCell::~StatusCell() {}
