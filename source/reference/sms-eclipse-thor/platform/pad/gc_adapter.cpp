// GameCube adapters in Wii U mode (Nintendo WUP-028, Mayflash and other copies;
// USB 057e:0337), read by the port itself. SDL can't be counted on for them:
// its HIDAPI layer only takes HID devices on the Generic Desktop usage page and
// this adapter says Game Controls, so SDL reads it only through libusb, which
// the Windows and macOS builds of SDL lack; on Linux SDL needs the adapter's
// hidraw node, which udev keeps root-only unless a rule opens it. So the port
// asks SDL to leave the adapter alone (port_gc_adapter_sdl_ignore, used by the
// GX layer before SDL_Init) and a thread here reads it, opened through the
// first way that works:
//   Windows  WinUSB (the driver Zadig or Dolphin installs), else the stock HID
//            driver ("USB Input Device"), through winusb.dll / hid.dll
//   Linux    /dev/hidraw*, else libusb (Steam's udev rule opens the adapter's
//            USB device to the logged-in user)
//   macOS    libusb, when it is installed (Homebrew)
// Libraries are loaded at run time, so nothing extra is linked or required.
// SMS_GC_ADAPTER=VVVV:PPPP (hex) adds another USB id; SMS_NO_GC_ADAPTER=1 turns
// all of this off and leaves the adapter to SDL.
#include "port_compat.h"
#include "port_platform.h"
#include "gc_adapter.h"
#include <atomic>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define GCAD_WINDOWS 1
#include <windows.h>
#elif (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
#define GCAD_POSIX 1
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace {

// latest report of the first connected port (written by the thread, read by PADRead)
struct State {
	std::atomic<int> present{ 0 };
	std::atomic<int> b1{ 0 }, b2{ 0 }, lx{ 0 }, ly{ 0 }, cx{ 0 }, cy{ 0 }, lt{ 0 }, rt{ 0 };
	std::atomic<int> rumble{ 0 }; // written by PADControlMotor
};
State g_state;

// USB ids of adapters that speak the Nintendo protocol: the official adapter
// and the copies of it, plus SMS_GC_ADAPTER's.
struct UsbId {
	unsigned vid, pid;
};
UsbId g_ids[2] = { { 0x057E, 0x0337 }, { 0, 0 } };

int id_count()
{
	static const int n = [] {
		unsigned v, p;
		const char* e = getenv("SMS_GC_ADAPTER");
		if (!e || sscanf(e, "%x:%x", &v, &p) != 2)
			return 1;
		g_ids[1].vid = v & 0xFFFF;
		g_ids[1].pid = p & 0xFFFF;
		return 2;
	}();
	return n;
}

bool is_adapter(unsigned vid, unsigned pid)
{
	for (int i = 0; i < id_count(); i++)
		if (g_ids[i].vid == vid && g_ids[i].pid == pid)
			return true;
	return false;
}

// "vid_057e&pid_0337" anywhere in a Windows instance id or device path, any case.
bool names_adapter(const char* s)
{
	char low[512];
	size_t n = 0;
	for (; s[n] && n < sizeof low - 1; n++)
		low[n] = (char)tolower((unsigned char)s[n]);
	low[n] = 0;
	for (int i = 0; i < id_count(); i++) {
		char want[32];
		snprintf(want, sizeof want, "vid_%04x&pid_%04x", g_ids[i].vid, g_ids[i].pid);
		if (strstr(low, want))
			return true;
	}
	return false;
}

void sleep_ms(int ms)
{
#if GCAD_WINDOWS
	Sleep(ms);
#elif GCAD_POSIX
	usleep(ms * 1000);
#endif
}

// An open adapter. read() returns one report's length, 0 when none came
// within about 100 ms, or -1 once the adapter is gone, saying why in `error`.
struct Link {
	const char* how = "";
	char error[96] = "";
	virtual ~Link() {}
	virtual int read(unsigned char* buf, int cap) = 0;
	virtual bool write(const unsigned char* buf, int len) = 0;
};

// Reads the adapter until it goes away. Report 0x21 is 9 bytes per port after
// the id: status (0x10 wired, 0x20 WaveBird), two button bytes, the sticks and
// the triggers.
void session(Link& link)
{
	port_log("[pad] GameCube adapter opened (%s)\n", link.how);
	const unsigned char init = 0x13; // starts the reports
	if (!link.write(&init, 1))
		port_log("[pad] GameCube adapter: the start command failed, reading anyway\n");
	int origin[4]   = { 128, 128, 128, 128 };
	bool haveOrigin = false;
	int slot        = -1; // the port feeding controller 1
	int motor       = 0;  // the port whose motor runs, 1-4, or 0
	for (;;) {
		// Rumble: one byte per port after 0x11. The motors only run with
		// the adapter's second (power) cable plugged in.
		const int want = g_state.rumble && slot >= 0 ? slot + 1 : 0;
		if (want != motor) {
			unsigned char cmd[5] = { 0x11, 0, 0, 0, 0 };
			if (want)
				cmd[want] = 1;
			link.write(cmd, sizeof cmd);
			motor = want;
		}
		// Exactly one report: 37 bytes is also the adapter's USB packet size, so
		// a longer read through libusb or WinUSB runs on into the next reports
		// and fails with an overflow once one no longer fits.
		unsigned char rep[37];
		const int n = link.read(rep, sizeof rep);
		if (n < 0)
			break; // unplugged
		if (n < 37 || rep[0] != 0x21)
			continue;
		if (slot < 0 || !(rep[1 + slot * 9] & 0x30)) {
			slot = -1;
			for (int p = 0; p < 4; p++)
				if (rep[1 + p * 9] & 0x30) { // wired or WaveBird
					slot = p;
					break;
				}
			haveOrigin = false;
		}
		if (slot < 0) {
			g_state.present = 0;
			continue;
		}
		const unsigned char* c = rep + 1 + slot * 9;
		if (!haveOrigin) { // sticks as found on connect are centre, as on hardware
			for (int k = 0; k < 4; k++)
				origin[k] = c[3 + k];
			haveOrigin = true;
		}
		g_state.b1      = c[1];
		g_state.b2      = c[2];
		g_state.lx      = c[3] - origin[0];
		g_state.ly      = c[4] - origin[1];
		g_state.cx      = c[5] - origin[2];
		g_state.cy      = c[6] - origin[3];
		g_state.lt      = c[7];
		g_state.rt      = c[8];
		g_state.present = 1;
	}
	g_state.present = 0;
	port_log("[pad] GameCube adapter disconnected%s%s\n", link.error[0] ? ": " : "", link.error);
}

#if GCAD_WINDOWS
// --- Windows: WinUSB, else the stock HID driver ------------------------------
typedef void* HDEVINFO_;
struct SpDevinfoData { DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved; };
struct SpDeviceInterfaceData { DWORD cbSize; GUID InterfaceClassGuid; DWORD Flags; ULONG_PTR Reserved; };
struct SpDeviceInterfaceDetailW { DWORD cbSize; WCHAR DevicePath[1]; };
// HIDP_CAPS
struct HidCaps {
	USHORT Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
	USHORT Reserved[17], Counts[10];
};

typedef HDEVINFO_ (WINAPI *PGetClassDevs)(const GUID*, PCWSTR, HWND, DWORD);
typedef BOOL (WINAPI *PEnumDeviceInfo)(HDEVINFO_, DWORD, SpDevinfoData*);
typedef BOOL (WINAPI *PGetInstanceId)(HDEVINFO_, SpDevinfoData*, PWSTR, DWORD, PDWORD);
typedef HKEY (WINAPI *POpenDevRegKey)(HDEVINFO_, SpDevinfoData*, DWORD, DWORD, DWORD, REGSAM);
typedef BOOL (WINAPI *PEnumInterfaces)(HDEVINFO_, SpDevinfoData*, const GUID*, DWORD, SpDeviceInterfaceData*);
typedef BOOL (WINAPI *PGetDetail)(HDEVINFO_, SpDeviceInterfaceData*, SpDeviceInterfaceDetailW*, DWORD, PDWORD, SpDevinfoData*);
typedef BOOL (WINAPI *PDestroyList)(HDEVINFO_);
typedef BOOL (WINAPI *PWuInit)(HANDLE, void**);
typedef BOOL (WINAPI *PWuFree)(void*);
typedef BOOL (WINAPI *PWuWrite)(void*, UCHAR, PUCHAR, ULONG, PULONG, void*);
typedef BOOL (WINAPI *PWuRead)(void*, UCHAR, PUCHAR, ULONG, PULONG, void*);
typedef BOOL (WINAPI *PWuPolicy)(void*, UCHAR, ULONG, ULONG, PVOID);
typedef void (WINAPI *PHidGuid)(GUID*);
typedef BOOLEAN (WINAPI *PHidPreparsed)(HANDLE, void**);
typedef BOOLEAN (WINAPI *PHidFreePreparsed)(void*);
typedef LONG (WINAPI *PHidCaps)(void*, HidCaps*);
typedef BOOLEAN (WINAPI *PHidSetOutput)(HANDLE, void*, ULONG);

PGetClassDevs pGetClassDevs; PEnumDeviceInfo pEnumDeviceInfo; PGetInstanceId pGetInstanceId;
POpenDevRegKey pOpenDevRegKey; PEnumInterfaces pEnumInterfaces; PGetDetail pGetDetail; PDestroyList pDestroyList;
PWuInit pWuInit; PWuFree pWuFree; PWuWrite pWuWrite; PWuRead pWuRead; PWuPolicy pWuPolicy;
PHidGuid pHidGuid; PHidPreparsed pHidPreparsed; PHidFreePreparsed pHidFreePreparsed; PHidCaps pHidCaps;
PHidSetOutput pHidSetOutput;
bool g_winusb, g_hid;

bool load()
{
	HMODULE sa = LoadLibraryA("setupapi.dll");
	if (!sa)
		return false;
	pGetClassDevs   = (PGetClassDevs)GetProcAddress(sa, "SetupDiGetClassDevsW");
	pEnumDeviceInfo = (PEnumDeviceInfo)GetProcAddress(sa, "SetupDiEnumDeviceInfo");
	pGetInstanceId  = (PGetInstanceId)GetProcAddress(sa, "SetupDiGetDeviceInstanceIdW");
	pOpenDevRegKey  = (POpenDevRegKey)GetProcAddress(sa, "SetupDiOpenDevRegKey");
	pEnumInterfaces = (PEnumInterfaces)GetProcAddress(sa, "SetupDiEnumDeviceInterfaces");
	pGetDetail      = (PGetDetail)GetProcAddress(sa, "SetupDiGetDeviceInterfaceDetailW");
	pDestroyList    = (PDestroyList)GetProcAddress(sa, "SetupDiDestroyDeviceInfoList");
	if (!(pGetClassDevs && pEnumDeviceInfo && pGetInstanceId && pOpenDevRegKey && pEnumInterfaces && pGetDetail &&
	      pDestroyList))
		return false;
	if (HMODULE wu = LoadLibraryA("winusb.dll")) {
		pWuInit   = (PWuInit)GetProcAddress(wu, "WinUsb_Initialize");
		pWuFree   = (PWuFree)GetProcAddress(wu, "WinUsb_Free");
		pWuWrite  = (PWuWrite)GetProcAddress(wu, "WinUsb_WritePipe");
		pWuRead   = (PWuRead)GetProcAddress(wu, "WinUsb_ReadPipe");
		pWuPolicy = (PWuPolicy)GetProcAddress(wu, "WinUsb_SetPipePolicy");
		g_winusb  = pWuInit && pWuFree && pWuWrite && pWuRead && pWuPolicy;
	}
	if (HMODULE hid = LoadLibraryA("hid.dll")) {
		pHidGuid          = (PHidGuid)GetProcAddress(hid, "HidD_GetHidGuid");
		pHidPreparsed     = (PHidPreparsed)GetProcAddress(hid, "HidD_GetPreparsedData");
		pHidFreePreparsed = (PHidFreePreparsed)GetProcAddress(hid, "HidD_FreePreparsedData");
		pHidCaps          = (PHidCaps)GetProcAddress(hid, "HidP_GetCaps");
		pHidSetOutput     = (PHidSetOutput)GetProcAddress(hid, "HidD_SetOutputReport");
		g_hid             = pHidGuid && pHidPreparsed && pHidFreePreparsed && pHidCaps && pHidSetOutput;
	}
	return g_winusb || g_hid;
}

void narrow(const WCHAR* w, char* out, size_t cap)
{
	size_t i = 0;
	for (; w[i] && i < cap - 1; i++)
		out[i] = w[i] < 128 ? (char)w[i] : '?';
	out[i] = 0;
}

// Device path of the adapter's WinUSB interface. *seen: the adapter is plugged in.
bool find_winusb_path(WCHAR* out, size_t cap, bool* seen)
{
	HDEVINFO_ set = pGetClassDevs(NULL, L"USB", NULL, 0x2 | 0x4 /* DIGCF_PRESENT | DIGCF_ALLCLASSES */);
	if (set == (HDEVINFO_)INVALID_HANDLE_VALUE)
		return false;
	bool found = false;
	SpDevinfoData dev;
	for (DWORD i = 0; !found; i++) {
		dev.cbSize = sizeof dev;
		if (!pEnumDeviceInfo(set, i, &dev))
			break;
		WCHAR id[200];
		char narrowId[200];
		if (!pGetInstanceId(set, &dev, id, 200, NULL))
			continue;
		narrow(id, narrowId, sizeof narrowId);
		if (strncasecmp(narrowId, "USB\\", 4) != 0 || !names_adapter(narrowId))
			continue;
		*seen    = true;
		HKEY key = pOpenDevRegKey(set, &dev, 1 /* DICS_FLAG_GLOBAL */, 0, 1 /* DIREG_DEV */, KEY_READ);
		if (key == (HKEY)INVALID_HANDLE_VALUE)
			continue;
		WCHAR guids[200] = {};
		DWORD sz         = sizeof guids - 4;
		LSTATUS st       = RegQueryValueExW(key, L"DeviceInterfaceGUIDs", NULL, NULL, (LPBYTE)guids, &sz);
		RegCloseKey(key);
		if (st != ERROR_SUCCESS || !guids[0])
			continue; // not WinUSB (the stock HID driver, say)
		// Device path = \\?\ + instance id with separators turned into '#' + '#' + interface GUID
		// (the first "{...}" of DeviceInterfaceGUIDs).
		for (WCHAR* c = id; *c; c++)
			if (*c == L'\\' || *c == L'/')
				*c = L'#';
		swprintf(out, cap, L"\\\\?\\%ls#%ls", id, guids);
		found = true;
	}
	pDestroyList(set);
	return found;
}

// Device path of the adapter's HID interface (the stock driver).
bool find_hid_path(WCHAR* out, size_t cap, bool* seen)
{
	GUID hid;
	pHidGuid(&hid);
	HDEVINFO_ set = pGetClassDevs(&hid, NULL, NULL, 0x2 | 0x10 /* DIGCF_PRESENT | DIGCF_DEVICEINTERFACE */);
	if (set == (HDEVINFO_)INVALID_HANDLE_VALUE)
		return false;
	bool found = false;
	for (DWORD i = 0; !found; i++) {
		SpDeviceInterfaceData ifd;
		ifd.cbSize = sizeof ifd;
		if (!pEnumInterfaces(set, NULL, &hid, i, &ifd))
			break;
		union {
			SpDeviceInterfaceDetailW d;
			BYTE raw[sizeof(DWORD) + 512 * sizeof(WCHAR)];
		} detail;
		detail.d.cbSize = sizeof(void*) == 8 ? 8 : 6; // sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)
		if (!pGetDetail(set, &ifd, &detail.d, sizeof detail, NULL, NULL))
			continue;
		char path[512];
		narrow(detail.d.DevicePath, path, sizeof path);
		if (!names_adapter(path))
			continue;
		*seen = true;
		wcsncpy(out, detail.d.DevicePath, cap - 1);
		out[cap - 1] = 0;
		found        = true;
	}
	pDestroyList(set);
	return found;
}

struct WinUsbLink : Link {
	HANDLE h;
	void* wu;
	WinUsbLink(HANDLE h_, void* wu_) : h(h_), wu(wu_)
	{
		how           = "WinUSB";
		ULONG timeout = 100;
		pWuPolicy(wu, 0x81, 0x03 /* PIPE_TRANSFER_TIMEOUT */, sizeof timeout, &timeout);
	}
	~WinUsbLink()
	{
		pWuFree(wu);
		CloseHandle(h);
	}
	int read(unsigned char* buf, int cap) override
	{
		ULONG n = 0;
		if (!pWuRead(wu, 0x81, buf, cap, &n, NULL)) {
			const DWORD e = GetLastError();
			if (e == ERROR_SEM_TIMEOUT)
				return 0;
			snprintf(error, sizeof error, "WinUSB read error %lu", e);
			return -1;
		}
		return (int)n;
	}
	bool write(const unsigned char* buf, int len) override
	{
		ULONG n = 0;
		return pWuWrite(wu, 0x02, (PUCHAR)buf, len, &n, NULL) && n == (ULONG)len;
	}
};

// The stock HID driver: reports carry their id in front, as from the adapter,
// and are padded to the lengths in the adapter's report descriptor.
struct WinHidLink : Link {
	HANDLE h;
	int inLen, outLen;
	OVERLAPPED rov = {}, wov = {};
	bool pending = false;
	unsigned char in[256];
	WinHidLink(HANDLE h_, int inLen_, int outLen_) : h(h_), inLen(inLen_), outLen(outLen_)
	{
		how         = "HID driver";
		rov.hEvent  = CreateEventW(NULL, TRUE, FALSE, NULL);
		wov.hEvent  = CreateEventW(NULL, TRUE, FALSE, NULL);
	}
	~WinHidLink()
	{
		DWORD n;
		if (pending) {
			CancelIoEx(h, &rov);
			GetOverlappedResult(h, &rov, &n, TRUE);
		}
		CloseHandle(h);
		CloseHandle(rov.hEvent);
		CloseHandle(wov.hEvent);
	}
	int read(unsigned char* buf, int cap) override
	{
		if (!pending) {
			ResetEvent(rov.hEvent);
			if (!ReadFile(h, in, inLen, NULL, &rov) && GetLastError() != ERROR_IO_PENDING) {
				snprintf(error, sizeof error, "HID read error %lu", GetLastError());
				return -1;
			}
			pending = true;
		}
		if (WaitForSingleObject(rov.hEvent, 100) == WAIT_TIMEOUT)
			return 0;
		pending = false;
		DWORD n = 0;
		if (!GetOverlappedResult(h, &rov, &n, FALSE)) {
			snprintf(error, sizeof error, "HID read error %lu", GetLastError());
			return -1;
		}
		const int m = (int)n < cap ? (int)n : cap;
		memcpy(buf, in, m);
		return m;
	}
	bool write(const unsigned char* buf, int len) override
	{
		unsigned char out[64] = {};
		int n                 = outLen > len ? outLen : len;
		if (n > (int)sizeof out)
			n = sizeof out;
		memcpy(out, buf, len);
		ResetEvent(wov.hEvent);
		if (!WriteFile(h, out, n, NULL, &wov)) {
			if (GetLastError() != ERROR_IO_PENDING)
				return pHidSetOutput(h, out, n); // as a SET_REPORT request instead
			if (WaitForSingleObject(wov.hEvent, 200) == WAIT_TIMEOUT) {
				CancelIoEx(h, &wov);
				DWORD done;
				GetOverlappedResult(h, &wov, &done, TRUE);
				return false;
			}
		}
		DWORD done = 0;
		return GetOverlappedResult(h, &wov, &done, FALSE) && done > 0;
	}
};

Link* open_link(char* why, size_t cap, bool* seen)
{
	WCHAR path[512];
	if (g_winusb && find_winusb_path(path, 512, seen)) {
		HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
		void* wu = NULL;
		if (h != INVALID_HANDLE_VALUE && pWuInit(h, &wu))
			return new WinUsbLink(h, wu);
		snprintf(why, cap, "WinUSB would not open it (error %lu)", GetLastError());
		if (h != INVALID_HANDLE_VALUE)
			CloseHandle(h);
	}
	if (g_hid && find_hid_path(path, 512, seen)) {
		HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		                       OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			snprintf(why, cap, "its HID device would not open (error %lu)", GetLastError());
			return NULL;
		}
		int inLen = 38, outLen = 6; // the adapter's, if Windows can't say
		void* pp  = NULL;
		HidCaps caps;
		if (pHidPreparsed(h, &pp)) {
			if (pHidCaps(pp, &caps) == 0x00110000 /* HIDP_STATUS_SUCCESS */ && caps.InputReportByteLength >= 37) {
				inLen  = caps.InputReportByteLength;
				outLen = caps.OutputReportByteLength;
			}
			pHidFreePreparsed(pp);
		}
		if (inLen > 256)
			inLen = 256;
		return new WinHidLink(h, inLen, outLen);
	}
	return NULL;
}

DWORD WINAPI thread_main(void*);
void start_thread() { CreateThread(NULL, 0, thread_main, NULL, 0, NULL); }

#elif GCAD_POSIX
// --- Linux: hidraw, else libusb; macOS: libusb --------------------------------
bool read_text(const char* path, char* buf, size_t cap)
{
	FILE* f = fopen(path, "r");
	if (!f)
		return false;
	const size_t n = fread(buf, 1, cap - 1, f);
	fclose(f);
	buf[n] = 0;
	return true;
}

// The adapter's USB device is in sysfs (Linux), so it is plugged in.
bool usb_present()
{
#ifdef __linux__
	DIR* d = opendir("/sys/bus/usb/devices");
	if (!d)
		return false;
	bool found = false;
	while (dirent* e = readdir(d)) {
		char path[300], v[16], p[16];
		snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idVendor", e->d_name);
		if (!read_text(path, v, sizeof v))
			continue;
		snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idProduct", e->d_name);
		if (read_text(path, p, sizeof p) && is_adapter(strtoul(v, NULL, 16), strtoul(p, NULL, 16))) {
			found = true;
			break;
		}
	}
	closedir(d);
	return found;
#else
	return false;
#endif
}

struct FdLink : Link {
	int fd;
	explicit FdLink(int fd_) : fd(fd_) { how = "hidraw"; }
	~FdLink() { close(fd); }
	int read(unsigned char* buf, int cap) override
	{
		pollfd p = { fd, POLLIN, 0 };
		const int r = poll(&p, 1, 100);
		if (r == 0)
			return 0;
		if (r < 0 && errno == EINTR)
			return 0;
		if (r < 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) {
			snprintf(error, sizeof error, "hidraw: %s", r < 0 ? strerror(errno) : "device gone");
			return -1;
		}
		const ssize_t n = ::read(fd, buf, cap);
		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			return 0;
		if (n < 0) {
			snprintf(error, sizeof error, "hidraw: %s", strerror(errno));
			return -1;
		}
		return (int)n;
	}
	bool write(const unsigned char* buf, int len) override { return ::write(fd, buf, len) == len; }
};

#ifdef __linux__
Link* open_hidraw(char* why, size_t cap, bool* seen)
{
	DIR* d = opendir("/sys/class/hidraw");
	if (!d)
		return NULL;
	Link* link = NULL;
	while (dirent* e = readdir(d)) {
		if (strncmp(e->d_name, "hidraw", 6) != 0)
			continue;
		char path[300], uevent[1024];
		snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", e->d_name);
		if (!read_text(path, uevent, sizeof uevent))
			continue;
		const char* id = strstr(uevent, "HID_ID=");
		unsigned bus, vid, pid;
		if (!id || sscanf(id + 7, "%x:%x:%x", &bus, &vid, &pid) != 3 || !is_adapter(vid, pid))
			continue;
		*seen = true;
		snprintf(path, sizeof path, "/dev/%s", e->d_name);
		const int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) {
			snprintf(why, cap, "%s: %s", path, strerror(errno));
			continue;
		}
		link = new FdLink(fd);
		break;
	}
	closedir(d);
	return link;
}
#endif

// libusb-1.0, loaded at run time.
struct Libusb {
	void* ctx;
	int (*init)(void**);
	void* (*open_vid_pid)(void*, unsigned short, unsigned short);
	void (*close)(void*);
	int (*auto_detach)(void*, int);
	int (*claim)(void*, int);
	int (*release)(void*, int);
	int (*interrupt)(void*, unsigned char, unsigned char*, int, int*, unsigned int);
	const char* (*error_name)(int);
} lu;
enum { LU_ERROR_BUSY = -6, LU_ERROR_TIMEOUT = -7 };

bool libusb_ready()
{
	static const bool ok = [] {
		static const char* const kNames[] = {
#ifdef __APPLE__
			"libusb-1.0.0.dylib", "/opt/homebrew/lib/libusb-1.0.0.dylib", "/usr/local/lib/libusb-1.0.0.dylib",
#else
			"libusb-1.0.so.0", "libusb-1.0.so",
#endif
		};
		void* lib = NULL;
		for (size_t i = 0; i < sizeof kNames / sizeof kNames[0] && !lib; i++)
			lib = dlopen(kNames[i], RTLD_NOW | RTLD_LOCAL);
		if (!lib)
			return false;
		*(void**)&lu.init         = dlsym(lib, "libusb_init");
		*(void**)&lu.open_vid_pid = dlsym(lib, "libusb_open_device_with_vid_pid");
		*(void**)&lu.close        = dlsym(lib, "libusb_close");
		*(void**)&lu.auto_detach  = dlsym(lib, "libusb_set_auto_detach_kernel_driver");
		*(void**)&lu.claim        = dlsym(lib, "libusb_claim_interface");
		*(void**)&lu.release      = dlsym(lib, "libusb_release_interface");
		*(void**)&lu.interrupt    = dlsym(lib, "libusb_interrupt_transfer");
		*(void**)&lu.error_name   = dlsym(lib, "libusb_error_name");
		return lu.init && lu.open_vid_pid && lu.close && lu.auto_detach && lu.claim && lu.release && lu.interrupt &&
		       lu.error_name && lu.init(&lu.ctx) == 0;
	}();
	return ok;
}

struct UsbLink : Link {
	void* dev;
	explicit UsbLink(void* dev_) : dev(dev_) { how = "libusb"; }
	~UsbLink()
	{
		lu.release(dev, 0);
		lu.close(dev); // gives the adapter back to the system's HID driver
	}
	int read(unsigned char* buf, int cap) override
	{
		int n       = 0;
		const int r = lu.interrupt(dev, 0x81, buf, cap, &n, 100);
		if (r == LU_ERROR_TIMEOUT)
			return 0;
		if (r == 0)
			return n;
		snprintf(error, sizeof error, "libusb: %s", lu.error_name(r));
		return -1;
	}
	bool write(const unsigned char* buf, int len) override
	{
		int n = 0;
		return lu.interrupt(dev, 0x02, (unsigned char*)buf, len, &n, 100) == 0 && n == len;
	}
};

Link* open_libusb(char* why, size_t cap)
{
	if (!libusb_ready())
		return NULL;
	for (int i = 0; i < id_count(); i++) {
		void* dev = lu.open_vid_pid(lu.ctx, (unsigned short)g_ids[i].vid, (unsigned short)g_ids[i].pid);
		if (!dev)
			continue;
		lu.auto_detach(dev, 1); // from the kernel's HID driver, while we have it (Linux)
		const int r = lu.claim(dev, 0);
		if (r == 0)
			return new UsbLink(dev);
		snprintf(why, cap, "libusb: %s%s", lu.error_name(r),
		         r == LU_ERROR_BUSY ? " (another program has it; Steam's GameCube support passes it on as a controller)" : "");
		lu.close(dev);
	}
	return NULL;
}

Link* open_link(char* why, size_t cap, bool* seen)
{
#ifdef __linux__
	// sysfs says whether it is plugged in; hidraw may not show it once libusb
	// has detached the kernel's driver (Steam reading it, say)
	*seen = usb_present();
	if (!*seen)
		return NULL;
	if (Link* link = open_hidraw(why, cap, seen))
		return link;
	char usbWhy[200] = "";
	Link* link       = open_libusb(usbWhy, sizeof usbWhy);
	if (!link && usbWhy[0])
		snprintf(why + strlen(why), cap - strlen(why), "%s%s", why[0] ? "; " : "", usbWhy);
	else if (!link && libusb_ready())
		snprintf(why + strlen(why), cap - strlen(why), "%slibusb could not open its USB device", why[0] ? "; " : "");
	return link;
#else
	Link* link = open_libusb(why, cap);
	*seen      = link || why[0];
	return link;
#endif
}

void* thread_main(void*);
void start_thread()
{
	pthread_t t;
	if (pthread_create(&t, NULL, thread_main, NULL) == 0)
		pthread_detach(t);
}
#endif

#if GCAD_WINDOWS || GCAD_POSIX
#if GCAD_WINDOWS
DWORD WINAPI thread_main(void*)
#else
void* thread_main(void*)
#endif
{
	bool told = false; // said why a plugged-in adapter can't be read
	for (;;) {
		char why[400] = "";
		bool seen     = false;
		if (Link* link = open_link(why, sizeof why, &seen)) {
			told = false;
			session(*link);
			delete link;
		} else if (seen && !told) {
			port_log("[pad] GameCube adapter plugged in but not readable: %s\n", why[0] ? why : "no way to open it");
#ifdef __linux__
			port_log("[pad] Let your user read it: the launcher can (Settings > Controls), or see the "
			         "port's README (GameCube adapters) for the udev rule\n");
#endif
			told = true;
		} else if (!seen) {
			told = false;
		}
		sleep_ms(1000);
	}
	return 0;
}
#endif

} // namespace

void gc_adapter_start()
{
#if GCAD_WINDOWS || GCAD_POSIX
	static bool started;
	if (started || getenv("SMS_NO_GC_ADAPTER"))
		return;
	started = true;
#if GCAD_WINDOWS
	if (!load())
		return;
#endif
	start_thread();
#endif
}

bool gc_adapter_read(GcAdapterInput* in)
{
	if (!g_state.present)
		return false;
	in->b1 = g_state.b1;
	in->b2 = g_state.b2;
	in->lx = g_state.lx;
	in->ly = g_state.ly;
	in->cx = g_state.cx;
	in->cy = g_state.cy;
	in->lt = g_state.lt;
	in->rt = g_state.rt;
	return true;
}

void gc_adapter_rumble(bool on) { g_state.rumble = on; }

void gc_adapter_flush_rumble()
{
	if (g_state.present)
		sleep_ms(30); // for the adapter's thread to send it
}

// USB ids SDL should leave alone (its SDL_HIDAPI_IGNORE_DEVICES form), or NULL
// when SDL keeps the adapter (no reader here, or SMS_NO_GC_ADAPTER=1).
extern "C" const char* port_gc_adapter_sdl_ignore(void)
{
#if GCAD_WINDOWS || GCAD_POSIX
	if (getenv("SMS_NO_GC_ADAPTER"))
		return NULL;
	static char list[64];
	list[0] = 0;
	for (int i = 0; i < id_count(); i++)
		snprintf(list + strlen(list), sizeof list - strlen(list), "%s0x%04x/0x%04x", i ? "," : "", g_ids[i].vid,
		         g_ids[i].pid);
	return list;
#else
	return NULL;
#endif
}
