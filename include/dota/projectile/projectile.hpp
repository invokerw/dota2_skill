#pragma once

#include "dota/core/types.hpp"

#include <functional>
#include <string>
#include <unordered_set>

namespace dota {

class Unit;
class World;

// 抽象投射物基类. 每 tick 调用 advance(dt, world); 返回 false 表示请求销毁.
class Projectile {
public:
    using HitCallback    = std::function<void(Unit& victim, Vec2 hit_point)>;
    using FinishCallback = std::function<void()>;

    virtual ~Projectile() = default;

    virtual bool advance(double dt, World& world) = 0;

    // 通用元数据
    EntityId source_id() const { return source_id_; }
    Team     source_team() const { return source_team_; }
    EntityId pid() const { return pid_; }
    void     set_pid(EntityId id) { pid_ = id; }
    Vec2     position() const { return pos_; }

    // 用于事件 / 渲染的描述查询. 默认为 tracking 风格(无 dir/length/width).
    virtual bool     is_linear() const { return false; }
    virtual Vec2     direction() const { return {0.0, 0.0}; }
    virtual double   speed()     const { return 0.0; }
    virtual double   length_total() const { return 0.0; }
    virtual double   width()     const { return 0.0; }
    virtual EntityId target_id() const { return kInvalidEntityId; }

    void set_on_hit(HitCallback cb)    { on_hit_ = std::move(cb); }
    void set_on_finish(FinishCallback cb) { on_finish_ = std::move(cb); }

    // 录像层用的资源名 (粒子). 空表示走默认.
    const std::string& name() const { return name_; }
    void set_name(std::string s)    { name_ = std::move(s); }

protected:
    EntityId       pid_{kInvalidEntityId};
    EntityId       source_id_{kInvalidEntityId};
    Team           source_team_{Team::Neutral};
    Vec2           pos_{};
    HitCallback    on_hit_;
    FinishCallback on_finish_;
    std::string    name_;
};

// 直线投射物: 从 origin 沿 direction 推进, 扫描 (prev, cur) 段对敌人.
// destroy_on_first_hit=true → 命中第一目标后销毁; 否则穿透(已命中集合避免重复).
class LinearProjectile : public Projectile {
public:
    struct Params {
        EntityId source_id      = kInvalidEntityId;
        Team     source_team    = Team::Neutral;
        Vec2     origin{};
        Vec2     direction{1.0, 0.0};
        double   speed          = 1000.0;
        double   length         = 1000.0;     // 最大飞行距离
        double   width          = 100.0;
        bool     destroy_on_first_hit = false;
    };

    explicit LinearProjectile(const Params& p);

    bool advance(double dt, World& world) override;

    bool   is_linear() const override { return true; }
    Vec2   direction() const override { return dir_; }
    double speed()     const override { return speed_; }
    double length_total() const override { return total_length_; }
    double width()     const override { return width_; }

private:
    Vec2   dir_;
    double speed_;
    double total_length_;        // 初始 length, 用于事件 / 渲染
    double remaining_distance_;
    double width_;
    bool   destroy_on_first_hit_;
    std::unordered_set<EntityId> already_hit_;
};

// 跟踪投射物: 追逐目标 unit. 目标死亡 / 不存在 / Untargetable 时 fizzle.
class TrackingProjectile : public Projectile {
public:
    struct Params {
        EntityId source_id   = kInvalidEntityId;
        Team     source_team = Team::Neutral;
        Vec2     origin{};
        EntityId target_id   = kInvalidEntityId;
        double   speed       = 900.0;
        bool     dodgeable   = true;          // 仅作元数据; 本 framework 暂不模拟"闪避"
    };

    explicit TrackingProjectile(const Params& p);

    bool advance(double dt, World& world) override;

    bool     is_linear() const override { return false; }
    double   speed()     const override { return speed_; }
    EntityId target_id() const override { return target_id_; }

private:
    EntityId target_id_;
    double   speed_;
    bool     dodgeable_;
    bool     fizzled_{false};
};

} // namespace dota
