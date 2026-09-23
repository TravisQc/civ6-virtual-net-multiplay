// 引擎公共接口：ProxyManager（中转）与 DivertForwarder（同机）都实现它，
// 便于 AppController 用同一指针持有并统一启停/切换详细日志。
#pragma once

namespace civ6 {

class Engine {
public:
    virtual ~Engine() = default;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual void set_verbose(bool enabled) = 0;
};

}  // namespace civ6
