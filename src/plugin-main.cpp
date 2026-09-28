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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QPointer>

#include "times-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static QPointer<TimesDock> g_dock;

static void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT && g_dock)
		g_dock->shutdown();
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

// The main window exists by now, and every other plugin (including Source Record) is loaded.
void obs_module_post_load(void)
{
	auto *dock = new TimesDock();
	g_dock = dock;
	// OBS takes ownership of the widget and wraps it in a dock (View > Docks > Source Record Times).
	if (!obs_frontend_add_dock_by_id("source_record_times", "Source Record Times", dock)) {
		obs_log(LOG_WARNING, "could not add the dock");
		delete dock;
		return;
	}
	obs_frontend_add_event_callback(frontendEvent, nullptr);
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	obs_log(LOG_INFO, "plugin unloaded");
}
