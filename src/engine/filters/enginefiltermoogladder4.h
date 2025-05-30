#pragma once

// Filter based on the text "Non linear digital implementation of the moog ladder filter"
// by Antti Houvilainen
// This implementation is probably a more accurate digital representation of the original analogue filter.
// This is version 2 (revised 14/DEC/04), with improved amplitude/resonance scaling and frequency
// correction using a couple of polynomials,as suggested by Antti.

// Adopted from Csound code at http://www.kunstmusik.com/udo/cache/moogladder.udo
// Based on C Source from R. Lindner published at public domain
// http://musicdsp.org/showArchiveComment.php?ArchiveID=196

#include "audio/types.h"
#include "engine/engine.h"
#include "engine/engineobject.h"
#include "util/sample.h"

namespace {

// 'thermal voltage of a transistor'
// defines the strange of the non linearity
// 1.2 = drives the transistor in full range, giving a maximum Waveshaper effect
// big values disables the non linearity
constexpr float kVt = 1.2f;
constexpr float kPi = 3.14159265358979323846f;

} // anonymous namespace

enum class MoogMode {
    LowPass,
    HighPass,
    LowPassOversampling,
    HighPassOversampling,
};

template<MoogMode MODE>
class EngineFilterMoogLadderBase : public EngineObjectConstIn {

  private:
    struct Buffer {
         CSAMPLE m_azt1;
         CSAMPLE m_azt2;
         CSAMPLE m_azt3;
         CSAMPLE m_azt4;
         CSAMPLE m_az5;
         CSAMPLE m_amf;
    };

  public:
    EngineFilterMoogLadderBase(
            mixxx::audio::SampleRate sampleRate, float cutoff, float resonance) {
         initBuffers();
         setParameter(sampleRate, cutoff, resonance);
         m_postGain = m_postGainNew;
         m_kacr = m_kacrNew;
         m_k2vg = m_k2vgNew;
    }

    virtual ~EngineFilterMoogLadderBase() {
    }

    void initBuffers() {
        memset(&m_buf, 0, sizeof(m_buf));
        m_buffersClear = true;
        m_doRamping = true;
    }

    // cutoff  in Hz
    // resonance  range 0 ... 4 (4 = self resonance)
    void setParameter(mixxx::audio::SampleRate sampleRate, float cutoff, float resonance) {
        constexpr float v2 = 2 + kVt; // twice the 'thermal voltage of a transistor'

        float kfc = cutoff / sampleRate;
        float kf = kfc;
        if (MODE == MoogMode::LowPassOversampling || MODE == MoogMode::HighPassOversampling) {
            // m_inputSampeRate is half the actual filter sample rate in oversampling mode
            kf = kfc / 2;
        }

        // frequency & amplitude correction
        const float kfcr = 1.8730f * (kfc * kfc * kfc) + 0.4955f * (kfc * kfc) -
                0.6490f * kfc + 0.9988f;

        float x = -2.0f * kPi * kfcr * kf; // input for taylor approximations
        float exp_out  = expf(x);
        m_k2vgNew = v2 * (1 - exp_out); // filter tuning

        // Resonance correction for self oscillation ~4
        m_kacrNew = resonance * (-3.9364f * (kfc * kfc) + 1.8409f * kfc + 0.9968f);

        // See https://github.com/mixxxdj/mixxx/pull/11177 for the Jupyter
        // notebook used to derive this
        if (MODE == MoogMode::HighPassOversampling || MODE == MoogMode::HighPass) {
            // This for all intents and purposes is just 1.0, so let's just stick with that
            // m_postGainNew = 0.9999999983339118f + (4.58745459575558e-13f * resonance);
            m_postGainNew = 1.0;
        } else {
            // Analyzing this filter as a linear system will show a small dip in
            // passband/DC gain when the cutoff frequency is around 10 kHz:
            // https://github.com/mixxxdj/mixxx/pull/11177#issuecomment-1374833386
            //
            // In practice this is either not a noticeable issue when running
            // music through the filter, or it might even be desirable as it
            // keeps the overall gain increase a bit lower when the resonance is
            // turned up and the cutoff frequency is around 10 kHz. This may be
            // worth more experimentation in the future.
            m_postGainNew = 1.0001784074555027f + (0.9331585678097162f * resonance);
        }

        m_doRamping = true;

        // qDebug() << "setParameter" << m_cutoff << m_resonance;
    }

    // this is can be used instead off a final process() call before pause
    // It fades to dry or 0 according to the m_startFromDry parameter
    // it is an alternative for using pauseFillter() calls
    void processAndPauseFilter(const CSAMPLE* M_RESTRICT pIn,
            CSAMPLE* M_RESTRICT pOutput,
            const std::size_t bufferSize) {
        process(pIn, pOutput, bufferSize);
        SampleUtil::linearCrossfadeBuffersOut(
                pOutput, // fade out filtered
                pIn,     // fade in dry
                bufferSize,
                mixxx::kEngineChannelOutputCount);
        initBuffers();
    }

    virtual void process(const CSAMPLE* pIn, CSAMPLE* pOutput, const std::size_t bufferSize) {
        if (!m_doRamping) {
            for (std::size_t i = 0; i < bufferSize; i += 2) {
                pOutput[i] = processSample(pIn[i], &m_buf[0]);
                pOutput[i+1] = processSample(pIn[i+1], &m_buf[1]);
            }
        } else if (!m_buffersClear) {
            float startPostGain = m_postGain;
            float startKacr = m_kacr;
            float startK2vg = m_k2vg;
            double cross_mix = 0.0;
            double cross_inc = 2.0 / static_cast<double>(bufferSize);

            for (std::size_t i = 0; i < bufferSize; i += 2) {
                cross_mix += cross_inc;
                m_postGain = static_cast<float>(m_postGainNew * cross_mix +
                        startPostGain * (1.0 - cross_mix));
                m_kacr = static_cast<float>(m_kacrNew * cross_mix + startKacr * (1.0 - cross_mix));
                m_k2vg = static_cast<float>(m_k2vgNew * cross_mix + startK2vg * (1.0 - cross_mix));
                pOutput[i] = processSample(pIn[i], &m_buf[0]);
                pOutput[i+1] = processSample(pIn[i+1], &m_buf[1]);
            }

        } else {
            m_postGain = m_postGainNew;
            m_kacr = m_kacrNew;
            m_k2vg = m_k2vgNew;
            double cross_mix = 0.0;
            double cross_inc = 4.0 / static_cast<double>(bufferSize);
            for (std::size_t i = 0; i < bufferSize; i += 2) {
                // Do a linear cross fade between the output of the old
                // Filter and the new filter.
                // The new filter is settled for Input = 0 and it sees
                // all frequencies of the rectangular start impulse.
                // Since the group delay, after which the start impulse
                // has passed is unknown here, we just what the half
                // bufferSize until we use the samples of the new filter.
                // In one of the previous version we have faded the Input
                // of the new filter but it turns out that this produces
                // a gain drop due to the filter delay which is more
                // conspicuous than the settling noise.
                CSAMPLE old1 = pIn[i];
                CSAMPLE old2 = pIn[i + 1];
                double new1 = processSample(pIn[i], &m_buf[0]);
                double new2 = processSample(pIn[i+1], &m_buf[1]);

                if (i < bufferSize / 2) {
                    pOutput[i] = old1;
                    pOutput[i + 1] = old2;
                } else {
                    pOutput[i] = static_cast<CSAMPLE>(new1 * cross_mix + old1 * (1.0 - cross_mix));
                    pOutput[i + 1] = static_cast<CSAMPLE>(
                            new2 * cross_mix + old2 * (1.0 - cross_mix));
                    cross_mix += cross_inc;
                }
            }
        }
        m_doRamping = false;
        m_buffersClear = false;
    }

    inline CSAMPLE processSample(float input, struct Buffer* pB) {
        constexpr float v2 = 2 + kVt; // twice the 'thermal voltage of a transistor'

        // cascade of 4 1st-order sections
        float x1 = input - pB->m_amf * m_kacr;
        float az1 = pB->m_azt1 + m_k2vg * tanh_approx(x1 / v2);
        float at1 = m_k2vg * tanh_approx(az1 / v2);
        pB->m_azt1 = az1 - at1;
        float az2 = pB->m_azt2 + at1;
        float at2 = m_k2vg * tanh_approx(az2 / v2);
        pB->m_azt2 = az2 - at2;
        float az3 = pB->m_azt3 + at2;
        float at3 = m_k2vg * tanh_approx(az3 / v2);
        pB->m_azt3 = az3 - at3;
        float az4 = pB->m_azt4 + at3;
        float at4 = m_k2vg * tanh_approx(az4 / v2);
        pB->m_azt4 = az4 - at4;

        // Oversampling if requested
        if (MODE == MoogMode::LowPassOversampling || MODE == MoogMode::HighPassOversampling) {
            // 1/2-sample delay for phase compensation
            pB->m_amf = (az4 + pB->m_az5) / 2;
            pB->m_az5 = az4;

            // Oversampling (repeat same block)
            float x1 = input - pB->m_amf * m_kacr;
            float az1 = pB->m_azt1 + m_k2vg * tanh_approx(x1 / v2);
            float at1 = m_k2vg * tanh_approx(az1 / v2);
            pB->m_azt1 = az1 - at1;
            float az2 = pB->m_azt2 + at1;
            float at2 = m_k2vg * tanh_approx(az2 / v2);
            pB->m_azt2 = az2 - at2;
            float az3 = pB->m_azt3 + at2;
            float at3 = m_k2vg * tanh_approx(az3 / v2);
            pB->m_azt3 = az3 - at3;
            float az4 = pB->m_azt4 + at3;
            float at4 = m_k2vg * tanh_approx(az4 / v2);
            pB->m_azt4 = az4 - at4;

            // 1/2-sample delay for phase compensation
            pB->m_amf = (az4 + pB->m_az5) / 2;
            pB->m_az5 = az4;
        } else {
            pB->m_amf = az4;
        }

        if (MODE == MoogMode::HighPassOversampling || MODE == MoogMode::HighPass) {
            return (x1 - 3 * az3 + 2 * az4) * m_postGain;
        }
        return pB->m_amf * m_postGain;
    }

    inline float tanh_approx(float input) {
        // return tanhf(input); // 142ns for process;
        return input / (1 + input * input / (3 + input * input / 5)); // 119ns for process
    }


  private:

    struct Buffer m_buf[2];

    float m_postGain;
    float m_kacr; // resonance factor
    float m_k2vg; // IIF factor

    float m_postGainNew;
    float m_kacrNew; // resonance factor
    float m_k2vgNew; // IIF factor

    bool m_doRamping;

    bool m_buffersClear;
};

class EngineFilterMoogLadder4Low : public EngineFilterMoogLadderBase<MoogMode::LowPassOversampling> {
    Q_OBJECT
  public:
    EngineFilterMoogLadder4Low(mixxx::audio::SampleRate sampleRate,
            double freqCorner1,
            double resonance);
};


class EngineFilterMoogLadder4High : public EngineFilterMoogLadderBase<MoogMode::HighPassOversampling> {
    Q_OBJECT
  public:
    EngineFilterMoogLadder4High(mixxx::audio::SampleRate sampleRate,
            double freqCorner1,
            double resonance);
};
