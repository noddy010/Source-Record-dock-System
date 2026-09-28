/*
Source Record Times
Copyright (C) 2026

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

#include "record-monitor.hpp"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QLabel;
class QTableWidget;

class TimesDock : public QWidget {
	Q_OBJECT

public:
	explicit TimesDock(QWidget *parent = nullptr);
	~TimesDock() override;

	void shutdown();

private slots:
	void onRecordEvent(const RecEventInfo &e);
	void tick();
	void clearAll();
	void copyLog();
	void saveCsv();

private:
	struct Session {
		quint64 key = 0;
		QString source;
		QDateTime started;
		QDateTime ended;
		QDateTime lastPause;
		QDateTime lastResume;
		qint64 recordedMs = 0; // net recorded time, pauses excluded, up to the last pause/stop
		qint64 runStartMs = 0; // when the current running stretch began
		int pauses = 0;
		int stopCode = 0;
		int startLogRow = -1;
		bool active = false;
		bool paused = false;
		bool approxStart = false;
	};
	struct LogEntry {
		QDateTime time;
		QString source;
		QString event;
		qint64 positionMs = 0;
		bool approximate = false;
	};

	qint64 recordedNow(const Session &s, qint64 nowMs) const;
	void updateSessionRow(int index);
	void setLogRow(int row);
	int addLog(const Session &s, const QString &event, const QDateTime &t, qint64 positionMs, bool approx);
	void refreshHeaderStatus();
	QString logAsText(QChar separator, bool fullTimestamps) const;

	static QString fmtTime(const QDateTime &t);
	static QString fmtFull(const QDateTime &t);
	static QString fmtDuration(qint64 ms);

	RecordMonitor *monitor_ = nullptr;
	QLabel *status_ = nullptr;
	QTableWidget *sessionsTable_ = nullptr;
	QTableWidget *logTable_ = nullptr;
	QTimer tickTimer_;

	QVector<Session> sessions_;
	QVector<LogEntry> log_;
	QHash<quint64, int> activeSession_; // output key -> index into sessions_
	bool sourceRecordFound_ = false;
};
