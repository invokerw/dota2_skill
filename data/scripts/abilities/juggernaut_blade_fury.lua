-- Juggernaut 的剑刃风暴. 持续引导的范围旋转技能, 按固定时间间隔
-- 脉冲式造成魔法伤害. C++ 端在每个世界 tick(1/30秒)调用 on_channel_think,
-- 因此我们累积 dt, 并在跨越技能特殊值 tick_interval 时释放伤害.

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
