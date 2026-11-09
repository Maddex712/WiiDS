// Fallback for builds where generated/guest_symbol_table.cpp has not been
// produced yet (generate-data-init emits the real table from the project's
// function map). An empty table simply disables crash-report symbolization.
#include <cstdint>

extern "C" {

// The extern declarations force external linkage: namespace-scope const
// objects are internal-linkage by default in C++, even inside extern "C".
extern const uint32_t kGuestMapSymbolCount;
extern const uint32_t kGuestMapSymbolAddresses[];
extern const char* const kGuestMapSymbolNames[];

const uint32_t kGuestMapSymbolCount = 0u;
const uint32_t kGuestMapSymbolAddresses[] = {0u};
const char* const kGuestMapSymbolNames[] = {""};

} // extern "C"
