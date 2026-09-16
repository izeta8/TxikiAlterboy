#pragma once

// Monophonic voice pitch + formant shifter.
//  - Pitch detection: YIN (de Cheveigné & Kawahara 2002).
//  - Synthesis: pitch-synchronous overlap-add (PSOLA). Each grain is resampled
//    by the formant factor, which moves the spectral envelope independently of
//    the grain spacing (that sets the output pitch).
// JUCE-free so it can be tested offline.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace txiki
{

enum class Mode
{
    Transpose = 0,
    Quantize = 1,
    Robot = 2
};

class YinDetector
{
public:
    void prepare (double sampleRate, double minHz, double maxHz)
    {
        sr = sampleRate;
        maxLag = (int) std::ceil (sr / minHz) + 2;
        minLag = std::max (2, (int) std::floor (sr / maxHz));
        window = maxLag;
        diff.assign ((size_t) maxLag + 1, 0.0f);
    }

    int getRequiredLength() const { return window + maxLag + 1; }

    // x points at getRequiredLength() contiguous samples.
    // Returns period in samples (0 if unvoiced).
    float analyse (const float* x, float& aperiodicity) const
    {
        double energy = 0.0;
        for (int j = 0; j < window; ++j)
            energy += (double) x[j] * x[j];

        aperiodicity = 1.0f;
        if (energy / window < 1.0e-6) // about -60 dBFS RMS
            return 0.0f;

        auto& d = const_cast<std::vector<float>&> (diff);
        d[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const float delta = x[j] - x[j + tau];
                sum += (double) delta * delta;
            }
            running += sum;
            d[(size_t) tau] = running > 0.0 ? (float) (sum * tau / running) : 1.0f;
        }

        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (d[(size_t) tau] < kThreshold)
            {
                while (tau + 1 < maxLag && d[(size_t) tau + 1] < d[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            float minVal = 10.0f;
            for (int tau = minLag; tau < maxLag; ++tau)
                if (d[(size_t) tau] < minVal)
                {
                    minVal = d[(size_t) tau];
                    best = tau;
                }
            if (best < 0 || minVal > kVoicedLimit)
            {
                aperiodicity = minVal;
                return 0.0f;
            }
        }

        aperiodicity = d[(size_t) best];

        // Parabolic interpolation around the dip.
        float period = (float) best;
        if (best > 1 && best < maxLag)
        {
            const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1.0e-9f)
                period += 0.5f * (a - c) / den;
        }
        return period;
    }

private:
    static constexpr float kThreshold = 0.15f;
    static constexpr float kVoicedLimit = 0.3f;

    double sr = 44100.0;
    int minLag = 2, maxLag = 800, window = 800;
    std::vector<float> diff;
};

class VoiceShifter
{
public:
    static constexpr double kMinF0 = 75.0;
    static constexpr double kMaxF0 = 1000.0;

    void prepare (double sampleRate, int channels = 1)
    {
        sr = sampleRate;
        numCh = std::max (1, channels);
        decim = std::max (1, (int) std::floor (sr / 44100.0 + 0.01));
        tMax = (int) std::ceil (sr / kMinF0);
        latency = (int) std::ceil (4.25 * tMax) + 8;
        uvHop = sr * 0.005;
        fadeCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));

        yin.prepare (sr / decim, kMinF0, kMaxF0);
        yinLen = yin.getRequiredLength();
        yinHop = 256;

        const double lpHz = 900.0;
        lpCoeff = (float) (1.0 - std::exp (-2.0 * 3.141592653589793 * lpHz / sr));
        const double aaHz = 0.45 * sr / decim;
        aaCoeff = (float) (1.0 - std::exp (-2.0 * 3.141592653589793 * aaHz / sr));

        int size = 1;
        while (size < 8 * tMax + 4 * yinLen * decim)
            size <<= 1;
        bufSize = size;
        mask = size - 1;
        inBuf.assign ((size_t) numCh, std::vector<float> ((size_t) size, 0.0f));
        lpBuf.assign ((size_t) size, 0.0f);
        outBuf.assign ((size_t) numCh, std::vector<float> ((size_t) size, 0.0f));
        decBuf.assign ((size_t) yinLen * 2, 0.0f);
        yinScratch.assign ((size_t) yinLen, 0.0f);

        reset();
    }

    void reset()
    {
        for (auto& b : inBuf)
            std::fill (b.begin(), b.end(), 0.0f);
        std::fill (lpBuf.begin(), lpBuf.end(), 0.0f);
        for (auto& b : outBuf)
            std::fill (b.begin(), b.end(), 0.0f);
        std::fill (decBuf.begin(), decBuf.end(), 0.0f);
        inPos = 0;
        lp1 = lp2 = aa1 = aa2 = 0.0f;
        decCount = 0;
        decAccum = 0.0f;
        decWrite = 0;
        decFilled = 0;
        hopCount = 0;

        estimates.assign (kEstCap, Estimate {});
        estHead = 0;
        estCount = 0;
        estCursor = 0;
        periodHistory[0] = periodHistory[1] = periodHistory[2] = 0.0f;

        marks.assign (kMarkCap, Mark {});
        markHead = 0;
        markCount = 0;
        pushMark ({ 0, (float) uvHop, false });
        nextMarkPos = uvHop;

        nextSyn = 0.0;
        synCursorAbs = 0;
        haveNote = false;
        currentNote = 0.0f;
        lastRatio = 1.0f;
        neutralFade = 0.0f;
        smoothInit = false;
    }

    int getLatencySamples() const { return latency; }

    void setParameters (Mode newMode, float pitchSemis, float formantSemis, bool linkOn)
    {
        mode = newMode;
        pitch = pitchSemis;
        formant = formantSemis;
        link = linkOn;
    }

    float getDetectedHz() const { return lastDetectedHz; }

    int getNumChannels() const { return numCh; }

    // Mono convenience wrapper (prepare with 1 channel).
    float processSample (float x)
    {
        float y = 0.0f;
        processFrame (&x, &y);
        return y;
    }

    // One frame of numCh samples. Pitch is analysed on the channel average, and
    // every channel is resynthesised with the same grains, so the stereo image
    // (panning, width, inter-channel timing) is kept intact.
    void processFrame (const float* in, float* out)
    {
        float x = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            x += in[ch];
            inBuf[(size_t) ch][(size_t) (inPos & mask)] = in[ch];
        }
        x /= (float) numCh;

        // Low-passed copy used to align analysis marks to glottal peaks.
        lp1 += lpCoeff * (x - lp1);
        lp2 += lpCoeff * (lp1 - lp2);
        lpBuf[(size_t) (inPos & mask)] = lp2;
        ++inPos;

        feedDetector (x);
        generateMarks();

        const int64_t outPos = inPos - 1 - latency; // output(n) = input(n - latency) when neutral
        while (nextSyn <= (double) (outPos + 2 * tMax))
            placeGrain();

        // Neutral settings: crossfade to the (latency-aligned) input so the
        // plugin is fully transparent, like the hardware-style original.
        const bool neutral = mode == Mode::Transpose && std::abs (pitch) < 0.05f && (link || std::abs (formant) < 0.05f);
        neutralFade += ((neutral ? 1.0f : 0.0f) - neutralFade) * fadeCoeff;

        for (int ch = 0; ch < numCh; ++ch)
        {
            if (outPos < 0)
            {
                out[ch] = 0.0f;
                continue;
            }
            auto& slot = outBuf[(size_t) ch][(size_t) (outPos & mask)];
            float y = slot;
            slot = 0.0f;
            if (neutralFade > 1.0e-4f)
                y += neutralFade * (inBuf[(size_t) ch][(size_t) (outPos & mask)] - y);
            out[ch] = y;
        }
    }

private:
    struct Estimate
    {
        int64_t centre = 0;
        float period = 0.0f;
        bool voiced = false;
    };

    struct Mark
    {
        int64_t pos = 0;
        float period = 0.0f;
        bool voiced = false;
    };

    static constexpr int kEstCap = 256;
    static constexpr int kMarkCap = 1024;

    // ---------------------------------------------------------------- detection
    void feedDetector (float x)
    {
        float v = x;
        if (decim > 1)
        {
            aa1 += aaCoeff * (x - aa1);
            aa2 += aaCoeff * (aa1 - aa2);
            decAccum += aa2;
            if (++decCount < decim)
                return;
            v = decAccum / (float) decim;
            decAccum = 0.0f;
            decCount = 0;
        }

        // Double-length linear buffer so the newest yinLen samples are contiguous.
        decBuf[(size_t) decWrite] = v;
        decBuf[(size_t) (decWrite + yinLen)] = v;
        decWrite = (decWrite + 1) % yinLen;
        if (decFilled < yinLen)
            ++decFilled;

        if (++hopCount < yinHop || decFilled < yinLen)
            return;
        hopCount = 0;

        const float* window = &decBuf[(size_t) decWrite];
        float aperiodicity = 1.0f;
        float period = yin.analyse (window, aperiodicity) * (float) decim;

        // Median of three kills isolated octave jumps.
        periodHistory[0] = periodHistory[1];
        periodHistory[1] = periodHistory[2];
        periodHistory[2] = period;
        if (periodHistory[0] > 0.0f && periodHistory[1] > 0.0f && period > 0.0f)
        {
            float a = periodHistory[0], b = periodHistory[1], c = period;
            period = std::max (std::min (a, b), std::min (std::max (a, b), c));
        }

        Estimate e;
        e.centre = inPos - (int64_t) (yinLen * decim / 2);
        e.voiced = period > 0.0f && period <= (float) tMax;
        e.period = e.voiced ? period : 0.0f;
        lastDetectedHz = e.voiced ? (float) (sr / period) : 0.0f;

        estimates[(size_t) ((estHead + estCount) % kEstCap)] = e;
        if (estCount < kEstCap)
            ++estCount;
        else
        {
            estHead = (estHead + 1) % kEstCap;
            estCursor = std::max (0, estCursor - 1);
        }
    }

    Estimate estimateAt (int64_t t)
    {
        if (estCount == 0)
            return {};
        // Cursor is an offset from estHead; move it forward monotonically.
        if (estCursor >= estCount)
            estCursor = estCount - 1;
        auto get = [this] (int i) -> const Estimate& { return estimates[(size_t) ((estHead + i) % kEstCap)]; };
        while (estCursor + 1 < estCount
               && std::llabs (get (estCursor + 1).centre - t) <= std::llabs (get (estCursor).centre - t))
            ++estCursor;
        return get (estCursor);
    }

    // ------------------------------------------------------------ analysis marks
    void pushMark (const Mark& m)
    {
        if (markCount == kMarkCap)
        {
            markHead = (markHead + 1) % kMarkCap;
            --markCount;
            if (synCursorAbs > 0)
                --synCursorAbs;
        }
        marks[(size_t) ((markHead + markCount) % kMarkCap)] = m;
        ++markCount;
    }

    const Mark& markAt (int i) const { return marks[(size_t) ((markHead + i) % kMarkCap)]; }

    void generateMarks()
    {
        const int64_t limit = inPos - tMax; // estimates are valid up to here
        for (;;)
        {
            const Mark& last = markAt (markCount - 1);
            int64_t candidate = (int64_t) std::llround (nextMarkPos);
            const Estimate est = estimateAt (candidate);

            if (est.voiced)
            {
                const float period = est.period;
                if (!last.voiced)
                    candidate = last.pos + (int64_t) std::llround (std::min ((double) period, uvHop));
                else
                    candidate = last.pos + (int64_t) std::llround (period);

                const int radius = std::max (1, (int) (period * 0.2f));
                if (candidate + radius >= limit)
                    return;

                // Snap to the strongest low-passed peak near the expected epoch.
                float peakAbs = 1.0e-9f;
                for (int64_t n = candidate - radius; n <= candidate + radius; ++n)
                    peakAbs = std::max (peakAbs, std::abs (lpBuf[(size_t) (n & mask)]));
                double bestScore = -1.0e30;
                int64_t bestPos = candidate;
                for (int64_t n = candidate - radius; n <= candidate + radius; ++n)
                {
                    const double dist = (double) (n - candidate) / radius;
                    const double score = lpBuf[(size_t) (n & mask)] - 0.35 * peakAbs * dist * dist;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestPos = n;
                    }
                }
                bestPos = std::max (bestPos, last.pos + (int64_t) std::ceil (period * 0.5f));
                pushMark ({ bestPos, period, true });
                nextMarkPos = (double) bestPos + period;
            }
            else
            {
                if (last.voiced)
                    candidate = last.pos + (int64_t) std::llround (std::min ((double) last.period, uvHop));
                if (candidate >= limit)
                    return;
                pushMark ({ candidate, (float) uvHop, false });
                nextMarkPos = (double) candidate + uvHop;
            }
        }
    }

    // ---------------------------------------------------------------- synthesis
    static float semisToRatio (float s) { return std::pow (2.0f, s / 12.0f); }

    float readInput (const std::vector<float>& buf, double pos) const
    {
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int64_t i = (int64_t) fl;
        const float xm1 = buf[(size_t) ((i - 1) & mask)];
        const float x0 = buf[(size_t) (i & mask)];
        const float x1 = buf[(size_t) ((i + 1) & mask)];
        const float x2 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    void placeGrain()
    {
        const int64_t s = (int64_t) std::floor (nextSyn);

        // Floor analysis mark (<= s).
        if (synCursorAbs >= markCount)
            synCursorAbs = markCount - 1;
        while (synCursorAbs + 1 < markCount && markAt (synCursorAbs + 1).pos <= s)
            ++synCursorAbs;
        Mark a = markAt (synCursorAbs);
        // Prefer the nearest voiced epoch so small corrections stay time-aligned with the dry signal.
        if (a.voiced && synCursorAbs + 1 < markCount)
        {
            const Mark& next = markAt (synCursorAbs + 1);
            if (next.voiced && (double) next.pos - nextSyn < nextSyn - (double) a.pos)
                a = next;
        }

        // Glide knob/automation changes over ~15 ms instead of stepping at grain rate.
        if (!smoothInit)
        {
            pitchSm = pitch;
            formantSm = formant;
            smoothInit = true;
        }
        const float smoothA = 1.0f - std::exp (-(float) std::max (1.0, lastHop) / (0.015f * (float) sr));
        pitchSm += (pitch - pitchSm) * smoothA;
        formantSm += (formant - formantSm) * smoothA;

        const float formantRatio = semisToRatio (formantSm);
        double hop, halfLen, readRate, readCentre, voicedMakeup = 1.0;
        float ratio = 1.0f;

        if (a.voiced)
        {
            const double T = a.period;
            const float midiIn = 69.0f + 12.0f * std::log2 ((float) (sr / T) / 440.0f);
            float target = midiIn + pitchSm;

            if (mode == Mode::Quantize)
            {
                const float wanted = midiIn + pitchSm;
                if (!haveNote || std::abs (wanted - currentNote) > 0.52f)
                    currentNote = std::round (wanted);
                haveNote = true;
                target = currentNote;
            }
            else if (mode == Mode::Robot)
            {
                target = 72.0f + pitchSm; // pitch 0 = one octave above middle C
            }

            ratio = std::clamp (semisToRatio (target - midiIn), 0.25f, 4.0f);
            hop = T / ratio;
            // When (almost) unshifted, pull the grain train back onto the
            // analysis epochs so wet and dry stay phase-coherent (no comb when mixing).
            const double align = std::clamp (1.0 - std::abs (target - midiIn) / 0.15, 0.0, 1.0);
            voicedMakeup = 1.0 + 0.2 * std::min (1.0, (double) std::abs (target - midiIn));
            hop -= align * 0.5 * (nextSyn - (double) a.pos);
            readRate = link ? ratio : formantRatio;
            // Two source periods around the epoch: neighbouring pulses fall on
            // the window zeros, so each grain is (almost) one glottal response.
            halfLen = T / readRate;
            readCentre = (double) a.pos;
        }
        else
        {
            haveNote = false;
            hop = uvHop;
            const float noiseRatio = mode == Mode::Transpose ? semisToRatio (pitchSm) : 1.0f;
            readRate = link ? noiseRatio : formantRatio;
            halfLen = 2.0 * uvHop / readRate;
            readCentre = nextSyn; // time-aligned so unshifted noise reconstructs exactly
        }

        halfLen = std::min (halfLen, 2.0 * tMax / std::max (1.0, readRate));
        halfLen = std::min (halfLen, 2.0 * tMax);
        halfLen = std::max (halfLen, 2.0);

        double gain;
        if (a.voiced)
        {
            // Pulses add incoherently and the window tapers each response a bit;
            // a fixed make-up measured on synthetic vowels keeps levels within ~2 dB.
            gain = voicedMakeup;
        }
        else
        {
            const double coherent = hop / halfLen;
            const double incoherent = std::sqrt (hop / (0.75 * halfLen));
            const double t = std::min (1.0, std::abs (std::log2 (readRate)) / 0.1);
            gain = std::min (coherent + t * (incoherent - coherent), 2.0);
        }
        const double centre = nextSyn;
        const int64_t first = (int64_t) std::ceil (centre - halfLen);
        const int64_t last = (int64_t) std::floor (centre + halfLen);
        const double invHalf = 3.141592653589793 / halfLen;

        for (int64_t n = first; n <= last; ++n)
        {
            const double k = (double) n - centre;
            const double w = 0.5 * (1.0 + std::cos (k * invHalf));
            const float gw = (float) (gain * w);
            const double src = readCentre + k * readRate;
            for (int ch = 0; ch < numCh; ++ch)
                outBuf[(size_t) ch][(size_t) (n & mask)] += gw * readInput (inBuf[(size_t) ch], src);
        }

        lastRatio = ratio;
        lastHop = std::max (hop, 1.0);
        nextSyn += lastHop;
    }

    double sr = 44100.0;
    int decim = 1, tMax = 588, latency = 2500, bufSize = 0, mask = 0;
    double uvHop = 220.0;

    YinDetector yin;
    int yinLen = 0, yinHop = 256;

    int numCh = 1;
    std::vector<std::vector<float>> inBuf, outBuf;
    std::vector<float> lpBuf, decBuf, yinScratch;
    int64_t inPos = 0;

    float lpCoeff = 0.1f, lp1 = 0.0f, lp2 = 0.0f;
    float aaCoeff = 0.5f, aa1 = 0.0f, aa2 = 0.0f;
    int decCount = 0, decWrite = 0, decFilled = 0, hopCount = 0;
    float decAccum = 0.0f;

    std::vector<Estimate> estimates;
    int estHead = 0, estCount = 0, estCursor = 0;
    float periodHistory[3] {};
    float lastDetectedHz = 0.0f;

    std::vector<Mark> marks;
    int markHead = 0, markCount = 0, synCursorAbs = 0;
    double nextMarkPos = 0.0;

    double nextSyn = 0.0;
    bool haveNote = false;
    float currentNote = 0.0f, lastRatio = 1.0f;

    Mode mode = Mode::Transpose;
    float pitch = 0.0f, formant = 0.0f;
    bool link = false;
    float neutralFade = 0.0f, fadeCoeff = 0.001f;
    float pitchSm = 0.0f, formantSm = 0.0f;
    bool smoothInit = false;
    double lastHop = 1.0;
};

} // namespace txiki
