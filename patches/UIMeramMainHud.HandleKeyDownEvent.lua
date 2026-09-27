-- 원본 F12(디버그 HUD 토글) 분기는 그대로 두고, F1 따라가기/F2 자힐/F3 자동사냥/F4 자동줍기 On/Off,
-- [ ] 로 자힐 HP% 10%p 단위 조절, -/+ 로 공격 사거리(1~10칸) 조절, ` 로 현재 맵ID 확인 핫키로 추가한다.
-- 전부 ___MOD._G.__LuahookFollow 브리지의 서로 다른 필드라 독립적으로 켜고 끌 수 있다.
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
  elseif event.key == ___MOD.KeyboardKey.LeftBracket or event.key == ___MOD.KeyboardKey.RightBracket then
    local state = getState()
    local pct = state.hpHealPercent or 30  -- 기본값(HP_SELF_HEAL_THRESHOLD)과 맞춤
    pct = pct + (event.key == ___MOD.KeyboardKey.RightBracket and 10 or -10)
    pct = ___MOD._MeramUtils:Clamp(pct, 10, 90)
    state.hpHealPercent = pct
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage("[자힐] HP 기준 " .. pct .. "%")
    end
  elseif event.key == ___MOD.KeyboardKey.Period then
    local state = getState()
    local dist = state.followDistance or 2  -- 기본값(KEEP_DISTANCE)과 맞춤
    dist = dist + 1
    if dist > 3 then dist = 1 end
    state.followDistance = dist
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage("[따라가기] 유지 거리 " .. dist .. "칸")
    end
  elseif event.key == ___MOD.KeyboardKey.Minus or event.key == ___MOD.KeyboardKey.Plus then
    -- '-'가 증가(1→10 순차), '+'가 감소로 지정됨(요청 사양). 10을 넘어가면 1로 순환.
    local state = getState()
    local range = state.attackRange or 1  -- 기본값(ATTACK_RANGE_DEFAULT)과 맞춤
    range = range + (event.key == ___MOD.KeyboardKey.Minus and 1 or -1)
    if range > 10 then range = 1 elseif range < 1 then range = 10 end
    state.attackRange = range
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    if gameHud then
      gameHud:SystemMessage("[자동사냥] 공격 사거리 " .. range .. "칸")
    end
  elseif event.key == ___MOD.KeyboardKey.BackQuote then
    local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
    local localPlayer = ___MOD._UserService and ___MOD._UserService.LocalPlayer
    local playerEntity = localPlayer and localPlayer:GetChildByName("Player")
    local myMov = playerEntity and playerEntity.MeramMovementComponent
    if gameHud then
      local mapId = myMov and myMov.MapId
      local ok, mapName = ___MOD.pcall(function()
        local data = ___MOD._MeramWorldDataService and ___MOD._MeramWorldDataService:ClientGetData()
        return data and data.Name
      end)
      gameHud:SystemMessage("[맵정보] MapId=" .. tostring(mapId) .. " Name=" .. tostring(ok and mapName or "err"))

      local okInst, instMsg = ___MOD.pcall(function()
        local svc = ___MOD._MeramInstanceMapService
        if not svc then return "인스턴스 서비스 없음" end
        local isInst = svc:IsInstanceMapId(mapId)
        if not isInst then
          return "아니오"
        end
        local orig = svc:GetOriginalMapId(mapId)
        return "예 (원본 MapId=" .. tostring(orig) .. ")"
      end)
      gameHud:SystemMessage("[맵정보] 인스턴스맵? " .. tostring(okInst and instMsg or ("에러: " .. tostring(instMsg))))

      -- 포탈 목록도 같이 보여준다 (CacheFieldPortals/CachePortalChunkData를 여기서도 필요하면
      -- 직접 호출 — 맵 에디터만 초기화하는 걸 우리가 대신 해주는 것, MeramTargettingService.OnUpdate와 동일 이유)
      local okPortal, msg = ___MOD.pcall(function()
        local svc = ___MOD._MeramPortalDataService
        if not svc then return "포탈 서비스 없음" end
        -- 맵 에디터의 DrawPortals와 동일한 재시도 방식: 데이터가 비동기로 늦게 뜰 수 있어
        -- 한 번 실패해도 즉시 포기하지 않고 wait(0.01)로 최대 3초 정도 재시도한다.
        local tries = 0
        while not (svc.InitializedFieldPortal and svc.InitializedPortalChunk) and tries < 300 do
          ___MOD.pcall(function() svc:LoadPortalChunk() end)
          ___MOD.pcall(function() svc:LoadFieldPortal() end)
          ___MOD.wait(0.01)
          tries = tries + 1
        end
        if not (svc.InitializedFieldPortal and svc.InitializedPortalChunk) then
          local nameField = tostring(svc.TABLE_NAME_FIELD_PORTAL)
          local nameChunk = tostring(svc.TABLE_NAME_MAP_TO_PORTAL_CHUNK)
          local gotField = tostring(___MOD._DataService:GetTable(svc.TABLE_NAME_FIELD_PORTAL))
          local gotChunk = tostring(___MOD._DataService:GetTable(svc.TABLE_NAME_MAP_TO_PORTAL_CHUNK))
          return "포탈 데이터 초기화 실패 field(name=" .. nameField .. ",got=" .. gotField .. ") chunk(name=" .. nameChunk .. ",got=" .. gotChunk .. ")"
        end
        local portals = svc:GetPortalLocal(mapId)
        if not portals or #portals == 0 then
          return "포탈 없음"
        end
        local parts = {}
        for i, p in ___MOD.ipairs(portals) do
          parts[i] = "(" .. tostring(p.FromX) .. "," .. tostring(p.FromY) .. ")->" .. tostring(p.ToMapId or p.FieldMapName)
        end
        return #portals .. "개: " .. ___MOD.table.concat(parts, " / ")
      end)
      gameHud:SystemMessage("[맵정보] 포탈 " .. tostring(okPortal and msg or ("에러: " .. tostring(msg))))
    end
  end
end
