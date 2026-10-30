using System.Diagnostics;
using WiiCompiled.Setup.Common;

namespace WiiCompiled.Setup.Windows;

/// <summary>
/// Refuses to replace installed products while one of them is running: publishing renames the
/// product directories, which Windows rejects while a process runs from them.
/// </summary>
internal static class RunningProductGuard
{
    public static void EnsureProductsNotRunning(string installDirectory)
    {
        var installation = new Installation(installDirectory);
        var running = FindProcessesUnder(installation.BaseDirectory, installation.RetroDirectory);
        if (running.Count == 0) return;

        throw new InvalidOperationException(
            $"Mario Kart Wii is still running ({string.Join(", ", running)}). " +
            "Close the game, then retry the update.");
    }

    private static List<string> FindProcessesUnder(params string[] roots)
    {
        var names = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var process in Process.GetProcesses())
        {
            try
            {
                if (process.Id == Environment.ProcessId) continue;
                var path = process.MainModule?.FileName;
                if (path is null) continue;
                if (roots.Any(root => FileSystemUtilities.PathContains(root, Path.GetFullPath(path))))
                    names.Add(Path.GetFileName(path));
            }
            catch
            {
                // Inaccessible processes (elevated, exited, protected) cannot run our products' exes
                // from a user-writable install directory in any case that matters here.
            }
            finally
            {
                process.Dispose();
            }
        }
        return names.ToList();
    }
}
