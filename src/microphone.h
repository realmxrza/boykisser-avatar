#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <atomic>

class Microphone
{
public:
    Microphone();
    ~Microphone();

    bool Start();
    void Stop();
    void SetMuted(bool muted);
    float Level() const;

private:
    static void CALLBACK WaveProc(HWAVEIN handle, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);
    void OnData(WAVEHDR* header);

    static const int BufferCount = 4;
    static const int SamplesPerBuffer = 2048;

    HWAVEIN handle_;
    WAVEHDR headers_[BufferCount];
    short* buffers_[BufferCount];
    std::atomic<float> level_;
    std::atomic<bool> muted_;
    std::atomic<bool> running_;
};
