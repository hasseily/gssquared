#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <functional>
#include <vector>

// Host GPU resources are disposable; guest RAM/card state is not. Owners
// register only their texture/cache release and rebuild operations here.
// The application calls both phases at a frame boundary around renderer reset.
class RendererResource {
    SDL_Renderer* renderer_ = nullptr;
    std::function<void()> release_;
    std::function<void(SDL_Renderer*)> restore_;
    static std::vector<RendererResource*>& owners() {
        static std::vector<RendererResource*> list;
        return list;
    }
public:
    RendererResource() = default;
    RendererResource(const RendererResource&) = delete;
    RendererResource& operator=(const RendererResource&) = delete;
    ~RendererResource() { unregister(); }
    void register_owner(SDL_Renderer* renderer, std::function<void()> release,
                        std::function<void(SDL_Renderer*)> restore) {
        unregister();
        renderer_ = renderer;
        release_ = std::move(release);
        restore_ = std::move(restore);
        owners().push_back(this);
    }
    void unregister() {
        auto& list = owners();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
        renderer_ = nullptr;
    }
    static void release_all(SDL_Renderer* renderer) {
        // Callbacks must not destroy other registered owners mid-iteration.
        for (auto* owner : owners()) if (owner->renderer_ == renderer) owner->release_();
    }
    static void restore_all(SDL_Renderer* previous, SDL_Renderer* replacement) {
        for (auto* owner : owners()) if (owner->renderer_ == previous) {
            owner->renderer_ = replacement;
            owner->restore_(replacement);
        }
    }
};
