module;
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <android/asset_manager.h>
#include <cassert>
#include <vector>
#include <optional>
#include <string_view>
export module ce.platform.android;
import ce.platform;

export namespace ce::platform
{
class Android final : public Platform
{
    AAssetManager* m_asset_manager = nullptr;
public:
    void setup_android(android_app *pApp)
    {
        m_asset_manager = pApp->activity->assetManager;
    }
    [[nodiscard]] std::optional<std::vector<uint8_t>> read_file(
        const std::string& path) const noexcept
    {
        if (path.starts_with("assets/"))
        {
            const auto assets_path = path.substr(7);
            AAsset* asset = AAssetManager_open(
                m_asset_manager, assets_path.data(), AASSET_MODE_BUFFER);
            if (asset == nullptr)
                return std::nullopt;
            const size_t fileLength = AAsset_getLength(asset);
            std::vector<uint8_t> buffer(fileLength);
            const int bytes_read = AAsset_read(asset, buffer.data(), buffer.size());
            AAsset_close(asset);
            assert(bytes_read == fileLength && "Platform::read_file: file length mismatch");
            return buffer;
        }
        else
        {
            return Platform::read_file(path);
        }
    }
};
}
