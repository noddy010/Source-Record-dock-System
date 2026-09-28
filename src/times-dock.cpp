#include "times-dock.hpp"

#include <QColor>
#include <QFont>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QRadialGradient>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <obs.h>
#include <util/platform.h>

#include <algorithm>

class StatusPanel final : public QWidget {
public:
	enum class State { Stopped, Recording, Paused };

	explicit StatusPanel(QWidget *parent = nullptr) : QWidget(parent)
	{
		setObjectName(QStringLiteral("cleanRecordingPanel"));
		setMinimumSize(320, 260);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	void setStatus(State state, const QString &timeText, const QString &sizeText, const QString &statsText)
	{
		if (state_ == state && timeText_ == timeText && sizeText_ == sizeText && statsText_ == statsText)
			return;
		state_ = state;
		timeText_ = timeText;
		sizeText_ = sizeText;
		statsText_ = statsText;
		update();
	}

protected:
	void paintEvent(QPaintEvent *event) override
	{
		Q_UNUSED(event);

		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);

		const QRectF outer = rect().adjusted(4, 4, -4, -4);

		QLinearGradient bg(outer.topLeft(), outer.bottomLeft());
		bg.setColorAt(0.0, QColor("#242424"));
		bg.setColorAt(1.0, QColor("#181818"));
		p.setPen(QPen(QColor("#353535"), 1));
		p.setBrush(bg);
		p.drawRoundedRect(outer, 18, 18);

		const qreal cx = width() / 2.0;

		QFont header = font();
		header.setBold(true);
		header.setPointSizeF(std::max(9.0, font().pointSizeF() + 1.0));
		header.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
		p.setFont(header);
		p.setPen(QColor("#a7a7a7"));
		p.drawText(QRectF(outer.left(), outer.top() + 22, outer.width(), 24),
			   Qt::AlignCenter, QStringLiteral("CLEAN RECORDING"));

		const QColor stateColor = colorForState();
		const qreal rowY = outer.top() + 78;

		QRadialGradient glow(QPointF(cx - 48, rowY), 28.0);
		QColor glowColor = stateColor;
		glowColor.setAlpha(85);
		glow.setColorAt(0.0, glowColor);
		glowColor.setAlpha(0);
		glow.setColorAt(1.0, glowColor);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(QPointF(cx - 48, rowY), 28.0, 28.0);

		QRadialGradient dot(QPointF(cx - 51, rowY - 3), 16.0);
		dot.setColorAt(0.0, stateColor.lighter(155));
		dot.setColorAt(0.55, stateColor);
		dot.setColorAt(1.0, stateColor.darker(180));
		p.setBrush(dot);
		p.drawEllipse(QPointF(cx - 48, rowY), 9.0, 9.0);

		QFont stateFont = font();
		stateFont.setBold(true);
		stateFont.setPointSizeF(std::max(16.0, font().pointSizeF() * 1.65));
		p.setFont(stateFont);
		p.setPen(stateColor);
		p.drawText(QRectF(cx - 24, rowY - 18, 170, 36),
			   Qt::AlignVCenter | Qt::AlignLeft, textForState());

		QFont timerFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		timerFont.setBold(true);
		timerFont.setPointSizeF(std::max(24.0, font().pointSizeF() * 3.0));
		p.setFont(timerFont);
		p.setPen(QColor("#f1f1f1"));
		p.drawText(QRectF(outer.left(), outer.top() + 111, outer.width(), 52),
			   Qt::AlignCenter, timeText_);

		QFont sizeFont = font();
		sizeFont.setBold(true);
		sizeFont.setPointSizeF(std::max(9.0, font().pointSizeF() + 0.5));
		p.setFont(sizeFont);
		p.setPen(QColor("#a7a7a7"));
		p.drawText(QRectF(outer.left(), outer.top() + 165, outer.width(), 24),
			   Qt::AlignCenter, sizeText_);

		QFont statsFont = font();
		statsFont.setBold(true);
		statsFont.setPointSizeF(std::max(8.0, font().pointSizeF()));
		p.setFont(statsFont);
		p.setPen(QColor("#b5b5b5"));
		p.drawText(QRectF(outer.left() + 10, outer.top() + 190, outer.width() - 20, 40),
			   Qt::AlignCenter, statsText_);

		QFont footer = font();
		footer.setPointSizeF(std::max(8.0, font().pointSizeF() - 1.0));
		p.setFont(footer);
		p.setPen(QColor("#707a8a"));
		p.drawText(QRectF(outer.left(), outer.bottom() - 30, outer.width(), 18),
			   Qt::AlignCenter, QStringLiteral("Source Record 0.4.8"));
	}

private:
	QColor colorForState() const
	{
		switch (state_) {
		case State::Recording:
			return QColor("#ef4444");
		case State::Paused:
			return QColor("#f59e0b");
		case State::Stopped:
		default:
			return QColor("#777184");
		}
	}

	QString textForState() const
	{
		switch (state_) {
		case State::Recording:
			return QStringLiteral("RECORDING");
		case State::Paused:
			return QStringLiteral("PAUSED");
		case State::Stopped:
		default:
			return QStringLiteral("STOPPED");
		}
	}

	State state_ = State::Stopped;
	QString timeText_ = QStringLiteral("00:00:00");
	QString sizeText_ = QStringLiteral("FILE SIZE 0 B");
	QString statsText_ = QStringLiteral("CPU --%   FPS --   BITRATE --   DROP --");
};

TimesDock::TimesDock(QWidget *parent) : QWidget(parent)
{
	sourceRecordFound_ = RecordMonitor::sourceRecordInstalled();

	setMinimumSize(320, 240);
	setObjectName(QStringLiteral("sourceRecordTimesDock"));
	setStyleSheet(QStringLiteral("QWidget#sourceRecordTimesDock { background: #111111; }"));

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(0);

	panel_ = new StatusPanel(this);
	layout->addWidget(panel_, 1);

	cpuInfo_ = os_cpu_usage_info_start();

	monitor_ = new RecordMonitor(this);
	connect(monitor_, &RecordMonitor::recordEvent, this, &TimesDock::onRecordEvent);
	connect(monitor_, &RecordMonitor::sourcesChanged, this, &TimesDock::onSourcesChanged);

	tickTimer_.setInterval(250);
	connect(&tickTimer_, &QTimer::timeout, this, &TimesDock::tick);
	tickTimer_.start();

	updatePanel();
}

TimesDock::~TimesDock()
{
	shutdown();
}

void TimesDock::shutdown()
{
	tickTimer_.stop();
	if (cpuInfo_) {
		os_cpu_usage_info_destroy(static_cast<os_cpu_usage_info_t *>(cpuInfo_));
		cpuInfo_ = nullptr;
	}
	if (monitor_)
		monitor_->shutdown();
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


QString TimesDock::fmtFileSize(qint64 bytes)
{
	if (bytes < 0)
		bytes = 0;
	static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
	double value = static_cast<double>(bytes);
	int unit = 0;
	while (value >= 1024.0 && unit < 4) {
		value /= 1024.0;
		++unit;
	}
	if (unit == 0)
		return QStringLiteral("FILE SIZE %1 B").arg(bytes);
	return QStringLiteral("FILE SIZE %1 %2")
		.arg(value, 0, 'f', value >= 100.0 ? 0 : 1)
		.arg(QString::fromLatin1(units[unit]));
}

qint64 TimesDock::recordedNow(const Session &s, qint64 nowMs) const
{
	if (!s.active || s.paused)
		return s.recordedMs;
	return s.recordedMs + nowMs - s.runStartMs;
}

int TimesDock::latestSessionIndex() const
{
	if (displaySession_ >= 0 && displaySession_ < sessions_.size())
		return displaySession_;
	return sessions_.isEmpty() ? -1 : sessions_.size() - 1;
}

void TimesDock::updatePanel()
{
	if (!panel_)
		return;

	const int idx = latestSessionIndex();
	if (idx < 0 || idx >= sessions_.size()) {
		panel_->setStatus(StatusPanel::State::Stopped, QStringLiteral("00:00:00"), QStringLiteral("FILE SIZE 0 B"), QStringLiteral("CPU --%   FPS --   BITRATE --   DROP --"));
		return;
	}

	const Session &s = sessions_[idx];
	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

	StatusPanel::State state = StatusPanel::State::Stopped;
	if (s.active && s.paused)
		state = StatusPanel::State::Paused;
	else if (s.active)
		state = StatusPanel::State::Recording;

	const qint64 fileSize = s.totalBytes > 0 ? static_cast<qint64>(s.totalBytes) : (s.filePath.isEmpty() ? 0 : QFileInfo(s.filePath).size());
	const double fps = obs_get_active_fps();
	const QString fpsText = fps > 0.0 ? QString::number(fps, 'f', 1) : QStringLiteral("--");
	const QString bitrateText = s.bitrateKbps > 0.0
		? QStringLiteral("%1 Mbps").arg(s.bitrateKbps / 1000.0, 0, 'f', 1)
		: QStringLiteral("--");
	const QString dropText = QStringLiteral("%1 (%2%)")
		.arg(s.framesDropped)
		.arg(s.totalFrames > 0 ? (100.0 * s.framesDropped / s.totalFrames) : 0.0, 0, 'f', 2);
	const QString stats = QStringLiteral("CPU %1%   FPS %2   BITRATE %3   DROP %4")
		.arg(cpuPercent_, 0, 'f', 1)
		.arg(fpsText)
		.arg(bitrateText)
		.arg(dropText);
	panel_->setStatus(state, fmtDuration(recordedNow(s, nowMs)), fmtFileSize(fileSize), stats);
}

void TimesDock::onSourcesChanged(const QStringList &names)
{
	Q_UNUSED(names);
	if (!sourceRecordFound_)
		sourceRecordFound_ = RecordMonitor::sourceRecordInstalled();
	updatePanel();
}

void TimesDock::onRecordEvent(const RecEventInfo &e)
{
	const qint64 nowMs = e.time.toMSecsSinceEpoch();
	int idx = activeSession_.value(e.key, -1);

	auto begin = [&]() {
		Session s;
		s.key = e.key;
		s.source = e.source;
		s.filePath = e.filePath;
		s.started = e.time;
		s.runStartMs = nowMs;
		s.active = true;
		s.approxStart = e.approximate;
		sessions_.push_back(s);
		idx = sessions_.size() - 1;
		activeSession_.insert(e.key, idx);
		displaySession_ = idx;
	};

	switch (e.type) {
	case RecEventType::Started:
		if (idx < 0) {
			begin();
		} else {
			Session &s = sessions_[idx];
			if (s.approxStart && !e.approximate && s.recordedMs == 0 && s.pauses == 0) {
				s.started = e.time;
				s.runStartMs = nowMs;
				s.approxStart = false;
			}
			s.active = true;
			s.paused = false;
			displaySession_ = idx;
		}
		break;

	case RecEventType::Paused:
		if (idx < 0)
			begin();
		if (!sessions_[idx].paused) {
			Session &s = sessions_[idx];
			s.recordedMs += nowMs - s.runStartMs;
			s.paused = true;
			s.pauses++;
			s.lastPause = e.time;
		}
		displaySession_ = idx;
		break;

	case RecEventType::Resumed:
		if (idx < 0)
			begin();
		if (sessions_[idx].paused) {
			Session &s = sessions_[idx];
			s.paused = false;
			s.runStartMs = nowMs;
			s.prevLastResume = s.lastResume;
			s.lastResume = e.time;
		}
		displaySession_ = idx;
		break;

	case RecEventType::Stats:
		if (idx >= 0 && idx < sessions_.size()) {
			Session &s = sessions_[idx];
			const qint64 t = e.time.toMSecsSinceEpoch();
			if (s.lastStatsMs > 0 && t > s.lastStatsMs && e.totalBytes >= s.lastStatsBytes) {
				const double seconds = static_cast<double>(t - s.lastStatsMs) / 1000.0;
				s.bitrateKbps = (static_cast<double>(e.totalBytes - s.lastStatsBytes) * 8.0 / 1000.0) / seconds;
			}
			s.totalBytes = e.totalBytes;
			s.framesDropped = e.framesDropped;
			s.totalFrames = e.totalFrames;
			s.lastStatsBytes = e.totalBytes;
			s.lastStatsMs = t;
		}
		break;

	case RecEventType::Stopped:
		if (idx < 0)
			break;
		{
			Session &s = sessions_[idx];

			// Source Record may emit an unpause immediately before stopping a paused file.
			if (!s.paused && s.lastResume.isValid() && s.lastResume.msecsTo(e.time) < 500) {
				s.paused = true;
				s.lastResume = s.prevLastResume;
			}

			if (!s.paused)
				s.recordedMs += nowMs - s.runStartMs;

			s.active = false;
			s.paused = false;
			s.ended = e.time;
			s.stopCode = e.stopCode;
			activeSession_.remove(e.key);
			displaySession_ = idx;
		}
		break;
	}

	updatePanel();
}

void TimesDock::tick()
{
	if (cpuInfo_)
		cpuPercent_ = os_cpu_usage_info_query(static_cast<os_cpu_usage_info_t *>(cpuInfo_));
	updatePanel();
}
