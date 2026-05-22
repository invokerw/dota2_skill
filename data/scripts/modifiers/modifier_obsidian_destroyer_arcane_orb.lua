-- OD 奥术飞弹. 法球: OnAttack 扣蓝认领 record, 改 damage_type=Magical 并加 bonus
-- damage; OnAttackLanded 给周围敌人溅射 splash_pct * (base + bonus) 的魔法伤害.
-- 蓝量大 (120+) -- cd 不需要, 单纯靠蓝量节流.

register_modifier("modifier_obsidian_destroyer_arcane_orb", {
    IsHidden      = true,
    IsPurgable    = false,
    IsDispellable = false,
    RemoveOnDeath = false,

    OnAttack = function(self, owner, ev)
        local a = self.ability
        if not a then return end
        if not a:autocast_on() then return end
        if owner:has_state(ModifierState.SILENCED) then return end
        if not a:use_resources_for_orb() then return end

        ev.bonus_damage = (ev.bonus_damage or 0.0) + a:get_special("damage")
        ev.damage_type  = DamageType.MAGICAL
        ev.claim = true
    end,

    OnAttackLanded = function(self, owner, ev)
        local w = owner:world()
        if not w then return end
        local main = w:find(ev.target_id)
        if not main then return end

        local radius = self.ability:get_special("splash_radius")
        local pct    = self.ability:get_special("splash_pct") / 100.0
        local total  = ev.base_damage + ev.bonus_damage
        local splash = total * pct

        local enemies = w:find_enemies_in_radius(main:position(), radius, owner:team())
        for _, e in ipairs(enemies) do
            if e and e:alive() and e:id() ~= main:id() then
                e:apply_damage(DamageType.MAGICAL, splash, owner)
            end
        end
    end,
})
