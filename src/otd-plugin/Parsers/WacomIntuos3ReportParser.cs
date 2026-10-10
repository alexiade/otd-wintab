// otd-wintab: Wacom Intuos3 (PTZ-xxx) report parser, a port of the Linux
// kernel's Intuos protocol handling (drivers/hid/wacom_wac.c:
// wacom_intuos_inout, wacom_intuos_general). Compared with
// OpenTabletDriver's built-in parser it
//
//  - accepts every proximity packet ((b1 & 0xFC) == 0xC0), so the tool id,
//    serial and eraser end are always known,
//  - reports the tool leaving ((b1 & 0xFE) == 0x80) as a final report with
//    no pressure and no buttons (see IToolLeavingReport), so nothing stays
//    pressed when the pen leaves with its tip down,
//  - decodes the airbrush finger wheel (packet type 0x0A), which the
//    built-in parser mistakes for a mouse report, and
//  - decodes Art Pen / marker barrel rotation (packet type 0x05).
//
// Use it by pointing a tablet configuration's ReportParser at
// "OtdWintab.Parsers.WacomIntuos3ReportParser" (see configurations/).
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using OpenTabletDriver.Plugin.Tablet;

namespace OtdWintab.Parsers;

[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicParameterlessConstructor)]
public class WacomIntuos3ReportParser : IReportParser<IDeviceReport>
{
    /// <summary>OTD_WINTAB_RAWLOG=1 logs every packet except plain pen
    /// packets to OpenTabletDriver's log, for protocol diagnosis.</summary>
    private static readonly bool RawLog = Environment.GetEnvironmentVariable("OTD_WINTAB_RAWLOG") == "1";

    private uint _toolId;       // 0 = unknown, packets are dropped as in Linux
    private ulong _serial;
    private uint _pressure;
    private Vector2 _tilt;
    private Vector2 _position;
    private bool[] _penButtons = new bool[2];
    private float _wheel;
    private float _rotation;
    private bool _hasWheel, _hasRotation;

    public virtual IDeviceReport Parse(byte[] data)
    {
        var report = ParseReport(data);
        if (RawLog && !(report is IntuosPenReport && ((data[1] >> 1) & 0x0F) <= 3))
            OpenTabletDriver.Plugin.Log.Write("otd-wintab raw",
                $"{Convert.ToHexString(data)} -> {report.GetType().Name} (tool {_toolId:X})");
        return report;
    }

    private IDeviceReport ParseReport(byte[] data)
    {
        if (data.Length < 10)
            return new DeviceReport(data);

        switch (data[0])
        {
        case 0x02:      // WACOM_REPORT_PENABLED
        case 0x10:      // WACOM_REPORT_INTUOS_PEN
            return ParsePen(data);
        case 0x0C:      // express keys
            return new Intuos3PadReport(data);
        case 0x03:
            return new IntuosV1PadReport(data);
        default:
            return new DeviceReport(data);
        }
    }

    private IDeviceReport ParsePen(byte[] d)
    {
        byte b1 = d[1];

        if ((b1 & 0xFC) == 0xC0)            /* enter: tool id and serial */
        {
            _serial = ((ulong)(d[3] & 0x0F) << 28) + ((ulong)d[4] << 20) + ((ulong)d[5] << 12) +
                      ((ulong)d[6] << 4) + (ulong)(d[7] >> 4);
            _toolId = (uint)((d[2] << 4) | (d[3] >> 4) | ((d[7] & 0x0F) << 16) | ((d[8] & 0xF0) << 8));
            _pressure = 0;
            _penButtons = new bool[2];
            _hasWheel = _hasRotation = false;
            _wheel = _rotation = 0;
            return new IntuosToolReport(d, _toolId, _serial);
        }
        if ((b1 & 0xFE) == 0x20)            /* in range, no data */
            return new DeviceReport(d);
        if ((b1 & 0xFE) == 0x80)            /* exit */
        {
            if (_toolId == 0)
                return new DeviceReport(d); /* Linux: no exit for an unknown tool */

            /* wacom_exit_report: everything released, at the last position */
            var leaving = new IntuosLeavingReport
            {
                Raw = d,
                Position = _position,
                Pressure = 0,
                PenButtons = new bool[2],
                Tilt = _tilt,
                Eraser = (_toolId & 0x0008) != 0,
                ToolId = _toolId,
                ToolSerial = _serial,
                NearProximity = false,
            };
            _toolId = 0;
            _pressure = 0;
            _penButtons = new bool[2];
            return leaving;
        }

        if (_toolId == 0 || IsPuck(_toolId))
            return new DeviceReport(d);     /* unknown tool, or mouse/lens cursor */

        var position = new Vector2(
            ((d[2] << 8 | d[3]) << 1) | ((d[9] >> 1) & 1),
            ((d[4] << 8 | d[5]) << 1) | (d[9] & 1));
        uint distance = (uint)(d[9] >> 2);
        var tilt = new Vector2(
            (((d[7] << 1) & 0x7E) | (d[8] >> 7)) - 64,
            (d[8] & 0x7F) - 64);

        switch ((b1 >> 1) & 0x0F)
        {
        case 0x00: case 0x01: case 0x02: case 0x03:     /* general pen packet */
            _pressure = (uint)((d[6] << 3) | ((d[7] & 0xC0) >> 5) | (b1 & 1));
            _tilt = tilt;
            _penButtons = new[] { (b1 & 2) != 0, (b1 & 4) != 0 };
            break;
        case 0x0A:                                      /* airbrush second packet */
            _wheel = ((d[6] << 2) | ((d[7] >> 6) & 3)) / 1023f;
            _hasWheel = true;
            _tilt = tilt;
            break;
        case 0x05:                                      /* Art Pen / marker rotation */
        {
            int t = (d[6] << 3) | ((d[7] >> 5) & 7);
            t = (d[7] & 0x20) != 0 ? (t > 900 ? (t - 1) / 2 - 1350 : (t - 1) / 2 + 450) : 450 - t / 2;
            /* Linux range -900..899 over a full turn */
            _rotation = ((t + 900) % 1800 + 1800) % 1800 * 0.2f;
            _hasRotation = true;
            break;
        }
        default:
            return new DeviceReport(d);
        }

        _position = position;
        var report = _hasWheel ? new IntuosAirbrushReport { Wheel = _wheel }
                   : _hasRotation ? new IntuosArtPenReport { Rotation = _rotation }
                   : new IntuosPenReport();
        report.Raw = d;
        report.Position = position;
        report.Pressure = _pressure;
        report.Tilt = _tilt;
        report.PenButtons = _penButtons;
        report.Eraser = (_toolId & 0x0008) != 0;
        report.ToolId = _toolId;
        report.ToolSerial = _serial;
        report.NearProximity = true;
        report.HoverDistance = distance;
        return report;
    }

    /// <summary>wacom_intuos_get_tool_type: mice and lens cursors</summary>
    internal static bool IsPuck(uint toolId) => toolId switch
    {
        0x007 or 0x09C or 0x094 or 0x017 or 0x806 => true,     /* mouse */
        0x096 or 0x097 or 0x006 => true,                       /* lens cursor */
        _ => false,
    };
}

/// <summary>Intuos3 as seen through Wacom's driver: one extra leading byte.</summary>
[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicParameterlessConstructor)]
public class WacomDriverIntuos3ReportParser : WacomIntuos3ReportParser
{
    public override IDeviceReport Parse(byte[] data) =>
        data.Length > 1 ? base.Parse(data[1..]) : new DeviceReport(data);
}

public class IntuosToolReport : IToolReport
{
    public IntuosToolReport(byte[] raw, uint toolId, ulong serial)
    {
        Raw = raw;
        RawToolID = toolId;
        Serial = serial;
        Tool = (toolId & 0x0008) != 0 ? ToolType.Eraser : ToolType.Pen;
    }

    public byte[] Raw { set; get; }
    public ulong Serial { set; get; }
    public uint RawToolID { set; get; }
    public ToolType Tool { set; get; }
}

public class IntuosPenReport : ITabletReport, ITiltReport, IEraserReport, IProximityReport, IToolIdentityReport
{
    public byte[] Raw { set; get; } = Array.Empty<byte>();
    public uint ToolId { set; get; }
    public ulong ToolSerial { set; get; }
    public bool IsEraserEnd => Eraser;
    public Vector2 Position { set; get; }
    public uint Pressure { set; get; }
    public bool[] PenButtons { set; get; } = Array.Empty<bool>();
    public Vector2 Tilt { set; get; }
    public bool Eraser { set; get; }
    public bool NearProximity { set; get; }
    public uint HoverDistance { set; get; }
}

/// <summary>The exit packet, as the tool's final, fully released report.</summary>
public class IntuosLeavingReport : IntuosPenReport, IToolLeavingReport
{
}

public class IntuosAirbrushReport : IntuosPenReport, IAirbrushWheelReport
{
    public float Wheel { set; get; }
}

public class IntuosArtPenReport : IntuosPenReport, IBarrelRotationReport
{
    public float Rotation { set; get; }
}

/// <summary>Intuos3 express keys (report 0x0C), as OpenTabletDriver reads them.</summary>
public class Intuos3PadReport : IAuxReport
{
    public Intuos3PadReport(byte[] d)
    {
        Raw = d;
        AuxButtons = new[]
        {
            (d[5] & 1) != 0, (d[5] & 2) != 0, (d[5] & 4) != 0, (d[5] & 8) != 0,
            (d[6] & 1) != 0, (d[6] & 2) != 0, (d[6] & 4) != 0, (d[6] & 8) != 0,
        };
    }

    public byte[] Raw { set; get; }
    public bool[] AuxButtons { set; get; }
}

/// <summary>Report 0x03 express keys, as OpenTabletDriver reads them.</summary>
public class IntuosV1PadReport : IAuxReport
{
    public IntuosV1PadReport(byte[] d)
    {
        Raw = d;
        AuxButtons = Enumerable.Range(0, 8).Select(i => (d[4] & (1 << i)) != 0).ToArray();
    }

    public byte[] Raw { set; get; }
    public bool[] AuxButtons { set; get; }
}
