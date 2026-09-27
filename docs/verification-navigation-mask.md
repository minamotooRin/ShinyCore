# Navigation mask verification

`sc.navigation.mask(radius?)` returns the current local grid's origin, cell size,
dimensions and passability rows. The native implementation uses the same clearance
function as A* and shared flow fields. A Tiled collision edit changes a new mask
snapshot; an invalid edit leaves the old passability intact. The call is read-only
and only allocates its bounded result when explicitly requested.

Verified on Windows with the full and lightweight builds, the focused navigation
contract, and the Tiled navigation-edit integration case. A Clang ASan/UBSan build
and the same navigation contract passed. The generated `--api` reference and 18
project-local annotations agree with both builds; affected manifested projects pin
SDK dev.75. The unchanged snapshot sample remains on its pinned dev.1 SDK.

This exposes accurate local data for a future offline connectivity summary. It does
not yet provide routing across unloaded sparse chunks or satisfy the full engine
acceptance criteria. There is no visual behavior change in this patch.
