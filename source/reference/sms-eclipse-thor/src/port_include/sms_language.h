#ifndef SMS_LANGUAGE_H
#define SMS_LANGUAGE_H
#ifdef __cplusplus
extern "C" {
#endif
// The language chosen with `language` in settings.txt (SMS_LANGUAGE): "en"
// (the disc's own text, the default), or the code of an installed pack
// ("fr", "de", "es", "it", "pl", ...).
const char* port_language_code(void);
// The installed pack for that language (mods/languages/<code>), or NULL for
// English or when the pack is missing.
const char* port_language_pack_dir(void);
// 1 while a language pack is in use: the game's text and menu pictures come
// from it, and the code that lays them out allows for their lengths.
int port_language_active(void);
// The language as the game's own option numbers it (the memory card label's
// table): 0 English, 1 German, 2 French, 3 Spanish, 4 Italian.
int port_language_index(void);
#ifdef __cplusplus
}
#endif
#endif
