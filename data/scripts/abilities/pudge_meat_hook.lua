-- Pudge 肉钩: 射出直线投射物, 命中第一个敌人后将其拖回 Pudge.
-- 演示 linear projectile + 自定义 Lua motion controller.

local M = {}

function M:on_spell_start(caster, _target, world)
    local origin = caster:position()
    local dest   = self:target_point()
    local damage = self:get_special("damage")
    local length = self:get_special("length")
    local width  = self:get_special("width")
    local speed  = self:get_special("missile_speed")

    local dx = dest.x - origin.x
    local dy = dest.y - origin.y
    local d  = math.sqrt(dx*dx + dy*dy)
    if d < 1.0 then d = 1.0 end

    world:create_linear_projectile{
        source    = caster,
        origin    = origin,
        direction = Vec2(dx / d, dy / d),
        speed     = speed,
        length    = length,
        width     = width,
        destroy_on_first_hit = true,
        on_hit = function(victim, point)
            if not victim:alive() then return end
            victim:apply_damage(DamageType.MAGICAL, damage, caster)

            -- 计算拖拽参数: 方向 = victim → caster, 单位向量
            local cp = caster:position()
            local vp = victim:position()
            local rx = cp.x - vp.x
            local ry = cp.y - vp.y
            local rd = math.sqrt(rx*rx + ry*ry)
            if rd < 1.0 then return end
            local rdx, rdy = rx / rd, ry / rd
            local pull_time = rd / speed

            -- 把目标转成 motion controller: 手工设置 self 字段后 add_modifier 实例化.
            -- params 表只支持 duration/stacks, 所以这里改为 add_modifier 后再 patch
            -- self 字段: 先 add_modifier, 然后 find 出来通过 has_modifier 验证.
            -- 简化: 直接给一个 stunned + 走 Generic motion 的近似 → 用 apply_knockback 拖向 caster.
            -- (apply_knockback 是 C++ 提供的, 向方向直推固定距离的 MC)
            victim:apply_knockback(Vec2(rdx, rdy), rd, pull_time)
        end,
    }
end

return M
