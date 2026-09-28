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
	};

	qint64 recordedNow(const Session &s, qint64 nowMs) const;
	int latestSessionIndex() const;
	void updatePanel();

	static QString fmtDuration(qint64 ms);

	RecordMonitor *monitor_ = nullptr;
	StatusPanel *panel_ = nullptr;
	QTimer tickTimer_;

	QVector<Session> sessions_;
	QHash<quint64, int> activeSession_;
	int displaySession_ = -1;
	bool sourceRecordFound_ = false;
};
