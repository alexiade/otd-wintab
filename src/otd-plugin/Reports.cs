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

/// <summary>Barrel rotation (Art Pen / marker).</summary>
public interface IBarrelRotationReport : IDeviceReport
{
    /// <summary>degrees, 0..360</summary>
    float Rotation { get; }
}
