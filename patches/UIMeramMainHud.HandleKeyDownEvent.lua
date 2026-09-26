-- 원본 F12(디버그 HUD 토글) 분기는 그대로 두고, F1 따라가기/F2 자힐/F3 자동사냥/F4 자동줍기 On/Off 핫키로 추가한다.
-- 둘 다 ___MOD._G.__LuahookFollow 브리지의 서로 다른 필드(enabled/healEnabled)라 독립적으로 켜고 끌 수 있다.
local function getState()
  local state = ___MOD._G.__LuahookFollow
  if not state then
    state = {}
    ___MOD._G.__LuahookFollow = state
  end
  return state
end

return function(self, event)
  if ___MOD._MeramCheatService:IsEnableCheatClientOnly() and not ___MOD._MeramApplicationService:IsInGame() and event.key == ___MOD.KeyboardKey.F12 then
    local DebugHud = self._T.DebugHud
    if DebugHud == nil then
      DebugHud = ___MOD._EntityService:GetEntityByPath("/ui/DebugGroup")
      self._T.DebugHud = DebugHud
    end
    if ___MOD.isvalid(DebugHud) then
      DebugHud.Visible = not DebugHud.Visible
    end
  elseif event.key == ___MOD.KeyboardKey.F1 then
    local state = getState()
    state.enabled = not state.enabled
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage(state.enabled and "[따라가기] ON (빨탭 대상 필요)" or "[따라가기] OFF")
    end
  elseif event.key == ___MOD.KeyboardKey.F2 then
    local state = getState()
    state.healEnabled = not state.healEnabled
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage(state.healEnabled and "[자힐] ON" or "[자힐] OFF")
    end
  elseif event.key == ___MOD.KeyboardKey.F3 then
    local state = getState()
    state.huntEnabled = not state.huntEnabled
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage(state.huntEnabled and "[자동사냥] ON" or "[자동사냥] OFF")
    end
  elseif event.key == ___MOD.KeyboardKey.F4 then
    local state = getState()
    state.lootEnabled = not state.lootEnabled
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage(state.lootEnabled and "[자동줍기] ON" or "[자동줍기] OFF")
    end
  end
end
