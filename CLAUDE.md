# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

AutoJankyBorders is a macOS-only C99 fork of [JankyBorders](https://github.com/FelixKratz/JankyBorders). It draws borders around windows using the private SkyLight framework. The fork adds per-window colors sampled from window contents (`color=auto`, `inverse`, `auto-gradient`, `inverse-gradient`). Upstream behavior and static color options must keep working.

## Commands

```bash
make            # release build: bin/borders
make menubar    # menu only: bin/bordersbar
make app        # all-in-one bundle: bin/AutoJankyBorders.app (menu + daemon, ad hoc signed)
make install-app # copies the bundle to /Applications
make debug      # -DDEBUG build: bin/debug (enables debug() output and a watcher on all SkyLight events)
make asan       # ASan/UBSan build of bin/debug, then runs it
make test       # builds and runs bin/auto_color_test and bin/menubar_options_test
make clean      # removes bin/
```

There is no linter, and `make test` has no way to select a single case. New `.c` files must be added to `FILES` or `MENUBAR_FILES` in the `makefile`.

## Running a local build

- The binary is both server and client. If an instance is already registered under `BS_NAME` (`src/mach.h`), a new invocation with valid arguments forwards them to that instance over Mach and exits, so your new code does not run. Stop any running instance first (`brew services stop autojankyborders`, `pkill borders`).
- `BS_NAME` is still upstream's `git.felix.borders`, so this fork and upstream `borders` cannot run side by side, and either one will send its arguments to the other.
- With no arguments, a new server executes `~/.config/borders/bordersrc`.
- Automatic color modes need Screen Recording permission. A rebuilt executable may need it granted again. When capture fails, `default_color` is used and cached per window, so relaunch after granting permission.

## Architecture

**Process flow (`src/main.c`):** parse arguments, then either forward them to a running server or become the server: register SkyLight event callbacks, create borders for existing windows, start the Mach server, and run `CFRunLoopRun()`. `message_handler` applies settings received from later invocations.

**Core types (`src/border.h`):** `g_windows` (`src/hashtable.c`) maps a target window id to a `struct border`. Each border owns a separate SkyLight connection and overlay window, sized to the target plus `border_width + BORDER_PADDING` and ordered relative to the target through SkyLight transactions.

**Updates:**
- `src/events.c` receives SkyLight notifications and calls into `src/windows.c`, which operates on the table and calls `border_update` / `border_move` / `border_hide` in `src/border.c`.
- `border_update` is effectively synchronous: it returns before the async path below it, which is dead code. Only `border_move` works off the main thread.
- `border_get_settings` asserts it is on the main thread.
- Delayed work must not hold a `struct border*` across the delay, because the border may be destroyed. Capture the window id and look it up again with `table_find`, and use a generation counter to drop stale work (see `color_refresh_generation` and `color_transition_generation`).

**Settings:**
- `g_settings` holds global settings; `border->setting_override` holds a per-window copy created by `apply-to=<wid>`. Global messages are also re-applied to existing overrides. Always read settings through `border_get_settings`.
- To add an option, update `struct settings` (`src/border.h`), its default (`src/main.c`), and `parse_settings` (`src/parse.c`), then the docs listed below.
- `parse_settings` matches by prefix in order, so branch order matters (exact `color=auto*` matches come before the general `color=` prefix).
- It returns a `BORDER_UPDATE_MASK_*` value that selects the update: `ACTIVE`/`INACTIVE`/`ALL` redraw, `RECREATE_ALL` rebuilds all borders, and `SETTING` only stores the value. A zero mask means no valid arguments, so return a nonzero mask for every accepted option or the client will not forward it.

**Automatic color (`src/auto_color.c`, driven from `border_draw`):**
- Capture happens lazily during drawing, only when `needs_color_sample` is set or no sample is cached. The result is cached on the border, including the fallback, so resizing never triggers captures.
- Sampling triggers: focus changes, window title/update events (debounced 150 ms), settings changes, and the `color_refresh` timer. The timer ticks every 100 ms, samples only the focused window, and waits until geometry has been stable for 250 ms.
- `color_transition` animates the cached colors on the main queue about every 16 ms without taking new captures. Differences at or below `color_threshold` are ignored.
- The `*_rgba` functions are pure pixel logic. `make test` links only `auto_color.c` with CoreGraphics and CoreFoundation, so that file must not depend on SkyLight or other project sources.

**Private APIs (`src/misc/`):** mostly header-only `static inline` helpers. `extern.h` declares the SkyLight functions and `window.h` holds window queries, including `window_suitable`, which decides which windows get borders. Symbols that are not available on every macOS version are resolved with `dlsym` (`SLSWindowIteratorGetCornerRadii` in `load_symbols`, `CGWindowListCreateImage` in `auto_color.c`).

**yabai integration (`src/misc/yabai.h`):** enabled by the `#define _YABAI_INTEGRATION` in that header. It creates proxy borders (`is_proxy`, `external_proxy_wid`) that track window proxies during yabai animations, using `src/animation.c` (CVDisplayLink).

**Menu bar app (`src/menubar/`):** a process separate from the daemon, even inside `AutoJankyBorders.app`, where both executables sit in `Contents/MacOS`. It is deliberately not part of the daemon: `event_callback` in `src/main.c` drains the main SkyLight connection's events, which AppKit also needs, and the daemon's run loop sources are registered only in the default mode, so they would stall while a menu is open.
- It sends arguments straight over Mach using `src/mach.c`. Do not run `borders <args>` from it: with no daemon registered, that child becomes the daemon.
- Mach messages are one-way, so the daemon's real settings cannot be read. The menu shows `bordersrc` values (best-effort scan) overlaid with the saved choices in `~/.config/borders/menu-options`, and resends those choices whenever the daemon reappears.
- `app_bundled()` selects the mode. The bundle starts its own daemon on launch and stops it on quit, and macOS attributes Screen Recording to the app. The bare `bordersbar` leaves the daemon running on quit and disclaims responsibility so that permission stays with `borders`.
- Spawned daemons reset signal dispositions, because AppKit ignores `SIGTERM` and a child inherits that.
- `options.c` is pure C covered by `make test`; AppKit, `launchctl`, and process handling stay in `menubar.m`.
- Daemon options offered in the menu must be added to `options.c` (with validation that matches `parse_settings`) and to the choice tables in `menubar.m`.

## Documentation and packaging

- A user-visible option must be documented in `README.md`, `docs/borders.1.scd`, and `docs/borders.1`. Both man page files are committed and there is no make target to regenerate `borders.1`; `Formula/autojankyborders.rb` installs it as is.
- The formula is HEAD-only and tracks `main`. It runs `make` and `make menubar`, so the default target must keep producing `bin/borders`. Its service label varies by Homebrew version (`sh.brew.autojankyborders` or `homebrew.mxcl.autojankyborders`); `menubar.m` handles both.
- The README promises that captured pixels are never saved or sent anywhere and that only the resulting colors are cached. Keep that true.
- The project is GPL-3.0, and credit to the original project stays in place.
