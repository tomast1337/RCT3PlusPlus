rct3++ is in development: expect missing features and bugs. Reports are welcome in the issues; say what
`rct3pp --version` prints.

**You need your own copy of RollerCoaster Tycoon 3 Complete Edition** (for example from Steam). rct3++
ships no game content: it reads the data from your install.

### Running it

1. Download the package for your system below and unpack it.
2. rct3++ looks for the game by itself: in the folder it was unpacked into (unpack it into the game's
   folder, the one that contains `gui/` and `Style/`, or into a folder inside it), in the Steam libraries,
   in Epic's installs, and in the usual GOG and disc install folders. If it does not find it, tell it where the game is: either set an
   environment variable,

   ```
   RCT3PP_GAME_PATH="/path/to/RollerCoaster Tycoon 3" ./rct3pp
   ```

   on Windows, in PowerShell:

   ```
   $env:RCT3PP_GAME_PATH = "C:\Program Files (x86)\Steam\steamapps\common\RollerCoaster Tycoon 3 Complete Edition"
   .\rct3pp.exe
   ```

   or write it once into the settings file, as `game_path`; `rct3pp --help` shows where that file is, and
   `rct3pp --write-settings` creates it.
3. Without a graphics card that does Vulkan, start it with `--renderer software` (slower, drawn on the CPU) or
   `--renderer opengl`.

**Linux:** x86-64, glibc 2.38 or newer (Ubuntu 24.04, Debian 13, Fedora 39 or later), and the Vulkan loader
(`libvulkan1` or `vulkan-loader`), which most desktops have. Everything else is built into the binary.

**Windows:** 64-bit Windows 10 or 11. Unpack the .zip and run `rct3pp.exe`; it needs no installer and no other
files. Windows may warn that the program is from an unknown publisher, as the builds are not signed: choose
"More info", then "Run anyway". The game's music and movies play through Windows' own Media Foundation, which
the N editions of Windows lack until the Media Feature Pack is installed.
