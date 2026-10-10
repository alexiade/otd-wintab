// otd-wintab: turns a tablet's report stream into ring events: proximity
// in/out per physical tool, and packets. Independent of OpenTabletDriver's
// types so it can be tested on its own.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using System.Numerics;

namespace OtdWintab.Core;

/// <summary>A physical tool end: tool type id, serial, and which end.</summary>
public readonly record struct ToolIdentity(uint ToolId, uint Serial, bool Eraser);

/// <summary>One position report, already mapped to the screen.</summary>
public struct PenSample
{
    public Vector2 Position;    // virtual-screen pixels
    public float Pressure;      // 0..1
    public Vector2? Tilt;       // degrees, + = right / toward the user
    public bool Tip;
    public bool LowerBarrel, UpperBarrel;
    public bool? Eraser;        // from the report, when the tablet flags it
    public float? Wheel;        // 0..1 (airbrush finger wheel)
    public float? Rotation;     // degrees 0..360 (barrel rotation)
    public ToolIdentity? Tool;  // when the report itself names the tool
}

public sealed class ToolTracker
{
    private readonly IEventSink _sink;
    private readonly StylusDatabase _stylus;
    private readonly uint _vendor;
    private readonly object _lock = new();

    private ToolIdentity? _reported;    // from the tablet's tool (proximity) report
    private bool _inProximity;
    private ToolIdentity _current;
    private string _currentName = "";
    private Axes _currentAxes;
    private uint _lastReportMs;

    public ToolTracker(IEventSink sink, StylusDatabase stylus, uint vendorId)
    {
        _sink = sink;
        _stylus = stylus;
        _vendor = vendorId;
    }

    public bool InProximity { get { lock (_lock) return _inProximity; } }

    /// <summary>
    /// The tablet reports leaving proximity itself, so silence means nothing:
    /// a tablet may go quiet for a while during contact (an Intuos3 does, and
    /// then repeats its proximity packet). Turns the watchdog off.
    /// </summary>
    public bool ReportsLeaving { get; set; }

    /// <summary>The tablet identified the tool entering proximity.</summary>
    public void OnToolReport(uint toolId, ulong serial, bool eraser, uint nowMs)
    {
        lock (_lock)
        {
            _reported = new ToolIdentity(toolId, (uint)serial, eraser);
            _lastReportMs = nowMs;
        }
    }

    public void OnSample(in PenSample s, uint nowMs)
    {
        lock (_lock)
        {
            var id = Resolve(s);
            if (_inProximity && id != _current)
                Leave(nowMs);
            if (!_inProximity)
                Enter(id, s, nowMs);
            Emit(EventType.Packet, s, nowMs);
            _lastReportMs = nowMs;
        }
    }

    /// <summary>The tablet reported the tool leaving.</summary>
    public void OnOutOfRange(uint nowMs)
    {
        lock (_lock)
        {
            if (_inProximity) Leave(nowMs);
        }
    }

    /// <summary>Proximity watchdog for tablets/parsers that don't report
    /// leaving: no report for timeoutMs means the tool left.</summary>
    public void Tick(uint nowMs, uint timeoutMs)
    {
        lock (_lock)
        {
            if (_inProximity && !ReportsLeaving && unchecked(nowMs - _lastReportMs) >= timeoutMs)
                Leave(nowMs);
        }
    }

    private ToolIdentity Resolve(in PenSample s)
    {
        bool eraser = s.Eraser == true;
        if (s.Tool is { } own)          // the report names its tool: authoritative
        {
            _reported = own;
            return own with { Eraser = own.Eraser || eraser };
        }
        if (_reported is { } r)
            return r with { Eraser = r.Eraser || eraser };
        return new ToolIdentity(0, 0, eraser);
    }

    private void Enter(ToolIdentity id, in PenSample s, uint nowMs)
    {
        _current = id;
        var info = id.ToolId != 0 ? _stylus.Lookup(_vendor, id.ToolId) : null;

        if (info != null)
        {
            _currentName = info.Name;
            _currentAxes = info.Axes | Axes.Pressure;
        }
        else
        {
            _currentName = id.ToolId != 0 ? $"Tool 0x{id.ToolId:X}" : "Pen";
            _currentAxes = Axes.Pressure;
        }
        if (id.Eraser && !_currentName.Contains("Eraser", StringComparison.OrdinalIgnoreCase))
            _currentName += " Eraser";

        // capabilities the reports show, even if the database doesn't know them
        if (s.Tilt.HasValue) _currentAxes |= Axes.Tilt;
        if (s.Wheel.HasValue) _currentAxes |= Axes.Wheel;
        if (s.Rotation.HasValue) _currentAxes |= Axes.Rotation;

        _inProximity = true;
        Emit(EventType.ProximityIn, s, nowMs);
    }

    private void Leave(uint nowMs)
    {
        var ev = Base(EventType.ProximityOut, nowMs);
        _sink.Publish(ev);
        _inProximity = false;
    }

    private OtdwtEvent Base(EventType type, uint nowMs)
    {
        var ev = new OtdwtEvent
        {
            Type = type,
            Flags = _current.Eraser ? EventFlags.Eraser : EventFlags.None,
            Axes = _currentAxes,
            ToolId = _current.ToolId,
            Serial = _current.Serial,
            TimeMs = nowMs,
        };
        ev.SetName(_currentName);
        return ev;
    }

    private void Emit(EventType type, in PenSample s, uint nowMs)
    {
        var ev = Base(type, nowMs);
        ev.X = (int)MathF.Round(s.Position.X * RingProtocol.PositionScale);
        ev.Y = (int)MathF.Round(s.Position.Y * RingProtocol.PositionScale);
        ev.Pressure = (uint)Math.Clamp(MathF.Round(s.Pressure * RingProtocol.PressureMax), 0, RingProtocol.PressureMax);
        ev.Buttons = (s.Tip ? 1u : 0) | (s.LowerBarrel ? 2u : 0) | (s.UpperBarrel ? 4u : 0);
        if (s.Tilt is { } t)
        {
            ev.TiltX = (int)MathF.Round(t.X * 10);
            ev.TiltY = (int)MathF.Round(t.Y * 10);
        }
        if (s.Wheel is { } w)
            ev.Wheel = (uint)Math.Clamp(MathF.Round(w * RingProtocol.WheelMax), 0, RingProtocol.WheelMax);
        if (s.Rotation is { } r)
            ev.Rotation = ((int)MathF.Round(r * 10) % 3600 + 3600) % 3600;
        if (type == EventType.ProximityIn)
            ev.Buttons = 0;   // nothing is pressed yet as the tool arrives
        _sink.Publish(ev);
    }
}
