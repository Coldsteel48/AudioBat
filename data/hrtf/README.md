# Bundled default HRTF data

`default.sofa` is the MIT KEMAR "normal pinna" HRTF dataset, in SOFA
format, fetched from the SOFA conventions database:
<https://sofacoustics.org/data/database/mit/mit_kemar_normal_pinna.sofa>

Original measurements: MIT Media Lab, 1994 ("KEMAR HRTF measurements").
The Media Lab placed the raw KEMAR data in the public domain for any use,
commercial or otherwise. This `.sofa` file is a SOFA-format conversion of
that same public-domain dataset, redistributed by the SOFA conventions
project.

AudioBat's `BinauralStage` (Advanced spatial mode) uses this file as its
default HRTF source when the `AUDIOBAT_HRTF_SOFA` environment variable
isn't set. Point that variable at any other SOFA file to use a different
HRTF dataset instead.
