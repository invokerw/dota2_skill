-- Lina 炽魂的常驻 modifier. ability `lina_fiery_soul` 在
-- AbilityRegistry::instantiate 时把它挂到 Lina 身上 (intrinsic).
--
-- 行为: Lina 每释放一个非 passive 技能 -> 叠 1 层并刷新持续计时;
-- 层数到 max_stacks 时不再增长. 在 duration 内没有再次施法 -> 计时归零,
-- 层数清空; modifier 实例本身保持 permanent (这样下一次施法仍能监听到事件).
--
-- 属性贡献: 引擎在聚合时会把 Properties 的 value × stack_count, 因此
-- 这里返回的是"每层"加成值. 攻速直接是常量 ( +N AS / 层 ),
-- 移速是百分比小数 ( 0.05 = +5% / 层 ).

register_modifier("modifier_lina_fiery_soul", {
    IsHidden        = false,
    IsPurgable      = false,
    IsDispellable   = false,
    IsDebuff        = false,
    RemoveOnDeath   = false,
    -- 用 think 来收回过期层数. 精度 0.1s 对 18s 持续时间足够.
    ThinkInterval   = 0.1,

    Properties = {
        { ModifierProperty.ATTACK_SPEED_BONUS_CONSTANT, "GetModifierAttackSpeed" },
        { ModifierProperty.MOVE_SPEED_BONUS_PCT,        "GetModifierMoveSpeedBonusPct" },
    },

    GetModifierAttackSpeed = function(self, _owner)
        return self.ability:get_special("attack_speed_per_stack")
    end,
    GetModifierMoveSpeedBonusPct = function(self, _owner)
        return self.ability:get_special("move_speed_pct_per_stack") / 100.0
    end,

    OnCreated = function(self, _owner)
        -- 起始无层数, 不提供加成.
        self.handle:set_stack_count(0)
        self.remaining = 0.0   -- 当前叠层剩余时间
    end,

    OnAbilityExecuted = function(self, _owner, ev)
        -- 自身被动 ability 不计入 (intrinsic 体系下也不会触发, 这里防御性写一下).
        if ev.is_passive then return end

        local max_st = math.floor(self.ability:get_special("max_stacks"))
        local dur    = self.ability:get_special("duration")

        local cur = self.handle:stack_count()
        if cur < max_st then
            self.handle:set_stack_count(cur + 1)
        end
        self.remaining = dur
    end,

    OnIntervalThink = function(self, _owner)
        if self.remaining <= 0.0 then return end
        self.remaining = self.remaining - 0.1
        if self.remaining <= 0.0 then
            self.handle:set_stack_count(0)
            self.remaining = 0.0
        end
    end,
})
