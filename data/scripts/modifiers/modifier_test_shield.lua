-- 仅吸收魔法伤害的护盾。验证 Lua modifier 可以
-- (a) 声明 States/Properties，
-- (b) 通过 OnPreTakeDamage 吸收伤害（返回吸收数值），
-- (c) 在 self 上保留实例状态（_remaining 计数）。

register_modifier("modifier_test_shield", {
    IsHidden      = false,
    IsPurgable    = true,
    IsDispellable = true,
    IsDebuff      = false,
    States        = {},
    Properties    = {
        { ModifierProperty.MAGIC_RESIST_BONUS, 0.10 },
    },
    OnCreated = function(self, _owner)
        self._remaining = self._remaining or 200.0
    end,
    OnPreTakeDamage = function(self, _owner, ev)
        local amount = ev.amount
        if amount <= 0.0 then return 0.0 end
        if ev.type ~= DamageType.MAGICAL then return 0.0 end
        local eat = math.min(self._remaining or 0.0, amount)
        self._remaining = (self._remaining or 0.0) - eat
        return eat
    end,
})
