# Bundled HRTF data

Advanced spatial mode (`BinauralStage`) can render through any of several
HRTF sources, selectable live from the GUI's HRTF dropdown (or the
`SetHrtfFile` control opcode) without restarting the daemon. Every entry
listed here is either public domain or permissively (Apache-2.0, CC BY
4.0, or MIT) licensed, safe to redistribute including in commercial
builds — no "free for research only" datasets are bundled. (Databases
considered and rejected for that reason, beyond the classics like CIPIC/
ARI/Listen/RIEC: the RWTH Aachen HRTF database, which is CC BY-NC-SA 4.0
and explicitly prohibits commercial exploitation.)

## `default.sofa` — MIT KEMAR "normal pinna" (catalog index 0, default)

Fetched from the SOFA conventions database:
<https://sofacoustics.org/data/database/mit/mit_kemar_normal_pinna.sofa>

Original measurements: MIT Media Lab, 1994 ("KEMAR HRTF measurements").
The Media Lab placed the raw KEMAR data in the public domain for any use,
commercial or otherwise. This `.sofa` file is a SOFA-format conversion of
that same public-domain dataset, redistributed by the SOFA conventions
project.

This is the default HRTF source when the `RAMKOLFX_HRTF_SOFA` environment
variable isn't set and no other catalog entry has been selected via the
control protocol.

## `sadie/sadie_*.sofa` — SADIE II database (full set: 18 human subjects + 2 dummy heads)

All 20 entries from the SADIE II HRTF database, University of York:
<https://www.york.ac.uk/sadie-project/database.html> — human subjects H3
through H20, plus dummy-head rigs D1 (KEMAR) and D2 (B&K Type 4128C).

Source: Zenodo record <https://zenodo.org/records/12092466>
(`<ID>_HRIR_SOFA.zip`, `<ID>_48K_24bit_256tap_FIR_SOFA.sofa` member
extracted — matches RamkolFX's fixed 48kHz pipeline rate).

**License: Apache License, Version 2.0** (full text bundled at
`sadie/LICENSE.txt`, fetched from the same Zenodo record). Redistribution
requires preserving copyright/license notices, which `LICENSE.txt`
satisfies; if you redistribute RamkolFX itself, keep it alongside these
files.

**Citation** (required whenever this data is used, per the database's own
terms): C. Armstrong, L. Thresh, D. Murphy, and G. Kearney, "A Perceptual
Evaluation of Individual and Non-Individual HRTFs: A Case Study of the
SADIE II Database," *Applied Sciences*, 8(11), 2029, 2018.
DOI: 10.3390/app8112029.

Human HRTF perception is highly ear-shape-specific — a dataset that
externalizes well for one listener may sound like plain stereo panning
for another. These 18 human subjects exist so a user can try several and
keep whichever their own ears respond to, rather than being stuck with
one generic dummy-head measurement. D1/D2 are included alongside them as
two more (non-individual) dummy-head options, in the same spirit as the
MIT KEMAR default.

## `hutubs/hutubs_pp*.sofa` — HUTUBS database (5 subjects)

Five individual human subjects (pp5, pp30, pp50, pp70, pp90) from the
HUTUBS HRTF database, Technical University of Berlin:
<https://depositonce.tu-berlin.de/items/dc2a3076-a291-417e-97f0-7697e332c960>
(DOI: 10.14279/depositonce-8487)

Source: the `HRIRs.zip` bitstream on that record, `pp<N>_HRIRs_measured.sofa`
member extracted per subject (the acoustically-measured HRIRs, not the
numerically-simulated ones also present in that archive). The database
has 96 subject slots in total; pp1/pp96 are repeated measurements of the
FABIAN head-and-torso simulator and pp22/pp88 are a repeated human
subject, so the five picked here are five distinct people.

**License: CC BY 4.0** (full text bundled at `hutubs/LICENSE.txt`, fetched
from creativecommons.org). Confirmed directly from the database's own
documentation PDF and DSpace `dc.rights.uri` metadata: "The data is
provided under the free culture CC BY license that grants unlimited
access for everyone." Redistribution requires attribution, which
`LICENSE.txt` plus this citation satisfies.

**Citation** (required whenever this data is used): F. Brinkmann, M.
Dinakaran, R. Pelzer, P. Grosche, D. Voss, and S. Weinzierl, "A
Cross-Evaluated Database of Measured and Simulated HRTFs Including 3D
Head Meshes, Anthropometric Features, and Headphone Impulse Responses,"
J. Audio Eng. Soc. DOI: 10.14279/depositonce-8487.

## `sonicom/sonicom_P*.sofa` — SONICOM database (5 subjects)

Five individual human subjects (P0002, P0057, P0102, P0201, P0301) from
the SONICOM HRTF Dataset, Audio Experience Design, Imperial College
London: <https://www.axdesign.co.uk/tools-and-devices/sonicom-hrtf-dataset>

Source: `<ID>/HRTF/HRTF/48kHz/<ID>_FreeFieldComp_48kHz.sofa` from the
dataset's own file server at
<https://transfer.ic.ac.uk:9090/#/2022_SONICOM-HRTF-DATASET/> — the
free-field-compensated, windowed, 48kHz variant with ITD intact (matches
RamkolFX's fixed 48kHz pipeline rate and is the directly-comparable
counterpart to the SADIE II files above). This is currently the largest
publicly available HRTF dataset (300+ subjects and growing).

**License: MIT** (bundled at `sonicom/LICENSE.txt`). Confirmed from the
dataset authors' own published paper: "The database is publicly
available under the MIT license at:
https://transfer.ic.ac.uk:9090/#/2022_SONICOM-HRTF-DATASET/" — no
separate LICENSE file is published alongside the raw data itself, so
`LICENSE.txt` renders the standard MIT text with that statement quoted
as its source.

**Citation** (required whenever this data is used): I. Engel, R.
Daugintis, T. Vicente, A. O. T. Hogg, J. Pauwels, A. J. Tournier, and L.
Picinali, "The SONICOM HRTF Dataset," J. Audio Eng. Soc., 2023, and (for
the extended dataset used here) the Forum Acusticum 2025 companion
paper, "The Extended SONICOM HRTF Dataset and Spatial Audio Metrics
Toolbox," arXiv:2507.05053.

## Synthetic spherical-head model (catalog index, no file)

A procedurally-computed fallback (`daemon/src/dsp/synthetic_hrtf.cpp`):
Woodworth-Schlosberg interaural time difference plus a simple single-pole
contralateral-ear shadow filter, generated at runtime from head geometry
constants. No measured data, so no data license at all — purely math.
Coarser than any measured dataset above (no pinna spectral detail, so
weaker front/back cues), but always available regardless of what's on
disk, and a genuinely license-free option for anyone redistributing
RamkolFX who wants to avoid bundling third-party data entirely.

## Bringing your own SOFA files

Beyond the vetted, redistributable datasets above, the daemon also watches
a user-writable directory and lists whatever `.sofa` files it finds there
in the GUI's HRTF dropdown, prefixed `(user) ` — no rebuild or restart
needed. **RamkolFX doesn't bundle, vet, or check the license of anything
placed here — that's entirely on you.** This is the place to put a SOFA
dataset with terms that don't allow redistribution (e.g. CIPIC, ARI,
Listen, or any "research only" dataset), or one of your own individual
HRIR measurements.

By default this is `$XDG_CONFIG_HOME/ramkolfx/hrtf`, falling back to
`~/.config/ramkolfx/hrtf` (created automatically on first run). Set
`RAMKOLFX_HRTF_DIR` to point the daemon at a different directory instead.
Files are matched purely by `.sofa` extension (case-insensitive) and
aren't parsed until selected — a corrupt or non-SOFA file will show up in
the list but fail gracefully (falling back to silence for that source)
rather than crashing the daemon when selected.

## Overriding at runtime

`RAMKOLFX_HRTF_SOFA` (environment variable) still works as a lower-level
override, pointing `BinauralStage` at any arbitrary SOFA file path outside
the catalog entirely (including outside the user directory above) —
useful for trying a single dataset without adding it to the catalog. The
GUI catalog dropdown is the normal way to switch between the bundled
datasets and anything placed in the user directory.
