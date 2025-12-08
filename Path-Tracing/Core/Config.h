#pragma once

#include <cstdint>
#include <filesystem>

#include "Core/Core.h"

namespace PathTracing
{

#ifdef CONFIG_RELEASE

#define CONFIG_OPTIMIZE_SHADERS
#define CONFIG_LOG_LEVEL_INFO
#define CONFIG_OPTIMIZE_SCENE

#ifndef CONFIG_MAX_SHADER_COMPILATION_BATCH_SIZE
#define CONFIG_MAX_SHADER_COMPILATION_BATCH_SIZE 128
#endif

#endif

#ifdef CONFIG_PROFILE

#define CONFIG_OPTIMIZE_SHADERS
#define CONFIG_SHADER_DEBUG_INFO
#define CONFIG_LOG_LEVEL_INFO
#define CONFIG_OPTIMIZE_SCENE

#ifndef CONFIG_MAX_SHADER_COMPILATION_BATCH_SIZE
#define CONFIG_MAX_SHADER_COMPILATION_BATCH_SIZE 128
#endif

#endif

#ifdef CONFIG_DEBUG

#define CONFIG_VALIDATION_LAYERS
#define CONFIG_ASSERTS
#define CONFIG_SHADER_DEBUG_INFO
#define CONFIG_LOG_LEVEL_DEBUG

#ifndef CONFIG_MAX_TEXTURE_LOADER_THREADS
#define CONFIG_MAX_TEXTURE_LOADER_THREADS 2
#endif

#ifndef CONFIG_MAX_BUFFERS_PER_LOADER_THREAD
#define CONFIG_MAX_BUFFERS_PER_LOADER_THREAD 1
#endif

#ifndef CONFIG_MAX_SHADER_COMPILATION_THEADS
#define CONFIG_MAX_SHADER_COMPILATION_THEADS 2
#endif

#ifndef CONFIG_MAX_SHADER_COMPILATION_BATCH_SIZE
#define CONFIG_MAX_SHADER_COMPILATION_BATCH_SIZE 16
#endif

#ifndef CONFIG_MAX_TEXTURE_MEMORY_BUDGET_ABSOLUTE_MIB
#define CONFIG_MAX_TEXTURE_MEMORY_BUDGET_ABSOLUTE_MIB 1024
#endif

#endif

#ifdef CONFIG_TRACE

#define CONFIG_VALIDATION_LAYERS
#define CONFIG_ASSERTS
#define CONFIG_SHADER_DEBUG_INFO
#define CONFIG_LOG_LEVEL_TRACE
#define CONFIG_LOG_TO_FILE

#ifndef CONFIG_MAX_TEXTURE_LOADER_THREADS
#define CONFIG_MAX_TEXTURE_LOADER_THREADS 2
#endif

#ifndef CONFIG_MAX_BUFFERS_PER_LOADER_THREAD
#define CONFIG_MAX_BUFFERS_PER_LOADER_THREAD 1
#endif

#ifndef CONFIG_MAX_SHADER_COMPILATION_THEADS
#define CONFIG_MAX_SHADER_COMPILATION_THEADS 2
#endif

#ifndef CONFIG_MAX_SHADER_COMPILATION_BATCH_SIZE
#define CONFIG_MAX_SHADER_COMPILATION_BATCH_SIZE 16
#endif

#ifndef CONFIG_MAX_TEXTURE_MEMORY_BUDGET_VRAM_ABSOLUTE_MIB
#define CONFIG_MAX_TEXTURE_MEMORY_BUDGET_VRAM_ABSOLUTE_MIB 1024
#endif

#endif

#ifndef CONFIG_ASSERTS
    #ifndef NDEBUG
        #define NDEBUG
    #endif
#endif

#ifndef NDEBUG
    #ifndef CONFIG_ASSERTS
        #define CONFIG_ASSERTS
    #endif
#endif

struct Config
{
    [[nodiscard]] static Config Create(int argc, const char *argv[]);

    enum class LogLevel : uint8_t
    {
        Trace,
        Debug,
        Info,
        Warning,
        Error,
    };

    bool ValidationLayers = false;
    bool Asserts = false;

    std::filesystem::path ConfigDirectoryPath;

    std::filesystem::path AssetDirectoryPath;
    bool OptimizeScene = false;

    LogLevel LoggerLevel = LogLevel::Error;
    bool LogToFile = false;
    std::filesystem::path LogFilePath;

    uint32_t MaxTextureLoaderThreads = std::numeric_limits<uint32_t>::max();
    uint32_t MaxBuffersPerLoaderThread = std::numeric_limits<uint32_t>::max();

    std::filesystem::path ShaderDirectoryPath;
    bool ShaderDebugInfo = false;
    bool OptimizeShaders = false;
    uint32_t MaxShaderIncludeDepth = 5;
    uint32_t MaxShaderIncludeCacheSize = 10;
    std::filesystem::path ShaderCachePath;
    std::filesystem::path ShaderSpvExtension;
  
    bool ShaderPrecompilation = true;
    uint32_t MaxPipelineVariantCacheSize = 1000;
    uint32_t MaxShaderCompilationThreads = std::numeric_limits<uint32_t>::max();
    uint32_t MaxShaderCompilationBatchSize = std::numeric_limits<uint32_t>::max();
    std::filesystem::path ShaderCacheExtension;

    uint64_t MaxStagingBufferSize = 64_MiB;
    uint64_t MaxTextureMemoryBudgetAbsolute = std::numeric_limits<uint64_t>::max();
    uint32_t MaxTextureMemoryBudgetVramPercent = 80;
};

class PrintHelpException : public std::exception
{
};

}
