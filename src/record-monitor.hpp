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

#include <obs.h>

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <vector>

enum class RecEventType { Started, Paused, Resumed, Stopped, Stats };

struct RecEventInfo {
	quint64 key = 0;          // identifies one Source Record file output
	QString source;           // name of the source (or scene) being recorded
	RecEventType type = RecEventType::Started;
	QDateTime time;           // local wall-clock time the event happened
	int stopCode = 0;         // for Stopped: 0 = clean stop, otherwise an OBS_OUTPUT_* error code
	bool approximate = false; // true when the event was detected by polling instead of a signal
	QString filePath;          // actual output path supplied by Source Record
	quint64 totalBytes = 0;
	int framesDropped = 0;
	int totalFrames = 0;
};

/*
 * Watches the file outputs that the Source Record plugin creates and turns their
 * libobs signals (start / pause / unpause / stop) into Qt signals on the UI thread.
 *
 * Source Record does not expose any events of its own, but every source recording is a
 * regular libobs output (ffmpeg_muxer / mp4_output / mov_output) named after the
 * Source Record filter, so we can hook those outputs directly.
 */
class RecordMonitor : public QObject {
	Q_OBJECT

public:
	explicit RecordMonitor(QObject *parent = nullptr);
	~RecordMonitor() override;

	// Disconnect from libobs. Safe to call more than once.
	void shutdown();

	static bool sourceRecordInstalled();

	// Called (via the UI thread) by the libobs signal callbacks.
	void handleSignal(quint64 key, const QString &outputName, const QString &directory, const QString &format,
			  const QString &filePath, RecEventType type, const QDateTime &time, int code);

signals:
	void recordEvent(const RecEventInfo &info);
	// Names of every source (or scene) that currently has a Source Record filter, sorted.
	void sourcesChanged(const QStringList &names);

private:
	struct Tracked {
		obs_weak_output_t *weak = nullptr;
		QString label;
		bool resolved = false;
	};
	struct FilterInfo {
		QString filterName;
		QString parentName;
		QString path;
		QString format;
	};

	void poll();
	void track(obs_output_t *output, quint64 key);
	void forget(quint64 key, bool emitStop);
	void refreshFilters();
	void publishSources();
	bool resolveLabel(const QString &outputName, const QString &directory, const QString &format, QString &label) const;
	void emitEvent(quint64 key, const QString &label, RecEventType type, const QDateTime &time, int code, bool approx);

	QTimer timer_;
	QHash<quint64, Tracked> tracked_;
	QHash<quint64, bool> sessionOpen_; // per output: did we report a start without a stop yet?
	std::vector<FilterInfo> filters_;
	QStringList lastSources_;
	int pollCount_ = 0;
	bool shutDown_ = false;

};
