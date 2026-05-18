-- Lion 的死亡一指。瞬间对一个敌方目标施加巨额魔法伤害。
-- 保留在 Lua 中以演示"一次性技能"的 Lua 编写模式
--（与 DataDriven 的地刺相对）。

local M = {}

function M:on_spell_start(caster, target, _world)
    if target == nil or not target:alive() then return end
    local dmg = self:get_special("damage")
    target:apply_damage(DamageType.MAGICAL, dmg, caster)
end

return M
