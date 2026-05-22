-- Stage 3: 法球框架测试用的 intrinsic modifier.
-- 行为类似冰箭: OnAttack 阶段尝试扣蓝 + 上 cd, 成功则认领 record 并加 bonus damage;
-- 失败 (cd / 蓝量不足 / autocast off / 沉默) 则普攻原样不附加效果.
-- OnAttackLanded 给目标挂减速 debuff. OnAttackFail 不做事 (record 已 claim 但实际没命中).

register_modifier("modifier_test_orb_slow", {
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

        ev.bonus_damage = (ev.bonus_damage or 0.0) + a:get_special("bonus_damage")
        ev.claim = true
    end,

    OnAttackLanded = function(self, owner, ev)
        local w = owner:world()
        if not w then return end
        local tgt = w:find(ev.target_id)
        if not tgt or not tgt:alive() then return end
        local dur = self.ability:get_special("slow_duration")
        tgt:add_modifier("modifier_test_orb_slow_debuff", owner, nil, { duration = dur })
    end,
})

-- 减速 debuff: 简单 movespeed pct.
register_modifier("modifier_test_orb_slow_debuff", {
    IsHidden      = false,
    IsPurgable    = true,
    IsDispellable = true,
    IsDebuff      = true,
    Properties    = {
        { ModifierProperty.MOVE_SPEED_BONUS_PCT, -0.25 },
    },
})
