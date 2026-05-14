-- Juggernaut's Healing Ward. Canonically a summoned unit pulsing heals in a
-- radius; we model it as a self-buff on the caster that heals a percentage
-- of max HP every `tick_interval` seconds for `duration` seconds. The heal
-- routes through the damage/heal pipeline so break-the-healing applies.

local M = {}

function M:on_spell_start(caster, _target, _world)
    local pct      = self:get_special("heal_pct")        -- %/sec
    local duration = self:get_special("duration")
    local interval = self:get_special("tick_interval")
    local per_tick = caster:max_health() * (pct / 100.0) * interval
    caster:add_periodic_heal(per_tick, interval, duration)
end

return M
