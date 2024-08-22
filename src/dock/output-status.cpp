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
#include <plugin-main.hpp>
#include "output-status.hpp"

#define TIMER_INTERVAL 2000

// FIXME: Duplicated definition error with util/base.h
extern "C" {
extern void obs_log(int log_level, const char *format, ...);
}

BranchOutputStatus::BranchOutputStatus(QWidget *parent) : QFrame(parent), timer(this)
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
    outputTable->setColumnCount(7);

    int col = 0;
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("FilterName")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("SourceName")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("Status")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("DropFrames")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("SentDataSize")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QTStr("BitRate")));
    outputTable->setHorizontalHeaderItem(col++, new QTableWidgetItem(QString::fromUtf8("")));

    QObject::connect(&timer, &QTimer::timeout, this, &BranchOutputStatus::Update);

    timer.setInterval(TIMER_INTERVAL);
    if (isVisible()) {
        timer.start();
    }

    // Tool buttons
    auto enableAllButton = new QPushButton(QTStr("EnableAll"));
    connect(enableAllButton, &QPushButton::clicked, [this]() { SetEabnleAll(true); });

    auto disableAllButton = new QPushButton(QTStr("DisableAll"));
    connect(disableAllButton, &QPushButton::clicked, [this]() { SetEabnleAll(false); });

    auto buttonsContainerLayout = new QHBoxLayout();
    buttonsContainerLayout->addWidget(enableAllButton);
    buttonsContainerLayout->addWidget(disableAllButton);
    buttonsContainerLayout->addStretch();

    QVBoxLayout *outputContainerLayout = new QVBoxLayout();
    outputContainerLayout->addWidget(outputTable);
    outputContainerLayout->addLayout(buttonsContainerLayout);
    this->setLayout(outputContainerLayout);
}

BranchOutputStatus::~BranchOutputStatus() {}

void BranchOutputStatus::AddFilter(filter_t *filter)
{
    auto parent = obs_filter_get_parent(filter->source);
    auto row = (int)outputLabels.size();

    OutputLabels ol;

    ol.filter = filter;
    ol.filterCell = new FilterCell(QString::fromUtf8(obs_source_get_name(filter->source)), filter->source, this);
    ol.parentCell = new ParentCell(QString::fromUtf8(obs_source_get_name(parent)), parent, this);
    ol.status = new QLabel(QTStr("Status.Inactive"), this);
    ol.droppedFrames = new QLabel(QString::fromUtf8(""), this);
    ol.megabytesSent = new QLabel(QString::fromUtf8(""), this);
    ol.bitrate = new QLabel(QString::fromUtf8(""), this);

    outputLabels.push_back(ol);

    auto col = 0;
    outputTable->setRowCount(row + 1);
    outputTable->setCellWidget(row, col++, ol.filterCell);
    outputTable->setCellWidget(row, col++, ol.parentCell);
    outputTable->setCellWidget(row, col++, ol.status);
    outputTable->setCellWidget(row, col++, ol.droppedFrames);
    outputTable->setCellWidget(row, col++, ol.megabytesSent);
    outputTable->setCellWidget(row, col++, ol.bitrate);

    outputTable->setRowHeight(row, 32);

    // Setup reset button
    auto resetButtonContainer = new QWidget(this);
    auto resetButtonContainerLayout = new QHBoxLayout();
    resetButtonContainerLayout->setContentsMargins(0, 0, 0, 0);
    resetButtonContainer->setLayout(resetButtonContainerLayout);

    auto resetButton = new QPushButton(QTStr("Reset"), this);
    connect(resetButton, &QPushButton::clicked, [this, row]() { outputLabels[row].Reset(); });
    resetButton->setProperty("toolButton", true);
    resetButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    resetButtonContainerLayout->addWidget(resetButton);
    outputTable->setCellWidget(row, col, resetButtonContainer);
}

void BranchOutputStatus::RemoveFilter(filter_t *filter)
{
    for (int i = 0; i < outputLabels.size(); i++) {
        if (outputLabels[i].filter == filter) {
            outputLabels.removeAt(i);
            outputTable->removeRow(i);
            break;
        }
    }
}

void BranchOutputStatus::Update()
{
    for (int i = 0; i < outputLabels.size(); i++) {
        outputLabels[i].Update(false);
    }
}

void BranchOutputStatus::showEvent(QShowEvent *)
{
    timer.start(TIMER_INTERVAL);
}

void BranchOutputStatus::hideEvent(QHideEvent *)
{
    timer.stop();
}

void BranchOutputStatus::SetEabnleAll(bool enabled)
{
    for (int i = 0; i < outputLabels.size(); i++) {
        obs_source_set_enabled(outputLabels[i].filter->source, enabled);
    }
}

// Imitate UI/window-basic-stats.cpp
void setThemeID(QWidget *widget, const QString &themeID)
{
    if (widget->property("themeID").toString() != themeID) {
        widget->setProperty("themeID", themeID);

        /* force style sheet recalculation */
        QString qss = widget->styleSheet();
        widget->setStyleSheet("/* */");
        widget->setStyleSheet(qss);
    }
}

// BranchOutputStatus::OutputLabels structure

// Imitate UI/window-basic-stats.cpp
void BranchOutputStatus::OutputLabels::Update(bool rec)
{
    auto output = filter->stream_output;
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

    QString str = QTStr("Status.Inactive");
    QString themeID;
    bool active = output ? obs_output_active(output) : false;
    if (rec) {
        if (active) {
            str = QTStr("Status.Recording");
        }
    } else {
        if (active) {
            bool reconnecting = output ? obs_output_reconnecting(output) : false;

            if (reconnecting) {
                str = QTStr("Status.Reconnecting");
                themeID = "error";
            } else {
                str = QTStr("Status.Live");
                themeID = "good";
            }
        }
    }

    status->setText(str);
    setThemeID(status, themeID);

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

    if (!rec) {
        int total = output ? obs_output_get_total_frames(output) : 0;
        int dropped = output ? obs_output_get_frames_dropped(output) : 0;

        if (total < first_total || dropped < first_dropped) {
            first_total = 0;
            first_dropped = 0;
        }

        total -= first_total;
        dropped -= first_dropped;

        num = total ? (long double)dropped / (long double)total * 100.0l : 0.0l;

        str = QString("%1 / %2 (%3%)")
                  .arg(QString::number(dropped), QString::number(total), QString::number((double)num, 'f', 1));
        droppedFrames->setText(str);

        if (num > 5.0l) {
            setThemeID(droppedFrames, "error");
        } else if (num > 1.0l) {
            setThemeID(droppedFrames, "warning");
        } else {
            setThemeID(droppedFrames, "");
        }
    }

    lastBytesSent = bytesSent;
    lastBytesSentTime = curTime;
}

void BranchOutputStatus::OutputLabels::Reset()
{
    auto output = filter->stream_output;
    if (!output) {
        return;
    }

    first_total = obs_output_get_total_frames(output);
    first_dropped = obs_output_get_frames_dropped(output);
    droppedFrames->setText(QString("0 / 0 (0)"));
    megabytesSent->setText(QString("0 MiB"));
    bitrate->setText(QString("0 kb/s"));
}

// FilterCell class

FilterCell::FilterCell(QString text, obs_source_t *source, QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(27);

    visibilityCheckbox = new QCheckBox(this);
    visibilityCheckbox->setProperty("visibilityCheckBox", true);
    visibilityCheckbox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    visibilityCheckbox->setChecked(obs_source_enabled(source));

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
    filterRenamedSignal.Connect(obs_source_get_signal_handler(source), "rename", FilterCell::FilterRenamed, this);

    // Listen signal for filter enabled/disabled
    enableSignal.Connect(obs_source_get_signal_handler(source), "enable", FilterCell::VisibilityChanged, this);
}

FilterCell::~FilterCell() {
    filterRenamedSignal.Disconnect();
    enableSignal.Disconnect();
}

void FilterCell::SetText(QString text)
{
    name->setText(text);
}

void FilterCell::FilterRenamed(void *data, calldata_t *cd)
{
    auto item = (FilterCell *)data;
    auto newName = calldata_string(cd, "new_name");
    item->SetText(QString::fromUtf8(newName));
}

void FilterCell::VisibilityChanged(void *data, calldata_t *cd)
{
    auto item = (FilterCell *)data;
    auto enabled = calldata_bool(cd, "enabled");
    item->visibilityCheckbox->setChecked(enabled);
}

// ParentCell class

ParentCell::ParentCell(QString text, obs_source_t *source, QWidget *parent) : QLabel(text, parent)
{
    parentRenamedSignal.Connect(obs_source_get_signal_handler(source), "rename", ParentCell::ParentRenamed, this);
}

ParentCell::~ParentCell() {
    parentRenamedSignal.Disconnect();
}

void ParentCell::ParentRenamed(void *data, calldata_t *cd)
{
    auto item = (ParentCell *)data;
    auto newName = calldata_string(cd, "new_name");
    item->setText(QString::fromUtf8(newName));
}
