-- 肉钩拉拽修饰器：把目标拖向施法者。
-- 由 LinkLuaModifier 风格全局注册，OnMotionTick 每个 motion tick 改写目标位置。
-- 演示自定义 Lua motion controller。

register_modifier("modifier_pudge_hook_drag", {
    IsHidden          = false,
    IsPurgable        = false,
    IsDispellable     = false,
    IsDebuff          = true,
    IsMotionController = true,
    MotionPriority    = 5,
    States            = { ModifierState.STUNNED, ModifierState.NO_UNIT_COLLISION },
    -- 直接在 OnCreated 时计算单位向量与每秒速度；OnMotionTick 推进。
    OnCreated = function(self, owner)
        self.dx = self.dx or 0.0
        self.dy = self.dy or 0.0
        self.speed = self.speed or 1300.0
    end,
    OnMotionTick = function(self, owner, dt)
        if not self.dx or not self.dy or not self.speed then return end
        local p = owner:position()
        p.x = p.x + self.dx * self.speed * dt
        p.y = p.y + self.dy * self.speed * dt
        owner:set_position(p)
    end,
})
