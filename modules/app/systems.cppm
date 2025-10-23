module;
#include <memory>
export module ce.app:systems;
import :physics;
import :audio;
import :server;
import :client;

export namespace ce::app::systems
{
struct SystemsCreateInfo
{
    bool xr_mode = false;
    bool server_mode = false;
};
std::shared_ptr<physics::PhysicsSystem> m_physics_system;
std::shared_ptr<audio::AudioSystem> m_audio_system;
std::shared_ptr<server::ServerSystem> m_server_system;
std::shared_ptr<client::ClientSystem> m_client_system;
bool create_systems(const SystemsCreateInfo& info) noexcept
{
    m_physics_system = std::make_shared<physics::PhysicsSystem>();
    m_physics_system->create_system();
    m_physics_system->create_shared_box();
    m_audio_system = std::make_shared<audio::AudioSystem>();
    m_audio_system->create_system();
    if (info.server_mode)
    {
        m_server_system = std::make_shared<server::ServerSystem>();
        m_server_system->create_system(globals::m_resources, m_physics_system);
    }
    else
    {
        m_client_system = std::make_shared<client::ClientSystem>();
        m_client_system->create_system();
    }
    return true;
}
void destroy_systems() noexcept
{
    if (m_physics_system) m_physics_system->destroy_system();
    if (m_audio_system) m_audio_system->destroy_system();
    if (m_server_system) m_server_system->destroy_system();
    if (m_client_system) m_client_system->destroy_system();
    m_physics_system.reset();
    m_audio_system.reset();
    m_server_system.reset();
    m_client_system.reset();
}
}
