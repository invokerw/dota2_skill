-- Lina's Laguna Blade. Massive single-target magical nuke. Straightforward
-- point-and-click damage, similar to Lion's Finger of Death but with higher
-- numbers.

local M = {}

function M:on_spell_start(caster, target, _world)
    if target == nil or not target:alive() then return end
    local dmg = self:get_special("damage")
    target:apply_damage(DamageType.MAGICAL, dmg, caster)
end

return M
