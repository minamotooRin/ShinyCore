# Horizontal UI scroll check

`kind="scroll", axis="horizontal"` lays out its visible children along X and
uses their actual widths to clamp the offset. Focus reveal, `UI.scroll_to`, wheel,
track paging and thumb dragging use that same offset; vertical remains the default.
`UI.inspect` reports `scroll_axis` alongside the offset and maximum.

On Windows with the full build, the focused `InputTests` horizontal case and three
existing vertical scroll cases passed. All six affected local projects passed
`--check-all`; SDK tooling tests passed separately.
The hidden, muted native capture at
`build/ui-scroll-final-reviewed/ui-scroll.png` was viewed: cards 05–08, focused
card 08, horizontal thumb, labels and clipping are visible without overlap.
This evidence does not cover physical mouse-wheel or controller hardware.
