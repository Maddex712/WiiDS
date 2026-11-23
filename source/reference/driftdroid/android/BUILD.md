# Build WiiDS 2.0

The Android project contains the frontend source, the Gradle wrapper, resources, shipped assets, and the prebuilt ARM64 game runtimes used by the release APK.

Requires JDK 17 or newer and Android SDK Platform 34. Point Gradle to your Android SDK via the environment or an untracked local.properties file.

```powershell
.\gradlew.bat :app:assembleRelease :app:lintVitalRelease -PthorBuild=true -PwiidsApp=true -PthorRetroRewind=true -PwiidsVersionName=2.0 -PwiidsVersionCode=2000
```

The output is app/build/outputs/apk/release/app-release.apk. Use the included signed APK to update a WiiDS installation. An independently built APK uses whichever signing key is configured on the build machine.

This directory includes the signed release's presentation inputs and prebuilt ARM64 runtimes. See the accompanying `source/` tree for original notices, build scripts and native changes. Nintendo game discs and private signing material are separate.
