-- Drow 冰箭. 法球: OnAttack 扣蓝认领 record + 加 bonus_damage; OnAttackLanded
-- 给 target 上减速 debuff. 蓝量不足 / cd / 沉默 / autocast off 时退化为普攻.

register_modifier("modifier_drow_ranger_frost_arrows", {
    IsHidden      = true,
    IsPurgable    = false,
    IsDispellable = false,
    RemoveOnDeath = false,

    -- 普攻投射物粒子 (录像层用; 命中逻辑无影响).
    GetAttackProjectileName = function(_self, _owner)
        return "particles/units/heroes/hero_drow_ranger/drow_frost_arrow.vpcf"
    end,

    OnAttack = function(self, owner, ev)
        local a = self.ability
        if not a then return end
        if not a:autocast_on() then return end
        if owner:has_state(ModifierState.SILENCED) then return end
        if not a:use_resources_for_orb() then return end

        ev.bonus_damage = (ev.bonus_damage or 0.0) + a:get_special("bonus_damage")
        ev.claim = true
    end,

    OnAttackLanded = function(self, owner, ev)
        local w = owner:world()
        if not w then return end
        local tgt = w:find(ev.target_id)
        if not tgt or not tgt:alive() then return end
        -- 把 caster ability 当前级的 slow_pct 快照下来透传给 debuff. debuff 挂在
        -- target 上, 拿不到 caster 的 ability, 用 add_modifier 的 params 自定义
        -- key 把数值随挂载瞬间锁死.
        local dur      = self.ability:get_special("slow_duration")
        local slow_pct = self.ability:get_special("slow_pct") / 100.0
        tgt:add_modifier("modifier_drow_ranger_frost_arrows_slow", owner, nil,
                         { duration = dur, slow_pct = slow_pct })
    end,
})

-- 减速 debuff. slow_pct 由 caster 端在 add_modifier 时通过 params 表传入,
-- 写到 self.slow_pct 上, 这里用 dynamic Properties 读快照值, 保证级别梯度正确.
register_modifier("modifier_drow_ranger_frost_arrows_slow", {
    IsHidden      = false,
    IsPurgable    = true,
    IsDispellable = true,
    IsDebuff      = true,
    Properties = {
        { ModifierProperty.MOVE_SPEED_BONUS_PCT, "GetMoveSpeedBonusPct" },
    },
    GetMoveSpeedBonusPct = function(self, _owner)
        return -(self.slow_pct or 0.0)
    end,
})
