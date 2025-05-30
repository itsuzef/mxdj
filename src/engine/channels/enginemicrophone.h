#pragma once

#include <QScopedPointer>

#include "engine/channels/enginechannel.h"
#include "soundio/soundmanagerutil.h"

class ControlAudioTaperPot;

// EngineMicrophone is an EngineChannel that implements a mixing source whose
// samples are fed directly from the SoundManager
class EngineMicrophone : public EngineChannel, public AudioDestination {
    Q_OBJECT
  public:
    EngineMicrophone(const ChannelHandleAndGroup& handleGroup,
            EffectsManager* pEffectsManager);
    ~EngineMicrophone() override;

    ActiveState updateActiveState() override;

    // Called by EngineMixer whenever is requesting a new buffer of audio.
    void process(CSAMPLE* pOutput, const std::size_t bufferSize) override;
    void collectFeatures(GroupFeatureState* pGroupFeatures) const override;

    // This is called by SoundManager whenever there are new samples from the
    // configured input to be processed. This is run in the callback thread of
    // the soundcard this AudioDestination was registered for! Beware, in the
    // case of multiple soundcards, this method is not re-entrant but it may be
    // concurrent with EngineMixer processing.
    void receiveBuffer(const AudioInput& input,
            const CSAMPLE* pBuffer,
            unsigned int iNumSamples) override;

    // Called by SoundManager whenever the microphone input is connected to a
    // soundcard input.
    void onInputConfigured(const AudioInput& input) override;

    // Called by SoundManager whenever the microphone input is disconnected from
    // a soundcard input.
    void onInputUnconfigured(const AudioInput& input) override;

    bool isSolo();
    double getSoloDamping();

  private:
    QScopedPointer<ControlObject> m_pInputConfigured;
    ControlAudioTaperPot* m_pPregain;
};
