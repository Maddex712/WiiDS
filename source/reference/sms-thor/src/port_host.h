#ifndef SMS_PORT_HOST_H
#define SMS_PORT_HOST_H

#ifdef _WIN32
#include <sys/types.h>
#include <stddef.h>
#include <time.h>
ssize_t port_pread(int fd, void* buffer, size_t count, unsigned long long offset);
int port_setenv(const char* name, const char* value, int overwrite);
int port_mkdir(const char* path, int mode);
void port_sleep_until(const struct timespec* deadline);
#else
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
static inline ssize_t port_pread(int fd, void* buffer, size_t count, unsigned long long offset)
{
#ifdef __linux__
	return pread64(fd, buffer, count, (off64_t)offset); // images past 2 GiB on 32-bit hosts
#else
	return pread(fd, buffer, count, (off_t)offset);
#endif
}
static inline int port_setenv(const char* name, const char* value, int overwrite)
{
	return setenv(name, value, overwrite);
}
static inline int port_mkdir(const char* path, int mode)
{
	return mkdir(path, mode);
}
static inline void port_sleep_until(const struct timespec* deadline)
{
#ifdef __APPLE__
	// macOS has no clock_nanosleep; sleep the remaining monotonic interval.
	struct timespec now, rem;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return;
	rem.tv_sec = deadline->tv_sec - now.tv_sec;
	rem.tv_nsec = deadline->tv_nsec - now.tv_nsec;
	if (rem.tv_nsec < 0) {
		rem.tv_sec -= 1;
		rem.tv_nsec += 1000000000L;
	}
	if (rem.tv_sec < 0)
		return;
	while (nanosleep(&rem, &rem) != 0) {
		if (errno != EINTR)
			break;
	}
#else
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline, NULL);
#endif
}
#endif

#endif
