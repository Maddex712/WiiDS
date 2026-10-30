using System.IO.Compression;

namespace BrawlRecompiled;

// "Add a mod": a mod's Wii download (a .zip, or the folder it was extracted to) has its build folder somewhere inside
// (rex_, Project+, P+Ex, ...): the folder holding the Gecko codes, RSBE01.GCT. That folder is copied onto the SD card,
// which is all the game needs; the download's apps, private and loader files are for a real Wii.
static class ModInstall
{
    // Copies the build into `sdDir` and returns its folder name. `progress` gets the files copied so far.
    public static string Install(string source, string sdDir, Action<int>? progress = null)
    {
        string? temp = null;
        try
        {
            var root = source;
            var ext = Path.GetExtension(source).ToLowerInvariant();
            if (File.Exists(source))
            {
                if (ext is ".rar" or ".7z")
                    throw new InvalidDataException("That's a " + ext + " archive. Extract it first (7-Zip or WinRAR), then choose the extracted folder.");
                if (ext != ".zip")
                    throw new InvalidDataException("Choose the mod's .zip download, or the folder it was extracted to.");
                temp = Path.Combine(Path.GetTempPath(), "BrawlRecompiled-mod-" + Guid.NewGuid().ToString("N"));
                ZipFile.ExtractToDirectory(source, temp);
                root = temp;
            }
            var build = FindBuild(root) ?? throw new InvalidDataException(
                "No Wii SD card build in there (no RSBE01.GCT). Use the mod's Wii / console download, not the Dolphin one.");
            var name = Path.GetFileName(build);
            var target = Path.Combine(sdDir, name);
            if (Path.GetFullPath(build).TrimEnd('\\', '/').Equals(Path.GetFullPath(target).TrimEnd('\\', '/'), StringComparison.OrdinalIgnoreCase))
                return name;  // already on the card
            int copied = 0;
            foreach (var file in Directory.EnumerateFiles(build, "*", SearchOption.AllDirectories))
            {
                var dest = Path.Combine(target, Path.GetRelativePath(build, file));
                Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                File.Copy(file, dest, true);
                progress?.Invoke(++copied);
            }
            return name;
        }
        finally
        {
            if (temp != null) try { Directory.Delete(temp, true); } catch { }
        }
    }

    // The shallowest folder holding RSBE01.GCT (the build), at most a few levels down.
    static string? FindBuild(string root)
    {
        var level = new List<string> { root };
        for (int depth = 0; depth < 5 && level.Count > 0; depth++)
        {
            foreach (var dir in level)
                if (Directory.EnumerateFiles(dir).Any(f => Path.GetFileName(f).Equals("RSBE01.GCT", StringComparison.OrdinalIgnoreCase)))
                    return dir;
            level = level.SelectMany(d => { try { return Directory.GetDirectories(d); } catch { return Array.Empty<string>(); } }).ToList();
        }
        return null;
    }
}
