-- Juggernaut 的治疗守卫. 原版是一个召唤单位, 在范围内脉冲式治疗;
-- 我们将其建模为施法者身上的自我增益, 每 `tick_interval` 秒治疗
-- 最大生命值的一定百分比, 持续 `duration` 秒. 治疗通过
-- 伤害/治疗管线, 因此破坏治疗效果会生效.

local M = {}

function M:on_spell_start(caster, _target, _world)
    local pct      = self:get_special("heal_pct")        -- %/秒
    local duration = self:get_special("duration")
    local interval = self:get_special("tick_interval")
    local per_tick = caster:max_health() * (pct / 100.0) * interval
    caster:add_periodic_heal(per_tick, interval, duration)
end

return M
