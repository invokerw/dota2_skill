-- Juggernaut's Blade Fury. Channelled AoE spin that pulses magical damage on
-- a fixed tick interval. The C++ side invokes on_channel_think every world
-- tick (1/30s), so we accumulate dt and emit damage whenever we cross the
-- ability-special tick_interval.

local M = {}

function M:on_spell_start(caster, _target, _world)
    self._accum = 0.0
end

function M:on_channel_think(caster, _target, world, dt)
    local interval = self:get_special("tick_interval")
    local radius   = self:get_special("radius")
    local dps      = self:get_special("damage_per_s")
    if interval <= 0.0 then return end

    self._accum = (self._accum or 0.0) + dt
    while self._accum >= interval do
        self._accum = self._accum - interval
        local per_pulse = dps * interval
        local enemies   = world:find_enemies_in_radius(
            caster:position(), radius, caster:team())
        for _, victim in ipairs(enemies) do
            victim:apply_damage(DamageType.MAGICAL, per_pulse, caster)
        end
    end
end

function M:on_channel_finish(_caster, _target, _world, _interrupted)
    self._accum = 0.0
end

return M
