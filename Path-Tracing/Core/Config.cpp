#include <chrono>
#include <format>

#include "Config.h"

namespace PathTracing
{

namespace
{

std::filesystem::path FindAssetsFolder()
{
    // TODO: Look for the assets in other folders and verify that they are there
    return std::filesystem::current_path().parent_path() / "assets";
}

Config::LogLevel GetLogLevel()
{
#ifdef CONFIG_LOG_LEVEL_TRACE
    return Config::LogLevel::Trace;
#endif
#ifdef CONFIG_LOG_LEVEL_DEBUG
    return Config::LogLevel::Debug;
#endif
#ifdef CONFIG_LOG_LEVEL_INFO
    return Config::LogLevel::Info;
#endif
#ifdef CONFIG_LOG_LEVEL_WARNING
    return Config::LogLevel::Warning;
#endif
    return Config::LogLevel::Error;
}

std::filesystem::path GetLogFilePath()
{
    const auto time = std::chrono::system_clock::now();
    return std::filesystem::path("logs") / std::format("Path-Tracing-{:%d-%m-%Y-%H-%M-%OS}.log", time);
}

std::string GetShaderExtensionSuffix()
{
#ifdef CONFIG_OPTIMIZE_SHADERS
#ifdef CONFIG_SHADER_DEBUG_INFO
    return "od";
#else
    return "o";
#endif
#else
#ifdef CONFIG_SHADER_DEBUG_INFO
    return "d";
#else
    return "";
#endif
#endif
}

}

Config Config::Create(int argc, char *argv[])
{
    return Config {
#ifdef CONFIG_VALIDATION_LAYERS
        .ValidationLayers = true,
#endif

#ifdef CONFIG_ASSERTS
        .Asserts = true,
#endif

        .AssetFolderPath = FindAssetsFolder(),

#ifdef CONFIG_OPTIMIZE_SCENE
        .OptimizeScene = true,
#endif

        .LoggerLevel = GetLogLevel(),

#ifdef CONFIG_LOG_TO_FILE
        .LogToFile = true,
        .LogFilePath = GetLogFilePath(),
#endif

#ifdef CONFIG_MAX_TEXTURE_LOADER_THREADS
        .MaxTextureLoaderThreads = CONFIG_MAX_TEXTURE_LOADER_THREADS,
#endif

#ifdef CONFIG_MAX_BUFFERS_PER_LOADER_THREAD
        .MaxBuffersPerLoaderThread = CONFIG_MAX_BUFFERS_PER_LOADER_THREAD,
#endif

#ifdef CONFIG_SHADER_DEBUG_INFO
        .ShaderDebugInfo = true,
#endif

#ifdef CONFIG_OPTIMIZE_SHADERS
        .OptimizeShaders = true,
#endif

#ifdef CONFIG_MAX_SHADER_INCLUDE_DEPTH
        .MaxShaderIncludeDepth = CONFIG_MAX_SHADER_INCLUDE_DEPTH,
#endif

#ifdef CONFIG_MAX_SHADER_INCLUDE_CACHE_SIZE
        .MaxShaderIncludeCacheSize = CONFIG_MAX_SHADER_INCLUDE_CACHE_SIZE,
#endif

        .ShaderCachePath = "ShaderCache",
        .ShaderSpvExtension = "spv" + GetShaderExtensionSuffix(),

#ifdef CONFIG_DISABLE_SHADER_PRECOMPILATION
        .ShaderPrecompilation = false,
#endif

#ifdef CONFIG_MAX_PIPELINE_VARIANT_CACHE_SIZE
        .MaxPipelineVariantCacheSize = CONFIG_MAX_PIPELINE_VARIANT_CACHE_SIZE,
#endif

#ifdef CONFIG_MAX_SHADER_COMPILATION_THEADS
        .MaxShaderCompilationThreads = CONFIG_MAX_SHADER_COMPILATION_THEADS,
#endif

        .ShaderCacheExtension = "shadercache" + GetShaderExtensionSuffix(),
    };
}

}