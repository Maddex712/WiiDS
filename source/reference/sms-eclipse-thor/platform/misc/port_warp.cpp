// The warp menu's list and the warp it picked (src/port_include/port_warp.h),
// between the game (patch zzz-warp-01) and the menu (platform/gx).
#include "port_warp.h"

#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

std::mutex g_lock;
std::vector<PortWarpEntry> g_list, g_building;
bool g_wanted, g_built, g_ready, g_pending;
int g_area, g_episode;

void copy(char* dst, size_t n, const char* src)
{
	snprintf(dst, n, "%s", src ? src : "");
}

} // namespace

extern "C" {

int (*port_warp_scenario_name_id)(int area, int episode);

int port_warp_list_wanted(void)
{
	std::lock_guard<std::mutex> l(g_lock);
	return g_wanted && !g_built;
}

void port_warp_list_begin(void)
{
	g_building.clear();
}

void port_warp_list_add(const char* world, const char* name, const char* scene, int area, int episode)
{
	PortWarpEntry e;
	copy(e.world, sizeof e.world, world);
	copy(e.name, sizeof e.name, name);
	copy(e.scene, sizeof e.scene, scene);
	e.area    = area;
	e.episode = episode;
	g_building.push_back(e);
}

void port_warp_list_end(void)
{
	// SMS_WARP_LIST=1 prints the list (area,episode as SMS_WARP takes them)
	const char* print = getenv("SMS_WARP_LIST");
	if (print && *print && strcmp(print, "0") != 0)
		for (const PortWarpEntry& e : g_building)
			printf("[warp] %d,%d  %s / %s (%s)\n", e.area, e.episode, e.world, e.name, e.scene);
	printf("[warp] menu list: %d episodes\n", int(g_building.size()));
	std::lock_guard<std::mutex> l(g_lock);
	g_list.swap(g_building);
	g_building.clear();
	g_built = true;
}

void port_warp_set_ready(int ready)
{
	std::lock_guard<std::mutex> l(g_lock);
	g_ready = ready != 0;
}

int port_warp_take(int* area, int* episode)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (!g_pending)
		return 0;
	g_pending = false;
	*area     = g_area;
	*episode  = g_episode;
	return 1;
}

void port_warp_want_list(void)
{
	std::lock_guard<std::mutex> l(g_lock);
	g_wanted = true;
}

int port_warp_count(void)
{
	std::lock_guard<std::mutex> l(g_lock);
	return int(g_list.size());
}

int port_warp_entry(int index, PortWarpEntry* out)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (index < 0 || index >= int(g_list.size()))
		return 0;
	*out = g_list[index];
	return 1;
}

int port_warp_ready(void)
{
	std::lock_guard<std::mutex> l(g_lock);
	return g_ready;
}

void port_warp_request(int area, int episode)
{
	std::lock_guard<std::mutex> l(g_lock);
	g_pending = true;
	g_area    = area;
	g_episode = episode;
}

} // extern "C"
