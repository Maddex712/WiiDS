using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using Translator.Core.Loading;

namespace Translator.Core.IO;

internal sealed record AssemblyBlob(string FileName, string Symbol, ReadOnlyMemory<byte> Data, string Comment);

internal static class AssemblyBlobWriter
{
    public static void Write(
        string assemblyPath,
        string blobDirectory,
        string blobReferenceDirectory,
        IReadOnlyList<AssemblyBlob> blobs,
        params string[] headerLines)
    {
        Directory.CreateDirectory(blobDirectory);
        var expectedFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var assembly = new StringBuilder();
        foreach (var header in headerLines) assembly.AppendLine(header);
        // .S is preprocessed by the target compiler. A Windows-hosted translator may
        // generate data for Android, so choose COFF/ELF syntax from the compilation
        // target rather than the translator's host OS. %progbits also works on ARM.
        assembly.AppendLine("#if defined(_WIN32)");
        assembly.AppendLine(".section .rdata,\"dr\"");
        assembly.AppendLine("#else");
        assembly.AppendLine(".section .rodata,\"a\",%progbits");
        assembly.AppendLine("#endif");
        assembly.AppendLine();

        foreach (var blob in blobs)
        {
            var blobPath = Path.Combine(blobDirectory, blob.FileName);
            expectedFiles.Add(Path.GetFullPath(blobPath));
            FileOutput.WriteBytesIfChanged(blobPath, blob.Data.Span);
            var hash = ChecksumUtilities.Sha256Hex(blob.Data.Span);
            assembly.AppendLine($"// {blob.Comment}; sha256={hash}");
            assembly.AppendLine(".p2align 4");
            assembly.AppendLine($".globl {blob.Symbol}");
            assembly.AppendLine($"{blob.Symbol}:");
            var referencePath = Path.Combine(blobReferenceDirectory, blob.FileName);
            assembly.AppendLine($".incbin \"{SanitizeAssemblyPath(referencePath)}\"");
            assembly.AppendLine();
        }

        foreach (var stalePath in Directory.EnumerateFiles(blobDirectory, "*.bin"))
        {
            if (!expectedFiles.Contains(Path.GetFullPath(stalePath))) File.Delete(stalePath);
        }
        // The data payload never requires an executable stack on ELF targets.
        assembly.AppendLine("#if !defined(_WIN32)");
        assembly.AppendLine(".section .note.GNU-stack,\"\",%progbits");
        assembly.AppendLine("#endif");
        FileOutput.WriteTextIfChanged(assemblyPath, assembly.ToString());
    }

    private static string SanitizeAssemblyPath(string path) => Path.GetFullPath(path).Replace('\\', '/');
}
