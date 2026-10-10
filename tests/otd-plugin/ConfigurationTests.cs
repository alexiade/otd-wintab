// otd-wintab plugin tests: the tablet configuration overrides.
//
// Copyright 2026 otd-wintab contributors
// SPDX-License-Identifier: LGPL-2.1-or-later

using Newtonsoft.Json;
using OpenTabletDriver.Plugin.Tablet;
using OtdWintab.Parsers;
using Xunit;

namespace OtdWintab.Tests;

public class ConfigurationTests
{
    public static IEnumerable<object[]> Files()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
        {
            var dir = Path.Combine(d.FullName, "configurations");
            if (Directory.Exists(dir) && File.Exists(Path.Combine(d.FullName, "build.ps1")))
                return Directory.GetFiles(dir, "*.json").Select(f => new object[] { Path.GetFileName(f), f });
        }
        throw new DirectoryNotFoundException("configurations/");
    }

    [Theory]
    [MemberData(nameof(Files))]
    public void Override_uses_a_parser_from_this_plugin(string name, string path)
    {
        var config = JsonConvert.DeserializeObject<TabletConfiguration>(File.ReadAllText(path))!;
        Assert.StartsWith("Wacom PTZ-", config.Name);
        Assert.NotEmpty(config.DigitizerIdentifiers);

        foreach (var id in config.DigitizerIdentifiers)
        {
            // OpenTabletDriver resolves the name through its plugin manager
            var type = typeof(WacomIntuos3ReportParser).Assembly.GetType(id.ReportParser);
            Assert.True(type != null, $"{name}: unknown parser {id.ReportParser}");
            Assert.True(typeof(IReportParser<IDeviceReport>).IsAssignableFrom(type), $"{name}: {type} is no parser");
            Assert.NotNull(Activator.CreateInstance(type!));
        }
    }
}
