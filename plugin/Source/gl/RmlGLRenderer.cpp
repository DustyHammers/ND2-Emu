#include "RmlGLRenderer.h"
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Log.h>
#include "RmlUi_Renderer_GL3.h"

class RmlGLRenderer::Impl : public RenderInterface_GL3 {
public:
    explicit Impl(Decoder d) : decoder(std::move(d)) {}

    // Images come through the skin file interface (skin folder only) and the plugin's decoder (PNG, JPEG, GIF).
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
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

void RmlGLRenderer::beginFrame(int widthPx, int heightPx) {
    impl_->SetViewport(widthPx, heightPx);
    impl_->BeginFrame();
    impl_->Clear();
}

void RmlGLRenderer::endFrame() { impl_->EndFrame(); }
