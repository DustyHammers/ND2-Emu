// The skin renderer: RmlUi's own OpenGL 3 renderer (from the RmlUi package), with images decoded by the plugin.
// Kept apart from JUCE: this file and RmlGLRenderer.cpp see RmlUi's GL loader, the plugin code sees JUCE's.
// All calls on the OpenGL thread with the context current.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Rml { class RenderInterface; }

class RmlGLRenderer {
public:
    // Decodes an image file into premultiplied RGBA, top row first.
    using Decoder = std::function<bool(const std::vector<uint8_t>& file, std::vector<uint8_t>& rgba, int& width, int& height)>;

    static bool loadGL(std::string& message);        // loads the GL functions (once per process)
    explicit RmlGLRenderer(Decoder decoder);
    ~RmlGLRenderer();
    bool ok() const;
    Rml::RenderInterface* rml();
    void beginFrame(int widthPx, int heightPx);
    void endFrame();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
