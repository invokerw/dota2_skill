-- Luna 月之祝福. 被动 (非法球): 不认领 record, 单纯每次普攻往 record.bonus_damage
-- 加固定值. 与法球叠加无冲突 -- 只要其它法球不覆写 bonus_damage, 二者会一起合并
-- 进 complete_attack 的 base + bonus 总伤害.

register_modifier("modifier_luna_lunar_blessing", {
    IsHidden      = true,
    IsPurgable    = false,
    IsDispellable = false,
    RemoveOnDeath = false,

    OnAttack = function(self, _owner, ev)
        local a = self.ability
        if not a then return end
        ev.bonus_damage = (ev.bonus_damage or 0.0) + a:get_special("bonus_damage")
    end,
})
