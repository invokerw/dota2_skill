-- 撼地者沟壑的占位 thinker 修饰器：仅维持 thinker 存活、声明 untargetable。
-- 真正的寻路阻挡需要导航网格，本 demo 不实现，保留一个占位以演示 thinker 注册流程。
register_modifier("modifier_earthshaker_fissure_blocker", {
    IsHidden     = true,
    IsPurgable   = false,
    IsDispellable= false,
    IsDebuff     = false,
    States       = { ModifierState.UNTARGETABLE, ModifierState.NO_UNIT_COLLISION },
})
