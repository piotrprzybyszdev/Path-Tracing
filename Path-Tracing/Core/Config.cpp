#include <array>
#include <chrono>
#include <format>
#include <iostream>
#include <ranges>

#include "Config.h"
#include "Core.h"

namespace PathTracing
{

namespace
{

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

void PrintHelp()
{
    std::cout << "Path-Tracing - Photorealistic 3D scene rendering in Vulkan" << std::endl;
    std::cout << "Piotr Przybysz, Michal Popkowicz - Faculty of Mathematics and Information Science, Warsaw "
                 "University of Technology, 2025"
              << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "    Optional arguments:" << std::endl;
    std::cout << "        [-h, -H, --help] - Display this message" << std::endl;
    std::cout << "        [-A, --assets]   - Specify asset directory" << std::endl;
    std::cout << "        [-S, --shaders]  - Specify shader directory" << std::endl;

    throw PrintHelpException();
}

std::string_view getArgument(std::span<std::string_view> cmd, std::initializer_list<std::string_view> options)
{
    for (auto option : options)
    {
        auto it = std::find(cmd.begin(), cmd.end(), option);

        if (it == cmd.end())
            continue;

        if (++it != cmd.end())
            return *it;

        std::cout << std::format("USAGE ERROR: Option {} requires an argument", option) << std::endl << std::endl;
        PrintHelp();
    }

    return "";
}

bool getFlag(std::span<std::string_view> cmd, std::initializer_list<std::string_view> options)
{
    for (auto option : options)
        if (std::find(cmd.begin(), cmd.end(), option) != cmd.end())
            return true;
    return false;
}

std::filesystem::path FindFolder(std::string_view name)
{
    std::array<std::filesystem::path, 2> searchPaths = {
        std::filesystem::current_path(),
        std::filesystem::current_path() / "Path-Tracing",
    };

    for (std::filesystem::path path : searchPaths)
    {
        for (int i = 0; i < 3; i++)
        {
            if (std::filesystem::is_directory(path / name))
                return path / name;
            path = path.parent_path();
        }
    }

    std::cout << "ERROR: "<< name << " directory could not be found" << std::endl << std::endl;
    PrintHelp();
    return "";
}

std::filesystem::path GetDirectory(
    std::span<std::string_view> cmd, std::initializer_list<std::string_view> options, std::string_view folder
)
{
    auto argument = getArgument(cmd, options);
    if (!argument.empty())
        return std::filesystem::absolute(argument);

    return FindFolder(folder);
}

}

Config Config::Create(int argc, char *argv[])
{
    std::vector<std::string_view> cmd(argc);
    for (int i = 0; i < argc; i++)
        cmd.push_back(argv[i]);

    if (getFlag(cmd, { "-h", "-H", "--help" }))
        PrintHelp();

    const auto assetDirectory = GetDirectory(cmd, { "-A", "--assets" }, "assets");
    const auto shaderDirectory = GetDirectory(cmd, { "-S", "--shaders" }, "Shaders");

    return Config {
#ifdef CONFIG_VALIDATION_LAYERS
        .ValidationLayers = true,
#endif

#ifdef CONFIG_ASSERTS
        .Asserts = true,
#endif

        .AssetDirectoryPath = assetDirectory,

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

        .ShaderDirectoryPath = shaderDirectory,

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

        .ShaderCachePath = shaderDirectory.parent_path() / "ShaderCache",
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
