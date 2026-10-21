#include "abi_bridge.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "generated/RuntimeConfig.h"
#include "runtime_log.h"

// Nothing reads CpuContext by offset; these asserts just canary accidental field reordering
// (the order is a cache-locality choice explained at the struct definition).
static_assert(offsetof(CpuContext, gpr) == 0);
static_assert(offsetof(CpuContext, cr) == 128);
static_assert(offsetof(CpuContext, lr) == 132);
static_assert(offsetof(CpuContext, ctr) == 136);
static_assert(offsetof(CpuContext, xer) == 140);
static_assert(offsetof(CpuContext, fpscr) == 144);
static_assert(offsetof(CpuContext, pc) == 148);
static_assert(offsetof(CpuContext, fpr) == 152);
static_assert(offsetof(CpuContext, gqr) == 408);
static_assert(offsetof(CpuContext, hid0) == 440);
static_assert(offsetof(CpuContext, hid1) == 444);
static_assert(offsetof(CpuContext, hid2) == 448);
static_assert(offsetof(CpuContext, srr0) == 452);
static_assert(offsetof(CpuContext, srr1) == 456);
static_assert(offsetof(CpuContext, msr) == 460);
static_assert(sizeof(CpuContext) == 464);

namespace {
std::vector<TranslatedFunctionInfo>& Registry() {
    static std::vector<TranslatedFunctionInfo> entries;
    return entries;
}

size_t& PriorityOverrideCount() {
    static size_t count = 0;
    return count;
}

std::mutex& RegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<uint32_t, size_t>& AddressIndex() {
    static std::unordered_map<uint32_t, size_t> index;
    return index;
}

std::vector<RawDispatchRecord>& DynamicRawDispatchEntries() {
    static std::vector<RawDispatchRecord> entries;
    return entries;
}

const StaticIndirectDispatchTable*& GeneratedIndirectDispatchTable() {
    static const StaticIndirectDispatchTable* table = nullptr;
    return table;
}

// Every distinct table ever registered (via StaticIndirectDispatchTableRegistrar's static-init
// side effect) - a combined-library build links more than one product's table into the same
// process, each registering unconditionally at load time before any profile can be selected.
// GeneratedIndirectDispatchTable() above tracks which one is currently ACTIVE for dispatch;
// this tracks everything that's been registered, so TranslatedFunctionRegistry::SelectProfile can
// switch between them later.
std::vector<const StaticIndirectDispatchTable*>& GeneratedIndirectDispatchTables() {
    static std::vector<const StaticIndirectDispatchTable*> tables;
    return tables;
}

std::string& SelectedProfileName() {
    static std::string profile;
    return profile;
}

std::unordered_map<uint32_t, std::vector<size_t>>& AddressEntries() {
    static std::unordered_map<uint32_t, std::vector<size_t>> entries;
    return entries;
}

struct HostEntryRef {
    uintptr_t addr = 0;
    size_t index = 0;
};

std::vector<HostEntryRef>& HostIndex() {
    static std::vector<HostEntryRef> index;
    return index;
}

std::atomic_bool& RegistryFrozen() {
    static std::atomic_bool frozen{false};
    return frozen;
}

const char* KindLabel(FunctionKind kind) {
    switch (kind) {
    case FunctionKind::BaseTranslated:
        return "base translated";
    case FunctionKind::ModTranslated:
        return "mod translated";
    case FunctionKind::Native:
        return "native";
    }
    return "unknown";
}

uint32_t DefaultPriorityForKind(FunctionKind kind) {
    switch (kind) {
    case FunctionKind::BaseTranslated:
        return kBaseTranslatedFunctionPriority;
    case FunctionKind::ModTranslated:
        return kModTranslatedFunctionPriorityBase;
    case FunctionKind::Native:
        return kNativeFunctionPriority;
    }
    return 0;
}

uint32_t EffectivePriority(const TranslatedFunctionInfo& info) {
    return info.priority != 0 ? info.priority : DefaultPriorityForKind(info.kind);
}

uint32_t KindTieBreaker(FunctionKind kind) {
    switch (kind) {
    case FunctionKind::BaseTranslated:
        return 0;
    case FunctionKind::ModTranslated:
        return 1;
    case FunctionKind::Native:
        return 2;
    }
    return 0;
}

// A registration whose profileName doesn't match the currently selected profile is INELIGIBLE to
// win AddressIndex() at all, regardless of priority - not just deprioritized. FindByAddressPtr
// (used by InvokeIndirectCpu and other dispatch helpers - abi_bridge.h) resolves straight off
// AddressIndex(), bypassing the per-profile generated dispatch table entirely, so without this a
// combined build would silently execute the wrong profile's compiled body wherever two profiles
// both happen to register something at the same address under the same FunctionKind (this isn't
// limited to ModTranslated - a profile's own recompiled copy of otherwise-"shared" logic is
// BaseTranslated too; see the closure/symbol-aliasing steps in TranslatedBuildShardEmitter.cs
// that make sure those get distinct profileName-tagged registrations instead of silently
// colliding). An entry with no profileName at all (single-profile builds, which never call
// TranslatedFunctionRegistry::SelectProfile) is always eligible.
bool IsWinnerEligible(const TranslatedFunctionInfo& info) {
    return !info.profileName || SelectedProfileName().empty() ||
           SelectedProfileName() == info.profileName;
}

bool IsBetterCandidate(const TranslatedFunctionInfo& candidate, const TranslatedFunctionInfo& current) {
    const uint32_t candidatePriority = EffectivePriority(candidate);
    const uint32_t currentPriority = EffectivePriority(current);
    if (candidatePriority != currentPriority) {
        return candidatePriority > currentPriority;
    }

    const uint32_t candidateKindRank = KindTieBreaker(candidate.kind);
    const uint32_t currentKindRank = KindTieBreaker(current.kind);
    if (candidateKindRank != currentKindRank) {
        return candidateKindRank > currentKindRank;
    }

    return candidate.moduleId > current.moduleId;
}

bool IsSameRegistration(const TranslatedFunctionInfo& a, const TranslatedFunctionInfo& b) {
    return a.address == b.address &&
           a.kind == b.kind &&
           EffectivePriority(a) == EffectivePriority(b) &&
           a.moduleId == b.moduleId &&
           std::string_view(a.profileName ? a.profileName : "") ==
               std::string_view(b.profileName ? b.profileName : "");
}

void RebuildIndicesLocked() {
    auto& entries = Registry();
    auto& addrIndex = AddressIndex();
    auto& addressEntries = AddressEntries();
    auto& rawDispatchEntries = DynamicRawDispatchEntries();
    auto& hostIndex = HostIndex();

    addrIndex.clear();
    addressEntries.clear();
    rawDispatchEntries.clear();
    hostIndex.clear();
    addrIndex.reserve(entries.size());
    addressEntries.reserve(entries.size());

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        addressEntries[entry.address].push_back(i);
        // IsWinnerEligible must gate even the "first entry seen for this address" case, not just
        // the IsBetterCandidate tie-break - otherwise a registration belonging to a profile that
        // isn't currently selected would still win by simply being the only candidate, even
        // though it must be treated as absent under the active profile.
        auto addressIt = addrIndex.find(entry.address);
        if (IsWinnerEligible(entry) &&
            (addressIt == addrIndex.end() || IsBetterCandidate(entry, entries[addressIt->second]))) {
            addrIndex[entry.address] = i;
        }
        if (entry.entryPoint) {
            hostIndex.push_back({reinterpret_cast<uintptr_t>(entry.entryPoint), i});
        }
    }

    std::sort(hostIndex.begin(), hostIndex.end(), [](const HostEntryRef& a, const HostEntryRef& b) {
        return a.addr < b.addr;
    });

    const bool translatedWinnersAreGenerated = GeneratedIndirectDispatchTable() != nullptr;
    rawDispatchEntries.reserve(translatedWinnersAreGenerated ? 256u : addrIndex.size());
    for (const auto& [address, index] : addrIndex) {
        const auto& entry = entries[index];
        if (!entry.rawCpuInvoker || !entry.mustRemainDynamicallyDispatchable) {
            continue;
        }
        if (translatedWinnersAreGenerated && entry.kind != FunctionKind::Native) {
            continue;
        }
        rawDispatchEntries.push_back(RawDispatchRecord{
            .address = address,
            .entry = entry.rawCpuInvoker,
            .nonvolatileFprWriteMask = NonvolatileFprGuardMaskFor(&entry),
            .preserveNonvolatileGprs = ShouldPreserveNonvolatileGprsForRawCpuCall(&entry),
        });
    }
    std::sort(rawDispatchEntries.begin(), rawDispatchEntries.end(),
              [](const RawDispatchRecord& a, const RawDispatchRecord& b) {
                  return a.address < b.address;
              });
}

std::string ValidateGeneratedIndirectDispatchLocked() {
    const auto* table = GeneratedIndirectDispatchTable();
    if (!table) {
        return {};
    }
    if (!table->profileName || !table->segments || !table->entries || table->entryCount == 0) {
        return "Invalid generated indirect dispatch table";
    }

    uint32_t previousAddress = 0;
    for (size_t i = 0; i < table->entryCount; ++i) {
        const auto& record = table->entries[i];
        if ((record.address & 3u) != 0 || !record.entry ||
            (i != 0 && record.address <= previousAddress) ||
            FindStaticIndirectDispatchEntry(table, record.address) != &record) {
            std::ostringstream message;
            message << "Malformed generated indirect dispatch entry " << i
                    << " for profile '" << table->profileName << "'";
            return message.str();
        }
        previousAddress = record.address;

        const auto winner = AddressIndex().find(record.address);
        if (winner == AddressIndex().end()) {
            std::ostringstream message;
            message << "Generated indirect dispatch target 0x" << std::hex << record.address
                    << " is not registered for profile '" << table->profileName << "'";
            return message.str();
        }
        const auto& info = Registry()[winner->second];
        if (info.kind == FunctionKind::Native || info.rawCpuInvoker != record.entry ||
            NonvolatileFprGuardMaskFor(&info) != record.nonvolatileFprWriteMask ||
            record.preserveNonvolatileGprs) {
            std::ostringstream message;
            message << "Stale generated indirect dispatch winner at 0x" << std::hex
                    << record.address << " for profile '" << table->profileName << "'";
            return message.str();
        }
    }

    // AddressIndex() is already profile-scoped by this point (RebuildIndicesLocked only lets a
    // registration win if its profileName matches the currently selected profile - see
    // IsWinnerEligible), so every winner seen here already belongs to this table's own profile.
    for (const auto& [address, index] : AddressIndex()) {
        const auto& info = Registry()[index];
        if ((info.kind == FunctionKind::BaseTranslated || info.kind == FunctionKind::ModTranslated) &&
            info.rawCpuInvoker && !FindStaticIndirectDispatchEntry(table, address)) {
            std::ostringstream message;
            message << "Generated indirect dispatch profile '" << table->profileName
                    << "' omits translated winner 0x" << std::hex << address;
            return message.str();
        }
    }
    return {};
}
}

void RegisterStaticIndirectDispatchTable(const StaticIndirectDispatchTable* table) {
    std::lock_guard<std::mutex> lock(RegistryMutex());
    if (RegistryFrozen().load(std::memory_order_acquire)) {
        RT_LOG(RT_TAG_RUNTIME) << "ERROR: Generated indirect dispatch table registered after finalization" << std::endl;
        ShowRuntimeFatalPopup("translated dispatch initialization failed",
                              "A generated indirect-dispatch table was registered after the function registry was finalized.");
        std::abort();
    }
    if (!table || !table->profileName || table->profileName[0] == '\0') {
        RT_LOG(RT_TAG_RUNTIME) << "ERROR: Generated indirect dispatch table has no profile" << std::endl;
        ShowRuntimeFatalPopup("translated dispatch initialization failed",
                              "A generated indirect-dispatch table was linked with no profile name.");
        std::abort();
    }
    auto& tables = GeneratedIndirectDispatchTables();
    const auto duplicate = std::find_if(tables.begin(), tables.end(), [&](const auto* candidate) {
        return std::string_view(candidate->profileName) == table->profileName;
    });
    if (duplicate != tables.end() && *duplicate != table) {
        RT_LOG(RT_TAG_RUNTIME) << "ERROR: Duplicate generated indirect dispatch profile '"
                               << table->profileName << "'" << std::endl;
        ShowRuntimeFatalPopup("translated dispatch initialization failed",
                              "Two different generated indirect-dispatch tables were linked under the same profile name.");
        std::abort();
    }
    if (duplicate == tables.end()) {
        tables.push_back(table);
    }
    // A combined-library build links more than one product's table into this process, each
    // registering unconditionally at load time before any profile can be selected - so once a
    // second distinct profile shows up, there's no safe default to auto-publish anymore.
    // TranslatedFunctionRegistry::SelectProfile (called from RuntimeProduct::SetActive - see
    // combined_product.cpp) must explicitly choose one before any guest code runs. A
    // single-profile build (libWiiCompiled.so/libRetroRewind.so) never has a second table, so
    // this keeps auto-publishing the only one there ever is, unchanged from before.
    if (tables.size() == 1) {
        GeneratedIndirectDispatchTable() = table;
        SelectedProfileName() = table->profileName;
        // Mirror into the header-visible atomic under the same lock, so the
        // inlined miss path and this owning static can never disagree.
        g_publishedStaticIndirectDispatchTable.store(table, std::memory_order_release);
    } else {
        GeneratedIndirectDispatchTable() = nullptr;
        SelectedProfileName().clear();
        g_publishedStaticIndirectDispatchTable.store(nullptr, std::memory_order_release);
    }
}

void RegisterBulkTranslatedFunctions(const char* profileName,
                                     const BulkTranslatedFunctionRecord* records,
                                     size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const auto& record = records[i];
        TranslatedFunctionInfo info;
        info.address = record.address;
        info.name = record.name ? record.name : "";
        info.moduleId = record.moduleId;
        info.priority = record.priority;
        info.nonvolatileFprWriteMask = record.preservesNonvolatileFprs
            ? 0u
            : (record.nonvolatileFprWriteMask & kPpcAllNonvolatileFprMask);
        info.entryPoint = reinterpret_cast<void*>(record.entry);
        info.rawCpuInvoker = record.entry;
        info.mustRemainDynamicallyDispatchable = record.mustRemainDynamicallyDispatchable;
        info.kind = record.kind;
        info.profileName = profileName;
        TranslatedFunctionRegistry::Register(std::move(info));
    }
}

void TranslatedFunctionRegistry::SelectProfile(const char* profileName) {
    if (!profileName || profileName[0] == '\0') {
        RT_LOG(RT_TAG_RUNTIME) << "ERROR: Indirect dispatch profile name is empty" << std::endl;
        ShowRuntimeFatalPopup("translated dispatch initialization failed",
                              "An empty profile name was passed to TranslatedFunctionRegistry::SelectProfile.");
        std::abort();
    }
    std::lock_guard<std::mutex> lock(RegistryMutex());
    if (RegistryFrozen().load(std::memory_order_acquire)) {
        RT_LOG(RT_TAG_RUNTIME) << "ERROR: Indirect dispatch profile selected after finalization" << std::endl;
        ShowRuntimeFatalPopup("translated dispatch initialization failed",
                              "The active indirect-dispatch profile was selected after the function registry was finalized.");
        std::abort();
    }
    const auto& tables = GeneratedIndirectDispatchTables();
    const auto selected = std::find_if(tables.begin(), tables.end(), [&](const auto* table) {
        return table && table->profileName && std::string_view(table->profileName) == profileName;
    });
    if (selected == tables.end()) {
        RT_LOG(RT_TAG_RUNTIME) << "ERROR: No registered indirect dispatch table matches profile '"
                                << profileName << "'" << std::endl;
        ShowRuntimeFatalPopup("translated dispatch initialization failed",
                              "No linked generated indirect-dispatch table matches the requested active profile.");
        std::abort();
    }
    SelectedProfileName() = profileName;
    GeneratedIndirectDispatchTable() = *selected;
    g_publishedStaticIndirectDispatchTable.store(*selected, std::memory_order_release);
    RebuildIndicesLocked();
    RT_LOG(RT_TAG_RUNTIME) << "Selected translated function profile '" << profileName << "'" << std::endl;
}

void TranslatedFunctionRegistry::Register(TranslatedFunctionInfo info) {
    std::lock_guard<std::mutex> lock(RegistryMutex());
    if (RegistryFrozen().load(std::memory_order_acquire)) {
        RT_LOG(RT_TAG_RUNTIME) << "ERROR: Attempted to register function after registry was finalized: 0x"
                  << std::hex << info.address << " '" << info.name << "'" << std::dec << std::endl;
        ShowRuntimeFatalPopup("translated function registration failed",
                              "A translated function was registered after guest execution had already been finalized.");
        std::abort();
    }
    info.priority = EffectivePriority(info);

    auto& entries = Registry();
    auto& addrIndex = AddressIndex();
    auto& addressEntries = AddressEntries();

    auto sameAddressIt = addressEntries.find(info.address);
    if (sameAddressIt != addressEntries.end()) {
        for (const size_t existingIndex : sameAddressIt->second) {
            const auto& existing = entries[existingIndex];
            if (IsSameRegistration(existing, info)) {
                RT_LOG(RT_TAG_RUNTIME) << "Duplicate " << KindLabel(info.kind) << " registration for " << info.name << " (0x"
                          << std::hex << info.address << ") ignored" << std::dec << std::endl;
                return;
            }
        }
    }

    entries.push_back(std::move(info));
    const size_t index = entries.size() - 1;
    addressEntries[entries[index].address].push_back(index);
    auto bestAddressIt = addrIndex.find(entries[index].address);
    if (bestAddressIt == addrIndex.end() || IsBetterCandidate(entries[index], entries[bestAddressIt->second])) {
        if (bestAddressIt != addrIndex.end()) {
            // Expected on every mod entry - a Retro Rewind boot has thousands.
            // Counted here and reported once by Finalize instead of per address.
            ++PriorityOverrideCount();
        }
        addrIndex[entries[index].address] = index;
    }
    if (entries[index].entryPoint) {
        HostIndex().push_back({reinterpret_cast<uintptr_t>(entries[index].entryPoint), index});
    }
}

void TranslatedFunctionRegistry::Finalize() {
    if (const size_t overrides = PriorityOverrideCount(); overrides != 0) {
        RT_LOG(RT_TAG_RUNTIME) << overrides
                  << " registration(s) took over an address by priority" << std::endl;
    }
    std::string validationError;
    {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        // Publication is one-way: cached pointers into the rebuilt vectors
        // remain valid for the lifetime of the process.
        if (RegistryFrozen().load(std::memory_order_acquire)) {
            return;
        }
        RebuildIndicesLocked();
        validationError = ValidateGeneratedIndirectDispatchLocked();
        if (validationError.empty()) {
            // Duplicate-registration tracking is initialization-only. AddressIndex
            // owns every published winner after this point, and Register rejects
            // later writes, so release the per-address candidate vectors before
            // the game starts.
            auto& addressEntries = AddressEntries();
            addressEntries.clear();
            addressEntries.rehash(0);
            RegistryFrozen().store(true, std::memory_order_release);
            lookupPublished_.store(true, std::memory_order_release);
        }
    }
    if (!validationError.empty()) {
        throw std::runtime_error("[" RT_TAG_RUNTIME "] " + validationError);
    }
}

const TranslatedFunctionInfo* TranslatedFunctionRegistry::FindByAddressPtrSlow(uint32_t address) {
    if (RegistryFrozen().load(std::memory_order_acquire)) {
        const auto it = AddressIndex().find(address);
        return it != AddressIndex().end() ? &Registry()[it->second] : nullptr;
    }

    std::lock_guard<std::mutex> lock(RegistryMutex());
    auto it = AddressIndex().find(address);
    if (it != AddressIndex().end()) {
        return &Registry()[it->second];
    }
    for (auto& entry : Registry()) {
        if (entry.address == address) {
            return &entry;
        }
    }
    return nullptr;
}

const RawDispatchRecord* TranslatedFunctionRegistry::FindRawByAddressPtrSlow(uint32_t address) {
    if (!RegistryFrozen().load(std::memory_order_acquire)) {
        return nullptr;
    }
    if (const auto* generated = FindStaticIndirectDispatchEntry(GeneratedIndirectDispatchTable(), address)) {
        return generated;
    }
    const auto& entries = DynamicRawDispatchEntries();
    const auto it = std::lower_bound(entries.begin(), entries.end(), address,
        [](const RawDispatchRecord& entry, uint32_t target) {
            return entry.address < target;
        });
    return it != entries.end() && it->address == address ? &*it : nullptr;
}

std::optional<TranslatedFunctionInfo> TranslatedFunctionRegistry::FindByHostAddress(uintptr_t hostAddr) {
    const bool frozen = RegistryFrozen().load(std::memory_order_acquire);
    if (!frozen) {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        RebuildIndicesLocked();
    }

    const auto& entries = Registry();
    const auto& sorted = HostIndex();
    if (sorted.empty()) {
        return std::nullopt;
    }

    auto it = std::upper_bound(sorted.begin(), sorted.end(), hostAddr,
                               [](uintptr_t addr, const HostEntryRef& ref) { return addr < ref.addr; });

    if (it == sorted.begin()) {
        return std::nullopt;
    }
    --it;

    uintptr_t funcStart = it->addr;
    uintptr_t funcEnd;
    auto nextIt = it + 1;
    if (nextIt != sorted.end()) {
        funcEnd = nextIt->addr;
    } else {
        funcEnd = funcStart + 256 * 1024;
    }

    if (hostAddr >= funcStart && hostAddr < funcEnd) {
        return entries[it->index];
    }
    return std::nullopt;
}

std::optional<TranslatedFunctionInfo> TranslatedFunctionRegistry::FindNearestByAddress(uint32_t address) {
    std::optional<TranslatedFunctionInfo> best;
    uint32_t bestDelta = UINT32_MAX;
    const auto scan = [&] {
        for (const auto& info : Registry()) {
            if (address < info.address) {
                continue;
            }
            const uint32_t delta = address - info.address;
            if (delta < bestDelta) {
                bestDelta = delta;
                best = info;
            }
        }
    };
    if (RegistryFrozen().load(std::memory_order_acquire)) {
        scan();
    } else {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        scan();
    }
    return best;
}

namespace {
CpuContext g_persistentCpu{};
}

CpuContext& GetPersistentCpuContext() {
    return g_persistentCpu;
}

void InitializePersistentCpuContext() {
    // Idempotent by contract: generated mod code calls this again before each of
    // its initializers, after the boot path has already established r1 and live
    // guest state in the persistent context. Never clear the context here; only
    // seed the PowerPC ABI environmental registers if no one has yet:
    // r2 = _SDA2_BASE_ (read-only small data), r13 = _SDA_BASE_.
    if (g_persistentCpu.gpr[2] == 0) {
        g_persistentCpu.gpr[2] = RuntimeConfig::SDA2_BASE;
    }
    if (g_persistentCpu.gpr[13] == 0) {
        g_persistentCpu.gpr[13] = RuntimeConfig::SDA1_BASE;
    }
}

// See MKW_VERIFY_PRESERVED_GPR in abi_bridge.h. Only verification builds of the translated code
// ever call this; it reports the first few distinct sites and keeps counting.
extern "C" void RuntimeReportPreservedGprMismatch(unsigned index, uint32_t expected, uint32_t actual,
                                                   const char* function, uint32_t lr) noexcept {
    static std::atomic<uint64_t> total{0};
    const uint64_t count = total.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 200 || (count & (count - 1)) == 0) {
        std::fprintf(stderr,
                     "[verify-residency] #%llu r%u not preserved across a call in %s (lr=0x%08X): local=0x%08X ctx=0x%08X\n",
                     static_cast<unsigned long long>(count), index, function != nullptr ? function : "?", lr,
                     expected, actual);
        std::fflush(stderr);
    }
}
