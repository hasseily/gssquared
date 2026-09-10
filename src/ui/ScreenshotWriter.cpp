/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#include "ScreenshotWriter.hpp"
#include "CapturePixels.hpp"

#include <cstdio>
#include <cstring>

#include <SDL3_image/SDL_image.h>

#include "devices/displaypp/RGBA.hpp"
#include "util/Event.hpp"

int SDLCALL ScreenshotWriter::thread_entry(void *data) {
    static_cast<ScreenshotWriter *>(data)->worker_loop();
    return 0;
}

ScreenshotWriter::ScreenshotWriter() {
    sem_ = SDL_CreateSemaphore(0);
    thread_ = SDL_CreateThread(thread_entry, "gs2-screenshot", this);
    if (!thread_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ScreenshotWriter: SDL_CreateThread failed: %s", SDL_GetError());
    }
}

ScreenshotWriter::~ScreenshotWriter() {
    quit_.store(true, std::memory_order_release);
    if (sem_) {
        SDL_SignalSemaphore(sem_);
    }
    if (thread_) {
        SDL_WaitThread(thread_, nullptr);
        thread_ = nullptr;
    }
    if (sem_) {
        SDL_DestroySemaphore(sem_);
        sem_ = nullptr;
    }
}

bool ScreenshotWriter::try_submit(SDL_Surface *surface, const std::string &path, bool double_vertical) {
    if (!surface || !sem_ || !thread_) {
        return false;
    }

    bool expected = false;
    if (!pending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    if (!capture_rgba(surface, double_vertical, buffer_, width_, height_)) {
        pending_.store(false, std::memory_order_release);
        return false;
    }

    path_ = path;
    SDL_SignalSemaphore(sem_);
    return true;
}

void ScreenshotWriter::poll(EventQueue *event_queue) {
    if (!event_queue) {
        return;
    }
    // At most one per frame: EventQueue stores a pointer into display_msg_, and
    // frame_appevent consumes a single event per call.
    ScreenshotStatusMsg msg = status_q_.get();
    if (msg.type == ScreenshotStatusMsg::NONE) {
        return;
    }
    std::snprintf(display_msg_, sizeof(display_msg_), "%s", msg.text);
    event_queue->addEvent(new Event(EVENT_SHOW_MESSAGE, 0, display_msg_));
}

void ScreenshotWriter::worker_loop() {
    while (true) {
        SDL_WaitSemaphore(sem_);
        if (quit_.load(std::memory_order_acquire) &&
            !pending_.load(std::memory_order_acquire)) break;

        SDL_Surface *surf = SDL_CreateSurfaceFrom(
            width_, height_, SDL_PIXELFORMAT_RGBA32, buffer_.data(), width_ * 4);
        bool ok = false;
        if (surf) {
            ok = IMG_SavePNG(surf, path_.c_str());
            SDL_DestroySurface(surf);
        }

        ScreenshotStatusMsg msg;
        msg.type = ScreenshotStatusMsg::DONE;
        if (ok) {
            const char *base = path_.c_str();
            const char *slash = std::strrchr(base, '/');
#ifdef _WIN32
            const char *bslash = std::strrchr(base, '\\');
            if (bslash && (!slash || bslash > slash)) {
                slash = bslash;
            }
#endif
            if (slash && slash[1]) {
                base = slash + 1;
            }
            std::snprintf(msg.text, sizeof(msg.text), "Saved %s", base);
        } else {
            std::snprintf(msg.text, sizeof(msg.text), "Screenshot save failed");
        }
        // Worker → main: SPSC ring only. Never touch EventQueue here.
        status_q_.send(msg);
        pending_.store(false, std::memory_order_release);
        if (quit_.load(std::memory_order_acquire)) break;
    }
}
