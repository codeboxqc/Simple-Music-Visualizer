

 
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "engine.h"
#include "wasapi_capture.h"

#undef min
#undef max

// Global debug file
std::ofstream debugFile;

void writeDebugLog(const std::string& message) {
    if (!debugFile.is_open()) {
        debugFile.open("audio_debug.txt", std::ios::app);
    }

    // Get timestamp
    Uint32 time = SDL_GetTicks();
    debugFile << "[" << std::setfill('0') << std::setw(8) << time << "ms] " << message << std::endl;
    debugFile.flush();

    // Also print to console
    std::cout << "[DEBUG] " << message << std::endl;
}

// Static callback function for SDL audio
void AudioEngine::audioCallback(void* userdata, Uint8* stream, int len) {
    AudioEngine* engine = static_cast<AudioEngine*>(userdata);
    engine->processAudioInput(stream, len);
}

AudioEngine::AudioEngine() : deviceId(0), initialized(false), sampleRate(44100),
bufferSize(2048), currentWritePos(0) {
    audioBuffer.resize(bufferSize * 2, 0.0f);
    frequencyData.resize(64, 0.0f);
    smoothedFreqData.resize(64, 0.0f);

    // Initialize Hanning window for FFT
    hanningWindow.resize(bufferSize);
    for (int i = 0; i < bufferSize; i++) {
        hanningWindow[i] = 0.5f * (1.0f - cos(2.0f * M_PI * i / (bufferSize - 1)));
    }

    simulationMode = false;
    lastUpdateTime = 0;
    audioLevel = 0.0f;
    callbackCount = 0;

    // Clear previous debug file
    std::ofstream clearFile("audio_debug.txt", std::ios::trunc);
    clearFile.close();

    writeDebugLog("AudioEngine constructor called");
}

AudioEngine::~AudioEngine() {
    cleanup();
    if (debugFile.is_open()) {
        debugFile.close();
    }
}

bool AudioEngine::initialize() {
    writeDebugLog("=== AUDIO ENGINE INITIALIZATION START ===");
    writeDebugLog("Operating System: Windows 10 or later (WASAPI Mode)");
    writeDebugLog("Using WASAPI loopback for system audio capture");

    // Setup audio buffer and windowing
    audioBuffer.resize(bufferSize * 2, 0.0f);
    frequencyData.resize(64, 0.0f);
    smoothedFreqData.resize(64, 0.0f);

    hanningWindow.resize(bufferSize);
    for (int i = 0; i < bufferSize; i++) {
        hanningWindow[i] = 0.5f * (1.0f - cos(2.0f * M_PI * i / (bufferSize - 1)));
    }

    simulationMode = false;
    lastUpdateTime = 0;
    audioLevel = 0.0f;
    callbackCount = 0;

    // Clear previous debug file
    std::ofstream clearFile("audio_debug.txt", std::ios::trunc);
    clearFile.close();

    // Initialize WASAPI loopback capture
    wasapiCapture = std::make_unique<WASAPICapture>();
    bool success = wasapiCapture->initialize(
        [this](const float* data, size_t frames) {
            if (!data || frames == 0) return;
            // Convert to byte stream for existing processing
            const Uint8* byteStream = reinterpret_cast<const Uint8*>(data);
            size_t byteLen = frames * sizeof(float) * 2; // stereo float
            this->processAudioInput(const_cast<Uint8*>(byteStream), byteLen);
        }
    );

    if (!success) {
        writeDebugLog("WASAPI loopback initialization FAILED!");
        writeDebugLog("Falling back to simulation mode.");
        simulationMode = true;
        initialized = true;
        return true;
    }

    writeDebugLog("WASAPI loopback initialized successfully.");
    writeDebugLog("=== AUDIO ENGINE INITIALIZATION COMPLETE ===");

    initialized = true;
    return true;
}

void AudioEngine::processAudioInput(Uint8* stream, size_t len) {
    if (simulationMode) {
        writeDebugLog("ERROR: processAudioInput called in simulation mode!");
        return;
    }

    callbackCount++;

    // Log first few callbacks for debugging
    if (callbackCount <= 5) {
        writeDebugLog("Audio callback #" + std::to_string(callbackCount) +
            ", data length: " + std::to_string(len) + " bytes");
    }

    float* floatStream = reinterpret_cast<float*>(stream);
    size_t sampleCount = len / sizeof(float);

    std::lock_guard<std::mutex> lock(bufferMutex);

    // Track audio level for debugging
    float levelSum = 0.0f;
    size_t levelSamples = 0;
    float maxSample = 0.0f;

    for (size_t i = 0; i < sampleCount; i += 2) {
        // Mix stereo to mono
        float monoSample = (floatStream[i] + floatStream[i + 1]) * 0.5f;

        // Track max sample for debugging
        maxSample = std::max(maxSample, fabsf(monoSample));

        // Apply gain to make quiet audio more visible
        monoSample *= 3.0f;
        monoSample = std::max(-1.0f, std::min(1.0f, monoSample));

        audioBuffer[currentWritePos] = monoSample;
        currentWritePos = (currentWritePos + 1) % audioBuffer.size();

        // Track level
        levelSum += fabsf(monoSample);
        levelSamples++;
    }

    // Update audio level
    if (levelSamples > 0) {
        float newLevel = levelSum / levelSamples;
        audioLevel = audioLevel * 0.9f + newLevel * 0.1f;

        // Log significant audio activity
        if (callbackCount % 100 == 0) { // Every ~2 seconds at 44kHz
            writeDebugLog("Callback #" + std::to_string(callbackCount) +
                " - Level: " + std::to_string(audioLevel) +
                ", Max: " + std::to_string(maxSample));
        }
    }
}

void AudioEngine::update() {
    if (!initialized) return;

    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - lastUpdateTime < 16) return; // Limit to ~60fps
    lastUpdateTime = currentTime;

    if (simulationMode) {
        generateRealisticSimulation();
    }

    std::lock_guard<std::mutex> lock(bufferMutex);
    performOptimizedFFT();

    // Debug output every 2 seconds
    static int debugCounter = 0;
    static Uint32 lastDebugTime = 0;

    if (currentTime - lastDebugTime > 2000) { // Every 2 seconds
        lastDebugTime = currentTime;

        float totalEnergy = 0.0f;
        for (float val : frequencyData) {
            totalEnergy += val;
        }

        std::string debugMsg = "Update #" + std::to_string(debugCounter++) +
            " | Audio Level: " + std::to_string(audioLevel) +
            " | Freq Energy: " + std::to_string(totalEnergy) +
            " | Callbacks: " + std::to_string(callbackCount) +
            " | Mode: " + (simulationMode ? "SIM" : "LIVE");

        writeDebugLog(debugMsg);

        // Log first few frequency values
        if (!frequencyData.empty()) {
            std::stringstream ss;
            ss << "Frequency data: ";
            for (size_t i = 0; i < std::min(8, (int)frequencyData.size()); i++) {
                ss << std::fixed << std::setprecision(3) << frequencyData[i] << " ";
            }
            writeDebugLog(ss.str());
        }
    }
}

void AudioEngine::generateRealisticSimulation() {
    static float time = 0.0f;
    static bool loggedSim = false;

    if (!loggedSim) {
        writeDebugLog("Running in simulation mode - generating test audio");
        loggedSim = true;
    }

    time += 0.016f;

    std::lock_guard<std::mutex> lock(bufferMutex);

    // Create test signal
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        float sample = 0.0f;
        float t = time + i * 0.0001f;

        // Bass
        sample += 0.6f * sin(2.0f * M_PI * 60.0f * t);
        // Mid
        sample += 0.4f * sin(2.0f * M_PI * 440.0f * t);
        // High
        sample += 0.2f * sin(2.0f * M_PI * 2000.0f * t);

        audioBuffer[i] = sample * 0.5f;
    }

    audioLevel = 0.5f;
}

void AudioEngine::performOptimizedFFT() {
    const int numBands = 64;
    std::vector<float> magnitudes(numBands, 0.0f);

    // Simple frequency analysis
    for (int band = 0; band < numBands; band++) {
        float frequency = 20.0f * pow(2.0f, (float)band / 8.0f);
        float real = 0.0f, imag = 0.0f;

        int step = std::max(1, bufferSize / 512);
        for (int i = 0; i < bufferSize; i += step) {
            int idx = (currentWritePos - i + audioBuffer.size()) % audioBuffer.size();
            float sample = audioBuffer[idx] * hanningWindow[i];
            float angle = -2.0f * M_PI * frequency * i / sampleRate;

            real += sample * cos(angle);
            imag += sample * sin(angle);
        }

        magnitudes[band] = sqrt(real * real + imag * imag) / (bufferSize / step);
    }

    // Apply scaling and smoothing
    for (int i = 0; i < numBands; i++) {
        float logMag = log(1.0f + magnitudes[i] * 10000.0f) * 0.1f;
        float smoothFactor = 0.2f + 0.6f * (float)i / numBands;
        smoothedFreqData[i] = smoothedFreqData[i] * smoothFactor + logMag * (1.0f - smoothFactor);
        frequencyData[i] = smoothedFreqData[i];
    }

    // Normalize
    float maxVal = *std::max_element(frequencyData.begin(), frequencyData.end());
    if (maxVal > 0.001f) {
        for (float& val : frequencyData) {
            val = std::min(1.0f, val / maxVal);
        }
    }
}

std::vector<float> AudioEngine::getFrequencyData() const {
    std::lock_guard<std::mutex> lock(bufferMutex);
    return frequencyData;
}

float AudioEngine::getBeat() const {
    if (frequencyData.size() < 8) return 0.0f;
    float bassEnergy = 0.0f;
    for (int i = 0; i < 4; i++) {
        bassEnergy += frequencyData[i];
    }
    return std::min(1.0f, bassEnergy * 2.0f);
}

float AudioEngine::getAmplitude() const {
    std::lock_guard<std::mutex> lock(bufferMutex);
    return std::min(1.0f, audioLevel * 2.0f);
}

bool AudioEngine::isSimulationMode() const {
    return simulationMode;
}

float AudioEngine::getAudioLevel() const {
    return audioLevel;
}

void AudioEngine::cleanup() {
    if (initialized) {
        writeDebugLog("Cleaning up audio engine...");

        if (wasapiCapture) {
            wasapiCapture->shutdown();
            wasapiCapture.reset();
            writeDebugLog("WASAPI capture shutdown complete.");
        }

        initialized = false;

        writeDebugLog("Audio Engine cleanup complete.");
        writeDebugLog("=== DEBUG LOG END ===");
    }
}

 