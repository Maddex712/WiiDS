using System.Security.Cryptography;
using System.Text;
using WiiCompiled.Setup.Common;

namespace WiiCompiled.Setup.Windows;

/// <summary>
/// A fail-fast, cross-process lock covering install, repair and launch operations for one install
/// root. The lock file lives beside the installation, so replacing the installation cannot replace
/// or release the lock that protects it.
/// </summary>
internal sealed class InstallOperationLock : IDisposable
{
    private readonly FileStream _stream;

    private InstallOperationLock(FileStream stream) => _stream = stream;

    public string LockPath => _stream.Name;

    public static InstallOperationLock Acquire(string installDirectory, IInstallReporter? reporter = null)
    {
        var root = InstallOperationPaths.NormalizeRoot(installDirectory);
        var parent = InstallOperationPaths.GetParent(root);
        Directory.CreateDirectory(parent);
        var path = InstallOperationPaths.GetLockPath(root);

        FileStream stream;
        try
        {
            stream = new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None,
                bufferSize: 1, FileOptions.WriteThrough);
        }
        catch (IOException ex)
        {
            throw new InvalidOperationException(
                "Another WiiCompiled operation is already using this installation. " +
                "Wait for the current game, install, or repair operation to finish.", ex);
        }

        try
        {
            InstallTransaction.Recover(root, reporter);
            InstallScratchSpace.Recover(root, reporter);
            return new InstallOperationLock(stream);
        }
        catch
        {
            stream.Dispose();
            throw;
        }
    }

    public void Dispose() => _stream.Dispose();
}

/// <summary>Deterministic paths shared by the operation lock and its recovery journal.</summary>
internal static class InstallOperationPaths
{
    public static string NormalizeRoot(string installDirectory)
    {
        if (string.IsNullOrWhiteSpace(installDirectory))
            throw new ArgumentException("The installation directory is required.", nameof(installDirectory));
        return FileSystemUtilities.NormalizePath(installDirectory);
    }

    public static string GetParent(string normalizedRoot) =>
        Directory.GetParent(normalizedRoot)?.FullName
        ?? throw new InvalidOperationException("The installation directory must have a parent directory.");


    public static string GetScopeId(string normalizedRoot) =>
        HashId(16, normalizedRoot.ToUpperInvariant());

    public static string HashId(int hexLength, params string[] canonicalParts) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(string.Join('\n', canonicalParts))))
            .ToLowerInvariant()[..hexLength];

    public static string GetLockPath(string normalizedRoot) =>
        Path.Combine(GetParent(normalizedRoot), $".mkwc-operation-{GetScopeId(normalizedRoot)}.lock");

    public static string GetJournalPath(string normalizedRoot) =>
        Path.Combine(GetParent(normalizedRoot), $".mkwc-transaction-{GetScopeId(normalizedRoot)}.json");

    public static string GetWorkRoot(string normalizedRoot, Guid transactionId) =>
        Path.Combine(GetParent(normalizedRoot),
            $".mkwc-transaction-{GetScopeId(normalizedRoot)}-{transactionId:N}");
}
