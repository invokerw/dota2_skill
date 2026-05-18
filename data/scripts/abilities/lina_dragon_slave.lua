-- Lina's Dragon Slave. A line-shaped nuke fired towards target_point. We
-- approximate the line shape by checking enemies within `radius` of the
-- midpoint between caster and target_point (clamped to `range`). This is a
-- simplification -- true line-shape would sample multiple circles along the
-- vector -- but is sufficient for the Stage 6 scripting demo.

local M = {}

function M:on_spell_start(caster, _target, world)
    local origin = caster:position()
    local dest   = self:target_point()
    local radius = self:get_special("radius")
    local damage = self:get_special("damage")
    local range  = self:get_special("range")

    -- Midpoint of the line (approximation: AoE centered halfway along range).
    local dx = dest.x - origin.x
    local dy = dest.y - origin.y
    local dist = math.sqrt(dx * dx + dy * dy)
    if dist < 1.0 then dist = 1.0 end
    local half_range = range * 0.5
    local mx = origin.x + dx / dist * half_range
    local my = origin.y + dy / dist * half_range
    local mid = Vec2(mx, my)

    local enemies = world:find_enemies_in_radius(mid, radius, caster:team())
    for _, victim in ipairs(enemies) do
        if victim:alive() then
            victim:apply_damage(DamageType.MAGICAL, damage, caster)
        end
    end
end

return M
