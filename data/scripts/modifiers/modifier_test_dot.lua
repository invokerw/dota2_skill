-- 测试用: 每秒造成 50 点 magical 伤害的 DoT.
-- 演示 OnIntervalThink 与 ThinkInterval 字段.

register_modifier("modifier_test_dot", {
    IsHidden        = false,
    IsDebuff        = true,
    IsPurgable      = true,
    ThinkInterval   = 1.0,
    States          = {},
    OnIntervalThink = function(self, owner)
        owner:apply_damage(DamageType.MAGICAL, 50.0)
    end,
})
