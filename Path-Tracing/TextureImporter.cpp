#include <glm/ext/matrix_relational.hpp>
#define GLM_STATIC_ASSERT(...)
#include <gli/gli.hpp>
#include <stb_image.h>

#include <fstream>
#include <set>

#include "Core/Config.h"
#include "Core/Core.h"

#include "Application.h"
#include "TextureImporter.h"

namespace PathTracing
{

namespace
{

static inline constexpr TextureInfo::LoaderType StbiLoader = 0;
static inline constexpr TextureInfo::LoaderType GliLoader = 1;

void PremultiplyTextureData(const std::string &name, std::span<std::byte> data)
{
    // TODO: Remove when mip map generation is moved into a compute shader
    // Color channels should be premultiplied by the alpha channel between generation of every mip level
    // Doing full premultiplication only here would give wrong results
    // Therefore here we only premultiply pixels that have alpha channel of 0
    // This improves the mip maps around transparency edges and doesn't produce incorrect result

    auto pixels = SpanCast<std::byte, glm::u8vec4>(data);
    bool warned = false;

    for (auto &pixel : pixels)
    {
        if (pixel.a == 0)
        {
            pixel.r = 0;
            pixel.g = 0;
            pixel.b = 0;
        }
        else if (pixel.a != 255 && !warned)
        {
            logger::debug(
                "Texture {} has semi-transparent pixels. Generated mips may contain artifacts", name
            );
            warned = true;
        }
    }
}

TextureFormat ToTextureFormat(gli::format format)
{
    switch (format)
    {
    case gli::format::FORMAT_RGBA_DXT1_UNORM_BLOCK8:
        return TextureFormat::BC1;
    case gli::format::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
        return TextureFormat::BC3;
    case gli::format::FORMAT_RG_ATI2N_UNORM_BLOCK16:
        return TextureFormat::BC5;
    default:
        throw error("Unsupported texture format");
    }
}

// TODO: Make a fork of GLI
TextureInfo GetDDSTextureInfo(const char *Data, size_t Size)
{
    GLI_ASSERT(Data && (Size >= sizeof(gli::detail::FOURCC_DDS)));

    if (strncmp(Data, gli::detail::FOURCC_DDS, 4) != 0)
        throw error("Not a DDS texture");
    std::size_t Offset = sizeof(gli::detail::FOURCC_DDS);

    GLI_ASSERT(Size >= sizeof(gli::detail::dds_header));

    gli::detail::dds_header const &Header(*reinterpret_cast<gli::detail::dds_header const *>(Data + Offset));
    Offset += sizeof(gli::detail::dds_header);

    gli::detail::dds_header10 Header10;
    if ((Header.Format.flags & gli::dx::DDPF_FOURCC) &&
        (Header.Format.fourCC == gli::dx::D3DFMT_DX10 || Header.Format.fourCC == gli::dx::D3DFMT_GLI1))
    {
        std::memcpy(&Header10, Data + Offset, sizeof(Header10));
        Offset += sizeof(gli::detail::dds_header10);
    }

    gli::dx DX;

    gli::format Format(gli::FORMAT_UNDEFINED);
    if ((Header.Format.flags & (gli::dx::DDPF_RGB | gli::dx::DDPF_ALPHAPIXELS | gli::dx::DDPF_ALPHA |
                                gli::dx::DDPF_YUV | gli::dx::DDPF_LUMINANCE)) &&
        Format == gli::FORMAT_UNDEFINED && Header.Format.bpp > 0 && Header.Format.bpp < 64)
    {
        switch (Header.Format.bpp)
        {
        default:
            GLI_ASSERT(0);
            break;
        case 8:
        {
            if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RG4_UNORM_PACK8).Mask)))
                Format = gli::FORMAT_RG4_UNORM_PACK8;
            else if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_L8_UNORM_PACK8).Mask)))
                Format = gli::FORMAT_L8_UNORM_PACK8;
            else if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_A8_UNORM_PACK8).Mask)))
                Format = gli::FORMAT_A8_UNORM_PACK8;
            else if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_R8_UNORM_PACK8).Mask)))
                Format = gli::FORMAT_R8_UNORM_PACK8;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RG3B2_UNORM_PACK8).Mask)
                     ))
                Format = gli::FORMAT_RG3B2_UNORM_PACK8;
            else
                GLI_ASSERT(0);
            break;
        }
        case 16:
        {
            if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RGBA4_UNORM_PACK16).Mask)))
                Format = gli::FORMAT_RGBA4_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_BGRA4_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_BGRA4_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_R5G6B5_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_R5G6B5_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_B5G6R5_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_B5G6R5_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RGB5A1_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_RGB5A1_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_BGR5A1_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_BGR5A1_UNORM_PACK16;
            else if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_LA8_UNORM_PACK8).Mask)))
                Format = gli::FORMAT_LA8_UNORM_PACK8;
            else if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RG8_UNORM_PACK8).Mask)))
                Format = gli::FORMAT_RG8_UNORM_PACK8;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_L16_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_L16_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_A16_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_A16_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_R16_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_R16_UNORM_PACK16;
            else
                GLI_ASSERT(0);
            break;
        }
        case 24:
        {
            if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RGB8_UNORM_PACK8).Mask)))
                Format = gli::FORMAT_RGB8_UNORM_PACK8;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_BGR8_UNORM_PACK8).Mask)
                     ))
                Format = gli::FORMAT_BGR8_UNORM_PACK8;
            else
                GLI_ASSERT(0);
            break;
        }
        case 32:
        {
            if (glm::all(glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_BGR8_UNORM_PACK32).Mask)))
                Format = gli::FORMAT_BGR8_UNORM_PACK32;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_BGRA8_UNORM_PACK8).Mask)
                     ))
                Format = gli::FORMAT_BGRA8_UNORM_PACK8;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RGBA8_UNORM_PACK8).Mask)
                     ))
                Format = gli::FORMAT_RGBA8_UNORM_PACK8;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RGB10A2_UNORM_PACK32).Mask)
                     ))
                Format = gli::FORMAT_RGB10A2_UNORM_PACK32;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_LA16_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_LA16_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_RG16_UNORM_PACK16).Mask)
                     ))
                Format = gli::FORMAT_RG16_UNORM_PACK16;
            else if (glm::all(
                         glm::equal(Header.Format.Mask, DX.translate(gli::FORMAT_R32_SFLOAT_PACK32).Mask)
                     ))
                Format = gli::FORMAT_R32_SFLOAT_PACK32;
            else
                GLI_ASSERT(0);
            break;
        }
        }
    }
    else if ((Header.Format.flags & gli::dx::DDPF_FOURCC) && (Header.Format.fourCC != gli::dx::D3DFMT_DX10) &&
             (Header.Format.fourCC != gli::dx::D3DFMT_GLI1) && (Format == gli::FORMAT_UNDEFINED))
    {
        gli::dx::d3dfmt const FourCC = gli::detail::remap_four_cc(Header.Format.fourCC);
        Format = DX.find(FourCC);
    }
    else if (Header.Format.fourCC == gli::dx::D3DFMT_DX10 || Header.Format.fourCC == gli::dx::D3DFMT_GLI1)
        Format = DX.find(Header.Format.fourCC, Header10.Format);

    GLI_ASSERT(Format != gli::FORMAT_UNDEFINED);

    size_t const MipMapCount = (Header.Flags & gli::detail::DDSD_MIPMAPCOUNT) ? Header.MipMapLevels : 1;
    size_t FaceCount = 1;
    if (Header.CubemapFlags & gli::detail::DDSCAPS2_CUBEMAP)
        FaceCount = int(glm::bitCount(Header.CubemapFlags & gli::detail::DDSCAPS2_CUBEMAP_ALLFACES));
    else if (Header10.MiscFlag & gli::detail::D3D10_RESOURCE_MISC_TEXTURECUBE)
        FaceCount = 6;

    size_t DepthCount = 1;
    if (Header.CubemapFlags & gli::detail::DDSCAPS2_VOLUME)
        DepthCount = Header.Depth;

    return TextureInfo {
        .Format = ToTextureFormat(Format),
        .Loader = GliLoader,
        .Levels = static_cast<uint16_t>(MipMapCount),
        .Width = Header.Width,
        .Height = Header.Height,
    };
}

TextureInfo GetDDSTextureInfo(TextureSourceVariant source)
{
    if (const FileTextureSource *src = std::get_if<FileTextureSource>(&source))
    {
        std::ifstream file(*src, std::ios::binary);

        if (!file.is_open())
            throw error(std::format("DDS Texture file {} cannot be opened", src->string()));

        size_t size = sizeof(gli::detail::dds_header) + sizeof(gli::detail::dds_header10);
        std::vector<char> buffer(size);

        file.read(buffer.data(), size);
        file.close();

        return GetDDSTextureInfo(buffer.data(), buffer.size());
    }
    else
        throw error("Unhandled texture source type");
}

std::optional<TextureInfo> GetTextureInfoGli(TextureSourceVariant source, bool *hasTransparency)
{
    if (const FileTextureSource *src = std::get_if<FileTextureSource>(&source))
    {
        const auto extension = src->extension();
        if (hasTransparency)
            *hasTransparency = true;
        if (extension.string() == ".dds")
            return GetDDSTextureInfo(source);
    }

    return std::optional<TextureInfo>();
}

std::optional<TextureInfo> GetTextureInfoStbi(TextureSourceVariant source, bool *hasTransparency)
{
    int ret;
    int x, y, channels;
    bool isHdr;

    if (const FileTextureSource *src = std::get_if<FileTextureSource>(&source))
    {
        std::string path = src->string();
        ret = stbi_info(path.c_str(), &x, &y, &channels);
        isHdr = stbi_is_hdr(path.c_str());
    }
    else if (const MemoryTextureSource *src = std::get_if<MemoryTextureSource>(&source))
    {
        ret = stbi_info_from_memory(src->data(), src->size_bytes(), &x, &y, &channels);
        isHdr = stbi_is_hdr_from_memory(src->data(), src->size_bytes());
    }
    else
        throw error("Unhandled texture source type");

    if (ret == 0)
        return std::optional<TextureInfo>();

    if (hasTransparency)
        *hasTransparency = channels == 4;

    return TextureInfo {
        .Format = isHdr ? TextureFormat::RGBAF32 : TextureFormat::RGBAU8,
        .Loader = StbiLoader,
        .Levels = 1,
        .Width = static_cast<uint32_t>(x),
        .Height = static_cast<uint32_t>(y),
    };
}

TextureData LoadTextureDataGli(const TextureInfo &info)
{
    if (const FileTextureSource *src = std::get_if<FileTextureSource>(&info.Source))
    {
        const auto extension = src->extension();
        if (extension.string() == ".dds")
        {
            gli::texture texture = gli::load(src->string());
            assert(texture.faces() == 1);
            assert(texture.layers() == 1);

            assert(info.Loader == GliLoader);
            assert(info.Width == texture.extent().x);
            assert(info.Height == texture.extent().y);
            assert(info.Format == ToTextureFormat(texture.format()));
            assert(info.Levels == texture.levels());

            TextureData data(new std::byte[texture.size()], texture.size());

            size_t offset = 0;
            for (int level = 0; level < texture.levels(); level++)
            {
                const size_t size = texture.size(level);
                memcpy(data.data() + offset, texture.data(0, 0, level), size);
                offset += size;
            }

            return data;
        }
    }

    throw error(std::format("Could not load texture {}", info.Name));
}

TextureData LoadTextureDataStbi(const TextureInfo &info)
{
    int x, y, channels;
    std::byte *data;
    size_t size;

    if (const FileTextureSource *source = std::get_if<FileTextureSource>(&info.Source))
    {
        const std::string path = source->string();

        if (info.Format == TextureFormat::RGBAF32)
        {
            data = reinterpret_cast<std::byte *>(stbi_loadf(path.c_str(), &x, &y, &channels, STBI_rgb_alpha));
            size = static_cast<size_t>(x) * y * 4 * sizeof(float);
        }
        else
        {
            data = reinterpret_cast<std::byte *>(stbi_load(path.c_str(), &x, &y, &channels, STBI_rgb_alpha));
            size = static_cast<size_t>(x) * y * 4 * sizeof(uint8_t);
        }
    }
    else if (const MemoryTextureSource *source = std::get_if<MemoryTextureSource>(&info.Source))
    {
        assert(info.Format != TextureFormat::RGBAF32);

        data = reinterpret_cast<std::byte *>(
            stbi_load_from_memory(source->data(), source->size_bytes(), &x, &y, &channels, STBI_rgb_alpha)
        );
        size = static_cast<size_t>(x) * y * 4;
    }
    else
        throw error("Unhandled texture source type");

    if (data == nullptr)
        throw error(std::format("Could not load texture {}: {}", info.Name, stbi_failure_reason()));

    assert(info.Loader == StbiLoader);
    assert(info.Width == x);
    assert(info.Height == y);
    assert(info.Format == TextureFormat::RGBAU8 || info.Format == TextureFormat::RGBAF32);

    if (info.Type == TextureType::Color && channels == 4 && info.Format == TextureFormat::RGBAU8)
        PremultiplyTextureData(info.Name, std::span(data, size));

    return TextureData(data, size);
}

}

TextureInfo TextureImporter::GetTextureInfo(
    TextureSourceVariant source, TextureType type, std::string &&name, bool *hasTransparency
)
{
    std::optional<TextureInfo> info = GetTextureInfoGli(source, hasTransparency);

    if (!info.has_value())
        info = GetTextureInfoStbi(source, hasTransparency);

    if (!info.has_value())
        throw error(std::format("Could not get info for texture {}", name));

    TextureInfo &ret = info.value();
    ret.Type = type;
    ret.Name = std::move(name);
    ret.Source = std::move(source);
    return ret;
}

TextureData TextureImporter::LoadTextureData(const TextureInfo &info)
{
    switch (info.Loader)
    {
    case StbiLoader:
        return LoadTextureDataStbi(info);
    case GliLoader:
        return LoadTextureDataGli(info);
    default:
        throw error(std::format("Unknown loader texture {}", info.Loader));
    }
}

void TextureImporter::ReleaseTextureData(const TextureInfo &info, TextureData &data)
{
    assert(info.Loader == StbiLoader || info.Loader == GliLoader);

    if (info.Loader == StbiLoader)
        stbi_image_free(data.data());
    else
        delete[] data.data();
}

}
