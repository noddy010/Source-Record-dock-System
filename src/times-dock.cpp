#include "times-dock.hpp"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QRadialGradient>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>

class StatusPanel final : public QWidget {
public:
	enum class State { Stopped, Recording, Paused };

	explicit StatusPanel(QWidget *parent = nullptr) : QWidget(parent)
	{
		setObjectName(QStringLiteral("cleanRecordingPanel"));
		setMinimumSize(300, 220);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	void setStatus(State state, const QString &timeText)
	{
		if (state_ == state && timeText_ == timeText)
			return;
		state_ = state;
		timeText_ = timeText;
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
		panel_->setStatus(StatusPanel::State::Stopped, QStringLiteral("00:00:00"));
		return;
	}

	const Session &s = sessions_[idx];
	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

	StatusPanel::State state = StatusPanel::State::Stopped;
	if (s.active && s.paused)
		state = StatusPanel::State::Paused;
	else if (s.active)
		state = StatusPanel::State::Recording;

	panel_->setStatus(state, fmtDuration(recordedNow(s, nowMs)));
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
	updatePanel();
}
