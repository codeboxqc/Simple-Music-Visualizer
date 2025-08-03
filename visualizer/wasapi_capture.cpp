// wasapi_capture.cpp
#define NOMINMAX
#include <Windows.h>
#include <SDL2/SDL.h>  
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <functional>
#include <string>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <iostream>
#include <comdef.h> // For _com_error and HRESULT to string conversion
#include "wasapi_capture.h"

// Declare writeDebugLog as extern to use the implementation from engine.cpp
extern std::ofstream debugFile;
extern void writeDebugLog(const std::string& message);

WASAPICapture::WASAPICapture() : deviceEnumerator(nullptr), defaultDevice(nullptr),
audioClient(nullptr), captureClient(nullptr), running(false) {
    // Comment adjusted for clarity; original code was fine
}

WASAPICapture::~WASAPICapture() {
    shutdown();
}

bool WASAPICapture::initialize(Callback cb) {
    userCallback = cb;
    running = true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        _com_error err(hr);
        writeDebugLog("CoInitializeEx failed: " + std::string(err.ErrorMessage()));
        return false;
    }

    // Initialize WASAPI
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
    if (FAILED(hr)) {
        _com_error err(hr);
        writeDebugLog("CoCreateInstance failed: " + std::string(err.ErrorMessage()));
        CoUninitialize(); // Uninitialize COM if this step fails
        return false;
    }

    // Get default audio endpoint (eRender for loopback of playback, eCapture for mic)
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr)) {
        _com_error err(hr);
        writeDebugLog("GetDefaultAudioEndpoint failed: " + std::string(err.ErrorMessage()));
        deviceEnumerator->Release();
        deviceEnumerator = nullptr; // Set to nullptr after releasing
        CoUninitialize();
        return false;
    }

    // Activate audio client
    hr = defaultDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr)) {
        _com_error err(hr);
        writeDebugLog("Device Activate failed: " + std::string(err.ErrorMessage()));
        defaultDevice->Release();
        defaultDevice = nullptr; // Set to nullptr after releasing
        deviceEnumerator->Release();
        deviceEnumerator = nullptr; // Set to nullptr after releasing
        CoUninitialize();
        return false;
    }

    // Get mix format
    WAVEFORMATEX* format;
    hr = audioClient->GetMixFormat(&format);
    if (FAILED(hr)) {
        _com_error err(hr);
        writeDebugLog("GetMixFormat failed: " + std::string(err.ErrorMessage()));
        audioClient->Release();
        audioClient = nullptr; // Set to nullptr after releasing
        defaultDevice->Release();
        defaultDevice = nullptr; // Set to nullptr after releasing
        deviceEnumerator->Release();
        deviceEnumerator = nullptr; // Set to nullptr after releasing
        CoUninitialize();
        return false;
    }

    // Initialize audio client for loopback
    // AUDCLNT_STREAMFLAGS_LOOPBACK is key for capturing output
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, format, nullptr);
    CoTaskMemFree(format); // Free the format allocated by GetMixFormat
    if (FAILED(hr)) {
        _com_error err(hr);
        writeDebugLog("AudioClient Initialize failed: " + std::string(err.ErrorMessage()));
        audioClient->Release();
        audioClient = nullptr; // Set to nullptr after releasing
        defaultDevice->Release();
        defaultDevice = nullptr; // Set to nullptr after releasing
        deviceEnumerator->Release();
        deviceEnumerator = nullptr; // Set to nullptr after releasing
        CoUninitialize();
        return false;
    }

    // Get capture client
    hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
    if (FAILED(hr)) {
        _com_error err(hr);
        writeDebugLog("GetService failed: " + std::string(err.ErrorMessage()));
        audioClient->Release();
        audioClient = nullptr; // Set to nullptr after releasing
        defaultDevice->Release();
        defaultDevice = nullptr; // Set to nullptr after releasing
        deviceEnumerator->Release();
        deviceEnumerator = nullptr; // Set to nullptr after releasing
        CoUninitialize();
        return false;
    }

    // Start capturing
    hr = audioClient->Start();
    if (FAILED(hr)) {
        _com_error err(hr);
        writeDebugLog("AudioClient Start failed: " + std::string(err.ErrorMessage()));
        captureClient->Release();
        captureClient = nullptr; // Set to nullptr after releasing
        audioClient->Release();
        audioClient = nullptr; // Set to nullptr after releasing
        defaultDevice->Release();
        defaultDevice = nullptr; // Set to nullptr after releasing
        deviceEnumerator->Release();
        deviceEnumerator = nullptr; // Set to nullptr after releasing
        CoUninitialize();
        return false;
    }

    // Start capture thread
    captureThread = std::thread(&WASAPICapture::captureLoop, this);
    writeDebugLog("WASAPI capture initialized successfully");
    return true;
}

void WASAPICapture::shutdown() {
    running = false; // Signal the captureLoop to stop
    if (captureThread.joinable()) {
        captureThread.join(); // Wait for the capture thread to finish
    }

    // Stop and release COM interfaces in reverse order of acquisition
    if (audioClient) {
        audioClient->Stop(); // Stop the audio client before releasing
        audioClient->Release();
        audioClient = nullptr;
    }
    if (captureClient) {
        captureClient->Release();
        captureClient = nullptr;
    }
    if (defaultDevice) {
        defaultDevice->Release();
        defaultDevice = nullptr;
    }
    if (deviceEnumerator) {
        deviceEnumerator->Release();
        deviceEnumerator = nullptr;
    }
    CoUninitialize(); // Uninitialize COM
    writeDebugLog("WASAPI capture shutdown");
}

void WASAPICapture::captureLoop() {
    while (running) {
        UINT32 packetLength = 0;
        HRESULT hr = captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            _com_error err(hr);
            writeDebugLog("GetNextPacketSize failed in captureLoop: " + std::string(err.ErrorMessage()));
            // Consider more robust error handling, perhaps set running = false;
            break; // Exit loop on critical error
        }

        while (packetLength != 0) {
            BYTE* data;
            UINT32 numFramesAvailable;
            DWORD flags;
            // Get the captured data buffer
            hr = captureClient->GetBuffer(&data, &numFramesAvailable, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                _com_error err(hr);
                writeDebugLog("GetBuffer failed in captureLoop: " + std::string(err.ErrorMessage()));
                break; // Exit inner loop on critical error
            }

            // Check for silent buffer (optional, but good for performance if not processing silence)
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                // Call the user-provided callback with the audio data
                // Assuming the audio format is float as per reinterpret_cast
                userCallback(reinterpret_cast<float*>(data), numFramesAvailable);
            }

            // Release the buffer back to WASAPI
            hr = captureClient->ReleaseBuffer(numFramesAvailable);
            if (FAILED(hr)) {
                _com_error err(hr);
                writeDebugLog("ReleaseBuffer failed in captureLoop: " + std::string(err.ErrorMessage()));
                break; // Exit inner loop on critical error
            }

            // Check for the next packet size to continue processing available data
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                _com_error err(hr);
                writeDebugLog("GetNextPacketSize after ReleaseBuffer failed in captureLoop: " + std::string(err.ErrorMessage()));
                break; // Exit inner loop on critical error
            }
        }

        Sleep(10); // Prevent tight looping, give CPU a break if no data is available
    }
    writeDebugLog("WASAPI capture loop exited."); // Log when the loop finishes
}