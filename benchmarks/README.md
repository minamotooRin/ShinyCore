# Fixed performance workloads

The combined barrage workload is executable. Rigid bodies, UI, streaming and advanced
lighting acceptance workloads are still missing; this directory is not a full pass report.

```powershell
python tools/benchmark.py build/full/shiny.exe --output build/barrage-smoke --smoke
python tools/benchmark.py build/full/shiny.exe --output build/barrage-acceptance
```

The output directory must be new. The full command opens three native windows
in sequence and occupies the desktop for about eleven minutes; arrange an idle period
with the computer user before starting it. Closing a window stops the whole runner
and records an incomplete measurement instead of launching the next window.
The full command runs three independent native
processes, each 12,720 frames: 30 seconds warmup, 180 measured wall seconds, and a
tail for asynchronous GPU results. VSync/host pacing stays enabled for bounded 60 Hz
playback; the profiler excludes normal frame pacing from CPU/GPU processing time.
Output uses the desktop in borderless mode. **Set the monitor to 1920×1080 and verify
the captured image size, discrete GPU renderer, Release build, i7-8700 and 16 GiB RAM.**
The script does not change OS display settings or claim hardware/VRAM qualification.

Reports enforce CPU/GPU p99 ≤16.67 ms, peak process memory ≤512 MiB, at least 20,000
projectiles, 2,000 entities and 20,000 particles in every measured frame, and ≥1,000
actual hits per wall second. Insufficient duration, missing GPU samples and low load
fail the report. A short diagnostic pass is never a complete performance acceptance.

The scene uses 2,000 moving bodyless enemy targets, one shared flow field, genuine
nonpiercing circle sweeps and consumption of batched hit results into per-enemy damage.
Targets follow four rotating goals and reset their formation every 480 ticks to make
the workload loopable. They remain active, move and receive hits throughout; rigid-body
work belongs in the separate physics workload. Bullets move right, remain within the
viewport until expiry, and are replenished toward 24,000. Particle curves and velocities
advance natively and replenish toward 21,000. Surplus covers deaths before a profiled
frame is sampled. Counts may never fall below the specified minima after startup.

All workloads and reporting are offline. Python is a development dependency only.
