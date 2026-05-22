-- Sven 大劈砍. 被动 (非法球): 不认领 record. 在 OnAttackLanded 时检测目标周围
-- 锥形敌人, 对每个落 base*cleave_pct 物理伤害. 不读 record.bonus_damage, 因此
-- 与 Luna 月之祝福 / 其它 PreAttack BonusDamage 互不干扰; 只用 base_damage 折算.
--
-- 注意: 这里是被动监听, 不在 record.orb_listeners 里, 所以 OnAttackLanded 默认
-- 不会调到. 需要 self push 进 listeners (但又不算 "认领" -- 这里复用 ev.claim
-- 机制以保证 OnAttackLanded 触发).

register_modifier("modifier_sven_great_cleave", {
    IsHidden      = true,
    IsPurgable    = false,
    IsDispellable = false,
    RemoveOnDeath = false,

    OnAttack = function(_self, _owner, ev)
        -- 不改 bonus_damage, 不改 type, 仅 claim 让 OnAttackLanded 被派发.
        ev.claim = true
    end,

    OnAttackLanded = function(self, owner, ev)
        local w = owner:world()
        if not w then return end
        local main = w:find(ev.target_id)
        if not main then return end

        local pct    = self.ability:get_special("cleave_pct") / 100.0
        local radius = self.ability:get_special("cleave_radius")
        local angle  = self.ability:get_special("cleave_angle")
        local half_rad = (angle * 0.5) * math.pi / 180.0

        -- 锥形原点 = attacker 位置, 方向 = attacker -> main target.
        local apos = owner:position()
        local mpos = main:position()
        local dx = mpos.x - apos.x
        local dy = mpos.y - apos.y
        local len = math.sqrt(dx*dx + dy*dy)
        if len < 1e-6 then return end
        local dir = Vec2(dx / len, dy / len)

        local cleave_dmg = ev.base_damage * pct
        local enemies = w:find_enemies_in_cone(apos, dir, radius, half_rad,
                                                owner:team())
        for _, e in ipairs(enemies) do
            if e and e:alive() and e:id() ~= main:id() then
                e:apply_damage(DamageType.PHYSICAL, cleave_dmg, owner)
            end
        end
    end,
})
