#pragma once
#if defined(__ANDROID__)
// Called on the guest thread at PADRead; UI reads only the copied snapshot.
void AndroidPublishRaceMap() noexcept;
// Read-only course identity from a successfully resolved/read archive, never a retail slot.
void AndroidRaceMapCourseRead(const char* dvdPath, const char* hostPath) noexcept;
#endif
