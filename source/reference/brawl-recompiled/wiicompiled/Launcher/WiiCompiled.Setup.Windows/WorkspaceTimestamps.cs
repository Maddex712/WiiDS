namespace WiiCompiled.Setup.Windows;


internal static class WorkspaceTimestamps
{
    public static void MarkChangedFiles(string installedWorkspace, string stagedWorkspace,
        Action<string> diagnostic, CancellationToken cancellationToken = default)
    {
        if (!Directory.Exists(installedWorkspace) || !Directory.Exists(stagedWorkspace)) return;
        installedWorkspace = Path.GetFullPath(installedWorkspace);
        stagedWorkspace = Path.GetFullPath(stagedWorkspace);

        var stampUtc = DateTime.UtcNow;
        var changed = 0;
        var unchanged = 0;
        foreach (var stagedFile in Directory.EnumerateFiles(stagedWorkspace, "*",
                     SearchOption.AllDirectories))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var relative = Path.GetRelativePath(stagedWorkspace, stagedFile);
            var installedFile = Path.Combine(installedWorkspace, relative);
            if (FileUnchanged(installedFile, stagedFile))
            {
                // Clang validates PCH dependency mtimes by equality, so an unchanged file must
                // keep the exact timestamp the previous build recorded, not the normalized one.
                InheritTimestamp(installedFile, stagedFile);
                unchanged++;
                continue;
            }
            File.SetLastWriteTimeUtc(stagedFile, stampUtc);
            changed++;
        }
        diagnostic($"Marked {changed} changed workspace file(s) for recompilation; " +
                   $"{unchanged} unchanged file(s) keep the incremental build cache valid.");
    }

    private static void InheritTimestamp(string installedFile, string stagedFile)
    {
        try
        {
            File.SetLastWriteTimeUtc(stagedFile, File.GetLastWriteTimeUtc(installedFile));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
    }

    private static bool FileUnchanged(string installedFile, string stagedFile)
    {
        try
        {
            if (!File.Exists(installedFile)) return false;
            using var installed = File.OpenRead(installedFile);
            using var staged = File.OpenRead(stagedFile);
            if (installed.Length != staged.Length) return false;
            var installedBuffer = new byte[81920];
            var stagedBuffer = new byte[81920];
            while (true)
            {
                var installedRead = installed.ReadAtLeast(installedBuffer, installedBuffer.Length,
                    throwOnEndOfStream: false);
                var stagedRead = staged.ReadAtLeast(stagedBuffer, stagedBuffer.Length,
                    throwOnEndOfStream: false);
                if (installedRead != stagedRead) return false;
                if (installedRead == 0) return true;
                if (!installedBuffer.AsSpan(0, installedRead)
                        .SequenceEqual(stagedBuffer.AsSpan(0, stagedRead)))
                    return false;
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }
}
