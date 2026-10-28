#pragma once

namespace gui {
    // On Android the volume key acts as a PTT key. It must only capture the
    // volume key when transmission is actually possible: a compatible
    // transmitter (Hermes) is available, the transceiver layout is active and
    // the QSO panel is shown. Otherwise Android must handle the key as volume.
    inline bool volumeKeyActsAsPtt(bool transmitterAvailable, bool transceiverLayoutActive, bool qsoPanelShown) {
        return transmitterAvailable && transceiverLayoutActive && qsoPanelShown;
    }
}