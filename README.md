# Freecam for LeviLauncher

A Freecam-style native mod for **Minecraft Bedrock 1.26.52 (Android)**, packaged as a
`.levipack` for [LeviLaunchroid](https://github.com/LiteLDev/LeviLaunchroid), plus a small
Android library (AAR) for reading and writing its settings from Kotlin or Java.

The pack icon is the icon of the original [Freecam](https://github.com/MinecraftFreecam/Freecam)
mod (MIT, see `mod/assets/ICON-LICENSE.txt`).

> ## ⚠️ Read this first: the camera hooks are not implemented
>
> Detaching the camera from the player means hooking three functions inside
> `libminecraftpe.so` of **your exact Minecraft build**. This project cannot ship those: they
> need the 1.26.52 binary (signatures and function prototypes), which the author of this
> project had no access to. So today:
>
> * ✅ Implemented and tested on a host machine: the Mod Menu module, the floating **Cam**
>   button, both settings, the config file, the camera maths and the input logic
> * ✅ The pack satisfies LeviLauncher's import rules (checked with a port of them)
> * ⚠️ Never run on a device yet: the first CI build and the first load in LeviLauncher are the
>   real test (see [What has and has not been verified](#what-has-and-has-not-been-verified))
> * ❌ **The in-game camera does not move yet**
>
> The module description in the Mod Menu says so (`Note: …`) and logcat shows why. Section
> [Hook signatures](#hook-signatures) lists exactly what is missing and where it plugs in. Once
> the three detours exist, nothing else has to change.

---

## မြန်မာလို အကျဉ်းချုပ်

- ဒီ project က Minecraft Bedrock **1.26.52** (Android) အတွက် Freecam-style mod ကို LeviLaunchroid ရဲ့ `.levipack` format နဲ့ ထုတ်ပေးတဲ့ source project ပါ။ Pack icon ကို မူရင်း Freecam mod ရဲ့ icon နဲ့ ထားပါတယ်။
- Settings နှစ်ခု: **Show Name Tags** (toggle) နဲ့ **Movement Speed** (0.5 – 50 blocks/s)။ Mod Menu ထဲမှာရော launcher ရဲ့ config page ထဲမှာပါ ချိန်လို့ရပါတယ်။
- `aar/` ထဲမှာ Kotlin/Java ကနေ settings ကို ဖတ်/ရေးနိုင်တဲ့ Android Library (AAR) ပါပါတယ်။ AAR ထဲမှာ `.levipack` ကိုလည်း ထည့်ပေးထားနိုင်ပါတယ်။
- ⚠️ **ကန့်သတ်ချက်**: Game ရဲ့ camera ကို တကယ်ခွဲထုတ်ဖို့လိုတဲ့ hook (1.26.52 `libminecraftpe.so` signature + detour code) မပါသေးပါဘူး။ ဒါကြောင့် pack ကို import လုပ်ပြီး menu/settings ပေါ်လာရင်တောင် in-game မှာ camera မရွေ့သေးပါဘူး။ Menu/settings အပိုင်းကိုလည်း device ပေါ်မှာ မစမ်းရသေးဘဲ host test တွေနဲ့ပဲ စစ်ထားတာပါ။ ဘာလိုသလဲ ဆိုတာ [Hook signatures](#hook-signatures) မှာ ရေးထားပါတယ်။
- Build လုပ်နည်း: GitHub repository အသစ်တစ်ခုထဲ ဒီ folder ကို push → **Actions** tab → `Build Freecam` → ပြီးရင် artifacts ထဲက `freecam-levipack` (`freecam.levipack`) နဲ့ `freecam-aar` ကို download လုပ်ပါ။ NDK/Gradle ကို ဒီ project ရေးခဲ့တဲ့ ကွန်ပျူတာမှာ မရခဲ့လို့ Android build အပိုင်းကို CI က ပထမဆုံးအကြိမ် လုပ်မှာပါ။

---

## What you get

| Path | What it is |
|---|---|
| `mod/` | The native mod (C++20, arm64-v8a): Mod Menu module, floating button, settings, camera maths |
| `aar/` | Android library: `FreecamSettings`, `FreecamConfigStore`, `FreecamPackage` (Kotlin, usable from Java) |
| `tools/` | `package_levipack.py` (build + verify the pack), `embed_icon.py`, tests |
| `.github/workflows/build.yml` | CI: host tests → arm64 build → `freecam.levipack` → AAR → release on `v*` tags |

### Settings

| Setting | Type | Range / default | Meaning |
|---|---|---|---|
| **Show Name Tags** | toggle | on | Keep entity name tags visible while the camera is detached. Off hides them. |
| **Movement Speed** | slider | 0.5 – 50 blocks/s, default 10 | How fast the detached camera flies. |

*"Name tag" is read as the toggle for entity name tags in the camera view. If you meant
something else, it is a one-line change in `mod/src/Controller.cpp` (`shouldDrawNameTags`).*

Where to change them:

1. **In game**, Mod Menu → *Freecam* (applies immediately and is saved).
2. **In the launcher's config page** for the mod (it is generated from `config/config.schema.json`).
3. **From an app** via the AAR (`FreecamConfigStore`); picked up the next time the game starts.

Everything is stored in the mod's `config/config.json`:

```json
{ "version": 1, "showNameTags": true, "moveSpeed": 10.0 }
```

Out-of-range values are repaired on load (speed is clamped to 0.5 – 50; NaN/∞ fall back to 10) and
a corrupt file is replaced by the defaults. Re-importing a newer pack keeps your `config.json`; the icon, the schema and
`data/signatures.json` are refreshed (`overwrite_files` in the manifest).

### How it is meant to be used

Enable the **Freecam** module in the Mod Menu → a floating **Cam** button appears. Tap it to detach
the camera at the player's eye; move and look as usual (the player stands still); tap it again to
re-attach. Disabling the module removes the button and always ends the session.

*(This is the intended behaviour; it needs the hooks described below to have any effect in game.)*

---

## Build with GitHub Actions (recommended)

1. Create a GitHub repository and push this folder to it (`.github/` must be at the repository root).
2. Open **Actions → Build Freecam** (it runs on every push, or press *Run workflow*).
3. When it is green, download from the run page:
   * **`freecam-levipack`** → `freecam.levipack`
   * **`freecam-aar`** → `freecam-library-release.aar` (contains the pack as an asset)
4. Pushing a tag such as `v1.0.0` also creates a GitHub release with both files, and the tag
   becomes the pack's `version`.

What the pipeline does: Python tests → host build + C++ unit tests → NDK r28c arm64-v8a build →
strip → `package_levipack.py` (builds the zip and re-checks it against LeviLauncher's import
rules) → Kotlin unit tests → release AAR → check that the AAR really contains the same pack.

Then in LeviLauncher: import `freecam.levipack` (Mods screen), pick the Minecraft **1.26.52**
version, start the game, open the in-game Mod Menu.

## Build locally

Needs git, CMake ≥ 3.22, a C++20 compiler and network access (dependencies are fetched with
CMake `FetchContent`).

```sh
# 1. Host build: unit tests and the config generator
cmake -S mod -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python3 tools/test_package_levipack.py

# 2. The Android library (Android NDK r28c recommended)
cmake -S mod -B build/android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
cmake --build build/android --target freecam
$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip --strip-unneeded \
  build/android/out/arm64-v8a/libfreecam.so

# 3. The pack (also verifies it)
python3 tools/package_levipack.py \
  --so build/android/out/arm64-v8a/libfreecam.so \
  --config-gen build/host/freecam_config_gen --out dist
python3 tools/package_levipack.py --verify dist/freecam.levipack
```

AAR (JDK 17, Android SDK platform 35; the Gradle wrapper downloads Gradle 8.10.2):

```sh
cp dist/freecam.levipack aar/freecam-library/src/main/assets/   # optional: bundle the pack
cd aar && ./gradlew :freecam-library:testDebugUnitTest :freecam-library:assembleRelease
# → aar/freecam-library/build/outputs/aar/freecam-library-release.aar
```

### About the preloader SDK version

The mod is built against [`LiteLDev/preloader-android`](https://github.com/LiteLDev/preloader-android)
at commit `5d108da350ce3e7f13f51415f81e65344ecd5d14`, the revision LeviLaunchroid's `main`
pinned as its `preloader` submodule when this was written. Mod Menu structs cross the library
boundary, so **build against the same SDK revision as the launcher you run**:

```sh
cmake -S mod -B build/android … -DFREECAM_PRELOADER_REF=<tag-or-commit>
# or use a local checkout:        -DPRELOADER_ANDROID_ROOT=/path/to/preloader-android
```

---

## Hook signatures

`mod/src/GameBackend.cpp` is the only part that touches the Minecraft binary. It already does
this much:

1. reads `data/signatures.json` from the mod's data folder,
2. tries every rule in order and picks the first whose three entries resolve inside
   `libminecraftpe.so`,
3. reports the outcome in the Mod Menu description and in logcat.

What is **missing** is (a) the signature rules for 1.26.52 and (b) the detours installed at the
marked `EXTENSION POINT`. Together that is the whole job.

### `data/signatures.json`

```json
{
  "schemaVersion": 1,
  "module": "libminecraftpe.so",
  "rules": [
    {
      "label": "1.26.52",
      "cameraPose": "48 8B ?? ?? 89 …",
      "moveInput":  "…",
      "nameTag":    "…"
    }
  ]
}
```

Each value is anything `pl::memory::resolveSignature()` accepts: a byte pattern (`?` / `??` are
wildcards, `A?` / `?F` nibble wildcards) or an exported symbol name. Rules are tried in order, so
you can keep patterns for several builds in one file.

The status line you will see:

| Summary shown | Meaning |
|---|---|
| `data/signatures.json is missing …` / parse error | file absent or invalid |
| `No game signatures are bundled for this Minecraft build …` | `rules` is empty (**current state**) |
| `None of the signatures … match this Minecraft build.` | patterns do not resolve |
| `Signatures matched (<label>), but this build has no hook detours yet.` | rules fine, detours still to write |

### The three hook points

The detours talk to `FreecamController` (thread-safe, unit-tested), so they stay small:

| Hook | Where in the game | What the detour does |
|---|---|---|
| `cameraPose` | where the render camera is built each frame | after the original ran, call `controller.advance(dt, playerEye, input, look)`; if it returns a pose, write position/yaw/pitch into the camera the renderer uses |
| `moveInput` | where the local player's movement input is sampled | pass the sampled axes to `advance()`; while `controller.suppressPlayerInput()` is true, zero what the game applies to the player |
| `nameTag` | where entity name tags are drawn | skip the original call when `controller.shouldDrawNameTags()` is false |

Convention used by the maths (`mod/src/FreecamCore.cpp`): +X east, +Y up, +Z south; yaw 0 faces
+Z and 90° faces −X; positive pitch looks down.

Install them with `pl::memory::HookHandle` ([Hook API](https://github.com/LiteLDev/LeviLaunchroid/blob/main/docs/api/hook.md)),
keep the handles as `GameBackend` members, reset them in `detach()`, set
`mStatus.hooksInstalled = true`, and delete the `EXTENSION POINT` marker (the CI warning that
reminds you about the missing hooks keys off it). A detour must match the real function
prototype exactly, which is why this cannot be done without the 1.26.52 binary: load
`libminecraftpe.so` from your installed APK in a disassembler, find the three functions, and
write the byte patterns and prototypes down.

---

## Android library (AAR)

Group `dev.freecam.levi`, package `dev.freecam.levi`, `minSdk 24`, pure Kotlin, one dependency
(Gson, used internally).

**Why there is no native code in it:** LeviLauncher loads a mod's `.so` inside the Minecraft
process through its preloader, from the `.levipack`. A library inside another app cannot hook the
game. So the AAR is the app-side companion: a settings API with the same field names, limits and
clamping as the native mod (cross-checked against the real C++ loader), and optionally the pack
itself as an asset so an app can hand it to the launcher.

```kotlin
// Kotlin
val modDir = File(launcherModsDir, FreecamPackage.MOD_ID)      // the folder LeviLauncher installed "Freecam" into
val store = FreecamConfigStore(modDir)

val current = store.load()                                      // defaults if missing or corrupt
store.save(current.copy(showNameTags = false, moveSpeed = 25.0)) // clamped to 0.5–50, throws IOException on failure

if (FreecamPackage.isLevipackBundled(context)) {
    FreecamPackage.exportLevipack(context, File(context.getExternalFilesDir(null), "freecam.levipack"))
}
```

```java
// Java
FreecamConfigStore store = new FreecamConfigStore(modDir);
FreecamSettings s = store.load();
store.save(new FreecamSettings(false, 25.0));
```

Notes:

* The AAR does not guess where the launcher keeps its mods; pass the mod's directory in.
* `save()` writes atomically and keeps fields it does not know (for example options added by a
  newer version of the mod). `load()` never throws because of file content. The native mod itself
  rewrites `config.json` at start-up and drops fields it does not know.
* Used as a plain `.aar` file (not from a Maven repository) you must add Gson yourself:
  `implementation("com.google.code.gson:gson:2.11.0")`.
* `freecam.levipack` is only inside the AAR when it was built after the pack existed (the CI
  does that; locally copy it to `aar/freecam-library/src/main/assets/` first).

---

## Layout

```
.github/workflows/build.yml      CI
mod/
  manifest.json                  preload-native, targets 1.26.52 and 1.26.52.*
  CMakeLists.txt
  assets/icon.svg, icon.png      Freecam's icon (MIT notice in ICON-LICENSE.txt)
  data/signatures.json           per-build hook signatures (empty!)
  src/
    FreecamMod.cpp               lifecycle, Mod Menu module + floating button, config persistence
    FreecamCore.*                camera maths (no dependencies)
    Controller.*                 session state shared by menu thread and game hooks
    Settings.hpp, ConfigSchema.hpp
    SignatureRules.*             signatures.json parser
    GameBackend.*                attaches to the game; hook detours go here
    IconData.hpp                 icon.png as bytes (tools/embed_icon.py)
    GenerateConfig.cpp           writes config/config.json + config.schema.json for the pack
  tests/                         host tests, incl. a fake preloader to drive the real lifecycle code
aar/                             Gradle project: freecam-library
tools/                           package_levipack.py, embed_icon.py, test_package_levipack.py
```

## What has and has not been verified

Checked on a Linux host while writing this:

* Camera maths, controller and signature parser (unit tests with mutation checks).
* The real `FreecamMod` lifecycle driven against a fake preloader: registration contents, config
  persistence, callbacks, enable/disable/unload, corrupt or out-of-range config files.
* The generated `config.json` / schema, read by both the C++ `ConfigFile` and the Kotlin store.
* Packaging: deterministic zip, a port of LeviLauncher's import rules, Java `ZipInputStream`
  compatibility, negative cases.
* The Kotlin library: compiled with Kotlin 2.0.21 and its JUnit tests run on the JVM.

**Not verified** (no Android NDK, no Android Gradle Plugin, no device in that environment):

* The arm64 compile/link of `libfreecam.so` against the preloader (the first CI run is the real test).
* The Gradle/AGP build of the AAR.
* Loading on a real device with LeviLauncher and the behaviour of the Mod Menu entries there.
* Everything that depends on the game binary (see above).

## Troubleshooting

* **Launcher refuses the pack**: run `python3 tools/package_levipack.py --verify <file>`; it prints
  which import rule fails.
* **Pack does not appear for your game version**: `minecraft_versions` in `mod/manifest.json`
  (`1.26.52`, `1.26.52.*`). Use `*` or an empty list to allow every version.
* **Menu says "Note: No game signatures …"**: expected until the hooks are added.
* **"Failed to load config" in logcat**: the mod's `config/` folder is not writable. A corrupt or
  out-of-range `config.json` is repaired automatically, deleting it is never needed.
* **Crash right after loading**: build against the same preloader revision as your launcher
  (see *About the preloader SDK version*).

## Credits

* Icon: [Freecam](https://github.com/MinecraftFreecam/Freecam) by its contributors, MIT.
* Mod SDK and launcher: [LiteLDev](https://github.com/LiteLDev) (`preloader-android`, `LeviLaunchroid`).
