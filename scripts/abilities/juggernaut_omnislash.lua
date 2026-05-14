-- Juggernaut's Omnislash. A canonical multi-hit ultimate: the hero enters an
-- out-of-game state and strikes the target (and nearby enemies) N times. For
-- Stage 6 content we resolve all slashes instantly against the primary
-- target; modelling the hop-between-enemies + out-of-game state belongs in a
-- later pass (would require adding Team-pierce + Invulnerable state gating).

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
