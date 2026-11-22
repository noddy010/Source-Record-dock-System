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
#include <QRegularExpression>
#include <QStringList>
#include <QHash>

#include <obs.h>
#include <util/platform.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi1_4.h>
#endif

#include <algorithm>
#include <vector>

#ifdef _WIN32
namespace {
QString findNvidiaAdapterLuid()
{
	IDXGIFactory1 *factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))))
		return QString();

	QString result;
	for (UINT index = 0;; ++index) {
		IDXGIAdapter1 *adapter = nullptr;
		if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
			break;
		if (!adapter)
			continue;

		DXGI_ADAPTER_DESC1 desc = {};
		if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
		    QString::fromWCharArray(desc.Description).contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive)) {
			result = QStringLiteral("luid_0x%1_0x%2")
					 .arg(static_cast<quint32>(desc.AdapterLuid.HighPart), 8, 16, QLatin1Char('0'))
					 .arg(static_cast<quint32>(desc.AdapterLuid.LowPart), 8, 16, QLatin1Char('0'));
			adapter->Release();
			break;
		}
		adapter->Release();
	}
	factory->Release();
	return result;
}
} // namespace
#endif

class StatusPanel final : public QWidget {
public:
	enum class State { Stopped, Recording, Paused };

	explicit StatusPanel(QWidget *parent = nullptr) : QWidget(parent)
	{
		setObjectName(QStringLiteral("cleanRecordingPanel"));
		setMinimumSize(360, 380);
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
		const QStringList statLines = statsText_.split(QLatin1Char('\n'));
		const qreal statTop = outer.top() + 194;
		const qreal statStep = 30.0;
		for (int i = 0; i < statLines.size(); ++i) {
			p.drawText(QRectF(outer.left() + 22, statTop + i * statStep,
					  outer.width() - 44, 24),
				   Qt::AlignLeft | Qt::AlignVCenter, statLines.at(i));
		}

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
	QString statsText_ = QStringLiteral("CPU --%   SPEED -- GHz\nGPU --%   TEMP --°C   POWER --W\nNET ↓ -- KB/s   ↑ -- KB/s\nFPS --   BITRATE --   DROP --");
};

TimesDock::TimesDock(QWidget *parent) : QWidget(parent)
{
	sourceRecordFound_ = RecordMonitor::sourceRecordInstalled();

	setMinimumSize(360, 380);
	setObjectName(QStringLiteral("sourceRecordTimesDock"));
	setStyleSheet(QStringLiteral("QWidget#sourceRecordTimesDock { background: #111111; }"));

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(0);

	panel_ = new StatusPanel(this);
	layout->addWidget(panel_, 1);

	cpuInfo_ = os_cpu_usage_info_start();

#ifdef _WIN32
	PDH_HQUERY cpuSpeedQuery = nullptr;
	PDH_HCOUNTER cpuSpeedCounter = nullptr;
	if (PdhOpenQueryW(nullptr, 0, &cpuSpeedQuery) == ERROR_SUCCESS &&
	    PdhAddEnglishCounterW(cpuSpeedQuery, L"\\Processor Information(_Total)\\Processor Frequency", 0, &cpuSpeedCounter) == ERROR_SUCCESS) {
		cpuSpeedQuery_ = cpuSpeedQuery;
		cpuSpeedCounter_ = cpuSpeedCounter;
		PdhCollectQueryData(cpuSpeedQuery);
	} else if (cpuSpeedQuery) {
		PdhCloseQuery(cpuSpeedQuery);
	}

	PDH_HQUERY networkQuery = nullptr;
	PDH_HCOUNTER downloadCounter = nullptr;
	PDH_HCOUNTER uploadCounter = nullptr;
	if (PdhOpenQueryW(nullptr, 0, &networkQuery) == ERROR_SUCCESS &&
	    PdhAddEnglishCounterW(networkQuery, L"\\Network Interface(*)\\Bytes Received/sec", 0, &downloadCounter) == ERROR_SUCCESS &&
	    PdhAddEnglishCounterW(networkQuery, L"\\Network Interface(*)\\Bytes Sent/sec", 0, &uploadCounter) == ERROR_SUCCESS) {
		networkQuery_ = networkQuery;
		networkDownloadCounter_ = downloadCounter;
		networkUploadCounter_ = uploadCounter;
		PdhCollectQueryData(networkQuery);
	} else if (networkQuery) {
		PdhCloseQuery(networkQuery);
	}

	PDH_HQUERY query = nullptr;
	PDH_HCOUNTER counter = nullptr;
	if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS &&
	    PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) == ERROR_SUCCESS) {
		gpuQuery_ = query;
		gpuCounter_ = counter;
		gpuAdapterLuid_ = findNvidiaAdapterLuid();
		PdhCollectQueryData(query);
	} else if (query) {
		PdhCloseQuery(query);
	}

	IDXGIFactory1 *factory = nullptr;
	if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) {
		for (UINT index = 0;; ++index) {
			IDXGIAdapter1 *adapter = nullptr;
			if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
			if (!adapter) continue;
			DXGI_ADAPTER_DESC1 desc = {};
			if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
			    QString::fromWCharArray(desc.Description).contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive)) {
				IDXGIAdapter3 *adapter3 = nullptr;
				if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void **>(&adapter3))))
					gpuDxgiAdapter_ = adapter3;
				adapter->Release();
				break;
			}
			adapter->Release();
		}
		factory->Release();
	}

	// NVML is optional and loaded dynamically: no extra plugin/runtime dependency.
	HMODULE nvml = LoadLibraryW(L"nvml.dll");
	if (nvml) {
		auto init = reinterpret_cast<int (*)()>(GetProcAddress(nvml, "nvmlInit_v2"));
		auto getHandle = reinterpret_cast<int (*)(unsigned int, void **)>(GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex_v2"));
		auto getTemp = reinterpret_cast<int (*)(void *, int, unsigned int *)>(GetProcAddress(nvml, "nvmlDeviceGetTemperature"));
		auto getPower = reinterpret_cast<int (*)(void *, unsigned int *)>(GetProcAddress(nvml, "nvmlDeviceGetPowerUsage"));
		auto shutdownNvml = reinterpret_cast<int (*)()>(GetProcAddress(nvml, "nvmlShutdown"));
		void *device = nullptr;
		if (init && getHandle && getTemp && getPower && init() == 0 && getHandle(0, &device) == 0) {
			nvmlLibrary_ = nvml;
			nvmlDevice_ = device;
			nvmlGetTemperature_ = reinterpret_cast<void *>(getTemp);
			nvmlGetPowerUsage_ = reinterpret_cast<void *>(getPower);
			nvmlShutdown_ = reinterpret_cast<void *>(shutdownNvml);
		} else {
			if (shutdownNvml) shutdownNvml();
			FreeLibrary(nvml);
		}
	}
#endif

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
#ifdef _WIN32
	if (gpuQuery_) {
		PdhCloseQuery(static_cast<PDH_HQUERY>(gpuQuery_));
		gpuQuery_ = nullptr;
		gpuCounter_ = nullptr;
	}
	if (cpuSpeedQuery_) {
		PdhCloseQuery(static_cast<PDH_HQUERY>(cpuSpeedQuery_));
		cpuSpeedQuery_ = nullptr;
		cpuSpeedCounter_ = nullptr;
	}
	if (networkQuery_) {
		PdhCloseQuery(static_cast<PDH_HQUERY>(networkQuery_));
		networkQuery_ = nullptr;
		networkDownloadCounter_ = nullptr;
		networkUploadCounter_ = nullptr;
	}
	if (gpuDxgiAdapter_) {
		static_cast<IDXGIAdapter3 *>(gpuDxgiAdapter_)->Release();
		gpuDxgiAdapter_ = nullptr;
	}
	if (nvmlLibrary_) {
		if (nvmlShutdown_) reinterpret_cast<int (*)()>(nvmlShutdown_)();
		FreeLibrary(static_cast<HMODULE>(nvmlLibrary_));
		nvmlLibrary_ = nullptr;
		nvmlDevice_ = nullptr;
	}
#endif
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
		? QStringLiteral("%1 kb/s").arg(s.bitrateKbps, 0, 'f', 0)
		: QStringLiteral("--");
	const QString dropText = QStringLiteral("%1 (%2%)")
		.arg(s.framesDropped)
		.arg(s.totalFrames > 0 ? (100.0 * s.framesDropped / s.totalFrames) : 0.0, 0, 'f', 2);
	auto pct = [](double v) { return v >= 0.0 ? QString::number(v, 'f', 0) + QLatin1Char('%') : QStringLiteral("--"); };
	const QString cpuSpeedText = cpuSpeedMHz_ > 0.0
		? QString::number(cpuSpeedMHz_ / 1000.0, 'f', 1)
		: QStringLiteral("N/A");
	const QString gpuTempText = gpuTemperatureC_ >= 0.0 ? QStringLiteral("%1°C").arg(gpuTemperatureC_, 0, 'f', 0) : QStringLiteral("N/A");
	const QString powerText = gpuPowerWatts_ >= 0.0 ? QStringLiteral("%1W").arg(gpuPowerWatts_, 0, 'f', 1) : QStringLiteral("N/A");
	auto rateText = [](double bytesPerSecond) {
		if (bytesPerSecond < 0.0) return QStringLiteral("--");
		// Network rates are displayed in bits/s (not bytes/s), using decimal units.
		const double bitsPerSecond = bytesPerSecond * 8.0;
		if (bitsPerSecond >= 1000000.0)
			return QStringLiteral("%1 Mb/s").arg(bitsPerSecond / 1000000.0, 0, 'f', 1);
		return QStringLiteral("%1 Kb/s").arg(bitsPerSecond / 1000.0, 0, 'f', 0);
	};
	const QString stats = QStringLiteral("CPU %1   SPEED %2 GHz\nGPU %3   TEMP %4   POWER %5\nNET ↓ %6   ↑ %7\nFPS %8   BITRATE %9   DROP %10")
		.arg(QString::number(cpuPercent_, 'f', 1) + QLatin1Char('%')).arg(cpuSpeedText).arg(pct(gpuOverallPercent_))
		.arg(gpuTempText).arg(powerText).arg(rateText(networkDownloadBytesPerSec_)).arg(rateText(networkUploadBytesPerSec_))
		.arg(fpsText).arg(bitrateText).arg(dropText);
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
			// Keep the other stats live, but calculate/display bitrate from a full
			// one-second byte interval so the number does not jump around every poll.
			if (s.lastBitrateMs > 0 && t - s.lastBitrateMs >= 1000 && e.totalBytes >= s.lastBitrateBytes) {
				const double seconds = static_cast<double>(t - s.lastBitrateMs) / 1000.0;
				s.bitrateKbps = (static_cast<double>(e.totalBytes - s.lastBitrateBytes) * 8.0 / 1000.0) / seconds;
				s.lastBitrateBytes = e.totalBytes;
				s.lastBitrateMs = t;
			} else if (s.lastBitrateMs == 0) {
				s.lastBitrateBytes = e.totalBytes;
				s.lastBitrateMs = t;
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

void TimesDock::updateGpuUsage()
{
#ifdef _WIN32
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (lastGpuPollMs_ != 0 && now - lastGpuPollMs_ < 1000) return;
	lastGpuPollMs_ = now;

	if (gpuQuery_ && gpuCounter_) {
		const auto query = static_cast<PDH_HQUERY>(gpuQuery_);
		const auto counter = static_cast<PDH_HCOUNTER>(gpuCounter_);
		if (PdhCollectQueryData(query) == ERROR_SUCCESS) {
			DWORD bytes = 0, count = 0;
			PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
			if (status == PDH_MORE_DATA && bytes && count) {
				std::vector<unsigned char> buffer(bytes);
				auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
				status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items);
				if (status == ERROR_SUCCESS) {
					QHash<QString, double> allEngines;
					static const QRegularExpression pattern(
						QStringLiteral("^(?:pid_\\d+_)?(.+)_engtype_(?:3D|VideoEncode|VideoDecode|Copy)$"),
						QRegularExpression::CaseInsensitiveOption);
					for (DWORD i = 0; i < count; ++i) {
						if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
						    items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
						const QString instance = QString::fromWCharArray(items[i].szName);
						if (!gpuAdapterLuid_.isEmpty() && !instance.contains(gpuAdapterLuid_, Qt::CaseInsensitive)) continue;
						const auto match = pattern.match(instance);
						if (!match.hasMatch()) continue;
						allEngines[match.captured(1)] += items[i].FmtValue.doubleValue;
					}
					auto busiest = [](const QHash<QString, double> &map) {
						double value = -1.0;
						for (auto it = map.cbegin(); it != map.cend(); ++it) value = std::max(value, it.value());
						return value < 0.0 ? -1.0 : std::clamp(value, 0.0, 100.0);
					};
					// Overall GPU load: busiest physical engine, not a sum of engines.
					gpuOverallPercent_ = busiest(allEngines);
				}
			}
		}
	}
	if (networkQuery_ && networkDownloadCounter_ && networkUploadCounter_) {
		const auto query = static_cast<PDH_HQUERY>(networkQuery_);
		if (PdhCollectQueryData(query) == ERROR_SUCCESS) {
			auto sumCounter = [](PDH_HCOUNTER counter) {
				DWORD bytes = 0, count = 0;
				PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
				if (status != PDH_MORE_DATA || bytes == 0 || count == 0) return -1.0;
				std::vector<unsigned char> buffer(bytes);
				auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
				status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items);
				if (status != ERROR_SUCCESS) return -1.0;
				double total = 0.0;
				bool found = false;
				for (DWORD i = 0; i < count; ++i) {
					if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
					    items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
					total += std::max(0.0, items[i].FmtValue.doubleValue);
					found = true;
				}
				return found ? total : -1.0;
			};
			networkDownloadBytesPerSec_ = sumCounter(static_cast<PDH_HCOUNTER>(networkDownloadCounter_));
			networkUploadBytesPerSec_ = sumCounter(static_cast<PDH_HCOUNTER>(networkUploadCounter_));
		}
	}
	if (cpuSpeedQuery_ && cpuSpeedCounter_) {
		const auto query = static_cast<PDH_HQUERY>(cpuSpeedQuery_);
		const auto counter = static_cast<PDH_HCOUNTER>(cpuSpeedCounter_);
		if (PdhCollectQueryData(query) == ERROR_SUCCESS) {
			PDH_FMT_COUNTERVALUE value = {};
			if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS &&
			    (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA) &&
			    value.doubleValue > 0.0)
				cpuSpeedMHz_ = value.doubleValue;
		}
	}
	if (nvmlDevice_ && nvmlGetTemperature_ && nvmlGetPowerUsage_) {
		unsigned int value = 0;
		if (reinterpret_cast<int (*)(void *, int, unsigned int *)>(nvmlGetTemperature_)(nvmlDevice_, 0, &value) == 0)
			gpuTemperatureC_ = static_cast<double>(value);
		if (reinterpret_cast<int (*)(void *, unsigned int *)>(nvmlGetPowerUsage_)(nvmlDevice_, &value) == 0)
			gpuPowerWatts_ = static_cast<double>(value) / 1000.0;
	}
#endif
}
void TimesDock::tick()
{
	if (cpuInfo_)
		cpuPercent_ = os_cpu_usage_info_query(static_cast<os_cpu_usage_info_t *>(cpuInfo_));
	updateGpuUsage();
	updatePanel();
}
