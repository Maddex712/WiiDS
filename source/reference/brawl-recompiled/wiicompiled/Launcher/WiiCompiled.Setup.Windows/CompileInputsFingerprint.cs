using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using WiiCompiled.Setup.Common;

namespace WiiCompiled.Setup.Windows;

internal sealed record RetroRewindCompileInputs(
    string RetroRewindRoot,
    string CodePulSha256,
    string CompileInputsSha256);

internal static class CompileInputsFingerprint
{
    private const string FormatVersion = "mkwc-retro-compile-inputs-v3";

    /// <summary>The exact directory name a build snapshot uses, because it is the mod root name
    /// the translator turns into the product's relative DVD overlay root.</summary>
    public const string PackageDirectoryName = "RetroRewind6";

    public static RetroRewindCompileInputs Compute(string selectedDirectory,
        CancellationToken cancellationToken = default)
    {
        var root = RetroRewindSource.ResolveRetroRewind6(selectedDirectory);
        return new RetroRewindCompileInputs(root, ComputeCodePulSha256(root, cancellationToken),
            ComputeCompileInputsSha256(root, cancellationToken));
    }

    public static string ComputeCodePulSha256(string retroRewindRoot,
        CancellationToken cancellationToken = default)
    {
        var codePul = CodePulPath(Path.GetFullPath(retroRewindRoot));
        if (!File.Exists(codePul)) throw MissingCodePul(retroRewindRoot);
        cancellationToken.ThrowIfCancellationRequested();
        return InputValidation.Sha256File(codePul);
    }


    /// Hashes only the translator inputs without relying on timestamps.
    public static string ComputeCompileInputsSha256(string retroRewindRoot,
        CancellationToken cancellationToken = default)
    {
        var root = Path.GetFullPath(retroRewindRoot);
        if (!Directory.Exists(root))
            throw new DirectoryNotFoundException($"The Retro Rewind folder is missing: {root}");
        var codePul = CodePulPath(root);
        if (!File.Exists(codePul)) throw MissingCodePul(root);

        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendField(hash, FormatVersion);
        AppendFile(hash, root, codePul, required: true, cancellationToken);

        AppendTopology(hash, "topology:mod-root", root);
        AppendTopology(hash, "topology:files", Path.Combine(root, "files"));

        return Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    /// <summary>
    /// Copies just the compile inputs of a Wheel Wizard-owned Retro Rewind folder into
    /// operation-owned scratch and proves the copy is a faithful snapshot. This is the Code.pul
    /// </summary>
    public static RetroRewindCompileInputs Snapshot(string selectedDirectory, string scratchDirectory,
        CancellationToken cancellationToken = default)
    {
        var source = Compute(selectedDirectory, cancellationToken);
        var destination = Path.Combine(Path.GetFullPath(scratchDirectory), PackageDirectoryName);
        if (Directory.Exists(destination) || File.Exists(destination))
            throw new IOException($"The compile-input snapshot destination already exists: {destination}");

        CopyCompileInputs(source.RetroRewindRoot, destination, cancellationToken);
        var snapshot = new RetroRewindCompileInputs(destination,
            ComputeCodePulSha256(destination, cancellationToken),
            ComputeCompileInputsSha256(destination, cancellationToken));
        // A snapshot that hashes exactly like its source is that source; anything else means the
        // folder was being written while it was read, which is the frontend's lease to hold.
        if (!snapshot.CodePulSha256.Equals(source.CodePulSha256, StringComparison.OrdinalIgnoreCase) ||
            !snapshot.CompileInputsSha256.Equals(source.CompileInputsSha256,
                StringComparison.OrdinalIgnoreCase))
        {
            throw new IOException(
                "The Retro Rewind compile inputs changed while they were being copied. " +
                "Wait for its update to finish and retry.");
        }
        return snapshot;
    }

    private static void CopyCompileInputs(string root, string destination,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(destination);
        CopyInput(root, destination, CodePulPath(root), required: true, cancellationToken);
        ReproduceTopology(root, destination, "files", cancellationToken);
    }

    private static void CopyInput(string root, string destination, string path, bool required,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!File.Exists(path))
        {
            if (required) throw MissingCompileInput(path);
            return;
        }
        RejectLink(path);
        var output = Path.Combine(destination, RelativePath(root, path)
            .Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        File.Copy(path, output, overwrite: true);
    }


    private static void ReproduceTopology(string root, string destination, string relative,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var source = Path.Combine(root, relative);
        if (!File.Exists(source) && !Directory.Exists(source)) return;
        RejectLink(source);
        var output = Path.Combine(destination, relative);
        if (Directory.Exists(source)) Directory.CreateDirectory(output);
        else File.Copy(source, output, overwrite: true);
    }

    private static void AppendFile(IncrementalHash hash, string root, string path, bool required,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var relative = RelativePath(root, path);
        if (!File.Exists(path))
        {
            if (required) throw MissingCompileInput(path);
            AppendField(hash, "file:" + relative);
            AppendField(hash, "absent");
            return;
        }

        RejectLink(path);
        AppendField(hash, "file:" + relative);
        AppendField(hash, InputValidation.Sha256File(path));
    }

    private static void AppendTopology(IncrementalHash hash, string name, string path)
    {
        AppendField(hash, name);
        if (!File.Exists(path) && !Directory.Exists(path))
        {
            AppendField(hash, "absent");
            return;
        }

        var attributes = File.GetAttributes(path);
        var kind = Directory.Exists(path) ? "directory" : "file";
        if ((attributes & FileAttributes.ReparsePoint) != 0) kind += "+link";
        AppendField(hash, kind);
    }

    private static void AppendField(IncrementalHash hash, string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        Span<byte> length = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(length, bytes.Length);
        hash.AppendData(length);
        hash.AppendData(bytes);
    }

    private static string RelativePath(string root, string path) =>
        Path.GetRelativePath(root, path).Replace('\\', '/');

    private static string CodePulPath(string root) => Path.Combine(root, "Binaries", "Code.pul");

    private static void RejectLink(string path)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException(
                $"The Retro Rewind compile inputs contain a link instead of a regular entry: {path}");
    }

    private static InvalidDataException MissingCompileInput(string path) =>
        new($"The Retro Rewind compile input is missing: {path}");

    private static InvalidDataException MissingCodePul(string retroRewindRoot) =>
        new($"The Retro Rewind folder is missing Binaries\\Code.pul: {retroRewindRoot}");
}
