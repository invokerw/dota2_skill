-- 仅吸收魔法伤害的护盾。由 Stage 4 测试使用，用于验证 Lua
-- modifier 可以 (a) 声明状态/属性，(b) 通过 on_pre_take_damage 吸收伤害，
-- 以及 (c) 返回吸收的数量。

local M = {}

M.states = {}
M.properties = {
    { ModifierProperty.MAGIC_RESIST_BONUS, 0.10 },
}

function M:on_created(_owner)
    self._remaining = self._remaining or 200.0
end

function M:on_pre_take_damage(_owner, amount, dtype)
    if amount <= 0.0 then return 0.0 end
    if dtype ~= DamageType.MAGICAL then return 0.0 end
    local eat = math.min(self._remaining or 0.0, amount)
    self._remaining = (self._remaining or 0.0) - eat
    return eat
end

return M
