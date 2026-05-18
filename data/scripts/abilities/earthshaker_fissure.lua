-- 撼地者 沟壑：从 caster 沿 target_point 方向喷射一条直线，命中线上敌人 → 伤害 + 眩晕 + 击退。
-- 落地形成不可通行的"沟壑" thinker 持续若干秒（仅作占位，本系统未实现真正的寻路阻挡）。

local M = {}

function M:on_spell_start(caster, _target, world)
    local origin = caster:position()
    local dest   = self:target_point()

    local damage   = self:get_special("damage")
    local stun     = self:get_special("stun_duration")
    local length   = self:get_special("length")
    local width    = self:get_special("width")
    local kb_dist  = self:get_special("knockback")
    local kb_time  = self:get_special("knockback_time")
    local fdur     = self:get_special("fissure_duration")

    -- 计算沿 caster→target_point 方向的固定长度终点。
    local dx = dest.x - origin.x
    local dy = dest.y - origin.y
    local d  = math.sqrt(dx*dx + dy*dy)
    if d < 1.0 then d = 1.0 end
    local dirx, diry = dx / d, dy / d
    local end_x = origin.x + dirx * length
    local end_y = origin.y + diry * length

    local enemies = world:find_enemies_in_line(origin, Vec2(end_x, end_y), width, caster:team())
    for _, e in ipairs(enemies) do
        if e:alive() then
            e:apply_damage(DamageType.MAGICAL, damage, caster)
            e:add_stunned(stun)
            -- 击退方向：沿沟壑方向（与 caster 朝向一致）。
            e:apply_knockback(Vec2(dirx, diry), kb_dist, kb_time)
        end
    end

    -- 在沟壑中点放一个 thinker 占位（演示 thinker API；真正的阻挡逻辑超出本 demo 范围）。
    local mid = Vec2(origin.x + dirx * length * 0.5, origin.y + diry * length * 0.5)
    world:create_thinker(mid, fdur, "modifier_earthshaker_fissure_blocker", caster)
end

return M
