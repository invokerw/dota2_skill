-- 测试用: 注册风格的 Lua 修饰器, 宣称提供 25% 闪避.
-- 通过 register_modifier(name, spec) 注册, 运行时由 unit:add_modifier 实例化.

register_modifier("modifier_test_evasion", {
    IsHidden   = true,
    IsPurgable = false,
    IsDebuff   = false,
    Properties = {
        { ModifierProperty.EVASION, 0.25 },
    },
})
