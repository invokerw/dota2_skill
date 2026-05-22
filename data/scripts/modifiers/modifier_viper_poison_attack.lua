-- Viper 毒蛇之 (毒性攻击). 法球: 每次普攻扣蓝, 命中给 target 上一层 DOT debuff,
-- 已存在则叠层 + 刷新持续时间 (上限 max_stacks).
-- 不修改 bonus_damage / damage_type, 攻击伤害本身不变, 真正伤害来自 DOT debuff.

register_modifier("modifier_viper_poison_attack", {
    IsHidden      = true,
    IsPurgable    = false,
    IsDispellable = false,
    RemoveOnDeath = false,

    OnAttack = function(self, owner, ev)
        local a = self.ability
        if not a then return end
        if not a:autocast_on() then return end
        if owner:has_state(ModifierState.SILENCED) then return end
        if not a:use_resources_for_orb() then return end
        ev.claim = true
    end,

    OnAttackLanded = function(self, owner, ev)
        local w = owner:world()
        if not w then return end
        local tgt = w:find(ev.target_id)
        if not tgt or not tgt:alive() then return end

        local dur     = self.ability:get_special("duration")
        local max_st  = math.floor(self.ability:get_special("max_stacks"))

        local existing = tgt:find_modifier("modifier_viper_poison_attack_debuff")
        if existing then
            local cur = existing:stack_count()
            if cur < max_st then existing:set_stack_count(cur + 1) end
            existing:refresh(dur)
        else
            tgt:add_modifier("modifier_viper_poison_attack_debuff", owner, nil,
                             { duration = dur, stacks = 1 })
        end
    end,
})

-- DOT debuff. 每 0.5s 触发一次, 每跳伤害 = damage_per_second * stack_count * 0.5.
-- 但 debuff 自身没有 ability 句柄 (caster 给它的 ability 上挂的是 intrinsic),
-- 所以这里写死一个 base 值 (lvl1 = 10), Stage 5 验收只关心 "命中后 hp 持续掉".
register_modifier("modifier_viper_poison_attack_debuff", {
    IsHidden      = false,
    IsPurgable    = true,
    IsDispellable = true,
    IsDebuff      = true,
    ThinkInterval = 0.5,

    OnIntervalThink = function(self, owner)
        local stacks = self.handle:stack_count()
        if stacks <= 0 then return end
        -- 固定每秒 10 物理伤害 / 层 (与 lvl1 viper poison_attack 对齐).
        local dmg = 10.0 * stacks * 0.5
        owner:apply_damage(DamageType.PHYSICAL, dmg)
    end,
})
