# Tablet configuration overrides

OpenTabletDriver's configurations for the Wacom Intuos3 family (PTZ-xxx),
taken from [OpenTabletDriver](https://github.com/OpenTabletDriver/OpenTabletDriver)
`v0.6.7` (`OpenTabletDriver.Configurations/Configurations/Wacom`), with one
change: the report parser is otd-wintab's
`OtdWintab.Parsers.WacomIntuos3ReportParser` (and its Wacom-driver variant).

Placed in `%LOCALAPPDATA%\OpenTabletDriver\Configurations`, they override the
built-in configurations of the same name. With the otd-wintab plugin
installed this gives Intuos3 tablets reliable tool identification (serial,
eraser end), immediate proximity-out, the airbrush finger wheel and Art Pen
rotation. Without the plugin installed, OpenTabletDriver can't find the
parser and the tablet won't work, so `scripts/install-otd.ps1` installs both
together and removes both together.

Regenerate with `scripts/gen-configs.ps1`. These files derive from
OpenTabletDriver and are covered by its license (GNU LGPL 3.0).
