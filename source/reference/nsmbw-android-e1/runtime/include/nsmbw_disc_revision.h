#pragma once

#include <cstdint>

// E1 is the upstream default. Thor E2 must opt in at compile time and translate
// the matching owner-supplied E2 DOL/REL inputs with recomp-e2.yml.
#if defined(NSMBW_DISC_REVISION_E2) && NSMBW_DISC_REVISION_E2
inline constexpr uint32_t kNsmbwDiscRevision = 2;
inline constexpr uint32_t NsmbwRevisionAddress(uint32_t e1, uint32_t e2) { return e2; }
#else
inline constexpr uint32_t kNsmbwDiscRevision = 1;
inline constexpr uint32_t NsmbwRevisionAddress(uint32_t e1, uint32_t e2) { return e1; }
#endif
