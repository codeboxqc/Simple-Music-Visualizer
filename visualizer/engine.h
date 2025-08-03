#ifndef ENGINE_H
#define ENGINE_H

#include <SDL2/SDL.h>
#include <vector>
#include <mutex>
#include <memory> // Added for std::unique_ptr
#include <cmath>
#include "wasapi_capture.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class AudioEngine {
private:
    SDL_AudioDeviceID deviceId;
    bool initialized;
    bool simulationMode;

    int sampleRate;
    int bufferSize;

    std::vector<float> audioBuffer;
    std::vector<float> frequencyData;
    std::vector<float> smoothedFreqData;
    std::vector<float> hanningWindow;
    std::unique_ptr<WASAPICapture> wasapiCapture;

    int currentWritePos;
    mutable std::mutex bufferMutex;

    // Debug and monitoring
    Uint32 lastUpdateTime;
    float audioLevel;
    int callbackCount;

    // Static callback function for SDL
    static void audioCallback(void* userdata, Uint8* stream, int len);

    // Internal processing methods
    void processAudioInput(Uint8* stream, size_t len);
    void generateRealisticSimulation();
    void performOptimizedFFT();

public:
    AudioEngine();
    ~AudioEngine();

    bool initialize();
    void update();
    void cleanup();

    // Data access methods
    std::vector<float> getFrequencyData() const;
    float getBeat() const;
    float getAmplitude() const;
    bool isSimulationMode() const;
    float getAudioLevel() const;
};

#endif // ENGINE_H