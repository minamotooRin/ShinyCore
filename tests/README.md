# Tests

`test_*.cpp` cover native components; Python files cover bindings, games, tools and platform behavior. Native screenshot scripts run only when explicitly invoked with a graphical executable. Test paths stay flat because CMake, documentation and project-local fixtures invoke them directly.

Use the smallest relevant CTest group or script after a change. Headless checks do not prove rendered pixels or physical device behavior.
