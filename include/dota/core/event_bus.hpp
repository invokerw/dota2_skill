#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace dota {

// Minimal synchronous event bus.
//
// Events are plain structs; listeners take a non-const reference so they can
// mutate the event (Dota modifiers frequently rewrite fields on an event — e.g.
// damage amount in OnTakeDamage). Publish order = subscribe order.
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
        // Copy the entries to a local vector so a handler can safely
        // subscribe/unsubscribe during dispatch.
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
