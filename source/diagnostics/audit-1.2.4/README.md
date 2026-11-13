# Hotfix parser checks

These C++ test sources ran against the hotfix native inputs. font_test.cpp exercises section bounds and accepts optional BRFNT paths from your own disc. config_test.cpp tests the runtime TOML parser wrapper. extraction_test.cpp includes the production disc reader and supports --cached-range, --decrypted-range, --tree-* fixtures and --real-plan <sys folder> <scratch sentinel>. review_mutations.cpp uses deterministic font/disc mutations; supply valid disc fixture paths from your own tests. memory_guard.cpp is a test-only 64 MiB allocation guard.

Build with C++17 and the same disc/art sources, compression libraries and toml11 dependency as WiiDS. AddressSanitizer was enabled in the host checks. Do not link the allocation guard into the app. No game assets, owner saves, signing files or device data are included. See docs/release/HOTFIX-1.2.4.md for test scope and limits.
