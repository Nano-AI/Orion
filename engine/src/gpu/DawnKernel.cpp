// The Dawn backend's libraries and kernels (#375).
//
// A library is one prepared .wgsl (engine/shaders/wgsl_prep.py): WGSL plus a
// header saying how the Metal-shaped host call maps onto it. WGSL fixes each
// storage texture's format in the shader, so the shader keeps one compute
// pipeline per combination of formats it has been dispatched with, made on
// first use and shared by every kernel made from that library.

#include "gpu/DawnKernel.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace orion::gpu::dawn {
namespace {

std::string wgslPath(std::string path) {
    // Callers name the .metallib on both backends; see Resources.h.
    const std::string metal = ".metallib";
    if (path.size() > metal.size() &&
        path.compare(path.size() - metal.size(), metal.size(), metal) == 0) {
        path.replace(path.size() - metal.size(), metal.size(), ".wgsl");
    }
    return path;
}

void parseHeader(Shader& s) {
    std::istringstream in(s.source);
    std::string line;
    while (std::getline(in, line) && line.rfind("// orion-", 0) == 0) {
        std::istringstream f(line.substr(9));
        std::string key, access;
        f >> key;
        if (key == "texture") {
            Shader::Binding b;
            f >> b.slot >> b.binding;
            s.textures.push_back(b);
        } else if (key == "storage") {
            Shader::Storage st;
            f >> st.binding >> st.slangFormat >> access;
            s.storage.push_back(st);
        } else if (key == "params") {
            f >> s.paramBinding >> s.paramBytes >> s.hostBytes;
            s.hasParams = true;
        } else if (key == "copy") {
            Shader::Copy c;
            f >> c.to >> c.from >> c.bytes;
            s.copies.push_back(c);
        }
        if (f.fail()) throw std::runtime_error("bad WGSL header line in " + s.path + ": " + line);
    }
}

}  // namespace

const Shader::Variant& Shader::pipeline(const std::vector<PixelFormat>& formats) {
    std::lock_guard<std::mutex> hold(lock_);
    for (const auto& v : variants_) {
        if (v.formats == formats) return v;
    }

    std::string code = source;
    for (std::size_t i = 0; i < storage.size(); ++i) {
        const std::string token = "ORION_FMT_" + std::to_string(storage[i].binding);
        const auto at = code.find(token);
        if (at == std::string::npos) throw std::runtime_error("no " + token + " in " + path);
        code.replace(at, token.size(), wgslFormat(formats[i]));
    }

    ErrorScope scope(*ctx);
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = code.c_str();
    wgpu::ShaderModuleDescriptor sd{.nextInChain = &wgsl};
    wgpu::ShaderModule module = ctx->device.CreateShaderModule(&sd);
    wgpu::ComputePipelineDescriptor pd{.compute = {.module = module, .entryPoint = entry.c_str()}};
    wgpu::ComputePipeline pipe = ctx->device.CreateComputePipeline(&pd);
    scope.check("Dawn pipeline for '" + entry + "'");
    variants_.push_back(Variant{formats, pipe, pipe.GetBindGroupLayout(0)});
    return variants_.back();
}

std::unique_ptr<Library::Impl> makeLibrary(std::shared_ptr<Context> ctx, const std::string& path) {
    const std::string file = wgslPath(path);
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("could not load WGSL at " + file);
    std::ostringstream all;
    all << in.rdbuf();

    auto lib = std::make_unique<DawnLibrary>();
    lib->ctx = ctx;
    // ⚠ **One Shader per file per device, kept.** Every graph built (a photo
    // opens two; a pan past the region bucket builds a third) would otherwise
    // compile each of its ~48 shaders again — measured as a 100% pan at 134 ms
    // against Metal's 82, whose driver keeps compiled pipelines for us. Keyed
    // by path, and checked against the text, so a shader rebuilt on disk under
    // a running process is compiled afresh rather than served stale.
    std::lock_guard<std::mutex> hold(ctx->shaderLock);
    auto& slot = ctx->shaders[file];
    auto cached = std::static_pointer_cast<Shader>(slot);
    if (cached && cached->source == all.str()) {
        lib->shader = std::move(cached);
        return lib;
    }
    auto s = std::make_shared<Shader>();
    s->ctx    = ctx.get();
    s->path   = file;
    s->source = all.str();
    // One entry point per file, named after it (engine/shaders/CMakeLists.txt).
    // Set here, under the lock, so two threads making kernels never race on it.
    s->entry  = std::filesystem::path(file).stem().string();
    parseHeader(*s);
    slot = s;
    lib->shader = std::move(s);
    return lib;
}

std::unique_ptr<Kernel::Impl> makeKernel(Library& library, const std::string& entryPoint) {
    auto* lib = dynamic_cast<DawnLibrary*>(&library.impl());
    if (!lib) throw std::runtime_error("a Metal library was given to a Dawn device");
    Shader& s = *lib->shader;
    if (s.entry != entryPoint) {
        throw std::runtime_error("no kernel named '" + entryPoint + "' in " + s.path);
    }

    auto k = std::make_unique<DawnKernel>();
    k->ctx        = lib->ctx;
    k->shader     = lib->shader;
    k->execWidth  = 32;
    k->maxThreads = 256;   // [numthreads(16,16,1)]; WebGPU's default ceiling
    for (const auto& b : s.textures) k->textureSlots = std::max(k->textureSlots, b.slot + 1);

    // Compiled now for the likeliest formats, so a shader Tint rejects fails
    // at graph compile, as a Metal one does. Slang's guess is 32-bit; the
    // working format is RGBA16F.
    std::vector<PixelFormat> guess;
    for (const auto& st : s.storage) {
        guess.push_back(st.slangFormat == "r32float"  ? PixelFormat::R32Float
                      : st.slangFormat == "rg32float" ? PixelFormat::RG32Float
                                                      : PixelFormat::RGBA16Float);
    }
    s.pipeline(guess);
    return k;
}

}  // namespace orion::gpu::dawn
