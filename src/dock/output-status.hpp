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
#include <QWidget>
#include <QList>
#include <QString>
#include <QPointer>
#include <QLabel>
#include <QTimer>


class QGridLayout;

class BranchOutputStatus : public QWidget {
    Q_OBJECT

	struct OutputLabels {
		obs_output_t* output;
		QPointer<QLabel> name;
		QPointer<QLabel> status;
		QPointer<QLabel> droppedFrames;
		QPointer<QLabel> megabytesSent;
		QPointer<QLabel> bitrate;

		uint64_t lastBytesSent = 0;
		uint64_t lastBytesSentTime = 0;

		int first_total = 0;
		int first_dropped = 0;

		void Update(bool rec);
		void Reset();

		long double kbps = 0.0l;
	};

	QTimer timer;
    QGridLayout *outputLayout = nullptr;
    QList<OutputLabels> outputLabels;

	void Update();

public:
    BranchOutputStatus(QWidget *parent = (QWidget*)nullptr);
    ~BranchOutputStatus();

    void AddOutputLabels(QString name, obs_output_t* output);
	void RemoveOutputLabels(obs_output_t* output);

protected:
	virtual void showEvent(QShowEvent *event) override;
	virtual void hideEvent(QHideEvent *event) override;
};


inline QString QTStr(const char *lookupVal)
{
	return QString::fromUtf8(obs_module_text(lookupVal));
}
