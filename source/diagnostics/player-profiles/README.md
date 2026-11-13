# Player profile regression checks

Compile KartPlayerDataTest.java with production SaveArchive.java and KartPlayerData.java using JDK 17 or newer. Run KartPlayerDataTest with a writable scratch directory as its only argument. The test creates synthetic saves and Miis in a new subdirectory and needs no game or player files. Checks cover exact byte preservation, checksums, stale/corrupt refusal, linked identities, matching ratings, transactional backups and restore.
