// otd-wintab: publishes tool events from any absolute output mode, as a
// post-transform filter. Combined with a Windows Ink output mode this gives
// the "hybrid" setup of vendor drivers: Windows Ink applications use the
// Windows Ink pen while Wintab applications get otd-wintab's packets, with
// no switching between modes.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later
using OpenTabletDriver.Plugin;
using OpenTabletDriver.Plugin.Attributes;
using OpenTabletDriver.Plugin.Output;
using OpenTabletDriver.Plugin.Tablet;
using OtdWintab.Core;

namespace OtdWintab;

[PluginName("Wintab publisher (hybrid)")]
public sealed class WintabPublisherFilter : IPositionedPipelineElement<IDeviceReport>
{
    private const string LogGroup = "otd-wintab";

    // OpenTabletDriver rebuilds filters on every Apply without disposing the old
    // ones: one ring, tracker and watchdog are shared by the whole daemon, so
    // there is never more than one producer.
    private static readonly object s_lock = new();
    private static RingWriter? s_ring;
    private static ToolTracker? s_tracker;
    private static Timer? s_watchdog;
    private static volatile int s_proximityTimeout = 100;

    private TabletReference? _tablet;
    private bool _failed;

    [TabletReference]
    public TabletReference Tablet { set => _tablet = value; }

    [Property("Proximity timeout"), Unit("ms"), DefaultPropertyValue(100),
     ToolTip("otd-wintab:\n\nTreat the pen as lifted after this long without a report.")]
    public int ProximityTimeout { set => s_proximityTimeout = Math.Max(value, 20); get => s_proximityTimeout; }

    [Property("Tip threshold"), Unit("%"), DefaultPropertyValue(1f),
     ToolTip("otd-wintab:\n\nPressure above which the tip counts as touching.")]
    public float TipThreshold { set; get; } = 1f;

    public PipelinePosition Position => PipelinePosition.PostTransform;

    public event Action<IDeviceReport?>? Emit;

    public void Consume(IDeviceReport? report)
    {
        if (report != null && !_failed)
        {
            try
            {
                Observe(report);
            }
            catch (Exception e)
            {
                _failed = true;
                Log.Write(LogGroup, $"publisher disabled after error: {e}", LogLevel.Error);
            }
        }
        Emit?.Invoke(report);
    }

    private ToolTracker Tracker()
    {
        lock (s_lock)
        {
            if (s_tracker != null) return s_tracker;
            s_ring = new RingWriter();
            uint vendor = (uint)(_tablet?.Properties.DigitizerIdentifiers.FirstOrDefault()?.VendorID ?? 0);
            var tracker = new ToolTracker(s_ring, StylusDatabase.LoadEmbedded(), vendor);
            s_watchdog = new Timer(_ => tracker.Tick(WintabClock.NowMs, (uint)s_proximityTimeout), null, 25, 25);
            Log.Write(LogGroup, $"hybrid: publishing tool events for {_tablet?.Properties.Name ?? "tablet"}");
            return s_tracker = tracker;
        }
    }

    private void Observe(IDeviceReport report)
    {
        var tracker = Tracker();
        uint now = WintabClock.NowMs;

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
            float maxPressure = _tablet?.Properties.Specifications.Pen?.MaxPressure ?? 0;
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

            if (report is IToolLeavingReport)
                tracker.OnOutOfRange(now);
        }
    }
}
