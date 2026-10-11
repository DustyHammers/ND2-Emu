#include "RmlGLRenderer.h"
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Log.h>
#include "RmlUi_Renderer_GL3.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

namespace {
std::string key(const std::string& path) { return std::filesystem::u8path(path).lexically_normal().u8string(); }
struct Decoded { std::vector<uint8_t> rgba; int w = 0, h = 0; };
}  // namespace

class RmlGLRenderer::Impl : public RenderInterface_GL3 {
public:
    explicit Impl(Decoder d) : decoder(std::move(d)) {}

    // Images come through the skin file interface (skin folder only) and the plugin's decoder (PNG, JPEG, GIF).
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        if (auto it = prefetched.find(key(source)); it != prefetched.end()) {
            Decoded d = std::move(it->second);
            prefetched.erase(it);
            dimensions = {d.w, d.h};
            return GenerateTexture({d.rgba.data(), d.rgba.size()}, dimensions);
        }
        Rml::String data;
        if (!Rml::GetFileInterface()->LoadFile(source, data) || data.empty()) {
            Rml::Log::Message(Rml::Log::LT_WARNING, "Image not found: %s", source.c_str());
            return {};
        }
        std::vector<uint8_t> file(data.begin(), data.end()), rgba;
        int w = 0, h = 0;
        if (!decoder || !decoder(file, rgba, w, h) || w <= 0 || h <= 0) {
            Rml::Log::Message(Rml::Log::LT_WARNING, "Image could not be decoded: %s", source.c_str());
            return {};
        }
        dimensions = {w, h};
        return GenerateTexture({rgba.data(), rgba.size()}, dimensions);
    }

    Decoder decoder;
    std::map<std::string, Decoded> prefetched;
};

bool RmlGLRenderer::loadGL(std::string& message) {
    static bool loaded = false;
    if (loaded) return true;
    Rml::String m;
    loaded = RmlGL3::Initialize(&m);
    message = m;
    return loaded;
}

RmlGLRenderer::RmlGLRenderer(Decoder decoder) : impl_(std::make_unique<Impl>(std::move(decoder))) {}
RmlGLRenderer::~RmlGLRenderer() = default;
bool RmlGLRenderer::ok() const { return bool(*impl_); }
Rml::RenderInterface* RmlGLRenderer::rml() { return impl_.get(); }

void RmlGLRenderer::prefetch(const std::vector<std::string>& paths) {
    std::vector<Decoded> out(paths.size());
    std::atomic<size_t> next{0};
    auto work = [&] {
        for (size_t i; (i = next.fetch_add(1)) < paths.size();) {
            std::ifstream in(std::filesystem::u8path(paths[i]), std::ios::binary);
            std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), {});
            Decoded& d = out[i];
            if (file.empty() || !impl_->decoder || !impl_->decoder(file, d.rgba, d.w, d.h) || d.w <= 0 || d.h <= 0) d = {};
        }
    };
    const size_t n = std::min<size_t>(paths.size(), std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (size_t t = 1; t < n; ++t) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
    for (size_t i = 0; i < paths.size(); ++i)
        if (!out[i].rgba.empty()) impl_->prefetched[key(paths[i])] = std::move(out[i]);
}

void RmlGLRenderer::dropPrefetched() { impl_->prefetched.clear(); }

void RmlGLRenderer::beginFrame(int widthPx, int heightPx) {
    impl_->SetViewport(widthPx, heightPx);
    impl_->BeginFrame();
    impl_->Clear();
}

void RmlGLRenderer::endFrame() { impl_->EndFrame(); }
