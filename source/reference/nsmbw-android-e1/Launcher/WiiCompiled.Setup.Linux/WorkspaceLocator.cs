namespace WiiCompiled.Setup.Linux;

/// <summary>
/// Finds the repo checkout this tool is running from by walking up from its own directory looking
/// for Launcher/local-build.sh - this tool operates directly on a git checkout (no bundled/staged
/// workspace copy), so there is no installed "Toolkit" layout to anchor on the way the Windows
/// installer's Installation.cs does.
/// </summary>
internal static class WorkspaceLocator
{
    private const int MaxSearchDepth = 6;

    public static string FindFrom(string startDirectory)
    {
        var current = new DirectoryInfo(startDirectory);
        for (var level = 0; level <= MaxSearchDepth && current is not null; level++, current = current.Parent)
        {
            if (File.Exists(Path.Combine(current.FullName, "Launcher", "local-build.sh")))
            {
                return current.FullName;
            }
        }
        throw new InvalidOperationException(
            "Could not find the WiiCompiled repository (looked for Launcher/local-build.sh walking up " +
            $"from {startDirectory}). Pass --workspace <path-to-checkout> explicitly.");
    }
}
