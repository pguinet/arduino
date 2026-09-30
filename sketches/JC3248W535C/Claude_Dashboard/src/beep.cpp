#include "beep.h"
#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>

#define I2S_BCK   42
#define I2S_LRCK  2
#define I2S_DOUT  41
#define RATE      16000
#define EDGE_SAMPLES (RATE / 200)   // enveloppe lineaire de 5 ms (pas de clic)
#define TAIL_MS   100               // > tampon DMA (6 x 240 trames = 90 ms) : tout est joue avant la coupure
#define FRAMES    256

static I2SClass i2s;
static bool ready = false;

// Le canal est cree une fois, puis active seulement pendant un bip : canal
// desactive = plus d'horloge, le NS4168 se met en veille (pas de souffle).
// i2s.end() a chaque bip journaliserait "perimanSetPinBus(): Invalid pin: 255"
// (ESP_I2S 3.0.7 libere MCLK meme non utilise).
bool beep_begin()
{
    i2s.setPins(I2S_BCK, I2S_LRCK, I2S_DOUT);
    // Stereo, echantillon duplique : le NS4168 lit le canal choisi par sa broche CTRL.
    ready = i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO) &&
            i2s_channel_disable(i2s.txChan()) == ESP_OK;
    if (!ready) Serial.println("I2S : echec init, bips desactives");
    return ready;
}

static void writeNote(uint16_t freqHz, uint16_t durationMs, int16_t amp)
{
    static int16_t buf[FRAMES * 2];
    const uint32_t total = (uint32_t)RATE * durationMs / 1000;
    const float step = 2.0f * (float)M_PI * freqHz / RATE;
    uint32_t n = 0;
    while (n < total) {
        size_t f = 0;
        for (; f < FRAMES && n < total; f++, n++) {
            int16_t v = 0;
            if (freqHz) {
                uint32_t edge = n < total - n ? n : total - n;
                float env = edge < EDGE_SAMPLES ? (float)edge / EDGE_SAMPLES : 1.0f;
                v = (int16_t)(amp * env * sinf(step * (float)n));
            }
            buf[2 * f] = v;
            buf[2 * f + 1] = v;
        }
        i2s.write(reinterpret_cast<uint8_t *>(buf), f * 2 * sizeof(int16_t));
    }
}

void beep_play(const BeepNote *notes, size_t count, uint8_t volume)
{
    if (!ready || count == 0) return;
    if (volume > 100) volume = 100;
    if (i2s_channel_enable(i2s.txChan()) != ESP_OK) return;
    const int16_t amp = (int16_t)(32767L * volume / 100);
    for (size_t i = 0; i < count; i++) writeNote(notes[i].freqHz, notes[i].durationMs, amp);
    writeNote(0, TAIL_MS, 0);  // la queue remplit le DMA : rien d'audible ne reste en file
    i2s_channel_disable(i2s.txChan());
}
