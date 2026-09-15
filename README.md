# AutoJankyBorders

<img align="right" width="50%" src="images/screenshot.png" alt="Screenshot">

*AutoJankyBorders* is a community fork of
[JankyBorders](https://github.com/FelixKratz/JankyBorders) by Felix Kratz. It
adds automatic per-window border colors sampled from window headers, along
with an inverse-color mode. This fork is not affiliated with or maintained by
the original JankyBorders project.

Like the original project, AutoJankyBorders is a lightweight macOS utility for
drawing borders around user windows. The original behavior and static color
options remain available.

## Fork features

- `color=auto` selects a dominant color from each window's header.
- `color=inverse` uses the inverse of the detected header color.
- Colors are cached per window so resizing remains responsive.
- `default_color=0xAARRGGBB` provides a configurable capture fallback.
- Existing per-window settings through `apply-to=<window-id>` are preserved.

## Usage

### Build this fork

```bash
git clone https://github.com/spoulin/AutoJankyBorders.git
cd AutoJankyBorders
make
./bin/borders color=auto default_color=0xffe1e3e4
```

Automatic modes require Screen Recording permission in **System Settings →
Privacy & Security → Screen & System Audio Recording**. When permission or
window capture is unavailable, `default_color` is used.

### About the Homebrew version

The official Homebrew formula installs upstream JankyBorders and does not
include this fork's automatic modes:

```bash
brew tap FelixKratz/formulae
brew install borders
```

Run `./bin/borders` directly after building this fork, or replace the Homebrew
link locally if you specifically want the `borders` command to use this build.

### Bootstrap with yabai
For example, if you are using `yabai`, you could add:
```bash
borders active_color=0xffe1e3e4 inactive_color=0xff494d64 width=5.0 &
```
to the very end of your `yabairc`. This will start the borders with the
specified options along with yabai.

### Bootstrap with AeroSpace
You could add:
```toml
after-startup-command = [
  'exec-and-forget borders active_color=0xffe1e3e4 inactive_color=0xff494d64 width=5.0'
]
```
to you `aerospace.toml`. This will start borders with the specified options
along with AeroSpace.

### Bootstrap with brew
The following command starts the upstream Homebrew build, not AutoJankyBorders,
unless you provide your own formula or service definition:
```bash
brew services start borders
```

### Configuring the appearance
You can either configure the appearance directly when starting the borders
process (as shown in "Bootstrap with yabai") or use a configuration file.
The appearance can be adapted at any point in time.

#### Using a configuration file (Optional)
If the primary `borders` process is started without any arguments (or launched
as a service by brew), it will search for a file at
`~/.config/borders/bordersrc` and execute it on launch if found.

An example configuration file could look like this:
`~/.config/borders/bordersrc`
```bash
#!/bin/bash

options=(
	style=round
	width=6.0
	hidpi=off
	active_color=0xffe2e2e3
	inactive_color=0xff414550
)

borders "${options[@]}"
```

#### Automatic per-window colors

Use `color=auto` to sample a narrow band in each window's header. Each window
keeps its own sampled color. If macOS does not allow the capture (for
example, before Screen Recording permission is granted), `default_color` is
used instead:
```bash
borders color=auto default_color=0xffe1e3e4 width=5.0
```

The alpha component of `default_color` is also applied to successfully sampled
colors. Passing `color=0xAARRGGBB`, `active_color=...`, or `inactive_color=...`
switches automatic color back off.

Use `color=inverse` to sample the same per-window header color and invert its
RGB components. For example, black becomes white while the configured alpha
is preserved. `default_color` remains unchanged when capture fails.

#### Updating the border properties during runtime
If a `borders` process is already running, invoking a new `borders` instance
with any combination of the available options will update the properties of
the already running instance.

## Documentation

The updated local manual sources are in `docs/`. The upstream manual is
available in the
[JankyBorders Wiki](https://github.com/FelixKratz/JankyBorders/wiki/Man-Page),
but it does not document this fork's automatic modes.

## License and attribution

AutoJankyBorders remains licensed under the GNU General Public License v3.0,
as required by the original project. See [LICENSE](LICENSE). Copyright and
credit for the original JankyBorders implementation remain with its original
authors; subsequent changes are maintained in this fork's Git history.
