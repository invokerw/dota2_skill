-- A magic-damage-only shield. Used by Stage 4 tests to verify that a Lua
-- modifier can (a) declare states/properties, (b) absorb damage via
-- on_pre_take_damage, and (c) return the absorbed amount.

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
