// wasapi_capture.h
#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <thread>
#include <atomic>
#include <functional>

class WASAPICapture {
public:
    using Callback = std::function<void(const float* data, size_t frames)>;

    WASAPICapture();
    ~WASAPICapture();

    bool initialize(Callback cb);
    void shutdown();

private:
    void captureLoop();

    std::thread captureThread;
    std::atomic<bool> running{ false };
    Callback userCallback;

    IMMDeviceEnumerator* deviceEnumerator = nullptr;
    IMMDevice* defaultDevice = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
};