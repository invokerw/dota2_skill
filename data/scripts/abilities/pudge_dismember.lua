-- Pudge 肢解: 引导式单体技能, 每秒造成多次伤害并按比例回血给 Pudge.
-- 演示 channelled ability + on_channel_think 间隔触发.

local M = {}

function M:on_spell_start(caster, _target, _world)
    local target = self:target_unit()
    if not target then return end
    -- 把目标设为眩晕直到引导结束(Dota 中肢解使受害者无法行动).
    local dur = self:get_special("total_duration")
    target:add_stunned(dur)
    self._tick_acc = 0.0
end

function M:on_channel_think(caster, _target, _world, dt)
    local target = self:target_unit()
    if not target or not target:alive() then return end
    local interval = self:get_special("tick_interval")
    self._tick_acc = (self._tick_acc or 0.0) + dt
    while self._tick_acc >= interval do
        self._tick_acc = self._tick_acc - interval
        local dmg = self:get_special("damage_per_tick")
        target:apply_damage(DamageType.MAGICAL, dmg, caster)
        caster:heal(dmg * 0.5)
    end
end

function M:on_channel_finish(_caster, _target, _world, _interrupted)
    self._tick_acc = nil
end

return M
