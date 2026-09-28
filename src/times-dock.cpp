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

#include "times-dock.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

enum SessionColumn {
	ColSource,
	ColStatus,
	ColStarted,
	ColEnded,
	ColPauses,
	ColLastPause,
	ColLastResume,
	ColRecorded,
	SessionColumnCount
};

enum LogColumn { LogTime, LogSource, LogEvent, LogPosition, LogColumnCount };

const QString kDash = QStringLiteral("\u2014");

QTableWidget *makeTable(const QStringList &headers)
{
	auto *table = new QTableWidget();
	table->setColumnCount(headers.size());
	table->setHorizontalHeaderLabels(headers);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setAlternatingRowColors(true);
	table->verticalHeader()->hide();
	table->horizontalHeader()->setStretchLastSection(true);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->setWordWrap(false);
	return table;
}

void setCell(QTableWidget *table, int row, int col, const QString &text, const QString &tip = QString())
{
	QTableWidgetItem *item = table->item(row, col);
	if (!item) {
		item = new QTableWidgetItem();
		table->setItem(row, col, item);
	}
	item->setText(text);
	item->setToolTip(tip);
}

} // namespace

TimesDock::TimesDock(QWidget *parent) : QWidget(parent)
{
	sourceRecordFound_ = RecordMonitor::sourceRecordInstalled();

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	layout->addWidget(status_);

	sessionsTable_ = makeTable({tr("Source"), tr("Status"), tr("Started"), tr("Ended"), tr("Pauses"), tr("Last pause"),
				    tr("Last resume"), tr("Recorded")});
	logTable_ = makeTable({tr("Time"), tr("Source"), tr("Event"), tr("Position in file")});

	auto *splitter = new QSplitter(Qt::Vertical, this);
	splitter->addWidget(sessionsTable_);
	splitter->addWidget(logTable_);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 2);
	layout->addWidget(splitter, 1);

	auto *buttons = new QHBoxLayout();
	auto *copyButton = new QPushButton(tr("Copy log"), this);
	auto *saveButton = new QPushButton(tr("Save log as CSV..."), this);
	auto *clearButton = new QPushButton(tr("Clear"), this);
	copyButton->setToolTip(tr("Copy the event log to the clipboard (tab separated)"));
	saveButton->setToolTip(tr("Save the event log to a CSV file"));
	clearButton->setToolTip(tr("Clear finished recordings and the event log"));
	buttons->addWidget(copyButton);
	buttons->addWidget(saveButton);
	buttons->addStretch(1);
	buttons->addWidget(clearButton);
	layout->addLayout(buttons);

	connect(copyButton, &QPushButton::clicked, this, &TimesDock::copyLog);
	connect(saveButton, &QPushButton::clicked, this, &TimesDock::saveCsv);
	connect(clearButton, &QPushButton::clicked, this, &TimesDock::clearAll);

	monitor_ = new RecordMonitor(this);
	connect(monitor_, &RecordMonitor::recordEvent, this, &TimesDock::onRecordEvent);

	tickTimer_.setInterval(1000);
	connect(&tickTimer_, &QTimer::timeout, this, &TimesDock::tick);
	tickTimer_.start();

	refreshHeaderStatus();
}

TimesDock::~TimesDock()
{
	shutdown();
}

void TimesDock::shutdown()
{
	tickTimer_.stop();
	if (monitor_)
		monitor_->shutdown();
}

// ---- formatting ---------------------------------------------------------------------------

QString TimesDock::fmtTime(const QDateTime &t)
{
	if (!t.isValid())
		return kDash;
	return t.date() == QDate::currentDate() ? t.toString(QStringLiteral("HH:mm:ss"))
						: t.toString(QStringLiteral("MMM d HH:mm:ss"));
}

QString TimesDock::fmtFull(const QDateTime &t)
{
	return t.isValid() ? t.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")) : QString();
}

QString TimesDock::fmtDuration(qint64 ms)
{
	if (ms < 0)
		ms = 0;
	const qint64 total = ms / 1000;
	const qint64 h = total / 3600;
	const qint64 m = (total / 60) % 60;
	const qint64 s = total % 60;
	return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

qint64 TimesDock::recordedNow(const Session &s, qint64 nowMs) const
{
	const bool running = s.active && !s.paused;
	return s.recordedMs + (running ? nowMs - s.runStartMs : 0);
}

// ---- table updates ------------------------------------------------------------------------

void TimesDock::updateSessionRow(int index)
{
	const Session &s = sessions_[index];
	const QString approx = s.approxStart ? QStringLiteral("~") : QString();

	QString status;
	if (s.active)
		status = s.paused ? tr("Paused") : tr("Recording");
	else if (s.stopCode != 0)
		status = tr("Stopped (error %1)").arg(s.stopCode);
	else
		status = tr("Stopped");

	setCell(sessionsTable_, index, ColSource, s.source);
	setCell(sessionsTable_, index, ColStatus, status);
	setCell(sessionsTable_, index, ColStarted, approx + fmtTime(s.started), fmtFull(s.started));
	setCell(sessionsTable_, index, ColEnded, fmtTime(s.ended), fmtFull(s.ended));
	setCell(sessionsTable_, index, ColPauses, QString::number(s.pauses));
	setCell(sessionsTable_, index, ColLastPause, fmtTime(s.lastPause), fmtFull(s.lastPause));
	setCell(sessionsTable_, index, ColLastResume, fmtTime(s.lastResume), fmtFull(s.lastResume));
	setCell(sessionsTable_, index, ColRecorded,
		fmtDuration(recordedNow(s, QDateTime::currentMSecsSinceEpoch())),
		tr("Time actually written to the file (pauses are not counted)"));
}

void TimesDock::setLogRow(int row)
{
	const LogEntry &e = log_[row];
	setCell(logTable_, row, LogTime, (e.approximate ? QStringLiteral("~") : QString()) + fmtTime(e.time),
		fmtFull(e.time));
	setCell(logTable_, row, LogSource, e.source);
	setCell(logTable_, row, LogEvent, e.event);
	setCell(logTable_, row, LogPosition, fmtDuration(e.positionMs),
		tr("How far into the recorded file this happened (pauses are cut out of the file)"));
}

int TimesDock::addLog(const Session &s, const QString &event, const QDateTime &t, qint64 positionMs, bool approx)
{
	LogEntry entry;
	entry.time = t;
	entry.source = s.source;
	entry.event = event;
	entry.positionMs = positionMs;
	entry.approximate = approx;
	log_.push_back(entry);

	const int row = log_.size() - 1;
	logTable_->insertRow(row);
	setLogRow(row);
	logTable_->scrollToBottom();
	return row;
}

void TimesDock::refreshHeaderStatus()
{
	if (!sourceRecordFound_) {
		status_->setText(tr("<b>Source Record plugin not found.</b> Install it and restart OBS; "
				    "this dock only tracks recordings made by its filter."));
		return;
	}
	const int n = activeSession_.size();
	if (n == 0)
		status_->setText(tr("Waiting for a Source Record recording..."));
	else
		status_->setText(tr("%n Source Record recording(s) active", nullptr, n));
}

// ---- events -------------------------------------------------------------------------------

void TimesDock::onRecordEvent(const RecEventInfo &e)
{
	const qint64 nowMs = e.time.toMSecsSinceEpoch();
	int idx = activeSession_.value(e.key, -1);

	auto begin = [&](bool approx) {
		Session s;
		s.key = e.key;
		s.source = e.source;
		s.started = e.time;
		s.runStartMs = nowMs;
		s.active = true;
		s.approxStart = approx;
		sessions_.push_back(s);
		idx = sessions_.size() - 1;
		activeSession_.insert(e.key, idx);
		sessionsTable_->insertRow(idx);
		sessions_[idx].startLogRow = addLog(sessions_[idx], tr("Started"), e.time, 0, approx);
		sessionsTable_->scrollToBottom();
	};

	switch (e.type) {
	case RecEventType::Started: {
		if (idx >= 0) {
			// We had already noticed this recording by polling; replace the estimate with the real time.
			Session &s = sessions_[idx];
			if (s.approxStart && !e.approximate && s.pauses == 0 && s.recordedMs == 0) {
				s.started = e.time;
				s.runStartMs = nowMs;
				s.approxStart = false;
				if (s.startLogRow >= 0) {
					log_[s.startLogRow].time = e.time;
					log_[s.startLogRow].approximate = false;
					setLogRow(s.startLogRow);
				}
			}
		} else {
			begin(e.approximate);
		}
		break;
	}
	case RecEventType::Paused: {
		if (idx < 0)
			begin(true);
		Session &s = sessions_[idx];
		if (s.paused)
			break;
		s.recordedMs += nowMs - s.runStartMs;
		s.paused = true;
		s.pauses++;
		s.lastPause = e.time;
		addLog(s, tr("Paused"), e.time, s.recordedMs, e.approximate);
		break;
	}
	case RecEventType::Resumed: {
		if (idx < 0)
			begin(true);
		Session &s = sessions_[idx];
		if (!s.paused)
			break;
		s.paused = false;
		s.runStartMs = nowMs;
		s.lastResume = e.time;
		addLog(s, tr("Resumed"), e.time, s.recordedMs, e.approximate);
		break;
	}
	case RecEventType::Stopped: {
		if (idx < 0)
			break;
		Session &s = sessions_[idx];
		if (!s.paused)
			s.recordedMs += nowMs - s.runStartMs;
		s.active = false;
		s.paused = false;
		s.ended = e.time;
		s.stopCode = e.stopCode;
		activeSession_.remove(e.key);
		QString label = tr("Stopped");
		if (e.stopCode != 0)
			label = tr("Stopped (error %1)").arg(e.stopCode);
		addLog(s, label, e.time, s.recordedMs, e.approximate);
		break;
	}
	}

	if (idx >= 0)
		updateSessionRow(idx);
	sessionsTable_->resizeColumnsToContents();
	refreshHeaderStatus();
}

void TimesDock::tick()
{
	// Keep the "Recorded" column of running sessions ticking.
	for (auto it = activeSession_.constBegin(); it != activeSession_.constEnd(); ++it)
		updateSessionRow(it.value());
}

// ---- buttons ------------------------------------------------------------------------------

void TimesDock::clearAll()
{
	// Keep sessions that are still recording; drop everything else.
	QVector<Session> keep;
	for (const Session &s : sessions_)
		if (s.active)
			keep.push_back(s);

	sessions_ = keep;
	activeSession_.clear();
	log_.clear();
	logTable_->setRowCount(0);
	sessionsTable_->setRowCount(0);

	for (int i = 0; i < sessions_.size(); i++) {
		sessions_[i].startLogRow = -1;
		activeSession_.insert(sessions_[i].key, i);
		sessionsTable_->insertRow(i);
		updateSessionRow(i);
	}
	refreshHeaderStatus();
}

QString TimesDock::logAsText(QChar separator, bool fullTimestamps) const
{
	auto field = [&](QString text) {
		if (separator == QLatin1Char(',')) {
			text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
			return QStringLiteral("\"%1\"").arg(text);
		}
		return text;
	};

	QString out;
	QTextStream ts(&out);
	ts << field(tr("Time")) << separator << field(tr("Source")) << separator << field(tr("Event")) << separator
	   << field(tr("Position in file")) << "\n";
	for (const LogEntry &e : log_) {
		const QString time = fullTimestamps ? fmtFull(e.time) : fmtTime(e.time);
		ts << field((e.approximate ? QStringLiteral("~") : QString()) + time) << separator << field(e.source)
		   << separator << field(e.event) << separator << field(fmtDuration(e.positionMs)) << "\n";
	}
	return out;
}

void TimesDock::copyLog()
{
	QApplication::clipboard()->setText(logAsText(QLatin1Char('\t'), true));
}

void TimesDock::saveCsv()
{
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	const QString name = QStringLiteral("source-record-times-%1.csv")
				     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
	const QString path =
		QFileDialog::getSaveFileName(this, tr("Save log as CSV"), QDir(dir).filePath(name), tr("CSV files (*.csv)"));
	if (path.isEmpty())
		return;

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
		return;
	file.write(logAsText(QLatin1Char(','), true).toUtf8());
}
