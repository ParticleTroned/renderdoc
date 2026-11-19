<p align="center"><img src="https://user-images.githubusercontent.com/661798/36482670-f81601c0-170b-11e8-8adb-2365b346ac27.png" /></p>

> **This is an unofficial community fork of [RenderDoc](https://github.com/baldurk/renderdoc), based on [alandtse/renderdoc](https://github.com/alandtse/renderdoc) and maintained at [ParticleTroned/renderdoc](https://github.com/ParticleTroned/renderdoc).**
>
> This fork has two purposes. First, it permits the **appropriate** use of LLMs and AI-assisted tooling in development — the one policy where it departs from upstream. AI-generated code submitted without review or understanding is not acceptable, and contributors must be able to explain their changes. All other upstream contribution guidelines are followed as closely as possible.
>
> Second, it is structured to protect the community's contributions. Fork-specific code is GPL-3.0-or-later, ensuring it remains open source regardless of any future change in upstream's direction. Reintegration with upstream is the goal if upstream policy allows, but if upstream ever moves away from an open-source license, no further contributions from this fork will be eligible for integration. See [COPYING](COPYING) and the [Contributor License Agreement](CONTRIBUTOR_LICENSE_AGREEMENT.md) for details.
>
> This fork tracks upstream `v1.x` closely. Issues arising from fork-specific changes must be reported here and **not** to the upstream project — do not file issues or pull requests related to this fork's changes against the upstream repository.

[![Upstream license: MIT](https://img.shields.io/badge/upstream%20license-MIT-green.svg)](LICENSE.md)
[![Fork additions: GPL v3](https://img.shields.io/badge/fork%20additions-GPL--3.0--or--later-blue.svg)](COPYING)
[![CI](https://github.com/ParticleTroned/renderdoc/actions/workflows/ci.yml/badge.svg?branch=dev&event=push)](https://github.com/ParticleTroned/renderdoc/actions)
[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-v2.0%20adopted-ff69b4.svg)](docs/CODE_OF_CONDUCT.md) 

RenderDoc is a frame-capture based graphics debugger, currently available for Vulkan, D3D11, D3D12, OpenGL, and OpenGL ES development on Windows, Linux, Android, and Nintendo Switch&trade;. It is completely open-source.

RenderDoc is intended for debugging your own programs only. Any discussion of capturing programs that you did not create will not be allowed in any official public RenderDoc setting, including the issue tracker, discord, or via email. For example this includes capturing commercial games that you did not create, or capturing Google Maps or Google Earth. Note: Capturing projects you created that use a third party engine like Unreal or Unity, or open source and free projects is completely fine and supported.

If you have any questions, suggestions or problems with this fork you can [create an issue](https://github.com/ParticleTroned/renderdoc/issues/new/choose) here on github. For questions about upstream RenderDoc, see the [upstream repository](https://github.com/baldurk/renderdoc).

To install on windows run the appropriate installer for your OS ([64-bit](https://renderdoc.org/stable/latest/RenderDoc_latest_64.msi) | [32-bit](https://renderdoc.org/stable/latest/RenderDoc_latest_32.msi)) or download the portable zip from the [builds page](https://renderdoc.org/builds). The 64-bit windows build fully supports capturing from 32-bit programs. On linux only 64-bit x86 is supported - there is a precompiled [binary tarball](https://renderdoc.org/stable/latest/renderdoc_latest.tar.gz) available, or your distribution may package it. If not you can [build from source](docs/CONTRIBUTING/Compiling.md).

* **Downloads**: Stable and nightly builds: https://renderdoc.org/builds ( [Symbol server](https://renderdoc.org/symbols) )
* **Documentation**: [HTML online](https://renderdoc.org/docs), [CHM in builds](https://renderdoc.org/docs/renderdoc.chm), [Videos](https://www.youtube.com/user/baldurkarlsson)
* **Fork issues**: [ParticleTroned/renderdoc issue tracker](https://github.com/ParticleTroned/renderdoc/issues)
* **Fork downloads**: [Versioned fork releases](https://github.com/ParticleTroned/renderdoc/releases)
* **Upstream contact**: [baldurk@baldurk.org](mailto:baldurk@baldurk.org), [#renderdoc on OFTC IRC](https://webchat.oftc.net/?channels=renderdoc), [Discord server](https://discord.gg/ahq6yRB)
* **Code of Conduct**: [Contributor Covenant](docs/CODE_OF_CONDUCT.md)
* **Information for contributors**: [All contribution information](docs/CONTRIBUTING.md), [Compilation instructions](docs/CONTRIBUTING/Compiling.md)
* **Community extensions**: [Extensions repository](https://github.com/baldurk/renderdoc-contrib)

Application-side D3D11 compatibility
------------------------------------

This fork exports `RENDERDOC_GetD3D11StateCompatibilityVersion` with C linkage
and the `RENDERDOC_CC` calling convention. Applications can resolve it from
the loaded library as a function returning `uint32_t`. An absent export or
zero means that the contract is unavailable; Windows builds return 2.

The loaded runtime DLL provides this capability. Applications must deploy a
compatible build of this fork and retain its matching replay tools. Updating
`renderdoc_app.h` or merging changes in this repository does not update a DLL
already bundled with an application.

Version 1 guarantees that `GetPredication` returns an owned wrapper reference,
null `SwapDeviceContextState` input is a no-op, and previous-state references
are balanced whether returned, reused, or discarded. Later versions preserve
these guarantees. The export does not change the upstream application API
table or identify a RenderDoc release.

Version 2 also preserves dynamic class-instance bindings in all six shader
getters, respects the caller's class-array capacity, and supports count-only
queries. Applications restoring complete pipeline bindings must require at
least version 2, since version 1 does not guarantee these shader getters.

This contract permits applications to gate manual D3D11 binding restoration
on the required getter fixes. It does not promise vendor-extension replay:
applications must negotiate the capture API separately and validate their
vendor work and frame boundaries. NvAPI passthrough must be configured before
initializing vendor SDKs, and application-owned VR captures must encompass
both eye submissions.

Opaque vendor output capture
----------------------------

Windows builds independently export `RENDERDOC_GetD3D11ExternalWriteVersion`
(version 1), `RENDERDOC_CaptureD3D11ExternalWrite`, and
`RENDERDOC_FailD3D11ExternalWrite`. See
[`renderdoc_d3d11.h`](renderdoc/api/app/renderdoc_d3d11.h) for the C ABI.
The state-compatibility export does not imply this capability.

Applications call the extension after ordering an opaque evaluation's
completion on their immediate D3D11 context and before consuming its output.
During capture, the extension reads the actual output subresource and records
its application-declared input dependencies and evaluation metadata. Outside
capture, it only invalidates cached initial contents. No output pixels or
pipeline bindings are changed. A missing readback or invalid arguments on a
valid immediate context invalidate the capture instead of saving incomplete
vendor work. Applications can explicitly reject incomplete metadata too.

Replay restores those bytes in a dedicated `External::CapturedVendorOutput`
event, labelled **opaque vendor boundary**. It is a restoration copy, not a
recorded inference dispatch. NVIDIA's inference kernels are not re-executed;
input usages are declared dependencies, not a hidden access trace. Pixel
history can inspect values before/after the boundary but cannot inspect its
internal computation. Upstream shader/resource edits do not propagate through
it. GPU counters time restoration, not inference; captures are not evidence of
vendor performance or the absence of synchronization races. Each boundary
emits a high-severity replay diagnostic explaining these limitations.

Captures use fork D3D11 section version `0x50540001`. Unmodified upstream
rejects this version; the fork continues reading upstream versions 0x0f
through 0x14. Keep the matching fork tools with the capture.

Fork versioning
---------------

This fork retains upstream's numeric RenderDoc version and reports its own
release separately, for example `1.47+pt.1.0.0`. The fork component comes from
the nearest reachable `pt-vX.Y.Z` tag on the first-parent history. Upstream
and alandtse release tags do not participate. Updating upstream does not
reset the fork version or modify upstream's version macros.

Untagged builds append `.dev.<distance>.<commit>`; tracked local changes add
`.dirty`. Clone with tags and full history. Missing release tags fail by
default. Before the first fork release, the explicit `--allow-unreleased`
option produces `upstream+pt.0.0.0.dev.<commit>` rather than claiming a release.

Prepare Windows build metadata with Python 3.9 or later, then build the
solution normally:

```powershell
python util/fork_version.py metadata --msbuild-props Directory.Build.props
```

The generated props file is ignored by Git and preserves existing compiler
definitions. An unmanaged props file is never overwritten. Regenerate the
metadata after changing commits, tags, or tracked files. The combined
version appears in the startup distribution log, UI, and Windows product
version string. Numeric API and Windows file versions remain upstream's.
Linux builds pass the helper's version, distribution, and contact outputs
to CMake's `BUILD_VERSION_DIST_VER`, `BUILD_VERSION_DIST_NAME`, and
`BUILD_VERSION_DIST_CONTACT` options.

On `ParticleTroned/renderdoc`, merges into `dev` use an independent `pt-v`
release sequence without a version-bump commit. The first release is
`pt-v1.0.0`; subsequent `fix`, `perf`, and `revert` changes bump the patch,
`feat` bumps the minor, and explicit breaking changes bump the major.
After the initial release, build, CI, documentation, and test changes do
not release automatically.
Use the Release workflow's manual `patch`, `minor`, or `major` input when
those changes need a new distribution, such as a compatibility export.
An explicit input cannot lower a required semantic bump.

Release builds check out the exact tag and embed its combined identity.
Windows and Linux assets are attached to a draft release, which becomes
public after both builds succeed. Retry a failed artifact build with the
Build workflow and the existing `pt-vX.Y.Z` tag. Release notes include the
upstream base and fork version. Synchronize upstream by merge and preserve
the fork's tags; do not edit upstream's numeric version for fork changes.

Screenshots
--------------

| [ ![Texture view](https://renderdoc.org/fp/ts_screen1.jpg?2) ](https://renderdoc.org/fp/screen1.jpg) | [ ![Pixel history & shader debug](https://renderdoc.org/fp/ts_screen2.jpg?2) ](https://renderdoc.org/fp/screen2.png) |
| --- | --- |
| [ ![Mesh viewer](https://renderdoc.org/fp/ts_screen3.jpg?2) ](https://renderdoc.org/fp/screen3.png) | [ ![Pipeline viewer & constants](https://renderdoc.org/fp/ts_screen4.jpg?2) ](https://renderdoc.org/fp/screen4.png) |

API Support
--------------

|                          | Windows                  | Linux                    | Android                   |
| ------------------------ | ------------------------ | ------------------------ | ------------------------  |
| Vulkan                   | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark:        |
| OpenGL ES 2.0 - 3.2      | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark:        |
| OpenGL 3.2 - 4.6 Core    | :heavy_check_mark:       | :heavy_check_mark:       |  N/A                      |
| D3D11 & D3D12            | :heavy_check_mark:       |  N/A                     |  N/A                      |
| OpenGL 1.0 - 2.0 Compat  | :heavy_multiplication_x: | :heavy_multiplication_x: |  N/A                      |
| D3D9 & 10                | :heavy_multiplication_x: |  N/A                     |  N/A                      |
| Metal                    |  N/A                     |  N/A                     |  N/A                      |

* Nintendo Switch&trade; support is distributed separately for authorized developers as part of the NintendoSDK. For more information, consult the Nintendo Developer Portal.

Downloads
--------------

There are [binary releases](https://renderdoc.org/builds) available, built from the release targets. If you just want to use the program and you ended up here, this is what you want :).

It's recommended that if you're new you start with the stable builds. Nightly builds are available every day from the [v1.x branch here](https://renderdoc.org/builds#nightly) if you need it, but correspondingly may be less stable.

Documentation
--------------

The text documentation is available [online for the latest stable version](https://renderdoc.org/docs/), as well as in [renderdoc.chm](https://renderdoc.org/docs/renderdoc.chm) in any build. It's built from [restructured text with sphinx](docs).

As mentioned above there are some [youtube videos](https://www.youtube.com/user/baldurkarlsson) showing the use of some basic features and an introduction/overview.

There is also a great presentation by [@Icetigris](https://twitter.com/Icetigris) which goes into some details of how RenderDoc can be used in real world situations: [slides are up here](https://docs.google.com/presentation/d/1LQUMIld4SGoQVthnhT1scoA3k4Sg0as14G4NeSiSgFU/edit#slide=id.p).

License
--------------

This repository contains code under two licenses:

- **Files from upstream RenderDoc** are licensed under the **MIT License** — see [LICENSE.md](LICENSE.md). This covers all files present in [upstream](https://github.com/baldurk/renderdoc) at the time they were incorporated; their copyright notices must not be modified.
- **Fork-specific contributions** (new files and modifications made in this fork) are licensed under **GPL-3.0-or-later** — see [COPYING](COPYING).

By contributing to this fork you accept the [Contributor License Agreement](CONTRIBUTOR_LICENSE_AGREEMENT.md). For upstream reintegration, the CLA pre-authorizes a **copyright assignment to Baldur Karlsson** — the upstream project requires full copyright assignment, not merely a license. Read the full CLA before contributing.

Compiling
---------

Building RenderDoc is fairly straight forward on most platforms. See [Compiling.md](docs/CONTRIBUTING/Compiling.md) for more details.

Contributing & Development
--------------

I've added some notes on how to contribute, as well as where to get started looking through the code in [Developing-Change.md](docs/CONTRIBUTING/Developing-Change.md). All contribution information is available under [CONTRIBUTING.md](docs/CONTRIBUTING.md).

