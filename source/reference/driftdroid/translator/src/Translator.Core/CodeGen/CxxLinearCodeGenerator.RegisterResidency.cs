using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Translator.Core.Analysis;
using Translator.Core.Ir;

namespace Translator.Core.CodeGen;

public sealed partial class CxxLinearCodeGenerator
{
    /// <summary>
    /// Guest register residency for one emitted function body: C++ locals are authoritative between call boundaries (host regs), synced via flush/reload at each one.
    /// Emitted twice, a recording pass then the real pass, so any register reference outside the recorded set fails loud instead of silently reading stale <c>ctx-&gt;</c> state.
    /// </summary>
    private sealed class RegisterResidency
    {
        private readonly bool _recording;
        private readonly SortedSet<int> _gprs = new();
        private readonly SortedSet<int> _fprs = new();
        private readonly SortedSet<int> _writtenGprs = new();
        private readonly SortedSet<int> _writtenFprs = new();
        private bool _cr;
        private bool _ctr;
        private bool _xer;
        private bool _writesCr;
        private bool _writesCtr;
        private bool _writesXer;

        private RegisterResidency(bool recording) => _recording = recording;

        // ---- Dirty tracking -------------------------------------------------------------------------
        // A register is "clean" while its local is known to equal its CpuContext slot: after the entry
        // load, after a boundary published it, after a boundary read it back. A flush only has to
        // store the dirty ones. The recording pass logs, per block, every local write and every
        // boundary sync in emission order; the block-entry states are then solved over the CFG and the
        // real pass replays the same events from those states. Everything errs towards "dirty":
        // a register written anywhere in an instruction's emission is dirty again when that
        // instruction ends, and a flush or reload inside a conditional arm cleans nothing.
        private const byte SpecialCr = 1;
        private const byte SpecialCtr = 2;
        private const byte SpecialXer = 4;

        private readonly record struct RegisterBits(uint Gpr, uint Fpr, byte Special)
        {
            public static RegisterBits All => new(uint.MaxValue, uint.MaxValue, byte.MaxValue);
            public RegisterBits Union(RegisterBits other) =>
                new(Gpr | other.Gpr, Fpr | other.Fpr, (byte)(Special | other.Special));
            public RegisterBits Except(RegisterBits other) =>
                new(Gpr & ~other.Gpr, Fpr & ~other.Fpr, (byte)(Special & ~other.Special));
        }

        // Instruction carries the GPRs that instruction definitely writes (from the IR); Reload
        // marks a boundary reload whose GPR set liveness may narrow.
        // Flush marks any flush, boundary or not, with the registers its mask covers.
        private enum DirtyEventKind : byte { Instruction, Write, Clean, Read, Reload, Flush }

        private readonly record struct DirtyEvent(DirtyEventKind Kind, RegisterBits Bits);

        // Shared between the recorder and its snapshots: written by the former, read by the latter.
        private Dictionary<string, List<DirtyEvent>> _blockEvents = new(StringComparer.Ordinal);
        private Dictionary<string, RegisterBits>? _blockEntryDirty;
        // Per block, per boundary reload in emission order: the GPRs whose locals are read again
        // before being overwritten. Only those need reading back; the others stay stale, which is
        // sound because a stale local is never dirty (so never published) until it is rewritten.
        private Dictionary<string, uint[]>? _blockReloadLiveGprs;
        private uint _entryLiveGprs = uint.MaxValue;
        private int _reloadOrdinal;
        private List<DirtyEvent>? _currentEvents;
        private string? _currentBlock;
        private int _eventCursor;
        private RegisterBits _dirty = RegisterBits.All;
        private RegisterBits _instructionWrites;
        // Locals written before the first block (function prologue code): dirty on entry.
        private RegisterBits _prologueWrites;

        /// <summary>Starts one emission of the function body.</summary>
        public void BeginBody()
        {
            if (_recording)
            {
                _blockEvents.Clear();
                _prologueWrites = default;
            }

            _currentEvents = null;
            _currentBlock = null;
            _dirty = RegisterBits.All;
            _instructionWrites = default;
        }

        public void BeginBlock(string label)
        {
            EndBlock();
            _currentBlock = label;
            _eventCursor = 0;
            _reloadOrdinal = 0;
            _instructionWrites = default;
            if (_recording)
            {
                _currentEvents = new List<DirtyEvent>();
                _blockEvents[label] = _currentEvents;
                return;
            }

            _currentEvents = _blockEvents.GetValueOrDefault(label);
            _dirty = _blockEntryDirty is not null && _blockEntryDirty.TryGetValue(label, out var entry)
                ? entry
                : RegisterBits.All;
        }

        public void EndBlock()
        {
            if (_recording || _currentBlock is null || _currentEvents is null) return;
            if (_eventCursor != _currentEvents.Count)
            {
                throw new InvalidOperationException(
                    $"Register residency replayed {_eventCursor} sync events for block {_currentBlock}, recorded {_currentEvents.Count}.");
            }
        }

        /// <param name="definiteGprWrites">GPRs the IR instruction writes on every path through it.</param>
        /// <param name="irGprReads">GPRs the IR instruction reads, added to the reads the emitter records.</param>
        public void BeginInstruction(uint definiteGprWrites = 0, uint irGprReads = 0) =>
            Note(new DirtyEvent(DirtyEventKind.Instruction, new RegisterBits(definiteGprWrites, irGprReads, 0)));

        private void NoteRead(RegisterBits bits) => Note(new DirtyEvent(DirtyEventKind.Read, bits));

        private void NoteWrite(RegisterBits bits) => Note(new DirtyEvent(DirtyEventKind.Write, bits));

        private void NoteClean(RegisterBits bits) => Note(new DirtyEvent(DirtyEventKind.Clean, bits));

        private void Note(DirtyEvent dirtyEvent)
        {
            if (_currentBlock is null)
            {
                if (_recording && dirtyEvent.Kind == DirtyEventKind.Write)
                    _prologueWrites = _prologueWrites.Union(dirtyEvent.Bits);
                return;
            }

            if (_recording)
            {
                _currentEvents!.Add(dirtyEvent);
                return;
            }

            // The real pass must walk exactly the events the solved states were computed from.
            if (_currentEvents is null || _eventCursor >= _currentEvents.Count ||
                _currentEvents[_eventCursor] != dirtyEvent)
            {
                throw new InvalidOperationException(
                    $"Register residency sync event {_eventCursor} of block {_currentBlock} differs from the recorded pass.");
            }

            ++_eventCursor;
            Apply(dirtyEvent, ref _dirty, ref _instructionWrites);
        }

        private static void Apply(DirtyEvent dirtyEvent, ref RegisterBits dirty, ref RegisterBits instructionWrites)
        {
            switch (dirtyEvent.Kind)
            {
                case DirtyEventKind.Instruction:
                    dirty = dirty.Union(instructionWrites);
                    instructionWrites = default;
                    break;
                case DirtyEventKind.Write:
                    dirty = dirty.Union(dirtyEvent.Bits);
                    instructionWrites = instructionWrites.Union(dirtyEvent.Bits);
                    break;
                case DirtyEventKind.Clean:
                    dirty = dirty.Except(dirtyEvent.Bits);
                    break;
                case DirtyEventKind.Read:
                case DirtyEventKind.Reload:
                case DirtyEventKind.Flush:
                    break;
            }
        }

        /// <summary>
        /// Solves the block-entry dirty states from the recorded events. The entry block starts clean
        /// (every local was just loaded); a block nothing in the CFG leads to starts fully dirty.
        /// </summary>
        public void SolveDirtyFlow(
            IReadOnlyList<string> blockOrder,
            string entryLabel,
            Func<string, IEnumerable<string>> successors)
        {
            var predecessors = new Dictionary<string, List<string>>(StringComparer.Ordinal);
            foreach (var label in blockOrder) predecessors[label] = new List<string>();
            foreach (var label in blockOrder)
            {
                foreach (var successor in successors(label))
                {
                    if (predecessors.TryGetValue(successor, out var list)) list.Add(label);
                }
            }

            var entryStates = new Dictionary<string, RegisterBits>(StringComparer.Ordinal);
            var exitStates = new Dictionary<string, RegisterBits>(StringComparer.Ordinal);
            foreach (var label in blockOrder)
            {
                entryStates[label] = label == entryLabel
                    ? _prologueWrites
                    : predecessors[label].Count != 0 ? default : RegisterBits.All;
                exitStates[label] = default;
            }

            bool changed;
            do
            {
                changed = false;
                foreach (var label in blockOrder)
                {
                    var incoming = entryStates[label];
                    foreach (var predecessor in predecessors[label])
                        incoming = incoming.Union(exitStates[predecessor]);
                    var outgoing = incoming;
                    var instructionWrites = default(RegisterBits);
                    if (_blockEvents.TryGetValue(label, out var events))
                    {
                        foreach (var dirtyEvent in events) Apply(dirtyEvent, ref outgoing, ref instructionWrites);
                    }

                    outgoing = outgoing.Union(instructionWrites);
                    if (incoming != entryStates[label] || outgoing != exitStates[label])
                    {
                        entryStates[label] = incoming;
                        exitStates[label] = outgoing;
                        changed = true;
                    }
                }
            } while (changed);

            _blockEntryDirty = entryStates;
            SolveGprLiveness(blockOrder, entryLabel, successors);
        }

        private bool IsDirtyGpr(int index) => ((_dirty.Gpr | _instructionWrites.Gpr) & (1u << index)) != 0;
        private bool IsDirtyFpr(int index) => ((_dirty.Fpr | _instructionWrites.Fpr) & (1u << index)) != 0;
        private bool IsDirtySpecial(byte bit) => ((_dirty.Special | _instructionWrites.Special) & bit) != 0;

        private static RegisterBits FlushBits(ResidencyBoundarySync sync) => new(
            sync.FlushGprMask,
            sync.FlushFprMask,
            (byte)((sync.FlushesCr ? SpecialCr : 0) | (sync.FlushesCtr ? SpecialCtr : 0) |
                   (sync.FlushesXer ? SpecialXer : 0)));

        private static RegisterBits ReloadBits(ResidencyBoundarySync sync) => new(
            sync.ReloadGprMask,
            sync.ReloadFprMask,
            (byte)((sync.ReloadsCr ? SpecialCr : 0) | (sync.ReloadsCtr ? SpecialCtr : 0) |
                   (sync.ReloadsXer ? SpecialXer : 0)));

        /// <summary>
        /// A flush every path through the current instruction executes: what it publishes is clean
        /// afterwards. Use the plain <see cref="AppendFlush(StringBuilder, string, ResidencyBoundarySync)"/>
        /// for one inside a conditional arm or in front of a return.
        /// </summary>
        public void AppendBoundaryFlush(StringBuilder sb, string pad, ResidencyBoundarySync sync)
        {
            AppendFlush(sb, pad, sync);
            NoteClean(FlushBits(sync));
        }

        /// <summary>
        /// A reload every path through the current instruction executes. GPRs that are dead at this
        /// point (overwritten before any later read of the local) are not read back.
        /// </summary>
        public void AppendBoundaryReload(StringBuilder sb, string pad, ResidencyBoundarySync sync)
        {
            var liveGprs = uint.MaxValue;
            if (!_recording && _currentBlock is not null && _blockReloadLiveGprs is not null &&
                _blockReloadLiveGprs.TryGetValue(_currentBlock, out var perReload) &&
                _reloadOrdinal < perReload.Length)
            {
                liveGprs = perReload[_reloadOrdinal];
            }

            ++_reloadOrdinal;
            Note(new DirtyEvent(DirtyEventKind.Reload, default));
            AppendReload(sb, pad, sync with { ReloadGprMask = sync.ReloadGprMask & liveGprs });
            if (VerifyResidency && !_recording)
            {
                foreach (var gpr in _gprs)
                {
                    var bit = 1u << gpr;
                    if ((sync.ReloadGprMask & bit) != 0)
                    {
                        // Not read back because nothing reads it again: poison it, so a wrong
                        // liveness answer shows up as a wild value instead of a quiet stale one.
                        if ((liveGprs & bit) == 0)
                            sb.Append(pad).Append(GprLocal(gpr)).Append(" = 0xDEAD0000u | ").Append(gpr).AppendLine("u;");
                    }
                    else if ((GuestAbiContractAnalyzer.NonvolatileGprMask & bit) != 0 &&
                             (liveGprs & bit) != 0 && !IsDirtyGpr(gpr))
                    {
                        // Trusted to come back unchanged, and published before the call.
                        sb.Append(pad).Append("MKW_VERIFY_PRESERVED_GPR(ctx, ").Append(gpr).Append(", ")
                            .Append(GprLocal(gpr)).AppendLine(");");
                    }
                }
            }

            NoteClean(ReloadBits(sync));
        }

        /// <summary>
        /// MKW_TRANSLATOR_VERIFY_RESIDENCY=1 makes the generated code check itself: see
        /// MKW_VERIFY_PRESERVED_GPR in runtime/include/abi_bridge.h. For test builds only.
        /// </summary>
        private static readonly bool VerifyResidency =
            Environment.GetEnvironmentVariable("MKW_TRANSLATOR_VERIFY_RESIDENCY") == "1";

        /// <summary>
        /// Backward liveness of the GPR locals over the recorded events, one IR instruction at a
        /// time: every read recorded anywhere in an instruction counts as a use, and only a register
        /// the IR says the instruction always writes, and that the emitter did write, is a
        /// definition. A reload's live set also includes its own instruction's reads, since
        /// expression text may be formatted before the reload it ends up after.
        /// Two more things count as uses, because a stale local must never reach CpuContext: a
        /// write that is not such a definition (it marks the register dirty without proving the
        /// local was assigned), and a flush of a register that may be dirty at that point (on
        /// another path into it, say), which would publish the local.
        /// </summary>
        private void SolveGprLiveness(
            IReadOnlyList<string> blockOrder,
            string entryLabel,
            Func<string, IEnumerable<string>> successors)
        {
            var liveIn = new Dictionary<string, uint>(StringComparer.Ordinal);
            foreach (var label in blockOrder) liveIn[label] = 0;

            // Dirty GPRs in front of every flush event, replayed from the solved block-entry states.
            var flushDirty = new Dictionary<string, uint[]>(StringComparer.Ordinal);
            foreach (var label in blockOrder)
            {
                if (!_blockEvents.TryGetValue(label, out var events)) continue;
                var dirty = _blockEntryDirty is not null && _blockEntryDirty.TryGetValue(label, out var entry)
                    ? entry
                    : RegisterBits.All;
                var instructionWrites = default(RegisterBits);
                var perEvent = new uint[events.Count];
                for (var index = 0; index < events.Count; ++index)
                {
                    if (events[index].Kind == DirtyEventKind.Flush)
                        perEvent[index] = dirty.Gpr | instructionWrites.Gpr;
                    Apply(events[index], ref dirty, ref instructionWrites);
                }

                flushDirty[label] = perEvent;
            }

            uint Walk(string label, uint liveOut, uint[]? reloadLive)
            {
                if (!_blockEvents.TryGetValue(label, out var events)) return liveOut;
                var dirtyAtFlush = flushDirty[label];
                var live = liveOut;
                var reloadIndex = 0;
                if (reloadLive is not null)
                {
                    foreach (var dirtyEvent in events)
                        if (dirtyEvent.Kind == DirtyEventKind.Reload) ++reloadIndex;
                }

                // One instruction's events run from its Instruction marker to the next one.
                var end = events.Count;
                while (end > 0)
                {
                    var start = end - 1;
                    while (start > 0 && events[start].Kind != DirtyEventKind.Instruction) --start;
                    uint uses = 0, writes = 0, definite = 0;
                    var reloads = 0;
                    for (var index = start; index < end; ++index)
                    {
                        var dirtyEvent = events[index];
                        switch (dirtyEvent.Kind)
                        {
                            case DirtyEventKind.Instruction:
                                // The marker reuses the bit pair: Gpr = definite writes, Fpr = IR reads.
                                definite = dirtyEvent.Bits.Gpr;
                                uses |= dirtyEvent.Bits.Fpr;
                                break;
                            case DirtyEventKind.Read: uses |= dirtyEvent.Bits.Gpr; break;
                            case DirtyEventKind.Write: writes |= dirtyEvent.Bits.Gpr; break;
                            case DirtyEventKind.Reload: ++reloads; break;
                            case DirtyEventKind.Flush: uses |= dirtyEvent.Bits.Gpr & dirtyAtFlush[index]; break;
                        }
                    }

                    var isInstruction = events[start].Kind == DirtyEventKind.Instruction;
                    var defines = isInstruction ? definite & writes : 0u;
                    uses |= writes & ~defines;

                    if (reloadLive is not null)
                    {
                        for (var count = 0; count < reloads; ++count) reloadLive[--reloadIndex] = live | uses;
                    }

                    // Events before the first marker belong to no IR instruction: uses only.
                    live = uses | (live & ~defines);
                    end = start;
                }

                return live;
            }

            bool changed;
            do
            {
                changed = false;
                for (var position = blockOrder.Count - 1; position >= 0; --position)
                {
                    var label = blockOrder[position];
                    uint liveOut = 0;
                    foreach (var successor in successors(label))
                        if (liveIn.TryGetValue(successor, out var successorLive)) liveOut |= successorLive;
                    var incoming = Walk(label, liveOut, null);
                    if (incoming != liveIn[label])
                    {
                        liveIn[label] = incoming;
                        changed = true;
                    }
                }
            } while (changed);

            var reloadLiveByBlock = new Dictionary<string, uint[]>(StringComparer.Ordinal);
            foreach (var label in blockOrder)
            {
                if (!_blockEvents.TryGetValue(label, out var events)) continue;
                var reloadCount = 0;
                foreach (var dirtyEvent in events)
                    if (dirtyEvent.Kind == DirtyEventKind.Reload) ++reloadCount;
                if (reloadCount == 0) continue;
                var reloadLive = new uint[reloadCount];
                uint liveOut = 0;
                foreach (var successor in successors(label))
                    if (liveIn.TryGetValue(successor, out var successorLive)) liveOut |= successorLive;
                Walk(label, liveOut, reloadLive);
                reloadLiveByBlock[label] = reloadLive;
            }

            _blockReloadLiveGprs = reloadLiveByBlock;
            _entryLiveGprs = liveIn.GetValueOrDefault(entryLabel, uint.MaxValue);
        }

        public static RegisterResidency CreateRecorder() => new(true);

        public RegisterResidency Snapshot()
        {
            var snapshot = new RegisterResidency(false);
            snapshot._gprs.UnionWith(_gprs);
            snapshot._fprs.UnionWith(_fprs);
            snapshot._writtenGprs.UnionWith(_writtenGprs);
            snapshot._writtenFprs.UnionWith(_writtenFprs);
            snapshot._cr = _cr;
            snapshot._ctr = _ctr;
            snapshot._xer = _xer;
            snapshot._writesCr = _writesCr;
            snapshot._writesCtr = _writesCtr;
            snapshot._writesXer = _writesXer;
            snapshot._blockEvents = _blockEvents;
            snapshot._prologueWrites = _prologueWrites;
            return snapshot;
        }

        public bool IsEmpty => _gprs.Count == 0 && _fprs.Count == 0 && !_cr && !_ctr && !_xer;

        public static string GprLocal(int index) => GprRegisterName(index);
        public static string FprLocal(int index) => FprRegisterName(index);
        public const string CrLocal = "cr";
        public const string CtrLocal = "ctr";
        public const string XerLocal = "xer";

        public IEnumerable<string> DeclaredLocalNames
        {
            get
            {
                foreach (var gpr in _gprs) yield return GprLocal(gpr);
                foreach (var fpr in _fprs) yield return FprLocal(fpr);
                if (_cr) yield return CrLocal;
                if (_ctr) yield return CtrLocal;
                if (_xer) yield return XerLocal;
            }
        }

        public string Gpr(int index, bool written)
        {
            Track(_gprs, _writtenGprs, index, written, "r");
            return GprLocal(index);
        }

        /// <summary>Scalar (PS0) view of a localized FPR.</summary>
        public string FprScalar(int index, bool written)
        {
            Track(_fprs, _writtenFprs, index, written, "f");
            return FprScalarLocalName(index);
        }

        /// <summary>Whole <c>PPC_FPR</c> value of a localized FPR.</summary>
        public string FprStorage(int index, bool written)
        {
            Track(_fprs, _writtenFprs, index, written, "f");
            return FprLocal(index);
        }

        public string Cr(bool written)
        {
            if (written) NoteWrite(new RegisterBits(0, 0, SpecialCr));
            if (_recording)
            {
                _cr = true;
                _writesCr |= written;
            }
            else if (!_cr || (written && !_writesCr))
            {
                throw new InvalidOperationException(
                    "Register residency did not localize cr for this function body.");
            }

            return CrLocal;
        }

        public string Ctr(bool written)
        {
            if (written) NoteWrite(new RegisterBits(0, 0, SpecialCtr));
            if (_recording)
            {
                _ctr = true;
                _writesCtr |= written;
            }
            else if (!_ctr || (written && !_writesCtr))
            {
                throw new InvalidOperationException(
                    "Register residency did not localize ctr for this function body.");
            }

            return CtrLocal;
        }

        /// <summary>
        /// XER, localized as one value (not just summary-overflow) because the carry helpers
        /// read/modify/write it and every integer compare reads XER.SO into its CR field.
        /// </summary>
        public string Xer(bool written)
        {
            if (written) NoteWrite(new RegisterBits(0, 0, SpecialXer));
            if (_recording)
            {
                _xer = true;
                _writesXer |= written;
            }
            else if (!_xer || (written && !_writesXer))
            {
                throw new InvalidOperationException(
                    "Register residency did not localize xer for this function body.");
            }

            return XerLocal;
        }

        private void Track(SortedSet<int> referenced, SortedSet<int> written, int index, bool isWrite, string prefix)
        {
            if (isWrite)
            {
                NoteWrite(ReferenceEquals(referenced, _gprs)
                    ? new RegisterBits(1u << index, 0, 0)
                    : new RegisterBits(0, 1u << index, 0));
            }
            else if (ReferenceEquals(referenced, _gprs))
            {
                NoteRead(new RegisterBits(1u << index, 0, 0));
            }

            if (_recording)
            {
                referenced.Add(index);
                if (isWrite) written.Add(index);
                return;
            }

            if (!referenced.Contains(index) || (isWrite && !written.Contains(index)))
            {
                throw new InvalidOperationException(
                    $"Register residency did not localize {prefix}{index} for this function body.");
            }
        }

        /// <summary>
        /// Entry loads. Every referenced register is loaded unconditionally:
        /// liveness-narrowed initialization is a later optimization and reading
        /// an architectural register is always well defined.
        /// </summary>
        public void AppendEntryLoads(StringBuilder sb)
        {
            if (_recording) return;
            foreach (var gpr in _gprs)
            {
                // A register the body overwrites before reading starts as a plain zero: its local is
                // never published until that write, and never read before it.
                if ((_entryLiveGprs & (1u << gpr)) == 0)
                    sb.Append("    uint32_t ").Append(GprLocal(gpr))
                        .AppendLine(VerifyResidency ? $" = 0xDEAD0000u | {gpr}u;" : " = 0;");
                else
                    sb.Append("    uint32_t ").Append(GprLocal(gpr)).Append(" = ctx->gpr[").Append(gpr).AppendLine("];");
            }
            foreach (var fpr in _fprs)
                sb.Append("    PPC_FPR ").Append(FprLocal(fpr)).Append(" = ctx->fpr[").Append(fpr).AppendLine("];");
            if (_cr) sb.AppendLine($"    uint32_t {CrLocal} = ctx->cr;");
            if (_ctr) sb.AppendLine($"    uint32_t {CtrLocal} = ctx->ctr;");
            if (_xer) sb.AppendLine($"    uint32_t {XerLocal} = ctx->xer;");
        }

        /// <summary>
        /// Stores the possibly-written localized registers back into
        /// <c>CpuContext</c>. Storing an unmodified register writes the same
        /// value back, so the conservative write set stays correct.
        /// </summary>
        public void AppendFlush(StringBuilder sb, string pad) => AppendFlush(sb, pad, ResidencyBoundarySync.Full);

        /// <summary>
        /// Contract-narrowed flush. Only the registers the boundary can observe
        /// or overwrite are published; see <see cref="ResidencyBoundarySync"/>
        /// for why the write set has to participate in the flush mask.
        /// </summary>
        public void AppendFlush(StringBuilder sb, string pad, ResidencyBoundarySync sync)
        {
            Note(new DirtyEvent(DirtyEventKind.Flush, FlushBits(sync)));
            if (_recording) return;
            foreach (var gpr in _writtenGprs)
                if (sync.FlushesGpr(gpr) && IsDirtyGpr(gpr))
                    sb.Append(pad).Append("ctx->gpr[").Append(gpr).Append("] = ").Append(GprLocal(gpr)).AppendLine(";");
            foreach (var fpr in _writtenFprs)
                if (sync.FlushesFpr(fpr) && IsDirtyFpr(fpr))
                    sb.Append(pad).Append("ctx->fpr[").Append(fpr).Append("] = ").Append(FprLocal(fpr)).AppendLine(";");
            if (_writesCr && sync.FlushesCr && IsDirtySpecial(SpecialCr)) sb.Append(pad).AppendLine($"ctx->cr = {CrLocal};");
            if (_writesCtr && sync.FlushesCtr && IsDirtySpecial(SpecialCtr)) sb.Append(pad).AppendLine($"ctx->ctr = {CtrLocal};");
            if (_writesXer && sync.FlushesXer && IsDirtySpecial(SpecialXer)) sb.Append(pad).AppendLine($"ctx->xer = {XerLocal};");
        }

        /// <summary>
        /// Reloads the full localized set after a boundary returns, unconditionally: a callee can
        /// change any volatile register and a nonvolatile one too if it restores it itself.
        /// </summary>
        public void AppendReload(StringBuilder sb, string pad) => AppendReload(sb, pad, ResidencyBoundarySync.Full);

        /// <summary>Contract-narrowed reload.</summary>
        public void AppendReload(StringBuilder sb, string pad, ResidencyBoundarySync sync)
        {
            if (_recording) return;
            foreach (var gpr in _gprs)
                if (sync.ReloadsGpr(gpr))
                    sb.Append(pad).Append(GprLocal(gpr)).Append(" = ctx->gpr[").Append(gpr).AppendLine("];");
            foreach (var fpr in _fprs)
                if (sync.ReloadsFpr(fpr))
                    sb.Append(pad).Append(FprLocal(fpr)).Append(" = ctx->fpr[").Append(fpr).AppendLine("];");
            if (_cr && sync.ReloadsCr) sb.Append(pad).AppendLine($"{CrLocal} = ctx->cr;");
            if (_ctr && sync.ReloadsCtr) sb.Append(pad).AppendLine($"{CtrLocal} = ctx->ctr;");
            if (_xer && sync.ReloadsXer) sb.Append(pad).AppendLine($"{XerLocal} = ctx->xer;");
        }
    }

    /// <summary>
    /// Which localized registers one call boundary must sync. Flush mask is <c>read-before-write | possible-write</c>, not just callee reads, or an unflushed possibly-written register reloads stale.
    /// Reload mask is the callee's possible-write mask (transitive union, not net effect); safe even for a spill/restore, which just re-reads a value already restored to what this frame flushed.
    /// Falls back to <see cref="Full"/> without a precise contract (missing, full-fence, indirect, or CpuContext-touching marshalling).
    /// </summary>
    internal sealed record ResidencyBoundarySync(
        uint FlushGprMask,
        uint FlushFprMask,
        bool FlushesCr,
        bool FlushesCtr,
        uint ReloadGprMask,
        uint ReloadFprMask,
        bool ReloadsCr,
        bool ReloadsCtr,
        bool FlushesXer = false,
        bool ReloadsXer = false)
    {
        public static ResidencyBoundarySync Full { get; } = new(
            uint.MaxValue, uint.MaxValue, true, true,
            uint.MaxValue, uint.MaxValue, true, true,
            FlushesXer: true, ReloadsXer: true);

        /// <summary>
        /// An indirect guest call: everything is published, and everything except r14-r31 is read back. The callee returns with the nonvolatile
        /// GPRs as it found them; see <see cref="GuestAbiContractAnalyzer.NonvolatileGprMask"/>.
        /// Not for call sites that resume through the link register, which keep <see cref="Full"/>.
        /// </summary>
        public static ResidencyBoundarySync FullGuestCall { get; } =
            Full with { ReloadGprMask = ~GuestAbiContractAnalyzer.NonvolatileGprMask };

        /// <summary>
        /// Fast arm of a state-free call site (explicit-state ABI, locals carry args/results) still flushes r1 and XER unconditionally, since a guest gather-pipe store can trigger GX HLE mid-call servicing a VI retrace or OS alarm whose handlers read them architecturally.
        /// Flush-only; the slow arm (<c>InvokeDirectCpu</c>) keeps a full <see cref="Full"/> pair.
        /// </summary>
        public static ResidencyBoundarySync StateFreeResidentOuter { get; } = new(
            FlushGprMask: 1u << 1,
            FlushFprMask: 0u,
            FlushesCr: false,
            FlushesCtr: false,
            ReloadGprMask: 0u,
            ReloadFprMask: 0u,
            ReloadsCr: false,
            ReloadsCtr: false,
            FlushesXer: true,
            ReloadsXer: false);

        public bool FlushesGpr(int index) => (FlushGprMask & (1u << index)) != 0;
        public bool FlushesFpr(int index) => (FlushFprMask & (1u << index)) != 0;
        public bool ReloadsGpr(int index) => (ReloadGprMask & (1u << index)) != 0;
        public bool ReloadsFpr(int index) => (ReloadFprMask & (1u << index)) != 0;

        public bool IsFull =>
            FlushGprMask == uint.MaxValue && FlushFprMask == uint.MaxValue && FlushesCr && FlushesCtr &&
            ReloadGprMask == uint.MaxValue && ReloadFprMask == uint.MaxValue && ReloadsCr && ReloadsCtr &&
            FlushesXer && ReloadsXer;

        /// <summary>
        /// Adds a register class back to both masks, for call sites that emit their own architectural
        /// <c>CpuContext</c> traffic (paired-single return normalization) the callee contract omits.
        /// </summary>
        public ResidencyBoundarySync WithFpr(int index) => this with
        {
            FlushFprMask = FlushFprMask | (1u << index),
            ReloadFprMask = ReloadFprMask | (1u << index)
        };

        public bool IsEmpty =>
            FlushGprMask == 0 && FlushFprMask == 0 && !FlushesCr && !FlushesCtr &&
            ReloadGprMask == 0 && ReloadFprMask == 0 && !ReloadsCr && !ReloadsCtr &&
            !FlushesXer && !ReloadsXer;

        /// <summary>
        /// Boundary set for a non-guest helper's implicit <c>CpuContext</c> effects, from
        /// <see cref="Translator.Core.Analysis.GuestHelperEffectCatalog"/>. Explicit argument/destination registers
        /// need no sync; unknown helpers catalog as complete boundaries and arrive as full masks; a pure helper is empty.
        /// </summary>
        public static ResidencyBoundarySync FromHelperEffect(Translator.Core.Analysis.GuestHelperEffect effect) => new(
            FlushGprMask: effect.GprReadMask | effect.GprWriteMask,
            FlushFprMask: effect.FprReadMask | effect.FprWriteMask,
            FlushesCr: (effect.CrReadMask | effect.CrWriteMask) != 0,
            FlushesCtr: effect.ReadsCtr || effect.WritesCtr,
            ReloadGprMask: effect.GprWriteMask,
            ReloadFprMask: effect.FprWriteMask,
            ReloadsCr: effect.CrWriteMask != 0,
            ReloadsCtr: effect.WritesCtr,
            FlushesXer: effect.ReadsXer || effect.WritesXer,
            ReloadsXer: effect.WritesXer);

        /// <summary>
        /// r1 flushes unconditionally: typed native stubs derive their contract from a host signature and can't see the body re-enter guest code via HLE interrupt delivery, whose callback chains build frames from <c>ctx-&gt;gpr[1]</c>.
        /// A stale r1 once let such a callback overwrite a live frame's save area (return address landed in nw4r::ef's saved r29 at postVRetrace, 0x80210050, as a wild read). Flush-only; caller keeps its stack pointer.
        /// </summary>
        private const uint StackPointerFlushMask = 1u << 1;

        public static ResidencyBoundarySync FromCalleeContract(GuestAbiContract contract)
        {
            // Nonvolatile GPRs are read back only when the callee can really return them changed.
            var reloadGprMask =
                (contract.GprPossibleWriteMask & ~GuestAbiContractAnalyzer.NonvolatileGprMask) |
                (contract.GprPossibleWriteMask & contract.GprNetWriteMask &
                 GuestAbiContractAnalyzer.NonvolatileGprMask);
            // A fence's own masks prove nothing about the volatile registers; only the nonvolatile
            // ones it is known to hand back unchanged are left out.
            if (contract.HasFullSynchronizationFence)
            {
                return Full with
                {
                    ReloadGprMask = ~GuestAbiContractAnalyzer.NonvolatileGprMask |
                                    (contract.GprNetWriteMask & GuestAbiContractAnalyzer.NonvolatileGprMask)
                };
            }

            return new ResidencyBoundarySync(
                FlushGprMask: contract.GprReadBeforeWriteMask | contract.GprPossibleWriteMask |
                              StackPointerFlushMask,
                FlushFprMask: contract.FprReadBeforeWriteMask | contract.FprPossibleWriteMask,
                // The contract tracks CR per field while the local is the whole
                // packed register, so any intersecting field forces the sync.
                FlushesCr: (contract.CrReadBeforeWriteMask | contract.CrPossibleWriteMask) != 0,
                FlushesCtr: contract.ReadsCtrBeforeWrite || contract.MayWriteCtr,
                ReloadGprMask: reloadGprMask,
                ReloadFprMask: contract.FprPossibleWriteMask,
                ReloadsCr: contract.CrPossibleWriteMask != 0,
                ReloadsCtr: contract.MayWriteCtr,
                // Flushed unconditionally for the same reason as r1: interrupt handlers read XER.SO out
                // of the architectural copy. Reload stays contract-driven so a handler can't clobber it.
                FlushesXer: true,
                ReloadsXer: contract.MayWriteXer);
        }
    }

    /// <summary>
    /// Ambient residency for the body being emitted. Thread-local, not an explicit parameter, since threading one through 30+ static formatting helpers risks a missed path silently reading stale <c>ctx-&gt;gpr[N]</c>; also keeps parallel per-function emission independent.
    /// </summary>
    [ThreadStatic]
    private static RegisterResidency? _activeResidency;

    private static void AppendFlush(StringBuilder sb, string pad)
    {
        if (_activeResidency is { } residency) residency.AppendFlush(sb, pad);
    }

    private static void AppendReload(StringBuilder sb, string pad)
    {
        if (_activeResidency is { } residency) residency.AppendReload(sb, pad);
    }

    private static void AppendFlush(StringBuilder sb, string pad, ResidencyBoundarySync sync)
    {
        if (_activeResidency is { } residency) residency.AppendFlush(sb, pad, sync);
    }

    private static uint GprBit(string? name)
    {
        if (string.IsNullOrEmpty(name)) return 0;
        var baseName = GetRegisterBaseName(name);
        return baseName.Length >= 2 && baseName[0] == 'r' &&
               int.TryParse(baseName.AsSpan(1), out var index) && index is >= 0 and < 32
            ? 1u << index
            : 0u;
    }

    private static uint GprBit(IrValue? value) =>
        value is { Kind: "register" } ? GprBit(value.RegisterName) : 0u;

    /// <summary>
    /// GPRs an IR instruction writes on every path through it. Only the plain forms: anything the
    /// emitter may lower to a conditional write reports nothing and so never ends a live range.
    /// </summary>
    private static uint DefiniteGprWrites(IrInstruction instruction) => instruction switch
    {
        IrAssign value => GprBit(value.Destination),
        IrBinary value => GprBit(value.Destination),
        IrLoad value => GprBit(value.Destination),
        IrResolvedLoad value => GprBit(value.Destination),
        _ => 0u
    };

    /// <summary>GPR operands of an IR instruction, as a floor under the reads the emitter records.</summary>
    private static uint IrGprReads(IrInstruction instruction)
    {
        switch (instruction)
        {
            case IrAssign value: return GprBit(value.Value);
            case IrBinary value: return GprBit(value.Left) | GprBit(value.Right);
            case IrLoad value: return GprBit(value.Address.Base);
            case IrStore value: return GprBit(value.Address.Base) | GprBit(value.Source);
            case IrResolveGuestMemoryRange value: return GprBit(value.Base);
            case IrResolvedLoad value: return GprBit(value.OriginalAddress.Base);
            case IrResolvedStore value: return GprBit(value.OriginalAddress.Base) | GprBit(value.Source);
            case IrResolvedPsqLoad value: return GprBit(value.OriginalAddress);
            case IrResolvedPsqStore value: return GprBit(value.OriginalAddress) | GprBit(value.Source);
            case IrResolvedLoadPair value:
                return GprBit(value.FirstOriginalAddress.Base) | GprBit(value.SecondOriginalAddress.Base);
            case IrResolvedStorePair value:
                return GprBit(value.FirstOriginalAddress.Base) | GprBit(value.SecondOriginalAddress.Base) |
                       GprBit(value.FirstSource) | GprBit(value.SecondSource);
            case IrCall value:
            {
                // A guest callee takes its inputs from CpuContext, not from this frame's locals,
                // and the lifter lists every volatile argument register on every call: counting
                // those would keep r3-r10 alive across each one. A helper's arguments are real.
                if (TryParseAddress(value.Target, out _) || IsGuestCallTarget(value.Target)) return 0u;
                var reads = 0u;
                foreach (var argument in value.Arguments) reads |= GprBit(argument);
                return reads;
            }
            case IrIndirectCall value: return GprBit(value.Target);
            case IrIndirectJump value: return GprBit(value.Target);
            case IrSetCrField value: return GprBit(value.Left) | GprBit(value.Right);
            case IrPhi value:
            {
                var reads = 0u;
                foreach (var source in value.Sources.Values) reads |= GprBit(source);
                return reads;
            }
            case IrJumpTable value: return GprBit(value.Selector);
            case IrReturn { Value: { } returned }: return GprBit(returned);
            default: return 0u;
        }
    }

    private static void AppendBoundaryFlush(StringBuilder sb, string pad, ResidencyBoundarySync sync)
    {
        if (_activeResidency is { } residency) residency.AppendBoundaryFlush(sb, pad, sync);
    }

    private static void AppendBoundaryReload(StringBuilder sb, string pad, ResidencyBoundarySync sync)
    {
        if (_activeResidency is { } residency) residency.AppendBoundaryReload(sb, pad, sync);
    }

    private static void AppendReload(StringBuilder sb, string pad, ResidencyBoundarySync sync)
    {
        if (_activeResidency is { } residency) residency.AppendReload(sb, pad, sync);
    }

    /// <summary>
    /// Resolves the boundary sync set for a direct guest call, falling back to <see cref="ResidencyBoundarySync.Full"/> when narrowing is disabled, residency is inactive, the callee has no contract or a full-sync fence, or a mod could replace the callee with a re-translation this body's contract doesn't describe.
    /// </summary>
    private static ResidencyBoundarySync ResolveDirectCallBoundarySync(
        uint target,
        IReadOnlyDictionary<uint, GuestAbiContract> guestAbiContracts,
        IReadOnlySet<uint> modOverridableCallTargets)
    {
        if (_activeResidency is null ||
            modOverridableCallTargets.Contains(target) ||
            !guestAbiContracts.TryGetValue(target, out var contract))
        {
            // Not FullGuestCall: a direct call with no contract can be a mod hook, patched over one
            // instruction of this very function and written to change its registers (Retro Rewind's
            // hook at 0x800CB6C0 replaces "mr r29, r4" and sets r29 and r31 itself). Found by the
            // self-check on 2026-10-06. Only an indirect call is trusted to behave like a function.
            return ResidencyBoundarySync.Full;
        }

        return ResidencyBoundarySync.FromCalleeContract(contract);
    }

    /// <summary>
    /// Removes an explicit helper destination register from a helper-effect
    /// reload mask. The destination local holds the helper's return value; the
    /// implicit-context reload must not overwrite it.
    /// </summary>
    private static ResidencyBoundarySync ExcludeDestinationFromReload(
        ResidencyBoundarySync sync,
        string? destination)
    {
        if (string.IsNullOrWhiteSpace(destination))
        {
            return sync;
        }

        var baseName = GetRegisterBaseName(destination);
        if (baseName.Length > 1 && baseName[0] is 'r' or 'R' &&
            int.TryParse(baseName.AsSpan(1), out var gpr) && gpr is >= 0 and < 32)
        {
            return sync with { ReloadGprMask = sync.ReloadGprMask & ~(1u << gpr) };
        }

        if (baseName.Length > 1 && baseName[0] is 'f' or 'F' &&
            int.TryParse(baseName.AsSpan(1), out var fpr) && fpr is >= 0 and < 32)
        {
            return sync with { ReloadFprMask = sync.ReloadFprMask & ~(1u << fpr) };
        }

        if (baseName.Equals("cr", StringComparison.OrdinalIgnoreCase))
        {
            return sync with { ReloadsCr = false };
        }

        if (baseName.Equals("ctr", StringComparison.OrdinalIgnoreCase))
        {
            return sync with { ReloadsCtr = false };
        }

        if (baseName.Equals("xer", StringComparison.OrdinalIgnoreCase))
        {
            return sync with { ReloadsXer = false };
        }

        return sync;
    }

    /// <summary>
    /// Rejects a residency whose local names would shadow an emitted temporary. A guard against a
    /// future naming change, not an expected condition, since <see cref="IsCpuRegisterNoLocalNeeded"/>
    /// already keeps machine register names out of the ordinary local set.
    /// </summary>
    private static void RequireResidencyNamesAreFree(
        RegisterResidency residency,
        uint entryPoint,
        IEnumerable<string> emittedLocalNames)
    {
        var taken = new HashSet<string>(emittedLocalNames, StringComparer.Ordinal);
        var collisions = residency.DeclaredLocalNames.Where(taken.Contains).ToArray();
        if (collisions.Length != 0)
        {
            throw new InvalidOperationException(
                $"Register residency local name(s) {string.Join(", ", collisions)} collide with an existing " +
                $"local in 0x{entryPoint:X8}.");
        }
    }
}
