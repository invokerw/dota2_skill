-- Juggernaut 的无敌斩。经典的多段打击终极技能：英雄进入
-- 脱离游戏状态并对目标（及附近敌人）进行 N 次打击。对于
-- Stage 6 内容，我们将所有斩击瞬间作用于主要目标；
-- 在敌人之间跳跃 + 脱离游戏状态的建模属于后续阶段
--（需要添加穿透队伍 + 无敌状态判定）。

local M = {}

function M:on_spell_start(caster, target, _world)
    if target == nil or not target:alive() then return end
    local n    = math.floor(self:get_special("slashes"))
    local dmg  = self:get_special("damage_per_hit")
    for _ = 1, n do
        if not target:alive() then break end
        target:apply_damage(DamageType.PURE, dmg, caster)
    end
end

return M
