#pragma once

// Tube-style saturation: pre-emphasis, asymmetric soft clipper (adds even
// harmonics like a triode), de-emphasis and level compensation. Runs at 2x
// oversampling inside the processor.

#include <cmath>

namespace txiki
{

class TubeDrive
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double twoPi = 2.0 * 3.141592653589793;
        emphCoeff = (float) (1.0 - std::exp (-twoPi * 700.0 / sr));
        toneCoeff = (float) (1.0 - std::exp (-twoPi * 9000.0 / sr));
        dcCoeff = (float) (1.0 - std::exp (-twoPi * 12.0 / sr));
        reset();
    }

    void reset() { emphLp = deemphLp = toneLp = dcState = 0.0f; }

    // amount 0..1
    void setAmount (float amount)
    {
        amt = amount;
        drive = std::pow (10.0f, (amount * 30.0f) / 20.0f); // up to +30 dB into the tube
        const float probe = 0.25f;
        const float sat = shaper (drive * probe) - shaper (0.0f);
        makeup = sat > 1.0e-6f ? probe / sat : 1.0f;
        makeup = std::pow (makeup, 0.85f); // leave a little loudness increase, as analog would
    }

    float process (float x)
    {
        if (amt <= 0.0f)
            return x;

        // Pre-emphasis: push highs a bit into the clipper (bite), low end cleaner.
        emphLp += emphCoeff * (x - emphLp);
        const float emphasised = x + 0.5f * amt * (x - emphLp);

        float y = (shaper (drive * emphasised) - shaper (0.0f)) * makeup;

        // De-emphasis and output tone rolloff that grows with drive.
        deemphLp += emphCoeff * (y - deemphLp);
        y = y - (0.5f * amt / (1.0f + 0.5f * amt)) * (y - deemphLp);
        toneLp += toneCoeff * (y - toneLp);
        y = y + amt * 0.6f * (toneLp - y);

        // Remove DC produced by the asymmetry.
        dcState += dcCoeff * (y - dcState);
        y -= dcState;

        // Crossfade from clean at very small amounts to avoid a step at 0.
        const float blend = std::fmin (1.0f, amt * 20.0f);
        return x + blend * (y - x);
    }

private:
    static float shaper (float v)
    {
        const float bias = 0.18f;
        const float u = v + bias;
        // Positive side compresses softer than negative (grid conduction).
        return u >= 0.0f ? std::tanh (u) : std::tanh (1.35f * u) / 1.35f * 1.15f;
    }

    double sr = 44100.0;
    float amt = 0.0f, drive = 1.0f, makeup = 1.0f;
    float emphCoeff = 0.1f, toneCoeff = 0.5f, dcCoeff = 0.001f;
    float emphLp = 0.0f, deemphLp = 0.0f, toneLp = 0.0f, dcState = 0.0f;
};

} // namespace txiki
