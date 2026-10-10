// otd-wintab: report capabilities OpenTabletDriver's plugin API has no
// interface for. Implemented by this plugin's parsers.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using OpenTabletDriver.Plugin.Tablet;

namespace OtdWintab;

/// <summary>Airbrush finger wheel position.</summary>
public interface IAirbrushWheelReport : IDeviceReport
{
    /// <summary>0..1</summary>
    float Wheel { get; }
}

/// <summary>
/// The physical tool a position report belongs to. Carried on every pen
/// report, so the identity survives a missed proximity packet (e.g. a pen
/// already hovering while OpenTabletDriver sets up the output mode).
/// </summary>
public interface IToolIdentityReport : IDeviceReport
{
    uint ToolId { get; }
    ulong ToolSerial { get; }
    bool IsEraserEnd { get; }
}

/// <summary>
/// The tool's last report as it leaves proximity: pressure 0, every button
/// up, at its last position (as Linux's wacom_exit_report). It stands in for
/// OutOfRangeReport, because OpenTabletDriver 0.6's BindingHandler only
/// releases barrel-button bindings on OutOfRangeReport; the tip and eraser
/// bindings are released only by a report with no pressure. Without it, a pen
/// leaving with its tip down keeps the tip's mouse button pressed.
/// </summary>
public interface IToolLeavingReport : ITabletReport
{
}

/// <summary>Barrel rotation (Art Pen / marker).</summary>
public interface IBarrelRotationReport : IDeviceReport
{
    /// <summary>degrees, 0..360</summary>
    float Rotation { get; }
}
