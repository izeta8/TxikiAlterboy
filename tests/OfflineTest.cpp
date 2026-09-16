// Offline verification of the VoiceShifter DSP.
// Synthesises a vowel (glottal pulse train through formant resonators), runs it
// through every mode and checks output pitch (YIN) and spectral centroid.
// Also writes WAV files to the output directory for listening.

#include "../Source/dsp/VoiceShifter.h"

#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <fstream>
#include <numeric>
#include <string>

using namespace txiki;

static const double kPi = 3.141592653589793;

struct Resonator
{
    double b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0;
    Resonator (double freq, double bw, double sr)
    {
        const double r = std::exp (-kPi * bw / sr);
        a1 = -2.0 * r * std::cos (2.0 * kPi * freq / sr);
        a2 = r * r;
        b0 = 1.0 - r;
    }
    double process (double x)
    {
        const double y = b0 * x - a1 * y1 - a2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

static std::vector<float> makeVowel (double sr, double seconds, double f0, double vibratoCents, double formantScale = 1.0)
{
    std::vector<float> out ((size_t) (sr * seconds));
    Resonator f1 (700 * formantScale, 90, sr), f2 (1220 * formantScale, 110, sr), f3 (2600 * formantScale, 170, sr);
    double phase = 0.0, prev = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = (double) i / sr;
        const double f = f0 * std::pow (2.0, vibratoCents * std::sin (2.0 * kPi * 5.0 * t) / 1200.0);
        phase += f / sr;
        if (phase >= 1.0)
            phase -= 1.0;
        // Rosenberg-like glottal flow derivative.
        const double open = 0.6;
        double g = phase < open ? 0.5 * (1.0 - std::cos (kPi * phase / open)) : std::cos (kPi * (phase - open) / (2.0 * (1.0 - open)));
        if (phase >= open)
            g = std::max (0.0, g);
        const double dg = g - prev;
        prev = g;
        const double s = f1.process (dg) * 1.0 + f2.process (dg) * 0.6 + f3.process (dg) * 0.3;
        out[i] = (float) s;
    }
    float peak = 1.0e-9f;
    for (auto v : out)
        peak = std::max (peak, std::abs (v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

static void writeWav (const std::string& path, const std::vector<float>& data, int sr)
{
    std::ofstream f (path, std::ios::binary);
    auto w32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto w16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t bytes = (uint32_t) data.size() * 2;
    f.write ("RIFF", 4);
    w32 (36 + bytes);
    f.write ("WAVEfmt ", 8);
    w32 (16);
    w16 (1);
    w16 (1);
    w32 ((uint32_t) sr);
    w32 ((uint32_t) sr * 2);
    w16 (2);
    w16 (16);
    f.write ("data", 4);
    w32 (bytes);
    for (auto v : data)
    {
        const int16_t s = (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f);
        f.write ((const char*) &s, 2);
    }
}

// Median pitch over the steady part of the signal.
static double measurePitch (const std::vector<float>& x, double sr, size_t start, size_t end)
{
    YinDetector yin;
    yin.prepare (sr, 60.0, 1100.0);
    const int len = yin.getRequiredLength();
    std::vector<double> values;
    for (size_t i = start; i + (size_t) len < end; i += 512)
    {
        float ap = 0;
        const float p = yin.analyse (&x[i], ap);
        if (p > 0)
            values.push_back (sr / p);
    }
    if (values.empty())
        return 0.0;
    std::sort (values.begin(), values.end());
    return values[values.size() / 2];
}

// Harmonic magnitude profile (dB) at multiples of f0 between 200 Hz and 3.5 kHz.
static std::vector<double> harmonicProfile (const std::vector<float>& x, double sr, size_t start, double f0)
{
    std::vector<double> prof;
    const size_t n = (size_t) (1.0 * sr);
    for (int h = 1; h * f0 < 3500.0; ++h)
    {
        const double freq = h * f0;
        if (freq < 200.0)
            continue;
        double re = 0, im = 0;
        for (size_t i = 0; i < n && start + i < x.size(); ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * kPi * i / (n - 1));
            const double ang = 2 * kPi * freq * i / sr;
            re += x[start + i] * w * std::cos (ang);
            im += x[start + i] * w * std::sin (ang);
        }
        prof.push_back (10.0 * std::log10 (re * re + im * im + 1e-20));
    }
    return prof;
}

// RMS dB distance after removing overall level difference.
static double profileDistance (const std::vector<double>& a, const std::vector<double>& b)
{
    const size_t n = std::min (a.size(), b.size());
    if (n == 0)
        return 1e9;
    double mean = 0;
    for (size_t i = 0; i < n; ++i)
        mean += a[i] - b[i];
    mean /= n;
    double acc = 0;
    for (size_t i = 0; i < n; ++i)
        acc += std::pow (a[i] - b[i] - mean, 2.0);
    return std::sqrt (acc / n);
}

// In-place radix FFT (n power of two).
static void fft (std::vector<double>& re, std::vector<double>& im)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * kPi / (double) len;
        const double wr = std::cos (ang), wi = std::sin (ang);
        for (size_t i = 0; i < n; i += len)
        {
            double cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; ++k)
            {
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                const double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
                const double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

// Band-limited bright vowel: additive harmonics up to Nyquist with formant envelope.
static std::vector<float> makeBrightVowel (double sr, double seconds, double f0)
{
    std::vector<float> out ((size_t) (sr * seconds), 0.0f);
    auto env = [] (double f)
    {
        auto peak = [f] (double fc, double bw) { return 1.0 / (1.0 + std::pow ((f - fc) / bw, 2.0)); };
        return 0.15 + peak (700, 120) + 0.6 * peak (1220, 150) + 0.4 * peak (2600, 250) + 0.3 * peak (3500, 400);
    };
    for (int h = 1; h * f0 < 0.48 * sr; ++h)
    {
        const double a = env (h * f0) / std::sqrt ((double) h);
        const double w = 2 * kPi * h * f0 / sr;
        for (size_t i = 0; i < out.size(); ++i)
            out[i] += (float) (a * std::sin (w * i));
    }
    float peak = 1e-9f;
    for (auto v : out)
        peak = std::max (peak, std::abs (v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

// Energy away from the harmonics of f0 relative to harmonic energy, 1 kHz..20 kHz, in dB.
static double inharmonicDb (const std::vector<float>& x, double sr, size_t start, double f0)
{
    const size_t n = 32768;
    std::vector<double> re (n), im (n, 0.0);
    for (size_t i = 0; i < n; ++i)
        re[i] = x[start + i] * (0.5 - 0.5 * std::cos (2 * kPi * i / (n - 1)));
    fft (re, im);
    double harm = 0, inh = 0;
    for (size_t k = (size_t) (1000.0 * n / sr); k < (size_t) (20000.0 * n / sr); ++k)
    {
        const double freq = k * sr / n;
        const double p = re[k] * re[k] + im[k] * im[k];
        const double h = freq / f0;
        const double distHz = std::abs (h - std::round (h)) * f0;
        (distHz < 12.0 ? harm : inh) += p;
    }
    return 10 * std::log10 ((inh + 1e-30) / (harm + 1e-30));
}

// Minimal 16-bit PCM / 32-bit float WAV reader (first channel).
static std::vector<float> readWav (const std::string& path, int& sampleRate)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<char> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    std::vector<float> out;
    if (bytes.size() < 12 || std::string (bytes.data(), 4) != "RIFF")
        return out;
    auto u16 = [&] (size_t o) { return (uint16_t) ((uint8_t) bytes[o] | ((uint8_t) bytes[o + 1] << 8)); };
    auto u32 = [&] (size_t o) { return (uint32_t) u16 (o) | ((uint32_t) u16 (o + 2) << 16); };
    int format = 1, channels = 1, bits = 16;
    size_t pos = 12;
    while (pos + 8 <= bytes.size())
    {
        const std::string id (bytes.data() + pos, 4);
        const size_t len = u32 (pos + 4);
        if (id == "fmt ")
        {
            format = u16 (pos + 8);
            channels = u16 (pos + 10);
            sampleRate = (int) u32 (pos + 12);
            bits = u16 (pos + 22);
        }
        else if (id == "data")
        {
            const size_t frameBytes = (size_t) channels * bits / 8;
            for (size_t i = pos + 8; i + frameBytes <= pos + 8 + len && i + frameBytes <= bytes.size(); i += frameBytes)
            {
                if (format == 3 && bits == 32)
                {
                    float v;
                    std::memcpy (&v, &bytes[i], 4);
                    out.push_back (v);
                }
                else if (bits == 16)
                    out.push_back ((int16_t) u16 (i) / 32768.0f);
            }
            break;
        }
        pos += 8 + len + (len & 1);
    }
    return out;
}

// Count 2 ms frames whose high-frequency (2nd difference) energy jumps > 15 dB above both neighbours.
static int countClicks (const std::vector<float>& x, double sr)
{
    const size_t frame = (size_t) (0.002 * sr);
    std::vector<double> e;
    for (size_t s = 2; s + frame < x.size(); s += frame)
    {
        double acc = 0;
        for (size_t i = s; i < s + frame; ++i)
        {
            const double d2 = x[i] - 2.0 * x[i - 1] + x[i - 2];
            acc += d2 * d2;
        }
        e.push_back (acc / frame);
    }
    int clicks = 0;
    const double floor = std::pow (10.0, -70.0 / 10.0);
    for (size_t i = 1; i + 1 < e.size(); ++i)
        if (e[i] > floor && e[i] > 31.6 * e[i - 1] && e[i] > 31.6 * e[i + 1])
            ++clicks;
    return clicks;
}

static int processVoiceFile (const std::string& path, const std::string& outDir)
{
    int sr = 48000;
    const auto input = readWav (path, sr);
    if (input.empty())
    {
        std::printf ("could not read %s\n", path.c_str());
        return 1;
    }
    const std::string base = path.substr (path.find_last_of ("/\\") + 1, path.find_last_of ('.') - path.find_last_of ("/\\") - 1);
    const int inputClicks = countClicks (input, sr);
    std::printf ("\n%s: %.1fs @%d Hz, input clicks=%d\n", base.c_str(), input.size() / (double) sr, sr, inputClicks);

    struct Setting { const char* name; Mode mode; float pitch, formant; bool link; };
    const Setting settings[] = {
        { "up5", Mode::Transpose, 5.0f, 0.0f, false },
        { "down7", Mode::Transpose, -7.0f, 0.0f, false },
        { "down12", Mode::Transpose, -12.0f, 0.0f, false },
        { "formant+4", Mode::Transpose, 0.0f, 4.0f, false },
        { "formant-4", Mode::Transpose, 0.0f, -4.0f, false },
        { "link+7", Mode::Transpose, 7.0f, 0.0f, true },
        { "quantize", Mode::Quantize, 0.0f, 0.0f, false },
        { "robot", Mode::Robot, 0.0f, 0.0f, false },
    };
    int failures = 0;
    for (const auto& st : settings)
    {
        VoiceShifter vs;
        vs.prepare (sr);
        vs.setParameters (st.mode, st.pitch, st.formant, st.link);
        const int lat = vs.getLatencySamples();
        std::vector<float> out (input.size() + (size_t) lat);
        std::vector<float> hzTrack;
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = vs.processSample (i < input.size() ? input[i] : 0.0f);
            if (i % 256 == 0)
                hzTrack.push_back (vs.getDetectedHz());
        }
        out.erase (out.begin(), out.begin() + lat);

        int voiced = 0, octaveJumps = 0;
        for (size_t i = 0; i < hzTrack.size(); ++i)
        {
            if (hzTrack[i] > 0)
                ++voiced;
            if (i > 0 && hzTrack[i] > 0 && hzTrack[i - 1] > 0 && std::abs (12.0 * std::log2 (hzTrack[i] / hzTrack[i - 1])) > 7.0)
            {
                ++octaveJumps;
                if (std::getenv ("TXIKI_VERBOSE") && st.mode == Mode::Transpose && st.pitch == 5.0f)
                    std::printf ("    jump @%.3fs: %.1f -> %.1f -> %.1f -> %.1f Hz\n", i * 256.0 / sr, i > 1 ? hzTrack[i - 2] : 0.0f, hzTrack[i - 1], hzTrack[i],
                                 i + 1 < hzTrack.size() ? hzTrack[i + 1] : 0.0f);
            }
        }
        double eIn = 0, eOut = 0;
        for (size_t i = 0; i < input.size(); ++i)
        {
            eIn += (double) input[i] * input[i];
            eOut += (double) out[i] * out[i];
        }
        std::printf ("  %-10s clicks=%3d (input %d)  level=%+5.1f dB  voiced=%3.0f%%  pitch jumps>7st=%d\n", st.name, countClicks (out, sr),
                     inputClicks, 10.0 * std::log10 (eOut / eIn), 100.0 * voiced / hzTrack.size(), octaveJumps);
        writeWav (outDir + "/voice_" + base + "_" + st.name + ".wav", out, sr);
        const double levelDb = 10.0 * std::log10 (eOut / eIn);
        if (octaveJumps > 0 || std::abs (levelDb) > 3.0 || countClicks (out, sr) > inputClicks + 5)
        {
            std::printf ("    ^ FAIL\n");
            ++failures;
        }
    }
    return failures;
}

struct Case
{
    std::string name;
    Mode mode;
    float pitch, formant;
    bool link;
    double f0;
    double expectHz;       // expected output pitch
    double expectF1Ratio;  // expected F1 ratio vs input
};

int main (int argc, char** argv)
{
    if (argc > 3 && std::string (argv[1]) == "--voices")
    {
        int rc = 0;
        for (int i = 3; i < argc; ++i)
            rc |= processVoiceFile (argv[i], argv[2]);
        return rc;
    }

    const std::string outDir = argc > 1 ? argv[1] : ".";
    const double sr = 48000.0;
    int failures = 0;

    const Case cases[] = {
        { "transpose_+5", Mode::Transpose, 5.0f, 0.0f, false, 180.0, 180.0 * std::pow (2.0, 5.0 / 12.0), 1.0 },
        { "transpose_-12", Mode::Transpose, -12.0f, 0.0f, false, 220.0, 110.0, 1.0 },
        { "transpose_+12", Mode::Transpose, 12.0f, 0.0f, false, 150.0, 300.0, 1.0 },
        { "formant_+6", Mode::Transpose, 0.0f, 6.0f, false, 160.0, 160.0, std::pow (2.0, 6.0 / 12.0) },
        { "formant_-5", Mode::Transpose, 0.0f, -5.0f, false, 160.0, 160.0, std::pow (2.0, -5.0 / 12.0) },
        { "link_+7", Mode::Transpose, 7.0f, 0.0f, true, 150.0, 150.0 * std::pow (2.0, 7.0 / 12.0), std::pow (2.0, 7.0 / 12.0) },
        { "quantize", Mode::Quantize, 0.0f, 0.0f, false, 226.0, 220.0, 1.0 },
        { "quantize_+3", Mode::Quantize, 3.0f, 0.0f, false, 226.0, 220.0 * std::pow (2.0, 3.0 / 12.0), 1.0 },
        { "robot", Mode::Robot, 0.0f, 0.0f, false, 170.0, 523.2511, 1.0 },
        { "robot_-12", Mode::Robot, -12.0f, 0.0f, false, 170.0, 261.6256, 1.0 },
        { "low_voice_+5", Mode::Transpose, 5.0f, 0.0f, false, 90.0, 90.0 * std::pow (2.0, 5.0 / 12.0), 1.0 },
        { "bass_voice_+3", Mode::Transpose, 3.0f, 0.0f, false, 82.0, 82.0 * std::pow (2.0, 3.0 / 12.0), 1.0 },
    };

    for (const auto& c : cases)
    {
        const auto input = makeVowel (sr, 3.0, c.f0, c.mode == Mode::Transpose ? 0.0 : 0.0);
        VoiceShifter vs;
        vs.prepare (sr);
        vs.setParameters (c.mode, c.pitch, c.formant, c.link);

        std::vector<float> out (input.size());
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < input.size(); ++i)
            out[i] = vs.processSample (input[i]);
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double secs = std::chrono::duration<double> (t1 - t0).count();

        const size_t start = (size_t) (1.0 * sr), end = (size_t) (2.8 * sr);
        const double hz = measurePitch (out, sr, start, end);
        // Formant check: compare against ideal vowels at the output pitch.
        const double pitchRatio = c.expectHz / c.f0;
        const double wrongScale = std::abs (c.expectF1Ratio - 1.0) < 1e-6 ? pitchRatio : 1.0;
        const auto outProf = harmonicProfile (out, sr, start, c.expectHz);
        const double dRight = profileDistance (outProf, harmonicProfile (makeVowel (sr, 3.0, c.expectHz, 0.0, c.expectF1Ratio), sr, start, c.expectHz));
        const double dWrong = profileDistance (outProf, harmonicProfile (makeVowel (sr, 3.0, c.expectHz, 0.0, wrongScale), sr, start, c.expectHz));
        double peak = 0, rmsIn = 0, rmsOut = 0;
        for (size_t i = start; i < end; ++i)
        {
            peak = std::max (peak, (double) std::abs (out[i]));
            rmsIn += (double) input[i] * input[i];
            rmsOut += (double) out[i] * out[i];
        }
        const double gainDb = 10.0 * std::log10 ((rmsOut + 1e-12) / (rmsIn + 1e-12));

        const double centsErr = 1200.0 * std::log2 (std::max (hz, 1.0) / c.expectHz);
        const bool pitchOk = std::abs (centsErr) < 25.0;
        const bool formantOk = std::abs (wrongScale - 1.0) < 0.03 || (dRight < dWrong && dRight < 6.0);
        const bool ok = pitchOk && formantOk && std::abs (gainDb) < 6.0;
        if (!ok)
            ++failures;

        std::printf ("%-14s out=%7.2fHz exp=%7.2f (%+6.1fc) envelope dist right=%4.1fdB wrong=%4.1fdB gain=%+.1fdB rt=%.3fx %s\n",
                     c.name.c_str(), hz, c.expectHz, centsErr, dRight, dWrong, gainDb,
                     secs / 3.0, ok ? "OK" : "FAIL");

        writeWav (outDir + "/" + c.name + ".wav", out, (int) sr);
    }

    // Robustness: silence, glide with vibrato, noise burst, loud section, several sample rates.
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<float> sig ((size_t) (rate * 0.5), 0.0f);
        {
            std::vector<float> glide ((size_t) (rate * 2.0));
            Resonator r1 (650, 90, rate), r2 (1100, 110, rate);
            double ph = 0.0;
            for (size_t i = 0; i < glide.size(); ++i)
            {
                const double t = (double) i / rate;
                const double f = 110.0 * std::pow (4.0, t / 2.0) * std::pow (2.0, 40.0 * std::sin (2 * kPi * 5.5 * t) / 1200.0);
                ph += f / rate;
                const double pulse = ph >= 1.0 ? 1.0 : 0.0;
                if (ph >= 1.0)
                    ph -= 1.0;
                glide[i] = (float) (r1.process (pulse) + 0.5 * r2.process (pulse)) * 0.35f;
            }
            sig.insert (sig.end(), glide.begin(), glide.end());
        }
        uint32_t seed = 1;
        for (int i = 0; i < (int) (rate * 0.3); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            sig.push_back ((float) ((seed >> 8) / 16777216.0 - 0.5) * 0.3f);
        }
        const auto loud = makeVowel (rate, 1.0, 140.0, 30.0);
        for (auto v : loud)
            sig.push_back (v * 1.9f);

        for (int modeIdx = 0; modeIdx < 3; ++modeIdx)
            for (bool link : { false, true })
            {
                VoiceShifter vs;
                vs.prepare (rate);
                vs.setParameters ((Mode) modeIdx, modeIdx == 2 ? -5.0f : 7.0f, link ? 0.0f : -4.0f, link);
                double peak = 0;
                bool finite = true;
                std::vector<float> out (sig.size());
                const auto t0 = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < sig.size(); ++i)
                {
                    out[i] = vs.processSample (sig[i]);
                    finite = finite && std::isfinite (out[i]);
                    peak = std::max (peak, (double) std::abs (out[i]));
                }
                const double secs = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
                const bool ok = finite && peak < 3.0;
                if (!ok)
                    ++failures;
                std::printf ("robust sr=%6.0f mode=%d link=%d peak=%.2f cpu=%.3fx realtime %s\n", rate, modeIdx, (int) link, peak,
                             secs / (sig.size() / rate), ok ? "OK" : "FAIL");
                if (rate == 48000.0)
                    writeWav (outDir + "/robust_mode" + std::to_string (modeIdx) + (link ? "_link" : "") + ".wav", out, (int) rate);
            }
        if (rate == 48000.0)
            writeWav (outDir + "/robust_input.wav", sig, (int) rate);

        // Neutral settings should be (nearly) transparent after latency compensation.
        VoiceShifter vs;
        vs.prepare (rate);
        vs.setParameters (Mode::Transpose, 0.0f, 0.0f, false);
        const int lat = vs.getLatencySamples();
        double sigE = 0, errE = 0;
        for (size_t i = 0; i < sig.size(); ++i)
        {
            const float y = vs.processSample (sig[i]);
            if (i >= (size_t) lat + (size_t) rate)
            {
                const float ref = sig[i - (size_t) lat];
                sigE += (double) ref * ref;
                errE += (double) (y - ref) * (y - ref);
            }
        }
        const double snr = 10 * std::log10 (sigE / (errE + 1e-12));
        std::printf ("neutral sr=%6.0f SNR vs dry = %.1f dB %s\n", rate, snr, snr > 40.0 ? "OK" : "FAIL");
        if (snr <= 40.0)
            ++failures;
    }

    // Latency budget: no grain may read unreceived input or start before the output position.
    {
        uint64_t future = 0, late = 0, grains = 0;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (double f0 : { 76.0, 110.0, 300.0, 900.0 })
                for (int modeIdx = 0; modeIdx < 3; ++modeIdx)
                    for (float pitch : { -12.0f, 0.5f, 12.0f })
                        for (float formant : { -12.0f, 12.0f })
                            for (bool link : { false, true })
                            {
                                auto in = makeVowel (rate, 0.6, f0, 80.0);
                                uint32_t seed = 7;
                                for (size_t i = (size_t) (0.3 * rate); i < (size_t) (0.4 * rate); ++i)
                                {
                                    seed = seed * 1664525u + 1013904223u;
                                    in[i] = (float) ((seed >> 8) / 16777216.0 - 0.5) * 0.4f;
                                }
                                VoiceShifter vs;
                                vs.prepare (rate);
                                vs.setParameters ((Mode) modeIdx, pitch, formant, link);
                                for (auto v : in)
                                    vs.processSample (v);
                                future += vs.getStats().futureReads;
                                late += vs.getStats().lateGrains;
                                grains += vs.getStats().grains;
                            }
        const bool ok = future == 0 && late == 0;
        if (!ok)
            ++failures;
        std::printf ("latency budget: %llu grains, future reads=%llu, late grains=%llu %s\n", (unsigned long long) grains,
                     (unsigned long long) future, (unsigned long long) late, ok ? "OK" : "FAIL");
    }

    // Fast glides: output pitch must follow input pitch (x ratio) within tight limits.
    {
        auto makeGlide = [&] (double seconds)
        {
            std::vector<float> out ((size_t) (sr * seconds));
            Resonator f1 (700, 90, sr), f2 (1220, 110, sr), f3 (2600, 170, sr);
            double phase = 0.0, prev = 0.0;
            for (size_t i = 0; i < out.size(); ++i)
            {
                const double t = (double) i / sr;
                const double tri = std::abs (std::fmod (t, 0.6) / 0.3 - 1.0); // 1 octave in 300 ms, up and down
                const double f = 150.0 * std::pow (2.0, 1.0 - tri);
                phase += f / sr;
                if (phase >= 1.0)
                    phase -= 1.0;
                const double open = 0.6;
                double g = phase < open ? 0.5 * (1.0 - std::cos (kPi * phase / open)) : std::cos (kPi * (phase - open) / (2.0 * (1.0 - open)));
                if (phase >= open)
                    g = std::max (0.0, g);
                const double dg = g - prev;
                prev = g;
                out[i] = (float) (f1.process (dg) + f2.process (dg) * 0.6 + f3.process (dg) * 0.3) * 20.0f;
            }
            return out;
        };
        const auto glideIn = makeGlide (3.0);
        VoiceShifter vs;
        vs.prepare (sr);
        vs.setParameters (Mode::Transpose, 5.0f, 0.0f, false);
        const int lat = vs.getLatencySamples();
        std::vector<float> out (glideIn.size());
        for (size_t i = 0; i < glideIn.size(); ++i)
            out[i] = vs.processSample (glideIn[i]);

        YinDetector yin;
        yin.prepare (sr, 60.0, 1100.0);
        const int len = yin.getRequiredLength();
        std::vector<double> errs;
        double riseErr = 0, fallErr = 0;
        int riseN = 0, fallN = 0;
        for (size_t i = (size_t) sr / 2 + lat; i + len < out.size(); i += 256)
        {
            float ap = 0;
            const float pOut = yin.analyse (&out[i], ap);
            const float pIn = yin.analyse (&glideIn[i - lat], ap);
            if (pOut > 0 && pIn > 0)
            {
                const double signedErr = 1200.0 * std::log2 ((double) pIn / pOut) - 500.0;
                errs.push_back (std::abs (signedErr));
                const double tIn = (double) (i - lat) / sr;
                const bool rising = std::fmod (tIn, 0.6) < 0.3;
                (rising ? riseErr : fallErr) += signedErr;
                (rising ? riseN : fallN) += 1;
            }
        }
        std::printf ("glide signed mean error: rising %.1fc falling %.1fc\n", riseErr / std::max (1, riseN), fallErr / std::max (1, fallN));
        std::sort (errs.begin(), errs.end());
        const double med = errs[errs.size() / 2], p95 = errs[(size_t) (errs.size() * 0.95)];
        const bool ok = med < 15.0 && p95 < 60.0;
        if (!ok)
            ++failures;
        std::printf ("glide +5st latency=%d (%.1f ms) tracking error median=%.1fc p95=%.1fc %s\n", lat, 1000.0 * lat / sr, med, p95, ok ? "OK" : "FAIL");
    }

    // Worst-case block cost: 64-sample blocks, stereo, formant up (sinc path) at 48 kHz.
    {
        const auto voice = makeVowel (sr, 6.0, 150.0, 30.0);
        VoiceShifter vs;
        vs.prepare (sr, 2);
        vs.setParameters (Mode::Transpose, 7.0f, 5.0f, false);
        const int block = 64;
        // Audio threads in a DAW run at elevated priority; do the same here.
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        std::vector<double> times;
        for (size_t start = 0; start + block <= voice.size(); start += block)
        {
            const auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < block; ++i)
            {
                const float in[2] = { voice[start + i], voice[start + i] };
                float o[2];
                vs.processFrame (in, o);
            }
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::high_resolution_clock::now() - t0).count();
            times.push_back (ms);
        }
        // OS noise floor: a constant synthetic workload with the same mean cost per block.
        const double meanMs = std::accumulate (times.begin(), times.end(), 0.0) / times.size();
        int iters = 1000;
        {
            const auto t0 = std::chrono::high_resolution_clock::now();
            volatile double acc = 0;
            for (int i = 0; i < 1000000; ++i)
                acc = acc + std::sin ((double) i);
            const double perIter = std::chrono::duration<double, std::milli> (std::chrono::high_resolution_clock::now() - t0).count() / 1000000.0;
            iters = std::max (1, (int) (meanMs / perIter));
        }
        std::vector<double> noise;
        for (size_t start = 0; start + block <= voice.size(); start += block)
        {
            const auto t0 = std::chrono::high_resolution_clock::now();
            volatile double acc = 0;
            for (int i = 0; i < iters; ++i)
                acc = acc + std::sin ((double) i);
            noise.push_back (std::chrono::duration<double, std::milli> (std::chrono::high_resolution_clock::now() - t0).count());
        }
        std::sort (noise.begin(), noise.end());
        std::printf ("reference load p99.9=%.3fms worst=%.3fms\n", noise[(size_t) (noise.size() * 0.999)], noise.back());

        std::sort (times.begin(), times.end());
        const double budget = 1000.0 * block / sr;
        const double mean = std::accumulate (times.begin(), times.end(), 0.0) / times.size();
        const double p999 = times[(size_t) (times.size() * 0.999)];
        const double worst = times.back();
        const double noiseP999 = noise[(size_t) (noise.size() * 0.999)];
        // Algorithmic spikes only: the engine may not be much spikier than an equal constant load.
        const bool ok = p999 < std::max (0.5 * budget, 2.0 * noiseP999);
        if (!ok)
            ++failures;
        std::printf ("blocks(64) mean=%.3fms p99.9=%.3fms worst=%.3fms budget=%.2fms worst/mean=%.1fx %s\n", mean, p999, worst, budget,
                     worst / mean, ok ? "OK" : "FAIL");
    }

    // Aliasing: bright band-limited vowel shifted up; report inharmonic energy.
    {
        struct AliasCase { const char* name; Mode mode; float pitch, formant; bool link; double outF0; double maxDb; };
        const AliasCase aliasCases[] = {
            { "formant+12", Mode::Transpose, 0.0f, 12.0f, false, 197.3, -45.0 },
            { "formant+6", Mode::Transpose, 0.0f, 6.0f, false, 197.3, -45.0 },
            { "link+12", Mode::Transpose, 12.0f, 0.0f, true, 394.6, -45.0 },
            { "pitch+7", Mode::Transpose, 7.0f, 0.0f, false, 197.3 * std::pow (2.0, 7.0 / 12.0), -26.0 },
        };
        const auto bright = makeBrightVowel (sr, 2.0, 197.3);
        const double inputInh = inharmonicDb (bright, sr, (size_t) sr / 2, 197.3);
        for (const auto& ac : aliasCases)
        {
            VoiceShifter vs;
            vs.prepare (sr);
            vs.setParameters (ac.mode, ac.pitch, ac.formant, ac.link);
            std::vector<float> out (bright.size());
            for (size_t i = 0; i < bright.size(); ++i)
                out[i] = vs.processSample (bright[i]);
            const double measured = measurePitch (out, sr, (size_t) (0.9 * sr), (size_t) (1.9 * sr));
            const double inh = inharmonicDb (out, sr, (size_t) (0.9 * sr), measured > 0 ? measured : ac.outF0);
            const bool ok = inh < ac.maxDb;
            if (!ok)
                ++failures;
            std::printf ("alias %-11s inharmonic=%6.1f dB (limit %5.1f, input %6.1f dB) %s\n", ac.name, inh, ac.maxDb, inputInh, ok ? "OK" : "FAIL");
            writeWav (outDir + "/alias_" + ac.name + ".wav", out, (int) sr);
        }
    }

    // Stereo image: R = 0.5 * L delayed by 15 samples. Output must keep level ratio and inter-channel delay.
    for (int modeIdx = 0; modeIdx < 3; ++modeIdx)
    {
        const auto mono = makeVowel (sr, 3.0, 170.0, 20.0);
        VoiceShifter vs;
        vs.prepare (sr, 2);
        vs.setParameters ((Mode) modeIdx, modeIdx == 2 ? -3.0f : 5.0f, -2.0f, false);
        std::vector<float> outL (mono.size()), outR (mono.size());
        for (size_t i = 0; i < mono.size(); ++i)
        {
            const float in[2] = { mono[i], i >= 15 ? 0.5f * mono[i - 15] : 0.0f };
            float o[2];
            vs.processFrame (in, o);
            outL[i] = o[0];
            outR[i] = o[1];
        }
        const size_t s0 = (size_t) sr, s1 = (size_t) (2.8 * sr);
        int bestLag = 0;
        double bestCorr = -1e30;
        for (int lag = -40; lag <= 40; ++lag)
        {
            double acc = 0;
            for (size_t i = s0; i < s1; ++i)
                acc += (double) outL[i] * outR[(size_t) ((int64_t) i + lag)];
            if (acc > bestCorr)
            {
                bestCorr = acc;
                bestLag = lag;
            }
        }
        double eL = 0, eR = 0;
        for (size_t i = s0; i < s1; ++i)
        {
            eL += (double) outL[i] * outL[i];
            eR += (double) outR[i] * outR[i];
        }
        const double ratio = std::sqrt (eR / eL);
        const double expLag = 15.0 / std::pow (2.0, -2.0 / 12.0); // formant stretch scales intra-grain time
        const bool ok = std::abs (bestLag - expLag) <= 2.5 && std::abs (ratio - 0.5) < 0.05;
        if (!ok)
            ++failures;
        std::printf ("stereo mode=%d  R/L level=%.3f (exp 0.500)  R delay=%d (exp %.1f) %s\n", modeIdx, ratio, bestLag, expLag, ok ? "OK" : "FAIL");
    }

    writeWav (outDir + "/input_vowel.wav", makeVowel (sr, 3.0, 180.0, 0.0), (int) sr);
    std::printf ("latency @48k: %d samples\n", [] { VoiceShifter v; v.prepare (48000.0); return v.getLatencySamples(); }());
    std::printf ("%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
