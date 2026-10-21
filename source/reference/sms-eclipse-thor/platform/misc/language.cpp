// Language packs: `language = fr` in settings.txt (SMS_LANGUAGE) plays the
// game in that language with the pack installed in mods/languages/<code>/
// (tools/mods/get.py language <code>, or SMS Launcher). Any code with a pack
// installed works, so a new language needs only its pack; the list below
// adds other spellings and the game's own numbering of its five. A pack holds
// a language's text and pictures (the European release's, or a community
// translation's), laid out as game file mods: files/
// replaces whole disc files, and a folder named after a disc archive
// (files/data/scene/bianco0.szs/map/message.bmg) replaces files inside it
// (platform/dvd/dvd.cpp). English, the default, uses the disc unchanged.
#include <sms_language.h>
#include "port_platform.h"
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <strings.h>
#include <sys/stat.h>

namespace {
struct Language {
	const char* code;
	int index; // the game's own numbering (port_language_index)
	const char* names[4];
};
// Codes, then the other spellings `language` accepts.
const Language kLanguages[] = {
	{ "en", 0, { "english", "eng", nullptr, nullptr } },
	{ "de", 1, { "german", "deutsch", "ge", "ger" } },
	{ "fr", 2, { "french", "francais", "français", "fra" } },
	{ "es", 3, { "spanish", "espanol", "español", "sp" } },
	{ "it", 4, { "italian", "italiano", "ita", nullptr } },
};

const Language* chosen()
{
	static const Language* lang;
	static bool done;
	if (done)
		return lang;
	done = true;
	lang = &kLanguages[0];
	const char* e = getenv("SMS_LANGUAGE");
	if (!e || !*e)
		return lang;
	for (const Language& l : kLanguages) {
		bool match = !strcasecmp(e, l.code);
		for (const char* n : l.names)
			match = match || (n && !strcasecmp(e, n));
		if (match) {
			lang = &l;
			return lang;
		}
	}
	// another language: a pack's folder name (letters, digits, - and _)
	static Language other;
	static char code[16];
	size_t n = strlen(e);
	bool ok  = n > 0 && n < sizeof code;
	for (size_t i = 0; ok && i < n; i++)
		ok = isalnum((unsigned char)e[i]) || e[i] == '-' || e[i] == '_';
	if (!ok) {
		port_log("[language] \"%s\" is not a language code; playing in English\n", e);
		return lang;
	}
	for (size_t i = 0; i <= n; i++)
		code[i] = (char)tolower((unsigned char)e[i]);
	other = { code, 0, { nullptr, nullptr, nullptr, nullptr } }; // its memory card label stays English
	lang  = &other;
	return lang;
}

bool is_dir(const std::string& p)
{
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
} // namespace

extern "C" const char* port_language_code(void) { return chosen()->code; }

extern "C" const char* port_language_pack_dir(void)
{
	static std::string dir;
	static bool done;
	if (done)
		return dir.empty() ? nullptr : dir.c_str();
	done = true;
	const Language* l = chosen();
	if (!strcmp(l->code, "en"))
		return nullptr;
	for (const char* base : { "mods/languages/", "../../mods/languages/" }) {
		std::string d = std::string(base) + l->code;
		if (is_dir(d + "/files")) {
			dir = d;
			return dir.c_str();
		}
	}
	port_log("[language] %s: no pack in mods/languages/%s (install it with tools/mods/get.py language %s); "
	         "playing in English\n",
	         l->code, l->code, l->code);
	return nullptr;
}

extern "C" int port_language_active(void) { return port_language_pack_dir() != nullptr; }

extern "C" int port_language_index(void) { return port_language_active() ? chosen()->index : 0; }
