# Disc metadata backend

The isolated clean-E1 runtime now has a read-only `/dev/di` metadata adapter.
Normal game-file reads continue through the existing DVD filesystem backend.
The new adapter supports these startup requests:

| Request | Actual input | Returned bytes |
| --- | --- | --- |
| Disc ID | `game/sys/boot.bin` | First 32 bytes, unchanged |
| BCA | `game/disc/bca.bin` | Exactly 64 supplied bytes, unchanged |

Opening the device publishes a real reserved descriptor to the SDK's handle.
IOS open/close/scalar ioctl route to this adapter; unsupported vector operations
are rejected. Closed descriptors remain recognized so they cannot reach NAND's
generic success fallback. Async completion retains the runtime's callback queue
and the previously tested pre-scheduler delivery behavior.

The adapter requires a valid, exactly 32-byte command buffer and validates the
complete supplied output range. Reads finish into temporary storage before
touching guest memory. Missing, malformed or unreadable metadata returns an error
with the output unchanged. Only the returned payload bytes are written on success.
There is no replacement BCA pattern, successful-result override or game-check patch.

`stage_disc.py --revision 1 --disc <DATA> --serial <Thor> --bca <actual-file>`
can copy an existing 64-byte owner-provided sidecar verbatim. It includes its actual
hash in the same staging manifest and device verification. Omitting `--bca` does
not create a file. Existing installed data is never silently overwritten.

This is a metadata adapter, not a complete physical drive, partition-authentication
or raw-sector implementation. The owner's ISO had no BCA sidecar. An external owner-supplied file was later
staged unchanged and native boot observed (see E1_INPUT_AUDIT.md). This code does not reconstruct missing optical-drive data from the executable or ISO.

Actual-source ARM64 tests passed 55 checks, including file and memory error paths,
verbatim data transport, descriptor lifecycle and output preservation. Tests use
arbitrary fixture bytes only in a temporary shell-test directory, never in app data
or the real game checker. Temporary files were removed and absence verified.
Receipt: `build/nsmbw-thor/disc-interface-test/verification.json`.

A successful original-game boot does not follow from these tests. See the final
clean-E1 native/boot receipts before making any playable-port claim.
