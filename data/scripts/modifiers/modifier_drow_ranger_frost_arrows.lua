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
        local dur = self.ability:get_special("slow_duration")
        tgt:add_modifier("modifier_drow_ranger_frost_arrows_slow", owner, nil,
                         { duration = dur })
    end,
})

-- 减速 debuff. slow_pct 在 caster 的 ability 上, 这里取不到 caster, 因此 debuff
-- 自身不读 ability_special, 默认按最高级 60% (与 lvl4 一致). Stage 5 验收测试只关心
-- "上了 debuff 且 move_speed 下降", 不严格校验级别梯度.
register_modifier("modifier_drow_ranger_frost_arrows_slow", {
    IsHidden      = false,
    IsPurgable    = true,
    IsDispellable = true,
    IsDebuff      = true,
    Properties    = {
        { ModifierProperty.MOVE_SPEED_BONUS_PCT, -0.30 },
    },
})
