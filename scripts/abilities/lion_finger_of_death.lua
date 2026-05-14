-- Lion's Finger of Death. Instantly applies a huge magical nuke to one enemy
-- target. Kept in Lua to demonstrate the "one-shot spell" Lua-authored
-- pattern (as opposed to Earth Spike, which is DataDriven).

local M = {}

function M:on_spell_start(caster, target, _world)
    if target == nil or not target:alive() then return end
    local dmg = self:get_special("damage")
    target:apply_damage(DamageType.MAGICAL, dmg, caster)
end

return M
