-- 원본 동작(self:OnDraw())은 그대로 유지하고, F1로 켠 "따라가기"가 활성화된 동안
-- 빨탭(Tab-Tab으로 Fixed된) 대상 쪽으로 이 게임의 최대 입력 속도(1초 5틱 = 200ms 간격)에 맞춰
-- 이동 명령을 보낸다. 대상이 시야에서 사라지면(같은 맵일 때) 마지막으로 확인된 좌표까지 간 뒤,
-- 마지막으로 이동하던 방향으로 몇 칸 더 가보고 그래도 못 찾으면 정지한다.
-- On/Off 상태는 UIMeramMainHud.HandleKeyDownEvent(F1)와 ___MOD._G.__LuahookFollow로 공유한다
-- (서로 다른 청크라 self로 상태 공유 불가 → Lua 전역 테이블을 브리지로 사용).
local MOVE_INTERVAL = 0.2  -- 1초 5틱
local KEEP_DISTANCE = 1  -- 대상과 유지할 최소 거리(맨해튼)
local SEARCH_EXTRA_STEPS = 2  -- 마지막 좌표 도착 후 마지막 방향으로 더 가볼 칸 수
local MAP_MATCH_GRACE_SEC = 5  -- 맵 전환(포탈) 중 내 MapId 갱신을 기다려주는 유예 시간
local PATHFIND_MAX_NODES = 10  -- BFS 탐색 범위(칸 수). 벗어나면 기존 방향 추정 방식으로 대체
local elapsed = 0

-- 자힐(HP/MP) + 따라가기 대상 회복
local HP_SELF_HEAL_SLOT = 3
local HP_SELF_HEAL_THRESHOLD = 0.30
local MP_SELF_HEAL_SLOT = 2
local MP_SELF_HEAL_THRESHOLD = 0.30
local TARGET_HEAL_SLOT = 3
local TARGET_HEAL_THRESHOLD = 0.90
local HEAL_COOLDOWN_SEC = 1/3  -- 게임 최대 입력 5틱(200ms) 대비 3틱(≈333ms) 간격
local healCooldown = 0
local mpHealCooldown = 0
local targetHealCooldown = 0
local dbgElapsed = 0

-- 공격 스킬(캐릭터마다 다르지만 우선 1번 슬롯으로 고정)
local ATTACK_SPELL_SLOT = 1
local ATTACK_IS_RANGED = true  -- 1번 스킬이 원거리면 true(붙지 않아도 바로 시전), 근접이면 false

-- 자동사냥 (F3): 시야 안(같은 맵)의 가장 가까운 몬스터에게 이동+공격
local HUNT_INTERVAL = 0.2  -- 1초 5틱
local huntElapsed = 0
local NO_MONSTER_GRACE_SEC = 2  -- 몬스터가 없다고 바로 판단하지 않고 이만큼 기다려본다
local noMonsterElapsed = 0

-- 몬스터가 없을 때 다음 맵으로: 안 가본 맵으로 이어지는 포탈을 우선으로 고른다.
-- (게임 자체엔 "다음 층" 개념이 없고 맵마다 개별 포탈로만 연결돼 있어서, 방문 기록으로
-- 뒤로가기 포탈을 피하는 방식으로 대부분의 순차 던전 구조에서 앞으로 진행하게 한다.)
local function pickNextPortal(mapId, visitedMaps)
  local portals = ___MOD._MeramPortalDataService and ___MOD._MeramPortalDataService:GetPortalLocal(mapId)
  if not portals or #portals == 0 then
    return nil
  end
  local fallback
  for _, portal in ___MOD.ipairs(portals) do
    if portal.ToMapId then  -- Field 타입 포탈은 ToMapId가 없어 "안 가본 곳" 판단이 안 되니 fallback으로만 씀
      if not fallback then fallback = portal end
      if not visitedMaps[portal.ToMapId] then
        return portal
      end
    elseif not fallback then
      fallback = portal
    end
  end
  return fallback
end

-- 자동줍기 (F4): 시야 안(같은 맵)의 가장 가까운 필드 아이템 위치로 이동해서 줍는다.
local LOOT_INTERVAL = 0.2  -- 1초 5틱
local lootElapsed = 0
-- 비어있으면 전부 습득. 채우면 이 이름들만 습득 (예: {"금", "체력 물약"}).
-- 실제 이름은 [dbg loot] item=... 메시지로 확인 후 채워 넣는다.
local LOOT_NAME_FILTER = {}

local function isWantedItem(name)
  if #LOOT_NAME_FILTER == 0 then
    return true
  end
  for _, wanted in ___MOD.ipairs(LOOT_NAME_FILTER) do
    if name == wanted then
      return true
    end
  end
  return false
end

local function findNearestItem(myPos)
  local seen = ___MOD._MeramCreatureService and ___MOD._MeramCreatureService.SeenObjectsClientOnly
  if not seen then
    return nil
  end
  local nearest, nearestDist
  for _, entity in ___MOD.pairs(seen) do
    if ___MOD.isvalid(entity) then
      local cc = entity.MeramCreatureController
      if cc and ___MOD._MeramObjectKey:IsItem(cc.TypeKey) and isWantedItem(entity.Name) then
        local mov = entity.MeramMovementComponent
        local pos = mov and mov.Position
        if pos then
          local dist = ___MOD.math.abs(pos.x - myPos.x) + ___MOD.math.abs(pos.y - myPos.y)
          if not nearestDist or dist < nearestDist then
            nearest, nearestDist = entity, dist
          end
        end
      end
    end
  end
  return nearest, nearestDist
end

local function findNearestMonster(myPos)
  local seen = ___MOD._MeramCreatureService and ___MOD._MeramCreatureService.SeenObjectsClientOnly
  if not seen then
    return nil
  end
  local nearest, nearestDist
  for _, entity in ___MOD.pairs(seen) do
    if ___MOD.isvalid(entity) then
      local cc = entity.MeramCreatureController
      if cc and cc.TypeKey == ___MOD._MeramObjectKey.Monster and not cc:IsPendingKill() then
        local mov = entity.MeramMovementComponent
        local pos = mov and mov.Position
        if pos then
          local dist = ___MOD.math.abs(pos.x - myPos.x) + ___MOD.math.abs(pos.y - myPos.y)
          if not nearestDist or dist < nearestDist then
            nearest, nearestDist = entity, dist
          end
        end
      end
    end
  end
  return nearest, nearestDist
end

local function getSpellInventory(gameHud)
  if not gameHud then return nil end
  local sidePanel = gameHud:GetSidePanel()
  local spellEntity = sidePanel and sidePanel.Spell
  return spellEntity and spellEntity.UIMeramSpellInventory
end

local function doorStateGetter(pos)
  return ___MOD._MeramDoorStateService:IsDoorClosed(pos, nil)
end

-- 따라가기용 BFS 경로탐색: 이동불가 타일을 완벽히 피해서 최단경로를 찾는다.
-- 별도 타일맵 데이터 없이, 이미 쓰는 CanGo(임의 좌표에 대해 "이 방향으로 갈 수 있나" 판정)를
-- 그대로 간선(edge) 판정 함수로 재사용한다. maxNodes로 탐색 범위를 제한(가벼움 유지, 범위 밖/
-- 막혀서 못 찾으면 nil 반환 → 호출부가 기존 방식으로 대체).
local PATH_DIRS = nil  -- 지연 초기화 (___MOD._MeramDirection 준비된 뒤 채움)
local function bfsPath(mapId, startPos, goalPos, maxNodes)
  if startPos.x == goalPos.x and startPos.y == goalPos.y then
    return {}
  end
  if not PATH_DIRS then
    PATH_DIRS = {
      ___MOD._MeramDirection.NORTH, ___MOD._MeramDirection.EAST,
      ___MOD._MeramDirection.SOUTH, ___MOD._MeramDirection.WEST,
    }
  end
  local startKey = startPos.x .. "," .. startPos.y
  local visited = { [startKey] = true }
  local queue = { { pos = startPos, path = {} } }
  local head = 1
  local expanded = 0
  while head <= #queue and expanded < maxNodes do
    local node = queue[head]
    head = head + 1
    expanded = expanded + 1
    for _, dir in ___MOD.ipairs(PATH_DIRS) do
      if ___MOD._MeramTransformUtils:CanGo(mapId, node.pos, dir, doorStateGetter) then
        local nextPos = ___MOD._MeramDirection:GetNeighborPointVec2(node.pos, dir)
        local key = nextPos.x .. "," .. nextPos.y
        if not visited[key] then
          visited[key] = true
          if nextPos.x == goalPos.x and nextPos.y == goalPos.y then
            local path = {}
            for i, d in ___MOD.ipairs(node.path) do path[i] = d end
            path[#path + 1] = dir
            return path
          end
          local newPath = {}
          for i, d in ___MOD.ipairs(node.path) do newPath[i] = d end
          newPath[#newPath + 1] = dir
          queue[#queue + 1] = { pos = nextPos, path = newPath }
        end
      end
    end
  end
  return nil  -- 범위(maxNodes) 안에서 못 찾음
end

-- 목표 방향으로 가되 막혀 있으면 좌/우/반대 순으로 갈 수 있는 방향을 찾아 이동한다.
-- (MeramPlayerController.Move가 쓰는 CanGo, MeramMonsterContext.Follow와 같은 순서)
-- 반환값: 실제로 이동을 시도한 방향(막혀서 아무 데도 못 갔으면 nil).
local function moveToward(myMov, myPos, controller, direction)
  local function canGo(dir)
    return ___MOD._MeramTransformUtils:CanGo(myMov.MapId, myPos, dir, doorStateGetter)
  end
  local candidates = {
    direction,
    ___MOD._MeramDirection:GetLeftDirection(direction),
    ___MOD._MeramDirection:GetRightDirection(direction),
    ___MOD._MeramDirection:GetOppositeDirection(direction),
  }
  for _, dir in ___MOD.ipairs(candidates) do
    if canGo(dir) then
      controller:TryMove(dir, false)
      return dir
    end
  end
  return nil
end

local function directionTo(myPos, pos)
  local dx = pos.x - myPos.x
  local dy = pos.y - myPos.y
  if ___MOD.math.abs(dx) >= ___MOD.math.abs(dy) then
    return (dx > 0 and ___MOD._MeramDirection.EAST or ___MOD._MeramDirection.WEST), dx, dy
  end
  return (dy > 0 and ___MOD._MeramDirection.SOUTH or ___MOD._MeramDirection.NORTH), dx, dy
end

return function(self, delta)
  self:OnDraw()

  local state = ___MOD._G.__LuahookFollow
  if not state then
    elapsed = 0
    return
  end

  local gameHud = ___MOD._MeramHudService:GetCurrentGameHud()
  local avail = state.enabled and self:IsTargetAvailable()

  ___MOD.pcall(function()
    local localPlayer = ___MOD._UserService and ___MOD._UserService.LocalPlayer
    if not localPlayer then
      return
    end
    local playerEntity = localPlayer:GetChildByName("Player")
    if not playerEntity then
      return
    end
    local myMov = playerEntity.MeramMovementComponent
    if not myMov then
      return
    end
    local myPos = myMov.Position
    if not myPos then
      return
    end
    local controller = localPlayer.MeramPlayerController
    if not controller then
      return
    end

    dbgElapsed = dbgElapsed + (delta or 0)
    local dbg = dbgElapsed >= 1.0
    if dbg then dbgElapsed = 0 end

    -- 자힐(HP): F2로 따라가기와 독립적으로 켜고 끈다.
    -- HP/MP는 UIMeramUserInfo.SetProp 패치가 ___MOD._G 브리지에 캐싱해둔 값을 읽는다
    -- (UI 위젯 자체엔 현재값이 저장 안 돼 있어서 직접 못 읽음).
    if state.healEnabled then
      local myNetObjId = playerEntity.MeramCreatureController and playerEntity.MeramCreatureController.NetObjId
      healCooldown = healCooldown - (delta or 0)
      mpHealCooldown = mpHealCooldown - (delta or 0)
      local hp, maxHp = state.myHp, state.myMaxHp
      local mp, maxMp = state.myMp, state.myMaxMp
      if dbg then
        if gameHud then
          gameHud:SystemMessage("[dbg heal] hp=" .. tostring(hp) .. "/" .. tostring(maxHp)
            .. " mp=" .. tostring(mp) .. "/" .. tostring(maxMp) .. " netObjId=" .. tostring(myNetObjId))
        end
      end
      if hp and maxHp and maxHp > 0 and hp / maxHp <= HP_SELF_HEAL_THRESHOLD and healCooldown <= 0 then
        healCooldown = HEAL_COOLDOWN_SEC
        local spellInv = getSpellInventory(gameHud)
        if spellInv and myNetObjId then
          spellInv:TryServerUseSpell(HP_SELF_HEAL_SLOT, myNetObjId, 0, 0, 0, "")
          if gameHud then gameHud:SystemMessage("[자힐] HP 회복 시전") end
        elseif gameHud then
          gameHud:SystemMessage("[dbg heal] HP 시전 실패 spellInv=" .. tostring(spellInv) .. " netObjId=" .. tostring(myNetObjId))
        end
      end
      if mp and maxMp and maxMp > 0 and mp / maxMp <= MP_SELF_HEAL_THRESHOLD and mpHealCooldown <= 0 then
        mpHealCooldown = HEAL_COOLDOWN_SEC
        local spellInv = getSpellInventory(gameHud)
        if spellInv and myNetObjId then
          spellInv:TryServerUseSpell(MP_SELF_HEAL_SLOT, myNetObjId, 0, 0, 0, "")
          if gameHud then gameHud:SystemMessage("[자힐] MP 회복 시전") end
        elseif gameHud then
          gameHud:SystemMessage("[dbg heal] MP 시전 실패 spellInv=" .. tostring(spellInv) .. " netObjId=" .. tostring(myNetObjId))
        end
      end
    end

    -- 자동줍기(F4)를 자동사냥(F3)보다 먼저 체크한다: 주울 아이템이 있으면 그쪽을 우선한다.
    local nearItem = state.lootEnabled and findNearestItem(myPos)

    -- 자동사냥(F3): 자동줍기가 처리할 아이템이 없을 때만 동작한다.
    if state.huntEnabled and not nearItem then
      state.visitedMaps = state.visitedMaps or {}
      if state.visitedMaps[myMov.MapId] ~= true then
        state.visitedMaps[myMov.MapId] = true
        state.huntPortal = nil  -- 새 맵에 들어옴: 이전에 찍어둔 포탈 목표는 버리고 새로 판단
        noMonsterElapsed = 0
      end

      huntElapsed = huntElapsed + (delta or 0)
      if huntElapsed >= HUNT_INTERVAL then
        huntElapsed = huntElapsed - HUNT_INTERVAL
        local monster, dist = findNearestMonster(myPos)
        if monster then
          noMonsterElapsed = 0
          state.huntPortal = nil
          local monPos = monster.MeramMovementComponent and monster.MeramMovementComponent.Position
          if monPos then
            local facingDir = directionTo(myPos, monPos)
            if dist <= 1 or ATTACK_IS_RANGED then
              controller:ChangeDirection(facingDir)  -- 공격 전에 대상 쪽으로 방향부터 맞춘다
              local spellInv = getSpellInventory(gameHud)
              local monCC = monster.MeramCreatureController
              if spellInv and monCC and monCC.NetObjId then
                spellInv:TryServerUseSpell(ATTACK_SPELL_SLOT, monCC.NetObjId, 0, 0, 0, "")
              end
            end
            if dist > 1 then
              -- 이동불가 타일을 완벽히 피하도록 BFS로 다음 칸을 정한다(범위 밖/실패 시 기존 방식으로 대체).
              local path = bfsPath(myMov.MapId, myPos, monPos, PATHFIND_MAX_NODES)
              local moveDir = (path and path[1]) or facingDir
              moveToward(myMov, myPos, controller, moveDir)  -- 원거리라도 계속 접근(줍기 등 위해)
            end
          end
          if dbg and gameHud then
            gameHud:SystemMessage("[dbg hunt] target=" .. tostring(monster.Name) .. " dist=" .. tostring(dist))
          end
        else
          -- 몬스터가 안 보임: 바로 포기하지 않고 잠깐 더 기다렸다가(NO_MONSTER_GRACE_SEC) 포탈을 찾는다.
          noMonsterElapsed = noMonsterElapsed + HUNT_INTERVAL
          if noMonsterElapsed >= NO_MONSTER_GRACE_SEC then
            if not state.huntPortal then
              state.huntPortal = pickNextPortal(myMov.MapId, state.visitedMaps)
              if dbg and gameHud then
                gameHud:SystemMessage("[dbg hunt] 몬스터 없음 → 포탈 탐색: " .. tostring(state.huntPortal))
              end
            end
            local portal = state.huntPortal
            if portal then
              local portalPos = { x = portal.FromX, y = portal.FromY }
              local direction, dx, dy = directionTo(myPos, portalPos)
              if dx == 0 and dy == 0 then
                -- 포탈 타일 위: 서버가 알아서 텔레포트한다 (별도 액션 불필요)
              else
                moveToward(myMov, myPos, controller, direction)
              end
              if dbg and gameHud then
                gameHud:SystemMessage("[dbg hunt] 포탈로 이동 dx=" .. tostring(dx) .. " dy=" .. tostring(dy))
              end
            elseif dbg and gameHud then
              gameHud:SystemMessage("[dbg hunt] 이 맵엔 포탈이 없음")
            end
          elseif dbg and gameHud then
            gameHud:SystemMessage("[dbg hunt] 주변에 몬스터 없음 (" .. ___MOD.string.format("%.1f", noMonsterElapsed) .. "/" .. NO_MONSTER_GRACE_SEC .. ")")
          end
        end
      end
    end

    -- 자동줍기(F4): 자동사냥보다 우선 처리(위에서 nearItem으로 이미 조회함).
    if state.lootEnabled then
      lootElapsed = lootElapsed + (delta or 0)
      if lootElapsed >= LOOT_INTERVAL then
        lootElapsed = lootElapsed - LOOT_INTERVAL
        local item = nearItem
        if item then
          local itemPos = item.MeramMovementComponent and item.MeramMovementComponent.Position
          if itemPos then
            local direction, dx, dy = directionTo(myPos, itemPos)
            if dx == 0 and dy == 0 then
              controller:ActionGet(true)
              if gameHud then
                gameHud:SystemMessage("[자동줍기] " .. tostring(item.Name) .. " 획득 시도")
              end
            else
              moveToward(myMov, myPos, controller, direction)
            end
          end
        end
      end
    end

    if not state.enabled then
      elapsed = 0
      return
    end

    if avail then
      -- 정상 추적: 대상이 보이는 동안 마지막 위치/맵/방향을 계속 갱신해둔다.
      local target = self:GetTarget()
      local entity = target and target:GetEntity()
      if not entity or not ___MOD.isvalid(entity) then
        return
      end
      local targetMov = entity.MeramMovementComponent
      if not targetMov then
        return
      end
      local targetPos = targetMov.Position
      if not targetPos then
        return
      end

      state.searching = false
      state.mapWaitElapsed = 0
      -- 위치 추적은 이동 쓰로틀과 무관하게 매 프레임 갱신해둔다.
      state.lastPos = { x = targetPos.x, y = targetPos.y }
      state.lastMapId = targetMov.MapId

      -- 따라가기 대상 회복(F2와 연동): 피격 이벤트로 캐싱해둔 대상 HP를 읽는다.
      if state.healEnabled then
        targetHealCooldown = targetHealCooldown - (delta or 0)
        local targetCC = entity.MeramCreatureController
        local targetNetObjId = targetCC and targetCC.NetObjId
        local creatureHp = state.creatureHp
        local cached = targetNetObjId and creatureHp and creatureHp[targetNetObjId]
        if dbg and gameHud then
          gameHud:SystemMessage("[dbg heal] target hp=" .. tostring(cached and cached.hp) .. "/" .. tostring(cached and cached.maxHp))
        end
        if cached and cached.hp and cached.maxHp and cached.maxHp > 0
            and cached.hp / cached.maxHp <= TARGET_HEAL_THRESHOLD and targetHealCooldown <= 0 then
          targetHealCooldown = HEAL_COOLDOWN_SEC
          local spellInv = getSpellInventory(gameHud)
          if spellInv then
            spellInv:TryServerUseSpell(TARGET_HEAL_SLOT, targetNetObjId, 0, 0, 0, "")
            if gameHud then gameHud:SystemMessage("[자힐] 대상 회복 시전") end
          end
        end
      end

      local fallbackDir, dx, dy = directionTo(myPos, targetPos)
      if ___MOD.math.abs(dx) + ___MOD.math.abs(dy) <= KEEP_DISTANCE then
        return
      end

      elapsed = elapsed + (delta or 0)
      if elapsed < MOVE_INTERVAL then
        return
      end
      elapsed = elapsed - MOVE_INTERVAL

      -- 이동불가 타일을 완벽히 피하도록 BFS로 다음 칸을 정한다(범위 밖/실패 시 기존 방식으로 대체).
      local path = bfsPath(myMov.MapId, myPos, targetPos, PATHFIND_MAX_NODES)
      local direction = (path and path[1]) or fallbackDir
      local moved = moveToward(myMov, myPos, controller, direction)
      state.lastDir = moved or direction
    else
      -- 시야 이탈: 같은 맵이면 마지막 좌표로 가고, 도착하면 마지막 방향으로 더 가본다.
      if not state.lastPos then
        state.enabled = false
        if gameHud then
          gameHud:SystemMessage("[따라가기] 대상을 잃어 중지합니다.")
        end
        return
      end

      if state.lastMapId ~= myMov.MapId then
        -- 맵 전환 중엔 내 MapId가 아직 안 갱신됐을 수 있어서 바로 포기하지 않고
        -- MAP_MATCH_GRACE_SEC 동안 계속 재확인한다 (포탈 로딩 시간 고려).
        state.mapWaitElapsed = (state.mapWaitElapsed or 0) + (delta or 0)
        if state.mapWaitElapsed > MAP_MATCH_GRACE_SEC then
          state.enabled = false
          if gameHud then
            gameHud:SystemMessage("[따라가기] 다른 맵으로 이동한 것 같아 중지합니다.")
          end
        end
        return
      end
      state.mapWaitElapsed = 0

      if not state.searching then
        state.searching = true
        state.extrapolating = false
        state.searchSteps = 0
        if gameHud then
          gameHud:SystemMessage("[따라가기] 시야 이탈, 마지막 위치로 이동합니다.")
        end
      end

      elapsed = elapsed + (delta or 0)
      if elapsed < MOVE_INTERVAL then
        return
      end
      elapsed = elapsed - MOVE_INTERVAL

      if state.extrapolating then
        -- 마지막 좌표엔 이미 도착함: 거리 재계산 없이 마지막 방향으로 무조건 N칸 더 가본다
        -- (재계산하면 한 칸 나아간 순간 다시 "도착 전"으로 보여 제자리에서 왔다갔다하게 됨).
        state.searchSteps = state.searchSteps + 1
        if state.searchSteps > SEARCH_EXTRA_STEPS or not state.lastDir then
          state.enabled = false
          if gameHud then
            gameHud:SystemMessage("[따라가기] 대상을 찾지 못해 중지합니다.")
          end
          return
        end
        local moved = moveToward(myMov, myPos, controller, state.lastDir)
        if gameHud then
          gameHud:SystemMessage("[따라가기] 추적 " .. state.searchSteps .. "/" .. SEARCH_EXTRA_STEPS .. " dir=" .. tostring(state.lastDir) .. " moved=" .. tostring(moved))
        end
        return
      end

      local direction, dx, dy = directionTo(myPos, state.lastPos)
      if dx == 0 and dy == 0 then
        -- 마지막 좌표에 정확히 도착 (KEEP_DISTANCE와 무관하게 이 좌표 자체까지 간다)
        state.extrapolating = true  -- 다음 틱부터 위 분기로
      else
        moveToward(myMov, myPos, controller, direction)
      end
    end
  end)
end
