module;
#include <vector>
export module ce.app:ecs;
import :utils;

export namespace ce::app::ecs
{
class Entity : utils::NoCopy
{

};
class ComponentArrayBase : public utils::NoCopy
{
};
template<typename T>
class ComponentArray : public ComponentArrayBase
{
    std::vector<T> components{};
};
class Registry : utils::NoCopy
{

};
class System : utils::NoCopy
{

};
}