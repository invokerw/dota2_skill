-- Lina 的光击阵。延迟范围眩晕 + 伤害，作用于目标点。
-- 我们通过在 self 中存储施法参数并使用基于 think 的方法来模拟延迟。
-- 然而，由于我们的 ScriptedAbility 在 on_spell_start 中不原生支持延迟动作，
-- 目前我们将延迟模拟为瞬发（`delay` 特殊值的存在是为了数据完整性，但
-- 眩晕和伤害会立即生效）。正确的实现应该使用 thinker 实体或世界计时器。

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
