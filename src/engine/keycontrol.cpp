#include <QtDebug>

#include "engine/keycontrol.h"

#include "controlobject.h"
#include "controlpushbutton.h"
#include "controlpotmeter.h"
#include "engine/enginebuffer.h"
#include "track/keyutils.h"

KeyControl::KeyControl(const char* _group,
                       ConfigObject<ConfigValue>* _config)
        : EngineControl(_group, _config),
          m_dOldRate(0.0),
          m_bOldKeylock(false) {
    m_pPitch = new ControlPotmeter(ConfigKey(_group, "pitch"), -1.f, 1.f);
    connect(m_pPitch, SIGNAL(valueChanged(double)),
            this, SLOT(slotPitchChanged(double)),
            Qt::DirectConnection);
    connect(m_pPitch, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotPitchChanged(double)),
            Qt::DirectConnection);

    m_pButtonSyncKey = new ControlPushButton(ConfigKey(_group, "sync_key"));
    connect(m_pButtonSyncKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotSyncKey(double)),
            Qt::DirectConnection);

    m_pFileKey = new ControlObject(ConfigKey(_group, "file_key"));
    connect(m_pFileKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotFileKeyChanged(double)),
            Qt::DirectConnection);

    m_pEngineKey = new ControlObject(ConfigKey(_group, "key"));
    connect(m_pEngineKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotSetEngineKey(double)),
            Qt::DirectConnection);

    m_pRateSlider = ControlObject::getControl(ConfigKey(_group, "rate"));
    connect(m_pRateSlider, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateSlider, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pRateRange = ControlObject::getControl(ConfigKey(_group, "rateRange"));
    connect(m_pRateRange, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateRange, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pRateDir = ControlObject::getControl(ConfigKey(_group, "rate_dir"));
    connect(m_pRateDir, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateDir, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pKeylock = ControlObject::getControl(ConfigKey(_group, "keylock"));
    connect(m_pKeylock, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pKeylock, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
}

KeyControl::~KeyControl() {
    delete m_pPitch;
    delete m_pEngineKey;
    delete m_pFileKey;
}

double KeyControl::getPitchAdjustOctaves() {
    return m_pPitch->get();
}

double KeyControl::getKey() {
    return m_pEngineKey->get();
}

void KeyControl::slotRateChanged() {
    // If rate is non-1.0 then we have to try and calculate the octave change
    // caused by it.
    double dRate = 1.0 + m_pRateDir->get() * m_pRateRange->get() * m_pRateSlider->get();
    bool bKeylock = m_pKeylock->get() > 0;
    if (m_dOldRate != dRate || bKeylock != m_bOldKeylock) {
        m_dOldRate = dRate;
        m_bOldKeylock = bKeylock;
        double dFileKey = m_pFileKey->get();
        slotFileKeyChanged(dFileKey);
    }
}

void KeyControl::slotFileKeyChanged(double value) {
    mixxx::track::io::key::ChromaticKey key =
            KeyUtils::keyFromNumericValue(value);

    // The pitch adjust in octaves.
    double pitch_adjust = m_pPitch->get();
    bool keylock_enabled = m_pKeylock->get() > 0;

    // If keylock is enabled then rate only affects the tempo and not the pitch.
    if (m_dOldRate != 1.0 && !keylock_enabled) {
        pitch_adjust += KeyUtils::powerOf2ToOctaveChange(m_dOldRate);
    }

    mixxx::track::io::key::ChromaticKey adjusted =
            KeyUtils::scaleKeyOctaves(key, pitch_adjust);

    m_pEngineKey->set(KeyUtils::keyToNumericValue(adjusted));
}

void KeyControl::slotSetEngineKey(double key) {
    Q_UNUSED(key);
    // TODO(rryan): set m_pPitch to match the desired key.
}

void KeyControl::slotPitchChanged(double) {
    double dFileKey = m_pFileKey->get();
    slotFileKeyChanged(dFileKey);
}

void KeyControl::slotSyncKey(double v) {
    if (v > 0) {
        EngineBuffer* pOtherEngineBuffer = pickSyncTarget();
        syncKey(pOtherEngineBuffer);
    }
}

bool KeyControl::syncKey(EngineBuffer* pOtherEngineBuffer) {
    if (!pOtherEngineBuffer) {
        return false;
    }

    mixxx::track::io::key::ChromaticKey thisFileKey =
            KeyUtils::keyFromNumericValue(m_pFileKey->get());

    // Get the sync target's effective key, since that is what we aim to match.
    ControlObject otherKeyControl(ConfigKey(pOtherEngineBuffer->getGroup(), "key"));
    mixxx::track::io::key::ChromaticKey otherKey =
            KeyUtils::keyFromNumericValue(otherKeyControl.get());

    if (thisFileKey == mixxx::track::io::key::INVALID ||
        otherKey == mixxx::track::io::key::INVALID) {
        return false;
    }

    int stepsToTake = KeyUtils::shortestStepsToCompatibleKey(thisFileKey, otherKey);
    double newPitch = KeyUtils::stepsToOctaveChange(stepsToTake);
    // Compensate for the existing rate adjustment.
    bool keylock_enabled = m_pKeylock->get() > 0;
    if (m_dOldRate != 1.0 && !keylock_enabled) {
        newPitch -= KeyUtils::powerOf2ToOctaveChange(m_dOldRate);
    }
    m_pPitch->set(newPitch);
    return true;
}
