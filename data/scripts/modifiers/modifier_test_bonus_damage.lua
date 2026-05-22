-- Stage 2: PreAttack BonusDamage 测试用 modifier.
-- 验证 Lua 端 OnAttack 钩子能改 record.bonus_damage 和 damage_type.
-- 通过 self.bonus / self.dtype (实例字段, 默认值在 OnCreated 设置) 控制具体行为,
-- 测试用例可在 attach 后再覆盖.

register_modifier("modifier_test_bonus_damage", {
    IsHidden      = true,
    IsPurgable    = false,
    IsDispellable = false,
    OnCreated = function(self, _owner)
        self.bonus = self.bonus or 25.0
        self.dtype = self.dtype or DamageType.PHYSICAL
    end,
    OnAttack = function(self, _owner, ev)
        ev.bonus_damage = (ev.bonus_damage or 0.0) + (self.bonus or 0.0)
        ev.damage_type  = self.dtype or DamageType.PHYSICAL
    end,
})
