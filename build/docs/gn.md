(gn)=

# GN support in the build system

{abbr}`GN (Generated Ninja)` is a third-party build tool used by chromium and
some related projects that are vendored in the Firefox repository. Rather than
requiring `GN` to build or writing our own build definitions for these projects,
we have support in the build system for translating GN configuration
files into moz.build files. In most cases these moz.build files will be like any
others in the tree (except that they shouldn't be modified by hand), however
those updating vendored code or building on platforms not supported by
Mozilla automation may need to re-generate these files. This is a per-project
process, described in dom/media/webrtc/third_party_build/gn-configs/README.md for
webrtc. As of writing, it is very specific to webrtc, and likely doesn't work as-is
for other projects.
