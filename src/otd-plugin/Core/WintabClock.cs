using System.Diagnostics;

namespace OtdWintab.Core;

/// <summary>
/// Packet time in milliseconds, on the same base as GetTickCount but with 1 ms resolution.
/// Environment.TickCount only advances in steps of the system timer (about 15.6 ms), so at 200-300 reports
/// per second several packets carried the same pkTime and the next one jumped by 16 ms. Applications that
/// derive pen speed from pkTime (stroke stabilizers, for example) then see zero or huge intervals.
/// </summary>
internal static class WintabClock
{
    private static readonly long BaseTicks = Environment.TickCount64;
    private static readonly long BaseStamp = Stopwatch.GetTimestamp();

    public static uint NowMs =>
        unchecked((uint)(BaseTicks + (Stopwatch.GetTimestamp() - BaseStamp) * 1000 / Stopwatch.Frequency));
}
