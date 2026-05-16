-- 测试用 thinker 修饰器：每 0.5s 对周围敌人造成 25 点 magical 伤害。
register_modifier("modifier_test_aoe_thinker", {
    IsHidden        = true,
    IsDebuff        = false,
    IsPurgable      = false,
    ThinkInterval   = 0.5,
    OnIntervalThink = function(self, owner)
        local world = nil
        -- 没有直接的 World 引用 — Lua 测试中通过 self.world 注入；本测试简化做法是不在
        -- modifier 内部触发 AoE，仅记录 tick 数到 self.ticks（C++ 测试读取它）。
        self.ticks = (self.ticks or 0) + 1
    end,
})
