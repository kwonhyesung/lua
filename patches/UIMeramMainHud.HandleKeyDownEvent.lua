-- 원본 F12(디버그 HUD 토글) 분기는 그대로 두고, F1을 "따라가기" On/Off 핫키로 추가한다.
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
    local state = ___MOD._G.__LuahookFollow
    if not state then
      state = { enabled = false }
      ___MOD._G.__LuahookFollow = state
    end
    state.enabled = not state.enabled
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage(state.enabled and "[따라가기] ON (빨탭 대상 필요)" or "[따라가기] OFF")
    end
  end
end
