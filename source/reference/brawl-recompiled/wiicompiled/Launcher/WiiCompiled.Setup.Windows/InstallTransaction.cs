using WiiCompiled.Setup.Common;

namespace WiiCompiled.Setup.Windows;

internal enum InstallTransactionEntryKind
{
    Directory,
    File
}

/// <summary>An exact prepared-path to live-path replacement made by an install transaction.</summary>
internal sealed record InstallTransactionEntry(
    string PreparedPath,
    string DestinationPath,
    InstallTransactionEntryKind Kind)
{
    public static InstallTransactionEntry Directory(string preparedPath, string destinationPath) =>
        new(preparedPath, destinationPath, InstallTransactionEntryKind.Directory);

    public static InstallTransactionEntry File(string preparedPath, string destinationPath) =>
        new(preparedPath, destinationPath, InstallTransactionEntryKind.File);
}

/// <summary>
/// Publishes prepared files/directories as one recoverable operation. Destinations are journaled before the
/// first move, so an interrupted operation rolls back on disposal or the next lock acquisition. Because every
/// move is a same-volume rename, rollback needs no per-entry record, just backup/destination/prepared-path presence.
/// </summary>
internal sealed class InstallTransaction : IDisposable
{
    // v3 renamed the journal's version field from "Schema" to "SchemaVersion", so that every
    // persisted document in an installation spells it the same way. A journal only exists between
    // the first move of an update and its commit, and a document from another version has always
    // been rejected rather than interpreted, so the rename needs no read compatibility - it only
    // needs to be visible as a version change.
    private const int SchemaVersion = 3;

    private readonly string _journalPath;
    private readonly IInstallReporter? _reporter;
    private readonly TransactionJournal _journal;
    private bool _finished;

    private InstallTransaction(string journalPath, TransactionJournal journal, IInstallReporter? reporter)
    {
        _journalPath = journalPath;
        _journal = journal;
        _reporter = reporter;
    }

    public static InstallTransaction Begin(string installDirectory, IInstallReporter? reporter = null,
        params InstallTransactionEntry[] entries)
    {
        var root = InstallOperationPaths.NormalizeRoot(installDirectory);
        if (entries is null || entries.Length == 0)
            throw new ArgumentException("At least one publish entry is required.", nameof(entries));

        var journalPath = InstallOperationPaths.GetJournalPath(root);
        if (File.Exists(journalPath))
            throw new InvalidOperationException(
                "This installation has an unfinished transaction. Acquire the installation operation lock " +
                "so it can recover before beginning another transaction.");

        var transactionId = Guid.NewGuid();
        var workRoot = InstallOperationPaths.GetWorkRoot(root, transactionId);
        var journal = new TransactionJournal
        {
            SchemaVersion = SchemaVersion,
            TransactionId = transactionId,
            InstallRoot = root,
            WorkRoot = workRoot,
            State = TransactionState.Prepared,
            Entries = NormalizeEntries(root, workRoot, entries)
        };

        Directory.CreateDirectory(Path.Combine(workRoot, "backup"));
        JsonState.Write(journalPath, journal);
        return new InstallTransaction(journalPath, journal, reporter);
    }

    /// <summary>Moves every old destination aside and publishes every prepared entry.</summary>
    public void Publish()
    {
        EnsureActive(TransactionState.Prepared);
        try
        {
            foreach (var entry in _journal.Entries)
            {
                if (entry.OriginalExisted)
                    Move(entry.DestinationPath, entry.BackupPath, entry.Kind);
                Move(entry.PreparedPath, entry.DestinationPath, entry.Kind);
            }

            _journal.State = TransactionState.Published;
            WriteJournal();
        }
        catch (Exception publishFailure)
        {
            try
            {
                RollBack();
            }
            catch (Exception rollbackFailure)
            {
                throw new AggregateException(
                    "Publishing failed and the previous installation could not be fully restored. " +
                    "The recovery journal was retained for the next operation.",
                    publishFailure, rollbackFailure);
            }
            throw;
        }
    }

    /// <summary>
    /// Durably records the runtime configuration preimage before the caller mutates it. Recovery of
    /// any uncommitted transaction restores this exact preimage; committed transactions retain the
    /// new configuration. Call after <see cref="Publish"/> and before the first config write.
    /// </summary>
    public void RecordRuntimeConfigurationMutation(RuntimeConfigSnapshot snapshot)
    {
        EnsureActive(TransactionState.Published);
        _journal.RuntimeConfigExisted = snapshot.Existed;
        _journal.RuntimeConfigContents = snapshot.Contents;
        _journal.RuntimeConfigMutationPlanned = true;
        WriteJournal();
    }

    /// <summary>
    /// Makes the published paths authoritative. Cleanup failure is non-fatal: the committed journal
    /// makes the next lock acquisition finish deleting only this transaction's exact backup paths.
    /// </summary>
    public void Commit()
    {
        EnsureActive(TransactionState.Published);
        _journal.State = TransactionState.Committed;
        WriteJournal();
        _finished = true;
        TryCleanCommitted(_journalPath, _journal, _reporter);
    }

    public void Dispose()
    {
        if (_finished) return;
        RollBack();
    }

    /// <summary>Recovers the one deterministic journal for an installation. Call only while locked.</summary>
    public static void Recover(string installDirectory, IInstallReporter? reporter = null)
    {
        var root = InstallOperationPaths.NormalizeRoot(installDirectory);
        var journalPath = InstallOperationPaths.GetJournalPath(root);
        if (!File.Exists(journalPath)) return;

        var journal = JsonState.TryRead<TransactionJournal>(journalPath)
                      ?? throw new InvalidDataException(
                          $"The interrupted-install journal is unreadable: {journalPath}");
        ValidateJournal(root, journal);

        if (journal.State is TransactionState.Committed or TransactionState.RolledBack)
        {
            reporter?.Diagnostic("Finishing cleanup from the previous completed update operation...");
            TryCleanCommitted(journalPath, journal, reporter);
            if (File.Exists(journalPath))
                throw new IOException("The previous committed update's exact backup could not be removed.");
            return;
        }

        reporter?.Diagnostic("Restoring the installation after an interrupted update...");
        new InstallTransaction(journalPath, journal, reporter).RollBack();
    }

    private void RollBack()
    {
        if (_finished) return;
        ValidateJournal(InstallOperationPaths.NormalizeRoot(_journal.InstallRoot), _journal);

        foreach (var entry in _journal.Entries.AsEnumerable().Reverse())
            RollBackEntry(entry);

        if (_journal.RuntimeConfigMutationPlanned)
        {
            // The journal records the installation root, which is what decides whether this
            // installation's configuration lives in its portable root or in per-user data.
            RuntimeConfiguration.Restore(
                RuntimeConfiguration.ResolveConfigPath(_journal.InstallRoot),
                new RuntimeConfigSnapshot(_journal.RuntimeConfigExisted,
                    _journal.RuntimeConfigContents ?? []));
        }

        _journal.State = TransactionState.RolledBack;
        WriteJournal();
        _finished = true;
        FileSystemUtilities.DeleteDirectoryIfExists(_journal.WorkRoot);
        File.Delete(_journalPath);
    }

    private static void RollBackEntry(TransactionJournalEntry entry)
    {
        var destinationExists = Exists(entry.DestinationPath, entry.Kind);
        if (Exists(entry.BackupPath, entry.Kind))
        {
            if (destinationExists) Delete(entry.DestinationPath, entry.Kind);
            Move(entry.BackupPath, entry.DestinationPath, entry.Kind);
            return;
        }

        // No backup: either the original was never moved aside, or it has already been restored.
        // Both leave the original in place, so only a vanished original is a failure.
        if (entry.OriginalExisted)
        {
            if (!destinationExists)
                throw new IOException($"The transaction backup is missing for {entry.DestinationPath}.");
            return;
        }

        if (destinationExists && Exists(entry.PreparedPath, entry.Kind))
            throw new IOException(
                $"Both prepared and published paths exist for {entry.DestinationPath}; recovery is ambiguous.");
        if (destinationExists) Delete(entry.DestinationPath, entry.Kind);
    }

    private static List<TransactionJournalEntry> NormalizeEntries(string root, string workRoot,
        IReadOnlyList<InstallTransactionEntry> entries)
    {
        var normalized = new List<TransactionJournalEntry>(entries.Count);
        for (var index = 0; index < entries.Count; index++)
        {
            var requested = entries[index];
            var prepared = Path.GetFullPath(requested.PreparedPath);
            var destination = Path.GetFullPath(requested.DestinationPath);
            EnsureDestinationInScope(root, destination);
            if (FileSystemUtilities.PathsOverlap(prepared, destination))
                throw new InvalidOperationException("A prepared path and its destination must not overlap.");

            if (!Exists(prepared, requested.Kind))
                throw new FileNotFoundException("A prepared transaction entry is missing.", prepared);
            if (ExistsAsOtherKind(prepared, requested.Kind))
                throw new InvalidDataException($"The prepared path has the wrong entry type: {prepared}");
            if (ExistsAsOtherKind(destination, requested.Kind))
                throw new InvalidDataException($"The destination has the wrong entry type: {destination}");

            foreach (var previous in normalized)
            {
                if (FileSystemUtilities.PathsOverlap(destination, previous.DestinationPath))
                    throw new InvalidOperationException("Transaction destinations must not overlap.");
                if (FileSystemUtilities.PathsOverlap(prepared, previous.PreparedPath))
                    throw new InvalidOperationException("Transaction prepared paths must not overlap.");
                if (FileSystemUtilities.PathsOverlap(prepared, previous.DestinationPath) ||
                    FileSystemUtilities.PathsOverlap(destination, previous.PreparedPath))
                    throw new InvalidOperationException("Transaction sources and destinations must not overlap.");
            }

            normalized.Add(new TransactionJournalEntry
            {
                PreparedPath = prepared,
                DestinationPath = destination,
                BackupPath = Path.Combine(workRoot, "backup", index.ToString("D4")),
                Kind = requested.Kind,
                OriginalExisted = Exists(destination, requested.Kind)
            });
        }
        return normalized;
    }

    private static void ValidateJournal(string expectedRoot, TransactionJournal journal)
    {
        if (journal.SchemaVersion != SchemaVersion || journal.TransactionId == Guid.Empty)
            throw new InvalidDataException("The interrupted-install journal has an unsupported format.");

        var root = InstallOperationPaths.NormalizeRoot(journal.InstallRoot);
        if (!root.Equals(expectedRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The interrupted-install journal belongs to another installation.");

        var expectedWorkRoot = InstallOperationPaths.GetWorkRoot(root, journal.TransactionId);
        if (!Path.GetFullPath(journal.WorkRoot).Equals(expectedWorkRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The interrupted-install journal has an invalid work directory.");
        if (journal.Entries.Count == 0 || journal.Entries.Count > 64)
            throw new InvalidDataException("The interrupted-install journal has an invalid entry count.");

        for (var index = 0; index < journal.Entries.Count; index++)
        {
            var entry = journal.Entries[index];
            EnsureDestinationInScope(root, Path.GetFullPath(entry.DestinationPath));
            var expectedBackup = Path.Combine(expectedWorkRoot, "backup", index.ToString("D4"));
            if (!Path.GetFullPath(entry.BackupPath).Equals(expectedBackup, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The interrupted-install journal has an invalid backup path.");
        }
    }

    private static void EnsureDestinationInScope(string root, string destination)
    {
        if (!FileSystemUtilities.PathContains(root, destination))
            throw new InvalidOperationException("A transaction destination must be inside its installation root.");
    }

    private void EnsureActive(TransactionState expected)
    {
        if (_finished || _journal.State != expected)
            throw new InvalidOperationException("The install transaction is not in the required state.");
    }

    private void WriteJournal() => JsonState.Write(_journalPath, _journal);

    private static bool Exists(string path, InstallTransactionEntryKind kind) =>
        kind == InstallTransactionEntryKind.Directory ? Directory.Exists(path) : File.Exists(path);

    private static bool ExistsAsOtherKind(string path, InstallTransactionEntryKind kind) =>
        kind == InstallTransactionEntryKind.Directory ? File.Exists(path) : Directory.Exists(path);

    // Antivirus and indexers briefly hold handles inside freshly written trees; ride those out
    // before treating a locked path as fatal.
    private const int MoveAttempts = 10;
    private static readonly TimeSpan MoveRetryDelay = TimeSpan.FromMilliseconds(500);

    private static void Move(string source, string destination, InstallTransactionEntryKind kind)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        for (var attempt = 1; ; attempt++)
        {
            try
            {
                if (kind == InstallTransactionEntryKind.Directory) Directory.Move(source, destination);
                else File.Move(source, destination);
                return;
            }
            catch (Exception ex) when (IsTransientLock(ex))
            {
                if (attempt == MoveAttempts)
                    throw new IOException($"Could not replace \"{destination}\": {ex.Message} " +
                                          "Close any program using this folder and retry the update.", ex);
                Thread.Sleep(MoveRetryDelay);
            }
        }
    }

    internal static bool IsTransientLock(Exception ex) =>
        ex is IOException or UnauthorizedAccessException &&
        (ex.HResult & 0xFFFF) is 5 or 32 or 33; // ACCESS_DENIED, SHARING_VIOLATION, LOCK_VIOLATION

    private static void Delete(string path, InstallTransactionEntryKind kind)
    {
        if (kind == InstallTransactionEntryKind.Directory) FileSystemUtilities.DeleteDirectoryIfExists(path);
        else File.Delete(path);
    }

    private static void TryCleanCommitted(string journalPath, TransactionJournal journal,
        IInstallReporter? reporter)
    {
        try
        {
            FileSystemUtilities.DeleteDirectoryIfExists(journal.WorkRoot);
            File.Delete(journalPath);
        }
        catch (Exception ex)
        {
            reporter?.Diagnostic(
                "The update committed successfully, but its exact previous-version backup could not yet be " +
                "removed: " + ex.Message);
        }
    }

    private sealed class TransactionJournal
    {
        public TransactionJournal() { }

        public int SchemaVersion { get; set; }
        public Guid TransactionId { get; set; }
        public string InstallRoot { get; set; } = "";
        public string WorkRoot { get; set; } = "";
        public TransactionState State { get; set; }
        public List<TransactionJournalEntry> Entries { get; set; } = [];
        public bool RuntimeConfigMutationPlanned { get; set; }
        public bool RuntimeConfigExisted { get; set; }
        public byte[]? RuntimeConfigContents { get; set; }
    }

    private sealed class TransactionJournalEntry
    {
        public TransactionJournalEntry() { }

        public string PreparedPath { get; set; } = "";
        public string DestinationPath { get; set; } = "";
        public string BackupPath { get; set; } = "";
        public InstallTransactionEntryKind Kind { get; set; }
        public bool OriginalExisted { get; set; }
    }

    private enum TransactionState
    {
        Prepared,
        Published,
        Committed,
        RolledBack
    }
}
