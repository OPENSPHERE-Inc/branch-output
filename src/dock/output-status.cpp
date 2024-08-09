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
#include <plugin-main.hpp>
#include "output-status.hpp"

#define TIMER_INTERVAL 2000

// FIXME: Duplicated definition error with util/base.h
extern "C" {
    extern void obs_log(int log_level, const char *format, ...);
}

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

BranchOutputStatus::BranchOutputStatus(QWidget *parent) : QWidget(parent), timer(this)
{
    outputLayout = new QGridLayout();

    QObject::connect(&timer, &QTimer::timeout, this, &BranchOutputStatus::Update);

    timer.setInterval(TIMER_INTERVAL);
    if (isVisible()) {
        timer.start();
    }

    this->setLayout(outputLayout);
}

BranchOutputStatus::~BranchOutputStatus()
{

}

void BranchOutputStatus::AddOutputLabels(QString name, obs_output_t* output)
{
  	OutputLabels ol;
    ol.output = output;
	ol.name = new QLabel(name, this);
	ol.status = new QLabel(this);
	ol.droppedFrames = new QLabel(this);
	ol.megabytesSent = new QLabel(this);
	ol.bitrate = new QLabel(this);

	int col = 0;
	int row = outputLabels.size() + 1;
	outputLayout->addWidget(ol.name, row, col++);
	outputLayout->addWidget(ol.status, row, col++);
	outputLayout->addWidget(ol.droppedFrames, row, col++);
	outputLayout->addWidget(ol.megabytesSent, row, col++);
	outputLayout->addWidget(ol.bitrate, row, col++);
	outputLabels.push_back(ol);
}

void BranchOutputStatus::RemoveOutputLabels(obs_output_t* output)
{
    for (int i = 0; i < outputLabels.size(); i++) {
        const auto ol = outputLabels.at(i);
        if (ol.output == output) {
            outputLabels.removeAt(i);
            break;
        }
    }
}

void BranchOutputStatus::Update()
{

}


void BranchOutputStatus::showEvent(QShowEvent *)
{
	timer.start(TIMER_INTERVAL);
}

void BranchOutputStatus::hideEvent(QHideEvent *)
{
	timer.stop();
}

void BranchOutputStatus::OutputLabels::Update(bool rec)
{
	uint64_t totalBytes = output ? obs_output_get_total_bytes(output) : 0;
	uint64_t curTime = os_gettime_ns();
	uint64_t bytesSent = totalBytes;

	if (bytesSent < lastBytesSent)
		bytesSent = 0;
	if (bytesSent == 0)
		lastBytesSent = 0;

	uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;
	long double timePassed =
		(long double)(curTime - lastBytesSentTime) / 1000000000.0l;
	kbps = (long double)bitsBetween / timePassed / 1000.0l;

	if (timePassed < 0.01l)
		kbps = 0.0l;

	QString str = QTStr("Basic.Stats.Status.Inactive");
	QString themeID;
	bool active = output ? obs_output_active(output) : false;
	if (rec) {
		if (active)
			str = QTStr("Basic.Stats.Status.Recording");
	} else {
		if (active) {
			bool reconnecting =
				output ? obs_output_reconnecting(output)
				       : false;

			if (reconnecting) {
				str = QTStr("Basic.Stats.Status.Reconnecting");
				themeID = "error";
			} else {
				str = QTStr("Basic.Stats.Status.Live");
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
	megabytesSent->setText(QString("%1 %2").arg(num, 0, 'f', 1).arg(unit));

	num = kbps;
	unit = "kb/s";
	if (num >= 10'000) {
		num /= 1000;
		unit = "Mb/s";
	}
	bitrate->setText(QString("%1 %2").arg(num, 0, 'f', 0).arg(unit));

	if (!rec) {
		int total = output ? obs_output_get_total_frames(output) : 0;
		int dropped = output ? obs_output_get_frames_dropped(output)
				     : 0;

		if (total < first_total || dropped < first_dropped) {
			first_total = 0;
			first_dropped = 0;
		}

		total -= first_total;
		dropped -= first_dropped;

		num = total ? (long double)dropped / (long double)total * 100.0l
			    : 0.0l;

		str = QString("%1 / %2 (%3%)")
			      .arg(QString::number(dropped),
				   QString::number(total),
				   QString::number(num, 'f', 1));
		droppedFrames->setText(str);

		if (num > 5.0l)
			setThemeID(droppedFrames, "error");
		else if (num > 1.0l)
			setThemeID(droppedFrames, "warning");
		else
			setThemeID(droppedFrames, "");
	}

	lastBytesSent = bytesSent;
	lastBytesSentTime = curTime;
}

void BranchOutputStatus::OutputLabels::Reset()
{
	if (!output)
		return;

	first_total = obs_output_get_total_frames(output);
	first_dropped = obs_output_get_frames_dropped(output);
}