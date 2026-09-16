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
        frame.assign ((size_t) getRequiredLength(), 0.0f);
    }

    int getRequiredLength() const { return window + maxLag + 1; }
    int getMaxLag() const { return maxLag; }
    int getWindow() const { return window; }

    // One-shot analysis (offline use). x points at getRequiredLength() samples.
    float analyse (const float* x, float& aperiodicity)
    {
        begin (x);
        while (!step (maxLag))
        {
        }
        return result (aperiodicity);
    }

    // Incremental analysis for the audio thread: begin() copies the frame, step()
    // computes a slice of lags, so the O(W * maxLag) cost is spread over many samples.
    void begin (const float* x)
    {
        std::copy (x, x + getRequiredLength(), frame.begin());
        double energy = 0.0;
        for (int j = 0; j < window; ++j)
            energy += (double) frame[(size_t) j] * frame[(size_t) j];
        silent = energy / window < 1.0e-6; // about -60 dBFS RMS
        nextTau = 1;
        running = 0.0;
        diff[0] = 1.0f;
    }

    // Returns true when all lags are done.
    bool step (int numTaus)
    {
        if (silent)
            return true;
        const float* x = frame.data();
        const int end = std::min (maxLag, nextTau + numTaus - 1);
        for (int tau = nextTau; tau <= end; ++tau)
        {
            const float* y = x + tau;
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
            int j = 0;
            for (; j + 4 <= window; j += 4)
            {
                const float d0 = x[j] - y[j], d1 = x[j + 1] - y[j + 1], d2 = x[j + 2] - y[j + 2], d3 = x[j + 3] - y[j + 3];
                s0 += d0 * d0;
                s1 += d1 * d1;
                s2 += d2 * d2;
                s3 += d3 * d3;
            }
            double sum = (double) s0 + s1 + s2 + s3;
            for (; j < window; ++j)
            {
                const float d = x[j] - y[j];
                sum += (double) d * d;
            }
            running += sum;
            diff[(size_t) tau] = running > 0.0 ? (float) (sum * tau / running) : 1.0f;
        }
        nextTau = end + 1;
        return nextTau > maxLag;
    }

    float result (float& aperiodicity) const
    {
        aperiodicity = 1.0f;
        if (silent)
            return 0.0f;
        const auto& d = diff;

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

        // Sub-multiple check: if a period of best/2 or best/3 is almost as periodic,
        // the dip at best is a subharmonic (octave-down error).
        for (int k = 3; k >= 2; --k)
        {
            const int centre = (int) std::lround ((double) best / k);
            if (centre - 2 < minLag)
                continue;
            int local = centre;
            for (int tau = centre - 2; tau <= centre + 2; ++tau)
                if (d[(size_t) tau] < d[(size_t) local])
                    local = tau;
            if (d[(size_t) local] < 2.0f * kThreshold && d[(size_t) local] <= d[(size_t) best] + 0.08f)
            {
                best = local;
                break;
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
    std::vector<float> diff, frame;
    int nextTau = 1;
    double running = 0.0;
    bool silent = true;
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
        // Latency budget (see placeGrain/generateMarks):
        //  grains are placed kLookahead*tMax ahead of the output, the nearest epoch can sit
        //  half a period later, and a grain reads at most tMax of source around it.
        markLag = (int) std::ceil (0.25 * tMax) + 4;
        latency = (int) std::ceil ((kLookahead + 1.5) * tMax) + std::max (0, markLag - (int) (0.5 * tMax)) + 8;
        uvHop = sr * 0.005;
        fadeCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));

        yin.prepare (sr / decim, kMinF0, kMaxF0);
        yinLen = yin.getRequiredLength();
        yinHop = 256;
        // Finish each analysis within ~3/4 of a hop so work never piles up.
        yinTausPerSample = std::max (1, (int) std::ceil (yin.getMaxLag() / (0.75 * yinHop)));

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

        if (sincTable.empty())
        {
            // Blackman-windowed sinc, cutoff 0.92 x Nyquist of the stretched grid.
            sincTable.resize ((size_t) (kSincZeros * kSincRes) + 2);
            for (size_t i = 0; i < sincTable.size(); ++i)
            {
                const double u = (double) i / kSincRes;
                const double x = 3.141592653589793 * 0.92 * u;
                const double sinc = u == 0.0 ? 1.0 : std::sin (x) / x;
                const double t = std::min (1.0, u / kSincZeros);
                const double win = 0.42 + 0.5 * std::cos (3.141592653589793 * t) + 0.08 * std::cos (2.0 * 3.141592653589793 * t);
                sincTable[i] = (float) (sinc * win);
            }
        }

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
        yinPending = false;
        yinPendingFrameStart = 0;
        lastPeriod = candidatePeriod = 0.0f;

        estimates.assign (kEstCap, Estimate {});
        estHead = 0;
        estCount = 0;
        estCursor = 0;

        marks.assign (kMarkCap, Mark {});
        markHead = 0;
        markCount = 0;
        pushMark ({ 0.0, (float) uvHop, false });
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

    // Lightweight counters for profiling/tests.
    struct Stats
    {
        uint64_t grains = 0, grainSamples = 0, yinBegins = 0, yinCatchUps = 0, marks = 0;
        uint64_t futureReads = 0, lateGrains = 0; // must stay 0: latency budget violations
    };
    const Stats& getStats() const { return stats; }

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
        while (nextSyn <= (double) outPos + kLookahead * tMax)
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
        double pos = 0.0; // fractional: sub-sample epoch accuracy keeps the high band coherent
        float period = 0.0f;
        bool voiced = false;
    };

    static constexpr int kEstCap = 256;
    static constexpr int kMarkCap = 1024;
    static constexpr double kEpochPull = 0.15;
    static constexpr int kSincZeros = 6;
    static constexpr int kSincRes = 512;
    std::vector<float> sincTable;

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

        // Continue a running analysis a few lags per sample.
        if (yinPending && yin.step (yinTausPerSample))
        {
            yinPending = false;
            publishEstimate();
        }

        if (++hopCount < yinHop || decFilled < yinLen)
            return;
        hopCount = 0;

        if (yinPending)
        {
            ++stats.yinCatchUps;
            while (!yin.step (yinTausPerSample))
            {
            }
            publishEstimate();
        }
        ++stats.yinBegins;
        yin.begin (&decBuf[(size_t) decWrite]);
        yinPendingFrameStart = inPos - (int64_t) (yinLen * decim);
        yinPending = true;
    }

    void publishEstimate()
    {
        float aperiodicity = 1.0f;
        float period = yin.result (aperiodicity) * (float) decim;

        bool voiced = period > 0.0f && period <= (float) tMax;

        // Jumps of more than 7 semitones (or hesitant onsets) must be confirmed by the
        // next analysis; until then the previous state is held. Kills one-hop octave
        // and harmonic errors at consonant/vowel boundaries for ~5 ms of extra lag.
        auto semis = [] (float a, float b) { return std::abs (12.0f * std::log2 (a / b)); };
        if (voiced)
        {
            const bool confirmsPending = candidatePeriod > 0.0f && semis (period, candidatePeriod) < 1.0f;
            if (lastPeriod > 0.0f && semis (period, lastPeriod) > 7.0f && !confirmsPending)
            {
                candidatePeriod = period;
                period = lastPeriod;
            }
            else if (lastPeriod <= 0.0f && aperiodicity > kConfidentOnset && !confirmsPending)
            {
                candidatePeriod = period;
                voiced = false;
            }
            else
                candidatePeriod = 0.0f;
        }
        else
            candidatePeriod = 0.0f;
        lastPeriod = voiced ? period : 0.0f;

        Estimate e;
        // For a period T, YIN compares samples [0, W + T) of the frame: that span's
        // centre is the time the estimate really describes.
        const double usedSpan = (double) yin.getWindow() * decim + (voiced ? (double) period : (double) tMax);
        e.centre = yinPendingFrameStart + (int64_t) std::llround (0.5 * usedSpan);
        e.voiced = voiced;
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

    // Pitch at time t: linear interpolation (in log-period) between the two estimates
    // whose centres bracket t, so glides are followed without hop-sized steps.
    Estimate estimateAt (double t)
    {
        if (estCount == 0)
            return {};
        // Cursor is an offset from estHead; move it forward monotonically.
        if (estCursor >= estCount)
            estCursor = estCount - 1;
        auto get = [this] (int i) -> const Estimate& { return estimates[(size_t) ((estHead + i) % kEstCap)]; };
        while (estCursor + 1 < estCount && (double) get (estCursor + 1).centre <= t)
            ++estCursor;

        const Estimate& a = get (estCursor);
        if (estCursor + 1 >= estCount && estCount >= 2 && t > (double) a.centre && a.voiced)
        {
            // Beyond the newest analysis: extrapolate the log-period trend (bounded),
            // otherwise glides lag by the analysis delay.
            const Estimate& p = get (estCount - 2);
            if (p.voiced && a.centre > p.centre)
            {
                const double slope = (std::log ((double) a.period) - std::log ((double) p.period)) / (double) (a.centre - p.centre);
                const double maxSlope = std::log (2.0) / (0.1 * sr); // at most one octave per 100 ms
                const double dt = std::min (t - (double) a.centre, 2.0 * tMax);
                Estimate e = a;
                e.period = (float) ((double) a.period * std::exp (std::clamp (slope, -maxSlope, maxSlope) * dt));
                return e;
            }
        }
        if (estCursor + 1 >= estCount || t <= (double) a.centre)
            return a;
        const Estimate& b = get (estCursor + 1);
        const double frac = (t - (double) a.centre) / (double) std::max<int64_t> (1, b.centre - a.centre);
        if (!a.voiced || !b.voiced)
            return frac < 0.5 ? a : b;
        Estimate e = a;
        e.period = (float) std::exp (std::log ((double) a.period) + frac * (std::log ((double) b.period) - std::log ((double) a.period)));
        return e;
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
        ++stats.marks;
        marks[(size_t) ((markHead + markCount) % kMarkCap)] = m;
        ++markCount;
    }

    const Mark& markAt (int i) const { return marks[(size_t) ((markHead + i) % kMarkCap)]; }

    void generateMarks()
    {
        const int64_t limit = inPos - markLag; // pitch beyond the newest analysis is extrapolated
        for (;;)
        {
            const Mark& last = markAt (markCount - 1);
            int64_t candidate = (int64_t) std::llround (nextMarkPos);
            const Estimate est = estimateAt (nextMarkPos);

            if (est.voiced)
            {
                const float period = est.period;
                double expected = last.voiced ? last.pos + period
                                              : last.pos + std::min ((double) period, uvHop);

                const int radius = std::max (1, (int) (period * 0.2f));
                const int64_t centreIdx = (int64_t) std::llround (expected);
                if (centreIdx + radius + 1 >= limit)
                    return;

                // Locate the strongest low-passed peak near the expected epoch (sub-sample).
                float peakAbs = 1.0e-9f;
                for (int64_t n = centreIdx - radius; n <= centreIdx + radius; ++n)
                    peakAbs = std::max (peakAbs, std::abs (lpBuf[(size_t) (n & mask)]));
                double bestScore = -1.0e30;
                int64_t bestIdx = centreIdx;
                for (int64_t n = centreIdx - radius; n <= centreIdx + radius; ++n)
                {
                    const double dist = ((double) n - expected) / radius;
                    const double score = lpBuf[(size_t) (n & mask)] - 0.35 * peakAbs * dist * dist;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestIdx = n;
                    }
                }
                const double ym = lpBuf[(size_t) ((bestIdx - 1) & mask)];
                const double y0 = lpBuf[(size_t) (bestIdx & mask)];
                const double yp = lpBuf[(size_t) ((bestIdx + 1) & mask)];
                const double den = ym - 2.0 * y0 + yp;
                const double peakPos = (double) bestIdx + (std::abs (den) > 1.0e-12 ? std::clamp (0.5 * (ym - yp) / den, -0.5, 0.5) : 0.0);

                // Phase-locked epochs: follow the period exactly and only drift slowly
                // towards the glottal peak; a fresh voiced segment snaps straight to it.
                double pos = last.voiced ? expected + kEpochPull * (peakPos - expected) : peakPos;
                pos = std::max (pos, last.pos + period * 0.5);
                pushMark ({ pos, period, true });
                nextMarkPos = pos + period;
            }
            else
            {
                if (last.voiced)
                    candidate = (int64_t) std::llround (last.pos + std::min ((double) last.period, uvHop));
                if (candidate >= limit)
                    return;
                pushMark ({ (double) candidate, (float) uvHop, false });
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

    // Band-limited read for rate > 1 (grain compressed in time): windowed sinc whose
    // cutoff follows 1/rate so upward formant shifts do not fold back as aliasing.
    float readInputBandLimited (const std::vector<float>& buf, double pos, double rate) const
    {
        if (rate <= 1.0001)
            return readInput (buf, pos);
        const double span = kSincZeros * rate;
        const int64_t n0 = (int64_t) std::ceil (pos - span);
        const int64_t n1 = (int64_t) std::floor (pos + span);
        const double scale = (double) kSincRes / rate;
        double acc = 0.0, wsum = 0.0;
        for (int64_t n = n0; n <= n1; ++n)
        {
            const double u = std::abs ((double) n - pos) * scale;
            const size_t idx = (size_t) u;
            if (idx + 1 >= sincTable.size())
                continue;
            const double frac = u - (double) idx;
            const double w = sincTable[idx] + frac * (sincTable[idx + 1] - sincTable[idx]);
            acc += w * buf[(size_t) (n & mask)];
            wsum += w;
        }
        return wsum > 1.0e-9 ? (float) (acc / wsum) : 0.0f;
    }

    void placeGrain()
    {
        const int64_t s = (int64_t) std::floor (nextSyn);

        // Floor analysis mark (<= s).
        if (synCursorAbs >= markCount)
            synCursorAbs = markCount - 1;
        while (synCursorAbs + 1 < markCount && markAt (synCursorAbs + 1).pos <= (double) s)
            ++synCursorAbs;
        Mark a = markAt (synCursorAbs);
        // Prefer the nearest voiced epoch so small corrections stay time-aligned with the dry signal.
        if (a.voiced && synCursorAbs + 1 < markCount)
        {
            const Mark& next = markAt (synCursorAbs + 1);
            if (next.voiced && next.pos - nextSyn < nextSyn - a.pos)
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
            if (ratio > 1.0f)
                voicedMakeup *= std::pow ((double) ratio, -0.3); // denser pulses already add loudness
            hop -= align * 0.5 * (nextSyn - a.pos);
            readRate = link ? ratio : formantRatio;
            // Two source periods around the epoch: neighbouring pulses fall on
            // the window zeros, so each grain is (almost) one glottal response.
            halfLen = T / readRate;
            readCentre = a.pos;
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

        // Bounded by the latency budget: at most tMax of source read, tMax of output written.
        halfLen = std::min (halfLen, (double) tMax / std::max (1.0, readRate));
        halfLen = std::min (halfLen, kLookahead * tMax);
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
        ++stats.grains;
        stats.grainSamples += (uint64_t) (last - first + 1);
        {
            const double maxSrc = readCentre + halfLen * readRate + 3.0;
            if (maxSrc > (double) (inPos - 1))
                ++stats.futureReads;
            if (first <= inPos - 1 - latency - 1)
                ++stats.lateGrains;
        }

        for (int64_t n = first; n <= last; ++n)
        {
            const double k = (double) n - centre;
            const double w = 0.5 * (1.0 + std::cos (k * invHalf));
            const float gw = (float) (gain * w);
            const double src = readCentre + k * readRate;
            for (int ch = 0; ch < numCh; ++ch)
                outBuf[(size_t) ch][(size_t) (n & mask)] += gw * readInputBandLimited (inBuf[(size_t) ch], src, readRate);
        }

        lastRatio = ratio;
        lastHop = std::max (hop, 1.0);
        nextSyn += lastHop;
    }

    double sr = 44100.0;
    static constexpr double kLookahead = 1.0;
    int decim = 1, tMax = 588, latency = 2500, bufSize = 0, mask = 0, markLag = 150;
    double uvHop = 220.0;

    YinDetector yin;
    int yinLen = 0, yinHop = 256, yinTausPerSample = 4;
    bool yinPending = false;
    float lastPeriod = 0.0f, candidatePeriod = 0.0f;
    static constexpr float kConfidentOnset = 0.1f;
    int64_t yinPendingFrameStart = 0;

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
    Stats stats;
    double lastHop = 1.0;
};

} // namespace txiki
