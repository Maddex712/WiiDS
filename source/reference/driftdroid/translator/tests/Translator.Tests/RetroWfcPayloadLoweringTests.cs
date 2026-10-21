using System.Buffers.Binary;
using System.Text;
using Translator.Core.Mods;
using Translator.Core.Mods.Mkwii;
using Xunit;

namespace Translator.Tests;

public class RetroWfcPayloadLoweringTests
{
    [Fact]
    public void SharedPayloadDescriptorDrivesRelocationAndStaticLowering()
    {
        const uint moduleBase = 0x81700000u;
        const uint payloadModuleOffset = 0x2000u;
        var payload = BuildSharedPayloadFixture();

        var result = RetroWfcPayload.Parse(payload, TestManifest(), moduleBase, payloadModuleOffset, "fixture");

        Assert.Equal("RMCPD00", result.Summary.Game);
        Assert.Equal(3u, result.Summary.FormatVersion);
        Assert.Equal(moduleBase + payloadModuleOffset + 0x1E4u, result.Summary.InitializationTargetAddress);
        Assert.Equal(moduleBase + payloadModuleOffset + 0x210u, ReadU32(result.RelocatedImage, 0x1B4));
        Assert.Equal(moduleBase + payloadModuleOffset + 0x214u, ReadU32(result.RelocatedImage, 0x220));

        var callback = Assert.Single(result.Summary.InitializationCallbacks);
        Assert.Equal(moduleBase + payloadModuleOffset + 0x208u, callback.TargetAddress);

        var hook = Assert.Single(result.LoweringPlan.ExecutableHooks);
        Assert.Equal(moduleBase + payloadModuleOffset + 0x200u, hook.TargetAddress);
        Assert.Equal("moduleFunction", hook.TargetKind);

        var pointer = Assert.Single(result.LoweringPlan.StaticPointers);
        Assert.Equal(moduleBase + payloadModuleOffset + 0x204u, pointer.TargetAddress);
        Assert.Equal("moduleFunction", pointer.TargetKind);
    }

    private static BaseManifest TestManifest() =>
        new(
            "test",
            1,
            "RMCP01",
            "P",
            "",
            0,
            [
                new BaseSectionMetadata(".text", "main.dol", 0x80001000u, 0x80001020u, true, false, "base_text.bin", 0),
                new BaseSectionMetadata(".data", "main.dol", 0x80002000u, 0x80002020u, false, true, "base_data.bin", 0)
            ],
            [
                new BaseFunctionRangeMetadata(0x80001000u, 0x80001020u, "func_80001000", ".text", 0, "test", ["Executable"])
            ],
            "ranges.json");

    private static byte[] BuildSharedPayloadFixture()
    {
        var payload = new byte[0x240];
        Encoding.ASCII.GetBytes("WWFC/Payload").CopyTo(payload, 0);
        WriteU32(payload, 0x0C, (uint)payload.Length);
        WriteU32(payload, 0x130, 3);
        WriteU32(payload, 0x134, 1);
        Encoding.ASCII.GetBytes("RMCPD00").CopyTo(payload, 0x138);
        WriteU32(payload, 0x144, 0x00010000);
        WriteU32(payload, 0x148, 0x1B4);
        WriteU32(payload, 0x14C, 0x1B8);
        WriteU32(payload, 0x150, 0x1B8);
        WriteU32(payload, 0x154, 0x1BC);
        WriteU32(payload, 0x158, 0x1BC);
        WriteU32(payload, 0x15C, 0x1DC);
        WriteU32(payload, 0x1A0, 0x1A4);

        WriteU32(payload, 0x1A4, 1);
        WriteU32(payload, 0x1A8, 0x1E4);
        WriteU32(payload, 0x1AC, 0x1E4);
        WriteU32(payload, 0x1B0, 0x230);

        WriteU32(payload, 0x1B4, 0x210);
        WriteU32(payload, 0x1B8, 0x220);

        payload[0x1BC + 1] = 3;
        WriteU32(payload, 0x1BC + 4, 0x80001000);
        WriteU32(payload, 0x1BC + 8, 0x200);

        payload[0x1CC + 1] = 6;
        WriteU32(payload, 0x1CC + 4, 0x80002000);
        WriteU32(payload, 0x1CC + 8, 0x204);

        WriteU32(payload, 0x1DC, 0xFFFFFFFF);
        WriteU32(payload, 0x1E0, 0x208);
        WriteU32(payload, 0x220, 0x214);
        return payload;
    }

    private static uint ReadU32(byte[] image, int offset) =>
        BinaryPrimitives.ReadUInt32BigEndian(image.AsSpan(offset, 4));

    private static void WriteU32(byte[] image, int offset, uint value) =>
        BinaryPrimitives.WriteUInt32BigEndian(image.AsSpan(offset, 4), value);
}
