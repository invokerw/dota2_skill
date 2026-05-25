-- Luna 月刃. 被动 (非法球): 不认领 record (不改 bonus_damage / damage_type), 仅
-- claim 让 OnAttackLanded 触发. 主目标受到普攻后, 从主目标位置以 bounce_radius
-- 搜下一个未被本链命中的敌人, 用 TrackingProjectile 弹过去, 命中后按 1-reduction
-- 衰减伤害再继续下一跳, 最多弹 bounces 次.
--
-- 与 lunar_blessing 不冲突: lunar_blessing 在 OnAttack 加 bonus_damage, 本 modifier
-- 读 ev.base_damage + ev.bonus_damage 作为初始弹跳伤害 (即 luna 这次普攻的总伤),
-- 再逐跳衰减. 与法球也不冲突, 因为不动 record 字段.

register_modifier("modifier_luna_moon_glaive", {
    IsHidden      = true,
    IsPurgable    = false,
    IsDispellable = false,
    RemoveOnDeath = false,

    OnAttack = function(_self, _owner, ev)
        ev.claim = true
    end,

    OnAttackLanded = function(self, owner, ev)
        local w = owner:world()
        if not w then return end
        local main = w:find(ev.target_id)
        if not main then return end

        local bounces  = self.ability:get_special("bounces")
        if bounces <= 0 then return end
        local reduction = self.ability:get_special("damage_reduction") / 100.0
        local radius    = self.ability:get_special("bounce_radius")
        local speed     = self.ability:get_special("bounce_speed")
        local dtype     = ev.damage_type   -- 跟随主攻击的伤害类型 (法球可能改成 magical)
        local team      = owner:team()

        -- 已弹链单位 id 集合, 起手包含主目标 (避免立刻弹回自己).
        local visited = { [main:id()] = true }
        local cur_pos = main:position()
        local cur_damage = (ev.base_damage + ev.bonus_damage) * (1.0 - reduction)
        local remaining  = bounces

        local fire_next
        fire_next = function(origin, dmg, left)
            if left <= 0 then return end
            -- 找半径内未弹链过的最近敌人.
            local enemies = w:find_enemies_in_radius(origin, radius, team)
            local best, best_d2
            for _, e in ipairs(enemies) do
                if e and e:alive() and not visited[e:id()] then
                    local p = e:position()
                    local dx = p.x - origin.x
                    local dy = p.y - origin.y
                    local d2 = dx*dx + dy*dy
                    if not best or d2 < best_d2 then
                        best, best_d2 = e, d2
                    end
                end
            end
            if not best then return end
            visited[best:id()] = true

            w:create_tracking_projectile{
                source = owner,
                origin = origin,
                target = best,
                speed  = speed,
                on_hit = function(victim, point)
                    if victim and victim:alive() then
                        victim:apply_damage(dtype, dmg, owner)
                    end
                    fire_next(point, dmg * (1.0 - reduction), left - 1)
                end,
            }
        end

        fire_next(cur_pos, cur_damage, remaining)
    end,
})
