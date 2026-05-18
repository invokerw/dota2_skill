#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace dota {

// 最小化同步事件总线
//
// 事件是普通结构体; 监听器接收非 const 引用以便可以
// 修改事件(Dota 修饰器经常重写事件字段 -- 例如
// OnTakeDamage 中的伤害数值). 发布顺序 = 订阅顺序.
class EventBus {
public:
    using Token = std::uint64_t;

    template <typename E>
    Token subscribe(std::function<void(E&)> fn) {
        auto& slot = slots_[std::type_index(typeid(E))];
        const Token token = ++next_token_;
        slot.push_back(Entry{
            token,
            [callback = std::move(fn)](void* ev) { callback(*static_cast<E*>(ev)); },
        });
        return token;
    }

    template <typename E>
    void unsubscribe(Token token) {
        auto it = slots_.find(std::type_index(typeid(E)));
        if (it == slots_.end()) return;
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                           [token](const Entry& e) { return e.token == token; }),
            vec.end());
    }

    template <typename E>
    void publish(E& event) {
        auto it = slots_.find(std::type_index(typeid(E)));
        if (it == slots_.end()) return;
        // 将条目复制到本地向量, 以便处理器可以在分发期间安全地
        // 订阅/取消订阅.
        auto snapshot = it->second;
        for (auto& entry : snapshot) {
            entry.invoke(&event);
        }
    }

    void clear() {
        slots_.clear();
        next_token_ = 0;
    }

private:
    struct Entry {
        Token token;
        std::function<void(void*)> invoke;
    };

    std::unordered_map<std::type_index, std::vector<Entry>> slots_;
    Token next_token_{0};
};

} // namespace dota
