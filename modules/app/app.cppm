module;
export module ce.platform;

class AppBase
{
public:
    //bool init() noexcept;
    //bool create_surface() noexcept;
    virtual ~AppBase() = default;
};

export namespace ce::app
{
class AppInterface : public AppBase
{
public:
    virtual bool on_init() noexcept = 0;
    virtual bool on_surface() noexcept = 0;
};
}

