// otd-wintab plugin tests: the libwacom stylus database.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using OtdWintab.Core;
using Xunit;

namespace OtdWintab.Tests;

public class StylusDatabaseTests
{
    private readonly StylusDatabase _db = StylusDatabase.LoadEmbedded();

    [Fact]
    public void Loads_the_embedded_database()
    {
        Assert.True(_db.Count > 50, $"only {_db.Count} entries");
    }

    [Theory]
    [InlineData(0x823u, "Grip Pen", false, Axes.Pressure | Axes.Tilt)]
    [InlineData(0x82Bu, "Grip Pen Eraser", true, Axes.Pressure | Axes.Tilt)]
    [InlineData(0x913u, "Airbrush Pen", false, Axes.Pressure | Axes.Tilt | Axes.Wheel)]
    [InlineData(0x885u, "6D Art Pen", false, Axes.Pressure | Axes.Tilt | Axes.Rotation)]
    public void Intuos3_tools(uint id, string name, bool eraser, Axes axes)
    {
        var info = _db.Lookup(StylusDatabase.WacomVendor, id);
        Assert.NotNull(info);
        Assert.Equal(name, info!.Name);
        Assert.Equal(eraser, info.IsEraser);
        Assert.Equal(axes, info.Axes);
    }

    [Fact]
    public void Unknown_ids_are_null()
    {
        Assert.Null(_db.Lookup(StylusDatabase.WacomVendor, 0xABCDE));
        Assert.Null(_db.Lookup(0x1234, 0x823));
    }

    [Fact]
    public void Parses_the_key_file_format()
    {
        var db = StylusDatabase.Parse("""
            # comment
            [0x56a:0x123]
            Name=Test Pen
            Type=Airbrush
            Axes=Pressure;RotationZ;

            [0x56a:0x12b]
            Name=Test Eraser
            EraserType=Invert
            """);
        Assert.Equal(2, db.Count);
        Assert.Equal(Axes.Pressure | Axes.Rotation | Axes.Wheel, db.Lookup(0x56a, 0x123)!.Axes);
        Assert.True(db.Lookup(0x56a, 0x12b)!.IsEraser);
    }
}
