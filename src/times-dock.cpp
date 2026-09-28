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
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

// ---- one card per source ------------------------------------------------------------------

class RecordCard : public QFrame {
public:
	enum class State { Idle, Recording, Paused, Stopped };

	explicit RecordCard(const QString &name, QWidget *parent = nullptr) : QFrame(parent)
	{
		setObjectName(QStringLiteral("recordCard"));
		setStyleSheet(QStringLiteral("QFrame#recordCard { background: #202020; border: 1px solid #2e2e2e;"
					     " border-radius: 16px; }"
					     "QLabel { background: transparent; }"));

		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(14, 14, 14, 12);
		layout->setSpacing(6);

		title_ = new QLabel(name.toUpper(), this);
		QFont tf = title_->font();
		tf.setBold(true);
		tf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
		title_->setFont(tf);
		title_->setAlignment(Qt::AlignCenter);
		title_->setStyleSheet(QStringLiteral("color: #b5b5b5; background: transparent;"));
		title_->setToolTip(name);
		layout->addWidget(title_);

		auto *row = new QHBoxLayout();
		row->setSpacing(8);
		row->addStretch(1);
		dot_ = new QLabel(this);
		dot_->setFixedSize(16, 16);
		row->addWidget(dot_, 0, Qt::AlignVCenter);
		status_ = new QLabel(this);
		QFont sf = status_->font();
		sf.setBold(true);
		sf.setPointSizeF(sf.pointSizeF() * 1.5);
		status_->setFont(sf);
		row->addWidget(status_, 0, Qt::AlignVCenter);
		row->addStretch(1);
		layout->addLayout(row);

		timer_ = new QLabel(QStringLiteral("00:00:00"), this);
		QFont mf = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		mf.setBold(true);
		mf.setPointSizeF(mf.pointSizeF() * 2.2);
		timer_->setFont(mf);
		timer_->setAlignment(Qt::AlignCenter);
		timer_->setStyleSheet(QStringLiteral("color: #ffffff; background: transparent;"));
		layout->addWidget(timer_);

		info_ = new QLabel(this);
		QFont inf = info_->font();
		inf.setPointSizeF(inf.pointSizeF() * 0.9);
		info_->setFont(inf);
		info_->setAlignment(Qt::AlignCenter);
		info_->setWordWrap(true);
		info_->setStyleSheet(QStringLiteral("color: #8f8f8f; background: transparent;"));
		layout->addWidget(info_);

		setState(State::Idle, QStringLiteral("00:00:00"), QString(), 0);
	}

	void setState(State st, const QString &timerText, const QString &info, int blinkPhase)
	{
		timer_->setText(timerText);
		info_->setText(info);
		info_->setVisible(!info.isEmpty());

		const int key = static_cast<int>(st) * 2 + (st == State::Recording ? blinkPhase : 0);
		if (key == styleKey_)
			return;
		styleKey_ = key;

		QString light, dark, text, label;
		switch (st) {
		case State::Recording:
			light = blinkPhase ? QStringLiteral("#8a3535") : QStringLiteral("#ff9a9a");
			dark = blinkPhase ? QStringLiteral("#4a1414") : QStringLiteral("#d31f1f");
			text = QStringLiteral("#ff5c5c");
			label = tr("RECORDING");
			break;
		case State::Paused:
			light = QStringLiteral("#ffd98a");
			dark = QStringLiteral("#c98a0a");
			text = QStringLiteral("#ffb84d");
			label = tr("PAUSED");
			break;
		default:
			light = QStringLiteral("#7d7690");
			dark = QStringLiteral("#2b2735");
			text = QStringLiteral("#8c8c8c");
			label = tr("STOPPED");
			break;
		}
		dot_->setStyleSheet(QStringLiteral("border-radius: 8px; background: qradialgradient(cx:0.35, cy:0.35,"
						   " radius:0.75, fx:0.35, fy:0.35, stop:0 %1, stop:1 %2);")
					    .arg(light, dark));
		status_->setText(label);
		status_->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(text));
	}

private:
	QLabel *title_ = nullptr;
	QLabel *dot_ = nullptr;
	QLabel *status_ = nullptr;
	QLabel *timer_ = nullptr;
	QLabel *info_ = nullptr;
	int styleKey_ = -1;
};

namespace {

enum LogColumn { LogTime, LogSource, LogEvent, LogPosition, LogColumnCount };

QString pausesText(int n)
{
	return n == 1 ? QStringLiteral("1 pause") : QStringLiteral("%1 pauses").arg(n);
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

	// Cards (top) ...
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setMinimumHeight(120);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	cardsHost_ = new QWidget();
	cardsLayout_ = new QVBoxLayout(cardsHost_);
	cardsLayout_->setContentsMargins(0, 0, 0, 0);
	cardsLayout_->setSpacing(8);
	cardsLayout_->setSizeConstraint(QLayout::SetMinimumSize); // scroll instead of squashing the cards
	cardsLayout_->addStretch(1);
	scroll->setWidget(cardsHost_);

	// ... and the event log (bottom).
	logTable_ = new QTableWidget();
	logTable_->setColumnCount(LogColumnCount);
	logTable_->setHorizontalHeaderLabels({tr("Time"), tr("Source"), tr("Event"), tr("Position in file")});
	logTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	logTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
	logTable_->setAlternatingRowColors(true);
	logTable_->verticalHeader()->hide();
	logTable_->horizontalHeader()->setStretchLastSection(true);
	logTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	logTable_->setWordWrap(false);

	auto *splitter = new QSplitter(Qt::Vertical, this);
	splitter->addWidget(scroll);
	splitter->addWidget(logTable_);
	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);
	splitter->setSizes({330, 220});
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
	connect(monitor_, &RecordMonitor::sourcesChanged, this, &TimesDock::onSourcesChanged);

	tickTimer_.setInterval(250);
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
		return QStringLiteral("\u2014");
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
	return QStringLiteral("%1:%2:%3")
		.arg(total / 3600, 2, 10, QLatin1Char('0'))
		.arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(total % 60, 2, 10, QLatin1Char('0'));
}

qint64 TimesDock::recordedNow(const Session &s, qint64 nowMs) const
{
	const bool running = s.active && !s.paused;
	return s.recordedMs + (running ? nowMs - s.runStartMs : 0);
}

// ---- cards --------------------------------------------------------------------------------

RecordCard *TimesDock::ensureCard(const QString &label)
{
	if (RecordCard *existing = cards_.value(label, nullptr))
		return existing;

	auto *card = new RecordCard(label, cardsHost_);
	cards_.insert(label, card);
	const int pos = static_cast<int>(std::distance(cards_.begin(), cards_.find(label)));
	cardsLayout_->insertWidget(pos, card);
	updateCard(label);
	refreshHeaderStatus();
	return card;
}

void TimesDock::updateCard(const QString &label)
{
	RecordCard *card = cards_.value(label, nullptr);
	if (!card)
		return;

	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	const int blink = static_cast<int>((nowMs / 500) % 2);
	const int idx = latestByLabel_.value(label, -1);
	if (idx < 0 || idx >= sessions_.size()) {
		card->setState(RecordCard::State::Idle, QStringLiteral("00:00:00"), tr("Waiting for a recording"), blink);
		return;
	}

	const Session &s = sessions_[idx];
	const QString approx = s.approxStart ? QStringLiteral("~") : QString();
	const QString timerText = fmtDuration(recordedNow(s, nowMs));
	QString info;
	RecordCard::State st;

	if (s.active && s.paused) {
		st = RecordCard::State::Paused;
		info = tr("Paused at %1 \u00b7 started %2").arg(fmtTime(s.lastPause), approx + fmtTime(s.started));
	} else if (s.active) {
		st = RecordCard::State::Recording;
		info = tr("Started %1").arg(approx + fmtTime(s.started));
		if (s.pauses > 0)
			info += QStringLiteral(" \u00b7 ") + pausesText(s.pauses);
	} else {
		st = RecordCard::State::Stopped;
		info = QStringLiteral("%1 \u2192 %2").arg(approx + fmtTime(s.started), fmtTime(s.ended));
		if (s.pauses > 0)
			info += QStringLiteral(" \u00b7 ") + pausesText(s.pauses);
		if (s.stopCode != 0)
			info += tr(" \u00b7 error %1").arg(s.stopCode);
	}
	card->setState(st, timerText, info, blink);
}

void TimesDock::updateAllCards()
{
	for (auto it = cards_.constBegin(); it != cards_.constEnd(); ++it)
		updateCard(it.key());
}

void TimesDock::onSourcesChanged(const QStringList &names)
{
	currentNames_ = names;
	for (const QString &name : names)
		ensureCard(name);

	// Drop cards for sources that lost their filter, unless they still show a recording.
	const QStringList labels = cards_.keys();
	for (const QString &label : labels) {
		if (names.contains(label) || latestByLabel_.contains(label))
			continue;
		RecordCard *card = cards_.take(label);
		cardsLayout_->removeWidget(card);
		card->deleteLater();
	}
	refreshHeaderStatus();
}

void TimesDock::refreshHeaderStatus()
{
	if (!sourceRecordFound_) {
		status_->setText(tr("<b>Source Record plugin not found.</b> Install it and restart OBS; "
				    "this dock only tracks recordings made by its filter."));
		status_->show();
	} else if (cards_.isEmpty()) {
		status_->setText(tr("No Source Record filter found yet. Add the \"Source Record\" filter to a source "
				    "and it will show up here."));
		status_->show();
	} else {
		status_->hide();
	}
}

// ---- log ----------------------------------------------------------------------------------

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

// ---- events -------------------------------------------------------------------------------

void TimesDock::onRecordEvent(const RecEventInfo &e)
{
	const qint64 nowMs = e.time.toMSecsSinceEpoch();
	int idx = activeSession_.value(e.key, -1);
	ensureCard(e.source);

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
		latestByLabel_.insert(e.source, idx);
		sessions_[idx].startLogRow = addLog(sessions_[idx], tr("Started"), e.time, 0, approx);
	};

	switch (e.type) {
	case RecEventType::Started: {
		if (idx >= 0) {
			// Already noticed by polling; replace the estimate with the real time.
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
		s.prevLastResume = s.lastResume;
		s.lastResume = e.time;
		s.lastResumeLogRow = addLog(s, tr("Resumed"), e.time, s.recordedMs, e.approximate);
		break;
	}
	case RecEventType::Stopped: {
		if (idx < 0)
			break;
		Session &s = sessions_[idx];

		// When a recording is stopped while paused, OBS un-pauses it a split second before it stops.
		// That "Resumed" is not a real one, so take it back out of the log.
		bool whilePaused = s.paused;
		if (!s.paused && s.lastResumeLogRow >= 0 && s.lastResumeLogRow == log_.size() - 1 &&
		    s.lastResume.isValid() && s.lastResume.msecsTo(e.time) < 500) {
			log_.removeLast();
			logTable_->removeRow(log_.size());
			s.lastResume = s.prevLastResume;
			s.lastResumeLogRow = -1;
			s.paused = true;
			whilePaused = true;
		}

		if (!s.paused)
			s.recordedMs += nowMs - s.runStartMs;
		s.active = false;
		s.paused = false;
		s.ended = e.time;
		s.stopCode = e.stopCode;
		activeSession_.remove(e.key);

		QString label = whilePaused ? tr("Stopped (while paused)") : tr("Stopped");
		if (e.stopCode != 0)
			label = tr("Stopped (error %1)").arg(e.stopCode);
		addLog(s, label, e.time, s.recordedMs, e.approximate);
		break;
	}
	}

	logTable_->resizeColumnsToContents();
	updateCard(e.source);
}

void TimesDock::tick()
{
	// Keep the timers of running sessions ticking (and the recording dot blinking).
	for (auto it = latestByLabel_.constBegin(); it != latestByLabel_.constEnd(); ++it) {
		const int idx = it.value();
		if (idx >= 0 && idx < sessions_.size() && sessions_[idx].active)
			updateCard(it.key());
	}
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
	latestByLabel_.clear();
	log_.clear();
	logTable_->setRowCount(0);

	for (int i = 0; i < sessions_.size(); i++) {
		sessions_[i].startLogRow = -1;
		sessions_[i].lastResumeLogRow = -1;
		activeSession_.insert(sessions_[i].key, i);
		latestByLabel_.insert(sessions_[i].source, i);
	}

	onSourcesChanged(currentNames_);
	updateAllCards();
}

QString TimesDock::logAsText(QChar separator) const
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
		ts << field((e.approximate ? QStringLiteral("~") : QString()) + fmtFull(e.time)) << separator
		   << field(e.source) << separator << field(e.event) << separator << field(fmtDuration(e.positionMs))
		   << "\n";
	}
	return out;
}

void TimesDock::copyLog()
{
	QApplication::clipboard()->setText(logAsText(QLatin1Char('\t')));
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
	file.write(logAsText(QLatin1Char(',')).toUtf8());
}
