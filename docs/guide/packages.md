# Packages

Nyx has a built-in package manager. Install libraries, pin versions, and import them like any other module.

## Installing packages

```bash
nyx install http                    # latest from registry
nyx install http 1.2.0              # specific version
nyx install https://github.com/user/nyx-http.git   # direct git URL
```

Packages are installed to `NYX_HOME/libs/<name>/<version>/`.

## Using packages

Once installed, import them like any module:

```nyx
import http;

var response = http.get("https://example.com");
print(response.body);
```

Nyx looks for modules in this order:
1. **Relative to current file** — `./http.nyx`
2. **Project-local** — `./nyx_modules/http/lib.nyx`
3. **Global** — `NYX_HOME/libs/http/<version>/lib.nyx`

## Project manifest

Create a `nyx.toml` in your project root to pin dependency versions:

```toml
[project]
name = "my_app"
version = "0.1.0"

[dependencies]
http = "1.0.0"
json = "0.3.0"
```

Without a manifest, Nyx picks the latest installed version.

## Creating a package

A package is just a folder with a `nyx.toml` and a `lib.nyx` entry point:

```
my_library/
├── nyx.toml
├── lib.nyx          # entry point
└── src/
    └── internal.nyx
```

The manifest:

```toml
[package]
name = "my_library"
version = "1.0.0"
entry = "lib.nyx"
description = "What this library does"
author = "Your Name"
repository = "https://github.com/you/my_library"
```

Everything defined in `lib.nyx` (functions, classes, variables) becomes available to importers.

## Native packages

Packages can include C shared libraries for platform-specific functionality (audio, networking, graphics, etc.):

```
nyx-sound/
├── nyx.toml
├── lib.nyx
└── bin/
    ├── sound-windows-x64.dll
    ├── sound-linux-x64.so
    └── sound-macos-arm64.dylib
```

The `lib.nyx` loads the right binary automatically:

```nyx
load_native("bin/sound-" + platform() + "-" + arch());
// Sound class is now available
```

When you `nyx build` a project that uses native packages, the binaries get embedded in the `.nyxc` — one file, all platforms, runs anywhere. See [Native Modules](/embedding/native-modules) for how to write the C side.

## Project-local overrides

Drop a package into `nyx_modules/` in your project to override the global version:

```
my_project/
├── nyx_modules/
│   └── http/
│       └── lib.nyx    # this version takes priority
├── main.nyx
└── nyx.toml
```

Useful for patching a dependency without modifying the global install.

## Managing packages

```bash
nyx list                    # show installed packages
nyx uninstall http          # remove all versions
nyx uninstall http 1.0.0    # remove specific version
nyx registry update         # update package registry
```

## NYX_HOME

The `NYX_HOME` environment variable points to your Nyx installation directory. The installer sets this automatically. Structure:

```
NYX_HOME/
├── nyx.exe
├── registry.txt            # package name → git URL mapping
└── libs/
    ├── http/
    │   └── 1.0.0/
    │       ├── nyx.toml
    │       └── lib.nyx
    └── json/
        └── 0.3.0/
            └── ...
```

## Registry

The registry is a simple text file mapping package names to git URLs:

```
http=https://github.com/someone/nyx-http.git
json=https://github.com/someone/nyx-json.git
```

Run `nyx registry update` to fetch the latest registry from `nyx.nemesistech.ee`. You can also install directly from any git URL without the registry.
