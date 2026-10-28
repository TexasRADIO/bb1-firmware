/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SAMPLE_GAIN_H
#define BB_SAMPLE_GAIN_H
/* Default sampled-kit gain (SENDS K4 = 100%). Calibrated 27 Oct (integration step 3, #34): at the
 * former 0.25 the sampled kits played 12-15 dB below the native 808 (342-section host sweep, RMS);
 * at 1.0 they match it within 0-2.6 dB, with 0 limited frames and 0 invalid inputs in every case
 * (sampled alone, sampled + 808, two sampled layers; Dillo and both effects playing). Native 808
 * calibration is separate and unchanged. Values saved in user scenes are kept as saved. */
#define BB_SAMPLE_DEFAULT_GAIN 1.0f
#endif
