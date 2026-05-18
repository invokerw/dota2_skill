-- Sven 风暴之锤：追踪投射物，命中后落点 AoE 伤害 + 眩晕。
-- 演示 tracking projectile + radius 查询 + StatusResist。

local M = {}

function M:on_spell_start(caster, _target, world)
    local target = self:target_unit()
    if not target then return end

    local damage   = self:get_special("damage")
    local stun     = self:get_special("stun_duration")
    local radius   = self:get_special("radius")
    local speed    = self:get_special("missile_speed")
    local team     = caster:team()

    world:create_tracking_projectile{
        source = caster,
        target = target,
        speed  = speed,
        on_hit = function(victim, point)
            -- AoE：以命中点为圆心 radius 范围内的全部敌人受伤害 + 眩晕。
            local enemies = world:find_enemies_in_radius(point, radius, team)
            local saw_primary = false
            for _, e in ipairs(enemies) do
                if e:alive() then
                    e:apply_damage(DamageType.MAGICAL, damage, caster)
                    e:add_stunned(stun)
                end
                if e:id() == victim:id() then saw_primary = true end
            end
            -- 兜底：如果主目标因坐标边界没被命中（例如 Untargetable 中途变化），
            -- 这里不再处理；Dota 中风暴之锤命中 = 必然主目标。
            if not saw_primary and victim:alive() then
                victim:apply_damage(DamageType.MAGICAL, damage, caster)
                victim:add_stunned(stun)
            end
        end,
    }
end

return M
