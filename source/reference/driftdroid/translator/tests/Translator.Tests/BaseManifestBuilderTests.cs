using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using Translator.Core.Mods;
using Xunit;

namespace Translator.Tests;

public sealed class BaseManifestBuilderTests
{
    [Fact]
    public void EmbeddedSwitchCaseTranslationDoesNotSplitOwningFunctionRange()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"translator-base-manifest-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            File.WriteAllText(
                Path.Combine(directory, "func_8062C3A4.cpp"),
                "extern \"C\" void func_8062C3A4() { goto loc_8062C64C; loc_8062C64C:; }");
            File.WriteAllText(
                Path.Combine(directory, "func_8062C64C.cpp"),
                "extern \"C\" void func_8062C64C() { loc_8062C64C:; }");
            File.WriteAllText(
                Path.Combine(directory, "func_80630094.cpp"),
                "extern \"C\" void func_80630094() { loc_80630094:; }");

            var starts = BaseManifestBuilder.DiscoverCanonicalGeneratedFunctionStarts(directory);

            Assert.Equal([0x8062C3A4u, 0x80630094u], starts);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void MetadataEmbeddedLabelDoesNotSplitOwningFunctionRange()
    {
        var metadata = BaseTranslationOutputMetadata.Create([
            new BaseTranslationFunctionMetadata("func_8062C3A4.cpp", 100, new string('a', 64), 0x8062C3A4u, [0x8062C64Cu]),
            new BaseTranslationFunctionMetadata("func_8062C64C.cpp", 40, new string('b', 64), 0x8062C64Cu, []),
            new BaseTranslationFunctionMetadata("func_80630094.cpp", 80, new string('c', 64), 0x80630094u, [])
        ], TranslationQualityMetadata.Clean);

        var starts = BaseManifestBuilder.DiscoverCanonicalGeneratedFunctionStarts(metadata);

        Assert.Equal([0x8062C3A4u, 0x80630094u], starts);
    }

    [Fact]
    public void MetadataUnlabelledInteriorBctrDoesNotCaptureSwitchCasePatches()
    {
        // Section::CreatePages' bctr at +0x34 is translated separately, but the
        // owning switch consumes it without a loc label. A label-only manifest
        // assigns RR's later li r4,25 patches to this unreachable alias overlay.
        var metadata = BaseTranslationOutputMetadata.Create([
            new BaseTranslationFunctionMetadata("func_8062C3A4.cpp", 100, new string('a', 64),
                0x8062C3A4u, [0x8062C64Cu, 0x80630080u]),
            new BaseTranslationFunctionMetadata("func_8062C3D8.cpp", 40, new string('b', 64),
                0x8062C3D8u, [], InteriorToOtherTranslation: true),
            new BaseTranslationFunctionMetadata("func_80630094.cpp", 80, new string('c', 64),
                0x80630094u, [])
        ], TranslationQualityMetadata.Clean);

        var starts = BaseManifestBuilder.DiscoverCanonicalGeneratedFunctionStarts(metadata);
        Assert.Equal([0x8062C3A4u, 0x80630094u], starts);
        var owner = new BaseFunctionIndex([
            new BaseFunctionRangeMetadata(starts[0], starts[1], "Section::CreatePages", ".text", 0,
                "analysis", ["Executable"])
        ]);
        foreach (var patchedInstruction in new[] { 0x8062C658u, 0x8062C79Cu, 0x8062C8E0u })
            Assert.Equal(0x8062C3A4u, owner.FindContaining(patchedInstruction)!.Start);
    }

    [Fact]
    public void OwnershipMetadataRejectsStaleGeneratedFunctionContents()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"translator-ownership-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            const string original = "original";
            var source = Path.Combine(directory, "func_80001000.cpp");
            var path = Path.Combine(directory, "base_translation_output.json");
            File.WriteAllText(source, original);
            var metadata = BaseTranslationOutputMetadata.Create([
                new BaseTranslationFunctionMetadata("func_80001000.cpp", original.Length,
                    Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(original))), 0x80001000u, [])
            ], TranslationQualityMetadata.Clean);
            BaseTranslationOutputMetadataFile.WriteIfChangedAtomic(path, metadata);
            Assert.Single(BaseTranslationOutputMetadataFile.ReadMatchingFunctions(path, directory).Functions);
            File.WriteAllText(source, "changed!"); // Same length must still fail the digest check.
            Assert.Throws<InvalidDataException>(() =>
                BaseTranslationOutputMetadataFile.ReadMatchingFunctions(path, directory));
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void OutputMetadataWriteIsDeterministicAndChangeAware()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"translator-output-metadata-{Guid.NewGuid():N}");
        var path = Path.Combine(directory, "base_output.json");
        var metadata = BaseTranslationOutputMetadata.Create([
            new BaseTranslationFunctionMetadata("b.cpp", 2, new string('b', 64), 2, []),
            new BaseTranslationFunctionMetadata("a.cpp", 1, new string('a', 64), 1, [3])
        ], TranslationQualityMetadata.Clean, "identity");
        try
        {
            Assert.True(BaseTranslationOutputMetadataFile.WriteIfChangedAtomic(path, metadata));
            var timestamp = File.GetLastWriteTimeUtc(path);
            Assert.False(BaseTranslationOutputMetadataFile.WriteIfChangedAtomic(path, metadata));
            Assert.Equal(timestamp, File.GetLastWriteTimeUtc(path));

            var roundTrip = BaseTranslationOutputMetadataFile.Read(path);
            Assert.Equal("a.cpp", roundTrip.Functions[0].RelativePath);
            Assert.Equal("identity", roundTrip.TranslationIdentityHash);
            Assert.Equal(TranslationQualityMetadata.Clean, roundTrip.Quality);
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    [Theory]
    [InlineData(1, 0)]
    [InlineData(0, 1)]
    public void ReleaseEligibilityRejectsTranslationQualityFailures(
        int unsupportedInstructionCount,
        int invalidSsaFunctionCount)
    {
        var metadata = BaseTranslationOutputMetadata.Create([
            new BaseTranslationFunctionMetadata(
                "func_80001000.cpp", 1, new string('a', 64), 0x80001000u, [])
        ], new TranslationQualityMetadata(unsupportedInstructionCount, invalidSsaFunctionCount));

        var error = Assert.Throws<InvalidDataException>(() =>
            metadata.RequireReleaseEligible("test metadata"));

        Assert.Contains("Release translation quality failure", error.Message, StringComparison.Ordinal);
    }
}
