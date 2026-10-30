namespace WiiCompiled.Setup.Windows;

/// <summary>
/// Bridges a frontend-owned, named Windows event into the cancellation token used by setup.
/// </summary>
internal sealed class CancellationSignal : IDisposable
{
    public const string EnvironmentVariable = "MKWCOMPILED_CANCEL_EVENT";

    private readonly EventWaitHandle? _event;
    private readonly CancellationTokenSource? _source;
    private readonly RegisteredWaitHandle? _registration;

    private CancellationSignal(EventWaitHandle? @event, CancellationTokenSource? source,
        RegisteredWaitHandle? registration)
    {
        _event = @event;
        _source = source;
        _registration = registration;
    }

    public CancellationToken Token => _source?.Token ?? CancellationToken.None;

    public static CancellationSignal ObserveEnvironment()
    {
        var eventName = Environment.GetEnvironmentVariable(EnvironmentVariable);
        if (eventName is null)
            return new CancellationSignal(null, null, null);
        if (string.IsNullOrWhiteSpace(eventName))
            throw new InvalidOperationException(
                $"The cancellation event named by {EnvironmentVariable} is empty.");

        EventWaitHandle signal;
        try
        {
            signal = EventWaitHandle.OpenExisting(eventName.Trim());
        }
        catch (Exception ex) when (ex is WaitHandleCannotBeOpenedException or UnauthorizedAccessException
                                   or IOException or ArgumentException)
        {
            throw new InvalidOperationException(
                $"The cancellation event named by {EnvironmentVariable} could not be opened.", ex);
        }

        var source = new CancellationTokenSource();
        RegisteredWaitHandle registration;
        try
        {
            registration = ThreadPool.RegisterWaitForSingleObject(signal, static (state, _) =>
            {
                try { ((CancellationTokenSource)state!).Cancel(); }
                catch (ObjectDisposedException) { }
            }, source, Timeout.Infinite, executeOnlyOnce: true);

            // Registering and checking explicitly closes the race where the event was already set
            // before RegisterWaitForSingleObject installed its wait.
            if (signal.WaitOne(0)) source.Cancel();
        }
        catch
        {
            source.Dispose();
            signal.Dispose();
            throw;
        }

        return new CancellationSignal(signal, source, registration);
    }

    public void Dispose()
    {
        _registration?.Unregister(null);
        _event?.Dispose();
        _source?.Dispose();
    }
}
