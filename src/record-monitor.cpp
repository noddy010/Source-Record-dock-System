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

#include "record-monitor.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QMetaObject>
#include <QStringList>

#include <cstdint>
#include <cstring>

namespace {

constexpr const char *kFilterId = "source_record_filter";
constexpr int kPollIntervalMs = 50;

// Output types Source Record uses for its "record to file" mode.
bool isRecordingOutputId(const char *id)
{
	if (!id)
		return false;
	return strcmp(id, "ffmpeg_muxer") == 0 || strcmp(id, "mp4_output") == 0 || strcmp(id, "mov_output") == 0;
}

quint64 keyOf(obs_output_t *output)
{
	return static_cast<quint64>(reinterpret_cast<uintptr_t>(output));
}

QString settingString(obs_data_t *data, const char *name)
{
	return data ? QString::fromUtf8(obs_data_get_string(data, name)) : QString();
}

// ---- libobs signal callbacks -------------------------------------------------------------
// These run on OBS worker threads. They only capture what is needed and hand the rest to the
// UI thread, so no Qt widget is touched here.

void forward(void *param, calldata_t *cd, RecEventType type)
{
	auto *monitor = static_cast<RecordMonitor *>(param);
	auto *output = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	if (!monitor || !output)
		return;

	const quint64 key = keyOf(output);
	const QDateTime now = QDateTime::currentDateTime();
	const int code = type == RecEventType::Stopped ? static_cast<int>(calldata_int(cd, "code")) : 0;
	const QString name = QString::fromUtf8(obs_output_get_name(output));

	obs_data_t *settings = obs_output_get_settings(output);
	const QString directory = settingString(settings, "directory");
	const QString format = settingString(settings, "format");
	obs_data_release(settings);

	QMetaObject::invokeMethod(
		monitor, [=]() { monitor->handleSignal(key, name, directory, format, type, now, code); },
		Qt::QueuedConnection);
}

void onStart(void *param, calldata_t *cd)
{
	forward(param, cd, RecEventType::Started);
}
void onPause(void *param, calldata_t *cd)
{
	forward(param, cd, RecEventType::Paused);
}
void onUnpause(void *param, calldata_t *cd)
{
	forward(param, cd, RecEventType::Resumed);
}
void onStop(void *param, calldata_t *cd)
{
	forward(param, cd, RecEventType::Stopped);
}

struct FilterEnumCtx {
	std::vector<std::pair<obs_source_t *, obs_source_t *>> found; // (parent, filter), both with a ref held
};

void enumFiltersCb(obs_source_t *parent, obs_source_t *child, void *param)
{
	auto *ctx = static_cast<FilterEnumCtx *>(param);
	const char *id = obs_source_get_unversioned_id(child);
	if (!id || strcmp(id, kFilterId) != 0)
		return;
	obs_source_t *p = obs_source_get_ref(parent);
	obs_source_t *c = obs_source_get_ref(child);
	if (p && c) {
		ctx->found.emplace_back(p, c);
	} else {
		obs_source_release(p);
		obs_source_release(c);
	}
}

bool enumSourcesCb(void *param, obs_source_t *source)
{
	obs_source_enum_filters(source, enumFiltersCb, param);
	return true;
}

struct OutputEnumCtx {
	std::vector<obs_output_t *> outputs; // each with a ref held
};

bool enumOutputsCb(void *param, obs_output_t *output)
{
	auto *ctx = static_cast<OutputEnumCtx *>(param);
	if (!isRecordingOutputId(obs_output_get_id(output)))
		return true;
	obs_output_t *ref = obs_output_get_ref(output);
	if (ref)
		ctx->outputs.push_back(ref);
	return true;
}

} // namespace

RecordMonitor::RecordMonitor(QObject *parent) : QObject(parent)
{
	timer_.setInterval(kPollIntervalMs);
	connect(&timer_, &QTimer::timeout, this, &RecordMonitor::poll);
	timer_.start();
}

RecordMonitor::~RecordMonitor()
{
	shutdown();
}

bool RecordMonitor::sourceRecordInstalled()
{
	return obs_source_get_display_name(kFilterId) != nullptr;
}

void RecordMonitor::shutdown()
{
	if (shutDown_)
		return;
	shutDown_ = true;
	timer_.stop();

	for (auto it = tracked_.begin(); it != tracked_.end(); ++it) {
		obs_output_t *output = obs_weak_output_get_output(it->weak);
		if (output) {
			signal_handler_t *sh = obs_output_get_signal_handler(output);
			if (sh) {
				signal_handler_disconnect(sh, "start", onStart, this);
				signal_handler_disconnect(sh, "pause", onPause, this);
				signal_handler_disconnect(sh, "unpause", onUnpause, this);
				signal_handler_disconnect(sh, "stop", onStop, this);
			}
			obs_output_release(output);
		}
		obs_weak_output_release(it->weak);
	}
	tracked_.clear();
	sessionOpen_.clear();
}

void RecordMonitor::refreshFilters()
{
	FilterEnumCtx ctx;
	obs_enum_sources(enumSourcesCb, &ctx);
	obs_enum_scenes(enumSourcesCb, &ctx);

	filters_.clear();
	for (auto &pair : ctx.found) {
		FilterInfo info;
		info.filterName = QString::fromUtf8(obs_source_get_name(pair.second));
		info.parentName = QString::fromUtf8(obs_source_get_name(pair.first));
		obs_data_t *settings = obs_source_get_settings(pair.second);
		info.path = settingString(settings, "path");
		info.format = settingString(settings, "filename_formatting");
		obs_data_release(settings);
		filters_.push_back(info);

		obs_source_release(pair.first);
		obs_source_release(pair.second);
	}
}

void RecordMonitor::publishSources()
{
	refreshFilters();
	QStringList names;
	for (const FilterInfo &f : filters_) {
		const QString shown = f.parentName.isEmpty() ? f.filterName : f.parentName;
		if (!names.contains(shown))
			names << shown;
	}
	names.sort(Qt::CaseInsensitive);
	if (pollCount_ == 0 || names != lastSources_) {
		lastSources_ = names;
		emit sourcesChanged(names);
	}
}

bool RecordMonitor::resolveLabel(const QString &outputName, const QString &directory, const QString &format,
				 QString &label) const
{
	QStringList byName;
	QStringList exact;
	for (const FilterInfo &f : filters_) {
		if (f.filterName != outputName)
			continue;
		const QString shown = f.parentName.isEmpty() ? f.filterName : f.parentName;
		if (!byName.contains(shown))
			byName << shown;
		if (f.path == directory && f.format == format && !exact.contains(shown))
			exact << shown;
	}
	const QStringList &best = exact.isEmpty() ? byName : exact;
	if (best.isEmpty())
		return false;
	label = best.join(QStringLiteral(", "));
	return true;
}

void RecordMonitor::emitEvent(quint64 key, const QString &label, RecEventType type, const QDateTime &time, int code,
			      bool approx)
{
	if (type == RecEventType::Started)
		sessionOpen_[key] = true;
	else if (type == RecEventType::Stopped)
		sessionOpen_[key] = false;

	RecEventInfo info;
	info.key = key;
	info.source = label;
	info.type = type;
	info.time = time;
	info.stopCode = code;
	info.approximate = approx;
	emit recordEvent(info);
}

void RecordMonitor::handleSignal(quint64 key, const QString &outputName, const QString &directory,
				 const QString &format, RecEventType type, const QDateTime &time, int code)
{
	if (shutDown_)
		return;
	auto it = tracked_.find(key);
	if (it == tracked_.end())
		return;

	// The filter may already be gone by the time a stop arrives (Source Record can remove its
	// filter after recording), so we only look filters up again when we have to.
	if (!it->resolved || type == RecEventType::Started) {
		refreshFilters();
		QString label;
		if (resolveLabel(outputName, directory, format, label)) {
			it->label = label;
			it->resolved = true;
		}
	}
	if (!it->resolved)
		return; // not a Source Record output (e.g. OBS's own recording)

	emitEvent(key, it->label, type, time, code, false);
}

void RecordMonitor::track(obs_output_t *output, quint64 key)
{
	Tracked t;
	t.weak = obs_output_get_weak_output(output);

	signal_handler_t *sh = obs_output_get_signal_handler(output);
	if (sh) {
		signal_handler_connect(sh, "start", onStart, this);
		signal_handler_connect(sh, "pause", onPause, this);
		signal_handler_connect(sh, "unpause", onUnpause, this);
		signal_handler_connect(sh, "stop", onStop, this);
	}

	obs_data_t *settings = obs_output_get_settings(output);
	const QString directory = settingString(settings, "directory");
	const QString format = settingString(settings, "format");
	obs_data_release(settings);

	refreshFilters();
	QString label;
	t.resolved = resolveLabel(QString::fromUtf8(obs_output_get_name(output)), directory, format, label);
	t.label = label;
	tracked_.insert(key, t);

	// The output may already be running by the time we notice it (for example when the plugin was
	// loaded while a recording was active). Report it, marked as approximate.
	if (t.resolved && obs_output_active(output)) {
		const QDateTime now = QDateTime::currentDateTime();
		emitEvent(key, label, RecEventType::Started, now, 0, true);
		if (obs_output_paused(output))
			emitEvent(key, label, RecEventType::Paused, now, 0, true);
	}
}

void RecordMonitor::forget(quint64 key, bool emitStop)
{
	auto it = tracked_.find(key);
	if (it == tracked_.end())
		return;
	if (emitStop && it->resolved && sessionOpen_.value(key, false))
		emitEvent(key, it->label, RecEventType::Stopped, QDateTime::currentDateTime(), 0, true);
	if (it->weak)
		obs_weak_output_release(it->weak);
	tracked_.erase(it);
	sessionOpen_.remove(key);
}

void RecordMonitor::poll()
{
	if (shutDown_)
		return;

	// About once a second, refresh the list of sources that have a Source Record filter.
	if (pollCount_ % 20 == 0)
		publishSources();
	pollCount_++;

	OutputEnumCtx ctx;
	obs_enum_outputs(enumOutputsCb, &ctx);

	// Pick up outputs we have not seen yet (or whose address was reused by a new output).
	for (obs_output_t *output : ctx.outputs) {
		const quint64 key = keyOf(output);
		auto it = tracked_.constFind(key);
		if (it != tracked_.constEnd()) {
			if (obs_weak_output_references_output(it->weak, output))
				continue;
			forget(key, true);
		}
		track(output, key);
	}

	// Drop outputs that no longer exist.
	QList<quint64> gone;
	for (auto it = tracked_.constBegin(); it != tracked_.constEnd(); ++it) {
		obs_output_t *alive = obs_weak_output_get_output(it->weak);
		if (alive)
			obs_output_release(alive);
		else
			gone << it.key();
	}
	for (quint64 key : gone)
		forget(key, true);

	for (obs_output_t *output : ctx.outputs)
		obs_output_release(output);
}
