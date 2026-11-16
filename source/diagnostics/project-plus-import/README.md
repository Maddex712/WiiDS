# Project+ importer tests

The Java host test creates synthetic FAT/GCT/ZIP fixtures; no game data is included. Compile `ProjectPlusImport.java`, `ProjectPlusFiles.java` and `ProjectPlusImportTest.java`, then run `dev.wiids.brawl.ProjectPlusImportTest` with JDK 17 or newer. Production Java sources also compile with `--release 8`. Set `java.io.tmpdir` to a disposable test directory. Symlink tests report a skip where the host lacks permission.

`ProjectPlusAndroidTest.java` runs against compiled WiiDS Android classes with JUnit 4.13.2 and Robolectric 4.14.1/API 33. It uses a synthetic document provider, not native game execution. JDK 21 needs `--add-opens=java.base/java.io=ALL-UNNAMED` for Robolectric's file-descriptor shadow. The matching prior integration tests supply the remaining catalog/save regression coverage.

413 Java assertions and 16 combined Android framework host tests passed for the preview. Physical import and gameplay acceptance remain pending.
