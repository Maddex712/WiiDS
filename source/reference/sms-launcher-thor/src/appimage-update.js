'use strict';

// Launcher updates on Linux. Players add the AppImage to Steam (Add a
// Non-Steam Game, as on a Steam Deck) or to their desktop by its path.
// electron-updater replaces the running AppImage, but when the file's name has
// a version in it (SMS-Launcher-0.1.45.AppImage, as releases were named) it
// deletes that file and puts the update under the new release's name, which
// breaks those shortcuts. Here every update goes over the file the player
// started, under its own name.
const fs = require('node:fs');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');

// Puts the downloaded AppImage at appImage. A rename swaps it in at once,
// which works while the old one runs. From another disk (an SD card), it is
// copied beside the old one first, so a failed copy (a full disk) leaves the
// player's launcher as it was.
function replaceAppImage(downloaded, appImage, files = fs) {
  files.chmodSync(downloaded, 0o755);
  try {
    files.renameSync(downloaded, appImage);
    return;
  } catch (error) {
    if (error.code !== 'EXDEV') throw error;
  }
  const next = `${appImage}.update`;
  try {
    files.copyFileSync(downloaded, next);
    files.chmodSync(next, 0o755);
    files.renameSync(next, appImage);
  } catch (error) {
    files.rmSync(next, { force: true });
    throw error;
  }
  files.rmSync(downloaded, { force: true });
}

// Makes electron-updater's AppImage updater install over the AppImage the
// player started (env.APPIMAGE). It installs through doInstall, both from
// quitAndInstall and when the launcher quits with an update downloaded; the
// new AppImage then starts the way electron-updater starts it.
function keepAppImagePath(updater, { env = process.env, run = spawn, runSync = execFileSync, files = fs } = {}) {
  updater.doInstall = function doInstall(options) {
    const appImage = env.APPIMAGE;
    const downloaded = this.installerPath;
    if (!appImage || !path.isAbsolute(appImage) || appImage.includes('\0') || !downloaded)
      throw new Error('There is no AppImage to update.');
    replaceAppImage(downloaded, appImage, files);
    const childEnv = { ...env, APPIMAGE_SILENT_INSTALL: 'true' };
    if (options && options.isForceRunAfter) run(appImage, [], { detached: true, stdio: 'ignore', env: childEnv }).unref();
    else runSync(appImage, [], { env: { ...childEnv, APPIMAGE_EXIT_AFTER_INSTALL: 'true' } });
    return true;
  };
}

module.exports = { replaceAppImage, keepAppImagePath };
