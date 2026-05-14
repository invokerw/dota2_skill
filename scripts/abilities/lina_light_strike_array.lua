-- Lina's Light Strike Array. Delayed AoE stun + damage at target_point.
-- We model the delay by storing the cast parameters in self and using a
-- think-based approach. However, since our ScriptedAbility doesn't natively
-- support delayed actions within on_spell_start, we simulate the delay as
-- instantaneous for now (the `delay` special exists for data completeness but
-- the stun+damage resolve immediately). A proper implementation would use a
-- thinker entity or world timer.

local M = {}

function M:on_spell_start(caster, _target, world)
    local point  = self:target_point()
    local radius = self:get_special("radius")
    local damage = self:get_special("damage")
    local stun   = self:get_special("stun_duration")

    local enemies = world:find_enemies_in_radius(point, radius, caster:team())
    for _, victim in ipairs(enemies) do
        if victim:alive() then
            victim:add_stunned(stun)
            victim:apply_damage(DamageType.MAGICAL, damage, caster)
        end
    end
end

return M
