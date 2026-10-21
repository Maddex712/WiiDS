#ifdef _WIN32
#include "port_host.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <io.h>
#include <direct.h>
#include <mutex>
#include <climits>
#include <windows.h>

static std::mutex s_file_position_mutex;

ssize_t port_pread(int fd, void* buffer, size_t count, unsigned long long offset)
{
	std::lock_guard<std::mutex> lock(s_file_position_mutex);
	if (_lseeki64(fd, offset, SEEK_SET) < 0)
		return -1;
	return _read(fd, buffer, (unsigned int)std::min(count, (size_t)INT_MAX));
}

int port_setenv(const char* name, const char* value, int overwrite)
{
	if (!overwrite && getenv(name))
		return 0;
	return _putenv_s(name, value);
}

int port_mkdir(const char* path, int)
{
	return _mkdir(path);
}

void port_sleep_until(const struct timespec* deadline)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	long long ns = (long long)(deadline->tv_sec - now.tv_sec) * 1000000000LL +
	               (deadline->tv_nsec - now.tv_nsec);
	if (ns > 0)
		Sleep((DWORD)((ns + 999999LL) / 1000000LL));
}
#endif
