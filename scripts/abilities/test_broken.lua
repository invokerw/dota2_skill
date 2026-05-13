-- Intentionally broken ability, used by error_safety tests to confirm that
-- runtime Lua errors don't abort the engine.

local M = {}

function M:on_spell_start(_caster, _target, _world)
    -- Calling a non-existent function raises a Lua error. The engine should
    -- catch it via sol::protected_function, report it, and keep ticking.
    nonexistent_function()
end

return M
