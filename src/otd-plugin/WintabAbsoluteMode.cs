// otd-wintab: OpenTabletDriver output mode. Moves the system cursor like
// OpenTabletDriver's Absolute Mode, and additionally publishes every tool's
// proximity and packets to the otd-wintab ring, from which the Wintab
// provider in applications creates one Wintab cursor per physical stylus.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using OpenTabletDriver.Plugin;
using OpenTabletDriver.Plugin.Attributes;
using OpenTabletDriver.Plugin.DependencyInjection;
using OpenTabletDriver.Plugin.Output;
using OpenTabletDriver.Plugin.Platform.Pointer;
using OpenTabletDriver.Plugin.Tablet;
using OtdWintab.Core;

namespace OtdWintab;

[PluginName("Wintab Absolute Mode")]
public class WintabAbsoluteMode : AbsoluteOutputMode
{
    private const string LogGroup = "otd-wintab";

    private RingWriter? _ring;
    private ToolTracker? _tracker;
    private Timer? _watchdog;
    private bool _failed;

    [Resolved]
    public override IAbsolutePointer Pointer { set; get; } = null!;

    [Property("Proximity timeout"), Unit("ms"), DefaultPropertyValue(100),
     ToolTip("otd-wintab:\n\nTreat the pen as lifted after this long without a report.\n" +
             "Only used for tablets whose parser doesn't report leaving; off for Intuos3 with the otd-wintab parser.")]
    public int ProximityTimeout { set; get; } = 100;

    [Property("Tip threshold"), Unit("%"), DefaultPropertyValue(1f),
     ToolTip("otd-wintab:\n\nPressure above which the tip counts as touching.")]
    public float TipThreshold { set; get; } = 1f;

    protected override void OnOutput(IDeviceReport report)
    {
        base.OnOutput(report);   // the system cursor, exactly as Absolute Mode
        if (_failed) return;
        try
        {
            Observe(report);
        }
        catch (Exception e)
        {
            _failed = true;
            Log.Write(LogGroup, $"disabled after error: {e}", LogLevel.Error);
        }
    }

    private void Observe(IDeviceReport report)
    {
        var tracker = _tracker ??= CreateTracker();
        uint now = (uint)Environment.TickCount;

        // the watchdog is only for parsers that never report leaving: ours do
        // (they also name the tool on every report), and so does any tablet
        // that has sent an out-of-range once
        if (report is IToolIdentityReport or IToolLeavingReport or OutOfRangeReport)
            tracker.ReportsLeaving = true;

        if (report is IToolReport tool)
            tracker.OnToolReport(tool.RawToolID, tool.Serial, tool.Tool == ToolType.Eraser, now);

        if (report is OutOfRangeReport)
        {
            tracker.OnOutOfRange(now);
            return;
        }

        if (report is ITabletReport tablet && report is IAbsolutePositionReport position)
        {
            float maxPressure = Tablet?.Properties.Specifications.Pen?.MaxPressure ?? 0;
            float pressure = maxPressure > 0 ? tablet.Pressure / maxPressure : 0;
            var buttons = tablet.PenButtons ?? Array.Empty<bool>();

            var sample = new PenSample
            {
                Position = position.Position,
                Pressure = Math.Clamp(pressure, 0, 1),
                Tip = pressure * 100 > TipThreshold,
                LowerBarrel = buttons.Length > 0 && buttons[0],
                UpperBarrel = buttons.Length > 1 && buttons[1],
                Tilt = report is ITiltReport tilt ? tilt.Tilt : null,
                Eraser = report is IEraserReport eraser ? eraser.Eraser : null,
                Wheel = report is IAirbrushWheelReport wheel ? wheel.Wheel : null,
                Rotation = report is IBarrelRotationReport rotation ? rotation.Rotation : null,
                Tool = report is IToolIdentityReport id && id.ToolId != 0
                    ? new ToolIdentity(id.ToolId, (uint)id.ToolSerial, id.IsEraserEnd)
                    : null,
            };
            tracker.OnSample(sample, now);

            // the parser's stand-in for OutOfRangeReport (see IToolLeavingReport)
            if (report is IToolLeavingReport)
                tracker.OnOutOfRange(now);
        }
    }

    private ToolTracker CreateTracker()
    {
        _ring = new RingWriter();
        uint vendor = (uint)(Tablet?.Properties.DigitizerIdentifiers.FirstOrDefault()?.VendorID ?? 0);
        var tracker = new ToolTracker(_ring, StylusDatabase.LoadEmbedded(), vendor);
        _watchdog = new Timer(_ => tracker.Tick((uint)Environment.TickCount, (uint)Math.Max(ProximityTimeout, 20)),
                              null, 25, 25);
        Log.Write(LogGroup, $"publishing tool events for {Tablet?.Properties.Name ?? "tablet"}");
        return tracker;
    }

    internal ToolTracker? Tracker => _tracker;

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _watchdog?.Dispose();
            _tracker?.OnOutOfRange((uint)Environment.TickCount);
            _ring?.Dispose();
            _watchdog = null;
            _tracker = null;
            _ring = null;
        }
        base.Dispose(disposing);
    }
}
