# Bundled HRTF data

Advanced spatial mode (`BinauralStage`) can render through any of several
HRTF sources, selectable live from the GUI's HRTF dropdown (or the
`SetHrtfFile` control opcode) without restarting the daemon. Every entry
listed here is either public domain or permissively (Apache-2.0)
licensed, safe to redistribute including in commercial builds — no
"free for research only" datasets are bundled.

## `default.sofa` — MIT KEMAR "normal pinna" (catalog index 0, default)

Fetched from the SOFA conventions database:
<https://sofacoustics.org/data/database/mit/mit_kemar_normal_pinna.sofa>

Original measurements: MIT Media Lab, 1994 ("KEMAR HRTF measurements").
The Media Lab placed the raw KEMAR data in the public domain for any use,
commercial or otherwise. This `.sofa` file is a SOFA-format conversion of
that same public-domain dataset, redistributed by the SOFA conventions
project.

This is the default HRTF source when the `AUDIOBAT_HRTF_SOFA` environment
variable isn't set and no other catalog entry has been selected via the
control protocol.

## `sadie/sadie_H*.sofa` — SADIE II database (8 subjects)

Eight individual human subjects (H3, H5, H8, H10, H13, H15, H18, H20)
from the SADIE II HRTF database, University of York:
<https://www.york.ac.uk/sadie-project/database.html>

Source: Zenodo record <https://zenodo.org/records/12092466>
(`<ID>_HRIR_SOFA.zip`, `<ID>_48K_24bit_256tap_FIR_SOFA.sofa` member
extracted — matches AudioBat's fixed 48kHz pipeline rate).

**License: Apache License, Version 2.0** (full text bundled at
`sadie/LICENSE.txt`, fetched from the same Zenodo record). Redistribution
requires preserving copyright/license notices, which `LICENSE.txt`
satisfies; if you redistribute AudioBat itself, keep it alongside these
files.

**Citation** (required whenever this data is used, per the database's own
terms): C. Armstrong, L. Thresh, D. Murphy, and G. Kearney, "A Perceptual
Evaluation of Individual and Non-Individual HRTFs: A Case Study of the
SADIE II Database," *Applied Sciences*, 8(11), 2029, 2018.
DOI: 10.3390/app8112029.

Human HRTF perception is highly ear-shape-specific — a dataset that
externalizes well for one listener may sound like plain stereo panning
for another. These 8 subjects exist so a user can try several and keep
whichever their own ears respond to, rather than being stuck with one
generic dummy-head measurement.

## Synthetic spherical-head model (catalog index, no file)

A procedurally-computed fallback (`daemon/src/dsp/synthetic_hrtf.cpp`):
Woodworth-Schlosberg interaural time difference plus a simple single-pole
contralateral-ear shadow filter, generated at runtime from head geometry
constants. No measured data, so no data license at all — purely math.
Coarser than any measured dataset above (no pinna spectral detail, so
weaker front/back cues), but always available regardless of what's on
disk, and a genuinely license-free option for anyone redistributing
AudioBat who wants to avoid bundling third-party data entirely.

## Overriding at runtime

`AUDIOBAT_HRTF_SOFA` (environment variable) still works as a lower-level
override, pointing `BinauralStage` at any arbitrary SOFA file path outside
this catalog — useful for trying a dataset that isn't bundled without
rebuilding. The GUI catalog dropdown is the normal way to switch between
the datasets described above.
