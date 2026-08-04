// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "hrtf_filter.hpp"

namespace audiobat
{

// Near-field proximity correction: as a source approaches the head, head
// diffraction boosts interaural level difference (ILD) beyond what a
// far-field-measured HRTF captures (every bundled HRTF - SADIE II, MIT
// KEMAR, and the synthetic far-field model - is a far-field, effectively
// infinite-distance measurement/model). This is what actually produces
// "it's right next to my ear," not just "it got louder" (that broadband
// loudness falloff is handled separately, upstream, applied to every
// mode - see AudioEngine).
//
// Modeled as a low-frequency-concentrated shelf per ear, on the same
// rigid-sphere head (rigid_sphere_math.hpp) the synthetic HRTF uses:
// physically, near-field proximity is strongest at low frequency (this is
// the same "proximity effect" familiar from close-mic'd microphones) and
// negligible above roughly where the head's circumference stops being
// small relative to the wavelength. The near ear's shelf gain is derived
// from the classical DC (incompressible-flow) limit for a monopole near a
// rigid sphere on-axis, G(rho) = rho/(rho-1) for rho = distance/head
// radius > 1 (this specific limit is independently derivable from
// potential-flow theory around a sphere, not just recalled from a
// secondary source); the far ear gets a deliberately weaker version of
// the same boost so an ILD gap opens up as distance shrinks, rather than
// attempting to reproduce Duda & Martin's full angle-dependent closed
// form from memory. Both this and the exact DC-limit choice are called
// out explicitly since they're an engineering approximation of the real
// physics, not a verbatim reproduction of a published filter - see
// docs/near-field-distance-plan.md for the reasoning and the verification
// this was checked against.
//
// Both ears' gain is normalized to exactly 1.0 (no correction at all) at
// ReferenceDistanceMeters (1m, matching the loudness-falloff reference
// used elsewhere and roughly where HRTF measurements are typically made),
// so this only pushes away from what the far-field HRTF already captures
// as distance moves away from that reference, in either direction.

// Raw per-ear near-field shelf for one source's direction and distance,
// not yet combined with any far-field HRTF - exposed separately so it can
// be inspected/verified in isolation (see docs/near-field-distance-plan.md's
// Verification section). AzimuthDegrees: AudioBat convention (0 = front,
// positive = left). DistanceMeters: expected pre-clamped to
// [MinSpeakerDistanceMeters, MaxSpeakerDistanceMeters] (protocol.hpp) by
// the caller; this function clamps defensively too.
HrtfFilter ComputeNearFieldCorrection(float AzimuthDegrees, float DistanceMeters, float SampleRate);

// Cascades ComputeNearFieldCorrection's per-ear shelf onto an existing
// far-field HRTF filter (measured or synthetic) via convolution, for the
// same direction/distance. FarFieldFilter's DelayLeft/RightSamples pass
// through unchanged - this correction is magnitude-only (a shelf, not a
// delay), so it doesn't shift arrival time.
HrtfFilter ApplyNearFieldCorrection(const HrtfFilter& FarFieldFilter, float AzimuthDegrees,
                                     float DistanceMeters, float SampleRate);

} // namespace audiobat
