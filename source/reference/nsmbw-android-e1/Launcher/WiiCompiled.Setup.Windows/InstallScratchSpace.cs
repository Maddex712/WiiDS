using WiiCompiled.Setup.Common;

namespace WiiCompiled.Setup.Windows;

/// <summary>
/// Owns one temporary directory for an install operation. The name carries the installation's scope
/// id, so the next locked operation can remove trees left by forced process termination without
/// touching scratch belonging to another installation.
/// </summary>
internal sealed class InstallScratchSpace : IDisposable
{
    private readonly IInstallReporter? _reporter;
    private bool _disposed;

    private InstallScratchSpace(string root, IInstallReporter? reporter)
    {
        Root = root;
        _reporter = reporter;
    }

    public string Root { get; }

    /// <summary>Creates staging beside the installation, suitable even when the install is absent.</summary>
    public static InstallScratchSpace CreateSibling(string installDirectory, IInstallReporter? reporter = null) =>
        Create(InstallOperationPaths.GetParent(InstallOperationPaths.NormalizeRoot(installDirectory)),
            installDirectory, reporter);

    /// <summary>Creates build/update output inside an existing installation.</summary>
    public static InstallScratchSpace CreateInsideInstall(string installDirectory,
        IInstallReporter? reporter = null)
    {
        var installRoot = InstallOperationPaths.NormalizeRoot(installDirectory);
        if (!Directory.Exists(installRoot))
            throw new DirectoryNotFoundException(
                "An in-install scratch directory requires an existing installation root.");
        return Create(installRoot, installDirectory, reporter);
    }

    /// <summary>Deletes scratch left by an interrupted operation. Call only while locked.</summary>
    public static void Recover(string installDirectory, IInstallReporter? reporter = null)
    {
        var installRoot = InstallOperationPaths.NormalizeRoot(installDirectory);
        foreach (var container in new[] { InstallOperationPaths.GetParent(installRoot), installRoot })
        {
            if (!Directory.Exists(container)) continue;
            foreach (var leftover in Directory.EnumerateDirectories(container, Pattern(installRoot)))
            {
                reporter?.Diagnostic("Removing scratch space from an interrupted operation: " + leftover);
                FileSystemUtilities.DeleteDirectoryIfExists(leftover);
            }
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try
        {
            FileSystemUtilities.DeleteDirectoryIfExists(Root);
        }
        catch (Exception ex)
        {
            // The next locked operation removes this exact tree. A transient AV/file-indexer handle
            // must not turn an already committed installation into a reported failure.
            _reporter?.Diagnostic("Scratch cleanup is pending and will be retried: " + ex.Message);
        }
    }

    private static InstallScratchSpace Create(string container, string installDirectory,
        IInstallReporter? reporter)
    {
        var installRoot = InstallOperationPaths.NormalizeRoot(installDirectory);
        var root = Path.Combine(container,
            $".mkwc-scratch-{InstallOperationPaths.GetScopeId(installRoot)}-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        reporter?.Diagnostic("Prepared exact update scratch space at " + root);
        return new InstallScratchSpace(root, reporter);
    }

    private static string Pattern(string installRoot) =>
        $".mkwc-scratch-{InstallOperationPaths.GetScopeId(installRoot)}-*";
}
