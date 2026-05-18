-- Lina 的神灭斩. 大量单体目标魔法伤害. 简单直接的
-- 点击目标伤害技能, 类似于 Lion 的死亡一指但数值更高.

local M = {}

function M:on_spell_start(caster, target, _world)
    if target == nil or not target:alive() then return end
    local dmg = self:get_special("damage")
    target:apply_damage(DamageType.MAGICAL, dmg, caster)
end

return M
