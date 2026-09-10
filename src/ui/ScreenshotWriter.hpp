/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "util/EventQueue.hpp"

/** Status message from worker → main. Copied by value into the SPSC ring. */
struct ScreenshotStatusMsg {
    enum Type : uint8_t { NONE = 0, DONE = 1 };
    Type type = NONE;
    char text[192]{};
};

/**
 * SPSC ring (same shape as SerialQueue): worker may only send(), main may only get().
 * Depth 4 is ample for one-pending screenshot jobs.
 */
class ScreenshotStatusQueue {
    constexpr static uint32_t queue_depth = 4;
    constexpr static uint32_t queue_mask = queue_depth - 1;

    ScreenshotStatusMsg queue[queue_depth]{};
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};

public:
    inline bool is_empty() const { return head.load(std::memory_order_acquire) == tail.load(std::memory_order_relaxed); }
    inline bool is_full() const { return ((head.load(std::memory_order_relaxed) + 1) & queue_mask) == tail.load(std::memory_order_acquire); }

    inline ScreenshotStatusMsg get() {
        if (is_empty()) {
            return ScreenshotStatusMsg{};
        }
        const auto current = tail.load(std::memory_order_relaxed);
        ScreenshotStatusMsg msg = queue[current];
        tail.store((current + 1) & queue_mask, std::memory_order_release);
        return msg;
    }

    inline bool send(const ScreenshotStatusMsg &msg) {
        if (is_full()) {
            return false;
        }
        const auto current = head.load(std::memory_order_relaxed);
        queue[current] = msg;
        head.store((current + 1) & queue_mask, std::memory_order_release);
        return true;
    }
};

class ScreenshotWriter {
    std::vector<uint8_t> buffer_;
    int width_ = 0;
    int height_ = 0;
    std::string path_;
    char display_msg_[256]{}; // main-thread only; pointed at by EventQueue OSD events
    ScreenshotStatusQueue status_q_;
    std::atomic<bool> pending_{false};
    std::atomic<bool> quit_{false};
    SDL_Thread *thread_ = nullptr;
    SDL_Semaphore *sem_ = nullptr;

    static int SDLCALL thread_entry(void *data);
    void worker_loop();

public:
    ScreenshotWriter();
    ~ScreenshotWriter();

    ScreenshotWriter(const ScreenshotWriter &) = delete;
    ScreenshotWriter &operator=(const ScreenshotWriter &) = delete;

    /** Copy/convert a surface and wake the worker. Already composed captures
     *  keep their dimensions; raw legacy captures may request scanline doubling.
     *  Returns false if a write is already pending or conversion fails.
     *  Main thread only. */
    bool try_submit(SDL_Surface *surface, const std::string &path, bool double_vertical = true);
    bool is_pending() const { return pending_.load(std::memory_order_acquire); }

    /** Drain worker status messages into EventQueue. Main thread only; never blocks. */
    void poll(EventQueue *event_queue);
};
