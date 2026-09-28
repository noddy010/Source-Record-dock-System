#pragma once

#include "record-monitor.hpp"

#include <QDateTime>
#include <QHash>
#include <QTimer>
#include <QVector>
#include <QWidget>

class StatusPanel;

class TimesDock : public QWidget {
	Q_OBJECT

public:
	explicit TimesDock(QWidget *parent = nullptr);
	~TimesDock() override;

	void shutdown();

private slots:
	void onRecordEvent(const RecEventInfo &e);
	void onSourcesChanged(const QStringList &names);
	void tick();

private:
	struct Session {
		quint64 key = 0;
		QString source;
		QDateTime started;
		QDateTime ended;
		QDateTime lastPause;
		QDateTime lastResume;
		QDateTime prevLastResume;
		qint64 recordedMs = 0;
		qint64 runStartMs = 0;
		int pauses = 0;
		int stopCode = 0;
		bool active = false;
		bool paused = false;
		bool approxStart = false;
		QString filePath;
		quint64 totalBytes = 0;
		int framesDropped = 0;
		int totalFrames = 0;
		quint64 lastStatsBytes = 0;
		qint64 lastStatsMs = 0;
		double bitrateKbps = 0.0;
	};

	qint64 recordedNow(const Session &s, qint64 nowMs) const;
	int latestSessionIndex() const;
	void updatePanel();

	static QString fmtDuration(qint64 ms);
	static QString fmtFileSize(qint64 bytes);

	RecordMonitor *monitor_ = nullptr;
	StatusPanel *panel_ = nullptr;
	QTimer tickTimer_;

	QVector<Session> sessions_;
	QHash<quint64, int> activeSession_;
	int displaySession_ = -1;
	bool sourceRecordFound_ = false;
	void *cpuInfo_ = nullptr;
	double cpuPercent_ = 0.0;
};
